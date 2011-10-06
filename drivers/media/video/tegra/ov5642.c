/*
 * ov5642.c - ov5642 sensor driver
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
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <media/yuv_sensor.h>

#include "ov5642.h"
#if 0
struct ov5642_reg {
	u16 addr;
	u16 val;
};
#endif

enum ov5642_af_state {
	UNKNOWN_STATE,
	DOWNLOADING_STATE,
	IDLE_STATE,
};

struct ov5642_info {
	int mode;
	int capture;
	struct i2c_client *i2c_client;
	struct yuv_sensor_platform_data *pdata;
	int wb;
	int antibanding;

	char doAutoFocus;
	wait_queue_head_t waitAutoFocus;
	enum ov5642_af_state afState;
	struct work_struct af_download_work;
	struct semaphore af_sem;
};
#if 0
static struct ov5642_reg mode_start[] = {
	{0x4800, 0x04},
	{0x4803, 0x5f},
	{0x300e, 0x18},
	{0x4801, 0x03},
	
	
	{SENSOR_WAIT_MS, 5}, 
	{SENSOR_TABLE_END, 0x0000}
};
#endif
static struct ov5642_reg default_setting[] = {
    
    
    
    {0x3103, 0x93},
    {0x3008, 0x82},
    {SENSOR_WAIT_MS, 10}, 
    {0x3017, 0x7f},
    {0x3018, 0xfc},
    {0x3810, 0xc2},
    {0x3615, 0xf0},
    {0x3000, 0x20}, 
    {0x3001, 0x00},
    {0x3002, 0x00},
    {0x3003, 0x00},
    {0x3004, 0xdf}, 
    {0x3030, 0xb }, 
    {0x3011, 0x08},
    {0x3010, 0x10},
    {0x3604, 0x60},
    {0x3622, 0x60},
    {0x3621, 0x09},
    {0x3709, 0x00},
    {0x4000, 0x21},
    {0x401d, 0x22},
    {0x3600, 0x54},
    {0x3605, 0x04},
    {0x3606, 0x3f},

    
    {0x3623, 0x01},
    {0x3630, 0x24},
    {0x3633, 0x00},
    {0x3c00, 0x00},
    {0x3c01, 0x34},
    {0x3c04, 0x28},
    {0x3c05, 0x98},
    {0x3c06, 0x00},
    {0x3c07, 0x07},
    {0x3c08, 0x01},
    {0x3c09, 0xc2},
    {0x300d, 0x22}, 
    {0x3104, 0x01},
    {0x3c0a, 0x4e},
    {0x3c0b, 0x1f},

    
    {0x5020, 0x04},
    {0x5181, 0x79},
    {0x5182, 0x00},
    {0x5185, 0x22},
    {0x5197, 0x01},
    {0x5500, 0x0a},
    {0x5504, 0x00},
    {0x5505, 0x7f},
    {0x5080, 0x08},
    {0x300e, 0x18},
    {0x4610, 0x00},
    {0x471d, 0x05},
    {0x4708, 0x06},
    {0x370c, 0xa0},
    {0x3808, 0x0a},
    {0x3809, 0x20},
    {0x380a, 0x07},
    {0x380b, 0x98},
    {0x380c, 0x0c},
    {0x380d, 0x80},
    {0x380e, 0x07},
    {0x380f, 0xd0},
    {0x5687, 0x94},
    {0x501f, 0x00},

    

    {0x5001, 0xcf},

    {0x5183, 0x94}, 

    
    
    {0x460b, 0x35},
    {0x471d, 0x00},
    {0x3002, 0x0c},
    {0x3002, 0x00},
    {0x4713, 0x03},
    {0x471c, 0x50},
    {0x4721, 0x02},
    {0x4402, 0x90},
    {0x460c, 0x22},
    {0x3815, 0x44},
    {0x3503, 0x07},
    {0x3501, 0x73},
    {0x3502, 0x80},
    {0x350b, 0x00},
    {0x3818, 0xc8},
    {0x3801, 0x88},
    {0x3824, 0x11},
    {0x3a00, 0x78},
    {0x3a1a, 0x04},
    {0x3a13, 0x30},
    {0x3a18, 0x00},
    {0x3a19, 0x3e}, 

    
    
    
    
    
    
    
    
    
    {0x3500, 0x00},
    {0x3501, 0x00},
    {0x3502, 0x00},
    {0x350a, 0x00},
    {0x350b, 0x00},
    {0x3503, 0x00},
    {0x3030, 0xb }, 
    {0x4407, 0x04},
    {0x5193, 0x70},
    {0x589b, 0x00},
    {0x589a, 0xc0},
    {0x401e, 0x20},
    {0x4001, 0x42},
    {0x401c, 0x06},
    {0x3825, 0xac},
    {0x3827, 0x0c},
    {0x5300, 0x00},
    {0x5301, 0x20},
    {0x5302, 0x00},
    {0x5303, 0x7c},

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    {0x3406, 0x00},
    {0x3710, 0x10},
    {0x3632, 0x51},
    {0x3702, 0x10},
    {0x3703, 0xb2},
    {0x3704, 0x18},
    {0x370b, 0x40},
    {0x370d, 0x03},
    {0x3631, 0x01},
    {0x3632, 0x52},
    {0x3606, 0x24},
    {0x3620, 0x96},
    {0x5785, 0x07},
    {0x3a13, 0x30},
    {0x3600, 0x52},
    {0x3604, 0x48},
    {0x3606, 0x1b},
    {0x370d, 0x0b},
    {0x370f, 0xc0},
    {0x3709, 0x01},

    {0x3823, 0x00},
    {0x5007, 0x00},
    {0x5009, 0x00},
    {0x5011, 0x00},
    {0x5013, 0x00},
    {0x519e, 0x00},
    {0x5086, 0x00},
    {0x5087, 0x00},
    {0x5088, 0x00},
    {0x5089, 0x00},
    {0x302b, 0x00},
    {0x460c, 0x20},
    {0x460b, 0x37},
    {0x471c, 0xd0},
    {0x471d, 0x05},
    {0x3815, 0x01},
    {0x3818, 0xc0},
    {0x501f, 0x00},
    {0x5002, 0xe0},

    {0x4300, 0x32},
    {0x3002, 0x1c},
    {0x4800, 0x34}, 
    {0x4801, 0x0f},
    {0x3007, 0x3b},
    {0x300e, 0x04},
    {0x4803, 0x50},
    {0x3815, 0x01},
    {0x4713, 0x02},
    {0x4842, 0x01},
    {0x300f, 0x06}, 
    {0x3010, 0x00}, 
    {0x3003, 0x03},
    {0x3003, 0x01},
    {0x4837, 0x08}, 
    {SENSOR_WAIT_MS, 100}, 
    {SENSOR_TABLE_END, 0x0},
};

static struct ov5642_reg mode_2592x1944[] = {
    
    
    
    {0x3503, 0x00}, 
    {0x3406, 0x00}, 
    {0x3002, 0x00}, 
    {0x3003, 0x00}, 
    {0x3005, 0xff}, 
    {0x3006, 0xff}, 
    {0x3007, 0x3f}, 
    
    {0x3011, 0x08}, 
    {0x3012, 0x00}, 
    {0x350c, 0x07}, 
    {0x350d, 0xd0}, 
    {0x3602, 0xe4}, 
    {0x3612, 0xac}, 
    {0x3613, 0x44}, 
    {0x3621, 0x09}, 
    {0x3622, 0x60}, 
    {0x3623, 0x22}, 
    
    {0x3705, 0xda}, 
    {0x370a, 0x80}, 
    {0x370d, 0x0b}, 
    {0x3801, 0x88}, 
    {0x3803, 0x0a}, 
    {0x3804, 0x0a}, 
    {0x3805, 0x20}, 
    {0x3806, 0x07}, 
    {0x3807, 0x98}, 
    {0x3808, 0x0a}, 
    {0x3809, 0x20}, 
    {0x380a, 0x07}, 
    {0x380b, 0x98}, 
    {0x380c, 0x0c}, 
    {0x380d, 0x80}, 
    {0x380e, 0x07}, 
    {0x380f, 0xd0}, 
    {0x3810, 0xc2}, 
    {0x3815, 0x01}, 
    {0x3818, 0xc0}, 
    {0x3824, 0x01}, 
    {0x3825, 0xac}, 
    {0x3827, 0x0c}, 
    {0x3a08, 0x09}, 
    {0x3a09, 0x60}, 
    {0x3a0a, 0x07}, 
    {0x3a0b, 0xd0}, 
    {0x3a0d, 0x10}, 
    {0x3a0e, 0x0d}, 
    {0x3a1a, 0x04}, 
    {0x4007, 0x20}, 
    {0x5682, 0x0a}, 
    {0x5683, 0x20}, 
    {0x5686, 0x07}, 
    {0x5687, 0x94}, 
    {0x4407, 0x04}, 
    {0x589b, 0x00}, 
    {0x589a, 0xc0}, 
    
    {0x4837, 0x08}, 
    {0x3003, 0x03},
    {0x3003, 0x01},
    {SENSOR_TABLE_END, 0x0000}
};

static struct ov5642_reg mode_1280x960[] = {


    {0x3503, 0x00},
    {0x3406, 0x00},
    {0x3002, 0x5c},
    {0x3003, 0x02},
    {0x3005, 0xff},
    {0x3006, 0x43},
    {0x3007, 0x37},
    {0x350c, 0x03},
    {0x350d, 0xf0},
    {0x3602, 0xfc},
    {0x3612, 0xff},
    {0x3613, 0x00},
    {0x3621, 0x87},
    {0x3622, 0x60},
    {0x3623, 0x01},
    {0x3604, 0x48},
    {0x3705, 0xdb},
    {0x370a, 0x81},
    {0x370d, 0x0b},
    {0x3801, 0x50},
    {0x3803, 0x08},
    {0x3804, 0x05},
    {0x3805, 0x00},
    {0x3806, 0x03},
    {0x3807, 0xc0},
    {0x3808, 0x05},
    {0x3809, 0x00},
    {0x380a, 0x03},
    {0x380b, 0xc0},
    {0x380c, 0x0c}, 
    {0x380d, 0x80}, 
    {0x380e, 0x03}, 
    {0x380f, 0xe8}, 
    {0x3810, 0x80},
    {0x3818, 0xc1},
    {0x3824, 0x11},
    {0x3825, 0xb0},
    {0x3827, 0x08},
    {0x3a08, 0x12},
    {0x3a09, 0xc0},
    {0x3a0a, 0x0f},
    {0x3a0b, 0xa0},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a1a, 0x05},
    {0x401c, 0x04},
    {0x460b, 0x37},
    {0x471d, 0x05},
    {0x4713, 0x03},
    {0x471c, 0xd0},
    {0x5682, 0x05},
    {0x5683, 0x00},
    {0x5686, 0x03},
    {0x5687, 0xbc},
    {0x5001, 0xff},
    {0x589b, 0x04},
    {0x589a, 0xc5},
    {0x4407, 0x04},
    {0x589b, 0x00},
    {0x589a, 0xc0},
    {0x3002, 0x0c},
    {0x3002, 0x00},
    {0x3503, 0x00},
    {0x4801, 0x0f},
    {0x3007, 0x3b},
    {0x300e, 0x04},
    {0x4803, 0x50},
    {0x4713, 0x02},
    {0x4842, 0x01},
    {0x3818, 0xc1},
    {0x3621, 0x87},
    {0x300f, 0x0e},
    {0x3010, 0x00},
    {0x3011, 0x08},
    {0x3012, 0x00},
    {0x3815, 0x01},
    {0x3029, 0x00},
    {0x3033, 0x03},
    {0x3a08, 0x12}, 
    {0x3a09, 0xc0}, 
    {0x3a0e, 0x03}, 
    {0x3a0a, 0x0f}, 
    {0x3a0b, 0xa0}, 
    {0x3a0d, 0x04}, 
    {0x4837, 0x07},
    {0x3003, 0x03},
    {0x3003, 0x01},
    {SENSOR_TABLE_END, 0x0000}
};

static struct ov5642_reg mode_176x144[] = {
    {0x3503, 0x00},
    {0x3406, 0x00},
    {0x3002, 0x5c},
    {0x3003, 0x02},
    {0x3005, 0xff},
    {0x3006, 0x43},
    {0x3007, 0x37},
    {0x350c, 0x03},
    {0x350d, 0xf0},
    {0x3602, 0xfc},
    {0x3612, 0xff},
    {0x3613, 0x00},
    {0x3621, 0x87},
    {0x3622, 0x60},
    {0x3623, 0x01},
    {0x3604, 0x48},
    {0x3705, 0xdb},
    {0x370a, 0x81},
    {0x370d, 0x0b},
    {0x3801, 0x50},
    {0x3803, 0x08},
    {0x3804, 0x05},
    {0x3805, 0x00},
    {0x3806, 0x03},
    {0x3807, 0xc0},
    {0x3808, 0x00}, 
    {0x3809, 0xb0}, 
    {0x380a, 0x00}, 
    {0x380b, 0x90}, 
    {0x380c, 0x0c}, 
    {0x380d, 0x80}, 
    {0x380e, 0x03}, 
    {0x380f, 0xe8}, 
    {0x3810, 0x80},
    {0x3818, 0xc1},
    {0x3824, 0x11},
    {0x3825, 0xb0},
    {0x3827, 0x08},
    {0x3a08, 0x12},
    {0x3a09, 0xc0},
    {0x3a0a, 0x0f},
    {0x3a0b, 0xa0},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a1a, 0x05},
    {0x401c, 0x04},
    {0x460b, 0x37},
    {0x471d, 0x05},
    {0x4713, 0x03},
    {0x471c, 0xd0},
    {0x5682, 0x05},
    {0x5683, 0x00},
    {0x5686, 0x03},
    {0x5687, 0xbc},
    {0x5001, 0xff},
    {0x589b, 0x04},
    {0x589a, 0xc5},
    {0x4407, 0x04},
    {0x589b, 0x00},
    {0x589a, 0xc0},
    {0x3002, 0x0c},
    {0x3002, 0x00},
    {0x3503, 0x00},
    {0x4801, 0x0f},
    {0x3007, 0x3b},
    {0x300e, 0x04},
    {0x4803, 0x50},
    {0x4713, 0x02},
    {0x4842, 0x01},
    {0x3818, 0xc1},
    {0x3621, 0x87},
    {0x300f, 0x0e},
    {0x3010, 0x00},
    {0x3011, 0x08},
    {0x3012, 0x00},
    {0x3815, 0x01},
    {0x3029, 0x00},
    {0x3033, 0x03},
    {0x3a08, 0x12}, 
    {0x3a09, 0xc0}, 
    {0x3a0e, 0x03}, 
    {0x3a0a, 0x0f}, 
    {0x3a0b, 0xa0}, 
    {0x3a0d, 0x04}, 
    {0x4837, 0x07},
    {0x3003, 0x03},
    {0x3003, 0x01},
    {SENSOR_TABLE_END, 0x0000}
};
#if 0
static struct ov5642_reg mode_end[] = {
	{0x4801, 0x0f},
	{0x300e, 0x0c}, 
	{0x4803, 0x50},
	{0x4800, 0x34},

	
	{0x3003, 0x01}, 
	
	
	

	
	{SENSOR_TABLE_END, 0x0000}
};
#endif
static struct ov5642_reg safe_power_down[] = {


	{0x300e, 0x18}, 
	{0x3008, 0x42},
	{SENSOR_WAIT_MS, 10},
	{SENSOR_TABLE_END, 0x0000}
};

enum {
	OV5642_MODE_2592x1944,
	OV5642_MODE_1280x960,
	OV5642_MODE_176x144,
};

static struct ov5642_reg *mode_table[] = {
	[OV5642_MODE_2592x1944] = mode_2592x1944,
	[OV5642_MODE_1280x960] = mode_1280x960,
	[OV5642_MODE_176x144] = mode_176x144,
};


extern struct ov5642_ae_func_array ov5642_ae_func;
extern struct ov5642_iq_func_array ov5642_iq_func;
extern struct ov5642_af_func_array ov5642_af_func;



static struct ov5642_reg stop_ae_awb_regs[] = {
	{0x3406, 0x01},
	{0x3503, 0x07},
	{SENSOR_TABLE_END, 0x0}
};


static int ov5642_read_reg(struct i2c_client *client, u16 addr, u8 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[3];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;

	
	data[0] = (u8) (addr >> 8);;
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = data + 2;
	
	
	err = i2c_transfer(client->adapter, &msg[0], 1);
	if (err != 1)
		return -EINVAL;
	err = i2c_transfer(client->adapter, &msg[1], 1);
	if (err != 1)
		return -EINVAL;

	*val = data[2];

	return 0;
}

static int ov5642_write_reg(struct i2c_client *client, u16 addr, u8 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[3];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);;
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("ov5642: i2c transfer failed, retrying %x %x\n",
		       addr, val);
		msleep(3);
	} while (retry <= SENSOR_MAX_RETRIES);

	return err;
}

static int ov5642_write_table(struct i2c_client *client,
			      const struct ov5642_reg table[],
			      const struct ov5642_reg override_list[],
			      int num_override_regs)
{
	int err;
	const struct ov5642_reg *next;
	int i;
	u8 val;

	for (next = table; next->addr != SENSOR_TABLE_END; next++) {
		if (next->addr == SENSOR_WAIT_MS) {
			msleep(next->val);
			continue;
		}

		val = next->val;

		
		
		if (override_list) {
			for (i = 0; i < num_override_regs; i++) {
				if (next->addr == override_list[i].addr) {
					val = override_list[i].val;
					break;
				}
			}
		}

		if (next->addr == SENSOR_BIT_OPERATION) {
			u8 mask, val_read;
			u16 addr;

			
			mask = next->val;
			next++;
			addr = next->addr;
			val = next->val;
			err = ov5642_read_reg(client, addr, &val_read); 
			if (err)
				return err;
			val_read &= (~mask);
			val |= val_read;
		}

		err = ov5642_write_reg(client, next->addr, val);
		if (err)
			return err;
	}
	return 0;
}

static int ov5642_write_reg_burst_mode(struct i2c_client *client, u8 *data, int len)
{
	int err;
	struct i2c_msg msg;
	int retry = SENSOR_MAX_RETRIES; 

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("%s: i2c transfer failed %d, retrying %02x%02xh %d bytes\n",
			__func__, err, data[0], data[1], len);
		msleep(3);
	} while (retry <= SENSOR_MAX_RETRIES);

	return err;
}

#define OV5642_BURST_MODE_LEN 258 
static u8 burst_mode_buf[OV5642_BURST_MODE_LEN];
static int ov5642_write_table_burst_mode(struct i2c_client *client,
					const struct ov5642_reg table[])
{
	int err = 0;
	const struct ov5642_reg *next;
	int build_cnt = 0;

	for (next = table; next->addr != SENSOR_TABLE_END; next++) {
		if (build_cnt == OV5642_BURST_MODE_LEN) {
			err = ov5642_write_reg_burst_mode(client, burst_mode_buf, build_cnt);
			if (err)
				return err;
			build_cnt = 0;
		}

		if (build_cnt == 0) {
			burst_mode_buf[build_cnt++] = (u8) (next->addr >> 8);
			burst_mode_buf[build_cnt++] = (u8) (next->addr & 0xff);
		}	
		burst_mode_buf[build_cnt++] = (u8) (next->val & 0xff);
	}
	
	if (build_cnt)
		err = ov5642_write_reg_burst_mode(client, burst_mode_buf, build_cnt);
		 
	return err;
}

static int ov5642_read_af_status(struct ov5642_info *info)
{
	struct ov5642_reg *reg_list;
	int i, rc, reg_len;

	ov5642_af_func.fill_af_stats_regs((void**)&reg_list, &reg_len);
	
	for (i=0; i<reg_len; i++) {
		rc = ov5642_read_reg(info->i2c_client, 
				     reg_list[i].addr, 
				     &reg_list[i].val);
		if (rc)
			return rc;
	}
	return 0;
}

#define FOCUSING_POLLING_DUR 2000 
#define FOCUSING_POLLING_INT 100  
static int ov5642_set_focus(struct ov5642_info *info)
{
	int rc = 0;

	int i;
	struct timespec focusStart;
	unsigned long focusDur;

	down(&info->af_sem);
	info->doAutoFocus = 1;

	
	ov5642_write_table(info->i2c_client,
			(struct ov5642_reg *)ov5642_af_func.get_af_default_focus_regs(),
			NULL, 0);

	
	rc = ov5642_write_table(info->i2c_client,
			(struct ov5642_reg *)ov5642_af_func.get_af_single_focus_regs(),
			NULL, 0);
	if (rc)
		goto not_focused;

	
	focusStart = CURRENT_TIME;

	for (i=0; i<FOCUSING_POLLING_DUR/FOCUSING_POLLING_INT; i++) {
		up(&info->af_sem);
		rc = wait_event_interruptible_timeout(
				info->waitAutoFocus,
				!info->doAutoFocus,
				msecs_to_jiffies(FOCUSING_POLLING_INT));
		down(&info->af_sem);

		if (!info->doAutoFocus) {
			pr_info("%s: canceled\n", __func__);
			rc = -EINTR;
			break;
		}

		if (rc < 0) {
			pr_info("%s: signaled\n", __func__);
			break;
		}

		rc = ov5642_read_af_status(info);
		if (rc) {
			pr_err("%s: read status failed, %d\n", __func__, rc);
			break;
		}

		
		if (ov5642_af_func.af_stats_is_focusing())
			continue;
		
		else if (ov5642_af_func.af_stats_is_focused()) {
			
			focusStart = timespec_sub(CURRENT_TIME, focusStart);
			focusDur = (focusStart.tv_sec * 1000) + (focusStart.tv_nsec / 1000000);

			pr_info("%s: focused achieved, %ld ms\n",
					__func__, focusDur);
			goto exit_focus;
		}
		
		else {
			rc = -EIO;
			pr_err("%s: invalid status\n", __func__);
			break;
		}
	}
not_focused:
	ov5642_write_table(info->i2c_client,
			(struct ov5642_reg *)ov5642_af_func.get_af_default_focus_regs(),
			NULL, 0);
exit_focus:
	info->doAutoFocus = 0;
	up(&info->af_sem);

	return rc;
}

static int ov5642_set_default_focus(struct ov5642_info *info)
{
	int rc = 0;

	down(&info->af_sem);
	
	if (info->doAutoFocus) {
		info->doAutoFocus = 0;
		wake_up(&info->waitAutoFocus);
	} else {
		rc = ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_default_focus_regs(),
				NULL, 0);
	}
	up(&info->af_sem);

	return rc;
}

static int ov5642_detect_af(struct ov5642_info *info)
{
	int rc = 0;

	down(&info->af_sem);
	if (IDLE_STATE == info->afState) {
		rc = ov5642_read_af_status(info);
		if (rc)
			goto exit_detect_af;
	
		rc = ov5642_af_func.check_af_ready();
		if (rc) {
			ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_reset_regs(),
				NULL, 0);
			ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_enable_regs(),
				NULL, 0);
		}
	} else {
		rc = -EIO;
	}
exit_detect_af:
	up(&info->af_sem);

	return rc;
}

static void ov5642_af_download_func(struct work_struct *work)
{
	struct timespec start;
	struct ov5642_info *info = container_of(work, 
				struct ov5642_info, af_download_work);

	start = CURRENT_TIME;

	ov5642_write_table_burst_mode(info->i2c_client,
		(struct ov5642_reg *)ov5642_af_func.get_af_firmware_regs());

	ov5642_write_table(info->i2c_client,
		(struct ov5642_reg *)ov5642_af_func.get_af_enable_regs(),
		NULL, 0);

	down(&info->af_sem);
	info->afState = IDLE_STATE;
	up(&info->af_sem);

	start = timespec_sub(CURRENT_TIME, start);
	pr_info("%s: %ld ms\n", __func__,
		((long)start.tv_sec*1000)+((long)start.tv_nsec/1000000));
}

static int ov5642_prepare_af(struct ov5642_info *info)
{
	int rc = 0;

	down(&info->af_sem);
	if (UNKNOWN_STATE == info->afState) {
		rc = ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_firmware_download_regs(),
				NULL, 0);
		if (rc)
			goto exit_prepare;
		info->afState = DOWNLOADING_STATE;
		schedule_work(&info->af_download_work);
	} else if (IDLE_STATE == info->afState) {
		rc |= ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_reset_regs(),
				NULL, 0);
		rc |= ov5642_write_table(info->i2c_client,
				(struct ov5642_reg *)ov5642_af_func.get_af_enable_regs(),
				NULL, 0);
	}
exit_prepare:
	up(&info->af_sem);
	return rc;
}

static void ov5642_deinit_af(struct ov5642_info *info)
{
	cancel_work_sync(&info->af_download_work);
	down(&info->af_sem);
	if (info->doAutoFocus) {
		info->doAutoFocus = 0;
		wake_up(&info->waitAutoFocus);
	}
	up(&info->af_sem);
}

static void ov5642_init_af(struct ov5642_info *info)
{
	down(&info->af_sem);
	info->afState = UNKNOWN_STATE;
	up(&info->af_sem);
}
      
static int ov5642_set_af(struct ov5642_info *info, u8 fm)
{
	int rc;

	rc = ov5642_detect_af(info);
	if (rc)
		goto exit_set_af;

	switch (fm) {
	case YUV_FocusMode_Auto:
		rc = ov5642_set_focus(info);
		break;
	case YUV_FocusMode_Infinite:
		rc = ov5642_set_default_focus(info);
		break;
	default:
		rc = -EINVAL;
		break;
	}	
exit_set_af:
	if (rc)
		pr_err("%s(), set to %u failed, %d\n", __func__, fm, rc);
	return rc;
}

static int ov5642_set_wb(struct ov5642_info *info, u8 wb)
{
	struct ov5642_reg *reg_list;
	int ret;

	reg_list = (struct ov5642_reg *)ov5642_iq_func.get_white_balance_regs(wb);
	ret = ov5642_write_table(info->i2c_client, reg_list, NULL, 0);
	if (!ret)
		info->wb = wb;
	else
		pr_err("%s(), set to %u failed, %d\n", __func__, wb, ret);
	return ret;
}

static int ov5642_set_antibanding(struct ov5642_info *info, u8 banding)
{
	struct ov5642_reg *reg_list;
	int ret;

	reg_list = (struct ov5642_reg *)ov5642_iq_func.get_anti_banding_regs(banding);
	ret = ov5642_write_table(info->i2c_client, reg_list, NULL, 0);
	if (!ret)
		info->antibanding = banding;
	else
		pr_info("%s(), set to %u failed, %d\n", __func__, banding, ret);
	return ret;
}

static int ov5642_set_flashlight_status(struct ov5642_info *info, u8 power)
{

	int ret;
	pr_info("%s(), %d\n", __func__, power);

       if(power)
		ret = info->pdata->flash_light_enable();
       else
		ret = info->pdata->flash_light_disable();
       return ret;

}


static int ov5642_ae_full_to_half(struct ov5642_info *info, void *list, void *list_1)
{
	struct ov5642_reg exp_gain_vts_regs[] = {
		{ 0x3500, 0x00 }, 
		{ 0x3501, 0x00 },
		{ 0x3502, 0x00 },
		{ 0x350b, 0x00 }, 
		{ 0x350c, 0x00 }, 
		{ 0x350d, 0x00 },
	};

	struct ov5642_reg *override_list =
				(struct ov5642_reg *)list;
	struct ov5642_reg *exp_gain_reg_list =
				(struct ov5642_reg *)list_1;
	int override;

	override = ov5642_ae_func.full_to_half(exp_gain_vts_regs);

	if (override) {
		
		memcpy(exp_gain_reg_list, exp_gain_vts_regs, 4*sizeof(struct ov5642_reg));

		
		memcpy(override_list, &exp_gain_vts_regs[4], override*sizeof(struct ov5642_reg));
		override_list += override;
	}

	return override;
}

static int ov5642_ae_half_to_full(struct ov5642_info *info, void *list, void *list_1)
{
	struct ov5642_reg exp_gain_vts_regs[] = {
		{ 0x3500, 0x00 }, 
		{ 0x3501, 0x00 },
		{ 0x3502, 0x00 },
		{ 0x350b, 0x00 }, 
		{ 0x350c, 0x00 }, 
		{ 0x350d, 0x00 },
	};

	struct ov5642_reg light_freq_reg = { 0x3c0c, 0x00 };

	struct ov5642_reg *override_list =
				(struct ov5642_reg *)list;
	struct ov5642_reg *exp_gain_reg_list =
				(struct ov5642_reg *)list_1;

	int override = 0;
	int err, i, banding;

	
	err = ov5642_write_table(info->i2c_client, stop_ae_awb_regs, NULL, 0);
	if (err)
		goto exit;

	
	msleep(10);
	for(i=0; i<ARRAY_SIZE(exp_gain_vts_regs); i++) {
		err = ov5642_read_reg(info->i2c_client,
					exp_gain_vts_regs[i].addr,
					&exp_gain_vts_regs[i].val);
		if (err)
			goto exit;
	}

	banding = info->antibanding;
	if (banding == YUV_AntiBanding_Auto) {
		err = ov5642_read_reg(info->i2c_client, light_freq_reg.addr, &light_freq_reg.val);
		if (err)
			goto exit;
		banding = (light_freq_reg.val & 0x01)? YUV_AntiBanding_50Hz: YUV_AntiBanding_60Hz;
	}

	err = ov5642_ae_func.half_to_full(exp_gain_vts_regs, banding);
	if (!err) {
		
		memcpy(exp_gain_reg_list, exp_gain_vts_regs, 4*sizeof(struct ov5642_reg));

		
		memcpy(override_list, stop_ae_awb_regs, 2*sizeof(struct ov5642_reg));
		override_list += 2;
		override += 2;

		memcpy(override_list, &exp_gain_vts_regs[4], 2*sizeof(struct ov5642_reg));
		override_list += 2;
		override += 2;

		
		
	}
exit:
	return override;
}

static int ov5642_awb_override(struct ov5642_info *info, void *list)
{
	struct ov5642_reg manual_awb_regs[] = {
		{ 0x3406, 0x01 }, 
	};

	struct ov5642_reg *override_list =
				(struct ov5642_reg *)list;
	int override = 0;

	
	if (info->wb != YUV_Whitebalance_Auto && info->wb != YUV_Whitebalance_Invalid) {
		memcpy(override_list, manual_awb_regs, sizeof(manual_awb_regs));
		override++;
		ov5642_set_wb(info, info->wb);
	}
	return override;
}

static int ov5642_set_mode(struct ov5642_info *info, struct sensor_mode *mode)
{
	int sensor_mode;
	int err;
	struct ov5642_reg reg_list[6];
	struct ov5642_reg exp_gain_reg_list[5] = {
		{ SENSOR_TABLE_END, 0x0},
		{ SENSOR_TABLE_END, 0x0},
		{ SENSOR_TABLE_END, 0x0},
		{ SENSOR_TABLE_END, 0x0},
		{ SENSOR_TABLE_END, 0x0},
	};
	int override = 0;
	int capture_mode;
	int i;

	pr_info("%s: xres %u yres %u capture %u\n",
		__func__, mode->xres, mode->yres, mode->capture);
	if (mode->xres == 2592 && mode->yres == 1944)
		sensor_mode = OV5642_MODE_2592x1944;
	else if (mode->xres == 1280 && mode->yres == 960)
		sensor_mode = OV5642_MODE_1280x960;
	else if (mode->xres == 176 && mode->yres == 144)
		sensor_mode = OV5642_MODE_176x144;
	else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
		       __func__, mode->xres, mode->yres);
		return -EINVAL;
	}
	capture_mode = mode->capture;
	

	
	

	
        if (capture_mode) {
		if ((sensor_mode == OV5642_MODE_2592x1944) && (info->mode >= OV5642_MODE_1280x960))
			override = ov5642_ae_half_to_full(info, reg_list, exp_gain_reg_list);
		else
			override = ov5642_awb_override(info, reg_list);
        } else {
		if ((info->mode == OV5642_MODE_2592x1944) && (sensor_mode >= OV5642_MODE_1280x960))
			override = ov5642_ae_full_to_half(info, reg_list, exp_gain_reg_list);

		override += ov5642_awb_override(info, reg_list+override);
		ov5642_ae_func.init();
	}

	
	for (i=0; i<override; i++)
		pr_info("%s: override %04xh with 0x%02x\n", __func__, 
			reg_list[i].addr, reg_list[i].val);


	
	if (override)
		err = ov5642_write_table(info->i2c_client, mode_table[sensor_mode],
			reg_list, override);
	else
		err = ov5642_write_table(info->i2c_client, mode_table[sensor_mode],
			NULL, 0);
	if (err)
		return err;


	
	for (i=0; exp_gain_reg_list[i].addr != SENSOR_TABLE_END; i++)
		pr_info("%s: exp_gain_reg_list %04xh= 0x%02x\n", __func__, 
			exp_gain_reg_list[i].addr, exp_gain_reg_list[i].val);


	
	err = ov5642_write_table(info->i2c_client, exp_gain_reg_list, NULL, 0);
	if (err)
		return err;


	
	if (!capture_mode)
		ov5642_prepare_af(info);

	
	msleep(200);

	info->mode = sensor_mode;
	info->capture = capture_mode;
	pr_info("%s: success\n", __func__);
	return 0;
}

static int ov5642_detect_id(struct ov5642_info *info)
{
	int err;
	u8 id[2];

	id[0] = id[1] = 0;
	err = ov5642_read_reg(info->i2c_client, 0x300a, &id[0]) |
		ov5642_read_reg(info->i2c_client, 0x300b, &id[1]);
	if (!err)
		pr_info("%s: %02x%02x detected\n", __func__, id[0], id[1]);
	else
		pr_err("%s: return %d\n", __func__, err);
	return err;
}

static long ov5642_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct ov5642_info *info = file->private_data;

	switch (cmd) {
	case SENSOR_IOCTL_SET_MODE:
	{
		struct sensor_mode mode;
		if (copy_from_user(&mode,
				   (const void __user *)arg,
				   sizeof(struct sensor_mode))) {
			return -EFAULT;
		}

		return ov5642_set_mode(info, &mode);
	}
	case SENSOR_IOCTL_SET_WHITE_BALANCE:
	{
		return ov5642_set_wb(info, (u8)arg);
	}
	case SENSOR_IOCTL_SET_ANTI_BANDING:
	{
		return ov5642_set_antibanding(info, (u8)arg);
	}
    
	case SENSOR_IOCTL_SET_BLOCK_AF_MODE: 
	{
		return ov5642_set_af(info, (u8)arg);
	}
	
	case SENSOR_IOCTL_SET_FLASH_LIGHT_STATUS:
	{
		return ov5642_set_flashlight_status(info, (u8)arg);
	}
	
	default:
		return -EINVAL;
	}
	return 0;
}

static struct ov5642_info *info;

static int ov5642_open(struct inode *inode, struct file *file)
{
	int ret;
	struct ov5642_reg *reg_list;

	file->private_data = info;
	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();
	

	
	msleep(20);
	ret = ov5642_detect_id(info);
	if (ret)
		goto exit;

	
	reg_list = default_setting;
	ret = ov5642_write_table(info->i2c_client, reg_list, NULL, 0);
	if (ret)
		goto exit;

	
	info->wb = YUV_Whitebalance_Auto; 
	info->antibanding = YUV_AntiBanding_Auto; 
	info->capture = 0; 
	reg_list = (struct ov5642_reg *)ov5642_iq_func.get_iq_regs();
	ret = ov5642_write_table(info->i2c_client, reg_list, NULL, 0);

	
	ov5642_init_af(info);
exit:
	if (ret) {
		
		reg_list = safe_power_down;
		ov5642_write_table(info->i2c_client, reg_list, NULL, 0);
		if (info->pdata && info->pdata->power_off)
			info->pdata->power_off();
	}
	return ret;
}

int ov5642_release(struct inode *inode, struct file *file)
{
	ov5642_deinit_af(info);
	
	ov5642_write_table(info->i2c_client, safe_power_down, NULL, 0);
	if (info->pdata && info->pdata->power_off)
		info->pdata->power_off();
	file->private_data = NULL;
	return 0;
}


static const struct file_operations ov5642_fileops = {
	.owner = THIS_MODULE,
	.open = ov5642_open,
	.unlocked_ioctl = ov5642_ioctl,
	.release = ov5642_release,
};

static struct miscdevice ov5642_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ov5642",
	.fops = &ov5642_fileops,
};

static int ov5642_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;

	pr_info("ov5642: probing sensor.\n");

	info = kzalloc(sizeof(struct ov5642_info), GFP_KERNEL);
	if (!info) {
		pr_err("ov5642: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&ov5642_device);
	if (err) {
		pr_err("ov5642: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->pdata = client->dev.platform_data;
	info->i2c_client = client;

	init_MUTEX(&info->af_sem);
	INIT_WORK(&info->af_download_work, ov5642_af_download_func);
	init_waitqueue_head(&info->waitAutoFocus);

	i2c_set_clientdata(client, info);
	return 0;
}

static int ov5642_remove(struct i2c_client *client)
{
	struct ov5642_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&ov5642_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id ov5642_id[] = {
	{ "ov5642", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ov5642_id);

static struct i2c_driver ov5642_i2c_driver = {
	.driver = {
		.name = "ov5642",
		.owner = THIS_MODULE,
	},
	.probe = ov5642_probe,
	.remove = ov5642_remove,
	.id_table = ov5642_id,
};

static int __init ov5642_init(void)
{
	int ret;

	printk("BootLog, +%s+\n", __func__);
	pr_info("ov5642 sensor driver loading\n");
	ret = i2c_add_driver(&ov5642_i2c_driver);
	printk("BootLog, -%s-, ret=%d\n", __func__, ret);
	return ret;
}

static void __exit ov5642_exit(void)
{
	i2c_del_driver(&ov5642_i2c_driver);
}

module_init(ov5642_init);
module_exit(ov5642_exit);

