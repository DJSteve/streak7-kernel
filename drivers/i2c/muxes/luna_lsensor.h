#ifndef _LSENSOR_H_
#define _LSENSOR_H_
#include <asm/ioctl.h>
#include <linux/i2c.h>
#include <linux/input.h>




#define MYBIT(b)        (1<<b)
#define TST_BIT(x,b)    ((x & (1<<b)) ? 1 : 0)
#define CLR_BIT(x,b)    (x &= (~(1<<b)))
#define SET_BIT(x,b)    (x |= (1<<b))





#define LSENSOR_EN_AP     0x01  
#define LSENSOR_EN_LCD    0x02  
#define LSENSOR_EN_ENG    0x04  

#define TAOS_MAX_LUX        65535000
#define TAOS_ACCESS_FAIL    0xFFFFFFFF

#define LSENSOR_CALIBRATION_LOOP 6 

#define LUNA_ALS_BUF_LENGTH  256




struct lsensor_info_data {  
  
  unsigned int a_ms;      

  
  unsigned short cdata;   
  unsigned short irdata;  

  
  unsigned int  m_irf;    
  unsigned int  m_lux;    

  
  unsigned int  status;   
};



struct lux_bkl_table {
  short level;  
  short now;    
  short high;   
  short low;    
};
#define LSENSOR_BKL_TABLE_SIZE 16
struct lsensor_drv_data {
  int inited;
  int enable;     
  int opened;     
  int eng_mode;   
  int irq_enabled;
  int in_early_suspend;
  int in_suspend;
  int i2c_err;    
  int i2c_addr;
  struct i2c_client *client;
  struct input_dev  *input;

  
  unsigned int m_ga;    

  
  struct completion info_comp;
  int info_waiting;
  struct completion info_comp_als;
  int info_waiting_als;

  
  short brightness_backup;
  
  unsigned int lux_history[3];  
  unsigned int bkl_idx;         
  unsigned int bkl_idx_old;     
  struct lux_bkl_table bkl_table[LSENSOR_BKL_TABLE_SIZE];

  unsigned int millilux; 

  
  unsigned int als_nv;   

  
  unsigned long jiff_polling_interval;

  unsigned long jiff_update_bkl_wait_time;  

  unsigned long jiff_resume_fast_update_time; 
};



struct lsensor_eng_data { 
  unsigned char pon;    
  unsigned char aen;    
  unsigned char again;  
  unsigned char atime;  
  unsigned int  m_ga;   
};



struct lsensor_reg_data {
  
  union {           
    struct {
      unsigned char pon:1;   
      unsigned char aen:1;   
      unsigned char rev:2;   
      unsigned char valid:1; 
      unsigned char intr:1;  
    } bit;
    unsigned char byte;
  } r00;
  unsigned char atime;       
  union {           
    struct {
      unsigned char pers:4;  
      unsigned char mode:2;  
      unsigned char stop:1;  
    } bit;
    unsigned char byte;
  } r02;
  unsigned char low[2];      
  unsigned char high[2];     
  unsigned char again;       

  
  unsigned char id[2];       
  unsigned char cdata[2];    
  unsigned char irdata[2];   
  unsigned char time[2];     
};





struct lsensor_cal_data {
  unsigned int als;
  unsigned int status;  
};




#define LSENSOR_IOC_MAGIC       'l' 

#define LSENSOR_IOC_ENABLE      _IO(LSENSOR_IOC_MAGIC, 1)
#define LSENSOR_IOC_DISABLE     _IO(LSENSOR_IOC_MAGIC, 2)
#define LSENSOR_IOC_GET_STATUS  _IOR(LSENSOR_IOC_MAGIC, 3, unsigned int)
#define LSENSOR_IOC_CALIBRATION _IOR(LSENSOR_IOC_MAGIC, 4, struct lsensor_cal_data)
#define LSENSOR_IOC_ALS_WAKE    _IO(LSENSOR_IOC_MAGIC, 9)
#define LSENSOR_IOC_UPDATE_GA   _IOW(LSENSOR_IOC_MAGIC, 10, unsigned int)

#define LSENSOR_IOC_RESET       _IO(LSENSOR_IOC_MAGIC, 21)
#define LSENSOR_IOC_ENG_ENABLE  _IO(LSENSOR_IOC_MAGIC, 22)
#define LSENSOR_IOC_ENG_DISABLE _IO(LSENSOR_IOC_MAGIC, 23)
#define LSENSOR_IOC_ENG_CTL_R   _IOR(LSENSOR_IOC_MAGIC, 24, struct lsensor_eng_data)
#define LSENSOR_IOC_ENG_CTL_W   _IOW(LSENSOR_IOC_MAGIC, 25, struct lsensor_eng_data)
#define LSENSOR_IOC_ENG_INFO    _IOR(LSENSOR_IOC_MAGIC, 26, struct lsensor_info_data)

#endif

