// SPDX-License-Identifier: GPL-2.0-only
/*
 * Warm resets on the Xiaomi Mi MIX 2 (PM8998 PON), so that RAM and with it
 * the ramoops logs survive a reset the kernel did not plan.
 *
 * The bootloader leaves the PS_HOLD reset (a reset the SoC asks for: kernel
 * panic, secure watchdog, ...) and the power key S2 reset (holding the key
 * for ~6.5 s) as hard resets, which lose RAM. Stock Android uses warm resets.
 * Planned reboots are unaffected: pm8941-pwrkey sets PS_HOLD to hard reset
 * (or shutdown) in its reboot notifier. The old types come back on unload.
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PON_KPDPWR_S2_CNTL	0x42
#define PON_KPDPWR_S2_CNTL2	0x43
#define PON_PS_HOLD_RST_CTL	0x5a
#define PON_PS_HOLD_RST_CTL2	0x5b
#define PON_ENABLE		BIT(7)
#define PON_TYPE_MASK		0x0f
#define PON_TYPE_WARM_RESET	1

static bool ps_hold_warm = true;
module_param(ps_hold_warm, bool, 0444);
MODULE_PARM_DESC(ps_hold_warm, "Make PS_HOLD (SoC requested) resets warm resets");

static bool kpdpwr_warm = true;
module_param(kpdpwr_warm, bool, 0444);
MODULE_PARM_DESC(kpdpwr_warm, "Make the power key long press reset a warm reset");

static struct platform_device *pon_pdev;
static struct regmap *map;
static u32 base;
static unsigned int old_ps_hold = PON_TYPE_MASK + 1;
static unsigned int old_kpdpwr = PON_TYPE_MASK + 1;

/* The type may only change while the reset is disabled */
static int set_type(const char *name, unsigned int type_reg,
		    unsigned int en_reg, unsigned int type, unsigned int *old)
{
	unsigned int t, en;
	int ret;

	ret = regmap_read(map, base + type_reg, &t);
	if (!ret)
		ret = regmap_read(map, base + en_reg, &en);
	if (ret)
		return ret;
	if (old)
		*old = t & PON_TYPE_MASK;

	ret = regmap_update_bits(map, base + en_reg, PON_ENABLE, 0);
	if (ret)
		return ret;
	usleep_range(100, 1000);	/* 3 sleep clock cycles */
	ret = regmap_update_bits(map, base + type_reg, PON_TYPE_MASK, type);
	if (!ret && (en & PON_ENABLE))
		ret = regmap_update_bits(map, base + en_reg, PON_ENABLE,
					 PON_ENABLE);
	if (ret)
		return ret;

	regmap_read(map, base + type_reg, &t);
	pr_info("mix2-pon: %s reset type %u -> %u%s\n", name,
		old ? *old : 0, t & PON_TYPE_MASK,
		en & PON_ENABLE ? "" : " (reset disabled)");
	return 0;
}

static int __init mix2_pon_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "qcom,pm8998-pon");
	if (!np)
		return -ENODEV;
	ret = of_property_read_u32(np, "reg", &base);
	pon_pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (ret)
		goto err;
	if (!pon_pdev)
		return -EPROBE_DEFER;

	map = dev_get_regmap(pon_pdev->dev.parent, NULL);
	if (!map) {
		ret = -ENODEV;
		goto err;
	}

	if (ps_hold_warm) {
		ret = set_type("PS_HOLD", PON_PS_HOLD_RST_CTL,
			       PON_PS_HOLD_RST_CTL2, PON_TYPE_WARM_RESET,
			       &old_ps_hold);
		if (ret)
			goto err;
	}
	if (kpdpwr_warm) {
		ret = set_type("power key", PON_KPDPWR_S2_CNTL,
			       PON_KPDPWR_S2_CNTL2, PON_TYPE_WARM_RESET,
			       &old_kpdpwr);
		if (ret)
			goto err;
	}
	return 0;

err:
	if (pon_pdev)
		put_device(&pon_pdev->dev);
	return ret;
}

static void __exit mix2_pon_exit(void)
{
	if (old_kpdpwr <= PON_TYPE_MASK)
		set_type("power key", PON_KPDPWR_S2_CNTL, PON_KPDPWR_S2_CNTL2,
			 old_kpdpwr, NULL);
	if (old_ps_hold <= PON_TYPE_MASK)
		set_type("PS_HOLD", PON_PS_HOLD_RST_CTL, PON_PS_HOLD_RST_CTL2,
			 old_ps_hold, NULL);
	put_device(&pon_pdev->dev);
}

module_init(mix2_pon_init);
module_exit(mix2_pon_exit);

MODULE_DESCRIPTION("Warm PS_HOLD and power key resets on the Xiaomi Mi MIX 2");
MODULE_LICENSE("GPL");
