/*
** =============================================================================
** Copyright (c) 2016  Texas Instruments Inc.
** Copyright (C) 2017 XiaoMi, Inc.
**
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; version 2.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** File:
**     tas2559-regmap.c
**
** Description:
**     I2C driver with regmap for Texas Instruments TAS2559 High Performance 4W Smart Amplifier
**
** =============================================================================
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include "tas2559.h"
#include "tas2559-core.h"
#include "tas2560.h"

#ifdef CONFIG_TAS2559_CODEC
#include "tas2559-codec.h"
#endif

#ifdef CONFIG_TAS2559_MISC
#include "tas2559-misc.h"
#endif

#ifdef ENABLE_TILOAD
#include "tiload.h"
#endif

/*
* tas2559_i2c_write_device : write single byte to device
* platform dependent, need platform specific support
*/
static struct regmap *tas2559_address_map(struct tas2559_priv *tas,
					 unsigned char addr)
{
	if (addr == tas->mnDevAAddr)
		return tas->mpRegmap;
	if (addr == tas->mnDevBAddr)
		return tas->companion_regmap;
	return NULL;
}

static int tas2559_i2c_write_device(struct tas2559_priv *pTAS2559,
				    unsigned char addr,
				    unsigned char reg,
				    unsigned char value)
{
	int nResult = 0;

	struct regmap *map = tas2559_address_map(pTAS2559, addr);

	if (!map)
		return -EINVAL;
	nResult = regmap_write(map, reg, value);

	if (nResult < 0)
		dev_err(pTAS2559->dev, "%s[0x%x] Error %d\n",
			__func__, addr, nResult);

	return nResult;
}

/*
* tas2559_i2c_bulkwrite_device : write multiple bytes to device
* platform dependent, need platform specific support
*/
static int tas2559_i2c_bulkwrite_device(struct tas2559_priv *pTAS2559,
					unsigned char addr,
					unsigned char reg,
					unsigned char *pBuf,
					unsigned int len)
{
	int nResult = 0;

	struct regmap *map = tas2559_address_map(pTAS2559, addr);

	if (!map)
		return -EINVAL;
	nResult = regmap_bulk_write(map, reg, pBuf, len);

	if (nResult < 0)
		dev_err(pTAS2559->dev, "%s[0x%x] Error %d\n",
			__func__, addr, nResult);

	return nResult;
}

/*
* tas2559_i2c_read_device : read single byte from device
* platform dependent, need platform specific support
*/
static int tas2559_i2c_read_device(struct tas2559_priv *pTAS2559,
				   unsigned char addr,
				   unsigned char reg,
				   unsigned char *p_value)
{
	int nResult = 0;
	unsigned int val = 0;

	struct regmap *map = tas2559_address_map(pTAS2559, addr);

	if (!map)
		return -EINVAL;
	nResult = regmap_read(map, reg, &val);

	if (nResult < 0)
		dev_err(pTAS2559->dev, "%s[0x%x] Error %d\n",
			__func__, addr, nResult);
	else
		*p_value = (unsigned char)val;

	return nResult;
}

/*
* tas2559_i2c_bulkread_device : read multiple bytes from device
* platform dependent, need platform specific support
*/
static int tas2559_i2c_bulkread_device(struct tas2559_priv *pTAS2559,
				       unsigned char addr,
				       unsigned char reg,
				       unsigned char *p_value,
				       unsigned int len)
{
	int nResult = 0;

	struct regmap *map = tas2559_address_map(pTAS2559, addr);

	if (!map)
		return -EINVAL;
	nResult = regmap_bulk_read(map, reg, p_value, len);

	if (nResult < 0)
		dev_err(pTAS2559->dev, "%s[0x%x] Error %d\n",
			__func__, addr, nResult);

	return nResult;
}

static int tas2559_i2c_update_bits(struct tas2559_priv *pTAS2559,
				   unsigned char addr,
				   unsigned char reg,
				   unsigned char mask,
				   unsigned char value)
{
	int nResult = 0;

	struct regmap *map = tas2559_address_map(pTAS2559, addr);

	if (!map)
		return -EINVAL;
	nResult = regmap_update_bits(map, reg, mask, value);

	if (nResult < 0)
		dev_err(pTAS2559->dev, "%s[0x%x] Error %d\n",
			__func__, addr, nResult);

	return nResult;
}

/*
* tas2559_change_book_page : switch to certain book and page
* platform independent, don't change unless necessary
*/
/* Called with dev_lock held. A failed selector write leaves state unknown. */
static int tas2559_select_device(struct tas2559_priv *tas, unsigned char addr,
		int *book, int *page, unsigned char next_book, unsigned char next_page)
{
	int ret;

	if (*book != next_book) {
		ret = tas2559_i2c_write_device(tas, addr, TAS2559_PAGECTL_REG, 0);
		if (ret < 0)
			goto error;
		*page = 0;
		ret = tas2559_i2c_write_device(tas, addr, TAS2559_BOOKCTL_REG, next_book);
		if (ret < 0)
			goto error;
		*book = next_book;
	}
	if (*page != next_page) {
		ret = tas2559_i2c_write_device(tas, addr, TAS2559_PAGECTL_REG, next_page);
		if (ret < 0)
			goto error;
		*page = next_page;
	}
	return 0;
error:
	*book = -1;
	*page = -1;
	return ret;
}

static int tas2559_change_book_page(struct tas2559_priv *tas, enum channel chn,
		unsigned char book, unsigned char page)
{
	int ret;

	if (!chn || (chn & ~DevBoth))
		return -EINVAL;
	if (chn & DevA) {
		ret = tas2559_select_device(tas, tas->mnDevAAddr,
			&tas->mnDevACurrentBook, &tas->mnDevACurrentPage, book, page);
		if (ret < 0)
			return ret;
	}
	if (chn & DevB) {
		ret = tas2559_select_device(tas, tas->mnDevBAddr,
			&tas->mnDevBCurrentBook, &tas->mnDevBCurrentPage, book, page);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/*
* tas2559_dev_read :
* platform independent, don't change unless necessary
*/
static int tas2559_dev_read(struct tas2559_priv *pTAS2559,
			    enum channel chn,
			    unsigned int nRegister,
			    unsigned int *pValue)
{
	int nResult = 0;
	unsigned char Value = 0;

	mutex_lock(&pTAS2559->dev_lock);

	if (pTAS2559->mbTILoadActive) {
		if (!(nRegister & 0x80000000)) {
			/* let only reads from TILoad pass. */
			goto end;
		}

		nRegister &= ~0x80000000;

		dev_dbg(pTAS2559->dev, "TiLoad R CH[%d] REG B[%d]P[%d]R[%d]\n",
			chn,
			TAS2559_BOOK_ID(nRegister),
			TAS2559_PAGE_ID(nRegister),
			TAS2559_PAGE_REG(nRegister));
	}

	nResult = tas2559_change_book_page(pTAS2559, chn,
					   TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister));

	if (nResult >= 0) {
		if (chn == DevA)
			nResult = tas2559_i2c_read_device(pTAS2559,
							  pTAS2559->mnDevAAddr, TAS2559_PAGE_REG(nRegister), &Value);
		else
			if (chn == DevB)
				nResult = tas2559_i2c_read_device(pTAS2559,
								  pTAS2559->mnDevBAddr, TAS2559_PAGE_REG(nRegister), &Value);
			else {
				dev_err(pTAS2559->dev, "%s£¬ read chn ERROR %d\n", __func__, chn);
				nResult = -EINVAL;
			}

		if (nResult >= 0)
			*pValue = Value;
	}

end:

	mutex_unlock(&pTAS2559->dev_lock);
	return nResult;
}

/*
* tas2559_dev_write :
* platform independent, don't change unless necessary
*/
static int tas2559_dev_write(struct tas2559_priv *pTAS2559,
			     enum channel chn,
			     unsigned int nRegister,
			     unsigned int nValue)
{
	int nResult = 0;

	mutex_lock(&pTAS2559->dev_lock);

	if ((nRegister == 0xAFFEAFFE) && (nValue == 0xBABEBABE)) {
		pTAS2559->mbTILoadActive = true;
		dev_dbg(pTAS2559->dev, "TiLoad Active\n");
		goto end;
	}

	if ((nRegister == 0xBABEBABE) && (nValue == 0xAFFEAFFE)) {
		pTAS2559->mbTILoadActive = false;
		dev_dbg(pTAS2559->dev, "TiLoad DeActive\n");
		goto end;
	}

	if (pTAS2559->mbTILoadActive) {
		if (!(nRegister & 0x80000000)) {
			/* let only writes from TILoad pass. */
			goto end;
		}

		nRegister &= ~0x80000000;
		dev_dbg(pTAS2559->dev, "TiLoad W CH[%d] REG B[%d]P[%d]R[%d] =0x%x\n",
			chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister),
			TAS2559_PAGE_REG(nRegister), nValue);
	}

	nResult = tas2559_change_book_page(pTAS2559,
					   chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister));

	if (nResult >= 0) {
		if (chn & DevA)
			nResult = tas2559_i2c_write_device(pTAS2559,
							   pTAS2559->mnDevAAddr, TAS2559_PAGE_REG(nRegister), nValue);

		if (nResult >= 0 && (chn & DevB))
			nResult = tas2559_i2c_write_device(pTAS2559,
							   pTAS2559->mnDevBAddr, TAS2559_PAGE_REG(nRegister), nValue);
	}

end:

	mutex_unlock(&pTAS2559->dev_lock);
	return nResult;
}

/*
* tas2559_dev_bulk_read :
* platform independent, don't change unless necessary
*/
static int tas2559_dev_bulk_read(struct tas2559_priv *pTAS2559,
				 enum channel chn,
				 unsigned int nRegister,
				 unsigned char *pData,
				 unsigned int nLength)
{
	int nResult = 0;
	unsigned char reg = 0;

	mutex_lock(&pTAS2559->dev_lock);

	if (pTAS2559->mbTILoadActive) {
		if (!(nRegister & 0x80000000)) {
			/* let only writes from TILoad pass. */
			goto end;
		}

		nRegister &= ~0x80000000;
		dev_dbg(pTAS2559->dev, "TiLoad BR CH[%d] REG B[%d]P[%d]R[%d], count=%d\n",
			chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister),
			TAS2559_PAGE_REG(nRegister), nLength);
	}

	nResult = tas2559_change_book_page(pTAS2559, chn,
					   TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister));

	if (nResult >= 0) {
		reg = TAS2559_PAGE_REG(nRegister);

		if (chn == DevA)
			nResult = tas2559_i2c_bulkread_device(pTAS2559,
							      pTAS2559->mnDevAAddr, reg, pData, nLength);
		else
			if (chn == DevB)
				nResult = tas2559_i2c_bulkread_device(pTAS2559,
								      pTAS2559->mnDevBAddr, reg, pData, nLength);
			else {
				dev_err(pTAS2559->dev, "%s, chn ERROR %d\n", __func__, chn);
				nResult = -EINVAL;
			}
	}

end:

	mutex_unlock(&pTAS2559->dev_lock);
	return nResult;
}

/*
* tas2559_dev_bulk_write :
* platform independent, don't change unless necessary
*/
static int tas2559_dev_bulk_write(struct tas2559_priv *pTAS2559,
				  enum channel chn,
				  unsigned int nRegister,
				  unsigned char *pData,
				  unsigned int nLength)
{
	int nResult = 0;
	unsigned char reg = 0;

	mutex_lock(&pTAS2559->dev_lock);

	if (pTAS2559->mbTILoadActive) {
		if (!(nRegister & 0x80000000)) {
			/* let only writes from TILoad pass. */
			goto end;
		}

		nRegister &= ~0x80000000;
		dev_dbg(pTAS2559->dev, "TiLoad BW CH[%d] REG B[%d]P[%d]R[%d], count=%d\n",
			chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister),
			TAS2559_PAGE_REG(nRegister), nLength);
	}

	nResult = tas2559_change_book_page(pTAS2559, chn,
					   TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister));

	if (nResult >= 0) {
		reg = TAS2559_PAGE_REG(nRegister);

		if (chn & DevA)
			nResult = tas2559_i2c_bulkwrite_device(pTAS2559,
							       pTAS2559->mnDevAAddr, reg, pData, nLength);

		if (nResult >= 0 && (chn & DevB))
			nResult = tas2559_i2c_bulkwrite_device(pTAS2559,
							       pTAS2559->mnDevBAddr, reg, pData, nLength);
	}

end:

	mutex_unlock(&pTAS2559->dev_lock);
	return nResult;
}

/*
* tas2559_dev_update_bits :
* platform independent, don't change unless necessary
*/
static int tas2559_dev_update_bits(
	struct tas2559_priv *pTAS2559,
	enum channel chn,
	unsigned int nRegister,
	unsigned int nMask,
	unsigned int nValue)
{
	int nResult = 0;

	mutex_lock(&pTAS2559->dev_lock);

	if (pTAS2559->mbTILoadActive) {
		if (!(nRegister & 0x80000000)) {
			/* let only writes from TILoad pass. */
			goto end;
		}

		nRegister &= ~0x80000000;
		dev_dbg(pTAS2559->dev, "TiLoad SB CH[%d] REG B[%d]P[%d]R[%d], mask=0x%x, value=0x%x\n",
			chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister),
			TAS2559_PAGE_REG(nRegister), nMask, nValue);
	}

	nResult = tas2559_change_book_page(pTAS2559,
					   chn, TAS2559_BOOK_ID(nRegister), TAS2559_PAGE_ID(nRegister));

	if (nResult >= 0) {
		if (chn & DevA)
			nResult = tas2559_i2c_update_bits(pTAS2559,
							  pTAS2559->mnDevAAddr, TAS2559_PAGE_REG(nRegister), nMask, nValue);

		if (nResult >= 0 && (chn & DevB))
			nResult = tas2559_i2c_update_bits(pTAS2559,
							  pTAS2559->mnDevBAddr, TAS2559_PAGE_REG(nRegister), nMask, nValue);
	}

end:

	mutex_unlock(&pTAS2559->dev_lock);
	return nResult;
}

static void tas2559_clearIRQ(struct tas2559_priv *pTAS2559)
{
	unsigned int nValue;
	int nResult = 0;

	nResult = pTAS2559->read(pTAS2559, DevA, TAS2559_FLAGS_1, &nValue);

	if (nResult >= 0)
		pTAS2559->read(pTAS2559, DevA, TAS2559_FLAGS_2, &nValue);

	nResult = pTAS2559->read(pTAS2559, DevB, TAS2560_FLAGS_1, &nValue);

	if (nResult >= 0)
		pTAS2559->read(pTAS2559, DevB, TAS2560_FLAGS_2, &nValue);
}

/* irq_lock serializes IRQ depth changes in process/thread context. */
static void tas2559_sync_irqs(struct tas2559_priv *tas, bool enable)
{
	bool a, b;

	enable = enable && !tas->mbRuntimeSuspend && !tas->removing;
	a = enable && tas->irq_a_requested &&
		((tas->irq_wanted & DevA) ||
		 (tas->mnDevAGPIOIRQ == tas->mnDevBGPIOIRQ &&
		  (tas->irq_wanted & DevB)));
	b = enable && tas->irq_b_requested && (tas->irq_wanted & DevB);

	if (a != tas->irq_a_enabled) {
		if (a)
			enable_irq(tas->mnDevAIRQ);
		else
			disable_irq_nosync(tas->mnDevAIRQ);
		tas->irq_a_enabled = a;
	}
	if (b != tas->irq_b_enabled) {
		if (b)
			enable_irq(tas->mnDevBIRQ);
		else
			disable_irq_nosync(tas->mnDevBIRQ);
		tas->irq_b_enabled = b;
	}
	tas->mbIRQEnable = a || b;
}

static void tas2559_enableIRQ(struct tas2559_priv *tas, enum channel chl, bool enable)
{
	mutex_lock(&tas->irq_lock);
	if (enable && !tas->removing)
		tas->irq_wanted |= chl;
	else
		tas->irq_wanted &= ~chl;
	tas2559_sync_irqs(tas, !tas->mbRuntimeSuspend && !tas->irq_processing);
	if (enable && tas->mbIRQEnable)
		schedule_delayed_work(&tas->irq_work, msecs_to_jiffies(10));
	mutex_unlock(&tas->irq_lock);
}

static void tas2559_hw_reset(struct tas2559_priv *pTAS2559)
{
	dev_dbg(pTAS2559->dev, "%s\n", __func__);

	if (gpio_is_valid(pTAS2559->mnDevAGPIORST)) {
		gpio_direction_output(pTAS2559->mnDevAGPIORST, 0);
		msleep(5);
		gpio_direction_output(pTAS2559->mnDevAGPIORST, 1);
		msleep(2);
	}

	if (gpio_is_valid(pTAS2559->mnDevBGPIORST)) {
		if (pTAS2559->mnDevAGPIORST != pTAS2559->mnDevBGPIORST) {
			gpio_direction_output(pTAS2559->mnDevBGPIORST, 0);
			msleep(5);
			gpio_direction_output(pTAS2559->mnDevBGPIORST, 1);
			msleep(2);
		}
	}

	pTAS2559->mnDevACurrentBook = -1;
	pTAS2559->mnDevACurrentPage = -1;
	pTAS2559->mnDevBCurrentBook = -1;
	pTAS2559->mnDevBCurrentPage = -1;

	if (pTAS2559->mnErrCode)
		dev_info(pTAS2559->dev, "%s, ErrCode=0x%x\n", __func__, pTAS2559->mnErrCode);

	pTAS2559->mnErrCode = 0;
}

static bool tas2559_irq_ready(struct tas2559_priv *tas)
{
	struct TFirmware *fw = tas->mpFirmware;

	return fw && fw->mpPrograms && fw->mpConfigurations &&
		tas->mnCurrentProgram < fw->mnPrograms &&
		tas->mnCurrentConfiguration < fw->mnConfigurations &&
		fw->mpConfigurations[tas->mnCurrentConfiguration].mnProgram ==
			tas->mnCurrentProgram &&
		fw->mpPrograms[tas->mnCurrentProgram].mnAppMode ==
			TAS2559_APP_TUNINGMODE;
}

static void irq_work_routine(struct work_struct *work)
{
	struct tas2559_priv *pTAS2559 =
		container_of(work, struct tas2559_priv, irq_work.work);
	struct TConfiguration *pConfiguration;
	unsigned int nDevLInt1Status = 0, nDevLInt2Status = 0;
	unsigned int nDevRInt1Status = 0, nDevRInt2Status = 0;
	int nCounter = 2;
	int nResult = 0;

#ifdef CONFIG_TAS2559_CODEC
	mutex_lock(&pTAS2559->codec_lock);
#endif

#ifdef CONFIG_TAS2559_MISC
	mutex_lock(&pTAS2559->file_lock);
#endif

	if (pTAS2559->mbRuntimeSuspend) {
		dev_info(pTAS2559->dev, "%s, Runtime Suspended\n", __func__);
		goto end;
	}

	if (!tas2559_irq_ready(pTAS2559)) {
		nResult = -EINVAL;
		goto end;
	}

	if (pTAS2559->mnErrCode & ERROR_FAILSAFE)
		goto program;

	if (!pTAS2559->mbPowerUp) {
		dev_info(pTAS2559->dev, "%s, device not powered\n", __func__);
		goto end;
	}

	if ((!pTAS2559->mpFirmware->mnConfigurations)
	    || (!pTAS2559->mpFirmware->mnPrograms)) {
		dev_info(pTAS2559->dev, "%s, firmware not loaded\n", __func__);
		goto end;
	}

	pConfiguration = &(pTAS2559->mpFirmware->mpConfigurations[pTAS2559->mnCurrentConfiguration]);

	if (pConfiguration->mnDevices & DevA) {
		nResult = tas2559_dev_read(pTAS2559, DevA, TAS2559_FLAGS_1, &nDevLInt1Status);

		if (nResult >= 0)
			nResult = tas2559_dev_read(pTAS2559, DevA, TAS2559_FLAGS_2, &nDevLInt2Status);
		if (nResult < 0)
			goto program;

		if (((nDevLInt1Status & 0xfc) != 0) || ((nDevLInt2Status & 0x0c) != 0)) {
			/* in case of INT_OC, INT_UV, INT_OT, INT_BO, INT_CL, INT_CLK1, INT_CLK2 */
			dev_dbg(pTAS2559->dev, "IRQ critical Error DevA: 0x%x, 0x%x\n",
				nDevLInt1Status, nDevLInt2Status);

			if (nDevLInt1Status & 0x80) {
				pTAS2559->mnErrCode |= ERROR_OVER_CURRENT;
				dev_err(pTAS2559->dev, "DEVA SPK over current!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_OVER_CURRENT;

			if (nDevLInt1Status & 0x40) {
				pTAS2559->mnErrCode |= ERROR_UNDER_VOLTAGE;
				dev_err(pTAS2559->dev, "DEVA SPK under voltage!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_UNDER_VOLTAGE;

			if (nDevLInt1Status & 0x20) {
				pTAS2559->mnErrCode |= ERROR_CLK_HALT;
				dev_err(pTAS2559->dev, "DEVA clk halted!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_HALT;

			if (nDevLInt1Status & 0x10) {
				pTAS2559->mnErrCode |= ERROR_DIE_OVERTEMP;
				dev_err(pTAS2559->dev, "DEVA die over temperature!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_DIE_OVERTEMP;

			if (nDevLInt1Status & 0x08) {
				pTAS2559->mnErrCode |= ERROR_BROWNOUT;
				dev_err(pTAS2559->dev, "DEVA brownout!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_BROWNOUT;

			if (nDevLInt1Status & 0x04) {
				pTAS2559->mnErrCode |= ERROR_CLK_LOST;
				dev_err(pTAS2559->dev, "DEVA clock lost!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_LOST;

			if (nDevLInt2Status & 0x08) {
				pTAS2559->mnErrCode |= ERROR_CLK_DET1;
				dev_err(pTAS2559->dev, "DEVA clk detection 1!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_DET1;

			if (nDevLInt2Status & 0x04) {
				pTAS2559->mnErrCode |= ERROR_CLK_DET2;
				dev_err(pTAS2559->dev, "DEVA clk detection 2!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_DET2;

			goto program;
		} else {
			dev_dbg(pTAS2559->dev, "IRQ status DevA: 0x%x, 0x%x\n",
				nDevLInt1Status, nDevLInt2Status);
			nCounter = 2;

			while (nCounter > 0) {
				nResult = tas2559_dev_read(pTAS2559, DevA, TAS2559_POWER_UP_FLAG_REG, &nDevLInt1Status);

				if (nResult < 0)
					goto program;

				if ((nDevLInt1Status & 0xc0) == 0xc0)
					break;

				nCounter--;

				if (nCounter > 0) {
					/* in case check pow status just after power on TAS2559 */
					dev_dbg(pTAS2559->dev, "PowSts A: 0x%x, check again after 10ms\n",
						nDevLInt1Status);
					msleep(10);
				}
			}

			if ((nDevLInt1Status & 0xc0) != 0xc0) {
				dev_err(pTAS2559->dev, "%s, Critical DevA ERROR B[%d]_P[%d]_R[%d]= 0x%x\n",
					__func__,
					TAS2559_BOOK_ID(TAS2559_POWER_UP_FLAG_REG),
					TAS2559_PAGE_ID(TAS2559_POWER_UP_FLAG_REG),
					TAS2559_PAGE_REG(TAS2559_POWER_UP_FLAG_REG),
					nDevLInt1Status);
				pTAS2559->mnErrCode |= ERROR_CLASSD_PWR;
				goto program;
			}

			pTAS2559->mnErrCode &= ~ERROR_CLASSD_PWR;
		}
	}

	if (pConfiguration->mnDevices & DevB) {
		nResult = tas2559_dev_read(pTAS2559, DevB, TAS2560_FLAGS_1, &nDevRInt1Status);

		if (nResult >= 0)
			nResult = tas2559_dev_read(pTAS2559, DevB, TAS2560_FLAGS_2, &nDevRInt2Status);
		if (nResult < 0)
			goto program;

		if (((nDevRInt1Status & 0xfc) != 0) || ((nDevRInt2Status & 0xc0) != 0)) {
			/* in case of INT_OC, INT_UV, INT_OT, INT_BO, INT_CL, INT_CLK1, INT_CLK2 */
			dev_dbg(pTAS2559->dev, "IRQ critical Error DevB: 0x%x, 0x%x\n",
				nDevRInt1Status, nDevRInt2Status);

			if (nDevRInt1Status & 0x80) {
				pTAS2559->mnErrCode |= ERROR_OVER_CURRENT;
				dev_err(pTAS2559->dev, "DEVB SPK over current!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_OVER_CURRENT;

			if (nDevRInt1Status & 0x40) {
				pTAS2559->mnErrCode |= ERROR_UNDER_VOLTAGE;
				dev_err(pTAS2559->dev, "DEVB SPK under voltage!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_UNDER_VOLTAGE;

			if (nDevRInt1Status & 0x20) {
				pTAS2559->mnErrCode |= ERROR_CLK_HALT;
				dev_err(pTAS2559->dev, "DEVB clk halted!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_HALT;

			if (nDevRInt1Status & 0x10) {
				pTAS2559->mnErrCode |= ERROR_DIE_OVERTEMP;
				dev_err(pTAS2559->dev, "DEVB die over temperature!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_DIE_OVERTEMP;

			if (nDevRInt1Status & 0x08) {
				pTAS2559->mnErrCode |= ERROR_BROWNOUT;
				dev_err(pTAS2559->dev, "DEVB brownout!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_BROWNOUT;

			if (nDevRInt1Status & 0x04) {
				pTAS2559->mnErrCode |= ERROR_CLK_LOST;
				dev_err(pTAS2559->dev, "DEVB clock lost!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_LOST;

			if (nDevRInt2Status & 0x80) {
				pTAS2559->mnErrCode |= ERROR_CLK_DET1;
				dev_err(pTAS2559->dev, "DEVB clk detection 1!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_DET1;

			if (nDevRInt2Status & 0x40) {
				pTAS2559->mnErrCode |= ERROR_CLK_DET2;
				dev_err(pTAS2559->dev, "DEVB clk detection 2!\n");
			} else
				pTAS2559->mnErrCode &= ~ERROR_CLK_DET2;

			goto program;
		} else {
			dev_dbg(pTAS2559->dev, "IRQ status DevB: 0x%x, 0x%x\n",
				nDevRInt1Status, nDevRInt2Status);
			nCounter = 2;

			while (nCounter > 0) {
				nResult = tas2559_dev_read(pTAS2559, DevB, TAS2560_POWER_UP_FLAG_REG, &nDevRInt1Status);

				if (nResult < 0)
					goto program;

				if ((nDevRInt1Status & 0xc0) == 0xc0)
					break;

				nCounter--;

				if (nCounter > 0) {
					/* in case check pow status just after power on TAS2560 */
					dev_dbg(pTAS2559->dev, "PowSts B: 0x%x, check again after 10ms\n",
						nDevRInt1Status);
					msleep(10);
				}
			}

			if ((nDevRInt1Status & 0xc0) != 0xc0) {
				dev_err(pTAS2559->dev, "%s, Critical DevB ERROR B[%d]_P[%d]_R[%d]= 0x%x\n",
					__func__,
					TAS2559_BOOK_ID(TAS2560_POWER_UP_FLAG_REG),
					TAS2559_PAGE_ID(TAS2560_POWER_UP_FLAG_REG),
					TAS2559_PAGE_REG(TAS2560_POWER_UP_FLAG_REG),
					nDevRInt1Status);
				pTAS2559->mnErrCode |= ERROR_CLASSD_PWR;
				goto program;
			}

			pTAS2559->mnErrCode &= ~ERROR_CLASSD_PWR;
		}
	}

	goto end;

program:
	/* hardware reset and reload */
	nResult = tas2559_set_program(pTAS2559, pTAS2559->mnCurrentProgram,
				      pTAS2559->mnCurrentConfiguration);

end:
	mutex_lock(&pTAS2559->irq_lock);
	pTAS2559->irq_processing = false;
	tas2559_sync_irqs(pTAS2559, nResult >= 0 &&
		!pTAS2559->mbRuntimeSuspend && pTAS2559->mbPowerUp &&
		tas2559_irq_ready(pTAS2559) &&
		!(pTAS2559->mnErrCode & ERROR_FAILSAFE));
	mutex_unlock(&pTAS2559->irq_lock);

#ifdef CONFIG_TAS2559_MISC
	mutex_unlock(&pTAS2559->file_lock);
#endif

#ifdef CONFIG_TAS2559_CODEC
	mutex_unlock(&pTAS2559->codec_lock);
#endif
}

/* IRQF_ONESHOT keeps the level source masked while this thread runs. */
static irqreturn_t tas2559_irq_handler(int irq, void *dev_id)
{
	struct tas2559_priv *tas = dev_id;

	mutex_lock(&tas->irq_lock);
	/* Preserve the requested channels while the worker handles the fault. */
	tas2559_sync_irqs(tas, false);
	if (!tas->mbRuntimeSuspend && !tas->removing) {
		tas->irq_processing = true;
		schedule_delayed_work(&tas->irq_work, msecs_to_jiffies(100));
	}
	mutex_unlock(&tas->irq_lock);
	return IRQ_HANDLED;
}

static void timer_work_routine(struct work_struct *work)
{
	struct tas2559_priv *pTAS2559 = container_of(work, struct tas2559_priv, mtimerwork);
	int nResult, nTemp, nActTemp;
	struct TProgram *pProgram;

#ifdef CONFIG_TAS2559_CODEC
	mutex_lock(&pTAS2559->codec_lock);
#endif

#ifdef CONFIG_TAS2559_MISC
	mutex_lock(&pTAS2559->file_lock);
#endif

	if (pTAS2559->mbRuntimeSuspend) {
		dev_info(pTAS2559->dev, "%s, Runtime Suspended\n", __func__);
		goto end;
	}

	if (!pTAS2559->mpFirmware->mnConfigurations) {
		dev_info(pTAS2559->dev, "%s, firmware not loaded\n", __func__);
		goto end;
	}

	pProgram = &(pTAS2559->mpFirmware->mpPrograms[pTAS2559->mnCurrentProgram]);

	if (!pTAS2559->mbPowerUp
	    || (pProgram->mnAppMode != TAS2559_APP_TUNINGMODE)) {
		dev_info(pTAS2559->dev, "%s, pass, Pow=%d, program=%s\n",
			 __func__, pTAS2559->mbPowerUp, pProgram->mpName);
		goto end;
	}

	nResult = tas2559_get_die_temperature(pTAS2559, &nTemp);

	if (nResult >= 0) {
		nActTemp = (int)(nTemp >> 23);
		dev_dbg(pTAS2559->dev, "Die=0x%x, degree=%d\n", nTemp, nActTemp);

		if (!pTAS2559->mnDieTvReadCounter)
			pTAS2559->temperature_sum = 0;

		pTAS2559->mnDieTvReadCounter++;
		pTAS2559->temperature_sum += nActTemp;

		if (!(pTAS2559->mnDieTvReadCounter % LOW_TEMPERATURE_COUNTER)) {
			pTAS2559->temperature_sum /= LOW_TEMPERATURE_COUNTER;
			dev_dbg(pTAS2559->dev, "check : avg=%d\n", pTAS2559->temperature_sum);

			if ((pTAS2559->temperature_sum & 0x80000000) != 0) {
				/* if Die temperature is below ZERO */
				if (pTAS2559->mnDevCurrentGain != LOW_TEMPERATURE_GAIN) {
					nResult = tas2559_set_DAC_gain(pTAS2559, DevBoth, LOW_TEMPERATURE_GAIN);

					if (nResult < 0)
						goto end;

					pTAS2559->mnDevCurrentGain = LOW_TEMPERATURE_GAIN;
					dev_dbg(pTAS2559->dev, "LOW Temp: set gain to %d\n", LOW_TEMPERATURE_GAIN);
				}
			} else if (pTAS2559->temperature_sum > 5) {
				/* if Die temperature is above 5 degree C */
				if (pTAS2559->mnDevCurrentGain != pTAS2559->mnDevGain) {
					nResult = tas2559_set_DAC_gain(pTAS2559, DevBoth, pTAS2559->mnDevGain);

				if (nResult < 0)
					goto end;

				pTAS2559->mnDevCurrentGain = pTAS2559->mnDevGain;
				dev_dbg(pTAS2559->dev, "LOW Temp: set gain to original\n");
				}
			}

			pTAS2559->temperature_sum = 0;
		}

		if (pTAS2559->mbPowerUp)
			hrtimer_start(&pTAS2559->mtimer,
				      ns_to_ktime((u64)LOW_TEMPERATURE_CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
	}

end:

#ifdef CONFIG_TAS2559_MISC
	mutex_unlock(&pTAS2559->file_lock);
#endif

#ifdef CONFIG_TAS2559_CODEC
	mutex_unlock(&pTAS2559->codec_lock);
#endif
}

static enum hrtimer_restart temperature_timer_func(struct hrtimer *timer)
{
	struct tas2559_priv *pTAS2559 = container_of(timer, struct tas2559_priv, mtimer);

	if (!READ_ONCE(pTAS2559->mbRuntimeSuspend) &&
	    READ_ONCE(pTAS2559->mbPowerUp)) {
		schedule_work(&pTAS2559->mtimerwork);

		if (!delayed_work_pending(&pTAS2559->irq_work))
			schedule_delayed_work(&pTAS2559->irq_work, msecs_to_jiffies(20));
	}

	return HRTIMER_NORESTART;
}

static int tas2559_runtime_suspend(struct tas2559_priv *tas)
{
	/* Lock order: codec_lock -> irq_lock. Drain with neither lock held. */
	mutex_lock(&tas->codec_lock);
	mutex_lock(&tas->irq_lock);
	WRITE_ONCE(tas->mbRuntimeSuspend, true);
	tas2559_sync_irqs(tas, false);
	mutex_unlock(&tas->irq_lock);
	mutex_unlock(&tas->codec_lock);
	if (tas->irq_a_requested)
		synchronize_irq(tas->mnDevAIRQ);
	if (tas->irq_b_requested)
		synchronize_irq(tas->mnDevBIRQ);
	hrtimer_cancel(&tas->mtimer);
	cancel_work_sync(&tas->mtimerwork);
	cancel_delayed_work_sync(&tas->irq_work);
	mutex_lock(&tas->irq_lock);
	tas->irq_processing = false;
	mutex_unlock(&tas->irq_lock);
	return 0;
}

/* Called with codec_lock held. Validate state before restarting producers. */
static int tas2559_runtime_resume(struct tas2559_priv *tas)
{
	struct TProgram *program;

	if (!tas->mpFirmware || !tas->mpFirmware->mpPrograms ||
	    !tas->mpFirmware->mpConfigurations ||
	    tas->mnCurrentProgram >= tas->mpFirmware->mnPrograms ||
	    tas->mnCurrentConfiguration >= tas->mpFirmware->mnConfigurations)
		return -EINVAL;
	program = &tas->mpFirmware->mpPrograms[tas->mnCurrentProgram];
	mutex_lock(&tas->irq_lock);
	if (tas->removing) {
		mutex_unlock(&tas->irq_lock);
		return -ENODEV;
	}
	WRITE_ONCE(tas->mbRuntimeSuspend, false);
	/* Re-read status before rearming a possibly asserted level IRQ. */
	tas->irq_processing = tas->mbPowerUp && tas->irq_wanted && tas2559_irq_ready(tas);
	if (tas->irq_processing)
		schedule_delayed_work(&tas->irq_work, 0);
	mutex_unlock(&tas->irq_lock);
	if (tas->mbPowerUp && program->mnAppMode == TAS2559_APP_TUNINGMODE) {
		tas->mnDieTvReadCounter = 0;
		hrtimer_start(&tas->mtimer,
			ns_to_ktime((u64)LOW_TEMPERATURE_CHECK_PERIOD * NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	}
	return 0;
}

static bool tas2559_volatile(struct device *pDev, unsigned int nRegister)
{
	return true;
}

static bool tas2559_writeable(struct device *pDev, unsigned int nRegister)
{
	return true;
}

static const struct regmap_config tas2559_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas2559_writeable,
	.volatile_reg = tas2559_volatile,
	.cache_type = REGCACHE_NONE,
	.max_register = 128,
};

/*
* tas2559_i2c_probe :
* platform dependent
* should implement hardware reset functionality
*/
static void tas2559_hold_reset_a(void *data)
{
	struct tas2559_priv *tas = data;

	gpio_direction_output(tas->mnDevAGPIORST, 0);
}

static void tas2559_hold_reset_b(void *data)
{
	struct tas2559_priv *tas = data;

	gpio_direction_output(tas->mnDevBGPIORST, 0);
}

static void tas2559_cleanup(void *data)
{
	struct tas2559_priv *tas = data;

	mutex_lock(&tas->irq_lock);
	tas->removing = true;
	tas->irq_wanted = 0;
	tas2559_sync_irqs(tas, false);
	mutex_unlock(&tas->irq_lock);
	tas2559_runtime_suspend(tas);
	if (tas->component_registered) {
		tas2559_deregister_codec(tas);
		tas->component_registered = false;
	}
	/* Stop IRQ producers before draining work; no codec_lock held here. */
	if (tas->irq_a_requested) {
		free_irq(tas->mnDevAIRQ, tas);
		tas->irq_a_requested = false;
	}
	if (tas->irq_b_requested) {
		free_irq(tas->mnDevBIRQ, tas);
		tas->irq_b_requested = false;
	}
	if (tas->irq_gpio_a_requested)
		gpio_free(tas->mnDevAGPIOIRQ);
	if (tas->irq_gpio_b_requested)
		gpio_free(tas->mnDevBGPIOIRQ);
	if (tas->reset_a_requested)
		gpio_direction_output(tas->mnDevAGPIORST, 0);
	if (tas->reset_b_requested)
		gpio_direction_output(tas->mnDevBGPIORST, 0);
	tas->mbPowerUp = false;
	tas2559_release_firmware(tas);
}

static bool controlled_playback;
module_param(controlled_playback, bool, 0400);
MODULE_PARM_DESC(controlled_playback,
	"Experimental 48k stereo playback (see gain, play_limit_ms, reopen)");

static bool diagnostic_program;
module_param(diagnostic_program, bool, 0400);
MODULE_PARM_DESC(diagnostic_program,
	"Opt-in Chiron programming diagnostic; no playback, reset low on exit");

static int tas2559_diagnostic_reset(struct tas2559_priv *tas, bool hold)
{
	/* Physical level, independent of DT active polarity. */
	return gpiod_direction_output_raw(tas->diagnostic_reset_gpio, hold ? 0 : 1);
}

static void tas2559_diagnostic_hold_reset(void *data)
{
	struct tas2559_priv *tas = data;
	int ret = tas2559_diagnostic_reset(tas, true);

	if (ret)
		dev_err(tas->dev, "cannot hold diagnostic reset low: %d\n", ret);
}

static int tas2559_diagnostic_probe(struct i2c_client *client)
{
	struct tas2559_priv *tas;
	int ret;

	if (!of_machine_is_compatible("xiaomi,chiron") || client->addr != 0x4c ||
	    !client->dev.of_node)
		return -ENODEV;
	tas = devm_kzalloc(&client->dev, sizeof(*tas), GFP_KERNEL);
	if (!tas)
		return -ENOMEM;
	tas->dev = &client->dev;
	tas->client = client;
	i2c_set_clientdata(client, tas);
	tas->mnDevAAddr = 0x4c;
	tas->mnDevBAddr = 0x4d;
	tas->mbRuntimeSuspend = true;
	tas->mbYCRCEnable = true;
	mutex_init(&tas->dev_lock);
	mutex_init(&tas->codec_lock);
	mutex_init(&tas->irq_lock);
	/* ASIS avoids an unintended logical assertion before setting raw low. */
	tas->diagnostic_reset_gpio = devm_gpiod_get(&client->dev, "reset", GPIOD_ASIS);
	if (IS_ERR(tas->diagnostic_reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(tas->diagnostic_reset_gpio),
			"diagnostic reset GPIO required\n");
	tas->diagnostic_reset = tas2559_diagnostic_reset;
	ret = devm_add_action_or_reset(&client->dev, tas2559_diagnostic_hold_reset, tas);
	if (ret)
		return ret;
	ret = tas2559_diagnostic_reset(tas, true);
	if (ret)
		return ret;
	tas->mpRegmap = devm_regmap_init_i2c(client, &tas2559_i2c_regmap);
	if (IS_ERR(tas->mpRegmap))
		return PTR_ERR(tas->mpRegmap);
	tas->companion = devm_i2c_new_dummy_device(&client->dev, client->adapter, 0x4d);
	if (IS_ERR(tas->companion))
		return PTR_ERR(tas->companion);
	tas->companion_regmap = devm_regmap_init_i2c(tas->companion, &tas2559_i2c_regmap);
	if (IS_ERR(tas->companion_regmap))
		return PTR_ERR(tas->companion_regmap);
	tas->read = tas2559_dev_read;
	tas->write = tas2559_dev_write;
	tas->bulk_read = tas2559_dev_bulk_read;
	tas->bulk_write = tas2559_dev_bulk_write;
	tas->update_bits = tas2559_dev_update_bits;
	if (controlled_playback)
		return tas2559_controlled_register(tas);
	/* Deliberately no component, IRQ, worker, timer or legacy power callbacks. */
	ret = tas2559_diagnostic_firmware(tas);
	if (ret)
		return dev_err_probe(&client->dev, ret, "programming diagnostic failed\n");
	dev_info(&client->dev,
		"programming diagnostic passed; reset held low; playback unavailable\n");
	return 0;
}

static int tas2559_i2c_probe(struct i2c_client *pClient)
{
	struct tas2559_priv *pTAS2559;
	int nResult;
	unsigned int nValue = 0;
	const char *fw_name;

	if (diagnostic_program && controlled_playback)
		return -EINVAL;
	if (diagnostic_program || controlled_playback)
		return tas2559_diagnostic_probe(pClient);

	return dev_err_probe(&pClient->dev, -EOPNOTSUPP,
		"offline port: hardware enable blocked pending safety validation\n");

	dev_info(&pClient->dev, "%s enter\n", __func__);

	pTAS2559 = devm_kzalloc(&pClient->dev, sizeof(struct tas2559_priv), GFP_KERNEL);

	if (!pTAS2559) {
		dev_err(&pClient->dev, " -ENOMEM\n");
		nResult = -ENOMEM;
		goto err;
	}

	pTAS2559->client = pClient;
	pTAS2559->dev = &pClient->dev;
	i2c_set_clientdata(pClient, pTAS2559);
	dev_set_drvdata(&pClient->dev, pTAS2559);

	/* Initialize callback state before requesting any IRQ or publishing ASoC. */
	mutex_init(&pTAS2559->dev_lock);
	mutex_init(&pTAS2559->irq_lock);
#ifdef CONFIG_TAS2559_CODEC
	mutex_init(&pTAS2559->codec_lock);
#endif
	INIT_DELAYED_WORK(&pTAS2559->irq_work, irq_work_routine);
	hrtimer_setup(&pTAS2559->mtimer, temperature_timer_func,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	INIT_WORK(&pTAS2559->mtimerwork, timer_work_routine);

	pTAS2559->mpRegmap = devm_regmap_init_i2c(pClient, &tas2559_i2c_regmap);

	if (IS_ERR(pTAS2559->mpRegmap)) {
		nResult = PTR_ERR(pTAS2559->mpRegmap);
		dev_err(&pClient->dev, "Failed to allocate register map: %d\n",
			nResult);
		goto err;
	}

	if (!pClient->dev.of_node)
		return -EINVAL;
	nResult = tas2559_parse_dt(&pClient->dev, pTAS2559);
	if (nResult)
		return nResult;
	if (pTAS2559->mnDevAAddr != pClient->addr ||
	    pTAS2559->mnDevBAddr < 0x08 || pTAS2559->mnDevBAddr > 0x77 ||
	    pTAS2559->mnDevBAddr == pClient->addr)
		return -EINVAL;
	pTAS2559->companion = devm_i2c_new_dummy_device(&pClient->dev,
		pClient->adapter, pTAS2559->mnDevBAddr);
	if (IS_ERR(pTAS2559->companion))
		return PTR_ERR(pTAS2559->companion);
	pTAS2559->companion_regmap = devm_regmap_init_i2c(pTAS2559->companion,
		&tas2559_i2c_regmap);
	if (IS_ERR(pTAS2559->companion_regmap))
		return PTR_ERR(pTAS2559->companion_regmap);

	if (!gpio_is_valid(pTAS2559->mnDevAGPIORST))
		return dev_err_probe(&pClient->dev, pTAS2559->mnDevAGPIORST,
			"reset GPIO required\n");
	nResult = devm_gpio_request_one(&pClient->dev, pTAS2559->mnDevAGPIORST,
		GPIOF_OUT_INIT_LOW, "TAS2559-RESET");
	if (nResult)
		return nResult;
	pTAS2559->reset_a_requested = true;
	nResult = devm_add_action_or_reset(&pClient->dev, tas2559_hold_reset_a, pTAS2559);
	if (nResult)
		return nResult;
	if (gpio_is_valid(pTAS2559->mnDevBGPIORST) &&
	    pTAS2559->mnDevBGPIORST != pTAS2559->mnDevAGPIORST) {
		nResult = devm_gpio_request_one(&pClient->dev, pTAS2559->mnDevBGPIORST,
			GPIOF_OUT_INIT_LOW, "TAS2560-RESET");
		if (nResult)
			return nResult;
		pTAS2559->reset_b_requested = true;
		nResult = devm_add_action_or_reset(&pClient->dev, tas2559_hold_reset_b, pTAS2559);
		if (nResult)
			return nResult;
	}
	pTAS2559->mpFirmware = devm_kzalloc(&pClient->dev,
		sizeof(struct TFirmware), GFP_KERNEL);
	pTAS2559->mpCalFirmware = devm_kzalloc(&pClient->dev,
		sizeof(struct TFirmware), GFP_KERNEL);
	if (!pTAS2559->mpFirmware || !pTAS2559->mpCalFirmware)
		return -ENOMEM;
	nResult = devm_add_action_or_reset(&pClient->dev, tas2559_cleanup, pTAS2559);
	if (nResult)
		return nResult;
	tas2559_hw_reset(pTAS2559);

	pTAS2559->read = tas2559_dev_read;
	pTAS2559->write = tas2559_dev_write;
	pTAS2559->bulk_read = tas2559_dev_bulk_read;
	pTAS2559->bulk_write = tas2559_dev_bulk_write;
	pTAS2559->update_bits = tas2559_dev_update_bits;
	pTAS2559->enableIRQ = tas2559_enableIRQ;
	pTAS2559->clearIRQ = tas2559_clearIRQ;
	pTAS2559->set_config = tas2559_set_config;
	pTAS2559->set_calibration = tas2559_set_calibration;
	pTAS2559->hw_reset = tas2559_hw_reset;
	pTAS2559->runtime_suspend = tas2559_runtime_suspend;
	pTAS2559->runtime_resume = tas2559_runtime_resume;
	pTAS2559->mnRestart = 0;


	/* Reset the chip */
	nResult = tas2559_dev_write(pTAS2559, DevBoth, TAS2559_SW_RESET_REG, 1);
	if (nResult < 0) {
		dev_err(&pClient->dev, "I2c fail, %d\n", nResult);
		goto err;
	}
	msleep(1);
	nResult = pTAS2559->read(pTAS2559, DevA, TAS2559_REV_PGID_REG, &nValue);
	if (nResult < 0)
		goto err;
	pTAS2559->mnDevAPGID = nValue;
	dev_info(&pClient->dev, "TAS2559 PGID=0x%x\n", nValue);
	nResult = pTAS2559->read(pTAS2559, DevB, TAS2560_ID_REG, &nValue);
	if (nResult < 0)
		goto err;
	pTAS2559->mnDevBPGID = nValue;
	dev_info(pTAS2559->dev, "TAS2560 PGID=0x%02x\n", nValue);

	if (gpio_is_valid(pTAS2559->mnDevAGPIOIRQ)) {
		nResult = gpio_request(pTAS2559->mnDevAGPIOIRQ, "TAS2559-IRQ");

		if (nResult < 0) {
			dev_err(pTAS2559->dev,
				"%s: GPIO %d request INT error\n",
				__func__, pTAS2559->mnDevAGPIOIRQ);
			goto err;
		}

		pTAS2559->irq_gpio_a_requested = true;
		nResult = gpio_direction_input(pTAS2559->mnDevAGPIOIRQ);
		if (nResult)
			goto err;
		pTAS2559->mnDevAIRQ = gpio_to_irq(pTAS2559->mnDevAGPIOIRQ);
		if (pTAS2559->mnDevAIRQ < 0)
			return dev_err_probe(&pClient->dev, pTAS2559->mnDevAIRQ,
				"IRQ mapping failed\n");
		dev_dbg(pTAS2559->dev, "irq = %d\n", pTAS2559->mnDevAIRQ);
		nResult = request_threaded_irq(pTAS2559->mnDevAIRQ, NULL,
					       tas2559_irq_handler, IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_AUTOEN,
					       pClient->name, pTAS2559);

		if (nResult < 0) {
			dev_err(pTAS2559->dev,
				"request_irq failed, %d\n", nResult);
			goto err;
		}

		pTAS2559->irq_a_requested = true;
	}

	if (gpio_is_valid(pTAS2559->mnDevBGPIOIRQ)) {
		if (pTAS2559->mnDevAGPIOIRQ != pTAS2559->mnDevBGPIOIRQ) {
			nResult = gpio_request(pTAS2559->mnDevBGPIOIRQ, "TAS2560-IRQ");

			if (nResult < 0) {
				dev_err(pTAS2559->dev,
					"%s: GPIO %d request INT error\n",
					__func__, pTAS2559->mnDevBGPIOIRQ);
				goto err;
			}

			pTAS2559->irq_gpio_b_requested = true;
			nResult = gpio_direction_input(pTAS2559->mnDevBGPIOIRQ);
			if (nResult)
				goto err;
			pTAS2559->mnDevBIRQ = gpio_to_irq(pTAS2559->mnDevBGPIOIRQ);
			if (pTAS2559->mnDevBIRQ < 0)
				return dev_err_probe(&pClient->dev, pTAS2559->mnDevBIRQ,
					"IRQ mapping failed\n");
			if (pTAS2559->irq_a_requested &&
			    pTAS2559->mnDevBIRQ == pTAS2559->mnDevAIRQ)
				return dev_err_probe(&pClient->dev, -EINVAL,
					"distinct GPIOs alias the same IRQ\n");
			dev_dbg(pTAS2559->dev, "irq = %d\n", pTAS2559->mnDevBIRQ);
			nResult = request_threaded_irq(pTAS2559->mnDevBIRQ, NULL,
						       tas2559_irq_handler, IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_AUTOEN,
						       pClient->name, pTAS2559);

			if (nResult < 0) {
				dev_err(pTAS2559->dev,
					"request_irq failed, %d\n", nResult);
				goto err;
			}

			pTAS2559->irq_b_requested = true;
		} else
			pTAS2559->mnDevBIRQ = pTAS2559->mnDevAIRQ;
	}



#ifdef CONFIG_TAS2559_CODEC
	nResult = tas2559_register_codec(pTAS2559);
	if (nResult)
		goto err;
	pTAS2559->component_registered = true;
#endif

#ifdef CONFIG_TAS2559_MISC
	mutex_init(&pTAS2559->file_lock);
	tas2559_register_misc(pTAS2559);
#endif

#ifdef ENABLE_TILOAD
	tiload_driver_init(pTAS2559);
#endif


	fw_name = TAS2559_FW_NAME;
	of_property_read_string(pClient->dev.of_node, "firmware-name", &fw_name);

	dev_err(&pClient->dev, "Firmware file name is %s\n", fw_name);
	nResult = tas2559_load_firmware(pTAS2559, fw_name);

err:

	return nResult;
}

static const struct i2c_device_id tas2559_i2c_id[] = {
	{"tas2559", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, tas2559_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2559_of_match[] = {
	{.compatible = "ti,tas2559"},
	{},
};

MODULE_DEVICE_TABLE(of, tas2559_of_match);
#endif

static void tas2559_i2c_shutdown(struct i2c_client *client)
{
	struct tas2559_priv *tas = i2c_get_clientdata(client);

	if (!tas)
		return;
	if (tas->playback)
		tas2559_controlled_shutdown(tas);
	else if (tas->diagnostic_reset)
		tas2559_diagnostic_hold_reset(tas);
}

static struct i2c_driver tas2559_i2c_driver = {
	.driver = {
		.name = "tas2559",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(tas2559_of_match),
#endif
	},
	.probe = tas2559_i2c_probe,
	.shutdown = tas2559_i2c_shutdown,
	.id_table = tas2559_i2c_id,
};

module_i2c_driver(tas2559_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2559 Stereo I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
