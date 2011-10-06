







#define NV_DEBUG 0

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/tps6586x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>

#include <mach/luna_hwid.h>

static int led_log_on1  = 0;
static int led_log_on2  = 1;


#define MSG(format, arg...)   {if(led_log_on1)  printk(KERN_INFO "[LED]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(led_log_on2)  printk(KERN_INFO "[LED]" format "\n", ## arg);}





static void luna_led_red_set(struct led_classdev *led_cdev, enum led_brightness value)
{
  unsigned data;
  MSG("%s = %d", __func__,value);
  data = value >> 3;
  if((!data) && (value))
    data = 1;
  tps6586x_set_rgb1(TPS6586X_RGB1_GREEN, data); 
}
static void luna_led_green_set(struct led_classdev *led_cdev, enum led_brightness value)
{
  unsigned data;
  MSG("%s = %d", __func__,value);
  data = value >> 3;
  if((!data) && (value))
    data = 1;
  tps6586x_set_rgb1(TPS6586X_RGB1_RED, data);   
}


static ssize_t luna_led_blink_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  unsigned data;
  tps6586x_get_rgb1(TPS6586X_RGB1_BLINK, &data);
  return sprintf(buf, "%u\n", data);
}
static ssize_t luna_led_blink_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  char *after;
  unsigned long state = simple_strtoul(buf, &after, 10);
  unsigned data;

  MSG("%s = %d", __func__,(unsigned int)state);
  
  
  
  if(state <= 0x7F)
    data = (unsigned int)state;
  else
    data = 0x7F;

  
  
  tps6586x_set_rgb1(TPS6586X_RGB1_BLINK, data);

  return count;
}
static struct device_attribute luna_led_ctrl_attrs[] = {
  __ATTR(blink, 0664, luna_led_blink_show,  luna_led_blink_store),
  
  
};




static struct led_classdev luna_led_red = {
  .name           = "red",
  .brightness     = LED_OFF,
  .brightness_set = luna_led_red_set,
  
};
static struct led_classdev luna_led_green = {
  .name           = "green",
  .brightness     = LED_OFF,
  .brightness_set = luna_led_green_set,
};




static void luna_led_shutdown(struct platform_device *pdev)
{
  unsigned data;
  MSG("%s",__func__);
  
  data = 0;
  tps6586x_set_rgb1(TPS6586X_RGB1_GREEN, data); 
  tps6586x_set_rgb1(TPS6586X_RGB1_RED, data);   
  
  data = 0x7F;
  tps6586x_set_rgb1(TPS6586X_RGB1_BLINK, data);
}

static int luna_led_probe(struct platform_device *pdev)
{
  int ret=-EINVAL, fail=0, i;
  MSG2("%s+", __func__);

  
  
  ret = led_classdev_register(&pdev->dev, &luna_led_red);
  if(ret < 0) {fail = 2;  goto error_exit;}
  ret = led_classdev_register(&pdev->dev, &luna_led_green);
  if(ret < 0) {fail = 3;  goto error_exit;}

  
  
  for(i=0; i<ARRAY_SIZE(luna_led_ctrl_attrs); i++)
  {
    ret = device_create_file(luna_led_red.dev, &luna_led_ctrl_attrs[i]);
    if(ret) MSG2("%s: create FAIL, ret=%d",luna_led_ctrl_attrs[i].attr.name,ret);
  }

  
  #if defined(CONFIG_TINY_ANDROID)
    luna_led_green_set(&luna_led_green,LED_FULL);
  #endif

  MSG2("%s- PASS, ret=%d", __func__,ret);
  return ret;

error_exit:
  if(fail > 2)
    led_classdev_unregister(&luna_led_green);
  if(fail > 1)
    led_classdev_unregister(&luna_led_red);
  MSG2("%s- FAIL, ret=%d!", __func__,ret);
  return ret;
}



static struct platform_driver luna_led_driver = {
  
  
	.shutdown = luna_led_shutdown,
  .probe    = luna_led_probe,
  
  .driver   = {
    .name   = "luna_led",
    .owner    = THIS_MODULE,
  },
};

static int __init luna_led_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_led_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

module_init(luna_led_init);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Liu");
MODULE_DESCRIPTION("Luna LED driver");


