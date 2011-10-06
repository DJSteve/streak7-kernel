/*
 * sound/soc/tegra/tegra_wired_jack.c
 *
 * Copyright (c) 2011, NVIDIA Corporation.
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

#include <linux/types.h>
#include <linux/gpio.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include <linux/notifier.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <mach/audio.h>

#include <linux/input.h>
#include <linux/miscdevice.h>

#include "tegra_soc.h"

#define __LUNA_HEADSETIO 0xAA
#define LUNA_HEADSET_IOCTL_SET_CODEC_START  _IOW(__LUNA_HEADSETIO, 1, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_BIAS_ENABLE  _IOW(__LUNA_HEADSETIO, 2, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_BIAS_DISABLE _IOW(__LUNA_HEADSETIO, 3, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_FVS          _IOW(__LUNA_HEADSETIO, 4, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_LINEOUT_ON   _IOW(__LUNA_HEADSETIO, 5, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_LINEOUT_OFF  _IOW(__LUNA_HEADSETIO, 6, struct luna_hdst0_info_t )

#define HEAD_DET_GPIO 0
#define DOCK_DET_GPIO 0
#define HOOK_DET_GPIO 0

#define SET_REG_VAL(r,m,l,v) (((r)&(~((m)<<(l))))|(((v)&(m))<<(l)))
#define B07_AIF_ADCL		7
#define B06_AIF_ADCR		6
#define WM8903_AUDIO_INTERFACE_0                0x18

#define DEBOUNCE_DELAY_HOOKKEY	90
#define DEBOUNCE_DELAY_HEADSET	200

struct wm8903_wired_jack_conf s_wired_jack_conf = {
	._wired_jack_conf.hp_det_n = -1,
	._wired_jack_conf.dock_det = -1,
	._wired_jack_conf.en_mic_int = -1,
	._wired_jack_conf.hook_mic_ext = -1,
	._wired_jack_conf.cdc_irq = -1,
	._wired_jack_conf.en_spkr = -1,
	._wired_jack_conf.en_headphone = -1,
	._wired_jack_conf.spkr_amp_reg = NULL,
	._wired_jack_conf.amp_reg  = NULL,
	.amp_reg_enabled = 0,
};

struct luna_hdst0_info_t {
    uint32_t   val;
};

/* Based on hp_gpio and mic_gpio, hp_gpio is active low */
enum {
	HEADSET_WITHOUT_MIC = 0x00,
	HEADSET_WITH_MIC = 0x01,
	NO_DEVICE = 0x10,
	MIC = 0x11,
};

/* These values are copied from WiredAccessoryObserver */
enum headset_state {
	BIT_NO_HEADSET = 0,
	BIT_HEADSET = (1 << 0),
	BIT_HEADSET_NO_MIC = (1 << 1),
};

static struct snd_soc_codec *s_codec;

static atomic_t		is_button_press;
static atomic_t		is_hook_registered;

static struct input_dev    *input;

#ifdef LUNA_DEBUG_HOOK_KEY
static u64 hooktime_arr[10];
static int hooktime;
#endif

static int luna_jack_func(void);
static int luna_hook_func(void);
static struct notifier_block wired_hook_switch_nb;
static void button_released(void);
static void button_pressed(void);


static struct snd_soc_jack *tegra_wired_jack;
static struct snd_soc_jack *tegra_wired_hook;
static struct snd_soc_jack *tegra_dock;

static struct snd_soc_jack_gpio wired_jack_gpios[] = {
	{
		/* gpio pin depends on board traits */
		.name = "headphone-detect-gpio",
		.report = SND_JACK_HEADSET,
		.invert = 1,
		.debounce_time = 0,
		.jack_status_check = luna_jack_func,
	},
};

static struct snd_soc_jack_gpio wired_hook_gpios[] = {
	{
		/* gpio pin depens on board traits */
		.name = "mic-detect-gpio",
		.report = SND_JACK_MICROPHONE,
		.invert = 0,
		.debounce_time = 0,
		.jack_status_check = luna_hook_func,
	},
};

static struct snd_soc_jack_gpio dock_gpios[] = {
	{
		
		.name = "dock-detect-gpio",
		.report = SND_JACK_LINEOUT,
		.invert = 1,
		.debounce_time = 100,
	},
};

#ifdef CONFIG_SWITCH
static int hdst0_misc_open(struct inode *inode, struct file *fp);
static int hdst0_misc_release(struct inode *inode, struct file *fp);
static long hdst0_misc_ioctl( struct file *fp, unsigned int cmd, unsigned long arg );

static struct file_operations hdst0_misc_fops = {
    .owner  = THIS_MODULE,
    .open   = hdst0_misc_open,
    .release = hdst0_misc_release,
    .unlocked_ioctl = hdst0_misc_ioctl,
};

static struct miscdevice hdst0_misc_device = {
    .minor  = MISC_DYNAMIC_MINOR,
    .name   = "hdst0",
    .fops   = &hdst0_misc_fops,
};

static struct switch_dev wired_switch_dev = {
	.name = "h2w",
};

static struct switch_dev dock_switch_dev = {
	.name = "dock",
};

static struct switch_dev dock_mode_switch_dev = {
	.name = "dock_mode",
};

void tegra_switch_set_state(int state)
{
	switch_set_state(&wired_switch_dev, state);
}

static int luna_hook_func(void)
{
	int enable;

	ASOC_HEADSET_DBG("");

	msleep(DEBOUNCE_DELAY_HOOKKEY);

	if (atomic_read(&is_hook_registered) == 0)
	{
	   	ASOC_HEADSET_DBG("%s, hook key detect when headset disconnect\n", __func__);
		return 0;
	}

	enable = gpio_get_value(wired_hook_gpios[HOOK_DET_GPIO].gpio);
	if (wired_hook_gpios[HOOK_DET_GPIO].invert)
		enable = !enable;
	ASOC_HEADSET_DBG("%s hook key %s\n", __func__, (enable == 1) ? "press" : "release");
	return (enable == 1 ) ? SND_JACK_MICROPHONE : 0;
}

static int luna_jack_func(void)
{
	int headset_plugin = 0;

	ASOC_HEADSET_DBG("");

	if (s_codec == NULL)
	{
		ASOC_ERR("s_codec == NULL, function not ready\n");
		return 0;
	}

	msleep (DEBOUNCE_DELAY_HEADSET);

	headset_plugin =
			(gpio_get_value(wired_jack_gpios[HEAD_DET_GPIO].gpio) == (!wired_jack_gpios[HEAD_DET_GPIO].invert));
#if 0
	if (((headset_plugin) && (wired_jack_gpios[HEAD_DET_GPIO].report != 0))
			 || ((!headset_plugin) && (wired_jack_gpios[HEAD_DET_GPIO].report == 0)))
		return wired_jack_gpios[HEAD_DET_GPIO].report;
#endif

	if (headset_plugin)
	{
		int hook_value;

		ASOC_HEADSET_DBG("set_bias_level: SND_SOC_BIAS_ON, for checking headset / earphone");
		snd_soc_dapm_force_enable_pin(s_codec, "Mic Bias");
		snd_soc_dapm_sync(s_codec);

		
		msleep(400);

		hook_value = gpio_get_value(wired_hook_gpios[HOOK_DET_GPIO].gpio);
		ASOC_HEADSET_DBG("(%s) connect, gpio_get_value(%d) : %d\n", (hook_value == 1) ? "SND_JACK_HEADPHONE" : "SND_JACK_HEADSET", wired_hook_gpios[HOOK_DET_GPIO].gpio, hook_value);
		if (hook_value == 1)
		{
			snd_soc_dapm_disable_pin(s_codec, "Mic Bias");
			return SND_JACK_HEADPHONE;
		}
		else
		{
			if (atomic_read(&is_hook_registered) == 0)
			{
				ASOC_HEADSET_DBG("register hook function, wired_hook_gpios[HOOK_DET_GPIO].gpio: %d...\n", wired_hook_gpios[HOOK_DET_GPIO].gpio);
				snd_soc_jack_add_gpios(tegra_wired_hook,
						ARRAY_SIZE(wired_hook_gpios),
						wired_hook_gpios);
				snd_soc_jack_notifier_register(tegra_wired_hook,
						&wired_hook_switch_nb);
				atomic_set(&is_hook_registered, 1);
			}
			return SND_JACK_HEADSET;
		}
	}
	else
	{
		ASOC_HEADSET_DBG("headset disconnected...wired_jack_gpios.report: %d\n", wired_jack_gpios[HEAD_DET_GPIO].report);

		if (atomic_read(&is_hook_registered) == 1)
		{
			ASOC_HEADSET_DBG("unregister hook function...\n");
			snd_soc_jack_notifier_unregister(tegra_wired_hook, &wired_hook_switch_nb);

			snd_soc_jack_free_gpios(tegra_wired_hook,
					ARRAY_SIZE(wired_hook_gpios),
					wired_hook_gpios);

			snd_soc_dapm_disable_pin(s_codec, "Mic Bias");
			atomic_set(&is_hook_registered, 0);
		}

		if ( atomic_read(&is_button_press) == 1 )
		{
			ASOC_HEADSET_DBG("force release hook key...\n");
			
			button_released();
			atomic_set(&is_button_press, 0);
		}

		return 0;
	}
}

static int hdst0_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    ASOC_FUNCTION("");
    return result;
}

static int hdst0_misc_release(struct inode *inode, struct file *fp)
{
	int result = 0;
	ASOC_FUNCTION("");
	return result;
}

static long hdst0_misc_ioctl( struct file *fp,
                           unsigned int cmd,
                           unsigned long arg )
{   
	int  result = 0;
	ASOC_HEADSET_DBG("+hdst0_misc_ioctl, cmd: %x, %x", cmd, LUNA_HEADSET_IOCTL_SET_FVS);
	switch(cmd)
	{
		case LUNA_HEADSET_IOCTL_SET_LINEOUT_ON:
		{
			
			ASOC_HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_LINEOUT %d", 1);
			break;
		}
		case LUNA_HEADSET_IOCTL_SET_LINEOUT_OFF:
		{
			
			ASOC_HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_LINEOUT %d", 0);
			break;
		}
	} 
	return result;
}

static void button_pressed(void)
{
	ASOC_HEADSET_DBG("");
	input_report_key(input, KEY_MEDIA, 1);
	input_sync(input);
}

static void button_released(void)
{
	ASOC_HEADSET_DBG("");
	input_report_key(input, KEY_MEDIA, 0);
	input_sync(input);
}

static int dock_switch_notify(struct notifier_block *self,
			unsigned long action, void* dev)
{
	ASOC_FUNCTION("");

	ASOC_HEADSET_DBG("action %ld\n", action);
	if (action == SND_JACK_LINEOUT)
	{
		switch_set_state(&dock_switch_dev, 1);
	}
	else
	{
		switch_set_state(&dock_switch_dev, 0);
	}
	return NOTIFY_OK;
}

static int wired_switch_notify(struct notifier_block *self,
			unsigned long action, void* dev)
{
	int state = 0;
	ASOC_FUNCTION("");

	ASOC_HEADSET_DBG("action %ld\n", action);
	switch (action) {
	case SND_JACK_HEADSET:
		state = BIT_HEADSET;
		break;
	case SND_JACK_HEADPHONE:
		state = BIT_HEADSET_NO_MIC;
		break;
	default:
		state = BIT_NO_HEADSET;
	}

	tegra_switch_set_state(state);

	return NOTIFY_OK;
}

static struct notifier_block dock_switch_nb = {
	.notifier_call = dock_switch_notify,
};

static struct notifier_block wired_switch_nb = {
	.notifier_call = wired_switch_notify,
};

void tegra_jack_suspend(void)
{
	ASOC_FUNCTION("");

	disable_irq(s_wired_jack_conf._wired_jack_conf.hp_det_n);
}

void tegra_jack_resume(void)
{
	ASOC_FUNCTION("");

	tegra_switch_set_state(luna_jack_func());

	
#if 0
	tegra_switch_set_state(get_headset_state());
#endif
}

extern u64 tegra_rtc_read_ms(void);

static int wired_hook_switch_notify(struct notifier_block *self,
		unsigned long action, void* dev)
{
	ASOC_FUNCTION("");
	ASOC_HEADSET_DBG("action %ld\n", action);

#ifdef LUNA_DEBUG_HOOK_KEY
	hooktime_arr[hooktime] = tegra_rtc_read_ms();
	if ((hooktime_arr[hooktime] != 0) &&
		(hooktime_arr[hooktime] - hooktime_arr[hooktime + 1]) < 1000)
	{
		ASOC_HEADSET_DBG("hook failed\n");
		ASOC_HEADSET_DBG("unregister hook function...\n");
		snd_soc_jack_notifier_unregister(tegra_wired_hook, &wired_hook_switch_nb);

		snd_soc_jack_free_gpios(tegra_wired_hook,
				ARRAY_SIZE(wired_hook_gpios),
				wired_hook_gpios);
		atomic_set(&is_hook_registered, 0);
		button_released();
		return NOTIFY_OK;
	}

	if (++hooktime == 10)
		hooktime = 0;
#endif
	
	ASOC_HEADSET_DBG("get jack gpio in hook notify %d\n", gpio_get_value(wired_jack_gpios[HEAD_DET_GPIO].gpio));

	if (action == SND_JACK_MICROPHONE)
	{
		
		button_pressed();
		atomic_set(&is_button_press, 1);
	}
	else
	{
		if ( 1 == atomic_read(&is_button_press))
		{
			
			button_released();
			atomic_set(&is_button_press, 0);
		}
		else
		{
			ASOC_HEADSET_DBG("hook key event release with wrong state\n");
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block wired_hook_switch_nb = {
	.notifier_call = wired_hook_switch_notify,
};
#endif

/* platform driver */
static int tegra_wired_jack_probe(struct platform_device *pdev)
{
	int ret;
	int hp_det_n, cdc_irq, dock_det;
	int en_mic_int, hook_mic_ext;
	int en_spkr, en_headphone;
	struct tegra_wired_jack_conf *pdata;

	ASOC_FUNCTION("");
	pdata = (struct tegra_wired_jack_conf *)pdev->dev.platform_data;
#if 0
	if (!pdata || !pdata->hp_det_n || !pdata->en_spkr ||
		!pdata->cdc_irq || !pdata->en_mic_int || !pdata->hook_mic_ext) {
		ASOC_ERR(("Please set up gpio pins for jack.\n");
		return -EBUSY;
	}
#endif

	atomic_set(&is_button_press, 0);
	atomic_set(&is_hook_registered, 0);

	ret = misc_register( &hdst0_misc_device );
	if( ret )
	{
		ASOC_ERR("failed to register misc devices\n");
	}

	input = input_allocate_device();
	if (!input) {
		ret = -ENOMEM;
		ASOC_ERR ("err 2\n");
	}
	input->name = "Luna headset";
	input->evbit[0] = BIT_MASK(EV_KEY);
	set_bit(KEY_MEDIA, input->keybit);
	ret = input_register_device(input);
	if (ret < 0)
	{
		ASOC_ERR("Fail to register input device");
	}

#ifdef CONFIG_SWITCH
	snd_soc_jack_notifier_register(tegra_wired_jack,
			&wired_switch_nb);
	snd_soc_jack_notifier_register(tegra_dock,
			&dock_switch_nb);
#endif

	hook_mic_ext = pdata->hook_mic_ext;
	wired_hook_gpios[HOOK_DET_GPIO].gpio = hook_mic_ext;
	ASOC_INFO("hook_mic_ext gpio: %d\n", hook_mic_ext);

	dock_det = pdata->dock_det;
	dock_gpios[DOCK_DET_GPIO].gpio = dock_det;
	ASOC_INFO("dock gpio: %d\n", dock_det);

	if (dock_det >= 0)
	{
		ret = snd_soc_jack_add_gpios(tegra_dock,
						ARRAY_SIZE(dock_gpios),
						dock_gpios);
		if (ret) {
			ASOC_ERR("Could NOT set up gpio pins for dock.\n");
			return ret;
		}
	}

	hp_det_n = pdata->hp_det_n;
	wired_jack_gpios[HEAD_DET_GPIO].gpio = hp_det_n;
	ASOC_INFO("hp_det_n gpio: %d\n", hp_det_n);

	if (hp_det_n >= 0)
	{
		ret = snd_soc_jack_add_gpios(tegra_wired_jack,
						ARRAY_SIZE(wired_jack_gpios),
						wired_jack_gpios);
		if (ret) {
			ASOC_ERR("Could NOT set up gpio pins for jack.\n");
			return ret;
		}
	}

	cdc_irq = pdata->cdc_irq;
#if 0
	if (cdc_irq > 0)
	{
		wired_jack_gpios[MIC_DET_GPIO].gpio = cdc_irq;

		ret = snd_soc_jack_add_gpios(tegra_wired_jack,
				ARRAY_SIZE(wired_jack_gpios),
				wired_jack_gpios);

		if (ret) {
			ASOC_ERR("Could NOT set up gpio pins for jack.\n");
			snd_soc_jack_free_gpios(tegra_wired_jack,
						ARRAY_SIZE(wired_jack_gpios),
						wired_jack_gpios);
			return ret;
		}
	}
#endif


	/* Mic switch controlling pins */
	en_mic_int = pdata->en_mic_int;

	if (en_mic_int >= 0)
	{
		ret = gpio_request(en_mic_int, "en_mic_int");
		if (ret) {
			ASOC_ERR("Could NOT get gpio for internal mic controlling.\n");
			gpio_free(en_mic_int);
		}
		gpio_direction_output(en_mic_int, 0);
		gpio_export(en_mic_int, false);
	}

#if 0
	if (hook_mic_ext > 0)
	{
		ret = gpio_request(hook_mic_ext, "hook_mic_ext");
		if (ret) {
			ASOC_ERR("Could NOT get gpio for external mic controlling.\n");
			gpio_free(hook_mic_ext);
		}
		gpio_direction_output(hook_mic_ext, 0);
		gpio_export(hook_mic_ext, false);
	}
#endif

	en_spkr = pdata->en_spkr;

	if (en_spkr >= 0)
	{
		ret = gpio_request(en_spkr, "en_spkr");
		if (ret) {
			ASOC_ERR("Could NOT set up gpio pin for amplifier.\n");
			gpio_free(en_spkr);
		}
		gpio_direction_output(en_spkr, 0);
		gpio_export(en_spkr, false);
	}

	en_headphone = pdata->en_headphone;

	if (en_headphone >= 0)
	{
		ret = gpio_request(en_headphone, "hp_depop");
		if (ret) {
			ASOC_ERR("Could NOT set up gpio pin for headphone amplifier.\n");
			gpio_free(en_headphone);
		}
		gpio_direction_output(en_headphone, 0);
		gpio_export(en_headphone, false);
	}

	if (pdata->spkr_amp_reg)
		s_wired_jack_conf._wired_jack_conf.amp_reg =
			regulator_get(NULL, pdata->spkr_amp_reg);
	s_wired_jack_conf.amp_reg_enabled = 0;

	/* restore configuration of these pins */
	s_wired_jack_conf._wired_jack_conf.hp_det_n = hp_det_n;
	s_wired_jack_conf._wired_jack_conf.en_mic_int = en_mic_int;
	s_wired_jack_conf._wired_jack_conf.hook_mic_ext = hook_mic_ext;
	s_wired_jack_conf._wired_jack_conf.cdc_irq = cdc_irq;
	s_wired_jack_conf._wired_jack_conf.en_spkr = en_spkr;
	s_wired_jack_conf._wired_jack_conf.en_headphone = en_headphone;

#if 0
	// Communicate the jack connection state at device bootup
	tegra_switch_set_state(luna_jack_func());
#endif

	return ret;
}

static int tegra_wired_jack_remove(struct platform_device *pdev)
{
	snd_soc_jack_free_gpios(tegra_wired_jack,
				ARRAY_SIZE(wired_jack_gpios),
				wired_jack_gpios);

	snd_soc_jack_free_gpios(tegra_wired_hook,
				ARRAY_SIZE(wired_hook_gpios),
				wired_hook_gpios);

	snd_soc_jack_free_gpios(tegra_dock,
				ARRAY_SIZE(dock_gpios),
				dock_gpios);

	if (s_wired_jack_conf._wired_jack_conf.hook_mic_ext >= 0)
	{
		gpio_free(s_wired_jack_conf._wired_jack_conf.hook_mic_ext);
	}
	if (s_wired_jack_conf._wired_jack_conf.en_spkr >= 0)
	{
		gpio_free(s_wired_jack_conf._wired_jack_conf.en_spkr);
	}
	if (s_wired_jack_conf._wired_jack_conf.en_headphone >= 0)
	{
		gpio_free(s_wired_jack_conf._wired_jack_conf.en_headphone);
	}
	if (s_wired_jack_conf._wired_jack_conf.dock_det >= 0)
	{
		gpio_free(s_wired_jack_conf._wired_jack_conf.dock_det);
	}

	if (s_wired_jack_conf._wired_jack_conf.amp_reg) {
		if (s_wired_jack_conf.amp_reg_enabled)
			regulator_disable(s_wired_jack_conf._wired_jack_conf.amp_reg);
		regulator_put(s_wired_jack_conf._wired_jack_conf.amp_reg);
	}

	return 0;
}

static struct platform_driver tegra_wired_jack_driver = {
	.probe = tegra_wired_jack_probe,
	.remove = tegra_wired_jack_remove,
	.driver = {
		.name = "tegra_wired_jack",
		.owner = THIS_MODULE,
	},
};


int tegra_jack_init(struct snd_soc_codec *codec)
{
	int ret;

	if (!codec)
		return -1;

	printk(KERN_INFO "BootLog +%s+\n", __func__);

	s_codec = codec;

#ifdef CONFIG_SWITCH
	
	ret = switch_dev_register(&wired_switch_dev);
	if (ret < 0)
		goto switch_dev_failed;

	
	ret = switch_dev_register(&dock_switch_dev);
	if (ret < 0)
		goto switch_dev_failed;
	
	
	ret = switch_dev_register(&dock_mode_switch_dev);
	if (ret < 0)
		goto switch_dev_failed;
#endif

	tegra_wired_jack = kzalloc(sizeof(*tegra_wired_jack), GFP_KERNEL);
	if (!tegra_wired_jack) {
		ASOC_ERR("failed to allocate tegra_wired_jack \n");
		return -ENOMEM;
	}

	/* Add jack detection */
	ret = snd_soc_jack_new(codec->socdev->card, "Wired Accessory Jack",
			       SND_JACK_HEADSET, tegra_wired_jack);
	if (ret < 0)
		goto failed;

	tegra_dock = kzalloc(sizeof(*tegra_dock), GFP_KERNEL);
	if (!tegra_dock) {
		ASOC_ERR("failed to allocate tegra_dock \n");
		return -ENOMEM;
	}

	
	ret = snd_soc_jack_new(codec->socdev->card, "Dock Detection",
			       SND_JACK_LINEOUT, tegra_dock);
	if (ret < 0)
		goto failed;

	tegra_wired_hook = kzalloc(sizeof(*tegra_wired_hook), GFP_KERNEL);
	if (!tegra_wired_hook) {
		ASOC_ERR("failed to allocate tegra_wired_hook \n");
		return -ENOMEM;
	}

	
	ret = snd_soc_jack_new(codec->socdev->card, "Wired Accessory Hook",
			SND_JACK_MICROPHONE, tegra_wired_hook);
	if (ret < 0)
		goto failed;

	ret = platform_driver_register(&tegra_wired_jack_driver);
	if (ret < 0)
		goto platform_dev_failed;

	return 0;

#ifdef CONFIG_SWITCH
switch_dev_failed:
	switch_dev_unregister(&wired_switch_dev);
	switch_dev_unregister(&dock_switch_dev);
	switch_dev_unregister(&dock_mode_switch_dev);
#endif
platform_dev_failed:
	platform_driver_unregister(&tegra_wired_jack_driver);
failed:
	if (tegra_wired_jack) {
		kfree(tegra_wired_jack);
		tegra_wired_jack = 0;
	}
	if (tegra_wired_hook) {
		kfree(tegra_wired_hook);
		tegra_wired_hook = 0;
	}
	if (tegra_dock) {
		kfree(tegra_dock);
		tegra_dock = 0;
	}

	printk(KERN_INFO "BootLog -%s-\n", __func__);

	return ret;
}

void tegra_jack_exit(void)
{
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&wired_switch_dev);
	switch_dev_unregister(&dock_switch_dev);
	switch_dev_unregister(&dock_mode_switch_dev);
#endif
	platform_driver_unregister(&tegra_wired_jack_driver);

	if (tegra_wired_jack) {
		kfree(tegra_wired_jack);
		tegra_wired_jack = 0;
	}
	if (tegra_wired_hook) {
		kfree(tegra_wired_hook);
		tegra_wired_hook = 0;
	}
	if (tegra_dock) {
		kfree(tegra_dock);
		tegra_dock = 0;
	}
}
