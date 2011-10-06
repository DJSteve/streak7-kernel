/*
 * ov9665.h - ov9665 sensor driver
 *
 * Copyright (C) 2011 Qisda Inc.
 *
 * Contributors:
 *      Rebecca Schultz Zavin <rebecca@android.com>
 *
 * Leverage ov9665.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef __MEDIA_VIDEO_OV9665_H__
#define __MEDIA_VIDEO_OV9665_H__

#include <linux/kernel.h>
#include <linux/string.h>

#define OV9665_I2C_ID   0x30
#define AP1040_I2C_ID   0x48

#define MSG2(format, arg...)  printk(KERN_INFO "[CAM]" format "\n", ## arg)

enum FrontSensorTypeEnum {
  SensorNonCheck = 0,
  SensorOV9665,
  SensorAptina1040,
  SensorCheckFail = 0xFFFFFFFF
};

struct ov9665_reg {
  u16 reg;
  u16 val;
};


int ap1040_read_reg(u16 reg, u16 *dat);
int ov9665_write_reg_table(const struct ov9665_reg table[]);


int ap1040_set_white_balance(int wb);
int ap1040_set_anti_banding(int banding);
int ap1040_set_mode(struct sensor_mode *mode);

#endif 

