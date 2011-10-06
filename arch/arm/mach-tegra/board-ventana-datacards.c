/*
 * arch/arm/mach-tegra/board-ventana-datacards.c
 *
 * Copyright (c) 2011, NVIDIA, All Rights Reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/switch.h>
#include <linux/io.h>
#include <mach/iomap.h>

#include "devices.h"
#include "gpio-names.h"
#include "board-ventana.h"



#define HUAWEI_W_DISABLE_N_GPIO TEGRA_GPIO_PH1
#define HUAWEI_PRESET_N_GPIO    TEGRA_GPIO_PH3


#define SYS_CLK_REQ_GPIO        TEGRA_GPIO_PZ5
#define DPD_PADS_OVERRIDE       0x1c

static inline int __datacards_gpio_request(int gpio, char *label)
{
	int ret;
	ret = gpio_request(gpio, label);
	if (ret < 0) {
		printk(KERN_ERR "%s: request %s failed (%d)\n", __func__, label, ret);
	}
	return ret;
}

void ventana_huawei_power_up_sequence(int up)
{
	int state;

	if (up)
	{
		
		msleep(25);
		state = 1;
	}else {
		state = 0;
	}
	gpio_set_value(HUAWEI_W_DISABLE_N_GPIO, state);
	return;
}
EXPORT_SYMBOL(ventana_huawei_power_up_sequence);

static void __init ventana_datacards_power_on(void)
{
	int ret;

	tegra_gpio_enable(DATACARD_3G_3P3V);
	ret = __datacards_gpio_request(DATACARD_3G_3P3V, "ventana_datacards_3v3");
	if (ret < 0) 
		return;
	gpio_direction_output(DATACARD_3G_3P3V, 1);	
	return;
}

static void __init ventana_huawei_init(void)
{
	int ret;

	
	tegra_gpio_enable(HUAWEI_PRESET_N_GPIO);
	ret = __datacards_gpio_request(HUAWEI_PRESET_N_GPIO, "huawei_preset_n");
	if (ret < 0) 
		return;
	gpio_direction_output(HUAWEI_PRESET_N_GPIO, 1);
	gpio_export(HUAWEI_PRESET_N_GPIO, false);

	
	tegra_gpio_enable(HUAWEI_W_DISABLE_N_GPIO);
	ret = __datacards_gpio_request(HUAWEI_W_DISABLE_N_GPIO, "huawei_w_disable_n");
	if (ret < 0) 
		return;
	gpio_direction_output(HUAWEI_W_DISABLE_N_GPIO, 0);
	gpio_export(HUAWEI_W_DISABLE_N_GPIO, false);

	
	writel(0, IO_ADDRESS(TEGRA_PMC_BASE) + DPD_PADS_OVERRIDE);
	tegra_gpio_enable(SYS_CLK_REQ_GPIO);
	ret = __datacards_gpio_request(SYS_CLK_REQ_GPIO, "sys_clk_req_pz5");
	if (ret < 0) 
		return;
	gpio_direction_output(SYS_CLK_REQ_GPIO, 1);
	gpio_export(SYS_CLK_REQ_GPIO, false);

	return;
}

#ifdef CONFIG_USIM_PLUG
static struct gpio_switch_platform_data usim_plug_pdata =
{
	.name = "usimplug",
	.gpio = TEGRA_GPIO_PV0,
};
static struct platform_device usim_plug_device =
{
	.name = "usimplug",
	.id   = -1,
	.dev = {
		.platform_data = &usim_plug_pdata,
	},
};
#endif

int __init ventana_datacards_init(void)
{
	int ret;
	printk(KERN_INFO "%s()\n", __func__);

#ifdef CONFIG_USIM_PLUG
	tegra_gpio_enable(usim_plug_pdata.gpio);
	ret = platform_device_register(&usim_plug_device);
	if (ret < 0)
		printk(KERN_ERR "%s: %s register failed (%d)\n", __func__, usim_plug_device.name, ret);
#endif

	ventana_huawei_init();

	return 0;
}

int __init ventana_datacards_late_init(void)
{
	printk(KERN_INFO "%s()\n", __func__);
	ventana_datacards_power_on();
	ventana_huawei_power_up_sequence(1);

	return 0;
}
late_initcall(ventana_datacards_late_init);

