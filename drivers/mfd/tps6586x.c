/*
 * Core driver for TI TPS6586x PMIC family
 *
 * Copyright (c) 2010 CompuLab Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * Based on da903x.c.
 * Copyright (C) 2008 Compulab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps6586x.h>


static int pmu_log_on2  = 1;
#define MSG2(format, arg...)  {if(pmu_log_on2)  printk(KERN_INFO "[PMU]" format "\n", ## arg);}


#define TPS6586x_R49_CHG1                   0x49
#define TPS6586x_R4A_CHG2                   0x4A
#define TPS6586x_R4B_CHG3                   0x4B

#define TPS6586x_R50_RGB1FLASH              0x50
#define TPS6586x_R51_RGB1RED                0x51
#define TPS6586x_R52_RGB1GREEN              0x52
#define TPS6586x_R53_RGB1BLUE               0x53
#define TPS6586x_R54_RGB2RED                0x54
#define TPS6586x_R55_RGB2GREEN              0x55
#define TPS6586x_R56_RGB2BLUE               0x56

#define TPS6586X_LEDPWM                     0x59

#define TPS6586x_R60_ADCANLG                0x60

#define TPS6586x_R61_ADC0_SET               0x61
#define TPS6586x_R62_ADC0_WAIT              0x62
#define TPS6586x_R94_ADC0_SUM2              0x94
#define TPS6586x_R95_ADC0_SUM1              0x95
#define TPS6586x_R9A_ADC0_INT               0x9A

#define TPS6586x_RB9_STAT1                  0xB9
#define TPS6586x_RBA_STAT2                  0xBA
#define TPS6586x_RBB_STAT3                  0xBB
#define TPS6586x_RBC_STAT4                  0xBC


#define TPS6586X_SUPPLYENE  0x14
#define EXITSLREQ_BIT       BIT(1) /* Exit sleep mode request */
#define SLEEP_MODE_BIT      BIT(3) /* Sleep mode */

/* GPIO control registers */
#define TPS6586X_GPIOSET1	0x5d
#define TPS6586X_GPIOSET2	0x5e

/* interrupt control registers */
#define TPS6586X_INT_ACK1	0xb5
#define TPS6586X_INT_ACK2	0xb6
#define TPS6586X_INT_ACK3	0xb7
#define TPS6586X_INT_ACK4	0xb8

/* interrupt mask registers */
#define TPS6586X_INT_MASK1	0xb0
#define TPS6586X_INT_MASK2	0xb1
#define TPS6586X_INT_MASK3	0xb2
#define TPS6586X_INT_MASK4	0xb3
#define TPS6586X_INT_MASK5	0xb4

/* device id */
#define TPS6586X_VERSIONCRC	0xcd

struct tps6586x_irq_data {
	u8	mask_reg;
	u8	mask_mask;
};

#define TPS6586X_IRQ(_reg, _mask)				\
	{							\
		.mask_reg = (_reg) - TPS6586X_INT_MASK1,	\
		.mask_mask = (_mask),				\
	}

static const struct tps6586x_irq_data tps6586x_irqs[] = {
	[TPS6586X_INT_PLDO_0]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 0),
	[TPS6586X_INT_PLDO_1]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 1),
	[TPS6586X_INT_PLDO_2]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 2),
	[TPS6586X_INT_PLDO_3]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 3),
	[TPS6586X_INT_PLDO_4]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 4),
	[TPS6586X_INT_PLDO_5]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 5),
	[TPS6586X_INT_PLDO_6]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 6),
	[TPS6586X_INT_PLDO_7]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 7),
	[TPS6586X_INT_COMP_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK4, 1 << 0),
	[TPS6586X_INT_ADC]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 1),
	[TPS6586X_INT_PLDO_8]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 2),
	[TPS6586X_INT_PLDO_9]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 3),
	[TPS6586X_INT_PSM_0]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 4),
	[TPS6586X_INT_PSM_1]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 5),
	[TPS6586X_INT_PSM_2]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 6),
	[TPS6586X_INT_PSM_3]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 7),
	[TPS6586X_INT_RTC_ALM1]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 4),
	[TPS6586X_INT_ACUSB_OVP] = TPS6586X_IRQ(TPS6586X_INT_MASK5, 0x03),
	[TPS6586X_INT_USB_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 2),
	[TPS6586X_INT_AC_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 3),
	[TPS6586X_INT_BAT_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 1 << 0),
	[TPS6586X_INT_CHG_STAT]	= TPS6586X_IRQ(TPS6586X_INT_MASK4, 0xfc),
	[TPS6586X_INT_CHG_TEMP]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 0x06),
	[TPS6586X_INT_PP]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 0xf0),
	[TPS6586X_INT_RESUME]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 5),
	[TPS6586X_INT_LOW_SYS]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 6),
	[TPS6586X_INT_RTC_ALM2] = TPS6586X_IRQ(TPS6586X_INT_MASK4, 1 << 1),
};

struct tps6586x {
	struct mutex		lock;
	struct device		*dev;
	struct i2c_client	*client;

	struct gpio_chip	gpio;
	struct irq_chip		irq_chip;
	struct mutex		irq_lock;
	int			irq_base;
	u32			irq_en;
	u8			mask_cache[5];
	u8			mask_reg[5];
};

static inline int __tps6586x_read(struct i2c_client *client,
				  int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = (uint8_t)ret;

	return 0;
}

static inline int __tps6586x_reads(struct i2c_client *client, int reg,
				   int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading from 0x%02x\n", reg);
		return ret;
	}

	return 0;
}

static inline int __tps6586x_write(struct i2c_client *client,
				 int reg, uint8_t val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
				val, reg);
		return ret;
	}

	return 0;
}

static inline int __tps6586x_writes(struct i2c_client *client, int reg,
				  int len, uint8_t *val)
{
	int ret, i;

	for (i = 0; i < len; i++) {
		ret = __tps6586x_write(client, reg + i, *(val + i));
		if (ret < 0)
			return ret;
	}

	return 0;
}

int tps6586x_write(struct device *dev, int reg, uint8_t val)
{
	return __tps6586x_write(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_write);

int tps6586x_writes(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_writes(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_writes);

int tps6586x_read(struct device *dev, int reg, uint8_t *val)
{
	return __tps6586x_read(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_read);

int tps6586x_reads(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_reads(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_reads);

int tps6586x_set_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & bit_mask) == 0) {
		reg_val |= bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_set_bits);

int tps6586x_clr_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if (reg_val & bit_mask) {
		reg_val &= ~bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_clr_bits);

int tps6586x_update(struct device *dev, int reg, uint8_t val, uint8_t mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(tps6586x->client, reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		ret = __tps6586x_write(tps6586x->client, reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_update);

static struct i2c_client *tps6586x_i2c_client = NULL;
int tps6586x_power_off(void)
{
	struct device *dev = NULL;
	int ret = -EINVAL;

	if (!tps6586x_i2c_client)
		return ret;

	dev = &tps6586x_i2c_client->dev;

	ret = tps6586x_clr_bits(dev, TPS6586X_SUPPLYENE, EXITSLREQ_BIT);
	if (ret)
		return ret;

	{
		int tps6586x_soft_reset(uint32_t type);
		unsigned char ac=0;
		unsigned char usb=0;
		tps6586x_ac_usb_read(&ac, &usb);
		if (ac || usb)
			tps6586x_soft_reset(1);
	}

	ret = tps6586x_set_bits(dev, TPS6586X_SUPPLYENE, SLEEP_MODE_BIT);
	if (ret)
		return ret;

	return 0;
}


int tps6586x_soft_reset(uint32_t type)
{
#define RESET_TYPE_HARDRESET_MASK           (1 << 0)
#define RESET_TYPE_SOFTRESET_MASK           (1 << 1)
#define RESET_TYPE_FAKE_WATCHDOG_RESET_MASK (1 << 2)
#define IS_MATCH_MASK(x, mask)  (((x) & (mask)) == (mask))
#define TPS6586x_RC1_RTC_ALARM1_HI          0xC1
#define FLAG_SW_REBOOT            0X55
#define TPS6586x_RC2_RTC_ALARM1_MID         0xC2
#define FLAG_FAKE_WATCHDOG_REBOOT 0XAA
#define SOFT_RST_BIT      BIT(0) 

    struct device *dev = NULL;
    int ret = -EINVAL;

    if (!tps6586x_i2c_client)
    {
        MSG2("%s, i2c not ready!",__func__);
        goto fail;
    }

    if (IS_MATCH_MASK(type, RESET_TYPE_SOFTRESET_MASK))
    {
        ret = __tps6586x_write(tps6586x_i2c_client, TPS6586x_RC1_RTC_ALARM1_HI, FLAG_SW_REBOOT);
        if (ret)
        {
            MSG2("%s, write FLAG_SW_REBOOT fail",__func__);
            goto fail;
        }
    }
    else if (IS_MATCH_MASK(type, RESET_TYPE_FAKE_WATCHDOG_RESET_MASK))
    {
        ret = __tps6586x_write(tps6586x_i2c_client, TPS6586x_RC2_RTC_ALARM1_MID, FLAG_FAKE_WATCHDOG_REBOOT);
        if (ret)
        {
            MSG2("%s, write FLAG_FAKE_WATCHDOG_REBOOT fail",__func__);
            goto fail;
        }
    }

    dev = &tps6586x_i2c_client->dev;
    ret = tps6586x_set_bits(dev, TPS6586X_SUPPLYENE, SOFT_RST_BIT);
    if (ret)
    {
        MSG2("%s, set SOFT_RST_BIT fail",__func__);
        goto fail;
    }

    MSG2("%s-, pass!",__func__);
    return 0;
fail:
    MSG2("%s-, fail!",__func__);
    return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_soft_reset);






#include <linux/delay.h>
struct mutex tps6586x_adc_lock;
int tps6586x_adc_read(unsigned int channel, unsigned int *volt)
{
  unsigned int timeout  = 0;
  unsigned char dataS1  = 0;
  unsigned char dataH   = 0;
  unsigned char dataL   = 0;
  unsigned char Adc0SetVal = 0;

  if(!tps6586x_i2c_client)
  {
    MSG2("%s, i2c not ready!",__func__);
    goto fail;
  }

  mutex_lock(&tps6586x_adc_lock);
  

  *volt = 0;  

  if(channel >= TPS6586X_ADC_MAX || !volt || !tps6586x_i2c_client)
  {
    MSG2("%s, fail! channel=%d, volt=%x, tps6586x_i2c_client=%x",__func__,
      channel, (unsigned int)volt, (unsigned int)tps6586x_i2c_client);
    goto fail;
  }

  if(channel == TPS6586X_ADC_5)
  {
    if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R49_CHG1, 0x1C))  
      goto fail;
  }
  else
  {
    
    
    
    if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R60_ADCANLG, 0x28))
      goto fail;
  }

  
  
  
  if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R62_ADC0_WAIT, 0x80))
      goto fail;
      
  
  
  Adc0SetVal = (0x10 | channel);
  if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R61_ADC0_SET, Adc0SetVal))
      goto fail;
      
  
  
  if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R62_ADC0_WAIT, 0x21))
      goto fail;

  
  Adc0SetVal = (0x90 | channel);
  if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R61_ADC0_SET, Adc0SetVal))
      goto fail;

  
  msleep(ADC_CONVERSION_PREWAIT_MS);

  
  while (1)
  {
      
      if(__tps6586x_read(tps6586x_i2c_client, TPS6586x_R9A_ADC0_INT, &dataS1))
          goto fail;

      
      if (dataS1 & 0x80)
          break;
      
      
      if (dataS1 & 0x40)
      {
          MSG2("%s, ADC conversion error",__func__);
          goto fail;
      }

      udelay(ADC_CONVERSION_DELAY_USEC);
      timeout += ADC_CONVERSION_DELAY_USEC;
      if (timeout >= ADC_CONVERSION_TIMEOUT_USEC)
          goto fail;
  }

  
  if (__tps6586x_read(tps6586x_i2c_client, TPS6586x_R94_ADC0_SUM2, &dataH))
      goto fail;
      
  if (__tps6586x_read(tps6586x_i2c_client, TPS6586x_R95_ADC0_SUM1, &dataL))
      goto fail;

  if(channel == TPS6586X_ADC_5)
  {
    
    *volt = (((dataH << 8) | dataL) *  ADC_FULL_SCALE_READING_MV_TS) / 1023 / 16;

    if(__tps6586x_write(tps6586x_i2c_client, TPS6586x_R49_CHG1, 0x0c))  
      goto fail;
  }
  else
  {
    
    

    
    *volt = ((dataH << 8) | dataL) / 16;
  }

  mutex_unlock(&tps6586x_adc_lock);
  
  return 0;

fail:
  mutex_lock(&tps6586x_adc_lock);
  MSG2("%s-, fail!",__func__);
  return -1;
}
EXPORT_SYMBOL_GPL(tps6586x_adc_read);



int tps6586x_ac_usb_read(unsigned char *ac, unsigned char *usb)
{
  int ret;
  uint8_t data;
  
  if(!tps6586x_i2c_client)
  {
    MSG2("%s, i2c not ready!",__func__);
    goto fail;
  }
  ret = __tps6586x_read(tps6586x_i2c_client, TPS6586x_RBB_STAT3, &data);
  if(ret)   goto fail;
  if(data & 0x04) *usb = 1; 
  else            *usb = 0;
  if(data & 0x08) *ac = 1;  
  else            *ac = 0;
  
  return 0;
fail:
  *usb = 0;
  *ac = 0;
  MSG2("%s-, fail!",__func__);
  return -1;
}
EXPORT_SYMBOL_GPL(tps6586x_ac_usb_read);

const char *rgb1_led_name[TPS6586X_RGB1_MAX]      = {"RED1  ", "GREEN1", "BLUE1 ", "BLINK "};
static uint8_t rgb1_led_bypass[TPS6586X_RGB1_MAX] = {0,0,0,0};  
static uint8_t rgb1_led_onOff[TPS6586X_RGB1_MAX]  = {0,0,0,0};  
int tps6586x_get_rgb1(unsigned id, unsigned *val)
{
  int ret = 0;
  uint8_t data;
  if(!tps6586x_i2c_client)
  {
    MSG2("%s+, id=%s, i2c not ready!",__func__,rgb1_led_name[id]);
    ret = -EINVAL;
    goto fail;
  }
  else if(id >= TPS6586X_RGB1_MAX || !val)
  {
    MSG2("%s+, invalid",__func__);
    ret = -EINVAL;
    goto fail;
  }
  else
  {
    
  }
  switch(id)
  {
    case TPS6586X_RGB1_RED:   
      ret = __tps6586x_read(tps6586x_i2c_client, TPS6586x_R51_RGB1RED, &data);
      if(ret)   goto fail;
      else      *val = data & 0x1f;
      break;
    case TPS6586X_RGB1_GREEN: 
      ret = __tps6586x_read(tps6586x_i2c_client, TPS6586x_R52_RGB1GREEN, &data);
      if(ret)   goto fail;
      else      *val = data & 0x1f;
      break;
    case TPS6586X_RGB1_BLUE:
      ret = __tps6586x_read(tps6586x_i2c_client, TPS6586x_R53_RGB1BLUE, &data);
      if(ret)   goto fail;
      else      *val = data & 0x1f;
      break;
    case TPS6586X_RGB1_BLINK:
      ret = __tps6586x_read(tps6586x_i2c_client, TPS6586x_R50_RGB1FLASH, &data);
      if(ret)   goto fail;
      else      *val = data & 0x7f;
      break;
  }
  
  return ret;
fail:
  MSG2("%s-, Fail = %d",__func__,ret);
  return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_get_rgb1);



int tps6586x_set_rgb1(unsigned id, unsigned val)
{
  int ret = 0, reg_intensity, reg_enable;
  uint8_t data;
  if(!tps6586x_i2c_client)
  {
    MSG2("%s+, id=%s, i2c not ready!",__func__,rgb1_led_name[id]);
    ret = -EINVAL;
    goto fail;
  }
  else if(id >= TPS6586X_RGB1_MAX)
  {
    MSG2("%s+, invalid",__func__);
    ret = -EINVAL;
    goto fail;
  }
  else
  {
    
  }
  
  
  if(val == 0x5A5A5A5A)
  {
    rgb1_led_bypass[0] = 0;   
    rgb1_led_bypass[1] = 0;
    rgb1_led_bypass[2] = 0;
    rgb1_led_bypass[3] = 0;
    
    return 0;
  }
  if((val >> 24) == 0xA5)
  {
    rgb1_led_bypass[id] = 1;  
  }
  else if((val >> 24) == 0x5A)
  {
    rgb1_led_bypass[id] = 0;  
  }
  
  
  else if(rgb1_led_bypass[id])
  {
    MSG2("%s, %s BYPASS!",__func__,rgb1_led_name[id]);
    return 0;
  }
  
  switch(id)
  {
    case TPS6586X_RGB1_RED:   
      reg_intensity = TPS6586x_R51_RGB1RED;
      reg_enable    = TPS6586x_R52_RGB1GREEN;
      break;
    case TPS6586X_RGB1_GREEN: 
      reg_intensity = TPS6586x_R52_RGB1GREEN;
      reg_enable    = TPS6586x_R52_RGB1GREEN;
      break;
    case TPS6586X_RGB1_BLUE:
      reg_intensity = TPS6586x_R53_RGB1BLUE;
      reg_enable    = TPS6586x_R52_RGB1GREEN;
      break;
    case TPS6586X_RGB1_BLINK:
      reg_intensity = TPS6586x_R50_RGB1FLASH;
      break;
  }
  
  ret = __tps6586x_read(tps6586x_i2c_client, reg_intensity, &data);
  if(ret)   goto fail;
  if(id != TPS6586X_RGB1_BLINK)
    data = (data & 0xE0) | (val & 0x1F);  
  else
    data = (data & 0x80) | (val & 0x7F);  
  ret = __tps6586x_write(tps6586x_i2c_client, reg_intensity, data);
  if(ret)   goto fail;
  
  if(id != TPS6586X_RGB1_BLINK)
  {
    ret = __tps6586x_read(tps6586x_i2c_client, reg_enable, &data);
    if(ret)   goto fail;
    if(val)   rgb1_led_onOff[id] = 1;
    else      rgb1_led_onOff[id] = 0;
    if(rgb1_led_onOff[0] || rgb1_led_onOff[1] || rgb1_led_onOff[2]) 
      data = data | 0x80;
    else
      data = data & 0x7F;
    ret = __tps6586x_write(tps6586x_i2c_client, reg_enable, data);
    if(ret)   goto fail;
  }
  
  return ret;
fail:
  MSG2("%s-, Fail = %d",__func__,ret);
  return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_set_rgb1);


static int tps6586x_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val;
	int ret;

	ret = __tps6586x_read(tps6586x->client, TPS6586X_GPIOSET2, &val);
	if (ret)
		return ret;

	return !!(val & (1 << offset));
}


static void tps6586x_gpio_set(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	struct tps6586x *tps6586x = container_of(chip, struct tps6586x, gpio);
  
	__tps6586x_write(tps6586x->client, TPS6586X_GPIOSET2,
			 value << offset);
}

static int tps6586x_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	/* FIXME: add handling of GPIOs as dedicated inputs */
	return -ENOSYS;
}

static int tps6586x_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val, mask;
	int ret;
  
	val = value << offset;
	mask = 0x1 << offset;
	ret = tps6586x_update(tps6586x->dev, TPS6586X_GPIOSET2, val, mask);
	if (ret)
		return ret;

	val = 0x1 << (offset * 2);
	mask = 0x3 << (offset * 2);

	return tps6586x_update(tps6586x->dev, TPS6586X_GPIOSET1, val, mask);
}

static void tps6586x_gpio_init(struct tps6586x *tps6586x, int gpio_base)
{
	int ret;

	if (!gpio_base)
		return;

	tps6586x->gpio.owner		= THIS_MODULE;
	tps6586x->gpio.label		= tps6586x->client->name;
	tps6586x->gpio.dev		= tps6586x->dev;
	tps6586x->gpio.base		= gpio_base;
	tps6586x->gpio.ngpio		= 4;
	tps6586x->gpio.can_sleep	= 1;

	tps6586x->gpio.direction_input	= tps6586x_gpio_input;
	tps6586x->gpio.direction_output	= tps6586x_gpio_output;
	tps6586x->gpio.set		= tps6586x_gpio_set;
	tps6586x->gpio.get		= tps6586x_gpio_get;

	ret = gpiochip_add(&tps6586x->gpio);
	if (ret)
		dev_warn(tps6586x->dev, "GPIO registration failed: %d\n", ret);
}

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int tps6586x_remove_subdevs(struct tps6586x *tps6586x)
{
	return device_for_each_child(tps6586x->dev, NULL, __remove_subdev);
}

static void tps6586x_irq_lock(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);

	mutex_lock(&tps6586x->irq_lock);
}

static void tps6586x_irq_enable(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);
	unsigned int __irq = irq - tps6586x->irq_base;
	const struct tps6586x_irq_data *data = &tps6586x_irqs[__irq];

	tps6586x->mask_reg[data->mask_reg] &= ~data->mask_mask;
	tps6586x->irq_en |= (1 << __irq);
}

static void tps6586x_irq_disable(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);

	unsigned int __irq = irq - tps6586x->irq_base;
	const struct tps6586x_irq_data *data = &tps6586x_irqs[__irq];

	tps6586x->mask_reg[data->mask_reg] |= data->mask_mask;
	tps6586x->irq_en &= ~(1 << __irq);
}

static void tps6586x_irq_sync_unlock(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);
	int i;

	for (i = 0; i < ARRAY_SIZE(tps6586x->mask_reg); i++) {
		if (tps6586x->mask_reg[i] != tps6586x->mask_cache[i]) {
			if (!WARN_ON(tps6586x_write(tps6586x->dev,
						    TPS6586X_INT_MASK1 + i,
						    tps6586x->mask_reg[i])))
				tps6586x->mask_cache[i] = tps6586x->mask_reg[i];
		}
	}

	mutex_unlock(&tps6586x->irq_lock);
}

static irqreturn_t tps6586x_irq(int irq, void *data)
{
	struct tps6586x *tps6586x = data;
	u32 acks, acks_backup;
	int ret = 0;

	MSG2("%s+",__func__);
	ret = tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1,
			     sizeof(acks), (uint8_t *)&acks);
  acks_backup = acks;

	if (ret < 0) {
		dev_err(tps6586x->dev, "failed to read interrupt status\n");
  	MSG2("%s-, fail to read",__func__);
		return IRQ_NONE;
	}

	acks = le32_to_cpu(acks);

	while (acks) {
		int i = __ffs(acks);

		if (tps6586x->irq_en & (1 << i))
			handle_nested_irq(tps6586x->irq_base + i);

		acks &= ~(1 << i);
	}

 	MSG2("%s-, acks=%08X",__func__,acks_backup);
	return IRQ_HANDLED;
}

static int __devinit tps6586x_irq_init(struct tps6586x *tps6586x, int irq,
				       int irq_base)
{
	int i, ret;
	u8 tmp[4];

	if (!irq_base) {
		dev_warn(tps6586x->dev, "No interrupt support on IRQ base\n");
		return -EINVAL;
	}

	mutex_init(&tps6586x->irq_lock);
	for (i = 0; i < 5; i++) {
		tps6586x->mask_cache[i] = 0xff;
		tps6586x->mask_reg[i] = 0xff;
		tps6586x_write(tps6586x->dev, TPS6586X_INT_MASK1 + i, 0xff);
	}

	tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1, sizeof(tmp), tmp);

	tps6586x->irq_base = irq_base;

	tps6586x->irq_chip.name = "tps6586x";
	tps6586x->irq_chip.enable = tps6586x_irq_enable;
	tps6586x->irq_chip.disable = tps6586x_irq_disable;
	tps6586x->irq_chip.bus_lock = tps6586x_irq_lock;
	tps6586x->irq_chip.bus_sync_unlock = tps6586x_irq_sync_unlock;

	for (i = 0; i < ARRAY_SIZE(tps6586x_irqs); i++) {
		int __irq = i + tps6586x->irq_base;
		set_irq_chip_data(__irq, tps6586x);
		set_irq_chip_and_handler(__irq, &tps6586x->irq_chip,
					 handle_simple_irq);
		set_irq_nested_thread(__irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(__irq, IRQF_VALID);
#endif
	}

	ret = request_threaded_irq(irq, NULL, tps6586x_irq, IRQF_ONESHOT,
				   "tps6586x", tps6586x);

	if (!ret) {
		device_init_wakeup(tps6586x->dev, 1);
		enable_irq_wake(irq);
	}

	return ret;
}

static int __devinit tps6586x_add_subdevs(struct tps6586x *tps6586x,
					  struct tps6586x_platform_data *pdata)
{
	struct tps6586x_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);

		pdev->dev.parent = tps6586x->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret)
			goto failed;
	}
	return 0;

failed:
	tps6586x_remove_subdevs(tps6586x);
	return ret;
}


static int tps6586x_i2c_suspend(struct i2c_client *client, pm_message_t state)
{
  
  return 0;
}
static int tps6586x_i2c_resume(struct i2c_client *client)
{
  
  return 0;
}


static int __devinit tps6586x_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct tps6586x_platform_data *pdata = client->dev.platform_data;
	struct tps6586x *tps6586x;
	int ret;

	if (!pdata) {
		dev_err(&client->dev, "tps6586x requires platform data\n");
		return -ENOTSUPP;
	}

	ret = i2c_smbus_read_byte_data(client, TPS6586X_VERSIONCRC);
	if (ret < 0) {
		dev_err(&client->dev, "Chip ID read failed: %d\n", ret);
		return -EIO;
	}

	dev_info(&client->dev, "VERSIONCRC is %02x\n", ret);

	tps6586x = kzalloc(sizeof(struct tps6586x), GFP_KERNEL);
	if (tps6586x == NULL)
		return -ENOMEM;

	tps6586x->client = client;
	tps6586x->dev = &client->dev;
	i2c_set_clientdata(client, tps6586x);

	mutex_init(&tps6586x->lock);

	if (client->irq) {
		ret = tps6586x_irq_init(tps6586x, client->irq,
					pdata->irq_base);
		if (ret) {
			dev_err(&client->dev, "IRQ init failed: %d\n", ret);
			goto err_irq_init;
		}
	}

	ret = tps6586x_add_subdevs(tps6586x, pdata);
	if (ret) {
		dev_err(&client->dev, "add devices failed: %d\n", ret);
		goto err_add_devs;
	}

	tps6586x_gpio_init(tps6586x, pdata->gpio_base);

	tps6586x_i2c_client = client;

  
  {
    
    ret = i2c_smbus_write_byte_data(client, TPS6586X_GPIOSET1, 0x55);  
    
    ret = i2c_smbus_write_byte_data(client, TPS6586X_GPIOSET2, 0x02);  
    
    
    ret = i2c_smbus_write_byte_data(client, TPS6586X_LEDPWM, 0x00);  
    
    
    ret = i2c_smbus_write_byte_data(client, TPS6586x_R52_RGB1GREEN, 0x00); 
    
    ret = i2c_smbus_write_byte_data(client, TPS6586x_R50_RGB1FLASH, 0x7F); 
    
    ret = i2c_smbus_write_byte_data(client, TPS6586x_R51_RGB1RED,   0x60); 
    
    ret = i2c_smbus_write_byte_data(client, TPS6586x_R53_RGB1BLUE,  0x60); 
    
  }
  mutex_init(&tps6586x_adc_lock);
  

	return 0;

err_add_devs:
	if (client->irq)
		free_irq(client->irq, tps6586x);
err_irq_init:
	kfree(tps6586x);
	return ret;
}

static int __devexit tps6586x_i2c_remove(struct i2c_client *client)
{
	struct tps6586x *tps6586x = i2c_get_clientdata(client);

	if (client->irq)
		free_irq(client->irq, tps6586x);

	return 0;
}

static const struct i2c_device_id tps6586x_id_table[] = {
	{ "tps6586x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps6586x_id_table);

static struct i2c_driver tps6586x_driver = {
	.driver	= {
		.name	= "tps6586x",
		.owner	= THIS_MODULE,
	},
	
	.suspend  = tps6586x_i2c_suspend,
	.resume   = tps6586x_i2c_resume,
	
	.probe		= tps6586x_i2c_probe,
	.remove		= __devexit_p(tps6586x_i2c_remove),
	.id_table	= tps6586x_id_table,
};

static int __init tps6586x_init(void)
{
	return i2c_add_driver(&tps6586x_driver);
}
subsys_initcall(tps6586x_init);

static void __exit tps6586x_exit(void)
{
	i2c_del_driver(&tps6586x_driver);
}
module_exit(tps6586x_exit);

MODULE_DESCRIPTION("TPS6586X core driver");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");
