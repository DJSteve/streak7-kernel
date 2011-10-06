#ifndef __LINUX_MFD_TPS6586X_H
#define __LINUX_MFD_TPS6586X_H


#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define SM0_PWM_BIT 0
#define SM1_PWM_BIT 1
#define SM2_PWM_BIT 2

enum {
	TPS6586X_ID_SM_0,
	TPS6586X_ID_SM_1,
	TPS6586X_ID_SM_2,
	TPS6586X_ID_LDO_0,
	TPS6586X_ID_LDO_1,
	TPS6586X_ID_LDO_2,
	TPS6586X_ID_LDO_3,
	TPS6586X_ID_LDO_4,
	TPS6586X_ID_LDO_5,
	TPS6586X_ID_LDO_6,
	TPS6586X_ID_LDO_7,
	TPS6586X_ID_LDO_8,
	TPS6586X_ID_LDO_9,
	TPS6586X_ID_LDO_RTC,
  
  TPS6586X_ID_GPIO_1,
  TPS6586X_ID_GPIO_2,
  TPS6586X_ID_GPIO_3,
  TPS6586X_ID_GPIO_4,
  TPS6586X_ID_LED_PWM,
  
};

enum {
	TPS6586X_INT_PLDO_0,
	TPS6586X_INT_PLDO_1,
	TPS6586X_INT_PLDO_2,
	TPS6586X_INT_PLDO_3,
	TPS6586X_INT_PLDO_4,
	TPS6586X_INT_PLDO_5,
	TPS6586X_INT_PLDO_6,
	TPS6586X_INT_PLDO_7,
	TPS6586X_INT_COMP_DET,
	TPS6586X_INT_ADC,
	TPS6586X_INT_PLDO_8,
	TPS6586X_INT_PLDO_9,
	TPS6586X_INT_PSM_0,
	TPS6586X_INT_PSM_1,
	TPS6586X_INT_PSM_2,
	TPS6586X_INT_PSM_3,
	TPS6586X_INT_RTC_ALM1,
	TPS6586X_INT_ACUSB_OVP,
	TPS6586X_INT_USB_DET,
	TPS6586X_INT_AC_DET,
	TPS6586X_INT_BAT_DET,
	TPS6586X_INT_CHG_STAT,
	TPS6586X_INT_CHG_TEMP,
	TPS6586X_INT_PP,
	TPS6586X_INT_RESUME,
	TPS6586X_INT_LOW_SYS,
	TPS6586X_INT_RTC_ALM2,
};

enum pwm_pfm_mode {
	PWM_ONLY,
	AUTO_PWM_PFM,
	NOT_CONFIGURABLE
};

struct tps6586x_settings {
	/* SM0, SM1 and SM2 have PWM-only and auto PWM/PFM mode */
	enum pwm_pfm_mode sm_pwm_mode;
};

enum {
	TPS6586X_RTC_CL_SEL_1_5PF  = 0x0,
	TPS6586X_RTC_CL_SEL_6_5PF  = 0x1,
	TPS6586X_RTC_CL_SEL_7_5PF  = 0x2,
	TPS6586X_RTC_CL_SEL_12_5PF = 0x3,
};

struct tps6586x_subdev_info {
	int		id;
	const char	*name;
	void		*platform_data;
};

struct tps6586x_epoch_start {
	int year;
	int month;
	int day;
	int hour;
	int min;
	int sec;
};

struct tps6586x_rtc_platform_data {
	int irq;
	struct tps6586x_epoch_start start;
	int cl_sel; /* internal XTAL capacitance, see TPS6586X_RTC_CL_SEL* */
};

struct tps6586x_platform_data {
	int num_subdevs;
	struct tps6586x_subdev_info *subdevs;

	int gpio_base;
	int irq_base;
};

/*
 * NOTE: the functions below are not intended for use outside
 * of the TPS6586X sub-device drivers
 */
extern int tps6586x_write(struct device *dev, int reg, uint8_t val);
extern int tps6586x_writes(struct device *dev, int reg, int len, uint8_t *val);
extern int tps6586x_read(struct device *dev, int reg, uint8_t *val);
extern int tps6586x_reads(struct device *dev, int reg, int len, uint8_t *val);
extern int tps6586x_set_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int tps6586x_clr_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int tps6586x_update(struct device *dev, int reg, uint8_t val,
			   uint8_t mask);
extern int tps6586x_power_off(void);



struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
};
extern void ftm_test_mode_onOff(int onOff);
extern int ftm_test_regulator_enable(struct regulator *rr);
extern int ftm_test_regulator_disable(struct regulator *rr);
extern int tps6586x_adc_read(unsigned int channel, unsigned int *volt);
extern int tps6586x_ac_usb_read(unsigned char *ac, unsigned char *usb);
extern int tps6586x_get_rgb1(unsigned id, unsigned *val);
extern int tps6586x_set_rgb1(unsigned id, unsigned val);



#define ADC_CONVERSION_DELAY_USEC      70
#define ADC_CONVERSION_TIMEOUT_USEC    500
#define ADC_FULL_SCALE_READING_MV_TS   2600
#define ADC_CONVERSION_PREWAIT_MS      26
enum {
  TPS6586X_ADC_1 = 0, 
  TPS6586X_ADC_2,     
  TPS6586X_ADC_3,     
  TPS6586X_ADC_4,
  TPS6586X_ADC_5,     
  TPS6586X_ADC_6,
  TPS6586X_ADC_7,
  TPS6586X_ADC_8,
  TPS6586X_ADC_9,
  TPS6586X_ADC_10,
  TPS6586X_ADC_11,
  TPS6586X_ADC_MAX,
};



enum {
  TPS6586X_RGB1_RED = 0,  
  TPS6586X_RGB1_GREEN,    
  TPS6586X_RGB1_BLUE,
  TPS6586X_RGB1_BLINK,
  TPS6586X_RGB1_MAX,
};


#endif /*__LINUX_MFD_TPS6586X_H */
