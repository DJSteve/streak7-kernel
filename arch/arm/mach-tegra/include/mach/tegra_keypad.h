


#ifndef _TEGRA_KEYPAD_H_
#define _TEGRA_KEYPAD_H_

#include <linux/interrupt.h>
#define KEYPAD_DRIVER_NAME "tegra_keypad_key"
#define MAX_SUPPORT_KEYS 4
#define NUM_OF_1A_KEY_SIZE 3

#define NUM_OF_1B_KEY_SIZE 3 
#define NUM_OF_2_1_KEY_SIZE 4 

#define ECHOSTR_SIZE        20

struct key_t {
	int key_code;
	struct delayed_work      key_work;
	int state;
	int gpio_num;
	int irq;
};

struct tegra_keypad_platform_data_t {
	int gpio_power;
	int gpio_voldown;
	int gpio_volup;
	int gpio_bodysar;
	int gpio_bodysar_pwr;
	
	
	
};


enum{
CapSensor_Detectable,
CapSensor_PowerOff,
CapSensor_PowerOn
};

#endif
