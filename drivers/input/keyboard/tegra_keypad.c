/*
 * drivers/input/keyboard/tegra_keypad.c
 *
 * Keyboard class input driver for the NVIDIA Tegra SoC internal matrix
 * keyboard controller
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






#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/earlysuspend.h>
#include <mach/luna_hwid.h>
#include <linux/wakelock.h>
#include <linux/reboot.h>
#include <linux/switch.h>
#include <mach/tegra_keypad.h>
#define TCH_DBG(fmt, args...) printk(KERN_INFO "KEYPAD: " fmt, ##args)

static int DLL=0;
module_param_named( 
	DLL, DLL,
 	int, S_IRUGO | S_IWUSR | S_IWGRP
)
#define INFO_LEVEL  1
#define ERR_LEVEL   2
#define MY_INFO_PRINTK(level, fmt, args...) if(level <= DLL) printk( fmt, ##args);
#define PRINT_IN MY_INFO_PRINTK(4,"KEYPAD:+++++%s++++ %d\n",__func__,__LINE__);
#define PRINT_OUT MY_INFO_PRINTK(4,"KEYPAD:----%s---- %d\n",__func__,__LINE__);

static int WIFIVersion = 0;

struct keypad_t {
	struct input_dev         *keyarray_input;
    int                      open_count;
	int                      key_size; 
	int						 keypad_suspended; 
	int                      misc_open_count; 
    struct workqueue_struct  *key_wqueue;
	struct key_t             keys[MAX_SUPPORT_KEYS];
	struct early_suspend     key_early_suspend; 
	struct mutex	mutex;
	struct wake_lock wake_lock;
	struct wake_lock pwr_key_keep_1s_awake_wake_lock;
	struct delayed_work      pwrkey_work;
	struct workqueue_struct *pwrkey_wqueue;
	int	irq_requested;
    struct wake_lock capsensor_wakelock;
	struct switch_dev sdev;
	int chk_SAR_state;
	int fvs_mode;
	struct delayed_work capsensor_work;
	struct workqueue_struct *capsensor_wqueue;
	int detect_state;
	uint32_t capsensor_detect_count;
	uint32_t capsensor_detect_count_fvs;
	struct key_t *capsensor_key;
	int fvs_mode_sar;
	uint32_t capsensor_powercut;
	struct regulator 		*vdd_regulator; 
	int sar_pwr_gpio_num; 
};


static struct keypad_t         *g_kp;
static int __init keypad_probe(struct platform_device *pdev);
static irqreturn_t keypad_irqHandler(int irq, void *dev_id);

extern void Tps6586x_set_EXITSLREQ_clear_SYSINEN(void);
extern void Tps6586x_clear_EXITSLREQ_set_SYSINEN(void);
static ssize_t switch_cap_print_state(struct switch_dev *sdev, char *buf)
{

	uint32_t pinValue;
	uint32_t sar_rek_value;
	PRINT_IN
	switch (switch_get_state(&g_kp->sdev)) {
	case 0:
		pinValue = 1;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR is press p[%x]\n",pinValue);
	PRINT_OUT
		return sprintf(buf, "%x\n",pinValue);
	case 1:
		pinValue = 0;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR is release p[%x]\n",pinValue);
	PRINT_OUT
		return sprintf(buf,"%x\n",pinValue);
	case 2:
		sar_rek_value = 2;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR re k p[%x]\n",sar_rek_value);
	PRINT_OUT
		return sprintf(buf,"%x\n",sar_rek_value);
	}
    
	PRINT_OUT
	return -EINVAL;
}

static void keypad_irqWorkHandler( struct work_struct *work )
{
	int pinValue;
	struct key_t *key = container_of(work,struct key_t,key_work.work);
	
 	int send_key_event_flag = 0;
 	
	PRINT_IN
#if 0
	if(!rt_task(current))
    {
 		if(sched_setscheduler_nocheck(current, SCHED_FIFO, &s)!=0)
		{
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "fail to set rt pri...\n" );
		}
		else
		{
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "set rt pri...\n" );
		}
    }
#endif
	
	mutex_lock(&g_kp->mutex);
	if(key->key_code == KEY_POWER)
	{
		wake_lock_timeout(&g_kp->pwr_key_keep_1s_awake_wake_lock,2*HZ);
	}
	msleep(5); 
	for(;;)
	{
		
		pinValue = gpio_get_value(key->gpio_num); 
		MY_INFO_PRINTK( 1,"INFO_LEVEL:" "gpio_get_value[%d] : %d\n",key->gpio_num, gpio_get_value(key->gpio_num) );
		MY_INFO_PRINTK( 1,"INFO_LEVEL:" "pinValue : %d\n", pinValue );
		if(pinValue != key->state)
		{
			if( 0 == pinValue )
			{
				printk("keycode[%d] is pressed\n",key->key_code);
				input_report_key(g_kp->keyarray_input, key->key_code, 1);
				input_sync(g_kp->keyarray_input);
				send_key_event_flag = 1;
				if(key->key_code == KEY_POWER)
				{
					wake_lock(&g_kp->wake_lock);					
					
					queue_delayed_work(g_kp->pwrkey_wqueue, &g_kp->pwrkey_work, 8*HZ);
				}
				#if 0
				else if(key->key_code == KEY_F1)
				{
					MY_INFO_PRINTK( 1,"INFO_LEVEL:"" cap sensor is pressed p[%x]\n",pinValue);
					wake_lock_timeout(&g_kp->capsensor_wakelock,5*HZ);
					switch_set_state(&g_kp->sdev, pinValue);
				}
				#endif
				
				set_irq_type(key->irq,IRQF_TRIGGER_HIGH);
				
				
			}
			else
			{
				printk("keycode[%d] is released\n",key->key_code);
				input_report_key(g_kp->keyarray_input, key->key_code, 0);
				input_sync(g_kp->keyarray_input);
				send_key_event_flag = 1;
				if(key->key_code == KEY_POWER)
				{	
					
					while(cancel_delayed_work_sync(&g_kp->pwrkey_work));
					wake_unlock(&g_kp->wake_lock);
				}
				#if 0
				else if(key->key_code == KEY_F1)
				{
					MY_INFO_PRINTK( 1,"INFO_LEVEL:"" cap sensor is released p[%x]\n",pinValue);
					switch_set_state(&g_kp->sdev, pinValue);
					wake_lock_timeout(&g_kp->capsensor_wakelock,5*HZ);
				}
				#endif
				
				set_irq_type(key->irq,IRQF_TRIGGER_LOW);
				
				MY_INFO_PRINTK( 1,"INFO_LEVEL:" "gpio_set_value[%d] : %d\n",key->gpio_num, gpio_get_value(key->gpio_num) );
			}
			key->state = pinValue;
		}
		else
		{
			
			break;
		}
	}
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "before enable_irq()\n");
	
	enable_irq(key->irq);
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "enable irq num : %d\n",key->irq );
	
	if(send_key_event_flag == 0)
	{
		input_report_key(g_kp->keyarray_input, key->key_code, key->state?1:0);
		input_sync(g_kp->keyarray_input);
		input_report_key(g_kp->keyarray_input, key->key_code, key->state?0:1);
		input_sync(g_kp->keyarray_input);
	}

	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return;
}

static irqreturn_t keypad_irqHandler(int irq, void *dev_id)
{
    struct key_t *key = (struct key_t *)dev_id;
    	
	PRINT_IN
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "before disable_irq ()\n");
	disable_irq_nosync(irq);
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "before disable_irq num : %d\n",irq );
	queue_delayed_work(g_kp->key_wqueue, &key->key_work, 0);
    PRINT_OUT
	return IRQ_HANDLED;
}

static ssize_t kp_misc_write( struct file *fp,
                              const char __user *buffer,
                              size_t count,
                              loff_t *ppos )
{
	char echostr[ECHOSTR_SIZE];
    
	PRINT_IN
	if ( count > ECHOSTR_SIZE )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "ts_misc_write: invalid count %d\n", count );
        return -EINVAL;
    }
    
	if ( copy_from_user(echostr,buffer,count) )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "copy Echo String from user failed: %s\n",echostr);
		return -EINVAL;
	}
	mutex_lock(&g_kp->mutex);
	echostr[count-1]='\0';
	if ( strcmp(echostr,"enter fvs" ) == 0 )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		g_kp->keys[1].key_code = KEY_F8;
		g_kp->keys[2].key_code = KEY_F9;
		g_kp->fvs_mode = 1;
		
		gpio_set_value(g_kp->sar_pwr_gpio_num,0);
		MY_INFO_PRINTK( 1, "INFO_LEVEL:""CapSensor_PowerOff !!!\n");
		msleep(100);
		gpio_set_value(g_kp->sar_pwr_gpio_num,1);
		MY_INFO_PRINTK( 1, "INFO_LEVEL:""CapSensor_PowerOn !!!\n");
		g_kp->fvs_mode_sar = 1;
	}
	else if ( strcmp(echostr,"exit fvs" ) == 0 )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		g_kp->keys[1].key_code = KEY_VOLUMEDOWN;
		g_kp->keys[2].key_code = KEY_VOLUMEUP;
		g_kp->fvs_mode = 0;
		g_kp->fvs_mode_sar = 0;
	}
	else if( strcmp(echostr,"enable SAR" ) == 0  )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		
		g_kp->chk_SAR_state = 1;
	}
	else if( strcmp(echostr,"disable SAR" ) == 0  )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		
		g_kp->chk_SAR_state = 0;
	}
	else
	{
		MY_INFO_PRINTK(2, "ERROR_LEVEL:" "capkey in deep sleep mode and can't enter FVS mode\n");
	}
	mutex_unlock(&g_kp->mutex);
	PRINT_OUT
    return count;    
}

static int keypad_poweron_device(struct keypad_t *g_kp, int on)
{
    int     rc = 0;

	PRINT_IN
    MY_INFO_PRINTK( 4,"INFO_LEVEL:""power on device %d\n", on );
	if(on)
	{
		g_kp->vdd_regulator = regulator_get(NULL,"vdd_bodysar"); 
		if (IS_ERR_OR_NULL(g_kp->vdd_regulator)) 
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "bodysar_poweron_device: couldn't get regulator vdd_bodysar\n");
			g_kp->vdd_regulator = NULL;
			rc = -EFAULT;
			goto out;
		}
		else
		{
			regulator_enable(g_kp->vdd_regulator);
			mdelay(5);
		}
    }
	else
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "bodysar_poweron_device: does not support turn on/off vdd\n");
	}
	
out:
	PRINT_OUT
	return rc;
}


static int keypad_release_gpio(struct keypad_t *g_kp)
{
    int i;
	
	PRINT_IN
	for( i = 0 ; i < g_kp->key_size; i++ )
	{
		MY_INFO_PRINTK(4,"TEST_INFO_LEVEL:""keypad_release_gpio: releasing gpio_num[%d] %d\n",i, g_kp->keys[i].gpio_num);
		gpio_free(g_kp->keys[i].gpio_num);
	}	
    PRINT_OUT 
    return 0;
}


static int keypad_setup_gpio( struct keypad_t *g_kp )
{
    int i;
	int rc = 0;
	
    PRINT_IN
	MY_INFO_PRINTK( 4,"INFO_LEVEL:" "setup gpio Input pin \n");
    
	for( i = 0 ; i < g_kp->key_size; i++ )
	{
		
		if((i == g_kp->key_size - 1) && WIFIVersion)
		{
			rc = gpio_request(g_kp->keys[i].gpio_num, "BodySAR_PWR_GPIO");
			if (rc)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: request gpio_num[%d] %d failed (rc=%d)\n",i, g_kp->keys[i].gpio_num, rc );
				keypad_release_gpio(g_kp);
				PRINT_OUT
				return rc;
			}
			rc = gpio_direction_output(g_kp->keys[i].gpio_num, 0);
			if (rc)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: set gpio_num[%d] %d mode failed (rc=%d)\n",i, g_kp->keys[i].gpio_num, rc );
				keypad_release_gpio(g_kp);
				PRINT_OUT
				return rc;
			}
			
			
			gpio_set_value(g_kp->keys[i].gpio_num, 0);
			printk("config SAR_status gpio : output low\n");
		}
		else
		{
			rc = gpio_request(g_kp->keys[i].gpio_num, "ventana_keypad_irq");
			if (rc)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: request gpio_num[%d] %d failed (rc=%d)\n",i, g_kp->keys[i].gpio_num, rc );
				keypad_release_gpio(g_kp);
				PRINT_OUT
				return rc;
			}
			MY_INFO_PRINTK( 1,"INFO_LEVEL:""gpio_request[%d]:%d\n",i,g_kp->keys[i].gpio_num);
			rc = gpio_direction_input(g_kp->keys[i].gpio_num);
			if (rc)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: set gpio_num[%d] %d mode failed (rc=%d)\n",i, g_kp->keys[i].gpio_num, rc );
				keypad_release_gpio(g_kp);
				PRINT_OUT
				return rc;
			}
			MY_INFO_PRINTK( 1,"INFO_LEVEL:""gpio_direction_input[%d]:%d\n",i,g_kp->keys[i].gpio_num);
		}	
	}
	
	
	if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
	{
		rc = gpio_request(g_kp->sar_pwr_gpio_num, "BodySAR_PWR_GPIO");
		if (rc)
		{
			MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: request gpio_sar_pwr %d failed (rc=%d)\n",g_kp->sar_pwr_gpio_num, rc );
			gpio_free(g_kp->sar_pwr_gpio_num);
			PRINT_OUT
			return rc;
		}
		rc = gpio_direction_output(g_kp->sar_pwr_gpio_num, 0);
		if (rc)
		{
			MY_INFO_PRINTK( 2,"ERROR_LEVEL:""keypad_setup_gpio: set gpio_sar_pwr %d mode failed (rc=%d)\n", g_kp->sar_pwr_gpio_num, rc);
			gpio_free(g_kp->sar_pwr_gpio_num);
			PRINT_OUT
			return rc;
		}
		MY_INFO_PRINTK( 1,"INFO_LEVEL:""gpio_direction_output[%d]:%d\n",i,g_kp->sar_pwr_gpio_num);
		
		if(WIFIVersion) 
		{
			
			gpio_set_value(g_kp->sar_pwr_gpio_num, 0);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""keypad_setup_gpio: setup SAR_PWR state : low becasue of WIFIVersion\n");
			printk("config SAR_PWR gpio[%d] : output low\n",g_kp->sar_pwr_gpio_num);
		}
		else 
		{
			
			gpio_set_value(g_kp->sar_pwr_gpio_num, 1);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""keypad_setup_gpio: setup SAR_PWR state : high\n");
			printk("config SAR_PWR gpio[%d] : output high\n",g_kp->sar_pwr_gpio_num);
		}
	}

	PRINT_OUT
    return rc;
}

#if 0
static int keypad_keyarray_event(struct input_dev *dev, unsigned int type,
             unsigned int code, int value)
{
  return 0;
}
#endif

static int read_BodySAR_thread(void *key)
{
	int ret = 0;
    uint32_t pinValue;
	struct key_t *capsensor = (struct key_t *)key;
 	struct task_struct *tsk = current;
 	ignore_signals(tsk);
 	set_cpus_allowed_ptr(tsk,cpu_all_mask);
 	current->flags |= PF_NOFREEZE | PF_FREEZER_NOSIG;
	PRINT_IN
	
	for(;;)
	{
		msleep(1000);
		mutex_lock(&g_kp->mutex);
		if(g_kp->keypad_suspended == 0 && g_kp->chk_SAR_state == 1)
		{
			pinValue = gpio_get_value(capsensor->gpio_num);
			switch_set_state(&g_kp->sdev, pinValue);
			capsensor->state = pinValue;
			MY_INFO_PRINTK( 1,"INFO_LEVEL:" "%s BodySAR p[%x]\n",__func__,pinValue);
        }
		mutex_unlock(&g_kp->mutex);	
	}
	PRINT_OUT
	return ret;
}


void luna_bodysar_callback(int up)
{
	uint32_t pinValue;
	uint32_t sar_rek_value;
	
	
	
	if(WIFIVersion)
		return
	mutex_lock(&g_kp->mutex);
	if(up == 0) 
	{
		g_kp->capsensor_powercut = 1; 
		if(g_kp->keypad_suspended == 0)
		{	
			if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
			{
				pinValue = 1;
				switch_set_state(&g_kp->sdev, pinValue);
				g_kp->capsensor_key->state = pinValue;
				printk("%s BodySAR p[%x]\n",__func__,pinValue);
				mutex_unlock(&g_kp->mutex);
				cancel_delayed_work_sync(&g_kp->capsensor_work);
				mutex_lock(&g_kp->mutex);
				
				gpio_set_value(g_kp->sar_pwr_gpio_num,0);
				printk("config SAR_PWR gpio : output low\n");
			}
		}
	}
	else 
	{
		g_kp->capsensor_powercut = 0; 
		if(g_kp->keypad_suspended == 0)
		{
			if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
			{
				
				gpio_set_value(g_kp->sar_pwr_gpio_num,1);
				g_kp->detect_state = CapSensor_Detectable;
				g_kp->capsensor_detect_count = 0;
				g_kp->capsensor_detect_count_fvs = 0;
				sar_rek_value = 2;
				switch_set_state(&g_kp->sdev, sar_rek_value);
				queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
				printk("config SAR_PWR gpio : output high\n");
			}
		}
		else
		{
			
		}
	}
	
	mutex_unlock(&g_kp->mutex);
}
EXPORT_SYMBOL(luna_bodysar_callback);

static void detect_capsensor( struct work_struct *work )
{
	uint32_t pinValue;
    uint32_t sar_rek_value;
	
	PRINT_IN
	mutex_lock(&g_kp->mutex);
	if(g_kp->keypad_suspended == 1)
	{
		
		mutex_unlock(&g_kp->mutex);
		return;
	}
	switch(g_kp->detect_state)
	{
		case CapSensor_Detectable:
			if(g_kp->keypad_suspended == 0 && g_kp->chk_SAR_state == 1)
			{
				
				pinValue = gpio_get_value(g_kp->capsensor_key->gpio_num);
				switch_set_state(&g_kp->sdev, pinValue);
				g_kp->capsensor_key->state = pinValue;
				MY_INFO_PRINTK( 1,"INFO_LEVEL:" "%s BodySAR p[%x]\n",__func__,pinValue);
			}
			g_kp->capsensor_detect_count++;
			if(g_kp->fvs_mode_sar == 1)
			{
				g_kp->capsensor_detect_count_fvs++;
				MY_INFO_PRINTK( 1,"INFO_LEVEL:" "%s enter fvs mode\n",__func__);
				if(g_kp->capsensor_detect_count_fvs >= 9)
				{
					
					g_kp->detect_state = CapSensor_PowerOff;
				}	
			}
			else
			{
				if(g_kp->capsensor_detect_count >= 29)
				{
					g_kp->detect_state = CapSensor_PowerOff;
				}	
			}
			
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 1*HZ);
			
		break;
		
		case CapSensor_PowerOff:
			
			gpio_set_value(g_kp->sar_pwr_gpio_num,0);
			MY_INFO_PRINTK( 1,"INFO_LEVEL:" "CapSensor_PowerOff !!!\n");
			g_kp->detect_state = CapSensor_PowerOn;
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 1*HZ);
			
		break;
		
		case CapSensor_PowerOn:
			
			gpio_set_value(g_kp->sar_pwr_gpio_num,1);
			MY_INFO_PRINTK( 1,"INFO_LEVEL:" "CapSensor_PowerOn !!!\n");
			g_kp->detect_state = CapSensor_Detectable;
			if (g_kp->fvs_mode_sar == 1)
			{
				g_kp->capsensor_detect_count_fvs = 0;
			}
			else
			{
				g_kp->capsensor_detect_count = 0;
			}
			sar_rek_value = 2;
			switch_set_state(&g_kp->sdev, sar_rek_value);
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
			
		break;
		
		default:
			
		break;
	}
	mutex_unlock(&g_kp->mutex);
	PRINT_OUT
	
}

static int keypad_keyarray_open(struct input_dev *dev)
{
    int rc = 0;
    int i;
	
    PRINT_IN
    mutex_lock(&g_kp->mutex);	
    
    if(g_kp->open_count == 0)
    {	
		if(!g_kp->irq_requested)
		{
		for ( i = 0 ; i < g_kp->key_size ; i++ )
		{
			if(g_kp->keys[i].key_code != KEY_F1)
			{
				rc = request_irq( g_kp->keys[i].irq, keypad_irqHandler, IRQF_TRIGGER_LOW,"KEYPAD_IRQ",(void*)&g_kp->keys[i] );
				if (rc)
				{
					MY_INFO_PRINTK( 2, "ERROR_LEVEL:""keypad irq[%d] %d requested failed\n",i, g_kp->keys[i].irq);
					rc = -EFAULT;
					PRINT_OUT
					return rc;
				}
				else
				{
					MY_INFO_PRINTK( 4, "INFO_LEVEL:""keypad irq[%d] %d requested successfully\n",i, g_kp->keys[i].irq);
					MY_INFO_PRINTK( 4, "INFO_LEVEL:""keypad dev_id[%d] %p \n",i, &g_kp->keys[i]);
				}
			}
			else
			{
				if(!WIFIVersion) 
				{
					if(system_rev >= EVT2 && system_rev < EVT3)
					{
						kernel_thread(read_BodySAR_thread, &g_kp->keys[i], CLONE_FS | CLONE_FILES);
					}
					else if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
					{
						g_kp->capsensor_key = &g_kp->keys[i];
						INIT_DELAYED_WORK( &g_kp->capsensor_work, detect_capsensor );
						g_kp->capsensor_wqueue = create_singlethread_workqueue("detect_capsensor");
						g_kp->detect_state = CapSensor_Detectable;
						g_kp->capsensor_detect_count = 0;
						g_kp->capsensor_detect_count_fvs = 0;
						queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
						printk("int work and work Q for sar\n");			
					}
				}	
			}
        }
	}
		g_kp->irq_requested = 1;
		g_kp->open_count++; 
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "open count : %d\n",g_kp->open_count );
    }
	else
	{
        rc = -EFAULT;
        MY_INFO_PRINTK( 4,"INFO_LEVEL:" "opened %d times previously\n", g_kp->open_count);
    }
    mutex_unlock(&g_kp->mutex);
    
	PRINT_OUT
    return rc;
}

static void keypad_keyarray_close(struct input_dev *dev)
{
	
	
	PRINT_IN
	mutex_lock(&g_kp->mutex);
	if(  g_kp->open_count > 0 )
    {
        g_kp->open_count--;

#if 0
        MY_INFO_PRINTK( 4,"INFO_LEVEL:" "still opened %d times\n",  g_kp->open_count );
        for ( i = 0 ; i < g_kp->key_size; i++ )
		{		
			if(g_kp->keys[i].key_code != KEY_F1)
			{
				MY_INFO_PRINTK( 4,"INFO_LEVEL:" "irq[%d] %d will be freed\n",i,g_kp->keys[i].gpio_num );	
				
					
				gpio_free(g_kp->keys[i].gpio_num);	
			}
		}
#endif
    }

	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return;
}

static int kp_misc_release(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_kp->mutex);
    if( g_kp->misc_open_count )
    {
        g_kp->misc_open_count--;      
    }
    mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return result;
}

static int kp_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_kp->mutex);
    if( g_kp->misc_open_count ==0 )
    {
        g_kp->misc_open_count++;
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "misc open count : %d\n",g_kp->misc_open_count );          
    }	
    else
    { 
		result = -EFAULT;
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to open misc count : %d\n",g_kp->misc_open_count );  
    }
    mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return result;
}

static struct file_operations kp_misc_fops = {
	.owner 	= THIS_MODULE,
	.open 	= kp_misc_open,
	.release = kp_misc_release,
	.write = kp_misc_write,
	
    
};
static struct miscdevice kp_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "misc_keypad",
	.fops 	= &kp_misc_fops,
};


static void keypad_suspend(struct early_suspend *h)
{
    int ret = 0;
	int pinValue;
    struct keypad_t *g_kp = container_of( h,struct keypad_t,key_early_suspend);
	
	int i;
	
	PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL:" "keypad_suspend : E\n" );
	mutex_lock(&g_kp->mutex);
	if( g_kp->keypad_suspended )
	{
		mutex_unlock(&g_kp->mutex);
		PRINT_OUT
		return;
	}
	g_kp->keypad_suspended = 1;
	
	
	if( !WIFIVersion && g_kp->capsensor_powercut != 1)
	{
		if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
		{
			pinValue = 1;
			switch_set_state(&g_kp->sdev, pinValue);
			g_kp->capsensor_key->state = pinValue;
			printk("%s BodySAR p[%x]\n",__func__,pinValue);
			mutex_unlock(&g_kp->mutex);
			cancel_delayed_work_sync(&g_kp->capsensor_work);
			mutex_lock(&g_kp->mutex);
			
			gpio_set_value(g_kp->sar_pwr_gpio_num,0);
			printk("config SAR_PWR gpio : output low\n");
		}
	}
	
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1 )
		{
			
			disable_irq(g_kp->keys[i].irq);
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "disable keypad irq[%d] : %d\n",i,g_kp->keys[i].irq );
		}
		else
		{
			
		}
	}	
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1 )
		{
			mutex_unlock(&g_kp->mutex);
			ret = cancel_work_sync(&g_kp->keys[i].key_work.work);
			mutex_lock(&g_kp->mutex);
			if (ret) 
			{
				
				
				enable_irq(g_kp->keys[i].irq);
				MY_INFO_PRINTK( 1, "INFO_LEVEL:" "enable keypad irq[%d] : %d\n",i,g_kp->keys[i].irq );
			}
		}
    }
	
	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
	return;
}


static void keypad_resume(struct early_suspend *h)
{
    int pinValue;
	uint32_t sar_rek_value;
    struct keypad_t *g_kp = container_of( h,struct keypad_t,key_early_suspend);
	int i;
	
    PRINT_IN
	mutex_lock(&g_kp->mutex);
    if( 0 == g_kp->keypad_suspended )
	{
		mutex_unlock(&g_kp->mutex);
		PRINT_OUT
		return;
	} 
        
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		
		pinValue = gpio_get_value(g_kp->keys[i].gpio_num);
		if( 0 == pinValue )
		{
			
			set_irq_type(g_kp->keys[i].irq, IRQF_TRIGGER_HIGH);
			
		}
		else
		{
			
			set_irq_type(g_kp->keys[i].irq, IRQF_TRIGGER_LOW);
			
		}
		g_kp->keys[i].state = pinValue;
	}
	
	
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1)
		{
			
			enable_irq(g_kp->keys[i].irq);
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "enable keypad irq[%d] : %d\n",i,g_kp->keys[i].irq );
		}
	}	
	g_kp->keypad_suspended = 0;
	
	
	
	if( !WIFIVersion && g_kp->capsensor_powercut != 1)
	{
		if(system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
		{
			
			gpio_set_value(g_kp->sar_pwr_gpio_num,1);
			g_kp->detect_state = CapSensor_Detectable;
			g_kp->capsensor_detect_count = 0;
			g_kp->capsensor_detect_count_fvs = 0;
			sar_rek_value = 2;
			switch_set_state(&g_kp->sdev, sar_rek_value);
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
			printk("config SAR_PWR gpio : output high\n");
		}
	}
	
    mutex_unlock(&g_kp->mutex);
	PRINT_OUT
	return;
}

static int keyarray_register_input( struct input_dev **input,
                              struct platform_device *pdev )
{
  int rc = 0;
  struct input_dev *input_dev;
  int i;
  
  input_dev = input_allocate_device();
  if ( !input_dev )
  {
    rc = -ENOMEM;
    return rc;
  }

  input_dev->name = KEYPAD_DRIVER_NAME;
  input_dev->phys = "tegra_keypad_key/event0";
  input_dev->id.bustype = BUS_I2C;
  input_dev->id.vendor = 0x0001;
  input_dev->id.product = 0x0002;
  input_dev->id.version = 0x0100;

  input_dev->open = keypad_keyarray_open;
  input_dev->close = keypad_keyarray_close;
  

  input_dev->evbit[0] = BIT_MASK(EV_KEY) ;
  
	set_bit(KEY_POWER, input_dev->keybit);
	set_bit(KEY_VOLUMEDOWN, input_dev->keybit);
	set_bit(KEY_VOLUMEUP, input_dev->keybit);
	#if 0
	if (system_rev == EVT1A)
	{
		set_bit(KEY_MENU, input_dev->keybit);
		set_bit(KEY_BACK, input_dev->keybit);
		set_bit(KEY_HOME, input_dev->keybit);
	}
	else if (system_rev >= EVT2)
	#endif
	if (system_rev >= EVT2 || system_rev == HWID_UNKNOWN)
	{
		set_bit(KEY_F1, input_dev->keybit); 
	}
	
	
	set_bit(KEY_F8, input_dev->keybit);
	set_bit(KEY_F9, input_dev->keybit);
	g_kp->keys[0].key_code = KEY_POWER;
	g_kp->keys[1].key_code = KEY_VOLUMEDOWN;
	g_kp->keys[2].key_code = KEY_VOLUMEUP;
	#if 0
	if (system_rev == EVT1A)
	{
		g_kp->keys[3].key_code = KEY_MENU;
		g_kp->keys[4].key_code = KEY_BACK;
		g_kp->keys[5].key_code = KEY_HOME;
	}
    else if (system_rev >= EVT2)
	#endif
	if (system_rev >= EVT2 || system_rev == HWID_UNKNOWN)
	{
		g_kp->keys[3].key_code = KEY_F1;
	}
	
	for(i = 0;i< g_kp->key_size ;i++)
	{
		g_kp->keys[i].state = 1;
	}
  
	rc = input_register_device( input_dev );
	if ( rc )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to register keyarray input device\\n");
		input_free_device( input_dev );
	}
	else
	{
		*input = input_dev;
	}
	return rc;
}


static struct platform_driver tegra_keypad_driver = {
	.driver	 = {
		.name   = KEYPAD_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	  = keypad_probe,
	
	
};

static void pwrkey_work_func(struct work_struct *work)
{
  printk("[KEY]## %s+\n",__func__);
  msleep(1);
  
  
  printk("[KEY]## %s-\n",__func__);
}

static int __init keypad_probe(struct platform_device *pdev)
{
	struct tegra_keypad_platform_data_t *pdata;
	int    result;
	int i;
 
    PRINT_IN
	
    g_kp = kzalloc( sizeof(struct keypad_t), GFP_KERNEL );
    if( !g_kp )
    {
        result = -ENOMEM;
        return result;
    }
	pdata = pdev->dev.platform_data;
	
	if ( system_rev == EVT1A)
	{
		g_kp->key_size = NUM_OF_1A_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT1A\n",g_kp->key_size);
	}
	else if (system_rev == EVT1B || system_rev == EVT1_3)
	{
		g_kp->key_size = NUM_OF_1B_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT1B\n",g_kp->key_size);
	}
	else
	{
		g_kp->key_size = NUM_OF_2_1_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT2_1\n",g_kp->key_size);
	}
	g_kp->keys[0].gpio_num = pdata->gpio_power;
	g_kp->keys[1].gpio_num = pdata->gpio_voldown;
	g_kp->keys[2].gpio_num = pdata->gpio_volup;
	
	
	
	if (system_rev >= EVT2 || system_rev == HWID_UNKNOWN)
	{
		g_kp->keys[3].gpio_num = pdata->gpio_bodysar;
	}
	if ( system_rev >= EVT3 || system_rev == HWID_UNKNOWN)
	{
		g_kp->sar_pwr_gpio_num = pdata->gpio_bodysar_pwr;
	}
	for(i = 0 ; i < g_kp->key_size; i++)
	{
		g_kp->keys[i].irq = TEGRA_GPIO_TO_IRQ(g_kp->keys[i].gpio_num);
	}
	platform_set_drvdata(pdev, g_kp);
	mutex_init(&g_kp->mutex);
	wake_lock_init(&g_kp->wake_lock, WAKE_LOCK_SUSPEND, "power_key_lock");
	wake_lock_init(&g_kp->pwr_key_keep_1s_awake_wake_lock, WAKE_LOCK_SUSPEND, "pwr_key_keep_1s_awake_wake_lock");
	
	wake_lock_init(&g_kp->capsensor_wakelock, WAKE_LOCK_SUSPEND, "cap_sensor");
	INIT_DELAYED_WORK(&g_kp->pwrkey_work, pwrkey_work_func);
	g_kp->pwrkey_wqueue = create_singlethread_workqueue("pwrkey_workqueue");
	
	
	if(system_rev >= EVT2 && system_rev < EVT3 && (!WIFIVersion)) 
	{
    	result = keypad_poweron_device(g_kp,1);
    	if(result)
		{
    		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "keypad_probe: failed to power on device\n" );
			keypad_release_gpio(g_kp);
			kfree(g_kp);
        	PRINT_OUT
        	return result;
    	}
    }
	
	
    result = keypad_setup_gpio( g_kp );
    if( result )
	{
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to setup gpio_keyarray\n" );
        keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	
	enable_irq_wake(g_kp->keys[0].irq);
	
	
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		INIT_DELAYED_WORK( &g_kp->keys[i].key_work, keypad_irqWorkHandler );
	}	
	
	
    g_kp->key_wqueue = create_singlethread_workqueue("keypad_Wqueue");
    if (!g_kp->key_wqueue)
	{
		switch_dev_unregister(&g_kp->sdev);
		keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	
	result = keyarray_register_input( &g_kp->keyarray_input, NULL );
	if( result )
    {
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to register keyarray input\n" ); 
    	keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	input_set_drvdata(g_kp->keyarray_input, g_kp);
	
	
	if(system_rev >= EVT2 || system_rev == HWID_UNKNOWN)
	{
		if (!WIFIVersion)
		{
			g_kp->sdev.name = pdev->name;
			g_kp->sdev.print_name = switch_cap_print_state;
			result = switch_dev_register(&g_kp->sdev);
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed register switch driver\n" );
				keypad_release_gpio( g_kp );
				kfree(g_kp);
				PRINT_OUT
				return result;
			}
		}	
	}
	
	
	result = misc_register( &kp_misc_device );
    if( result )
    {
       	MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed register misc driver\n" );
		
		if(!WIFIVersion) switch_dev_unregister(&g_kp->sdev);
    	keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	
    g_kp->key_early_suspend.level = 150; 
    g_kp->key_early_suspend.suspend = keypad_suspend;
    g_kp->key_early_suspend.resume = keypad_resume;
    register_early_suspend(&g_kp->key_early_suspend);
    PRINT_OUT
    return 0;
}


extern int IsWIFIVersion(void);

static int __devinit keypad_init(void)
{
	int rc = 0;
	PRINT_IN
	MY_INFO_PRINTK( 1,"INFO_LEVEL:"" keypad system_rev=0x%x\n",system_rev);
	
	WIFIVersion = IsWIFIVersion();
	printk("system_rev=0x%x WIFI=%d\n",system_rev,WIFIVersion);
	rc = platform_driver_register(&tegra_keypad_driver);
	PRINT_OUT
	return rc;
}

static void __exit keypad_exit(void)
{
	PRINT_IN
	platform_driver_unregister(&tegra_keypad_driver);
	input_unregister_device(g_kp->keyarray_input);
	input_free_device(g_kp->keyarray_input);
    destroy_workqueue(g_kp->pwrkey_wqueue);
    keypad_release_gpio(g_kp);
	kfree(g_kp);
	PRINT_OUT
}

module_init(keypad_init);
module_exit(keypad_exit);

MODULE_DESCRIPTION("Tegra keypad driver");
