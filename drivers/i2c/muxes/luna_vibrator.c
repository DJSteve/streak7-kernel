















#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/mfd/tps6586x.h>
#include <mach/luna_hwid.h>
#include <../arch/arm/mach-tegra/gpio-names.h>
#include <../../../drivers/staging/android/timed_output.h>



static int vib_log_on1  = 0;
static int vib_log_on2  = 1;

#define MSG(format, arg...)   {if(vib_log_on1)  printk(KERN_INFO "[VIB]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(vib_log_on2)  printk(KERN_INFO "[VIB]" format "\n", ## arg);}


enum {VIB_HEN=0, VIB_LEN, VIB_CLK, VIB_MAX};
static const char *vib_pin_name[] = {"VIB_HEN", "VIB_LEN", "VIB_CLK"};
struct luna_vib_data
{
  struct i2c_client *client;
  unsigned int i2c_addr;
  unsigned int pin[VIB_MAX];
  int vibic_err;
  
  struct early_suspend drv_early_suspend;
  char  wake_flag;          
  char  early_suspend_flag;
  char  on_off;             
} luna_vib = 
{
  .client = NULL,
  .pin[VIB_HEN] = TEGRA_GPIO_PQ2, 
  .pin[VIB_LEN] = TEGRA_GPIO_PQ3, 
  .pin[VIB_CLK] = TEGRA_GPIO_PK6, 
  .vibic_err = 0,
  .wake_flag = 0,
  .early_suspend_flag = 0,
  .on_off = 0,
};
struct luna_vib_reg
{
  unsigned char addr;
  unsigned char data;
};
static DEFINE_MUTEX(luna_vib_lock);





static int vib_write_i2c(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  int i;
  unsigned char buf_w[64];
  struct i2c_msg msgs[] = {
    [0] = {
      .addr   = addr,
      .flags  = 0,
      .buf    = (void *)buf_w,
      .len    = len+1
    }
  };
  int ret;
  if(len >= sizeof(buf_w))  
    return -ENOMEM;
  if(!luna_vib.client)
    return -ENODEV;
  buf_w[0] = reg;
  for(i=0; i<len; i++)
    buf_w[i+1] = buf[i];
  ret = i2c_transfer(luna_vib.client->adapter, msgs, 1);

  if(ret == 1)
  {
    if(luna_vib.vibic_err)
      MSG2("%s, ret = 1, vibic_err = 0",__func__);
    luna_vib.vibic_err = 0;
  }
  else
  {
    luna_vib.vibic_err ++;
    if(luna_vib.vibic_err < 20)
      MSG2("%s, ret = %d, vibic_err = %d",__func__,ret,luna_vib.vibic_err);
  }
  return ret;
}
static int vib_write_i2c_retry(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
{
  int i,ret;
  for(i=0; i<5; i++)
  {
    ret = vib_write_i2c(addr,reg,buf,len);
    if(ret == 1)
      return ret;
    else
      msleep(10);
  }
  return ret;
}
void luna_vib_i2c0_shutdown(struct i2c_client *client)
{
  unsigned char data = 0x00;
  vib_write_i2c_retry(luna_vib.i2c_addr, 0x30, &data, sizeof(data));
  gpio_set_value( luna_vib.pin[VIB_HEN], 0);  
  gpio_set_value( luna_vib.pin[VIB_LEN], 0);  
  gpio_set_value( luna_vib.pin[VIB_CLK], 1);  
}
static int __devinit luna_vib_i2c0_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  
  
  MSG2("%s+",__func__);
  luna_vib.client = client;
  MSG2("%s-",__func__);
  return 0;
}
static struct i2c_device_id luna_vib_i2c0_id[] = { { "luna_vib_i2c0", 0 } };
static struct i2c_driver luna_vib_i2c0_driver = {
  .driver.owner = THIS_MODULE,
  .driver.name  = "luna_vib_i2c0",
  .id_table = luna_vib_i2c0_id,
  
  
  .shutdown = luna_vib_i2c0_shutdown,
  .probe    = luna_vib_i2c0_probe,
};




static int vib_init_chip(void)
{
  struct luna_vib_reg init[] =
  {
    #if 1 
    {0x30, 0x01},{0x00, 0x0F},{0x31, 0xCB},{0x32, 0x00},{0x33, 0x23},
    {0x34, 0x00},{0x35, 0x00},{0x36, 0x86},{0x30, 0x91}
    #else 
    {0x30, 0x01},{0x00, 0x0F},{0x31, 0xCB},{0x32, 0x00},{0x33, 0x23},
    {0x34, 0x00},{0x35, 0x00},{0x36, 0xB9},{0x30, 0x91}
    #endif
  };
  unsigned int i,i2c_ret;
  for(i=0; i<ARRAY_SIZE(init); i++)
  {
    i2c_ret = vib_write_i2c_retry(luna_vib.i2c_addr, init[i].addr, &init[i].data, 1);
    if(i2c_ret != 1)
    {
      MSG2("%s 0x%02X W 0x%02X Fail!",__func__,init[i].addr,init[i].data);
      return -ENODEV;
    }
  }
  return 0;
}
struct hrtimer luna_timed_vibrator_timer;
spinlock_t luna_timed_vibrator_lock;
static void vibrator_onOff(char onOff)
{
  mutex_lock(&luna_vib_lock);
  if(onOff)
  {
    
    
    gpio_set_value( luna_vib.pin[VIB_CLK], 0);  
    
    
    gpio_set_value( luna_vib.pin[VIB_HEN], 1);  
    luna_vib.on_off = 1;
  }
  else
  {
    luna_vib.on_off = 0;
    gpio_set_value( luna_vib.pin[VIB_HEN], 0);  
    gpio_set_value( luna_vib.pin[VIB_CLK], 1);  
    
    
  }
  mutex_unlock(&luna_vib_lock);
  MSG("VIB %s", onOff?"ON":"OFF");
}
static enum hrtimer_restart luna_timed_vibrator_timer_func(struct hrtimer *timer)
{
  MSG("%s", __func__);
  
  vibrator_onOff(0);
  return HRTIMER_NORESTART;
}
static void luna_timed_vibrator_init(void)
{
  
  hrtimer_init(&luna_timed_vibrator_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  luna_timed_vibrator_timer.function = luna_timed_vibrator_timer_func;
  spin_lock_init(&luna_timed_vibrator_lock);
}
int luna_timed_vibrator_get_time(struct timed_output_dev *sdev)
{
  ktime_t remain;
  int value = 0;
  MSG("%s", __func__);
  if(hrtimer_active(&luna_timed_vibrator_timer))
  {
    remain = hrtimer_get_remaining(&luna_timed_vibrator_timer);
    value = remain.tv.sec * 1000 + remain.tv.nsec / 1000000;
  }
  MSG("timeout = %d",value);
  return value;
}
void luna_timed_vibrator_enable(struct timed_output_dev *sdev, int timeout)
{
  unsigned long flags;
  MSG("%s", __func__);
  

  spin_lock_irqsave(&luna_timed_vibrator_lock, flags);
  hrtimer_cancel(&luna_timed_vibrator_timer);
  if(!timeout)  
    vibrator_onOff(0);
  else  
    vibrator_onOff(1);
  if(timeout > 0) 
  {
    hrtimer_start(&luna_timed_vibrator_timer,
      ktime_set(timeout / 1000, (timeout % 1000) * 1000000),
      HRTIMER_MODE_REL);
    MSG("%s Set timeout", __func__);
  }
  spin_unlock_irqrestore(&luna_timed_vibrator_lock, flags);
}

static struct timed_output_dev luna_timed_vibrator = {
  .name     = "vibrator",
  .enable   = luna_timed_vibrator_enable,
  .get_time = luna_timed_vibrator_get_time,
};




#define LUNA_VIB_BUF_LENGTH  256
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
static void vib_i2c_test(unsigned char *bufLocal, int count, struct i2c_client *client)
{
  struct i2c_msg msgs[2];
  int i2c_ret, i, j;
  char id, reg[2], len, dat[LUNA_VIB_BUF_LENGTH/4];

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
    if(id == 0x3C)  
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
    if(id == 0x3C)  
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
static ssize_t luna_vib_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  unsigned char bufLocal[LUNA_VIB_BUF_LENGTH];

  printk(KERN_INFO "\n");
  if(count >= sizeof(bufLocal))
  {
    MSG2("%s input invalid, count = %d", __func__, count);
    return count;
  }
  memcpy(bufLocal,buf,count);

  switch(bufLocal[0])
  {
    
    
    case 'z':
      if(bufLocal[1]=='0')
      {
        MSG2("Dynamic Log All Off");
        vib_log_on1 = 0;
        vib_log_on2 = 0;
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Dynamic Log 1 On");
        vib_log_on1 = 1;
      }
      else if(bufLocal[1]=='2')
      {
        MSG2("Dynamic Log 2 On");
        vib_log_on2 = 1;
      }
      break;

    case 'b':
      vib_init_chip();
      break;

    case 'g':
      switch(bufLocal[1])
      {
        case 'h':
          if(bufLocal[2]=='0')
          {
            MSG2("HEN = 0");
            gpio_set_value( luna_vib.pin[VIB_HEN], 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("HEN = 1 (On)");
            gpio_set_value( luna_vib.pin[VIB_HEN], 1);
          }
          break;
        case 'l':
          if(bufLocal[2]=='0')
          {
            MSG2("LEN = 0");
            gpio_set_value( luna_vib.pin[VIB_LEN], 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("LEN = 1 (On)");
            gpio_set_value( luna_vib.pin[VIB_LEN], 1);
          }
          break;
        case 'c':
          if(bufLocal[2]=='0')
          {
            MSG2("CLK = 0 (On)");
            gpio_set_value( luna_vib.pin[VIB_CLK], 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("CLK = 1");
            gpio_set_value( luna_vib.pin[VIB_CLK], 1);
          }
          break;
        default:
          {
            unsigned int i, pin_value[VIB_MAX];
            for(i=VIB_HEN; i<VIB_MAX; i++)
              pin_value[i] = gpio_get_value(luna_vib.pin[i]);
            MSG2("HEN LEN CLK = %d %d %d", pin_value[VIB_HEN],pin_value[VIB_LEN],pin_value[VIB_CLK]);
          }
          break;
      }
      break;

    
    
    case 'i':
      vib_i2c_test(bufLocal, count, luna_vib.client);
      break;

    default:
      break;
  }
  return count;
}

static struct device_attribute luna_vib_ctrl_attrs[] = {
  __ATTR(ctrl, 0664, NULL, luna_vib_ctrl_store),
};


static void luna_vib_early_suspend(struct early_suspend *h)
{
  
  luna_vib.early_suspend_flag = 1;
  if(luna_vib.on_off)   
  {
    int ret;
    ret = hrtimer_try_to_cancel(&luna_timed_vibrator_timer);
    MSG2("Off: (%s) (hrtimer_try_to_cancel = %d)", __func__,ret);
    vibrator_onOff(0);
  }
  
}
static void luna_vib_late_resume(struct early_suspend *h)
{
  
  luna_vib.early_suspend_flag = 0;
  
}


static int luna_vib_probe(struct platform_device *pdev)
{
  int i, ret;
  MSG2("%s+",__func__);

  
  
  
  ret = i2c_add_driver(&luna_vib_i2c0_driver);
  if(ret) {MSG2("%s, i2c0 add fail = %d", __func__,ret);}
  else    {MSG2("%s, i2c0 add Pass", __func__);}
  luna_vib.i2c_addr = 0x48;

  
  {
    i = 0;
    while(!luna_vib.client)
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

  
  
  
  for(i=VIB_HEN; i<VIB_MAX; i++)
  {
    unsigned int  gpio = luna_vib.pin[i];
    tegra_gpio_enable(gpio);
    ret = gpio_request(gpio, vib_pin_name[i]);
    if(ret) MSG2("%s, pin[%s] gpio_request fail = %d", __func__, vib_pin_name[i], ret);

    if(i==VIB_CLK ||  
      i==VIB_HEN)     
    {
      ret = gpio_direction_output(gpio,  0);
      if(ret) {MSG2("%s, pin[%s]=o/p 0, Fail = %d", __func__, vib_pin_name[i], ret);}
      else    {MSG2("%s, pin[%s]=o/p 0", __func__, vib_pin_name[i]);}
    }
    else              
    {
      ret = gpio_direction_output(gpio,  1);
      if(ret) {MSG2("%s, pin[%s]=o/p 1, Fail = %d", __func__, vib_pin_name[i], ret);}
      else    {MSG2("%s, pin[%s]=o/p 1", __func__, vib_pin_name[i]);}
    }
  }
  msleep(10);  

  
  
  
  {
    int i2c_ret;
    unsigned char data = 0x80;
    
    i2c_ret = vib_write_i2c_retry(luna_vib.i2c_addr, 0x32, &data, sizeof(data));
    if (i2c_ret == 1) {MSG2("%s, Vibrator Reset, Pass", __func__);}
    else              {MSG2("%s, Vibrator Reset, Fail = %d", __func__, i2c_ret);}

    
    
    {
      
      mdelay(1);
      if(!vib_init_chip())  
      {
        MSG2("%s, Vibrator init Pass!",__func__);
      }
      else
      {
        MSG2("%s, Vibrator init Fail!",__func__);
        
        
      }
    }
    
    
    
    
    
    
    gpio_set_value( luna_vib.pin[VIB_CLK], 1);  
  }

  
  
  
  

  
  
  
  luna_vib.drv_early_suspend.level    = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
  luna_vib.drv_early_suspend.suspend  = luna_vib_early_suspend;
  luna_vib.drv_early_suspend.resume   = luna_vib_late_resume;
  register_early_suspend(&luna_vib.drv_early_suspend);

  
  
  
  luna_timed_vibrator_init();
  ret = timed_output_dev_register(&luna_timed_vibrator);

  
  
  
  ret = device_create_file(&pdev->dev, &luna_vib_ctrl_attrs[0]);
  if(ret)
  {
    MSG2("%s: create FAIL, ret=%d",luna_vib_ctrl_attrs[0].attr.name,ret);
  }
  else
  {
    MSG2("%s: create PASS, ret=%d",luna_vib_ctrl_attrs[0].attr.name,ret);
  }

  MSG2("%s-",__func__);


  return ret;

}

static struct platform_driver luna_vibrator_driver = {
  .driver   = {
    .name   = "luna_vibrator",
    .owner    = THIS_MODULE,
  },
  .probe    = luna_vib_probe,
};
static int __init luna_vib_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_vibrator_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

module_init(luna_vib_init);


