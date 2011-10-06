/*
 * ov9665.c - ov9665 sensor driver
 *
 * Copyright (C) 2011 Google Inc.
 *
 * Contributors:
 *      Rebecca Schultz Zavin <rebecca@android.com>
 *
 * Leverage OV9665.c
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
#include "ov9665.h"






struct ov9665_drv {
  struct i2c_client *client;
  struct yuv_sensor_platform_data *pdata;
  int wb;
  int antibanding;
  int i2c_err;
  int sensor_type;
  int i2c_data_width;
} luna_ov9665 = {
  .client       = 0,
  .wb           = YUV_Whitebalance_Auto,
  .antibanding  = YUV_AntiBanding_Auto,
  .i2c_err      = 0,
  .sensor_type  = SensorNonCheck,
  .i2c_data_width = 16, 
};

static const struct ov9665_reg mode_1280x960[] = 
{
    
    {SENSOR_WAIT_MS, 100},
    {0x3e, 0xD0},
    {SENSOR_WAIT_MS, 5},
    {0x3e, 0xD0},
    {SENSOR_WAIT_MS, 5},

    
    {0x12, 0x80},
    {SENSOR_WAIT_MS, 100}, 

    
    {0xd5, 0xff},
    {0xd6, 0x3f},

    
    {0x09,0x01},

    
    {0x3d, 0x3c},
    {0x11, 0x80},
    {0x2a, 0x00},
    {0x2b, 0x00},

    
    {0x3a, 0xd9},
    {0x3b, 0x00},
    {0x3c, 0x58},
    {0x3e, 0x50},
    {0x71, 0x00},

    
    {0x15, 0x00},

    
    {0xd7, 0x10},
    {0x6a, 0x24},
    {0x85, 0xe7},

    
    {0x63, 0x01},

    
    {0x17, 0x0c},
    {0x18, 0x5c},
    {0x19, 0x01},
    {0x1a, 0x82},
    {0x03, 0x0f},
    {0x2b, 0x00},
    {0x32, 0x34},

    
    {0x36, 0xb4},
    {0x65, 0x10},
    {0x70, 0x02},
    {0x71, 0x9c},
    {0x64, 0x24},

    
    {0x43, 0x00},
    {0x5d, 0x55},
    {0x5e, 0x57},
    {0x5f, 0x21},

    
    {0x24, 0x38},
    {0x25, 0x32},
    {0x26, 0x72},

    
    {0x14, 0x28},
    {0x0c, 0x38},
    {0x4f, 0x9e},
    {0x50, 0x84},
    {0x5a, 0x67},

    
    {0x7d, 0x00},
    {0x7e, 0x30},
    {0x7f, 0x00},
    {0x80, 0x05},
    {0x81, 0x06},
    {0x82, 0x05},
    {0x83, 0x07},

    
    {0x96, 0xf0},
    {0x97, 0x00},
    {0x92, 0x3b},
    {0x94, 0x5a},
    {0x93, 0x3a},
    {0x95, 0x48},
    {0x91, 0xfc},
    {0x90, 0x7f},
    {0x8e, 0x4e},
    {0x8f, 0x4e},
    {0x8d, 0x13},
    {0x8c, 0x0c},
    {0x8b, 0x0c},
    {0x86, 0x9e},
    {0x87, 0x11},
    {0x88, 0x22},
    {0x89, 0x05},
    {0x8a, 0x03},

    
    {0x9b, 0x0e},
    {0x9c, 0x1c},
    {0x9d, 0x34},
    {0x9e, 0x5a},
    {0x9f, 0x68},
    {0xa0, 0x76},
    {0xa1, 0x82},
    {0xa2, 0x8e},
    {0xa3, 0x98},
    {0xa4, 0xa0},
    {0xa5, 0xb0},
    {0xa6, 0xbe},
    {0xa7, 0xd2},
    {0xa8, 0xe2},
    {0xa9, 0xee},
    {0xaa, 0x18},

    
    {0xab, 0xe7},
    {0xb0, 0x43},
    {0xac, 0x04},
    {0x84, 0x80},

    
    {0xad, 0x88},
    {0xd9, 0x12},
    {0xda, 0x00},
    {0xae, 0x10},

    
    {0xab, 0xe7},
    {0xb9, 0xa0},
    {0xba, 0x80},
    {0xbb, 0xa0},
    {0xbc, 0x78}, 

    
    
    

    
    {0xbd, 0x08},
    {0xbe, 0x19},
    {0xbf, 0x02},
    {0xc0, 0x08},
    {0xc1, 0x2a},
    {0xc2, 0x33},
    {0xc3, 0x2e},
    {0xc4, 0x1d},
    {0xc5, 0x10},
    {0xc6, 0x98},
    {0xc7, 0x18},
    {0x69, 0x48},

    
    {0x74, 0xc0},

    
    {0x7c, 0x10},
    {0x65, 0x10},
    {0x66, 0x60},
    {0x41, 0xa0},
    {0x5b, 0x34},
    {0x60, 0x80},
    {0x05, 0x06},
    {0x03, 0x13},
    {0xd2, 0x98},

    
    {0xc8, 0x06},
    {0xcb, 0x40},
    {0xcc, 0x40},
    {0xcf, 0x00},
    {0xd0, 0x20},
    {0xd1, 0x00},
    {0xc7, 0x98},

    
    {0x0d, 0x82},
    {0x0d, 0x80},

    
    {0x13, 0xe7},

    


    


    


    
    {SENSOR_WAIT_MS, 200},
    {SENSOR_TABLE_END, 0x0000}
};








static const struct ov9665_reg ov9665_auto_wb_reg[] =
{
    {0x13, 0xe7},
    {SENSOR_TABLE_END, 0x0000}
};
static const struct ov9665_reg ov9665_incandscent_reg[] =
{
    {0x13, 0xe5},
    {0x01, 0x5C}, 
    {0x02, 0x3c},
    {0x16, 0x41},
    {SENSOR_TABLE_END, 0x0000}
};
static const struct ov9665_reg ov9665_fluorescent_reg[] =
{
    {0x13, 0xe5},
    {0x01, 0x4C}, 
    {0x02, 0x52}, 
    {0x16, 0x40},
    {SENSOR_TABLE_END, 0x0000}
};
static const struct ov9665_reg ov9665_sun_light_reg[] =
{
    {0x13, 0xe5},
    {0x01, 0x38}, 
    {0x02, 0x60}, 
    {0x16, 0x40},
    {SENSOR_TABLE_END, 0x0000}
};








static const struct ov9665_reg ov9665_auto_banding_reg[] =
{
    {0x0c, 0x3a},
    {0x56, 0x40},
    {0x57, 0x03},
    {0x59, 0x0e},
    {0xdd, 0x60},
    {0x59, 0x00},
    {0xdd, 0x10},
    {0x59, 0x01},
    {0xdd, 0x18},
    {0x59, 0x0f},
    {0xdd, 0x00},
    {0x59, 0x0b},
    {0xdd, 0x21},
    {0x59, 0x0c},
    {0xdd, 0x0f},
    {0x59, 0x26},
    {0x56, 0x29},
    {SENSOR_TABLE_END, 0x0000}
};
static const struct ov9665_reg ov9665_50Hz_banding_reg[] =
{
    {0x0C, 0x3C},
    {SENSOR_TABLE_END, 0x0000}
};
static const struct ov9665_reg ov9665_60Hz_banding_reg[] =
{
    {0x0C, 0x38},
    {SENSOR_TABLE_END, 0x0000}
};




int ap1040_read_reg(u16 reg, u16 *dat)  
{
  int ret;
  u8 buf_w[2], buf_r[2];
  struct i2c_msg msgs[2];

  if (!luna_ov9665.client || !luna_ov9665.client->adapter)
    return -ENODEV;

  
  
  msgs[0].addr  = AP1040_I2C_ID;
  msgs[0].flags = 0;
  msgs[0].buf   = &buf_w[0];
  msgs[0].len   = 2;
  buf_w[0] =  (u8) (reg >> 8);    
  buf_w[1] =  (u8) (reg & 0xff);
  ret = i2c_transfer(luna_ov9665.client->adapter, &msgs[0], 1);
  if(ret == 1)  
  {
    if(luna_ov9665.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    luna_ov9665.i2c_err = 0;
  }
  else
  {
    luna_ov9665.i2c_err ++;
    if(luna_ov9665.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d (w)",__func__,ret,luna_ov9665.i2c_err);
    return ret;
  }

  
  msgs[1].addr  = AP1040_I2C_ID;
  msgs[1].flags = I2C_M_RD;
  msgs[1].buf   = &buf_r[0];
  if(luna_ov9665.i2c_data_width == 8)
    msgs[1].len = 1;
  else
    msgs[1].len = 2;

  ret = i2c_transfer(luna_ov9665.client->adapter, &msgs[1], 1);
  if(ret == 1)  
  {
    if(luna_ov9665.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    luna_ov9665.i2c_err = 0;
  }
  else
  {
    luna_ov9665.i2c_err ++;
    if(luna_ov9665.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d (r)",__func__,ret,luna_ov9665.i2c_err);
    return ret;
  }
  if(luna_ov9665.i2c_data_width == 8)
    *dat = buf_r[0];
  else
    *dat = (buf_r[0] << 8) + buf_r[1];

  return ret;
}
int ov9665_read_reg(u16 reg, u16 *dat)  
{
  int ret;
  u8 buf_w[2], buf_r[2];
  struct i2c_msg msgs[2];

  if (!luna_ov9665.client || !luna_ov9665.client->adapter)
    return -ENODEV;

  
  msgs[0].addr  = OV9665_I2C_ID;
  msgs[0].flags = 0;
  msgs[0].buf   = &buf_w[0];
  msgs[0].len   = 1;
  buf_w[0] =  (u8) (reg & 0xff);
  ret = i2c_transfer(luna_ov9665.client->adapter, &msgs[0], 1);
  if(ret == 1)  
  {
    if(luna_ov9665.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    luna_ov9665.i2c_err = 0;
  }
  else
  {
    luna_ov9665.i2c_err ++;
    if(luna_ov9665.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d (w)",__func__,ret,luna_ov9665.i2c_err);
    return ret;
  }

  
  msgs[1].addr  = OV9665_I2C_ID;
  msgs[1].flags = I2C_M_RD;
  msgs[1].buf   = &buf_r[0];
  msgs[1].len = 1;
  ret = i2c_transfer(luna_ov9665.client->adapter, &msgs[1], 1);
  if(ret == 1)  
  {
    if(luna_ov9665.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    luna_ov9665.i2c_err = 0;
  }
  else
  {
    luna_ov9665.i2c_err ++;
    if(luna_ov9665.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d (r)",__func__,ret,luna_ov9665.i2c_err);
    return ret;
  }
  *dat = buf_r[0];

  return ret;
}
int ov9665_write_reg(u16 reg, u16 dat)  
{
  int ret;
  u8 buf_w[4];
  struct i2c_msg msg = {
    .addr  = luna_ov9665.client->addr,
    .flags = 0,
    .buf   = &buf_w[0],
  };
  if (!luna_ov9665.client || !luna_ov9665.client->adapter)
    return -ENODEV;

  if(luna_ov9665.sensor_type == SensorOV9665) 
  {
    msg.len   = 2;
    buf_w[0]  = (u8) (reg & 0xff);
    buf_w[1]  = (u8) (dat & 0xff);
  }
  else  
  {
    buf_w[0]  = (u8) (reg >> 8);    
    buf_w[1]  = (u8) (reg & 0xff);
    if(luna_ov9665.i2c_data_width == 8)
    {
      msg.len = 3;
      buf_w[2]= (u8) (dat & 0xff);
    }
    else
    {
      msg.len = 4;
      buf_w[2]= (u8) (dat >> 8);
      buf_w[3]= (u8) (dat & 0xff);
    }
  }
  ret = i2c_transfer(luna_ov9665.client->adapter, &msg, 1);
  if(ret == 1)
  {
    if(luna_ov9665.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    luna_ov9665.i2c_err = 0;
  }
  else
  {
    luna_ov9665.i2c_err ++;
    if(luna_ov9665.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d",__func__,ret,luna_ov9665.i2c_err);
  }

  return ret;
}

int ov9665_write_reg_table(const struct ov9665_reg table[]) 
{
  int ret = 0, i;
  u16 reg, val;

  for(i=0; i<2048; i++) 
  {
    reg = table[i].reg;
    val = table[i].val;
    switch(reg)
    {
      case SENSOR_TABLE_END:
        i = 2048;
        break;
      case SENSOR_WAIT_MS:
        msleep(val);
        break;
      case WRITE_REG_DATA8:
        luna_ov9665.i2c_data_width = 8;
        break;
      case WRITE_REG_DATA16:
        luna_ov9665.i2c_data_width = 16;
        break;
      default:
        ret = ov9665_write_reg(reg, val);
        
        break;
    }
  }
  return ret;
}
static int ov9665_set_white_balance(int wb) 
{
  int ret;
  if(luna_ov9665.sensor_type == SensorAptina1040)
  {
    ret = ap1040_set_white_balance(wb);
  }
  else
  {
    switch(wb)
    {
      case YUV_Whitebalance_Auto:
        ret = ov9665_write_reg_table(ov9665_auto_wb_reg);
        MSG2("%s: Auto", __func__);
        break;
      case YUV_Whitebalance_Incandescent:
        ret = ov9665_write_reg_table(ov9665_incandscent_reg);
        MSG2("%s: Incandescent", __func__);
        break;
      case YUV_Whitebalance_Fluorescent:
        ret = ov9665_write_reg_table(ov9665_fluorescent_reg);
        MSG2("%s: Fluorescent", __func__);
        break;
      case YUV_Whitebalance_Daylight:
        ret = ov9665_write_reg_table(ov9665_sun_light_reg);
        MSG2("%s: Daylight", __func__);
        break;
      default:
        MSG2("%s: Invalid = %d", __func__,wb);
        ret = -1;
        break;
    }
  }
  if(ret == 1)
    luna_ov9665.wb = wb;
  return ret;
}
static int ov9665_set_anti_banding(int banding) 
{
  int ret;
  if(luna_ov9665.sensor_type == SensorAptina1040)
  {
    ret = ap1040_set_anti_banding(banding);
  }
  else
  {
    switch(banding)
    {
      case YUV_AntiBanding_Auto:
        ret = ov9665_write_reg_table(ov9665_auto_banding_reg);
        MSG2("%s: Auto", __func__);
        break;
      case YUV_AntiBanding_50Hz:
        ret = ov9665_write_reg_table(ov9665_50Hz_banding_reg);
        MSG2("%s: 50Hz", __func__);
        break;
      case YUV_AntiBanding_60Hz:
        ret = ov9665_write_reg_table(ov9665_60Hz_banding_reg);
        MSG2("%s: 60Hz", __func__);
        break;
      default:
        MSG2("%s: Invalid = %d", __func__,banding);
        ret = -1;
        break;
    }
  }
  if(ret == 1)
    luna_ov9665.antibanding = banding;
  return ret;
}
static int ov9665_set_mode(struct sensor_mode *mode)  
{
  int ret;

  if(luna_ov9665.sensor_type == SensorAptina1040)
  {
    ret = ap1040_set_mode(mode);
  }
  else
  {
    MSG2("%s+, %dx%d",__func__,mode->xres,mode->yres);
    ret = ov9665_write_reg_table(mode_1280x960);
  }
  MSG2("%s-",__func__);
  return ret;
}
static int ov9665_check_sensor_type(void) 
{
  int ret;
  u16 data;

  
  luna_ov9665.i2c_data_width = 16;
  ret = ap1040_read_reg(0x0000, &data); 
  if(ret == 1)
  {
    luna_ov9665.sensor_type   = SensorAptina1040;
    luna_ov9665.client->addr  = AP1040_I2C_ID;
    MSG2("%s, AP1040 Found, 0x0000 = 0x%04X",__func__,data);
    return 1;
  }
  
  
  luna_ov9665.i2c_data_width  = 8;
  ret = ov9665_read_reg(0x1C, &data); 
  if(ret == 1)
  {
    luna_ov9665.sensor_type   = SensorOV9665;
    luna_ov9665.client->addr  = OV9665_I2C_ID;
    MSG2("%s, OV9665 Found, 0x1C = 0x%02X",__func__,data);
    return 1;
  }

  
  luna_ov9665.sensor_type     = SensorNonCheck;
  MSG2("%s, No Sensor Found",__func__);
  return -1;
}



static ssize_t ov9665_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  switch(luna_ov9665.sensor_type)
  {
    case SensorOV9665:
      buf[0] = '9';
      buf[1] = '6';
      buf[2] = '6';
      buf[3] = '5';
      buf[4] = '\0';
      break;
    case SensorAptina1040:
      buf[0] = '1';
      buf[1] = '0';
      buf[2] = '4';
      buf[3] = '0';
      buf[4] = '\0';
      break;
    case SensorNonCheck:
    default:
      buf[0] = '0';
      buf[1] = '0';
      buf[2] = '0';
      buf[3] = '0';
      buf[4] = '\0';
      break;
  }
  return 5;
}
static struct device_attribute ov9665_id_attrs[] = {
  __ATTR(id, 0444, ov9665_id_show, NULL),
};




static long ov9665_ioctl(struct file *file,   
       unsigned int cmd, unsigned long arg)
{
  int ret;
  switch(cmd)
  {
    case SENSOR_IOCTL_SET_MODE:
      {
        struct sensor_mode mode;
        if(copy_from_user(&mode, (const void __user *)arg, sizeof(struct sensor_mode)) )
        {
          return -EFAULT;
        }
        ret = ov9665_set_mode(&mode); 
      }
      break;
    case SENSOR_IOCTL_SET_WHITE_BALANCE:
      {
        int wb = (u8)arg;
        ret = ov9665_set_white_balance(wb); 
      }
      break;
    case SENSOR_IOCTL_SET_ANTI_BANDING:
      {
        int banding = (u8)arg;
        ret = ov9665_set_anti_banding(banding); 
      }
      break;

    default:
      return -EINVAL;
  }
  return 0;
}

static int ov9665_open(struct inode *inode, struct file *file)
{
  int ret = 0;
  MSG2("%s+",__func__);

	if(luna_ov9665.pdata->power_on)
		luna_ov9665.pdata->power_on();
  
  msleep(20);

  
  if(luna_ov9665.sensor_type == SensorNonCheck)
    ret = ov9665_check_sensor_type();
  else
    ret = 1;
  MSG2("%s-, sensor type = %s",__func__,
    luna_ov9665.sensor_type == SensorNonCheck   ? "NonCheck" :
    luna_ov9665.sensor_type == SensorOV9665     ? "OV9665" :
    luna_ov9665.sensor_type == SensorAptina1040 ? "Ap1040" : "Invalid");
  if(ret == 1)
    return 0;
  else
    return -EIO; 
}
int ov9665_release(struct inode *inode, struct file *file)
{
  MSG2("%s+",__func__);
  
	if(luna_ov9665.pdata->power_off)
		luna_ov9665.pdata->power_off();
  file->private_data = NULL;
  MSG2("%s-",__func__);
  return 0;
}
static const struct file_operations ov9665_fileops = {
  .owner    = THIS_MODULE,
  .unlocked_ioctl = ov9665_ioctl,
  .open     = ov9665_open,
  .release  = ov9665_release,
};
static struct miscdevice ov9665_device = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "ov9665",
  .fops = &ov9665_fileops,
};



static int ov9665_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  int ret;

  MSG2("%s+",__func__);
  
  luna_ov9665.client = client;
  
  ret = misc_register(&ov9665_device);
  if(ret) MSG2("%s, misc_register fail = %d",__func__,ret);
  
  ret = device_create_file(ov9665_device.this_device, &ov9665_id_attrs[0]);
  if(ret) {MSG2("create %s FAIL, ret=%d",ov9665_id_attrs[0].attr.name,ret);}
  else    {MSG2("create /sys/devices/virtual/misc/ov9665/%s",ov9665_id_attrs[0].attr.name);}
  
  luna_ov9665.pdata = client->dev.platform_data;
  
  i2c_set_clientdata(client, &luna_ov9665);
  MSG2("%s-",__func__);
  return 0;
}

static const struct i2c_device_id ov9665_id[] = {
  { "ov9665", 0 },
  { },
};
static struct i2c_driver ov9665_i2c_driver = {
  .driver   = {
    .name   = "ov9665",
    .owner  = THIS_MODULE,
  },
  .id_table = ov9665_id,
  .probe    = ov9665_i2c_probe,
};

static int __init ov9665_init(void)
{
  int ret;

  printk("BootLog, +%s+\n", __func__);
  ret = i2c_add_driver(&ov9665_i2c_driver);
  printk("BootLog, -%s-, ret=%d\n", __func__, ret);
  return ret;
}
module_init(ov9665_init);


