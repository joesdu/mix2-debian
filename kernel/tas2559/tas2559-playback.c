// SPDX-License-Identifier: GPL-2.0-only
/* One-shot laboratory playback, separate from the legacy mixer/recovery path. */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "tas2559-core.h"
#include "tas2560.h"

#define TAS_POLL_MS 20

/* Defaults keep the laboratory limits: one open, 5 s after power-up. */
static unsigned int play_limit_ms = 5000;
module_param(play_limit_ms, uint, 0444);
MODULE_PARM_DESC(play_limit_ms, "stop the amplifier this long after power-up (0: no limit; fault polling stays)");
static bool reopen;
module_param(reopen, bool, 0444);
MODULE_PARM_DESC(reopen, "allow further opens/prepares after a stop (each re-programs and re-checks)");
/* Stock runs at 15 (6 below 0 degC); 0 is the quietest step. */
static unsigned int gain;
module_param(gain, uint, 0444);
MODULE_PARM_DESC(gain, "speaker DAC gain code for both amplifiers, 0..15 (stock: 15)");
static unsigned int earpiece_gain = 11;
module_param(earpiece_gain, uint, 0444);
MODULE_PARM_DESC(earpiece_gain, "earpiece (TAS2559 only) DAC gain code, 0..15 (stock: 11)");

/* Earpiece: firmware configuration 3 drives only DevA (TAS2559). */
static bool tas_play_earpiece(struct tas2559_priv *t)
{
	return t->play_config == 3;
}

enum tas_play_state { TAS_IDLE, TAS_OPEN, TAS_READY, TAS_RUNNING, TAS_DONE };
struct tas2559_playback {
	struct tas2559_priv *tas;
	struct mutex lock;
	struct delayed_work watchdog;
	enum tas_play_state state;
	unsigned long deadline;
	bool configured;
	bool registered;
	bool removing;
};

/* Never short-circuit shutdown on I/O errors: RESET_N is the final fallback. */
static int tas_play_stop(struct tas2559_playback *p)
{
	struct tas2559_priv *t = p->tas;
	int ret = 0, err;

	if (p->state == TAS_READY || p->state == TAS_RUNNING) {
		err = t->write(t, DevA, TAS2559_MUTE_REG, 3);
		if (err && !ret) ret = err;
		err = t->write(t, DevB, TAS2560_MUTE_REG, 1);
		if (err && !ret) ret = err;
		err = t->write(t, DevA, TAS2559_POWER_CTRL2_REG, 0);
		if (err && !ret) ret = err;
		err = t->write(t, DevA, TAS2559_POWER_CTRL1_REG, 0);
		if (err && !ret) ret = err;
	}
	err = t->diagnostic_reset(t, true);
	if (err) {
		dev_err(t->dev, "playback reset failed: %d; hardware state unknown\n", err);
		ret = err;
	}
	p->state = TAS_DONE;
	t->mbPowerUp = false;
	t->mbCalibrationLoaded = false;
	t->mnDevACurrentBook = t->mnDevACurrentPage = -1;
	t->mnDevBCurrentBook = t->mnDevBCurrentPage = -1;
	return ret;
}

static int tas_play_verify(struct tas2559_priv *t, enum channel ch,
		unsigned int reg, unsigned int mask, unsigned int expected)
{
	unsigned int v;
	int ret = t->read(t, ch, reg, &v);

	return ret < 0 ? ret : ((v & mask) == expected ? 0 : -EIO);
}

static int tas_play_status(struct tas2559_priv *t)
{
	int ret;

	ret = tas_play_verify(t, DevA, TAS2559_FLAGS_1, 0xfc, 0);
	if (ret) return ret;
	ret = tas_play_verify(t, DevA, TAS2559_FLAGS_2, 0x0c, 0);
	if (ret) return ret;
	ret = tas_play_verify(t, DevA, TAS2559_POWER_UP_FLAG_REG, 0xc0, 0xc0);
	if (ret || tas_play_earpiece(t)) return ret;
	ret = tas_play_verify(t, DevB, TAS2560_FLAGS_1, 0xfc, 0);
	if (ret) return ret;
	ret = tas_play_verify(t, DevB, TAS2560_FLAGS_2, 0xc0, 0);
	if (ret) return ret;
	return tas_play_verify(t, DevB, TAS2560_POWER_UP_FLAG_REG, 0xc0, 0xc0);
}

static int tas_play_write(struct tas2559_priv *t, enum channel ch,
		unsigned int reg, unsigned int value)
{
	int ret = t->write(t, ch, reg, value);

	if (ret) return ret;
	return tas_play_verify(t, ch, reg, 0xff, value);
}

/* Called only after checked programming, with CPU clocks already configured.
 * No failsafe/retry path. The gain code is verified, it is not an SPL limit.
 */
static int tas_play_power(struct tas2559_playback *p)
{
	struct tas2559_priv *t = p->tas;
	bool ear = tas_play_earpiece(t);
	unsigned int g = min(ear ? earpiece_gain : gain, 15U);
	int ret;

	ret = t->update_bits(t, DevA, TAS2559_SPK_CTRL_REG, 0x78, g << 3);
	if (ret) return ret;
	ret = tas_play_verify(t, DevA, TAS2559_SPK_CTRL_REG, 0x78, g << 3);
	if (ret) return ret;
	ret = t->update_bits(t, DevA, TAS2559_ASI1_DAC_FORMAT_REG, 0xf8, 0);
	if (ret) return ret;
	ret = tas_play_verify(t, DevA, TAS2559_ASI1_DAC_FORMAT_REG, 0xf8, 0);
	if (ret) return ret;
	if (!ear) {
		ret = t->update_bits(t, DevB, TAS2560_SPK_CTRL_REG, 0x0f, g);
		if (ret) return ret;
		ret = tas_play_verify(t, DevB, TAS2560_SPK_CTRL_REG, 0x0f, g);
		if (ret) return ret;
		ret = t->update_bits(t, DevB, TAS2560_DAI_FMT, 0x03, 0);
		if (ret) return ret;
		ret = tas_play_verify(t, DevB, TAS2560_DAI_FMT, 0x03, 0);
		if (ret) return ret;
	}
	ret = tas_play_write(t, DevA, TAS2559_GPIO1_PIN_REG, 1);
	if (ret) return ret;
	ret = tas_play_write(t, DevA, TAS2559_GPIO2_PIN_REG, 1);
	if (ret) return ret;
	ret = tas_play_write(t, DevA, TAS2559_POWER_CTRL2_REG, 0xa0);
	if (ret) return ret;
	ret = tas_play_write(t, DevA, TAS2559_POWER_CTRL2_REG, 0xa3);
	if (ret) return ret;
	ret = tas_play_write(t, DevA, TAS2559_POWER_CTRL1_REG, 0xf8);
	if (ret) return ret;
	msleep(2);
	if (!ear) {
		ret = tas_play_write(t, DevB, TAS2560_DEV_MODE_REG, 2);
		if (ret) return ret;
		ret = tas_play_write(t, DevB, TAS2560_MUTE_REG, 0x41);
		if (ret) return ret;
	}
	msleep(2);
	ret = tas_play_write(t, DevA, TAS2559_CLK_ERR_CTRL, 0x2b);
	if (ret) return ret;
	if (!ear) {
		ret = tas_play_write(t, DevB, TAS2560_CLK_ERR_CTRL, 0x0b);
		if (ret) return ret;
		ret = tas2559_playback_post(t, true);
		if (ret) return ret;
	}
	return tas_play_status(t);
}

static void tas_play_watchdog(struct work_struct *work)
{
	struct tas2559_playback *p = container_of(to_delayed_work(work),
		struct tas2559_playback, watchdog);

	mutex_lock(&p->lock);
	if (p->state == TAS_RUNNING) {
		int status = 0;

		if (p->removing ||
		    (play_limit_ms && time_after_eq(jiffies, p->deadline)) ||
		    (status = tas_play_status(p->tas))) {
			if (!p->removing)
				dev_warn(p->tas->dev, "playback stopped: %s (%d)\n",
					 status ? "fault/status check failed" : "time limit",
					 status);
			tas_play_stop(p);
		} else {
			schedule_delayed_work(&p->watchdog, msecs_to_jiffies(TAS_POLL_MS));
		}
	}
	mutex_unlock(&p->lock);
}

static struct tas2559_playback *tas_play_data(struct snd_soc_dai *dai)
{
	struct tas2559_priv *t = snd_soc_component_get_drvdata(dai->component);

	return t->playback;
}

static int tas_play_startup(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct tas2559_playback *p = tas_play_data(dai);
	int ret = 0;

	mutex_lock(&p->lock);
	if (p->removing || s->stream != SNDRV_PCM_STREAM_PLAYBACK ||
	    !(p->state == TAS_IDLE || (reopen && p->state == TAS_DONE))) {
		ret = -EPERM;
	} else {
		p->state = TAS_OPEN;
		p->configured = false;
	}
	mutex_unlock(&p->lock);
	return ret;
}

static int tas_play_params(struct snd_pcm_substream *s,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct tas2559_playback *p = tas_play_data(dai);
	int ret = 0;

	mutex_lock(&p->lock);
	if (reopen && !p->removing && p->state == TAS_DONE)
		p->state = TAS_OPEN;
	if (p->removing || p->state != TAS_OPEN || params_rate(params) != 48000 ||
	    params_channels(params) != 2 || params_format(params) != SNDRV_PCM_FORMAT_S16_LE)
		ret = -EINVAL;
	p->configured = !ret;
	mutex_unlock(&p->lock);
	return ret;
}

static int tas_play_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return fmt == (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBC_CFC) ? 0 : -EINVAL;
}

static int tas_play_prepare(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct tas2559_playback *p = tas_play_data(dai);
	int ret;

	mutex_lock(&p->lock);
	/* reopen: a re-prepare (e.g. after an underrun) starts from scratch. */
	if (reopen && !p->removing && p->configured &&
	    (p->state == TAS_READY || p->state == TAS_RUNNING || p->state == TAS_DONE)) {
		tas_play_stop(p);
		p->state = TAS_OPEN;
	}
	if (p->removing || p->state != TAS_OPEN || !p->configured) {
		ret = -EPERM;
		goto out;
	}
	ret = tas2559_playback_program(p->tas);
	if (ret)
		tas_play_stop(p);
	else
		p->state = TAS_READY;
out:
	mutex_unlock(&p->lock);
	return ret;
}

static int tas_play_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct tas2559_playback *p = tas_play_data(dai);
	struct tas2559_priv *t = p->tas;
	int ret = 0;

	mutex_lock(&p->lock);
	if (mute) {
		if (p->state == TAS_RUNNING || p->state == TAS_READY)
			ret = tas_play_stop(p);
		goto out;
	}
	if (p->removing || p->state != TAS_READY || !t->mbCalibrationLoaded) {
		ret = -EPERM;
		goto out;
	}
	p->deadline = jiffies + msecs_to_jiffies(play_limit_ms ? play_limit_ms : 5000);
	ret = tas_play_power(p);
	if (ret) goto stop;
	if (play_limit_ms && time_after_eq(jiffies, p->deadline)) {
		ret = -ETIMEDOUT;
		goto stop;
	}
	ret = tas_play_write(t, DevA, TAS2559_MUTE_REG, 0);
	if (ret) goto stop;
	if (!tas_play_earpiece(t)) {
		ret = tas_play_write(t, DevB, TAS2560_MUTE_REG, 0x40);
		if (ret) goto stop;
	}
	ret = tas_play_write(t, DevA, TAS2559_SOFT_MUTE_REG, 0);
	if (ret) goto stop;
	p->state = TAS_RUNNING;
	t->mbPowerUp = true;
	schedule_delayed_work(&p->watchdog, msecs_to_jiffies(TAS_POLL_MS));
	goto out;
stop:
	tas_play_stop(p);
out:
	mutex_unlock(&p->lock);
	return ret;
}

static int tas_play_free(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct tas2559_playback *p = tas_play_data(dai);
	int ret;

	mutex_lock(&p->lock);
	ret = tas_play_stop(p);
	mutex_unlock(&p->lock);
	cancel_delayed_work_sync(&p->watchdog);
	return ret;
}

static void tas_play_shutdown(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	tas_play_free(s, dai);
}

static int tas_play_suspend(struct snd_soc_component *component)
{
	struct tas2559_priv *t = snd_soc_component_get_drvdata(component);
	struct tas2559_playback *p = t->playback;
	int ret = 0;

	mutex_lock(&p->lock);
	/* No programmed state exists before prepare; preserve the unused open. */
	if (p->state == TAS_READY || p->state == TAS_RUNNING)
		ret = tas_play_stop(p);
	mutex_unlock(&p->lock);
	cancel_delayed_work_sync(&p->watchdog);
	return ret;
}

static const struct snd_soc_dai_ops tas_play_ops = {
	.startup = tas_play_startup, .hw_params = tas_play_params,
	.set_fmt = tas_play_fmt, .prepare = tas_play_prepare,
	.mute_stream = tas_play_mute, .no_capture_mute = 1,
	.hw_free = tas_play_free, .shutdown = tas_play_shutdown,
};
static struct snd_soc_dai_driver tas_play_dai = {
	.name = "tas2559 ASI1",
	.playback = {
		.stream_name = "ASI1 Playback", .channels_min = 2, .channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000, .formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tas_play_ops,
};
static const struct snd_soc_dapm_widget tas_play_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUT"),
};
static const struct snd_soc_dapm_route tas_play_routes[] = {
	{"OUT", NULL, "ASI1"},
};
/* Speaker: configuration 4, both amplifiers. Earpiece: configuration 3,
 * TAS2559 only (stock "handset"). Takes effect at the next prepare.
 */
static const char * const tas_play_outputs[] = { "Speaker", "Earpiece" };
static SOC_ENUM_SINGLE_EXT_DECL(tas_play_output_enum, tas_play_outputs);

static int tas_play_output_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct tas2559_priv *t = snd_soc_component_get_drvdata(c);

	ucontrol->value.enumerated.item[0] = tas_play_earpiece(t);
	return 0;
}

static int tas_play_output_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct tas2559_priv *t = snd_soc_component_get_drvdata(c);
	struct tas2559_playback *p = t->playback;
	unsigned int config = ucontrol->value.enumerated.item[0] ? 3 : 4;
	int ret = 0;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	mutex_lock(&p->lock);
	if (config == t->play_config)
		goto out;
	/* Never reconfigure an amplifier that is programmed or playing. */
	if (p->state == TAS_READY || p->state == TAS_RUNNING) {
		ret = -EBUSY;
		goto out;
	}
	t->play_config = config;
	ret = 1;
out:
	mutex_unlock(&p->lock);
	return ret;
}

static const struct snd_kcontrol_new tas_play_controls[] = {
	SOC_ENUM_EXT("TAS2559 Output", tas_play_output_enum,
		     tas_play_output_get, tas_play_output_put),
};

static const struct snd_soc_component_driver tas_play_component = {
	.suspend = tas_play_suspend,
	.controls = tas_play_controls, .num_controls = ARRAY_SIZE(tas_play_controls),
	.dapm_widgets = tas_play_widgets, .num_dapm_widgets = ARRAY_SIZE(tas_play_widgets),
	.dapm_routes = tas_play_routes, .num_dapm_routes = ARRAY_SIZE(tas_play_routes),
};

void tas2559_controlled_shutdown(struct tas2559_priv *t)
{
	struct tas2559_playback *p = t->playback;

	mutex_lock(&p->lock);
	p->removing = true;
	tas_play_stop(p);
	mutex_unlock(&p->lock);
	cancel_delayed_work_sync(&p->watchdog);
}

static void tas_play_cleanup(void *data)
{
	struct tas2559_playback *p = data;
	struct tas2559_priv *t = p->tas;

	mutex_lock(&p->lock);
	p->removing = true;
	tas_play_stop(p);
	mutex_unlock(&p->lock);
	if (p->registered)
		snd_soc_unregister_component_by_driver(t->dev, &tas_play_component);
	cancel_delayed_work_sync(&p->watchdog);
	tas2559_release_firmware(t);
	kfree(t->mpFirmware);
	kfree(t->mpCalFirmware);
	t->mpFirmware = t->mpCalFirmware = NULL;
}

int tas2559_controlled_register(struct tas2559_priv *t)
{
	struct tas2559_playback *p;
	int ret;

	p = devm_kzalloc(t->dev, sizeof(*p), GFP_KERNEL);
	if (!p) return -ENOMEM;
	p->tas = t;
	t->play_config = 4;
	mutex_init(&p->lock);
	INIT_DELAYED_WORK(&p->watchdog, tas_play_watchdog);
	t->playback = p;
	ret = devm_add_action_or_reset(t->dev, tas_play_cleanup, p);
	if (ret) return ret;
	ret = tas2559_playback_firmware(t);
	if (ret) return ret;
	ret = snd_soc_register_component(t->dev, &tas_play_component, &tas_play_dai, 1);
	if (ret) return ret;
	p->registered = true;
	return 0;
}
