




















#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <mach/gpio.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/wakelock.h>
#include <linux/spinlock.h>
#include <../arch/arm/mach-tegra/gpio-names.h>

#include "luna_lsensor.h"

static int lsensor_log_on = 0;
static int lsensor_log3_on = 0;



#define MSG(format, arg...) {if(lsensor_log_on)  printk(KERN_INFO "[ALS]" format "\n", ## arg);}
#define MSG2(format, arg...) {printk(KERN_INFO "[ALS]" format "\n", ## arg);} 
#define MSG3(format, arg...) {if(lsensor_log3_on)  printk(KERN_INFO "[ALS]" format "\n", ## arg);}

static DEFINE_MUTEX(lsensor_enable_lock);
#ifdef LSENSOR_USE_INTERRUPT
  static DEFINE_SPINLOCK(lsensor_irq_lock);
#endif


static int lsensor_k_1000_lux = 1;


static struct lsensor_info_data ls_info;
static struct lsensor_drv_data  ls_drv =
{
  .i2c_err = 0,
  .client = NULL,
  .input  = NULL,
  .m_ga = 8847,  
  .lux_history = {420,420,420},
  .bkl_idx = (LSENSOR_BKL_TABLE_SIZE>>1), 
  .bkl_table = {  
    {30,   0,     31,   0   },  
    {45,   25,    44,   5   },
    {60,   38,    58,   13  },
    {75,   51,    75,   32  },
    {90,   65,    95,   45  },
    {105,  85,    126,  58  },
    {120,  106,   180,  75  },
    {135,  146,   250,  96  },
    {150,  221,   350,  126 },
    {165,  296,   480,  190 },
    {180,  420,   600,  260 },
    {195,  545,   730,  360 },
    {210,  671,   860,  490 },
    {225,  797,   990,  610 },
    {240,  924,   1130, 740 },
    {255,  1075,  1200, 860 },  
    },
  .als_nv  = 55,  
  .jiff_polling_interval = HZ/2,
};
static struct lsensor_eng_data  ls_eng;
static struct lsensor_reg_data  ls_reg =
{
  .r00.bit.pon = 0,
  .r00.bit.aen = 0,
  .atime = 108, 
  .r02.bit.pers = 0,  
  .r02.bit.mode = 1,  
  .r02.bit.stop = 1,  
  
  
  
  
  .low  = {0x00, 0x00},
  .high = {0xFF, 0xFF},
  .again = 2,   
};


static struct delayed_work ls_work;
static struct workqueue_struct *ls_wqueue;




static int lsensor_read_i2c(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  struct i2c_msg msgs[] = {
    [0] = {
      .addr   = addr,
      .flags  = 0,
      .buf    = (void *)&reg,
      .len    = 1
    },
    [1] = {
      .addr   = addr,
      .flags  = I2C_M_RD,
      .buf    = (void *)buf,
      .len    = len
    }
  };
  int ret;
  if(!ls_drv.client)
    return -ENODEV;

  if(addr == ls_drv.i2c_addr) 
  {
    if(len == 1)        reg = reg | 0x80;
    else if(len == 2)   reg = reg | 0x80 | 0x20;
    else if(len > 2)    reg = reg | 0x80 | 0x40;
    
  }

  ret = i2c_transfer(ls_drv.client->adapter, msgs, 2);
  if(ret == 2)
  {
    if(ls_drv.i2c_err)
      MSG2("%s, ret = 2, i2c_err = 0",__func__);
    ls_drv.i2c_err = 0;
  }
  else
  {
    ls_drv.i2c_err ++;
    if(ls_drv.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d",__func__,ret,ls_drv.i2c_err);
  }
  return ret;
}
static int lsensor_read_i2c_retry(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  int i,ret;
  for(i=0; i<5; i++)
  {
    ret = lsensor_read_i2c(addr,reg,buf,len);
    if(ret == 2)
      return ret;
    else
      msleep(10);
  }
  return ret;
}
static int lsensor_write_i2c(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  int i, ret;
  unsigned char buf_w[64];
  struct i2c_msg msgs[] = {
    [0] = {
      .addr   = addr,
      .flags  = 0,
      .buf    = (void *)buf_w,
      .len    = len+1
    }
  };

  if(len >= sizeof(buf_w))  
    return -ENOMEM;
  if(!ls_drv.client)
    return -ENODEV;

  if(addr == ls_drv.i2c_addr) 
  {
    if(len == 1)        reg = reg | 0x80;
    else if(len == 2)   reg = reg | 0x80 | 0x20;
    else if(len > 2)    reg = reg | 0x80 | 0x40;
    
  }

  buf_w[0] = reg;
  for(i=0; i<len; i++)
    buf_w[i+1] = buf[i];
  ret = i2c_transfer(ls_drv.client->adapter, msgs, 1);

  if(ret == 1)
  {
    if(ls_drv.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    ls_drv.i2c_err = 0;
  }
  else
  {
    ls_drv.i2c_err ++;
    if(ls_drv.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d",__func__,ret,ls_drv.i2c_err);
  }
  return ret;
}
static int lsensor_write_i2c_retry(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  int i,ret;
  for(i=0; i<5; i++)
  {
    ret = lsensor_write_i2c(addr,reg,buf,len);
    if(ret == 1)
      return ret;
    else
      msleep(10);
  }
  return ret;
}
static int lsensor_clr_irq(void)
{
  unsigned char buf = 0x80 | 0x60 | 0x01;  
  struct i2c_msg msgs[] = {
    [0] = {
      .addr   = ls_drv.i2c_addr,
      .flags  = 0,
      .buf    = &buf,
      .len    = 1
    }
  };
  int ret;

  if(!ls_drv.client)
    return -ENODEV;

  ret = i2c_transfer(ls_drv.client->adapter, msgs, 1);
  if(ret == 1)
  {
    if(ls_drv.i2c_err)
      MSG2("%s, ret = 1, i2c_err = 0",__func__);
    ls_drv.i2c_err = 0;
  }
  else
  {
    ls_drv.i2c_err ++;
    if(ls_drv.i2c_err < 20)
      MSG2("%s, ret = %d, i2c_err = %d",__func__,ret,ls_drv.i2c_err);
  }
  return ret;  
}
static int lsensor_i2c3_suspend(struct i2c_client *client, pm_message_t state)
{
  MSG("%s+", __func__);
  
  ls_drv.in_suspend = 1;
  MSG("%s-", __func__);
  return 0;
}
static int lsensor_i2c3_resume(struct i2c_client *client)
{
  MSG("%s+", __func__);
  ls_drv.in_suspend = 0;
  MSG("%s-", __func__);
  return 0;
}
void lsensor_i2c3_shutdown(struct i2c_client *client)
{
  MSG("%s", __func__);
  ls_reg.r00.bit.pon  = 0;
  ls_reg.r00.bit.aen  = 0;
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
  lsensor_clr_irq();
}
static int __devinit lsensor_i2c3_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  MSG2("%s+",__func__);
  ls_drv.client = client;
  MSG2("%s-",__func__);
  return 0;
}
static struct i2c_device_id lsensor_i2c3_id[] = { { "luna_lsensor_i2c3", 0 } };
static struct i2c_driver lsensor_i2c3_driver = {
  .driver.owner = THIS_MODULE,
  .driver.name  = "luna_lsensor_i2c3",
  .id_table = lsensor_i2c3_id,
  .suspend  = lsensor_i2c3_suspend,
  .resume   = lsensor_i2c3_resume,
  .shutdown = lsensor_i2c3_shutdown,
  .probe    = lsensor_i2c3_probe,
};




#ifdef LSENSOR_USE_INTERRUPT
  static inline void lsensor_irq_onOff(unsigned int onOff)
  {
    unsigned long flags;
    spin_lock_irqsave(&lsensor_irq_lock, flags);
    if(onOff)
    {
      if(! ls_drv.irq_enabled)
      {
        ls_drv.irq_enabled = 1;
        
      }
    }
    else
    {
      if(ls_drv.irq_enabled)
      {
        
        ls_drv.irq_enabled = 0;
      }
    }
    spin_unlock_irqrestore(&lsensor_irq_lock, flags);
  }
#endif


static void lsensor_get_lux(void)
{
  const unsigned int gain_100_table[4] = {100, 800, 1600, 11100}; 
  const unsigned int coef[4][2] = 
  {
    {5200, 9600},   
    {6596, 14248},  
    {3896, 7144},   
    {2480, 4000},   
  };
  unsigned int raw_clear  = ls_info.cdata;
  unsigned int raw_ir     = ls_info.irdata;
  unsigned int saturation;
  unsigned int lux, gain_100;
  unsigned int ratio_1024, idx;

  saturation = (256 - ls_reg.atime)*1024;
  if(saturation > 65535)
    saturation = 65535;

  if(raw_clear >= saturation)
     goto lux_max;
  else if(!raw_clear) 
    goto lux_zero;

  
  ratio_1024 = (raw_ir * 1024) / raw_clear;
  if(ratio_1024 <= 307)       
    idx= 0;
  else if(ratio_1024 <= 389)  
    idx= 1;
  else if(ratio_1024 <= 460)  
    idx= 2;
  else if(ratio_1024 <= 552)  
    idx= 3;
  else                        
    goto lux_max;

  
  gain_100 = gain_100_table[ls_reg.again & 0x3];
  lux = coef[idx][0] * raw_clear - coef[idx][1] * raw_ir;
  lux = (lux / 1024) * ls_drv.m_ga / (ls_info.a_ms * gain_100);

  ls_info.m_irf = ratio_1024;
  ls_info.m_lux = lux;
  return;

lux_zero:
  ls_info.m_irf = 0;
  ls_info.m_lux = 0;
  return;
lux_max:
  ls_info.m_irf = TAOS_MAX_LUX;
  ls_info.m_lux = TAOS_MAX_LUX;
  return;
}

static void lsensor_update_result(void)
{
  static u8 middle[] = {1,0,2,0,0,2,0,1};
  int index;

  
  ls_drv.lux_history[2] = ls_drv.lux_history[1];
  ls_drv.lux_history[1] = ls_drv.lux_history[0];
  ls_drv.lux_history[0] = ls_info.m_lux;
  {
    index = 0;
    if( ls_drv.lux_history[0] > ls_drv.lux_history[1] ) index += 4;
    if( ls_drv.lux_history[1] > ls_drv.lux_history[2] ) index += 2;
    if( ls_drv.lux_history[0] > ls_drv.lux_history[2] ) index ++;
    ls_drv.millilux = ls_drv.lux_history[middle[index]];
  }
}


extern void luna_als_update_backlight(int brightness_als);

static void lsenosr_set_backlight(int level)
{
  luna_als_update_backlight(level);
  MSG3("%s = %d", __func__, level);
}
static void lsensor_bkl_fading(int start, int end)
{
  int dist, stepSize, level, sleep, step, i;
  unsigned long jiff_start = jiffies;

  MSG("%s+ (start=%d, end=%d)", __func__, start, end);

  dist = start >= end ? (start-end) : (end - start);
  if(dist <= 15)
  {
    stepSize = 6;
    sleep = 10;
  }
  else
  {
    stepSize = 12;
    sleep = 5;
  }
  if(start > end)
    step = - (start - end)*1024/stepSize;
  else
    step = (end - start)*1024/stepSize;
  for(i=0; i<(stepSize-1); i++)
  {
    level = (start*1024 + step*i)/1024;
    if(level > 255)     level = 255;
    else if(level < 0)  level = 0;
    lsenosr_set_backlight(level);
    msleep(sleep);
  }
  if(i == (stepSize-1))
  {
    lsenosr_set_backlight(end);
  }
  MSG("%s- (%d ms)", __func__, jiffies_to_msecs(jiffies- jiff_start));
}
static void lsenosr_update_bkl(void)
{
  int high, low, dd;
  unsigned int i, wait;

  MSG("%s",__func__);

  if(ls_drv.millilux == TAOS_ACCESS_FAIL)
  {
    
  }

  if(time_before(jiffies, ls_drv.jiff_update_bkl_wait_time)) 
    return;

  if(ls_drv.bkl_idx > (LSENSOR_BKL_TABLE_SIZE-1))
  {
    MSG2("ERROR: %s bkl_idx=%d (OVERFLOW!)",__func__,ls_drv.bkl_idx);
    ls_drv.bkl_idx = (LSENSOR_BKL_TABLE_SIZE-1);
  }
  high = ls_drv.bkl_table[ls_drv.bkl_idx].high;
  low  = ls_drv.bkl_table[ls_drv.bkl_idx].low;
  if(ls_drv.millilux <= high && ls_drv.millilux >= low)
  {
    
  }
  else if(ls_drv.millilux > high) 
  {
    if(ls_drv.bkl_idx < (LSENSOR_BKL_TABLE_SIZE-1))
      ls_drv.bkl_idx ++;
  }
  else if(ls_drv.millilux < low) 
  {
    if(ls_drv.bkl_idx > 0)
      ls_drv.bkl_idx --;
  }

  ls_info.status = ls_drv.bkl_idx;
  if(ls_drv.bkl_idx_old != ls_drv.bkl_idx)
  {
    
    for(i=LSENSOR_BKL_TABLE_SIZE-1; i>0; i--)
    {
      if(ls_drv.millilux > ls_drv.bkl_table[i].now)
        break;
    }
    if(i > ls_drv.bkl_idx)
    {
      dd = i - ls_drv.bkl_idx;
      wait = dd > 4? 200  :
             dd > 3? 400  :
             dd > 2? 800  : 1600;
    }
    else
    {
      dd = ls_drv.bkl_idx - i;
      wait = dd > 4? 400  :
             dd > 3? 800  :
             dd > 2? 1600 : 3200;
    }
    MSG3("idx=%02d i=%02d, now=%04d, lux=%04d, wait=%04d", ls_drv.bkl_idx, i, ls_drv.bkl_table[ls_drv.bkl_idx].now, ls_drv.millilux, wait);

    
    if(ls_drv.enable & LSENSOR_EN_LCD)
      lsensor_bkl_fading(ls_drv.bkl_table[ls_drv.bkl_idx_old].level, ls_drv.bkl_table[ls_drv.bkl_idx].level);

    
    if(time_before(jiffies, ls_drv.jiff_resume_fast_update_time)) 
      ls_drv.jiff_update_bkl_wait_time = jiffies;
    else
    ls_drv.jiff_update_bkl_wait_time = jiffies + HZ*wait/1024;
  }
  ls_drv.bkl_idx_old = ls_drv.bkl_idx;
}

static void lsensor_eng_set_reg(void)
{

  ls_reg.r00.bit.pon  = ls_eng.pon ? 1:0;
  ls_reg.r00.bit.aen  = ls_eng.aen ? 1:0;
  ls_reg.atime = ls_eng.atime;
  ls_reg.again = ls_eng.again & 0x03;
  ls_drv.m_ga = ls_eng.m_ga;
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,    sizeof(ls_reg.atime));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,    sizeof(ls_reg.again));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
  
  ls_info.a_ms = (256 - ls_reg.atime)*2785/1024;
  if(ls_info.a_ms <= 450) 
    ls_drv.jiff_polling_interval = HZ / 2;
  else                    
    ls_drv.jiff_polling_interval = HZ * 4 / 5;
}
static void lsensor_eng_get_reg(void)
{
  lsensor_read_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));
  lsensor_read_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,     sizeof(ls_reg.atime));
  lsensor_read_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,     sizeof(ls_reg.again));
  ls_eng.pon    = ls_reg.r00.bit.pon;
  ls_eng.aen    = ls_reg.r00.bit.aen;
  ls_eng.atime  = ls_reg.atime;
  ls_eng.again  = ls_reg.again;
  ls_eng.m_ga   = ls_drv.m_ga;
}





static void lsensor_calibration(struct lsensor_cal_data* p_cal_data)
{
  unsigned int m_ga_old;
  int i, lux_count;
  unsigned int lux_average, lux_sum;
  unsigned int lux[LSENSOR_CALIBRATION_LOOP];
  unsigned int lux_high, lux_low;
  struct lsensor_reg_data  ls_reg_old;
  int work_pending;

  MSG2("%s+", __func__);

  
  p_cal_data->status = 0;

  
  
  m_ga_old = ls_drv.m_ga;
  memcpy(&ls_reg_old, &ls_reg, sizeof(ls_reg));

  
  
  work_pending = cancel_delayed_work_sync(&ls_work);

  
  
  ls_reg.r00.bit.pon = 0;
  ls_reg.r00.bit.aen = 0;
  ls_reg.r00.bit.valid = 0;
  ls_reg.r00.bit.intr = 0;
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
  lsensor_clr_irq();

  
  
  ls_reg.r00.bit.pon = 1;
  ls_reg.r00.bit.aen = 0;
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
  
  ls_reg.atime = 108;       
  ls_reg.r02.bit.pers = 0;  
  ls_reg.r02.bit.mode = 1;  
  ls_reg.r02.bit.stop = 1;  
  ls_reg.low[0]  = 0x00;
  ls_reg.low[1]  = 0x00;
  ls_reg.high[0] = 0xFF;
  ls_reg.high[1] = 0xFF;
  ls_reg.again = 2,   
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,    sizeof(ls_reg.atime));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x02,&ls_reg.r02.byte, sizeof(ls_reg.r02.byte));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x03,&ls_reg.low[0],   sizeof(ls_reg.low));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x05,&ls_reg.high[0],  sizeof(ls_reg.high));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,    sizeof(ls_reg.again));
  msleep(2);
  lsensor_clr_irq();
  ls_reg.r00.bit.pon = 1;
  ls_reg.r00.bit.aen = 1;
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
  
  ls_info.a_ms = (256 - ls_reg.atime)*2785/1024;
  if(ls_info.a_ms <= 450) 
    ls_drv.jiff_polling_interval = HZ / 2;
  else                    
    ls_drv.jiff_polling_interval = HZ * 4 / 5;

  
  ls_drv.m_ga = 1024;

  
  
  lux_average = 0;
  lux_high  = 0;
  lux_low   = 0xFFFFFFFF;
  for(i=0; i<LSENSOR_CALIBRATION_LOOP; i++)
  {
    
    msleep(ls_info.a_ms + 40);

    
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,sizeof(&ls_reg.r00.byte));
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x14,&ls_reg.cdata[0],sizeof(&ls_reg.cdata));
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x16,&ls_reg.irdata[0],sizeof(&ls_reg.irdata));

    
    ls_info.cdata   = ls_reg.cdata[0]  + (ls_reg.cdata[1]<<8);
    ls_info.irdata  = ls_reg.irdata[0] + (ls_reg.irdata[1]<<8);
    ls_info.a_ms    = (256 - ls_reg.atime)*2785/1024;
    lsensor_get_lux();

    
    ls_reg.r00.bit.pon = 1;
    ls_reg.r00.bit.aen = 0;
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

    
    lsensor_clr_irq();

    
    if((ls_info.cdata > 50000) && ((ls_reg.again & 0x3) != 0))      
    {
      ls_reg.again = 0;
      lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
    }
    else if((ls_info.cdata < 2000) && ((ls_reg.again & 0x3) != 2))  
    {
      ls_reg.again = 2;
      lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
    }
    MSG2("%02d [L]%05d (C)%05d (R)%05d (G)%dX", i, ls_info.m_lux, ls_info.cdata, ls_info.irdata, ls_reg.again?16:1);

    
    ls_reg.r00.bit.pon = 1;
    ls_reg.r00.bit.aen = 1;
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

    
    lux[i]  = ls_info.m_lux;
    lux_average  += ls_info.m_lux;
    if(ls_info.m_lux >= lux_high)   lux_high = ls_info.m_lux;
    if(ls_info.m_lux <= lux_low)    lux_low  = ls_info.m_lux;
  }
  lux_average  /= LSENSOR_CALIBRATION_LOOP;
  MSG2("Lux= %d~%d~%d (Low~Average~High)",
    lux_low, lux_average, lux_high);

  
  
  lux_count   = 0;
  lux_sum     = 0;
  for(i=0; i<LSENSOR_CALIBRATION_LOOP; i++)
  {
    if(lux[i] != lux_high && lux[i] != lux_low) 
    {
      lux_count ++;
      lux_sum += lux[i];
    }
  }
  if(lux_count)
  {
    lux_sum /= lux_count;
  }
  else
  {
    MSG2("Lux have no middle value");
    lux_sum = lux_average;
  } 
  MSG2("Calibration ALS = %d, count = %d",
    lux_sum, lux_count);

  
  
  
  SET_BIT(p_cal_data->status,0);  
  if(lsensor_k_1000_lux)
  {
    MSG2("Modify for 1000 Lux test = %d", lux_sum/2);
    p_cal_data->als = lux_sum/2;
  }
  else
  {
    MSG2("Modify for 500 Lux test = %d", lux_sum);
    p_cal_data->als = lux_sum;
  }

  
  
  ls_drv.m_ga = m_ga_old;
  memcpy(&ls_reg, &ls_reg_old, sizeof(ls_reg));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,    sizeof(ls_reg.atime));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x02,&ls_reg.r02.byte, sizeof(ls_reg.r02.byte));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x03,&ls_reg.low[0],   sizeof(ls_reg.low));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x05,&ls_reg.high[0],  sizeof(ls_reg.high));
  lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,    sizeof(ls_reg.again));

  
  
  MSG2("work_pending = %d",work_pending);
  if(work_pending)
  {
    queue_delayed_work(ls_wqueue, &ls_work, ls_drv.jiff_polling_interval);
  }

  MSG2("%s-", __func__);
}
static void lsensor_update_equation_parameter(struct lsensor_cal_data *p_cal_data)
{
  unsigned int als;

  if(p_cal_data->als < 200 && p_cal_data->als > 20) 
  {
    als = p_cal_data->als;
  }
  else
  {
    als = 55;   
  }
  
  {
    ls_drv.m_ga = 500*1024/als; 
    MSG2("%s: m_ga = %d (update_ga = %d)",__func__, ls_drv.m_ga, p_cal_data->als);
  }
}




static void lsensor_work_func(struct work_struct *work)
{
  MSG("%s+", __func__);

  
  
  if(!ls_drv.inited)
  {
    MSG2("%s, not inited!",__func__);
    return;
  }

  
  
  if(ls_drv.in_suspend)
  {
    MSG2("%s, driver in_suspend",__func__);
    goto exit;
    
  }

  
  
  if(lsensor_read_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,sizeof(&ls_reg.r00.byte)) == 2 &&
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again)) == 2 &&
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x14,&ls_reg.cdata[0],sizeof(&ls_reg.cdata)) == 2 &&
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x16,&ls_reg.irdata[0],sizeof(&ls_reg.irdata)) )
  {
    MSG("%s, (C)%05d, (R)%05d, (00)%02X, (G)%dX",__func__,
      ls_reg.cdata[0] + (ls_reg.cdata[1]<<8), ls_reg.irdata[0] + (ls_reg.irdata[1]<<8), ls_reg.r00.byte, ls_reg.again?16:1);
  }
  else
  {
    ls_reg.cdata[0] = 0;
    ls_reg.cdata[1] = 0;
    ls_reg.irdata[0] = 0;
    ls_reg.irdata[1] = 0;
    MSG2("%s, read reg Fail! i2c_err = %d",__func__,ls_drv.i2c_err);
  }

  
  
  if(ls_drv.enable & LSENSOR_EN_ENG)
  {
    if(ls_drv.i2c_err)
    {
      ls_info.m_irf = TAOS_ACCESS_FAIL;
      ls_info.m_lux = TAOS_ACCESS_FAIL;
      MSG3("(C)00000 (R)00000 [L]0xFFFFFFFF");
    }
    else if(ls_eng.pon)
    {
      
      if(ls_reg.r00.bit.valid || ls_reg.r00.bit.intr)
      {
        MSG("%s, R00 = 01",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen = 0;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

        lsensor_clr_irq();

        MSG("%s, R00 = 03",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen  = ls_eng.aen ? 1:0;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

        ls_info.cdata   = ls_reg.cdata[0]  + (ls_reg.cdata[1]<<8);
        ls_info.irdata  = ls_reg.irdata[0] + (ls_reg.irdata[1]<<8);
        ls_info.a_ms    = (256 - ls_reg.atime)*2785/1024;

        lsensor_get_lux();        
        lsensor_update_result();  

        
        if((ls_info.cdata > 50000) && ((ls_reg.again & 0x3) != 0))       
        {
          ls_reg.again = 0;
          lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
          
        }
        else if((ls_info.cdata < 2000) && ((ls_reg.again & 0x3) != 2))  
        {
          ls_reg.again = 2;
          lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
          
        }
        MSG3("(C)%05d (R)%05d [L]%05d (G)%dX", ls_info.cdata, ls_info.irdata, ls_info.m_lux, ls_reg.again?16:1);
        lsenosr_update_bkl();     
      }
    }
    queue_delayed_work(ls_wqueue, &ls_work, ls_drv.jiff_polling_interval);
    goto exit;
  }

  
  
  if(!ls_drv.enable || ls_drv.in_early_suspend)
  {
    if(ls_drv.i2c_err)
    {
      
    }
    
    else if(ls_reg.r00.bit.pon) 
    {
      
      MSG("%s, R00 = 00",__func__);
      ls_reg.r00.bit.pon = 0;
      ls_reg.r00.bit.aen = 0;
      ls_reg.r00.bit.valid = 0;
      ls_reg.r00.bit.intr = 0;
      lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));
    }
    goto exit;
  }

  
  
  else if(ls_drv.enable)
  {
    if(ls_drv.i2c_err)
    {
      ls_info.m_irf = TAOS_ACCESS_FAIL;
      ls_info.m_lux = TAOS_ACCESS_FAIL;
      MSG3("(C)00000 (R)00000 [L]0xFFFFFFFF");
    }
    else
    {
      
      if(!ls_reg.r00.bit.pon)  
      {
        MSG("%s, R00 = 01",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen = 0;
        ls_reg.r00.bit.valid = 0;
        ls_reg.r00.bit.intr = 0;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));
        msleep(2);

        MSG("%s, R00 = 03",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen = 1;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));
      }
      
      if(ls_reg.r00.bit.valid || ls_reg.r00.bit.intr)
      {
        MSG("%s, R00 = 01",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen = 0;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

        lsensor_clr_irq();

        MSG("%s, R00 = 03",__func__);
        ls_reg.r00.bit.pon = 1;
        ls_reg.r00.bit.aen = 1;
        lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte,  sizeof(ls_reg.r00.byte));

        ls_info.cdata   = ls_reg.cdata[0]  + (ls_reg.cdata[1]<<8);
        ls_info.irdata  = ls_reg.irdata[0] + (ls_reg.irdata[1]<<8);
        ls_info.a_ms    = (256 - ls_reg.atime)*2785/1024;

        lsensor_get_lux();        
        lsensor_update_result();  

        
        if(ls_drv.enable & LSENSOR_EN_AP) 
        {
          if(ls_info.m_lux == TAOS_MAX_LUX)
          {
            input_event(ls_drv.input, EV_MSC, MSC_RAW, 4000);
          	input_sync(ls_drv.input);
          }
          else if(ls_info.m_lux == TAOS_ACCESS_FAIL)
          {
            input_event(ls_drv.input, EV_MSC, MSC_RAW, 800);
          	input_sync(ls_drv.input);
          }
          else
          {
            if(ls_info.m_lux > 4000)
              input_event(ls_drv.input, EV_MSC, MSC_RAW, 4000);
            else
              input_event(ls_drv.input, EV_MSC, MSC_RAW, ls_info.m_lux);
          	input_sync(ls_drv.input);
          }
        }

        
        if((ls_info.cdata > 50000) && ((ls_reg.again & 0x3) != 0))      
        {
          ls_reg.again = 0;
          lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
          
        }
        else if((ls_info.cdata < 2000) && ((ls_reg.again & 0x3) != 2))  
        {
          ls_reg.again = 2;
          lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,sizeof(ls_reg.again));
          
        }
        MSG3("(C)%05d (R)%05d [L]%05d (G)%dX", ls_info.cdata, ls_info.irdata, ls_info.m_lux, ls_reg.again?16:1);
      }
      lsenosr_update_bkl();     
    }
    queue_delayed_work(ls_wqueue, &ls_work, ls_drv.jiff_polling_interval);
  }

exit:
  if(ls_drv.info_waiting_als)
  {
    ls_drv.info_waiting_als = 0;
    complete(&(ls_drv.info_comp_als));
  }
  if(ls_drv.info_waiting)
  {
    ls_drv.info_waiting = 0;
    complete(&(ls_drv.info_comp));
  }
  MSG("%s-", __func__);
}




#ifdef LSENSOR_USE_INTERRUPT
  static irqreturn_t lsensor_irqhandler(int irq, void *args)
  {
    MSG2("%s", __func__);
    if(!ls_drv.inited)
    {
      MSG2("%s, lsensor not inited! Disable irq!", __func__);
      
    }
    else
    {
      lsensor_irq_onOff(0);
      
      
    }
    
  }
#endif




void lsensor_enable_onOff(unsigned int mode, unsigned int onOff)  
{
  

  MSG("%s+ mode=0x%02X onOff=0x%02X", __func__,mode,onOff);
  mutex_lock(&lsensor_enable_lock);

  
  
  if(mode==LSENSOR_EN_AP || mode==LSENSOR_EN_LCD || mode==LSENSOR_EN_ENG)
  {
    if(onOff) ls_drv.enable |=   mode;
    else      ls_drv.enable &= (~mode);
  }

  
  
  if(mode == LSENSOR_EN_ENG)
  {
    if(onOff)
    {
      MSG("%s, ENG MODE: Workqueu Start!",__func__);
      queue_delayed_work(ls_wqueue, &ls_work, ls_drv.jiff_polling_interval);
    }
    goto exit;
  }

  
  
  if(!ls_drv.enable || ls_drv.in_early_suspend)
  {
    MSG("%s, [Sensor Off+]",__func__);
    
    ls_reg.r00.bit.pon = 0;
    ls_reg.r00.bit.aen = 0;
    ls_reg.r00.bit.valid = 0;
    ls_reg.r00.bit.intr = 0;
    MSG("%s, R00 = 00",__func__);
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
    lsensor_clr_irq();
    MSG("%s, [Sensor Off-]",__func__);
  }
  
  
  else if(ls_drv.enable)
  {
    MSG("%s, [Sensor On+]",__func__);
    MSG("%s, R00 = 00",__func__);
    ls_reg.r00.bit.pon = 0;
    ls_reg.r00.bit.aen = 0;
    ls_reg.r00.bit.valid = 0;
    ls_reg.r00.bit.intr = 0;
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
    MSG("%s, R00 = 01",__func__);
    ls_reg.r00.bit.pon = 1;
    ls_reg.r00.bit.aen = 0;
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
    
    if(time_before(jiffies, ls_drv.jiff_resume_fast_update_time)) 
      ls_reg.atime = 182;       
    else
      ls_reg.atime = 108;       
    ls_reg.r02.bit.pers = 0;  
    ls_reg.r02.bit.mode = 1;  
    ls_reg.r02.bit.stop = 1;  
    ls_reg.low[0]  = 0x00;
    ls_reg.low[1]  = 0x00;
    ls_reg.high[0] = 0xFF;
    ls_reg.high[1] = 0xFF;
    ls_reg.again = 2,   
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,    sizeof(ls_reg.atime));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x02,&ls_reg.r02.byte, sizeof(ls_reg.r02.byte));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x03,&ls_reg.low[0],   sizeof(ls_reg.low));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x05,&ls_reg.high[0],  sizeof(ls_reg.high));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,    sizeof(ls_reg.again));
    msleep(2);
    lsensor_clr_irq();
    MSG("%s, R00 = 03",__func__);
    ls_reg.r00.bit.pon = 1;
    ls_reg.r00.bit.aen = 1;
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
    
    ls_info.a_ms = (256 - ls_reg.atime)*2785/1024;
    if(ls_info.a_ms <= 450) 
      ls_drv.jiff_polling_interval = HZ / 2;
    else                    
      ls_drv.jiff_polling_interval = HZ * 4 / 5;
    queue_delayed_work(ls_wqueue, &ls_work, ls_drv.jiff_polling_interval);
    MSG("%s, [Sensor On-]",__func__);
  }

exit:
  mutex_unlock(&lsensor_enable_lock);
  MSG("%s-", __func__);
}





static void a2h(char *in, char *out) 
{
  int i;
  char a, h[2];
  for(i=0; i<2; i++)
  {
    a = *in++;
    if(a <= '9')        h[i] = a - '0';
    else if (a <= 'F')  h[i] = a - 'A' + 10;
    else if (a <= 'f')  h[i] = a - 'a' + 10;
    else                h[i] = 0;
  }
  *out = (h[0]<<4) + h[1];
}
static void h2a(char *in, char *out) 
{
  static const char my_ascii[] = "0123456789ABCDEF";
  char c = *in;
  *out++ =  my_ascii[c >> 4];
  *out =    my_ascii[c & 0xF];
}
static void als_i2c_test(unsigned char *bufLocal, int count, struct i2c_client *client)
{
  struct i2c_msg msgs[2];
  int i2c_ret, i, j;
  char id, reg[2], len, dat[LUNA_ALS_BUF_LENGTH/4];

  printk(KERN_INFO "\n");

  
  
  
  if(bufLocal[1]=='r' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    a2h(&bufLocal[4], &reg[0]); 
    a2h(&bufLocal[6], &len);    
    if(len >= sizeof(dat))
    {
      MSG2("R %02X:%02X(%02d) Fail: max length=%d", id,reg[0],len,sizeof(dat));
      return;
    }
    msgs[0].addr = id;
    msgs[0].flags = 0;
    msgs[0].buf = &reg[0];
    msgs[0].len = 1;
    msgs[1].addr = id;
    msgs[1].flags = I2C_M_RD;
    msgs[1].buf = &dat[0];
    msgs[1].len = len;
    if(id == 0x30 ||  
      id == 0x48  ||  
      id == 0x3C  ||  
      id == 0x38  ||  
      id == 0x4A)     
    {
      i2c_ret = i2c_transfer(client->adapter, &msgs[0],1);  
      if(i2c_ret != 1)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d) (w)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
      i2c_ret = i2c_transfer(client->adapter, &msgs[1],1);  
      if(i2c_ret != 1)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d) (r)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
    }
    else
    {
      i2c_ret = i2c_transfer(client->adapter, msgs,2);
      if(i2c_ret != 2)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
    }
    j = 0;
    for(i=0; i<len; i++)
    {
      h2a(&dat[i], &bufLocal[j]);
      bufLocal[j+2] = ' ';
      j = j + 3;
    }
    bufLocal[j] = '\0';
    MSG2("R %02X:%02X(%02d) = %s", id,reg[0],len,bufLocal);
  }
  
  
  
  else if(bufLocal[1]=='R' && count>=11)
  {
    a2h(&bufLocal[2], &id);     
    a2h(&bufLocal[4], &reg[0]); 
    a2h(&bufLocal[6], &reg[1]); 
    a2h(&bufLocal[8], &len);    
    if(len >= sizeof(dat))
    {
      MSG2("R %02X:%02X%02X(%02d) Fail (max length=%d)", id,reg[0],reg[1],len,sizeof(dat));
      return;
    }
    msgs[0].addr = id;
    msgs[0].flags = 0;
    msgs[0].buf = &reg[0];
    msgs[0].len = 2;
    msgs[1].addr = id;
    msgs[1].flags = I2C_M_RD;
    msgs[1].buf = &dat[0];
    msgs[1].len = len;
    if(id == 0x30 ||  
      id == 0x48  ||  
      id == 0x3C  ||  
      id == 0x38  ||  
      id == 0x4A)     
    {
      i2c_ret = i2c_transfer(client->adapter, &msgs[0],1);  
      if(i2c_ret != 1)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d) (w)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
      i2c_ret = i2c_transfer(client->adapter, &msgs[1],1);  
      if(i2c_ret != 1)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d) (r)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
    }
    else
    {
      i2c_ret = i2c_transfer(client->adapter, msgs,2);
      if(i2c_ret != 2)
      {
        MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d)", id,reg[0],reg[1],len,i2c_ret);
        return;
      }
    }
    j = 0;
    for(i=0; i<len; i++)
    {
      h2a(&dat[i], &bufLocal[j]);
      bufLocal[j+2] = ' ';
      j = j + 3;
    }
    bufLocal[j] = '\0';
    MSG2("R %02X:%02X%02X(%02d) = %s", id,reg[0],reg[1],len,bufLocal);
  }
  
  
  
  else if(bufLocal[1]=='w' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    len = count - 5;
    if(len & 1)
    {
      MSG2("W %02X Fail (invalid data) len=%d", id,len);
      return;
    }
    len = len/2;
    if(len >= sizeof(dat))
    {
      MSG2("W %02X Fail (too many data)", id);
      return;
    }
    j = 4;
    for(i=0; i<len; i++)
    {
      a2h(&bufLocal[j], &dat[i]);
      j = j + 2;
    }
    msgs[0].addr = id;
    msgs[0].flags = 0;
    msgs[0].buf = &dat[0];
    msgs[0].len = len;
    i2c_ret = i2c_transfer(client->adapter, msgs,1);
    
    MSG2("W %02X = %s", id, i2c_ret==1 ? "Pass":"Fail");
  }
  else
  {
    MSG2("rd: r40000B   (addr=40(7bit), reg=00, read count=11");
    MSG2("Rd: R2C010902 (addr=2C(7bit), reg=0109, read count=2");
    MSG2("wr: w40009265CA (addr=40(7bit), reg & data=00,92,65,CA...");
  }
}


static ssize_t lsensor_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  return 0;
}
static ssize_t lsensor_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  unsigned char bufLocal[LUNA_ALS_BUF_LENGTH];
  unsigned char tmp8;

  
  

  printk(KERN_INFO "\n");
  if(count >= sizeof(bufLocal))
  {
    MSG2("%s input invalid, count = %d", __func__, count);
    return count;
  }
  memcpy(bufLocal,buf,count);

  switch(bufLocal[0])
  {
    case 'L':
      if(bufLocal[1]=='1')
      {
        MSG2("1000 Lux");
        lsensor_k_1000_lux = 1;
      }
      else
      {
        MSG2("500 Lux");
        lsensor_k_1000_lux = 0;
      }
      break;

    
    case 'z':
      if(bufLocal[1]=='0')
      {
        MSG2("Dynamic Log Off");
        lsensor_log_on = 0;
        lsensor_log3_on = 0;
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Dynamic Log On");
        lsensor_log_on = 1;
      }
      else if(bufLocal[1]=='3')
      {
        MSG2("Dynamic Log On");
        lsensor_log3_on = 1;
      }
      break;
    
    case 'm':
      if(bufLocal[1]=='0')
      {
        MSG2("Backlight Mode = [USER]");
        lsensor_enable_onOff(LSENSOR_EN_LCD,0);
        
        lsensor_bkl_fading(ls_drv.bkl_table[ls_drv.bkl_idx].level, ls_drv.brightness_backup);
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Backlight Mode = [SENSOR]");
        
        ls_drv.jiff_update_bkl_wait_time = jiffies + 3*HZ;
        lsensor_bkl_fading(ls_drv.brightness_backup, ls_drv.bkl_table[ls_drv.bkl_idx].level);
        lsensor_enable_onOff(LSENSOR_EN_LCD,1);
      }
      break;

    case 'b': 
      if(count >= 3)
      {
        a2h(&bufLocal[1], &tmp8);
        ls_drv.brightness_backup = tmp8;
        MSG2("Backlight level backup = %d",tmp8);
      }
      else
      {
        MSG2("Backlight level backup Fail, count=%d",count);
      }
      break;

    case 'p':

      break;

    
    case 'e':
      MSG2("lsensor enable = %c", bufLocal[1]);
      switch(bufLocal[1])
      {
        case '0':
          lsensor_enable_onOff(LSENSOR_EN_AP,0);
          break;
        case '1':
          lsensor_enable_onOff(LSENSOR_EN_AP,1);
          break;
      }
      break;

    
    
    case 'a':
      {
        static struct lsensor_cal_data cal_data = {0, 0};
        
        switch(bufLocal[1])
        {
          
          case '2':
            lsensor_calibration(&cal_data);
            MSG2("cal:  als = %d, status = %d", cal_data.als, cal_data.status);
            break;
          case '3':
            lsensor_update_equation_parameter(&cal_data);
            break;
          
        }
      }
      break;

    case 'c': 
      {
        unsigned char reg00_07[8], reg12_19[8];
        
        lsensor_read_i2c_retry(ls_drv.i2c_addr,0x00,&reg00_07[0],  sizeof(reg00_07));
        lsensor_read_i2c_retry(ls_drv.i2c_addr,0x12,&reg12_19[0],  sizeof(reg12_19));
        MSG2("Reg00_07 = %02X %02X %02X %02X %02X - %02X %02X %02X",
          reg00_07[0],reg00_07[1],reg00_07[2],reg00_07[3],reg00_07[4],
          reg00_07[5],reg00_07[6],reg00_07[7]);
        MSG2("Reg12_19 = %02X %02X %02X %02X %02X - %02X %02X %02X",
          reg12_19[0],reg12_19[1],reg12_19[2],reg12_19[3],reg12_19[4],
          reg12_19[5],reg12_19[6],reg12_19[7]);
      }
      
      {
        unsigned char *reg = (unsigned char *)&ls_reg;
        MSG2("r00_07 = %02X %02X %02X %02X - %02X %02X %02X %02X",
          reg[0],reg[1],reg[2],reg[3],reg[4],reg[5],reg[6],reg[7]);
      }
      break;



    
    
    case 'i':
      als_i2c_test(bufLocal, count, ls_drv.client);
      break;

    default:
      break;
  }

  return count;
}
static struct device_attribute lsensor_ctrl_attrs[] = {
  __ATTR(ctrl, 0664, lsensor_ctrl_show, lsensor_ctrl_store),
};




static void lsensor_early_suspend_func(struct early_suspend *h)
{
  MSG("%s+", __func__);

  ls_drv.in_early_suspend = 1;

  cancel_delayed_work_sync(&ls_work);
  flush_workqueue(ls_wqueue);
  lsensor_enable_onOff(0,0);
  if(ls_drv.enable & LSENSOR_EN_LCD)
    lsensor_bkl_fading(ls_drv.bkl_table[ls_drv.bkl_idx].level, 6);
  


  MSG("%s-", __func__);
}
static void lsensor_late_resume_func(struct early_suspend *h)
{
  MSG("%s+", __func__);
  if(ls_drv.enable & LSENSOR_EN_LCD)  
  {
    lsensor_bkl_fading(6, ls_drv.bkl_table[ls_drv.bkl_idx].level);
  }

  ls_drv.in_early_suspend = 0;
  ls_drv.jiff_update_bkl_wait_time    = jiffies + 2*HZ;
  ls_drv.jiff_resume_fast_update_time = jiffies + 10*HZ;
  lsensor_enable_onOff(0,0);

  MSG("%s-", __func__);
}
static struct early_suspend lsensor_early_suspend = {
  .level    = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
  .suspend  = lsensor_early_suspend_func,
  .resume   = lsensor_late_resume_func,
};




static int lsensor_misc_open(struct inode *inode_p, struct file *fp)
{
  ls_drv.opened ++;
  MSG("%s    <== [%d] (%04d)",__func__,ls_drv.opened,current->pid);
  return 0;
}
static int lsensor_misc_release(struct inode *inode_p, struct file *fp)
{
  ls_drv.opened --;
  MSG("%s <== [%d] (%04d)\n",__func__,ls_drv.opened,current->pid);
  return 0;
}
static long lsensor_misc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
  int ret = 0;
  unsigned int light, update_ga;
  long timeout;
  struct lsensor_cal_data cal_data;

  

  
  
  if(_IOC_TYPE(cmd) != LSENSOR_IOC_MAGIC)
  {
    MSG2("%s: Not LSENSOR_IOC_MAGIC", __func__);
    return -ENOTTY;
  }
  if(_IOC_DIR(cmd) & _IOC_READ)
  {
    ret = !access_ok(VERIFY_WRITE, (void __user*)arg, _IOC_SIZE(cmd));
    if(ret)
    {
      MSG2("%s: access_ok check write err", __func__);
      return -EFAULT;
    }
  }
  if(_IOC_DIR(cmd) & _IOC_WRITE)
  {
    ret = !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
    if(ret)
    {
      MSG2("%s: access_ok check read err", __func__);
      return -EFAULT;
    }
  }

  
  
  switch (cmd)
  {
    
    
    case LSENSOR_IOC_ENABLE:
      if(ls_drv.eng_mode)
      {
        MSG2("%s: LSENSOR_IOC_ENABLE Skip (Eng Mode)", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG2("%s: LSENSOR_IOC_ENABLE (%04d)", __func__,current->pid);
        lsensor_enable_onOff(LSENSOR_EN_AP,1);
      }
      break;

    case LSENSOR_IOC_DISABLE:
      if(ls_drv.eng_mode)
      {
        MSG2("%s: LSENSOR_IOC_DISABLE Skip (Eng Mode)", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG2("%s: LSENSOR_IOC_DISABLE (%04d)", __func__,current->pid);
        if(ls_drv.info_waiting_als)
        {
          ls_drv.info_waiting_als = 0;
          complete(&(ls_drv.info_comp_als));
        }
        lsensor_enable_onOff(LSENSOR_EN_AP,0);
      }
      break;

    case LSENSOR_IOC_ALS_WAKE:
      MSG("%s: LSENSOR_IOC_ALS_WAKE (%04d)", __func__,current->pid);
      if(ls_drv.info_waiting_als)
      {
        ls_drv.info_waiting_als = 0;
        complete(&(ls_drv.info_comp_als));
      }
      break;

    case LSENSOR_IOC_GET_STATUS:
      

      if(ls_drv.millilux == TAOS_ACCESS_FAIL) 
        light = TAOS_MAX_LUX;
      else
        light = ls_drv.millilux;  
      MSG("%s: LSENSOR_IOC_GET_STATUS = %d (%04d)", __func__,light,current->pid);
      
      if(copy_to_user((void __user*) arg, &light, _IOC_SIZE(cmd)))
      {
        MSG2("%s: LSENSOR_IOC_GET_STATUS Fail!", __func__);
        ret = -EFAULT;
      }
      break;

    case LSENSOR_IOC_CALIBRATION:
      lsensor_calibration(&cal_data); 
      
      SET_BIT(cal_data.status,2);   
      MSG("%s: LSENSOR_IOC_CALIBRATION = %d (%04d)", __func__,cal_data.als,current->pid);
      if(copy_to_user((void __user*) arg, &cal_data, _IOC_SIZE(cmd)))
      {
        MSG2("%s: LSENSOR_IOC_CALIBRATION Fail!", __func__);
        ret = -EFAULT;
      }
      break;

    case LSENSOR_IOC_UPDATE_GA:
      MSG("%s: LSENSOR_IOC_UPDATE_GA", __func__);
      
      if(copy_from_user(&update_ga, (void __user*) arg, _IOC_SIZE(cmd)))
      {
        MSG2("%s: LSENSOR_IOC_UPDATE_GA Fail", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG2("LSENSOR_IOC_UPDATE_GA = %d",update_ga);
        ls_drv.als_nv   = update_ga;
        cal_data.als    = update_ga;
        lsensor_update_equation_parameter(&cal_data);
      }
      break;

    
    
    case LSENSOR_IOC_ENG_ENABLE:
      MSG("%s: LSENSOR_IOC_ENG_ENABLE", __func__);
      ls_drv.eng_mode = 1;
      lsensor_enable_onOff(LSENSOR_EN_ENG,1);
      break;

    case LSENSOR_IOC_ENG_DISABLE:
      MSG("%s: LSENSOR_IOC_ENG_DISABLE", __func__);
      if(ls_drv.info_waiting)
      {
        ls_drv.info_waiting = 0;
        complete(&(ls_drv.info_comp));
      }
      ls_drv.eng_mode = 0;
      lsensor_enable_onOff(LSENSOR_EN_ENG,0);
      break;

    case LSENSOR_IOC_ENG_CTL_R:
      if(!ls_drv.eng_mode)
      {
        MSG2("%s: LSENSOR_IOC_ENG_CTL_R Skip (Not Eng Mode)", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG("%s: LSENSOR_IOC_ENG_CTL_R", __func__);
        
        lsensor_eng_get_reg();
        if(copy_to_user((void __user*) arg, &ls_eng, _IOC_SIZE(cmd)))
        {
          MSG("%s: LSENSOR_IOC_ENG_CTL_R Fail", __func__);
          ret = -EFAULT;
        }
      }
      break;

    case LSENSOR_IOC_ENG_CTL_W:
      if(!ls_drv.eng_mode)
      {
        MSG2("%s: LSENSOR_IOC_ENG_CTL_W Skip (Not Eng Mode)", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG("%s: LSENSOR_IOC_ENG_CTL_W", __func__);
        
        if(copy_from_user(&ls_eng, (void __user*) arg, _IOC_SIZE(cmd)))
        {
          MSG2("%s: LSENSOR_IOC_ENG_CTL_W Fail", __func__);
          ret = -EFAULT;
        }
        else
        {
          lsensor_eng_set_reg();
        }
      }
      break;

    case LSENSOR_IOC_ENG_INFO:
      if(!ls_drv.eng_mode)
      {
        MSG("%s: LSENSOR_IOC_ENG_INFO Skip (Not Eng Mode)", __func__);
        ret = -EFAULT;
      }
      else
      {
        MSG("%s: LSENSOR_IOC_ENG_INFO", __func__);
        
        if(ls_drv.info_waiting)   
        {
          MSG2("ERROR!!! info_waiting was TRUE before use");
        }
        ls_drv.info_waiting = 1;
        INIT_COMPLETION(ls_drv.info_comp);
        timeout = wait_for_completion_timeout(&(ls_drv.info_comp), 10*HZ);
        if(!timeout)
        {
          MSG2("ERROR!!! info_comp timeout");     
          ls_drv.info_waiting = 0;
        }
        if(copy_to_user((void __user*) arg, &ls_info, _IOC_SIZE(cmd)))
        {
          MSG2("%s: LSENSOR_IOC_ENG_INFO Fail", __func__);
          ret = -EFAULT;
        }
      }
      break;

    default:
      MSG("%s: unknown ioctl = 0x%X", __func__,cmd);
      break;
  }

  
  return ret;
}

static struct file_operations lsensor_misc_fops = {
  .owner          = THIS_MODULE,
  .open           = lsensor_misc_open,
  .release        = lsensor_misc_release,
  .unlocked_ioctl = lsensor_misc_ioctl, 
};
static struct miscdevice lsensor_misc_device = {
  .minor  = MISC_DYNAMIC_MINOR,
  .name   = "lsensor_taos",
  .fops   = &lsensor_misc_fops,
};




static int lsensor_probe(struct platform_device *pdev)
{
  int ret=0, err=0, fail=0, i2c_fail=0;

  printk("BootLog, +%s\n", __func__);

  
  
  
  ret = i2c_add_driver(&lsensor_i2c3_driver);
  if(ret) {MSG2("%s, lsensor_i2c3 add fail = %d", __func__,ret);}
  else    {MSG2("%s, lsensor_i2c3 add Pass", __func__);}
  ls_drv.i2c_addr = 0x39;

  
  {
    int i = 0;
    while(!ls_drv.client)
    {
      if(i++ > 10)
      {
        MSG2("%s, wait i2c probe, Timeout",__func__);
        break;
      }
      msleep(10);
    }
    if(i <= 10)
      MSG2("%s, wait i2c probe, Pass",__func__);
  }

  
  
  
  {
    unsigned char reg00_07[8], reg12_19[8];
    unsigned char err = 0;

    
    
    
    
    

    
    
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x00,&ls_reg.r00.byte, sizeof(ls_reg.r00.byte));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x01,&ls_reg.atime,    sizeof(ls_reg.atime));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x02,&ls_reg.r02.byte, sizeof(ls_reg.r02.byte));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x03,&ls_reg.low[0],   sizeof(ls_reg.low));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x05,&ls_reg.high[0],  sizeof(ls_reg.high));
    lsensor_write_i2c_retry(ls_drv.i2c_addr,0x07,&ls_reg.again,    sizeof(ls_reg.again));
    if(ls_drv.i2c_err)
    {
      err = 1;
      goto init_sensor_exit;
    }

    
    lsensor_clr_irq();

    
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x00,&reg00_07[0],  sizeof(reg00_07));
    lsensor_read_i2c_retry(ls_drv.i2c_addr,0x12,&reg12_19[0],  sizeof(reg12_19));
    MSG2("Reg00_07 = %02X %02X %02X %02X %02X - %02X %02X %02X",
      reg00_07[0],reg00_07[1],reg00_07[2],reg00_07[3],reg00_07[4],
      reg00_07[5],reg00_07[6],reg00_07[7]);
    MSG2("Reg12_19 = %02X %02X %02X %02X %02X - %02X %02X %02X",
      reg12_19[0],reg12_19[1],reg12_19[2],reg12_19[3],reg12_19[4],
      reg12_19[5],reg12_19[6],reg12_19[7]);

init_sensor_exit:
    if(err)
    {
      MSG2("%s, Lsensor init Fail!",__func__);
      i2c_fail = -1;
      
      goto exit_error;
    }
    else
    {
      MSG2("%s, Lsensor init Pass!",__func__);
    }
  }

  
  
  
  {
    unsigned int gpio = TEGRA_GPIO_PK7; 
    int PinValue;

    
    tegra_gpio_enable(gpio);
    ret = gpio_request(gpio, "ALS_INTR");
    if(ret) MSG2("%s, ALS_INTR gpio_request fail = %d", __func__,  ret);
    
    ret = gpio_direction_input(gpio);
    PinValue = gpio_get_value(gpio);
    if(ret) {MSG2("%s, INTR=i/p %d, Fail = %d", __func__, PinValue, ret);}
    else    {MSG2("%s, INTR=i/p %d", __func__, PinValue);}

    #ifdef LSENSOR_USE_INTERRUPT  
      ret = request_threaded_irq(TEGRA_GPIO_TO_IRQ(gpio),
        lsensor_irqhandler, NULL,
        IRQF_TRIGGER_FALLING,
        "lsensor_intr",
        NULL);
      if(ret) {MSG2("%s, INTR request_irq  Fail = %d", __func__, ret);}
    #endif
  }

  
  

  
  
  
  ls_drv.info_waiting = 0;
  init_completion(&(ls_drv.info_comp));
  ls_drv.info_waiting_als = 0;
  init_completion(&(ls_drv.info_comp_als));

  
  
  
  INIT_DELAYED_WORK(&ls_work, lsensor_work_func);
  ls_wqueue = create_singlethread_workqueue("luna_lsensor_workqueue");
  if(ls_wqueue) 
  {
    MSG2("%s luna_lsensor_workqueue created PASS!",__func__);
  }
  else  
  {
    MSG2("%s luna_lsensor_workqueue created FAIL!",__func__);
    fail = -1;
    goto exit_error;
  }

  
  ls_drv.jiff_update_bkl_wait_time = jiffies;

  ls_drv.inited = 1;

  
  
  
  ret = misc_register(&lsensor_misc_device);
  if(ret)
  {
    MSG2("%s: lsensor misc_register Fail, ret=%d", __func__, ret);
    err = 4;  goto exit_error;
  }

  #if 1
  
  ret = device_create_file(lsensor_misc_device.this_device, &lsensor_ctrl_attrs[0]);
  if(ret) {MSG2("create %s FAIL, ret=%d",lsensor_ctrl_attrs[0].attr.name,ret);}
  else    {MSG2("create /sys/devices/virtual/misc/lsensor_taos/%s",lsensor_ctrl_attrs[0].attr.name);}
  #endif

  
  
  
  ls_drv.input = input_allocate_device();
  if(!ls_drv.input)  {MSG2("input_allocate_device: FAIL");}
  else              {MSG2("input_allocate_device: PASS");}

  input_set_capability(ls_drv.input, EV_MSC, MSC_RAW);
  ls_drv.input->name = "lsensor_taos";

  ret = input_register_device(ls_drv.input);
  if(ret) {MSG2("input_register_device: FAIL=%d",ret);}
  else    {MSG2("input_register_device: PASS");}

  
  
  
  register_early_suspend(&lsensor_early_suspend);

  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;

exit_error:
  
  if(i2c_fail)
  {
    ret = device_create_file(&pdev->dev, &lsensor_ctrl_attrs[0]);
    if(ret) {MSG2("create %s FAIL, ret=%d",lsensor_ctrl_attrs[0].attr.name,ret);}
    else    {MSG2("create /sys/devices/platform/luna_lsensor/%s",lsensor_ctrl_attrs[0].attr.name);}
    printk("BootLog, -%s, ret=%d\n", __func__,ret);
    return ret;
  }


  ret = fail;
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

static struct platform_driver luna_lsensor_driver =
{
  .driver = {
    .name   = "luna_lsensor",
    .owner  = THIS_MODULE,
  },
  .probe    = lsensor_probe,
};
static int __init lsensor_init(void)
{
  int ret;
  ret = platform_driver_register(&luna_lsensor_driver);
  return ret;
}


module_init(lsensor_init);


