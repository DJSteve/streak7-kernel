


#ifndef _ATA2538_CAPKEY_H_
#define _ATA2538_CAPKEY_H_

#define CAPKEY_DRIVER_NAME "ATA2538_capkey"
#define CAPKEY_I2C_ADDR 0x68

#define ADD_PA0_ALPHA       	0x00
#define ADDR_REG_CHECK      	0x28
#define ADDR_COLD_RESET     	0xFE
#define ADDR_WARM_RESET     	0xFF
#define PA_TOUCH_BYTE       	0x68
#define Enter_SLEEP         	0xFD
#define WAKEUP_SLEEP        	0xFC
#define INIT_ALPHA_VALUE    	0x12
#define ADDR_GPIO_CONFIGURATION 0x1D
#define PA0_STRENGTH            0x50
#define PA1_STRENGTH            0x51
#define PA2_STRENGTH            0x52
#define PA3_STRENGTH            0x53
#define PA4_STRENGTH            0x54
#define PA5_STRENGTH            0x55
#define PA6_STRENGTH            0x56
#define PA7_STRENGTH            0x57
#define PA0_IMPEDANCE           0x58
#define PA1_IMPEDANCE           0x59
#define PA2_IMPEDANCE           0x5A
#define PA3_IMPEDANCE           0x5B
#define PA4_IMPEDANCE           0x5C
#define PA5_IMPEDANCE           0x5D
#define PA6_IMPEDANCE           0x5E
#define PA7_IMPEDANCE           0x5F
#define PA0_REFERENCE_IMPEDANCE 0x60
#define PA1_REFERENCE_IMPEDANCE 0x61
#define PA2_REFERENCE_IMPEDANCE 0x62
#define PA3_REFERENCE_IMPEDANCE 0x63
#define PA4_REFERENCE_IMPEDANCE 0x64
#define PA5_REFERENCE_IMPEDANCE 0x65
#define PA6_REFERENCE_IMPEDANCE 0x66
#define PA7_REFERENCE_IMPEDANCE 0x67
#define ALPHA_SIZE          8
#define TOTAL_REG_SIZE      61
#define ECHOSTR_SIZE            20
#define EMLIST_SIZE         9

#define ADD_PA0_ALPHA       	0x00
#define INIT_ALPHA_VALUE    	0x12
#define ALPHA_SIZE          8

struct ata2538_capkey_platform_data_t {
    unsigned int gpiorst;
    unsigned int gpioirq;
};


struct capkey_emlist_t {
    uint8_t   value[EMLIST_SIZE];
};

struct capkey_alpha_t {
	uint8_t   PA3_value;
	uint8_t   PA4_value;
	uint8_t   PA5_value;
    uint8_t   reference_delay;
};


#define __ATACAPKEYDRVIO 0xAA
#define ATA_CAPKEY_IOCTL_GET_SENSOR_VALUE _IOR(__ATACAPKEYDRVIO, 1, struct capkey_emlist_t )
#define ATA_CAPKEY_IOCTL_SET_ALPHA _IOW(__ATACAPKEYDRVIO, 2, struct capkey_alpha_t )
#define ATA_CAPKEY_IOCTL_GET_ALPHA _IOR(__ATACAPKEYDRVIO, 3, struct capkey_alpha_t )

#endif
