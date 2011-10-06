/*
 * tegra_soc_controls.c -- alsa controls for tegra SoC
 *
 * Copyright (c) 2010-2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/gpio.h>
#include "tegra_soc.h"
#include "../codecs/wm8903.h"

#define BPASS_FUNC
#define TEGRA_BPASS_OFF				0
#define TEGRA_BPASS_ENABLELOG		1
#define TEGRA_BPASS_HEADSET			4
#define TEGRA_BPASS_SPEAKER			5
#define TEGRA_BPASS_LINEOUT			6
#define TEGRA_BPASS_FORCE_SPEAKER	7


#define B00_IN_VOL		0
#define B00_INR_ENA		0
#define B01_INL_ENA		1
#define B01_MICDET_ENA		1
#define B00_MICBIAS_ENA		0
#define B15_DRC_ENA		15
#define B01_ADCL_ENA		1
#define B00_ADCR_ENA		0
#define B06_IN_CM_ENA		6
#define B04_IP_SEL_N		4
#define B02_IP_SEL_P		2
#define B00_MODE 		0
#define B07_AIF_ADCL		7
#define B06_AIF_ADCR		6
#define B04_ADC_HPF_ENA		4
#define R20_SIDETONE_CTRL	32
#define R29_DRC_1		41
#define SET_REG_VAL(r,m,l,v) (((r)&(~((m)<<(l))))|(((v)&(m))<<(l)))

extern struct wm8903_wired_jack_conf s_wired_jack_conf;

int luna_audio_debug_mask = LUNA_AUDIO_ERR
		| LUNA_AUDIO_DEBUG
#if 0
		| LUNA_AUDIO_FUNCTION_TRACE
		| LUNA_AUDIO_HEADSET_DEBUG
#endif
		; 

static struct snd_soc_codec *s_codec;

static int tegra_bypass_func;

static void tegra_audio_route(struct tegra_audio_data* audio_data,
			      int device_new, int is_call_mode_new)
{
	int play_device_new = device_new & TEGRA_AUDIO_DEVICE_OUT_ALL;
	int capture_device_new = device_new & TEGRA_AUDIO_DEVICE_IN_ALL;
	int codec_con = audio_data->codec_con;
	int is_bt_sco_mode =
		(play_device_new & TEGRA_AUDIO_DEVICE_OUT_BT_SCO) ||
		(capture_device_new & TEGRA_AUDIO_DEVICE_IN_BT_SCO);
	int was_bt_sco_mode =
		(audio_data->play_device & TEGRA_AUDIO_DEVICE_OUT_BT_SCO) ||
		(audio_data->capture_device & TEGRA_AUDIO_DEVICE_IN_BT_SCO);

	ASOC_FUNCTION("");
	pr_info("%s : device_new : %x\n", __func__, device_new);

	if (play_device_new != audio_data->play_device) {
		codec_con &= ~(TEGRA_HEADPHONE | TEGRA_LINEOUT |
			TEGRA_SPK | TEGRA_EAR_SPK | TEGRA_HEADSET);

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_HEADPHONE)
			codec_con |= TEGRA_HEADPHONE;

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_LINE)
			codec_con |= TEGRA_LINEOUT;

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_SPEAKER)
			codec_con |= TEGRA_SPK;

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_EAR_SPEAKER)
			codec_con |= TEGRA_EAR_SPK;

		if (play_device_new & TEGRA_AUDIO_DEVICE_OUT_HEADSET)
			codec_con |= TEGRA_HEADSET;

		tegra_ext_control(audio_data->codec, codec_con);
		audio_data->play_device = play_device_new;
	}

	if (capture_device_new != audio_data->capture_device) {
		codec_con &= ~(TEGRA_INT_MIC | TEGRA_EXT_MIC |
			TEGRA_LINEIN | TEGRA_HEADSET);

		if (capture_device_new & (TEGRA_AUDIO_DEVICE_IN_BUILTIN_MIC |
			TEGRA_AUDIO_DEVICE_IN_BACK_MIC))
			codec_con |= TEGRA_INT_MIC;

		if (capture_device_new & TEGRA_AUDIO_DEVICE_IN_MIC)
			codec_con |= TEGRA_EXT_MIC;

		if (capture_device_new & TEGRA_AUDIO_DEVICE_IN_LINE)
			codec_con |= TEGRA_LINEIN;

		if (capture_device_new & TEGRA_AUDIO_DEVICE_IN_HEADSET)
			codec_con |= TEGRA_HEADSET;

		tegra_ext_control(audio_data->codec, codec_con);
		audio_data->capture_device = capture_device_new;
	}

	if ((is_call_mode_new != audio_data->is_call_mode) ||
		(is_bt_sco_mode != was_bt_sco_mode)) {
		if (is_call_mode_new && is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_voicecall_with_bt);
		}
		else if (is_call_mode_new && !is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_voicecall_no_bt);
		}
		else if (!is_call_mode_new && is_bt_sco_mode) {
			tegra_das_set_connection
				(tegra_das_port_con_id_bt_codec);
		}
		else {
			tegra_das_set_connection
				(tegra_das_port_con_id_hifi);
		}
		audio_data->is_call_mode = is_call_mode_new;
	}
}

static int tegra_bypass_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	ASOC_FUNCTION("");
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TEGRA_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = TEGRA_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_bypass_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ASOC_FUNCTION("");
	ucontrol->value.integer.value[0] = tegra_bypass_func;

	return 0;
}

static int tegra_bypass_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 val;
	struct tegra_audio_data* audio_data = s_codec->socdev->codec_data;

	ASOC_FUNCTION("");
	tegra_bypass_func = ucontrol->value.integer.value[0];
	if (tegra_bypass_func == TEGRA_BPASS_OFF)
	{
		ASOC_DBG("tegra_bypass_func == TEGRA_BPASS_OFF");

		val = 0xc;
		snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_6, val);
		val = 0x8;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_MIX_0, val);
		val = 0x4;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_MIX_0, val);
		val = 0x8;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_LEFT_0, val);
		val = 0x4;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_RIGHT_0, val);
		snd_soc_dapm_sync(s_codec);
		gpio_set_value_cansleep(s_wired_jack_conf._wired_jack_conf.en_spkr, 0);
		return 0;
	}

	if (tegra_bypass_func == TEGRA_BPASS_ENABLELOG)
	{
		luna_audio_debug_mask |= LUNA_AUDIO_FUNCTION_TRACE;
		return 0;
	}

	if (tegra_bypass_func == TEGRA_BPASS_FORCE_SPEAKER)
	{
		ASOC_DBG("tegra_bypass_func == TEGRA_BPASS_FORCE_SPEAKER");

		val = 0xc;
		snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_6, val);
		val = 0x8;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_MIX_0, val);
		val = 0x4;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_MIX_0, val);
		val = 0x8;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_LEFT_0, val);
		val = 0x4;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_RIGHT_0, val);
		if (s_wired_jack_conf._wired_jack_conf.en_spkr != -1)
		{
			gpio_set_value_cansleep(s_wired_jack_conf._wired_jack_conf.en_spkr, 1);
		}
		return 0;
	}

	tegra_das_power_mode(true);
	clk_enable(audio_data->dap_mclk);

	snd_soc_dapm_force_enable_pin(s_codec, "Mic Bias");

	val = (0x1<<B00_MICBIAS_ENA) | (0x1<<B01_MICDET_ENA);
	snd_soc_write(s_codec, WM8903_MIC_BIAS_CONTROL_0, val);

	if (tegra_bypass_func == TEGRA_BPASS_SPEAKER)
	{
		ASOC_DBG("tegra_bypass_func == TEGRA_BPASS_SPEAKER");

		snd_soc_dapm_force_enable_pin(s_codec, "Int Spk");
		snd_soc_dapm_force_enable_pin(s_codec, "Int Mic");

		val = 0x39;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT3_LEFT, val);
		val = 0xb9;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT3_RIGHT, val);

		val = 0x1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_MIX_0, val);
		val = 0x1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_MIX_0, val);
		val = 0x1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_LEFT_0, val);
		val = 0x1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_RIGHT_0, val);
		val = 0x3;
		snd_soc_write(s_codec, WM8903_MIC_BIAS_CONTROL_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_INPUT_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_INPUT_0, val);

		val = snd_soc_read(s_codec, WM8903_AUDIO_INTERFACE_0);
		val  = SET_REG_VAL(val, 0x1, B06_AIF_ADCR, 0x0);
		val  = SET_REG_VAL(val, 0x1, B07_AIF_ADCL, 0x0);
		snd_soc_write(s_codec, WM8903_AUDIO_INTERFACE_0, val);

		val = LUNA_INTERNAL_MIC_SETTING_R1R2;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_INPUT_1, val);

		if (s_wired_jack_conf._wired_jack_conf.en_spkr != -1)
		{
			gpio_set_value_cansleep(s_wired_jack_conf._wired_jack_conf.en_spkr, 1);
		}
	}
	else if (tegra_bypass_func == TEGRA_BPASS_HEADSET)
	{
		ASOC_DBG("tegra_bypass_func == TEGRA_BPASS_HEADSET");

		val = (1 << 3) | (1<<2) | (1<<4);
		snd_soc_write(s_codec, WM8903_DC_SERVO_0, val);
		val = 0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LINEOUT_0, val);
		snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_3, val);

		val = (1<<1)|(1<<0);
		snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_2, val);

		val = 0x39;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT1_LEFT, val);
		val = 0xb9;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT1_RIGHT, val);

		val = 0x2;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_MIX_0, val);
		val = 0x2;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_MIX_0, val);
		val = 0x3;
		snd_soc_write(s_codec, WM8903_MIC_BIAS_CONTROL_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_INPUT_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_INPUT_0, val);

		val = snd_soc_read(s_codec, WM8903_AUDIO_INTERFACE_0);
		val  = SET_REG_VAL(val, 0x1, B06_AIF_ADCR, 0x1);
		val  = SET_REG_VAL(val, 0x1, B07_AIF_ADCL, 0x1);
		snd_soc_write(s_codec, WM8903_AUDIO_INTERFACE_0, val);

		snd_soc_dapm_force_enable_pin(s_codec, "Headset");
		snd_soc_dapm_force_enable_pin(s_codec, "Ext Mic");

		val = LUNA_EXTERNAL_MIC_SETTING_L2L1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_INPUT_1, val);
	}
	else if (tegra_bypass_func == TEGRA_BPASS_LINEOUT)
	{
		ASOC_DBG("tegra_bypass_func == TEGRA_BPASS_LINEOUT");

		val = 0x39;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT2_LEFT, val);
		val = 0xb9;
		snd_soc_write(s_codec, WM8903_ANALOGUE_OUT2_RIGHT, val);

		val = 0x8;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_LEFT_0, val);
		val = 0x4;
		snd_soc_write(s_codec, WM8903_ANALOGUE_SPK_MIX_RIGHT_0, val);
		val = 0x2;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_MIX_0, val);
		val = 0x2;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_MIX_0, val);
		val = 0x3;
		snd_soc_write(s_codec, WM8903_MIC_BIAS_CONTROL_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_INPUT_0, val);
		val = 0x0;
		snd_soc_write(s_codec, WM8903_ANALOGUE_RIGHT_INPUT_0, val);

		val = snd_soc_read(s_codec, WM8903_AUDIO_INTERFACE_0);
		val  = SET_REG_VAL(val, 0x1, B06_AIF_ADCR, 0x1);
		val  = SET_REG_VAL(val, 0x1, B07_AIF_ADCL, 0x1);
		snd_soc_write(s_codec, WM8903_AUDIO_INTERFACE_0, val);

		snd_soc_dapm_force_enable_pin(s_codec, "Lineout");
		snd_soc_dapm_force_enable_pin(s_codec, "Ext Mic");

		val = LUNA_EXTERNAL_MIC_SETTING_L2L1;
		snd_soc_write(s_codec, WM8903_ANALOGUE_LEFT_INPUT_1, val);
	}

	val = 0x3;
	snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_0, val);
	snd_soc_write(s_codec, WM8903_POWER_MANAGEMENT_6, val);
	snd_soc_dapm_force_enable_pin(s_codec, "ADCR");
	snd_soc_dapm_force_enable_pin(s_codec, "ADCL");

	snd_soc_dapm_sync(s_codec);

	return 0;
}

static int tegra_play_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	ASOC_FUNCTION("");
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TEGRA_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = TEGRA_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_play_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->play_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_play_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	if (audio_data) {
		int play_device_new = ucontrol->value.integer.value[0] &
				TEGRA_AUDIO_DEVICE_OUT_ALL;

		if (audio_data->play_device != play_device_new) {
			tegra_audio_route(audio_data,
				play_device_new | audio_data->capture_device,
				audio_data->is_call_mode);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_bypass_route_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Bypass Function",
	.private_value = 0xffff,
	.info = tegra_bypass_route_info,
	.get = tegra_bypass_route_get,
	.put = tegra_bypass_route_put
};

struct snd_kcontrol_new tegra_play_route_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Pcm Playback Route",
	.private_value = 0xffff,
	.info = tegra_play_route_info,
	.get = tegra_play_route_get,
	.put = tegra_play_route_put
};

static int tegra_capture_route_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	ASOC_FUNCTION("");
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TEGRA_AUDIO_DEVICE_NONE;
	uinfo->value.integer.max = TEGRA_AUDIO_DEVICE_MAX;
	return 0;
}

static int tegra_capture_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->capture_device;
		return 0;
	}
	return -EINVAL;
}

static int tegra_capture_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	if (audio_data) {
		int capture_device_new = ucontrol->value.integer.value[0] &
				TEGRA_AUDIO_DEVICE_IN_ALL;

		if (audio_data->capture_device != capture_device_new) {
			tegra_audio_route(audio_data,
				audio_data->play_device | capture_device_new,
				audio_data->is_call_mode);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_capture_route_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Pcm Capture Route",
	.private_value = 0xffff,
	.info = tegra_capture_route_info,
	.get = tegra_capture_route_get,
	.put = tegra_capture_route_put
};

static int tegra_call_mode_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	ASOC_FUNCTION("");
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int tegra_call_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	ucontrol->value.integer.value[0] = TEGRA_AUDIO_DEVICE_NONE;
	if (audio_data) {
		ucontrol->value.integer.value[0] = audio_data->is_call_mode;
		return 0;
	}
	return -EINVAL;
}

static int tegra_call_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_audio_data* audio_data = snd_kcontrol_chip(kcontrol);
	ASOC_FUNCTION("");
	if (audio_data) {
		int is_call_mode_new = ucontrol->value.integer.value[0];

		if (audio_data->is_call_mode != is_call_mode_new) {
			tegra_audio_route(audio_data,
				audio_data->play_device |
				audio_data->capture_device,
				is_call_mode_new);
			return 1;
		}
		return 0;
	}
	return -EINVAL;
}

struct snd_kcontrol_new tegra_call_mode_control = {
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Call Mode Switch",
	.private_value = 0xffff,
	.info = tegra_call_mode_info,
	.get = tegra_call_mode_get,
	.put = tegra_call_mode_put
};

int tegra_controls_init(struct snd_soc_codec *codec)
{
	struct tegra_audio_data* audio_data = codec->socdev->codec_data;
	int err;

	
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_bypass_route_control, audio_data));
	if (err < 0)
		return err;

	/* Add play route control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_play_route_control, audio_data));
	if (err < 0)
		return err;

	/* Add capture route control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_capture_route_control, audio_data));
	if (err < 0)
		return err;

	/* Add call mode switch control */
	err = snd_ctl_add(codec->card,
		snd_ctl_new1(&tegra_call_mode_control, audio_data));
	if (err < 0)
		return err;

	s_codec = codec;


	return 0;
}
