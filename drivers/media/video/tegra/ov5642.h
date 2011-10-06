/*
 * ov5642_comm.h - ov5642 sensor driver
 *
 * Copyright (C) 2011 Google Inc.
 *
 * Contributors:
 *      Rebecca Schultz Zavin <rebecca@android.com>
 *
 * Leverage OV9640.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef __MEDIA_VIDEO_OV5642_H__
#define __MEDIA_VIDEO_OV5642_H__
#include <linux/kernel.h>
#include <linux/string.h>

struct ov5642_reg {
	u16 addr;
	u8 val;
};

struct ov5642_ae_func_array {
	void (*init)(void);
	int (*full_to_half)(void*);
	int (*half_to_full)(void*, int);
};

struct ov5642_iq_func_array {
	void* (*get_iq_regs)(void);
	void* (*get_white_balance_regs)(u8);
	void* (*get_anti_banding_regs)(u8);
};

struct ov5642_af_func_array {
	void* (*get_af_reset_regs)(void);
	void* (*get_af_enable_regs)(void);
	void  (*fill_af_stats_regs)(void **, void *);
	int   (*check_af_ready)(void);
	void* (*get_af_default_focus_regs)(void);
	void* (*get_af_single_focus_regs)(void);
	int   (*af_stats_is_focusing)(void);
	int   (*af_stats_is_focused)(void);
	void* (*get_af_firmware_regs)(void);
	void* (*get_af_firmware_download_regs)(void);
};

#endif 

