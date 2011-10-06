/*
 * drivers/power/tegra_odm_battery.c
 *
 * Battery driver for batteries implemented using NVIDIA Tegra ODM kit PMU
 * adaptation interface
 *
 * Copyright (c) 2009, NVIDIA Corporation.
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



#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/mfd/tps6586x.h>
#include <mach/luna_hwid.h>
#include <../arch/arm/mach-tegra/gpio-names.h>
#include "luna_battery.h"

static int bat_log_on1  = 0;
static int bat_log_on2  = 1;
static int bat_log_on3  = 0;

#define MSG(format, arg...)   {if(bat_log_on1)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(bat_log_on2)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG3(format, arg...)  {if(bat_log_on3)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}

static DEFINE_SPINLOCK(luna_bat_irq_lock);


const char *status_text[] = {"Unknown", "Charging", "Discharging", "Not charging", "Full"};
const char *health_text[] = {"Unknown", "Good", "Overheat", "Dead", "Over voltage", "Unspecified failure", "cold"};
const char *technology_text[] = {"Unknown", "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd", "LiMn"};
const char *bat_temp_state_text[] = {"Normal", "Hot", "Cold"};
const char *bat_pin_name[] = {"IUSB", "USUS", "CEN ", "DCM ", "LIMD", "LIMB", "OTG ", "UOK ", "FLT ", "CHG ", "DOK ", "GLOW", "BLOW"};
const char *chg_in_name[] = {"NONE   ", "ERROR  ", "DOK_DET", "AC_DET ", "USB_DET"};



static struct luna_bat_eng_data luna_eng_data;
static struct timer_list luna_timer;
static struct work_struct luna_bat_work;
static struct workqueue_struct *luna_bat_wqueue;
static struct work_struct luna_bat_work_poweroff;
static struct workqueue_struct *luna_bat_wqueue_poweroff;
static struct i2c_client *luna_bat_gauge_client = NULL;

static int luna_bat_get_ac_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_usb_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_bat_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static ssize_t luna_bat_get_other_property(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static void luna_bat_work_func(struct work_struct *work);
static void luna_bat_timer_func(unsigned long temp);
static void luna_bat_early_suspend(struct early_suspend *h);
static void luna_bat_late_resume(struct early_suspend *h);

extern void ventana_huawei_power_up_sequence(int);
extern void luna_tmon_callback(int);
extern void luna_bodysar_callback(int);
extern int luna_capkey_callback(int);
extern void luna_backlight_callback(int up); 




static struct device_attribute luna_bat_ctrl_attrs[] = {
  __ATTR(batt_vol,  0664, luna_bat_get_other_property, NULL),
  __ATTR(batt_temp, 0664, luna_bat_get_other_property, NULL),
  __ATTR(chg_type,  0664, luna_bat_get_other_property, NULL),
  __ATTR(ctrl,      0664, luna_bat_ctrl_show, luna_bat_ctrl_store),
};
static enum power_supply_property luna_bat_ac_props[] = {
  POWER_SUPPLY_PROP_ONLINE,
};
static enum power_supply_property luna_bat_usb_props[] = {
  POWER_SUPPLY_PROP_ONLINE,
};
static enum power_supply_property luna_bat_bat_props[] = {
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_CAPACITY,
  POWER_SUPPLY_PROP_TECHNOLOGY,
};

static struct luna_bat_data luna_bat = {
  
  .pin[CHG_IUSB] = {.gpio=TEGRA_GPIO_PP0, .pin_in=0, .pin_en=0, },  
  
  .pin[CHG_CEN]  = {.gpio=TEGRA_GPIO_PP1, .pin_in=0, .pin_en=1, },  
  .pin[CHG_DCM]  = {.gpio=TEGRA_GPIO_PP2, .pin_in=0, .pin_en=1, },  
  .pin[CHG_LIMD] = {.gpio=TEGRA_GPIO_PR3, .pin_in=0, .pin_en=1, },  
  .pin[CHG_LIMB] = {.gpio=TEGRA_GPIO_PR4, .pin_in=0, .pin_en=1, },  
  .pin[CHG_OTG]  = {.gpio=TEGRA_GPIO_PU0, .pin_in=0, .pin_en=0, },  
  .pin[CHG_UOK]  = {.gpio=TEGRA_GPIO_PO5, .pin_in=1, .pin_en=0, },  
  .pin[CHG_FLT]  = {.gpio=TEGRA_GPIO_PQ7, .pin_in=1, .pin_en=0, },  
  .pin[CHG_CHG]  = {.gpio=TEGRA_GPIO_PV3, .pin_in=1, .pin_en=0,     
                    .intr_count=ATOMIC_INIT(0), },
  .pin[CHG_DOK]  = {.gpio=TEGRA_GPIO_PS2, .pin_in=1, .pin_en=0,     
                    .intr_count=ATOMIC_INIT(0), },
  .pin[CHG_GLOW] = {.gpio=TEGRA_GPIO_PW2, .pin_in=1, .pin_en=0,     
                    .intr_count=ATOMIC_INIT(0), },
  .pin[CHG_BLOW] = {.gpio=TEGRA_GPIO_PW3, .pin_in=1, .pin_en=0, },  
  .psy_ac = {
    .name   = "ac",
    .type   = POWER_SUPPLY_TYPE_MAINS,
    .properties = luna_bat_ac_props,
    .num_properties = ARRAY_SIZE(luna_bat_ac_props),
    .get_property = luna_bat_get_ac_property,
  },
  .psy_usb = {
    .name   = "usb",
    .type   = POWER_SUPPLY_TYPE_USB,
    .properties = luna_bat_usb_props,
    .num_properties = ARRAY_SIZE(luna_bat_usb_props),
    .get_property = luna_bat_get_usb_property,
  },
  .psy_bat = {
    .name   = "battery",
    .type   = POWER_SUPPLY_TYPE_BATTERY,
    .properties = luna_bat_bat_props,
    .num_properties = ARRAY_SIZE(luna_bat_bat_props),
    .get_property = luna_bat_get_bat_property,
  },
  
  #ifdef CONFIG_HAS_EARLYSUSPEND
    .drv_early_suspend.level = 150,
    .drv_early_suspend.suspend = luna_bat_early_suspend,
    .drv_early_suspend.resume = luna_bat_late_resume,
  #endif
  
  .jiff_property_valid_interval = 1*HZ/2,
  .jiff_polling_interval = 10*HZ,
  
  .bat_status   = POWER_SUPPLY_STATUS_UNKNOWN,
  .bat_health   = POWER_SUPPLY_HEALTH_UNKNOWN,
  .bat_present  = 1,
  .bat_capacity = 50,
  .bat_vol      = 3800,
  .bat_temp     = 270,
  .bat_technology = POWER_SUPPLY_TECHNOLOGY_LION,
  .bat_capacity_history[0] = 50,
  .bat_capacity_history[1] = 50,
  .bat_capacity_history[2] = 50,
  .gagic_err    = 0,
  .bat_low_count = 0,
  .poweroff_started = 0,
  .bat_health_err_count = 0,
  
  .inited       = 0,
  .suspend_flag = ATOMIC_INIT(0),
  .early_suspend_flag = 0,
  .wake_flag    = 0,
  
  .ac_online    = 0,
  .usb_online   = 0,
  
  .usb_current  = USB_STATUS_USB_0,
  .usb_pmu_det  = 0,
  .ac_pmu_det   = 0,
  .read_again   = ATOMIC_INIT(0),
  .chg_in       = CHG_IN_NONE,
  .chg_bat_current  = CHG_BAT_CURRENT_HIGH,
  .chg_ctl      = CHG_CTL_NONE,
};




static inline void pin_enable(struct chg_pin pin)
{
  gpio_set_value(pin.gpio, pin.pin_en);
}
static inline void pin_disable(struct chg_pin pin)
{
  gpio_set_value(pin.gpio, !pin.pin_en);
}
static inline int pin_value(struct chg_pin pin)
{
  return gpio_get_value(pin.gpio);
}




static int gag_read_i2c(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
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
  if(!luna_bat_gauge_client)
    return -ENODEV;
  ret = i2c_transfer(luna_bat_gauge_client->adapter, msgs, 2);
  if(ret == 2)
  {
    if(luna_bat.gagic_err)
      MSG2("%s, status = 2, gagic_err = 0",__func__);
    luna_bat.gagic_err = 0;
  }
  else
  {
    luna_bat.gagic_err ++;
    if(luna_bat.gagic_err < 20)
      MSG2("%s, ret = %d, gagic_err = %d",__func__,ret,luna_bat.gagic_err);
  }
  return ret;
}
static int gag_write_i2c(unsigned char addr, unsigned char reg, unsigned char* buf, unsigned char len)
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
  if(!luna_bat_gauge_client)
    return -ENODEV;
  buf_w[0] = reg;
  for(i=0; i<len; i++)
    buf_w[i+1] = buf[i];
  ret = i2c_transfer(luna_bat_gauge_client->adapter, msgs, 1);

  if(ret == 1)
  {
    if(luna_bat.gagic_err)
      MSG2("%s, status = 2, gagic_err = 0",__func__);
    luna_bat.gagic_err = 0;
  }
  else
  {
    luna_bat.gagic_err ++;
    if(luna_bat.gagic_err < 20)
      MSG2("%s, ret = %d, gagic_err = %d",__func__,ret,luna_bat.gagic_err);
  }
  return ret;
}




static int luna_bat_i2c_suspend(struct i2c_client *client, pm_message_t state)
{
  MSG("%s", __func__);
  
  
  atomic_set(&luna_bat.suspend_flag, 1);
  if(luna_bat.inited)
  {
    del_timer_sync(&luna_timer);
    cancel_work_sync(&luna_bat_work);
    flush_workqueue(luna_bat_wqueue);
  }
  return 0;
}
static int luna_bat_i2c_resume(struct i2c_client *client)
{
  MSG("%s", __func__);
  
  
  atomic_set(&luna_bat.suspend_flag, 0);
  if(luna_bat.inited)
  {
    int ret;
    atomic_set(&luna_bat.read_again, 3);
    ret = queue_work(luna_bat_wqueue, &luna_bat_work);
    if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
  }
  return 0;
}
static void luna_bat_i2c_shutdown(struct i2c_client *client)
{
  
  
  MSG2("%s",__func__);
}
static int __devinit luna_bat_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  
  MSG2("%s+, client=%s, id=%s, adapter=%s, id=%d, system_rev = %d",__func__,client->name, id->name,
    client->adapter->name, client->adapter->id, system_rev);
  luna_bat_gauge_client = client;
  MSG2("%s-",__func__);
  return 0;
}



static const struct i2c_device_id luna_bat_i2c4_id[] = { { "luna_bat_i2c4", 0 } };
static struct i2c_driver luna_bat_i2c4_driver = {
  .driver.owner = THIS_MODULE,
  .driver.name  = "luna_bat_i2c4",
  .id_table = luna_bat_i2c4_id,
  .suspend  = luna_bat_i2c_suspend,
  .resume   = luna_bat_i2c_resume,
  .shutdown = luna_bat_i2c_shutdown,
  .probe    = luna_bat_i2c_probe,
};


static int luna_bat_get_ac_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.ac_online;
      
      if(luna_bat.usb_online==1 &&
        (luna_bat.usb_current==USB_STATUS_USB_1000 || luna_bat.usb_current==USB_STATUS_USB_2000))
        val->intval = 1;
      if(luna_bat.poweroff_started) 
        val->intval = 0;
      MSG("ac:  online = %d", luna_bat.ac_online);
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}
static int luna_bat_get_usb_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.usb_online;
      
      if(luna_bat.usb_online==1 &&
        (luna_bat.usb_current==USB_STATUS_USB_1000 || luna_bat.usb_current==USB_STATUS_USB_2000))
        val->intval = 0;
      if(luna_bat.poweroff_started) 
        val->intval = 0;
      MSG("usb: online = %d", luna_bat.usb_online);
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}
static int luna_bat_get_bat_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  static int ap_get_cap_0 = 0;
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_STATUS:
      val->intval = luna_bat.bat_status;
      MSG("bat: status = %s", status_text[luna_bat.bat_status]);
      break;
    case POWER_SUPPLY_PROP_HEALTH:
      val->intval = luna_bat.bat_health;
      if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_COLD) 
        luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
      MSG("bat: health = %s", health_text[luna_bat.bat_health]);
      break;
    case POWER_SUPPLY_PROP_PRESENT:
      val->intval = luna_bat.bat_present;
      MSG("bat: present = %d", luna_bat.bat_present);
      break;
    case POWER_SUPPLY_PROP_CAPACITY:
      val->intval = luna_bat.bat_capacity;
      MSG("bat: capacity = %d", luna_bat.bat_capacity);
      if(val->intval != 0)
      {
        ap_get_cap_0 = 0;
      }
      else if(!ap_get_cap_0)
      {
        ap_get_cap_0 = 1;
        MSG2("## AP get bat_capacity = 0, it will power off!");
      }
      break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
      val->intval = luna_bat.bat_technology;
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}
static ssize_t luna_bat_get_other_property(struct device *dev, struct device_attribute *attr,char *buf)
{
  int val=0;
  const ptrdiff_t off = attr - luna_bat_ctrl_attrs;  
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(off)
  {
    case 0: 
      val = luna_bat.bat_vol;
      MSG("bat: batt_vol = %d", luna_bat.bat_vol);
      break;
    case 1: 
      val = luna_bat.bat_temp;
      MSG("bat: batt_temp = %d", luna_bat.bat_temp);
      break;
    case 2: 
      if(luna_bat.ac_online)
        val = 1;
      else if((luna_bat.usb_online==1 && luna_bat.usb_current==USB_STATUS_USB_1000) ||
              (luna_bat.usb_online==1 && luna_bat.usb_current==USB_STATUS_USB_2000) )
        val = 2;
      else
        val = 0;
      MSG("bat: batt_type = %d", val);
  }
  return sprintf(buf, "%d\n", val);
}

static struct regulator *luna_test_ldo0 = NULL;
static struct regulator *luna_test_ldo6 = NULL;
static struct regulator *luna_test_ldo7 = NULL;
static struct regulator *luna_test_ldo8 = NULL;
static struct regulator *luna_test_gpio1 = NULL;
static struct regulator *luna_test_gpio2 = NULL;
static struct regulator *luna_test_gpio3 = NULL;
static struct regulator *luna_test_gpio4 = NULL;
static struct regulator *luna_test_ledpwm = NULL;
static void luna_bat_pmu_test(unsigned char* bufLocal, size_t count)
{
  struct regulator *ldo = 0;
  unsigned id, led_val;
  int onOff;

  if(count < 4)
  {
    MSG2("Invalid parameters, count = %d", count);
    return;
  }

  if(bufLocal[3] == '0')
    onOff = 0;
  else if(bufLocal[3] == '1')
    onOff = 1;
  else
    onOff = -1;

  
  
  
  if(bufLocal[1] == 'l' || bufLocal[1] == 'L')
  {
    MSG2("LDO:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '0':
        ldo = luna_test_ldo0;
        break;
      case '6':
        ldo = luna_test_ldo6;
        break;
      case '7':
        ldo = luna_test_ldo7;
        break;
      case '8':
        ldo = luna_test_ldo8;
        break;
      default:
        MSG2("Invalid LDO");
        return;
    }
    
    
    
    ftm_test_mode_onOff(1);
    if(onOff == 1)      {ftm_test_regulator_enable(ldo);   MSG2("ON"); }
    else if(onOff == 0) {ftm_test_regulator_disable(ldo);  MSG2("OFF");}
    else                MSG2("Invalid LDO OnOff");
  }
  
  
  
  else if(bufLocal[1] == 'g' || bufLocal[1] == 'G')
  {
    MSG2("GPIO:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '1':
        ldo = luna_test_gpio1;  
        break;
      case '2':
        ldo = luna_test_gpio2;  
        break;
      case '3':
        ldo = luna_test_gpio3;  
        break;
      case '4':
        ldo = luna_test_gpio4;  
        break;
      default:
        MSG2("Invalid GPIO");
        return;
    }
    
    
    
    ftm_test_mode_onOff(1);
    if(onOff == 1)      {ftm_test_regulator_enable(ldo);   MSG2("ON"); }
    else if(onOff == 0) {ftm_test_regulator_disable(ldo);  MSG2("OFF");}
    else                MSG2("Invalid LDO OnOff");
  }
  
  
  
  else if(bufLocal[1] == 'r' || bufLocal[1] == 'R')  
  {
    MSG2("RGB:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '1':
        id = TPS6586X_RGB1_RED;   
        break;
      case '2':
        id = TPS6586X_RGB1_GREEN; 
        break;
      case '3':
        id = TPS6586X_RGB1_BLUE;
        break;
      case 'l':
      case 'L':
        
        
        
        ftm_test_mode_onOff(1);
        if(onOff == 1)      {ftm_test_regulator_enable(luna_test_ledpwm);   MSG2("ON"); }
        else if(onOff == 0) {ftm_test_regulator_disable(luna_test_ledpwm);  MSG2("OFF");}
        else                MSG2("Invalid LEDPWM OnOff");
        return;
      default:
        MSG2("Invalid LED");
        return;
    }
    
    if(onOff == 1)      {tps6586x_set_rgb1(id, 0x1F | 0xA5000000); MSG2("ON");  }
    else if(onOff == 0) {tps6586x_set_rgb1(id, 0x00 | 0xA5000000); MSG2("OFF"); }
    else                MSG2("Invalid LED OnOff");
  }
  
  
  
  else if(bufLocal[1] == 'c' || bufLocal[1] == 'C')
  {
    MSG2("LDO0  = %d", regulator_is_enabled(luna_test_ldo0));
    MSG2("LDO6  = %d", regulator_is_enabled(luna_test_ldo6));
    MSG2("LDO7  = %d", regulator_is_enabled(luna_test_ldo7));
    MSG2("LDO8  = %d", regulator_is_enabled(luna_test_ldo8));
    MSG2("GPIO1 = %d", regulator_is_enabled(luna_test_gpio1));
    MSG2("GPIO2 = %d", regulator_is_enabled(luna_test_gpio2));
    MSG2("GPIO3 = %d", regulator_is_enabled(luna_test_gpio3));
    MSG2("GPIO4 = %d", regulator_is_enabled(luna_test_gpio4));
    tps6586x_get_rgb1(TPS6586X_RGB1_RED, &led_val);
    MSG2("LED1  = %d", led_val);
    tps6586x_get_rgb1(TPS6586X_RGB1_GREEN, &led_val);
    MSG2("LED2  = %d", led_val);
    MSG2("LEDPWM = %d", regulator_is_enabled(luna_test_ledpwm));
  }
  
  
  
  else if(bufLocal[1] == 'z' || bufLocal[1] == 'Z')
  {
    
    tps6586x_set_rgb1(TPS6586X_RGB1_RED, 0x5A5A5A5A);
    ftm_test_mode_onOff(0);
    MSG2("Clear LDO Bypass Flags!");
  }
  
  
  
  else
  {
    MSG2("Invalid:%c", bufLocal[1]);
  }
}



static void luna_bat_datacard_reset(void)
{
  printk(KERN_CRIT "[BAT]%s+\n",__func__);

  wake_lock(&luna_bat.wlock_3g);

  luna_tmon_callback(0);
  luna_bodysar_callback(0);
  luna_capkey_callback(0);
  ventana_huawei_power_up_sequence(0);
  luna_backlight_callback(0);

  
  ftm_test_mode_onOff(1);
  ftm_test_regulator_disable(luna_test_gpio2);
  msleep(200); 
  ftm_test_regulator_enable(luna_test_gpio2);
  ftm_test_mode_onOff(0);
  
  mdelay(10);

  luna_backlight_callback(1);
  ventana_huawei_power_up_sequence(1);
  luna_capkey_callback(1);
  luna_bodysar_callback(1);
  luna_tmon_callback(1);

  wake_lock_timeout(&luna_bat.wlock_3g, HZ*5);

  printk(KERN_CRIT "[BAT]%s-\n",__func__);
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
static void luna_bat_i2c_test(unsigned char *bufLocal, int count, struct i2c_client *client)
{
  struct i2c_msg msgs[2];
  int i2c_ret, i, j;
  char id, reg[2], len, dat[LUNA_BAT_BUF_LENGTH/4];

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
    i2c_ret = i2c_transfer(client->adapter, msgs,2);
    if(i2c_ret != 2)
    {
      MSG2("R %02X:%02X(%02d) Fail: ret=%d", id,reg[0],len,i2c_ret);
      return;
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
    i2c_ret = i2c_transfer(client->adapter, msgs,2);
    if(i2c_ret != 2)
    {
      MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d)", id,reg[0],reg[1],len,i2c_ret);
      return;
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

#define SOC_CHECK_A 200
#define SOC_CHECK_B 202
static void luna_bat_gauge_reset(void)
{
  static unsigned char reset[] = {0x54, 0x00};
  MSG2("%s",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0xFE, &reset[0], sizeof(reset));
  msleep(10);
}
static bool luna_bat_gauge_verify(void)
{
  static unsigned char w2_0E[] = {0xE8, 0x20};
  static unsigned char w2_0C[] = {0xFF, 0x00};
  unsigned char data2[2], data4[4], result, i;
  unsigned char data14[14];
  static unsigned char unlock[]= {0x4A, 0x57};
  static unsigned char lock[]  = {0x00, 0x00};

  MSG2("%s+",__func__);
  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG2("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, read RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);
  if((data4[1] & 0x1F) != 0x1F)
  {
    MSG2("%s-, ALERT not match ### FAIL ###",__func__);
    return FALSE;
  }

  
  gag_read_i2c(luna_bat.i2c_addr, 0x00, &data2[0], sizeof(data2));

  
  MSG("%s, unlock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &unlock[0], sizeof(unlock));

  
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, backup RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w2_0E[0],w2_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w2_0E[0], sizeof(w2_0E));

  
  MSG("%s, write RCOMP Max %02X %02X",__func__,w2_0C[0],w2_0C[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &w2_0C[0], sizeof(w2_0C));
  msleep(150);

  
  for(i=0; i<2; i++)
  {
    msleep(150);
    gag_read_i2c(luna_bat.i2c_addr, 0x04, &data2[0], sizeof(data2));
    result = (data2[0] >= SOC_CHECK_A && data2[0] <= SOC_CHECK_B) ? TRUE : FALSE;
    MSG2("%s, TestSOC = %d %d ### %s ###",__func__,data2[0],data2[1],result ? "PASS":"FAIL");
    if(result == TRUE)
      break;
  }

  
  MSG("%s, restore RCOMP, OCV",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  msleep(10);

  
  MSG("%s, lock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &lock[0], sizeof(lock));
  msleep(400);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data4[0], sizeof(data4));
  MSG2("%s, VCELL = %d (%02X %02X), SOC = %d (%02X %02X)",__func__,
    ((data4[0]<<4)+(data4[1]>>4))*5/4, data4[0], data4[1],
    data4[2]>>1, data4[2], data4[3]);

  if(result == TRUE)
  {
    
    luna_bat.bat_capacity_history[2] = data4[2]>>1;
    luna_bat.bat_capacity_history[1] = data4[2]>>1;
    luna_bat.bat_capacity_history[0] = data4[2]>>1;
    MSG2("%s- ### PASS ###",__func__);
    return TRUE;
  }
  else
  {
    MSG2("%s- ### FAIL ###",__func__);
    return FALSE;
  }
}

static bool luna_bat_gauge_init(void)
{
  static unsigned char rcomp[] = {0x61, 0x1F}; 
  static unsigned char w2_0E[] = {0xE8, 0x20};
  static unsigned char w2_0C[] = {0xFF, 0x00};
  static unsigned char w3_40[] = {0x9D, 0x30, 0xAD, 0x10, 0xAD, 0x80, 0xAE, 0x20, 0xAE, 0xC0, 0xB2, 0x60, 0xB3, 0xF0, 0xB4, 0x60};
  static unsigned char w3_50[] = {0xB5, 0xA0, 0xB6, 0x90, 0xBD, 0x10, 0xBE, 0x80, 0xC9, 0xF0, 0xCB, 0xA0, 0xCF, 0x40, 0xDE, 0x20};
  static unsigned char w3_60[] = {0x00, 0x40, 0x62, 0x60, 0x26, 0x20, 0x2F, 0x60, 0x19, 0xC0, 0x0B, 0xA0, 0x98, 0x40, 0x43, 0xE0};
  static unsigned char w3_70[] = {0x0D, 0xA0, 0x0F, 0xE0, 0x39, 0x60, 0x0B, 0xA0, 0x24, 0x40, 0x0B, 0x80, 0x00, 0x60, 0x00, 0x60};
  static unsigned char w4_0E[] = {0xE8, 0x20};
  static unsigned char unlock[]= {0x4A, 0x57};
  static unsigned char lock[]  = {0x00, 0x00};
  unsigned char data2[2], data4[4], result, i;
  unsigned char data14[14];

  MSG2("%s+",__func__);
  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  
  MSG("%s, write RCOMP %02X %02X",__func__,rcomp[0],rcomp[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &rcomp[0], sizeof(rcomp));
  msleep(10);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x00, &data2[0], sizeof(data2));

  
  MSG("%s, unlock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &unlock[0], sizeof(unlock));

  
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, backup RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w4_0E[0],w4_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w2_0E[0], sizeof(w2_0E));

  
  MSG("%s, write RCOMP Max %02X %02X",__func__,w2_0C[0],w2_0C[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &w2_0C[0], sizeof(w2_0C));

  
  MSG("%s, write Model",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x40, &w3_40[0], sizeof(w3_40));
  gag_write_i2c(luna_bat.i2c_addr, 0x50, &w3_50[0], sizeof(w3_50));
  gag_write_i2c(luna_bat.i2c_addr, 0x60, &w3_60[0], sizeof(w3_60));
  gag_write_i2c(luna_bat.i2c_addr, 0x70, &w3_70[0], sizeof(w3_70));
  msleep(190);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w4_0E[0],w4_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w4_0E[0], sizeof(w4_0E));

  
  for(i=0; i<3; i++)
  {
    msleep(150);
    gag_read_i2c(luna_bat.i2c_addr, 0x04, &data2[0], sizeof(data2));
    result = (data2[0] >= SOC_CHECK_A && data2[0] <= SOC_CHECK_B) ? TRUE : FALSE;
    MSG2("%s, TestSOC = %d %d ## %s ##",__func__,data2[0],data2[1],result ? "PASS":"FAIL");
    if(result == TRUE)
      break;
  }

  
  MSG("%s, restore RCOMP, OCV",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  mdelay(10);

  
  MSG("%s, lock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &lock[0], sizeof(lock));
  msleep(400);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data4[0], sizeof(data4));
  MSG2("%s, VCELL = %d (%02X %02X), SOC = %d (%02X %02X)",__func__,
    ((data4[0]<<4)+(data4[1]>>4))*5/4, data4[0], data4[1],
    data4[2]>>1, data4[2], data4[3]);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  if(result == TRUE)
  {
    
    luna_bat.bat_capacity_history[2] = data4[2]>>1;
    luna_bat.bat_capacity_history[1] = data4[2]>>1;
    luna_bat.bat_capacity_history[0] = data4[2]>>1;
    MSG2("%s- ### PASS ###",__func__);
    return TRUE;
  }
  else
  {
    MSG2("%s- ### FAIL ###",__func__);
    return FALSE;
  }
}

static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  unsigned int  i;
  unsigned char gag_08_0D[6];
  
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }

  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if(i==CHG_USUS)
      continue;
    luna_eng_data.PinValue[i] = pin_value(luna_bat.pin[i]);
  }

  
  luna_eng_data.cap   = luna_bat.bat_capacity;  
  luna_eng_data.volt  = luna_bat.bat_vol;       

  gag_read_i2c(luna_bat.i2c_addr, 0x08, &gag_08_0D[0], sizeof(gag_08_0D));
  luna_eng_data.ver   = (gag_08_0D[0]<<8) + gag_08_0D[1]; 
  luna_eng_data.rcomp = (gag_08_0D[4]<<8) + gag_08_0D[5]; 

  
  luna_eng_data.temp  = luna_bat.bat_temp;      
  luna_eng_data.chg_vol = luna_bat.chg_vol;     
  luna_eng_data.state.ac_det  = luna_bat.ac_pmu_det;
  luna_eng_data.state.usb_det = luna_bat.usb_pmu_det;
  
  luna_eng_data.state.ac      = luna_bat.ac_online;
  luna_eng_data.state.usb     = luna_bat.usb_online;
  
  if(luna_bat.usb_current == USB_STATUS_USB_0)
    luna_eng_data.state.usb_ma = 0;
  else if(luna_bat.usb_current == USB_STATUS_USB_100)
    luna_eng_data.state.usb_ma = 1;
  else if(luna_bat.usb_current == USB_STATUS_USB_500)
    luna_eng_data.state.usb_ma = 2;
  else if(luna_bat.usb_current == USB_STATUS_USB_1000 ||
          luna_bat.usb_current == USB_STATUS_USB_2000 )
  {
    if(luna_eng_data.PinValue[CHG_LIMD] == luna_bat.pin[CHG_LIMD].pin_en) 
      luna_eng_data.state.usb_ma = 4;
    else  
      luna_eng_data.state.usb_ma = 3;
  }
  else
    luna_eng_data.state.usb_ma = 7;  

  luna_eng_data.end = '\0';

  memcpy(buf,&luna_eng_data,sizeof(luna_eng_data));
  return sizeof(luna_eng_data);
}

static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  unsigned char bufLocal[LUNA_BAT_BUF_LENGTH];

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
        bat_log_on1 = 0;
        bat_log_on2 = 1;
        bat_log_on3 = 0;
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Dynamic Log 1 On");
        bat_log_on1 = 1;
      }
      else if(bufLocal[1]=='3')
      {
        MSG2("Dynamic Log 3 On");
        bat_log_on3 = 1;
      }
      break;

    
    
    case 'f':
      if(count<2) break;
      MSG2("## Set chg_ctl mode = %c", bufLocal[1]);
      if(bufLocal[1]=='0')
      {
        
        luna_bat.chg_ctl = CHG_CTL_NONE;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else if(bufLocal[1]=='1')
      {
        
        luna_bat.chg_ctl = CHG_CTL_USB500_DIS;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else if(bufLocal[1]=='2')
      {
        luna_bat.chg_ctl = CHG_CTL_AC2A_EN;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else
      {
        MSG2("chg_ctl mode = %d", luna_bat.chg_ctl);
      }
      break;

    
    
    case 'g':
      
      if(bufLocal[1]=='i')
      {
        
        luna_bat_gauge_init();
      }
      else if(bufLocal[1]=='v')
      {
        
        luna_bat_gauge_verify();
      }
      else if(bufLocal[1]=='r')
      {
        
        luna_bat_gauge_reset();
      }
      break;

    
    
    case 'p':
      luna_bat_pmu_test(bufLocal, count);
      break;

    case 'r': 
      luna_bat_datacard_reset();
      break;

    case 'm': 
      MSG2("3G datacard disable RF+");
      ventana_huawei_power_up_sequence(0);
      MSG2("3G datacard disable RF-");
      break;

    
    
    case 'i':
      luna_bat_i2c_test(bufLocal, count, luna_bat_gauge_client);
      break;

    case 'u':
      if(bufLocal[1]=='0')
      {
        MSG2("USB 0");
        luna_bat.usb_current = USB_STATUS_USB_0;
        luna_bat.usb_online  = 0;
      }
      else 
      {
        MSG2("USB 2000");
        luna_bat.usb_current = USB_STATUS_USB_2000;
        luna_bat.usb_online  = 1;
      }
      break;
  }

  return count;
}


void luna_bat_update_usb_status(int flag)
{
  
  if(flag & USB_STATUS_USB_0)
  {
    luna_bat.usb_current = USB_STATUS_USB_0;    MSG3("Set [USB 0]");
    luna_bat.usb_online  = 0;
    MSG2("Set [USB 0]");
  }
  else if(flag & USB_STATUS_USB_100)
  {
    luna_bat.usb_current = USB_STATUS_USB_100;  MSG3("Set [USB 100]");
    luna_bat.usb_online  = 1;
    MSG2("Set [USB 100]");
  }
  else if(flag & USB_STATUS_USB_500)
  {
    luna_bat.usb_current = USB_STATUS_USB_500;  MSG3("Set [USB 500]");
    luna_bat.usb_online  = 1;
    MSG2("Set [USB 500]");
  }
  else if(flag & USB_STATUS_USB_1000)
  {
    luna_bat.usb_current = USB_STATUS_USB_1000; MSG3("Set [USB 1000]");
    luna_bat.usb_online  = 1;
    MSG2("Set [USB 1000]");
  }
  else if(flag & USB_STATUS_USB_2000)
  {
    luna_bat.usb_current = USB_STATUS_USB_2000; MSG3("Set [USB 2000]");
    luna_bat.usb_online  = 1;
    MSG2("Set [USB 2000]");
  }

  if(luna_bat.inited)
  {
    int ret;
    
    atomic_set(&luna_bat.read_again, 3);
    ret = queue_work(luna_bat_wqueue, &luna_bat_work);
    if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
    
  }
}
EXPORT_SYMBOL(luna_bat_update_usb_status);
void luna_bat_get_info(struct luna_bat_info_data* binfo)
{
  binfo->bat_status   = luna_bat.bat_status;
  binfo->bat_health   = luna_bat.bat_health;
  binfo->bat_capacity = luna_bat.bat_capacity;
  binfo->bat_vol      = luna_bat.bat_vol;
  binfo->bat_temp     = luna_bat.bat_temp;

  
  binfo->ac_online    = luna_bat.ac_online;
  binfo->usb_online   = luna_bat.usb_online;
  binfo->usb_current  = luna_bat.usb_current;    

  
  binfo->ac_pmu_det   = luna_bat.ac_pmu_det;
  binfo->usb_pmu_det  = luna_bat.usb_pmu_det;
}
EXPORT_SYMBOL(luna_bat_get_info);
int luna_bat_get_online(void)
{
  return ((luna_bat.ac_pmu_det << 8) | luna_bat.usb_pmu_det);
}
EXPORT_SYMBOL(luna_bat_get_online);
static int luna_bat_soc_filter(int input)
{
  static const int middle[] = {1,0,2,0,0,2,0,1};
  int old[3], index = 0;
  luna_bat.bat_capacity_history[2] = luna_bat.bat_capacity_history[1];
  luna_bat.bat_capacity_history[1] = luna_bat.bat_capacity_history[0];
  luna_bat.bat_capacity_history[0] = input;
  old[2] = luna_bat.bat_capacity_history[2];
  old[1] = luna_bat.bat_capacity_history[1];
  old[0] = luna_bat.bat_capacity_history[0];
  if( old[0] > old[1] ) index += 4;
  if( old[1] > old[2] ) index += 2;
  if( old[0] > old[2] ) index ++;
  if(old[middle[index]] > 100)
    return 100;
  else
    return old[middle[index]];
}
static int luna_bat_adc_to_temp(u_int32_t adc)
{
  struct {
    int temp;
    int volt;
  } static const mapping_table [] = {
    { -400, 2093},  { -350, 2060},  { -300, 2021},  { -250, 1974},  { -200, 1918},
    { -150, 1854},  { -100, 1781},  {  -50, 1698},  {    0, 1608},  {   50, 1512},
    {  100, 1412},  {  150, 1308},  {  200, 1203},  {  250, 1100},  {  300,  998},
    {  350,  901},  {  400,  810},  {  450,  725},  {  500,  646},  {  550,  574},
    {  600,  509},  {  650,  452},  {  700,  400},  {  750,  355},  {  800,  314},
    {  850,  278},  {  900,  247},  {  950,  219},  { 1000,  195},  { 1050,  173},
    { 1100,  155},  { 1150,  138},  { 1200,  123},  { 1250,  110},  { 1250,    0},
  };
  int i, tmp;
  for(i=0; i<ARRAY_SIZE(mapping_table); i++)
  {
    if(adc >= mapping_table[i].volt)
    {
      if(i == 0)
      {
        return mapping_table[0].temp;
      }
      else
      {
        tmp = (adc - mapping_table[i].volt) * 1024 / (mapping_table[i-1].volt - mapping_table[i].volt);
        return (mapping_table[i-1].temp - mapping_table[i].temp) * tmp / 1024 + mapping_table[i].temp;
      }
    }
  }
  return 1250;  
}
static u_int32_t luna_bat_get_chg_voltage(void)
{
  unsigned int ADC_1, DCinVolt;
  tps6586x_adc_read(TPS6586X_ADC_1, &ADC_1);
  DCinVolt = ADC_1 * 2600 / 256;  
  return DCinVolt;
}



extern int tp_cable_state(uint32_t send_cable_flag);
static void luna_bat_work_func_poweroff(struct work_struct *work)
{
  static int inited = 0;
  int count;
  
  

  MSG2("%s+",__func__);
  
  if(!inited)
  {
    inited = 1;
#if 0
    ret = sched_setscheduler_nocheck(current, SCHED_FIFO, &s);
    MSG2("%s, set real time %s",__func__,ret?"FAIL":"PASS");
    MSG2("%s, rt_task = %d",__func__,rt_task(current));
#endif
    goto exit;
  }

  
  wake_lock(&luna_bat.wlock_poweroff);
  count = 0;
  while(1)
  {
    MSG2("%s, (%02d) vol=%d, ac=%d(%d), usb=%d(%d), chg=%d",__func__,count,
      luna_bat.bat_vol, luna_bat.ac_online, luna_bat.ac_pmu_det,
      luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol);
    if(count > 15 && luna_bat.bat_vol < 2900)
    {
      MSG2("%s, 15 sec and vol < 2900",__func__);
      
      kernel_power_off();
    }
    if(count > 20 && luna_bat.bat_vol < 3000)
    {
      MSG2("%s, 20 sec and vol < 3000",__func__);
      
      kernel_power_off();
    }
    if(count > 60)
    {
      MSG2("%s, 60 sec",__func__);
      
      kernel_power_off();
    }    
    count ++;
    msleep(1000);
  }

exit:
  
  
  
  MSG2("%s-",__func__);
}
static void luna_bat_work_func(struct work_struct *work)
{
  static char ac_online_old = 0;
  static char usb_online_old = 0;
  static int usb_current_old = USB_STATUS_USB_0;
  static int bat_status_old = 0;
  static int bat_health_old = 0;
  static int bat_soc_old = 255;
  static int bat_aidl_done = 0;
  static int touch_online_old = 2;

  unsigned long flags = 0;
  int i,status_changed = 0, online_temp, ret;

  unsigned int volt = 3800, soc = 50;
  unsigned int chg_in_old;

  
  unsigned int PinValue[CHG_MAX];

  unsigned char  gag_02_05[4], gag_0c_0d[2];
  int rcomp = 0xFFFF;
  static int gag_init_retry = 3;

  MSG("%s+, read_again=%d",__func__,atomic_read(&luna_bat.read_again));

  if(!luna_bat.inited) 
  {
    MSG2("## Cancel Work, driver not inited! ##");
    return;
  }

  
  
  while(gag_init_retry > 0)
  {
    if(gag_init_retry == 3)
    {
      if(luna_bat_gauge_verify() == TRUE)
      {
        MSG2("%s Gauge verify PASS!",__func__);
        gag_init_retry = 0;
        break;
      }
      else
      {
        
        
      }
    }
    if(luna_bat_gauge_init() == TRUE)
      gag_init_retry = 0;
    else
      gag_init_retry --;
  }

  

  
  
  
  {
    
    
    {
      unsigned char ac, usb;
      for(i=0; i<5; i++)  
      {
        ret = tps6586x_ac_usb_read(&ac, &usb);
        if(!ret)
          break;
        else
          msleep(10);
      }
      if(ret)
      {
        luna_bat.ac_pmu_det   = 0;
        luna_bat.usb_pmu_det  = 0;
        MSG2("%s tps6586x_ac_usb_read Fail",__func__);
      }
      else
      {
        luna_bat.ac_pmu_det   = ac;
        luna_bat.usb_pmu_det  = usb;
        MSG("%s tps6586x_ac_usb_read, ac_det=%d, usb_det=%d",__func__,ac,usb);
      }
    }
    
    online_temp = (luna_bat.ac_pmu_det | luna_bat.usb_pmu_det)? 1:0;
    if(touch_online_old != online_temp)
    {
      touch_online_old = online_temp;
      tp_cable_state(((luna_bat.ac_pmu_det << 8) | luna_bat.usb_pmu_det));
    }

    
    
    {
      unsigned int adc5;
      for(i=0; i<5; i++)  
      {
        ret = tps6586x_adc_read(TPS6586X_ADC_5, &adc5); 
        if(!ret)
          break;
        else
          msleep(10);
      }
      if(ret)
      {
        luna_bat.bat_temp = 250;
        MSG2("%s tps6586x_adc_read Fail",__func__);
      }
      else
      {
        luna_bat.bat_temp = luna_bat_adc_to_temp(adc5);
        MSG("%s tps6586x_adc_read, temp=%d",__func__,luna_bat.bat_temp);
      }
    }
    
    
    luna_bat.chg_vol = luna_bat_get_chg_voltage();
    
    
    gag_read_i2c(luna_bat.i2c_addr, 0x02, &gag_02_05[0], sizeof(gag_02_05));
    gag_read_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
    if(!luna_bat.gagic_err)
    {
      volt  = ((gag_02_05[0]<<4) + (gag_02_05[1]>>4))*5/4;
      if(gag_02_05[2] == 1)     
        soc = 1;
      else
        soc = gag_02_05[2]>>1;  
      luna_bat.bat_vol = volt;
      luna_bat.bat_capacity = luna_bat_soc_filter(soc);
      
      if(luna_bat.bat_temp >= 200)
      {
        rcomp = 97 - (luna_bat.bat_temp - 200) * 21 / 200;  
      }
      else
      {
        rcomp = 97 - (luna_bat.bat_temp - 200) * 79 / 100;  
      }
      if(rcomp > 0xFF)
        rcomp = 0xFF;
      else if(rcomp < 0)
        rcomp = 0x00;
      if(rcomp != gag_0c_0d[0])
      {
        MSG("RCOMP: %02X->%02X (t=%d)",gag_0c_0d[0],rcomp,luna_bat.bat_temp);
        gag_0c_0d[0] = rcomp;
        gag_write_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
      }
      if(TST_BIT(gag_0c_0d[1],5) && (online_temp || soc > 3)) 
      {
        PinValue[CHG_GLOW] = pin_value(luna_bat.pin[CHG_GLOW]);
        MSG2("RCOMP: %02X->%02X (t=%d), GLOW=%d, CLEAR ALERT %02X",
          gag_0c_0d[0],rcomp,luna_bat.bat_temp,PinValue[CHG_GLOW],gag_0c_0d[1]);
        CLR_BIT(gag_0c_0d[1],5);  
        gag_0c_0d[0] = rcomp;
        gag_write_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
      }
    }
  }

  
  
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if(i!=CHG_USUS)
      PinValue[i] = pin_value(luna_bat.pin[i]);
  }
  MSG3("[%s %s %s %s %s %s %s %s] volt=%04d, soc=%03d, temp=%03d, rc=%02X chg=%04d, acd=%d, usbd=%d (%02X%02X)",
    PinValue[CHG_CEN]  == luna_bat.pin[CHG_CEN].pin_en  ?"ON ":"OFF" ,
    PinValue[CHG_DCM]  == luna_bat.pin[CHG_DCM].pin_en  ?
    (PinValue[CHG_LIMD] == luna_bat.pin[CHG_LIMD].pin_en ?"DC_2A__" : "DC_1A__") :
    PinValue[CHG_IUSB] == luna_bat.pin[CHG_IUSB].pin_en ?"USB_500":"USB_100" ,
    PinValue[CHG_LIMB] == luna_bat.pin[CHG_LIMB].pin_en ?"BAT_2A_":"BAT_500" ,
    PinValue[CHG_OTG]  == luna_bat.pin[CHG_OTG].pin_en  ?"OTG":"___" ,
    PinValue[CHG_CHG]  == luna_bat.pin[CHG_CHG].pin_en  ?"CHG":"___" ,
    PinValue[CHG_DOK]  == luna_bat.pin[CHG_DOK].pin_en  ?"DOK":"___" ,
    PinValue[CHG_FLT]  == luna_bat.pin[CHG_FLT].pin_en  ?"FLT":"___" ,
    PinValue[CHG_GLOW] == luna_bat.pin[CHG_GLOW].pin_en ?"LOW":"___" ,
    luna_bat.bat_vol, soc, luna_bat.bat_temp, rcomp, luna_bat.chg_vol, luna_bat.ac_pmu_det, luna_bat.usb_pmu_det,
    gag_02_05[2],gag_02_05[3]);

  
  
  
  
  luna_bat.bat_present = 1;

  
  
  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en)
  {
    if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en && 
      PinValue[CHG_CHG] != luna_bat.pin[CHG_CHG].pin_en &&  
      luna_bat.bat_vol > 4100)  
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_FULL;
    }
    else if(PinValue[CHG_CHG] == luna_bat.pin[CHG_CHG].pin_en)
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_CHARGING;
    }
    else
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
    }
  }
  else
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
  }

  
  
  
  if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det) 
  {
    if(bat_health_old == POWER_SUPPLY_HEALTH_OVERHEAT &&  
      luna_bat.bat_temp > 450)
    {
      luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
    }
    else if(bat_health_old == POWER_SUPPLY_HEALTH_COLD && 
      luna_bat.bat_temp < 50)
    {
      luna_bat.bat_health = POWER_SUPPLY_HEALTH_COLD;
    }
    else
    {
      if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en) 
      {
        if(PinValue[CHG_CHG] == luna_bat.pin[CHG_CHG].pin_en) 
        {
          luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
        }
        else  
        {
          if(PinValue[CHG_FLT] == luna_bat.pin[CHG_FLT].pin_en) 
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_DEAD;
          }
          
          else if(luna_bat.bat_temp > 450)  
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
          }
          else if(luna_bat.bat_temp < 50)   
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_COLD;
          }
          else  
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
          }
        }
      }
      else  
      {
        luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
      }
    }
  }
  else  
  {
    luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
  }

  
  
  
  {
    chg_in_old = luna_bat.chg_in;
    switch(luna_bat.chg_in)
    {
      case CHG_IN_NONE:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          luna_bat.chg_in = CHG_IN_DOK_DET;
        }
        else  
        {
          if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_ERROR;
          }
        }
        break;
      case CHG_IN_ERROR:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          luna_bat.chg_in = CHG_IN_DOK_DET;
        }
        else  
        {
          if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_NONE;
          }
        }
        break;
      case CHG_IN_DOK_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(luna_bat.ac_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_AC_DET;
          }
          else if(luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_USB_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
      case CHG_IN_AC_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_DOK_DET;
          }
          else if(!luna_bat.ac_pmu_det && luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_USB_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
      case CHG_IN_USB_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(luna_bat.ac_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_AC_DET;
          }
          else if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_DOK_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
    }
    
    if(luna_bat.chg_in == CHG_IN_AC_DET)
      luna_bat.ac_online = 1;
    else
      luna_bat.ac_online = 0;
    
    if(chg_in_old != luna_bat.chg_in)
    {
      MSG3("## %s ##", chg_in_name[luna_bat.chg_in]);
    }
  }

  
  
  
  if(ac_online_old != luna_bat.ac_online)
  {
    MSG2("## ac_online: %d -> %d (%d)",ac_online_old,luna_bat.ac_online,luna_bat.ac_pmu_det);
    ac_online_old = luna_bat.ac_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_online_old != luna_bat.usb_online)
  {
    MSG2("## usb_online: %d -> %d (%d)",usb_online_old,luna_bat.usb_online,luna_bat.usb_pmu_det);
    usb_online_old = luna_bat.usb_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_current_old != luna_bat.usb_current)
  {
    MSG2("## usb_current: %d -> %d",
      usb_current_old==USB_STATUS_USB_0? 0:
      usb_current_old==USB_STATUS_USB_100? 100:
      usb_current_old==USB_STATUS_USB_500? 500:
      usb_current_old==USB_STATUS_USB_1000? 1000:
      usb_current_old==USB_STATUS_USB_2000? 2000: 9999  ,
      luna_bat.usb_current==USB_STATUS_USB_0? 0:
      luna_bat.usb_current==USB_STATUS_USB_100? 100:
      luna_bat.usb_current==USB_STATUS_USB_500? 500:
      luna_bat.usb_current==USB_STATUS_USB_1000? 1000:
      luna_bat.usb_current==USB_STATUS_USB_2000? 2000: 9999
      );
    usb_current_old = luna_bat.usb_current;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_status_old != luna_bat.bat_status)
  {
    MSG2("## bat_status: %s -> %s",status_text[bat_status_old],status_text[luna_bat.bat_status]);
    bat_status_old = luna_bat.bat_status;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_health_old != luna_bat.bat_health)
  {
    MSG2("## bat_health: %s -> %s",health_text[bat_health_old],health_text[luna_bat.bat_health]);
    bat_health_old = luna_bat.bat_health;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_soc_old !=  soc)
  {
    
    
    MSG2("## bat_cap=%d(%d), vol=%d, temp=%d, ac=%d(%d), usb=%d(%d) %dmA, chg=%d",
      luna_bat.bat_capacity, soc, luna_bat.bat_vol, luna_bat.bat_temp,
      luna_bat.ac_online, luna_bat.ac_pmu_det, luna_bat.usb_online, luna_bat.usb_pmu_det,
      luna_bat.usb_current==USB_STATUS_USB_0? 0:
      luna_bat.usb_current==USB_STATUS_USB_100? 100:
      luna_bat.usb_current==USB_STATUS_USB_500? 500:
      luna_bat.usb_current==USB_STATUS_USB_1000? 1000:
      luna_bat.usb_current==USB_STATUS_USB_2000? 2000: 9999,
      luna_bat.chg_vol  );

    bat_soc_old = soc;
    status_changed ++;
    
  }

  if(luna_bat.bat_status == POWER_SUPPLY_STATUS_FULL)
  {
    luna_bat.bat_capacity = 100;
  }
  else if(luna_bat.bat_capacity >= 100)
  {
    if(luna_bat.bat_status == POWER_SUPPLY_STATUS_CHARGING)
      luna_bat.bat_capacity = 99;
    else
      luna_bat.bat_capacity = 100;
  }
  else if(luna_bat.bat_vol >= 3450 && luna_bat.bat_capacity == 0)
  {
    luna_bat.bat_capacity = 1;
  }

  
  if((soc <= 0 && luna_bat.bat_vol < 3450) ||
    (soc <= 4 && luna_bat.bat_vol < 3250) ||
    (luna_bat.bat_vol < 3200) )
  {
    if(luna_bat.bat_low_count > 10)
    {
      if(luna_bat.bat_low_count >= 11 &&  
        luna_bat.bat_low_count <= 17)
      {
        MSG2("## bat_capacity: 0, vol: %d, temp: %d (count = %d) ac=%d, usb=%d",
          luna_bat.bat_vol, luna_bat.bat_temp, luna_bat.bat_low_count, luna_bat.ac_online, luna_bat.usb_online);
        status_changed ++;
      }
      if(!luna_bat.poweroff_started) 
      {
        MSG2("## bat_capacity: 0, vol: %d, temp: %d (count = %d) start work", luna_bat.bat_vol, luna_bat.bat_temp, luna_bat.bat_low_count);
        luna_bat.poweroff_started = 1;
        queue_work(luna_bat_wqueue_poweroff, &luna_bat_work_poweroff);
        status_changed ++;
      }
      luna_bat.bat_low_count ++;
      luna_bat.bat_capacity = 0;
    }
    else
    {
      MSG2("## bat_capacity: %d, vol: %d, temp: %d (count = %d)",
        luna_bat.bat_capacity, luna_bat.bat_vol, luna_bat.bat_temp, luna_bat.bat_low_count);
      if(luna_bat.bat_vol < 3200)
        luna_bat.bat_low_count += 6;
      else if(soc <= 4 && luna_bat.bat_vol < 3250)
        luna_bat.bat_low_count += 2;
      else
        luna_bat.bat_low_count ++;
      if(soc == 0)
        luna_bat.bat_capacity = 1;
      atomic_set(&luna_bat.read_again, 3);
    }
  }
  else
  {
    luna_bat.bat_low_count = 0;
    
  }


  
  
  
  {
    int wake = 0;
    if(luna_bat.ac_online || luna_bat.usb_online)           
      wake |= 1;
    if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)         
      wake |= 1;
    if(PinValue[CHG_GLOW] == luna_bat.pin[CHG_GLOW].pin_en) 
      wake |= 1;
    if((soc <= 0 && luna_bat.bat_vol < 3450) ||             
      (soc <= 4 && luna_bat.bat_vol < 3250) ||
      (luna_bat.bat_vol < 3200) )
    {
      wake |= 1;
    }
    if(wake)
    {
      if(!luna_bat.wake_flag)
      {
        luna_bat.wake_flag = 1;
        wake_lock(&luna_bat.wlock);
        MSG2("## wake_lock: 0->1, vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d",
          luna_bat.bat_vol, PinValue[CHG_GLOW], luna_bat.ac_online, luna_bat.ac_pmu_det,
          luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, soc);
      }
    }
    else
    {
      if(luna_bat.wake_flag)
      {
        wake_lock_timeout(&luna_bat.wlock, HZ*2);
        luna_bat.wake_flag = 0;
        MSG2("## wake_lock: 1->0, vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d",
          luna_bat.bat_vol, PinValue[CHG_GLOW], luna_bat.ac_online, luna_bat.ac_pmu_det,
          luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, soc);
      }
    }
  }
  

  
  
  {
    
    
    if((chg_in_old == CHG_IN_AC_DET && luna_bat.chg_in != CHG_IN_AC_DET) ||
      (luna_bat.chg_in == CHG_IN_ERROR))  
    {
      
      pin_disable(luna_bat.pin[CHG_OTG]);
      msleep(90);
      pin_enable(luna_bat.pin[CHG_OTG]);
      MSG3("## Reset AC Detect");
    }

    
    
    if(PinValue[CHG_FLT] == luna_bat.pin[CHG_FLT].pin_en)
    {
      if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en) 
      {
        MSG2("## Reset FLT (Charging timeout)");
        pin_disable(luna_bat.pin[CHG_CEN]);
        msleep(15);
        pin_enable(luna_bat.pin[CHG_CEN]);
      }
    }

    
    
    if(luna_bat.chg_ctl == CHG_CTL_NONE)
    {
      
      
      switch(luna_bat.chg_bat_current)
      {
        case CHG_BAT_CURRENT_HIGH:
          if(luna_bat.bat_temp < 150)
          {
            MSG2("## BAT_CURRENT: High -> Low (temp = %d)", luna_bat.bat_temp);
            luna_bat.chg_bat_current = CHG_BAT_CURRENT_LOW;
            pin_disable(luna_bat.pin[CHG_LIMB]);
          }
          else if(PinValue[CHG_LIMB] != luna_bat.pin[CHG_LIMB].pin_en)
          {
            pin_enable(luna_bat.pin[CHG_LIMB]);
          }
          break;
        case CHG_BAT_CURRENT_LOW:
          if(luna_bat.bat_temp > 150)
          {
            MSG2("## BAT_CURRENT: Low -> High (temp = %d)", luna_bat.bat_temp);
            luna_bat.chg_bat_current = CHG_BAT_CURRENT_HIGH;
            pin_enable(luna_bat.pin[CHG_LIMB]);
          }
          else if(PinValue[CHG_LIMB] != !luna_bat.pin[CHG_LIMB].pin_en)
          {
            pin_disable(luna_bat.pin[CHG_LIMB]);
          }
          break;
      }

      
      
      if(luna_bat.chg_in == CHG_IN_NONE ||
        luna_bat.chg_in == CHG_IN_DOK_DET)
      {
        if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en  ||  
           PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en  ||  
           PinValue[CHG_IUSB] != !luna_bat.pin[CHG_IUSB].pin_en ||  
           PinValue[CHG_LIMD] != !luna_bat.pin[CHG_LIMD].pin_en )   
        {
          MSG3("## USB_100 + DIS");
          pin_disable(luna_bat.pin[CHG_CEN]);
          pin_disable(luna_bat.pin[CHG_DCM]);
          pin_disable(luna_bat.pin[CHG_IUSB]);
          pin_disable(luna_bat.pin[CHG_LIMD]);
        }
        bat_aidl_done = 0;
      }
      
      
      else if(luna_bat.chg_in == CHG_IN_AC_DET)
      {
        if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
        {
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + EN");
            pin_enable(luna_bat.pin[CHG_CEN]);
            pin_enable(luna_bat.pin[CHG_DCM]);
          }
        }
        else
        {
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + DIS");
            pin_disable(luna_bat.pin[CHG_CEN]);
            pin_enable(luna_bat.pin[CHG_DCM]);
          }
        }
        
        if(!bat_aidl_done ||
          (chg_in_old != CHG_IN_AC_DET && luna_bat.chg_in == CHG_IN_AC_DET))
        {
          luna_bat.chg_vol = luna_bat_get_chg_voltage();
          if(luna_bat.chg_vol < 4400)
          {
            
            {
              MSG2("## AC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
              pin_disable(luna_bat.pin[CHG_LIMD]);
            }
          }
          else
          {
            
            {
              MSG2("## AC dcin=%dmV (LIMD set 2A)",luna_bat.chg_vol);
              pin_enable(luna_bat.pin[CHG_LIMD]);
            }
            msleep(15);
            luna_bat.chg_vol = luna_bat_get_chg_voltage();
            if(luna_bat.chg_vol < 4400)
            {
              MSG2("## AC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
              pin_disable(luna_bat.pin[CHG_LIMD]);
            }
            else
            {
              MSG2("## AC dcin=%dmV (LIMD set 2A) after",luna_bat.chg_vol);
            }
          }
          bat_aidl_done = 1;
        }
      }
      
      
      else if(luna_bat.chg_in == CHG_IN_USB_DET)
      {
        switch(luna_bat.usb_current)
        {
          case USB_STATUS_USB_1000:
          case USB_STATUS_USB_2000:
            if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
            {
              if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB_HC + EN");
                pin_enable(luna_bat.pin[CHG_CEN]);
                pin_enable(luna_bat.pin[CHG_DCM]);
              }
            }
            else
            {
              if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB_HC + DIS");
                pin_disable(luna_bat.pin[CHG_CEN]);
                pin_enable(luna_bat.pin[CHG_DCM]);
              }
            }
            
            if(!bat_aidl_done ||
              (chg_in_old != CHG_IN_USB_DET && luna_bat.chg_in == CHG_IN_USB_DET))
            {
              luna_bat.chg_vol = luna_bat_get_chg_voltage();
              if(luna_bat.chg_vol < 4400)
              {
                
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
                  pin_disable(luna_bat.pin[CHG_LIMD]);
                }
              }
              else
              {
                
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 2A)",luna_bat.chg_vol);
                  pin_enable(luna_bat.pin[CHG_LIMD]);
                }
                msleep(15);
                luna_bat.chg_vol = luna_bat_get_chg_voltage();
                if(luna_bat.chg_vol < 4400)
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
                  pin_disable(luna_bat.pin[CHG_LIMD]);
                }
                else
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 2A) after",luna_bat.chg_vol);
                }
              }
              bat_aidl_done = 1;
            }
            break;
          
          
          case USB_STATUS_USB_500:
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
            {
              MSG3("## USB_500 + DIS");
              pin_disable(luna_bat.pin[CHG_CEN]);
              pin_disable(luna_bat.pin[CHG_DCM]);
              pin_enable(luna_bat.pin[CHG_IUSB]);
            }
            bat_aidl_done = 0;
            break;
          case USB_STATUS_USB_0:
          case USB_STATUS_USB_100:
          default:
            
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != !luna_bat.pin[CHG_IUSB].pin_en)  
            {
              MSG3("## USB_100 + Disable charging");
              pin_disable(luna_bat.pin[CHG_CEN]);
              pin_disable(luna_bat.pin[CHG_DCM]);
              pin_disable(luna_bat.pin[CHG_IUSB]);
            }
            bat_aidl_done = 0;
            break;
        }
      }
    }
    
    
    else
    {
      
      
      if(PinValue[CHG_LIMD] != luna_bat.pin[CHG_LIMD].pin_en) 
      {
        MSG2("## CTRL: HC_CURRENT: 2A");
        pin_enable(luna_bat.pin[CHG_LIMD]);
      }

      
      
      if(PinValue[CHG_LIMB] != luna_bat.pin[CHG_LIMB].pin_en) 
      {
        MSG2("## CTRL: BAT_CURRENT: Low -> High (temp = %d)", luna_bat.bat_temp);
        pin_enable(luna_bat.pin[CHG_LIMB]);
        luna_bat.chg_bat_current = CHG_BAT_CURRENT_HIGH;
      }

      
      
      switch(luna_bat.chg_ctl)
      {
        case CHG_CTL_USB500_DIS:
          if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + DIS");
            pin_disable(luna_bat.pin[CHG_CEN]);
            pin_disable(luna_bat.pin[CHG_DCM]);
            pin_enable(luna_bat.pin[CHG_IUSB]);
          }
          break;
        case CHG_CTL_USB500_EN:
          if(PinValue[CHG_CEN]  != luna_bat.pin[CHG_CEN].pin_en  || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + EN");
            pin_enable(luna_bat.pin[CHG_CEN]);
            pin_disable(luna_bat.pin[CHG_DCM]);
            pin_enable(luna_bat.pin[CHG_IUSB]);
          }
          break;
        case CHG_CTL_AC2A_DIS:
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||  
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + DIS");
            pin_disable(luna_bat.pin[CHG_CEN]);
            pin_enable(luna_bat.pin[CHG_DCM]);
          }
          break;
        case CHG_CTL_AC2A_EN:
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en ||   
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + EN");
            pin_enable(luna_bat.pin[CHG_CEN]);
            pin_enable(luna_bat.pin[CHG_DCM]);
          }
          break;
      }
    }
  }

  
  
  
  spin_lock_irqsave(&luna_bat_irq_lock, flags);
  
  if(!atomic_read(&luna_bat.pin[CHG_DOK].intr_count))
  {
    MSG3("## DOK_INTR ON");
    atomic_set(&luna_bat.pin[CHG_DOK].intr_count, 1);
    enable_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio));
    ret = set_irq_wake(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio), 1);
    if(ret) {MSG2("%s, pin[DOK ] set_irq_wake 1, Fail = %d", __func__, ret);}
  }
  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en &&
    !atomic_read(&luna_bat.pin[CHG_CHG].intr_count))
  {
    MSG3("## CHG_INTR ON");
    atomic_set(&luna_bat.pin[CHG_CHG].intr_count, 1);
    enable_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_CHG].gpio));
  }
  else if(PinValue[CHG_CEN] == !luna_bat.pin[CHG_CEN].pin_en &&
    atomic_read(&luna_bat.pin[CHG_CHG].intr_count) )
  {
    MSG3("## CHG_INTR OFF");
    atomic_set(&luna_bat.pin[CHG_CHG].intr_count, 0);
    disable_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_CHG].gpio));
  }
  
  
  spin_unlock_irqrestore(&luna_bat_irq_lock, flags);

  luna_bat.jiff_property_valid_time = jiffies + luna_bat.jiff_property_valid_interval;

  if(status_changed ||
    atomic_read(&luna_bat.read_again) == 1)
  {
    
    
    power_supply_changed(&luna_bat.psy_bat);
  }

  
  
  if(atomic_read(&luna_bat.suspend_flag) && !luna_bat.ac_pmu_det && luna_bat.usb_pmu_det)
  {
    
    
    del_timer_sync(&luna_timer);
  }
  else
  {
    if(atomic_read(&luna_bat.read_again) > 0)
    {
      atomic_dec(&luna_bat.read_again);
      mod_timer(&luna_timer, jiffies + 1*HZ);
    }
    else
    {
      mod_timer(&luna_timer, jiffies + luna_bat.jiff_polling_interval);
    }
  }

  
  MSG("%s-, read_again=%d", __func__,atomic_read(&luna_bat.read_again));
}
static void luna_bat_timer_func(unsigned long temp)
{
  MSG("%s",__func__);
  if(luna_bat.inited)
  {
    if(!atomic_read(&luna_bat.suspend_flag) || luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)
    {
      int ret;
      ret = queue_work(luna_bat_wqueue, &luna_bat_work);
      if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
    }
  }
}



static irqreturn_t luna_bat_chg_irq_handler(int irq, void *args)
{
  if(luna_bat.inited)
  {
    atomic_set(&luna_bat.pin[CHG_CHG].intr_count, 0);
    disable_irq_nosync(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_CHG].gpio));
    atomic_set(&luna_bat.read_again, 3);
    if(atomic_read(&luna_bat.suspend_flag)) 
    {
      MSG2("%s ## [CHG] (wake_lock=2)", __func__);
      wake_lock_timeout(&luna_bat.wlock, HZ*2);
    }
    else
    {
      int ret;
      MSG2("%s ## [CHG] (queue work)", __func__);
      ret = queue_work(luna_bat_wqueue, &luna_bat_work);
      if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
    }
  }
  else
  {
    MSG2("%s ## [CHG] Cancelled!", __func__);
  }
  return IRQ_HANDLED;
}
static irqreturn_t luna_bat_dok_irq_handler(int irq, void *args)
{
  int ret;
  if(luna_bat.inited)
  {
    atomic_set(&luna_bat.pin[CHG_DOK].intr_count, 0);
    ret = set_irq_wake(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio), 0);
    if(ret) {MSG2("%s, pin[DOK ] set_irq_wake 0, Fail = %d", __func__, ret);}
    disable_irq_nosync(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio));
    atomic_set(&luna_bat.read_again, 3);
    if(atomic_read(&luna_bat.suspend_flag)) 
    {
      MSG2("%s ## [DOK] (wake_lock=2)", __func__);
      wake_lock_timeout(&luna_bat.wlock, HZ*2);
    }
    else
    {
      int ret;
      MSG2("%s ## [DOK] (queue work)", __func__);
      ret = queue_work(luna_bat_wqueue, &luna_bat_work);
      if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
    }
  }
  else
  {
    MSG2("%s ## [DOK] Cancelled!", __func__);
  }
  return IRQ_HANDLED;
}
static irqreturn_t luna_bat_glow_irq_handler(int irq, void *args)
{
  u_int32_t PinValue;
  if(luna_bat.inited)
  {
    PinValue = pin_value(luna_bat.pin[CHG_GLOW]);
    atomic_set(&luna_bat.read_again, 3);
    if(atomic_read(&luna_bat.suspend_flag)) 
    {
      MSG2("%s ## [GLOW], vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d (wake_lock=2)", __func__,
        luna_bat.bat_vol, PinValue, luna_bat.ac_online, luna_bat.ac_pmu_det,
        luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, luna_bat.bat_capacity);
      wake_lock_timeout(&luna_bat.wlock, HZ*2);
    }
    else
    {
      int ret;
      MSG2("%s ## [GLOW], vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d (queue work)", __func__,
        luna_bat.bat_vol, PinValue, luna_bat.ac_online, luna_bat.ac_pmu_det,
        luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, luna_bat.bat_capacity);
      ret = queue_work(luna_bat_wqueue, &luna_bat_work);
      if(!ret)  MSG2("%s, ## queue_work already ##", __func__);
    }
  }
  else
  {
    MSG2("%s ## [GLOW] Cancelled!", __func__);
  }
  return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
  static void luna_bat_early_suspend(struct early_suspend *h)
  {
    
    if(luna_bat.ac_online || (luna_bat.usb_online && luna_bat.usb_current == USB_STATUS_USB_2000))
      luna_bat.jiff_charging_timeout = jiffies + 4*60*60*HZ; 
    else
      luna_bat.jiff_charging_timeout = jiffies + 30*24*60*60*HZ;  
    luna_bat.early_suspend_flag = 1;
    
  }
  static void luna_bat_late_resume(struct early_suspend *h)
  {
    
    
    luna_bat.jiff_charging_timeout = jiffies + 30*24*60*60*HZ;  
    luna_bat.early_suspend_flag = 0;
    if(luna_bat.inited)
    {
      atomic_inc(&luna_bat.read_again);
      queue_work(luna_bat_wqueue, &luna_bat_work);
    }
    
  }
#endif
static int luna_bat_probe(struct platform_device *plat_dev)
{
  unsigned long flags = 0;
  int i, ret, fail = 0, PinValue;

  MSG2("%s+, system_rev = %d", __func__, system_rev);

  
  
  
  
  {
    ret = i2c_add_driver(&luna_bat_i2c4_driver);  
    if(ret) {MSG2("%s, i2c4 add fail = %d", __func__,ret);}
    else    {MSG2("%s, i2c4 add Pass", __func__);}
    luna_bat.i2c_addr = 0x36;
  }
  
  
  {
    i = 0;
    while(!luna_bat_gauge_client)
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

  
  
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    unsigned int  gpio = luna_bat.pin[i].gpio;
    bool pin_in = luna_bat.pin[i].pin_in;
    bool pin_en = luna_bat.pin[i].pin_en;
    
    if(i == CHG_USUS)
      continue;
    
    tegra_gpio_enable(gpio);
    ret = gpio_request(gpio, bat_pin_name[i]);
    if(ret) MSG2("%s, pin[%s] gpio_request fail = %d", __func__, bat_pin_name[i], ret);
    
    if(pin_in)  
    {
      ret = gpio_direction_input(gpio);
      PinValue = gpio_get_value(gpio);
      if(ret) {MSG2("%s, pin[%s]=i/p %d, %s, Fail", __func__, bat_pin_name[i], PinValue, PinValue==pin_en? "Active":"De-active");}
      else    {MSG2("%s, pin[%s]=i/p %d, %s", __func__, bat_pin_name[i], PinValue, PinValue==pin_en? "Active":"De-active");}
    }
    else  
    {
      if(i==CHG_IUSB || i==CHG_LIMD || i==CHG_LIMB || i==CHG_OTG) 
      {
        ret = gpio_direction_output(gpio,  pin_en);
        if(ret) {MSG2("%s, pin[%s]=o/p %d, Active, Fail", __func__, bat_pin_name[i], pin_en);}
        else    {MSG2("%s, pin[%s]=o/p %d, Active", __func__, bat_pin_name[i], pin_en);}
      }
      else  
      {
        ret = gpio_direction_output(gpio, !pin_en);
        if(ret) {MSG2("%s, pin[%s]=o/p %d, De-active, Fail", __func__, bat_pin_name[i], !pin_en);}
        else    {MSG2("%s, pin[%s]=o/p %d, De-active", __func__, bat_pin_name[i], !pin_en);}
      }
    }
  }

  
  
  
#if 1
  spin_lock_init(&luna_bat_irq_lock);
  spin_lock_irqsave(&luna_bat_irq_lock, flags);

  
  atomic_set(&luna_bat.pin[CHG_DOK].intr_count, 1);
  ret = request_threaded_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio),
    luna_bat_dok_irq_handler, NULL,
    IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
    "luna_bat_dok",
    NULL);
  if(ret) {MSG2("%s, pin[%s] request_irq  Fail = %d", __func__, bat_pin_name[CHG_DOK], ret);}
  ret = set_irq_wake(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_DOK].gpio), 1);
  if(ret) {MSG2("%s, pin[%s] set_irq_wake 1, Fail = %d", __func__, bat_pin_name[CHG_DOK], ret);}

  
  atomic_set(&luna_bat.pin[CHG_CHG].intr_count, 1);
  ret = request_threaded_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_CHG].gpio),
    luna_bat_chg_irq_handler, NULL,
    IRQF_TRIGGER_RISING,
    "luna_bat_chg",
    NULL);
  if(ret) {MSG2("%s, pin[%s] request_irq  Fail = %d", __func__, bat_pin_name[CHG_CHG], ret);}
  
  

  
  atomic_set(&luna_bat.pin[CHG_GLOW].intr_count, 1);
  ret = request_threaded_irq(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_GLOW].gpio),
    luna_bat_glow_irq_handler, NULL,
    IRQF_TRIGGER_FALLING,
    "luna_bat_glow",
    NULL);
  if(ret) {MSG2("%s, pin[%s] request_irq  Fail = %d", __func__, bat_pin_name[CHG_GLOW], ret);}
  ret = set_irq_wake(TEGRA_GPIO_TO_IRQ(luna_bat.pin[CHG_GLOW].gpio), 1);
  if(ret) {MSG2("%s, pin[%s] set_irq_wake 1, Fail = %d", __func__, bat_pin_name[CHG_GLOW], ret);}
  spin_unlock_irqrestore(&luna_bat_irq_lock, flags);
#endif

  
  
  
  luna_test_ldo0  = regulator_get(&(plat_dev->dev), "test_ldo0");
  if(!luna_test_ldo0)
    MSG2("%s, regulator_get ldo0 = %x",__func__,(unsigned)luna_test_ldo0);
  luna_test_ldo6  = regulator_get(&(plat_dev->dev), "test_ldo6");
  if(!luna_test_ldo6)
    MSG2("%s, regulator_get ldo6 = %x",__func__,(unsigned)luna_test_ldo6);
  luna_test_ldo7  = regulator_get(&(plat_dev->dev), "test_ldo7");
  if(!luna_test_ldo7)
    MSG2("%s, regulator_get ldo7 = %x",__func__,(unsigned)luna_test_ldo7);
  luna_test_ldo8  = regulator_get(&(plat_dev->dev), "test_ldo8");
  if(!luna_test_ldo8)
    MSG2("%s, regulator_get ldo8 = %x",__func__,(unsigned)luna_test_ldo8);
  luna_test_gpio1 = regulator_get(&(plat_dev->dev), "test_gpio1");
  if(!luna_test_gpio1)
    MSG2("%s, regulator_get gpio1 = %x",__func__,(unsigned)luna_test_gpio1);
  luna_test_gpio2 = regulator_get(&(plat_dev->dev), "test_gpio2");
  if(!luna_test_gpio2)
    MSG2("%s, regulator_get gpio2 = %x",__func__,(unsigned)luna_test_gpio2);
  luna_test_gpio3 = regulator_get(&(plat_dev->dev), "test_gpio3");
  if(!luna_test_gpio3)
    MSG2("%s, regulator_get gpio3 = %x",__func__,(unsigned)luna_test_gpio3);
  luna_test_gpio4 = regulator_get(&(plat_dev->dev), "test_gpio4");
  if(!luna_test_gpio4)
    MSG2("%s, regulator_get gpio4 = %x",__func__,(unsigned)luna_test_gpio4);
  luna_test_ledpwm = regulator_get(&(plat_dev->dev), "test_ledpwm");
  if(!luna_test_ledpwm)
    MSG2("%s, regulator_get ledpwm = %x",__func__,(unsigned)luna_test_ledpwm);

  
  
  
  
  luna_bat.jiff_ac_online_debounce_time = jiffies + 30*24*60*60*HZ;  

  
  wake_lock_init(&luna_bat.wlock, WAKE_LOCK_SUSPEND, "luna_bat_active");
  wake_lock_init(&luna_bat.wlock_3g, WAKE_LOCK_SUSPEND, "luna_bat_reset_3g");
  wake_lock_init(&luna_bat.wlock_poweroff, WAKE_LOCK_SUSPEND, "luna_bat_poweroff");

  
  init_timer(&luna_timer);
  luna_timer.function = luna_bat_timer_func;
  luna_timer.expires = jiffies + 10*HZ;

  
  INIT_WORK(&luna_bat_work_poweroff, luna_bat_work_func_poweroff);
  luna_bat_wqueue_poweroff = create_singlethread_workqueue("luna_bat_workqueue_poweroff");
  if(luna_bat_wqueue_poweroff)
  {
    queue_work(luna_bat_wqueue_poweroff, &luna_bat_work_poweroff);
  }
  else
  {
    MSG2("%s luna_bat_wqueue_poweroff created FAIL!",__func__);
  }

  
  INIT_WORK(&luna_bat_work, luna_bat_work_func);
  luna_bat_wqueue = create_singlethread_workqueue("luna_bat_workqueue");
  if(luna_bat_wqueue) 
  {
    MSG("%s luna_bat_workqueue created PASS!",__func__);
  }
  else  
  {
    MSG2("%s luna_bat_workqueue created FAIL!",__func__);
    fail = -1;
    goto err_exit;
  }
  luna_bat.inited = 1;
  queue_work(luna_bat_wqueue, &luna_bat_work);

  
  
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_ac));
  if(ret) MSG2("%s luna_bat.psy_ac, Fail = %d", __func__, ret);
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_usb));
  if(ret) MSG2("%s luna_bat.psy_usb, Fail = %d", __func__, ret);
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_bat));
  if(ret) MSG2("%s luna_bat.psy_bat, Fail = %d", __func__, ret);

  
  for(i=0; i<ARRAY_SIZE(luna_bat_ctrl_attrs); i++)
  {
    ret = device_create_file(luna_bat.psy_bat.dev, &luna_bat_ctrl_attrs[i]);
    if(ret) MSG2("%s: create FAIL, ret=%d",luna_bat_ctrl_attrs[i].attr.name,ret);
  }

  MSG2("%s-, ret=0", __func__);
  return 0;

err_exit:

  MSG2("%s-, ret=-1", __func__);
  return -1;
}

static struct platform_driver luna_bat_driver =
{
  .driver.name  = "luna_battery",
  .driver.owner = THIS_MODULE,
  .probe    = luna_bat_probe,
};
static int __init luna_bat_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_bat_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

module_init(luna_bat_init);
MODULE_DESCRIPTION("Luna Battery Driver");

