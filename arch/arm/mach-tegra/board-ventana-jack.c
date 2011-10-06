/*
 * arch/arm/mach-tegra/board-ventana-jack.c
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

#include <linux/platform_device.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <mach/audio.h>

#include "gpio-names.h"
#include "board-ventana.h"

static struct tegra_wired_jack_conf ventana_wr_jack_conf = {
	.hp_det_n = TEGRA_GPIO_PS0,
	.dock_det = TEGRA_GPIO_PA0,
	.hook_mic_ext = TEGRA_GPIO_PS4,
	.en_mic_int = -1,
	.en_spkr = WM8903_GP3,
	.en_headphone = TEGRA_GPIO_PR0,
	.cdc_irq = -1,
};

static struct platform_device ventana_hs_jack_device = {
	.name = "tegra_wired_jack",
	.id = -1,
	.dev = {
		.platform_data = &ventana_wr_jack_conf,
	},
};

int __init ventana_wired_jack_init(void)
{
	int ret;

	if (ventana_wr_jack_conf.hp_det_n >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.hp_det_n);
	if (ventana_wr_jack_conf.en_mic_int >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.en_mic_int);
	if (ventana_wr_jack_conf.en_headphone >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.en_headphone);
	if (ventana_wr_jack_conf.hook_mic_ext >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.hook_mic_ext);
	if (ventana_wr_jack_conf.cdc_irq >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.cdc_irq);
	if (ventana_wr_jack_conf.dock_det >= 0)
		tegra_gpio_enable(ventana_wr_jack_conf.dock_det);

	ret = platform_device_register(&ventana_hs_jack_device);
	return ret;
}
late_initcall(ventana_wired_jack_init);
