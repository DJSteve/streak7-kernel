/*  drivers/input/keyboard/ata2538_capkey.c
 *
 *  Copyright (c) 2008 QUALCOMM USA, INC.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, you can find it at http:
 *
 *  Driver for QWERTY keyboard with I/O communications via
 *  the I2C Interface. The keyboard hardware is a reference design supporting
 *  the standard XT/PS2 scan codes (sets 1&2).
 */





#include <linux/kernel.h>
#include <linux/module.h>
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
#include <linux/slab.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/earlysuspend.h>
#include <mach/ATA2538_capkey.h>
#include <mach/ATA2538_capkey_init_data.h>
#include <mach/luna_hwid.h>




static int DLL=0;
module_param_named( 
	DLL, DLL,
 	int, S_IRUGO | S_IWUSR | S_IWGRP
)
#define INFO_LEVEL  1
#define ERR_LEVEL   2
#define MY_INFO_PRINTK(level, fmt, args...) if(level <= DLL) printk( fmt, ##args);
#define PRINT_IN MY_INFO_PRINTK(4,"+++++%s++++ %d\n",__func__,__LINE__);
#define PRINT_OUT MY_INFO_PRINTK(4,"----%s---- %d\n",__func__,__LINE__);

static int A02Version = 0;
struct capkey_t {
	struct i2c_client        *client_4_i2c;
    struct input_dev         *input_dev;
	struct regulator 		*vdd_regulator;
	int                      irq; 
	int                      gpio_irq; 
    int                      gpio_rst; 
	uint8_t                  i2c_addr;
	int                      open_count; 
	int                      misc_open_count; 
	int						 capkey_suspended; 
	int                      fvs_mode_flag;  
	struct delayed_work      capkey_work;
    struct workqueue_struct  *capkey_wqueue;
	struct early_suspend     capkey_early_suspend; 
	struct mutex             mutex;
	uint32_t capkey_powercut; 
};	

static struct capkey_t         *g_ck;
static int __devinit capkey_probe(struct i2c_client *client, const struct i2c_device_id *id);
int  capkey_LED_power_switch(int on);

static int capkey_write_i2c( struct i2c_client *client,
							   uint16_t			regBuf,
                               uint8_t          *dataBuf,
                               uint8_t          dataLen )
{
    int     result;
    uint8_t *buf = NULL;
    
    struct  i2c_msg msgs[] = { 
        [0] = {
            .addr   = g_ck->i2c_addr,
            .flags  = 0,
            .buf    = (void *)buf,
            .len    = 0
        }
    };
    
    PRINT_IN
    buf = kzalloc( dataLen+sizeof(regBuf), GFP_KERNEL );
    if( NULL == buf )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""touchpad_write_i2c: alloc memory failed\n" );
        return -EFAULT;
    }
    
    buf[0] = regBuf;
    memcpy( &buf[1], dataBuf, dataLen );
    msgs[0].buf = buf;
    msgs[0].len = dataLen+1;

    result = i2c_transfer( client->adapter, msgs, 1 );
    if( result != ARRAY_SIZE(msgs) )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""capkey_write_i2c: write 0x%x %d bytes return failure, %d\n", result, buf[0], dataLen );
        kfree( buf );
        return result;
    }
    kfree(buf);
    PRINT_OUT
    return 0;
}

static int capkey_read_i2c( struct i2c_client *client,
                            uint16_t           regBuf,
                            uint8_t           *dataBuf,
                            uint8_t           dataLen )
{
    int     result;
	
    struct  i2c_msg msgs[] = { 
        [0] = {
            .addr   = g_ck->i2c_addr,
            .flags  = 0,      
            .buf    = (void *)&regBuf,
            .len    = 1
        },
        [1] = {                     
            .addr   = g_ck->i2c_addr,
            .flags  = I2C_M_RD,
            .buf    = (void *)dataBuf,
            .len    = dataLen
        }
    };
    
 PRINT_IN
    result = i2c_transfer( client->adapter, msgs, ARRAY_SIZE(msgs) );
    if( result != ARRAY_SIZE(msgs) )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""read %Xh %d bytes return failure, %d\n", result, regBuf, dataLen );
        PRINT_OUT
        return result;
    }
 PRINT_OUT
    return 0;
}

static int capkey_config_ATA2538(struct capkey_t *g_ck)
{
    struct i2c_client *client = g_ck->client_4_i2c;
	int i;
	int result;
    uint8_t value;
	uint8_t data[ALPHA_SIZE];
    uint8_t warm_reset_data;
	
    PRINT_IN
    
    value = 0xFF;
    result = capkey_write_i2c(client,ADDR_REG_CHECK,&value,1);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg_check 0X%Xh = 0x%X\n",ADDR_REG_CHECK, value );
        PRINT_OUT
        return result;
    }
    MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "write reg_check 0X%Xh = 0x%X\n",ADDR_REG_CHECK,value );
	
    
    for ( i = 0; i < ALPHA_SIZE ; i++ )
    {
		
		if (system_rev <= EVT1_3)
		{
			data[0] = init_data_alpha[i];
		}
		else if (system_rev == EVT2)
		{
			data[0] = init_data_alpha_evt2_1[i];
		}
		else if (system_rev >= EVT2_2 && system_rev <= EVT2_4)
		{
			data[0] = init_data_alpha_evt2_2[i];
		}
		else 
		{
			data[0] = init_data_alpha_evt3[i];
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "read 0x0%d = 0x%X\n",i, data[0] );
		}
    	result = capkey_write_i2c(client,i,data,1);
    	if ( result )
    	{
    		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg_check 0X%Xh = 0x%x\n",i,data[0] );
        	PRINT_OUT
        	return result;	
    	}
    }
	
    
    for ( i = ALPHA_SIZE; i < TOTAL_REG_SIZE; i++ )
    {	
    	if (A02Version) 
		{
			if (i == ALPHA_SIZE)
			{
				init_data_burst_evt3[i-ALPHA_SIZE] =  0x63;
				data[0]	= init_data_burst_evt3[i-ALPHA_SIZE];
			}
			else
			{
				data[0]	= init_data_burst_evt3[i-ALPHA_SIZE];
			}
		}
		else 
		{
			if (system_rev <= EVT1_3)
			{
				data[0]	= init_data_burst[i-ALPHA_SIZE];
			}
			else if (system_rev == EVT2)
			{
				data[0] = init_data_burst_evt2_1[i-ALPHA_SIZE];
			}
			else if (system_rev >= EVT2_2 && system_rev <= EVT2_4)
			{
				data[0]	= init_data_burst_evt2_2[i-ALPHA_SIZE];
			}
			else 
			{
				data[0]	= init_data_burst_evt3[i-ALPHA_SIZE];
			}
		}
		if(i != ADDR_REG_CHECK)
		{
    		result = capkey_write_i2c(client,i,data,1);
    		if ( result )
    		{
    			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg init_data_burst 0x%Xh = 0x%x\n",i,data[0] );
        		PRINT_OUT
        		return result;
    		}	
        }
    }
    
    
    msleep(1);
    warm_reset_data = 0x01;
    result = capkey_write_i2c(client,ADDR_WARM_RESET,&warm_reset_data,1);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg warm reset 0xFFh, return %d\n", result );
        PRINT_OUT
        return result;
    }
    msleep(10);
    PRINT_OUT
    return result;
}

static int capkey_detect_ATA2538(struct capkey_t *g_ck)
{
    int result = 0;
	struct i2c_client *client = g_ck->client_4_i2c;
	int i;
    uint8_t value[ALPHA_SIZE];
     
    PRINT_IN
	result = capkey_read_i2c( client, ADD_PA0_ALPHA, &value[0], ALPHA_SIZE );				   
	if(result && value[0] != INIT_ALPHA_VALUE)
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg 0x00h and value failed 0x%x, return %d\n", value[0],result );
		PRINT_OUT
		result = -EFAULT;
		return result;
	}
	
	
	for (i = 0 ; i < ALPHA_SIZE ; i++)
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "read 0x0%d = 0x%X\n",i, value[i] );
	}
    PRINT_OUT
    return 0;
}

#if 0
static int capkey_release_gpio(struct capkey_t *g_ck )
{
    PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL:""capkey_release_gpio: releasing gpio IntPin \n" );
    NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hIntPin);
    
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_release_gpio: releasing gpio RstPin \n");
	NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hRstPin);

    PRINT_OUT 
    return 0;
}
#endif

static int capkey_poweron_device(struct capkey_t *g_ck,int on)
{
	int	ret = 0;
   
    PRINT_IN
	if(on)
	{
		g_ck->vdd_regulator = regulator_get(NULL,"vdd_capkey"); 
		if (IS_ERR_OR_NULL(g_ck->vdd_regulator)) 
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "capkey_poweron_device: couldn't get regulator vdd_capkey\n");
			g_ck->vdd_regulator = NULL;
			ret = -EFAULT;
			goto out;
		}
		else
		{
			regulator_enable(g_ck->vdd_regulator);
			mdelay(5);
		}
    }
	else
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_poweron_device: does not support turn off vdd\n");
	}

out:
	PRINT_OUT
	return ret;
}

static int capkey_release_gpio(int gpio_irq, int gpio_rst)
{
    PRINT_IN
    MY_INFO_PRINTK(4,"TEST_INFO_LEVEL:""capkey_release_gpio: releasing gpio irq %d & rst %d\n", gpio_irq,gpio_rst);
    gpio_free(gpio_irq);
    gpio_free(gpio_rst);
	
	
    PRINT_OUT 
    return 0;
}

static int capkey_setup_gpio(struct capkey_t *g_ck)
{
    int rc = 0;

    PRINT_IN
	
	rc = gpio_request(g_ck->gpio_irq, "ATA2538_Capkey_irq");
    if (rc)
    {
    	MY_INFO_PRINTK( 2,"ERROR_LEVEL:""capkey_setup_gpio: request gpio_irq %d failed (rc=%d)\n", g_ck->gpio_irq, rc );
		capkey_release_gpio(g_ck->gpio_irq, g_ck->gpio_rst);
		PRINT_OUT
		return rc;
    }
	rc = gpio_direction_input(g_ck->gpio_irq);
    if (rc)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""capkey_setup_gpio: set gpio_irq %d mode failed (rc=%d)\n", g_ck->gpio_irq, rc );
        capkey_release_gpio(g_ck->gpio_irq, g_ck->gpio_rst);
        PRINT_OUT
        return rc;
    }
	
	
	rc = gpio_request(g_ck->gpio_rst, "ATA2538_Capkey_rst");
    if (rc)
    {
    	MY_INFO_PRINTK( 2,"ERROR_LEVEL:""capkey_setup_gpio: request gpio_rst %d failed (rc=%d)\n", g_ck->gpio_rst, rc );
		capkey_release_gpio(g_ck->gpio_irq, g_ck->gpio_rst);
		PRINT_OUT
		return rc;
    }
	rc = gpio_direction_output(g_ck->gpio_rst, 0);
    if (rc)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""touchpad_setup_gpio: set gpio_rst %d mode failed (rc=%d)\n", g_ck->gpio_rst, rc);
        capkey_release_gpio(g_ck->gpio_irq, g_ck->gpio_rst);
        PRINT_OUT
        return rc;
    }   
    PRINT_OUT
    return rc;
}

static int capkey_config_gpio(struct capkey_t *g_ck)
{
    int rc = 0;
    
    PRINT_IN
	gpio_set_value(g_ck->gpio_rst, 0);
	MY_INFO_PRINTK( 4,"INFO_LEVEL:""touchpad_config_gpio reset get low : delay 1 ms\n");
	msleep(1);
	gpio_set_value(g_ck->gpio_rst, 1);
	MY_INFO_PRINTK( 4,"INFO_LEVEL:""config gpio RstPin get high ¡G delay 300 ms\n");
	msleep(300); 
    PRINT_OUT
    return rc;
}


int luna_capkey_callback(int up)
{
	struct i2c_client  *client = g_ck->client_4_i2c;
	int result = 0;
	uint8_t value;
	
	
	mutex_lock(&g_ck->mutex);
	if(up == 0) 
	{
		g_ck->capkey_powercut = 1; 
		if(g_ck->capkey_suspended == 0)
		{	
			if (g_ck->fvs_mode_flag == 0 ) 
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is released\n");
				input_report_key(g_ck->input_dev, KEY_HOME, 0);
				input_sync(g_ck->input_dev);
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is released\n");
				input_report_key(g_ck->input_dev, KEY_MENU, 0);
				input_sync(g_ck->input_dev);
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_BACK is released\n");
				input_report_key(g_ck->input_dev, KEY_BACK, 0);
				input_sync(g_ck->input_dev);
			}
			
			capkey_LED_power_switch(0);
			
			
			disable_irq( g_ck->irq );
			mutex_unlock(&g_ck->mutex);
			cancel_work_sync(&g_ck->capkey_work.work);
			mutex_lock(&g_ck->mutex);
			if (result) 
			{
				
				
				enable_irq(g_ck->irq);
				MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "enalbe irq %d\n",g_ck->irq );
			}
		}
	}
	else 
	{
		g_ck->capkey_powercut = 0; 
		if(g_ck->capkey_suspended == 0)
		{
			#if 0
			capkey_config_gpio(g_ck);
			#endif
			
			result = capkey_config_ATA2538(g_ck);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
				mutex_unlock(&g_ck->mutex);
				return result;
			}
			
			capkey_LED_power_switch(1);
			
			
			enable_irq(g_ck->irq);
		}
		else 
		{
			#if 0
			capkey_config_gpio(g_ck);
			#endif
			
			result = capkey_config_ATA2538(g_ck);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
				mutex_unlock(&g_ck->mutex);
				return result;
			}
			
			value = 0x01;
			result = capkey_write_i2c(client,Enter_SLEEP,&value,1);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read enter sleep 0X%Xh = 0x%X\n",Enter_SLEEP, result );
				PRINT_OUT
				return result;
			}
		}
	}
	
	mutex_unlock(&g_ck->mutex);
	return result;
}
EXPORT_SYMBOL(luna_capkey_callback);

static void capkey_irqWorkHandler( struct work_struct *work )
{	
    struct i2c_client  *client = g_ck->client_4_i2c;
	uint8_t value;
	
    
    
    PRINT_IN         
#if 0
    if(!rt_task(current))
    {
 		if(sched_setscheduler_nocheck(current, SCHED_FIFO, &s)!=0)
		{
			MY_INFO_PRINTK( 1, "TEST_INFO_LEVEL¡G" "fail to set rt pri...\n" );
		}
		else
		{
			MY_INFO_PRINTK( 1, "TEST_INFO_LEVEL¡G" "set rt pri...\n" );
		}
    }
#endif
	
	mutex_lock(&g_ck->mutex);
    do
	{
		capkey_read_i2c(client, PA_TOUCH_BYTE, &value, 1);
		if (g_ck->fvs_mode_flag == 1 ) 
		{
			if(value & 0x08)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F2 is pressed\n");
				input_report_key(g_ck->input_dev, KEY_F2, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F2 is released\n");
				input_report_key(g_ck->input_dev, KEY_F2, 0);
				input_sync(g_ck->input_dev);
			}
			if(value & 0x10)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F5 is pressed\n");
				input_report_key(g_ck->input_dev, KEY_F5, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F5 is released\n");
				input_report_key(g_ck->input_dev, KEY_F5, 0);
				input_sync(g_ck->input_dev);
			}
			if(value & 0x20)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F6 is pressed\n");
				input_report_key(g_ck->input_dev, KEY_F6, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F6 is released\n");
				input_report_key(g_ck->input_dev, KEY_F6, 0);
				input_sync(g_ck->input_dev);
			}
		}
		else 
		{
			if(value & 0x08)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is pressed\n");
				input_report_key(g_ck->input_dev, KEY_HOME, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is released\n");
				input_report_key(g_ck->input_dev, KEY_HOME, 0);
				input_sync(g_ck->input_dev);
			}
			if(value & 0x10)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is pressed\n");
				input_report_key(g_ck->input_dev, KEY_MENU, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is released\n");
				input_report_key(g_ck->input_dev, KEY_MENU, 0);
				input_sync(g_ck->input_dev);
			}
			if(value & 0x20)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" HOME_BACK is pressed\n");
				input_report_key(g_ck->input_dev, KEY_BACK, 1);
				input_sync(g_ck->input_dev);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_BACK is released\n");
				input_report_key(g_ck->input_dev, KEY_BACK, 0);
				input_sync(g_ck->input_dev);
			}
		}
		
		
		
		
    }while(gpio_get_value(g_ck->gpio_irq) == 1);
	
	
	MY_INFO_PRINTK( 4,"TEST_INFO_LEVEL:" "before enable_irq()\n");
	enable_irq( g_ck->irq );
	mutex_unlock(&g_ck->mutex);
	PRINT_OUT
	return;	
}	

static irqreturn_t capkey_irqHandler(int irq, void *dev_id)
{ 
	struct capkey_t *g_ck = dev_id;
	PRINT_IN
	disable_irq_nosync( irq );
    queue_delayed_work(g_ck->capkey_wqueue, &g_ck->capkey_work, 0);
    PRINT_OUT
	return IRQ_HANDLED;
}

int  capkey_LED_power_switch(int on)
{     	
	int result = 0;
	struct i2c_client *client = g_ck->client_4_i2c;
	uint8_t value;
	
	PRINT_IN;
	if (on) 
	{
		value = 0x0f;
		result = capkey_write_i2c(client,ADDR_GPIO_CONFIGURATION,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
			PRINT_OUT
			return result;
		}
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
		printk( "turn on capkey LED light\n" );
	}
	else 
	{
		value = 0x00;
		result = capkey_write_i2c(client,ADDR_GPIO_CONFIGURATION,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
			PRINT_OUT
			return result;
		}
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
		printk( "turn off capkey LED light\n" );
	} 
	PRINT_OUT;
    return result;    
}

static long ck_misc_ioctl( struct file *fp,
                           unsigned int cmd,
                           unsigned long arg )
{	
	
	int  result = 0;
	uint8_t value[9];
	uint8_t data = 0x7F;
	uint8_t addr;
	int i;
	struct capkey_alpha_t alpha;
	struct i2c_client *client = g_ck->client_4_i2c;
	uint8_t *pData = NULL;
    uint    length = 0;
	
	PRINT_IN
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "cmd number=%d\n", _IOC_NR(cmd) );
	switch(cmd)
    {
		case ATA_CAPKEY_IOCTL_GET_SENSOR_VALUE:
			
			result = capkey_read_i2c(client, PA3_STRENGTH , &value[0], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg STRENGTH 0x%x and value failed 0x%x,\n", PA3_STRENGTH,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			
			result = capkey_read_i2c(client, PA3_IMPEDANCE, &value[3], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg IMPEDANCE 0x%x and value failed 0x%x,\n", PA3_IMPEDANCE,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			
			result = capkey_read_i2c(client, PA3_REFERENCE_IMPEDANCE, &value[6], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg REFERENCE IMPEDANCE 0x%x and value failed 0x%x,\n", PA3_REFERENCE_IMPEDANCE,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			for ( i = 3 ; i < 9 ; i++ )
			{
				if ( i < 6)
				{
					value[i] = data - value[i];
					printk("read IMPEDANCE = %d\n",value[i] );
				}
				else
				{
					value[i] = data - value[i];
					printk("read Cal IMPEDANCE = %d\n",value[i] );
				}
			}
			
			result = copy_to_user( (void *)arg,&value[0],9 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy GET_SENSOR_VALUE to user failed!\n" );
                result = -EFAULT;
				return result;
			}
            break;
		 case ATA_CAPKEY_IOCTL_SET_ALPHA:
            addr = ADD_PA0_ALPHA+3;
            pData = (void *)&alpha;
            length = sizeof(alpha);
            if( copy_from_user( (void *)pData,
                                (void *)arg,
                                length) )
            {
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy SET_ALPHA from user failed!\n" );
                result = -EFAULT;
				return result;
            }
			else
			{
                value[0] = alpha.PA3_value;
				value[1] = alpha.PA4_value;
				value[2] = alpha.PA5_value;
				value[3] = alpha.reference_delay;
				result = capkey_write_i2c( client, addr, &value[0], 3 );
				if (result)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to write SET_ALPHA!\n" );
					result = -EFAULT;
					return result;
				}
				addr = ADD_PA0_ALPHA+8;
				result = capkey_write_i2c( client, addr, &value[3], 1 );
				if (result)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to write SET_ALPHA!\n" );
					result = -EFAULT;
					return result;
				}
            }
            break;
		case ATA_CAPKEY_IOCTL_GET_ALPHA:
			addr = ADD_PA0_ALPHA+3;
            result = capkey_read_i2c( client, addr, &value[0], 3 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to read GET_ALPHA!\n" );
                result = -EFAULT;
				return result;
			}
			addr = ADD_PA0_ALPHA+8;
            result = capkey_read_i2c( client, addr, &value[3], 1 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to read GET_ALPHA!\n" );
                result = -EFAULT;
				return result;
			}
			result = copy_to_user( (void *)arg,&value[0],4 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy GET_ALPHA to user failed!\n" );
                result = -EFAULT;
				return result;
			}
            break;
	}
	PRINT_OUT
    return result;		
}

static ssize_t ck_misc_write( struct file *fp,
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
	mutex_lock(&g_ck->mutex);
		
		if (g_ck->capkey_suspended == 0)
		{
			echostr[count-1]='\0';
			if ( strcmp(echostr,"on" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				capkey_LED_power_switch(1);
			}
			else if ( strcmp(echostr,"off" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				capkey_LED_power_switch(0);
			}
			else if ( strcmp(echostr,"enter fvs" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				g_ck->fvs_mode_flag = 1; 
			}
			else if ( strcmp(echostr,"exit fvs" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				g_ck->fvs_mode_flag = 0; 
			}
		}
		else
		{
			MY_INFO_PRINTK(4, "INFO_LEVEL:" "capkey in deep sleep mode and can't power switch LED light\n");
		}
	
	mutex_unlock(&g_ck->mutex);
	PRINT_OUT
    return count;    
}

static const struct i2c_device_id i2cATACapkey_idtable[] = {
       { CAPKEY_DRIVER_NAME, 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, i2cATACapkey_idtable);

static struct i2c_driver ata_capkey_driver = {
	.driver	 = {
		.name   = CAPKEY_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	  = capkey_probe,
#if 0
	.remove	 = ts_remove,
    .suspend = ts_suspend,
    .resume  = ts_resume,
#endif
	.id_table = i2cATACapkey_idtable,
};

static int capkey_open(struct input_dev *dev)
{
    int rc = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->open_count == 0 )
    {
        g_ck->open_count++; 
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "open count : %d\n",g_ck->open_count );      
    }	
    else
    {	
		rc = -EFAULT;
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return rc;
}

static void capkey_close(struct input_dev *dev)
{
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->open_count )
    {
        g_ck->open_count--;
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "input device still opened %d times\n",g_ck->open_count );        
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
}

static int ck_misc_release(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->misc_open_count )
    {
        g_ck->misc_open_count--;      
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return result;
}

static int ck_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->misc_open_count ==0 )
    {
        g_ck->misc_open_count++;
        MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "misc open count : %d\n",g_ck->misc_open_count );          
    }	
    else
    { 
		result = -EFAULT;
		MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "failed to open misc count : %d\n",g_ck->misc_open_count );  
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return result;
}

static struct file_operations ck_misc_fops = {
	.owner 	= THIS_MODULE,
	.open 	= ck_misc_open,
	.release = ck_misc_release,
	.write = ck_misc_write,
	
    .unlocked_ioctl = ck_misc_ioctl,
};

static struct miscdevice ck_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "ata_misc_capkey",
	.fops 	= &ck_misc_fops,
};


static void capkey_suspend(struct early_suspend *h)
{
	int result = 0;
    struct capkey_t *g_ck = container_of(h,struct capkey_t,capkey_early_suspend);
	struct i2c_client *client = g_ck->client_4_i2c;
    uint8_t value;	
	
	PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL¡G" "capkey_suspend : E\n" );
	mutex_lock(&g_ck->mutex);
	if( g_ck->capkey_suspended )
	{
		mutex_unlock(&g_ck->mutex);
		PRINT_OUT
		return;
	}
	
	g_ck->capkey_suspended = 1;
	
	if(g_ck->capkey_powercut != 1)
	{
		
		capkey_LED_power_switch(0);
		
		disable_irq( g_ck->irq );
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "disable irq %d\n",g_ck->irq );	
		result = cancel_work_sync(&g_ck->capkey_work.work);
		if (result) 
		{
			enable_irq(g_ck->irq); 
			MY_INFO_PRINTK( 4, "INFO_LEVEL:" "enalbe irq %d\n",g_ck->irq );
		}
		
		
		value = 0x01;
		result = capkey_write_i2c(client,Enter_SLEEP,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read enter sleep 0X%Xh = 0x%X\n",Enter_SLEEP, result );
			PRINT_OUT
			return;
		}
    }
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_suspend: X\n" );
	mutex_unlock(&g_ck->mutex);
    PRINT_OUT
	return;
}


static void capkey_resume(struct early_suspend *h)
{
    int result;
	
	uint8_t value;
    struct capkey_t *g_ck = container_of( h,struct capkey_t,capkey_early_suspend);
	
    PRINT_IN
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_resume: E\n" );
	mutex_lock(&g_ck->mutex);
    if( 0 == g_ck->capkey_suspended )
	{
		mutex_unlock(&g_ck->mutex);
		PRINT_OUT
		return;
	}
	
	
	if(g_ck->capkey_powercut != 1)
	{
		
		value = 0x01;
		result = capkey_write_i2c(g_ck->client_4_i2c,WAKEUP_SLEEP,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read wakeup sleep 0X%Xh = 0x%X\n",WAKEUP_SLEEP, result );
			mutex_unlock(&g_ck->mutex);
			PRINT_OUT
			return;
		}
		
		
		capkey_LED_power_switch(1);
		
		
		enable_irq( g_ck->irq );
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "enalbe irq %d\n",g_ck->irq );
	}
	g_ck->capkey_suspended = 0;
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_resume: X\n" );
    mutex_unlock(&g_ck->mutex);
	PRINT_OUT
	return;
}

static int capkey_register_input( struct input_dev **input,
                                    struct platform_device *pdev )
{
    int rc = 0;
    struct input_dev *input_dev;
    int i;
    
    PRINT_IN
    i = 0;
    input_dev = input_allocate_device();
    if ( !input_dev ) {
        rc = -ENOMEM;
        return rc;
    }
    input_dev->name = CAPKEY_DRIVER_NAME;
    input_dev->phys = "ATA2538_capkey/input0";
    input_dev->id.bustype = BUS_I2C;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = 0x0002;
    input_dev->id.version = 0x0100;
    input_dev->open = capkey_open;
    input_dev->close = capkey_close;
    
    
	input_dev->evbit[0] = BIT_MASK(EV_KEY);
    
    set_bit(KEY_MENU, input_dev->keybit);
	set_bit(KEY_HOME, input_dev->keybit);
	set_bit(KEY_BACK, input_dev->keybit);
	
	
	set_bit(KEY_F2, input_dev->keybit);
	set_bit(KEY_F5, input_dev->keybit);
	set_bit(KEY_F6, input_dev->keybit);
	
	rc = input_register_device( input_dev );
    if ( rc )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G""failed to register input device\\n");
        input_free_device( input_dev );
    }else {
        *input = input_dev;
    }
    PRINT_OUT
    return rc;
}

static int __devinit capkey_probe(struct i2c_client *client, 
				const struct i2c_device_id *id )
{
    int result = 0;
    struct ata2538_capkey_platform_data_t *pdata;
	
    PRINT_IN
#if 0
	if (client == NULL) {
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:""capkey_probe client == NULL\n");
		return -EINVAL;
	}
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey driver\n");
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\"%s\"\n", client->name);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\taddr:0x%x\n", client->addr);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\tirq:\t%d\n", client->irq);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\tflags:\t0x%04x\n", client->flags);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\tadapter:\"%s\"\n", client->adapter->name);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "\tdevice:\t\"%s\"\n", client->dev.init_name);
#endif
  
	g_ck = kzalloc(sizeof(struct capkey_t), GFP_KERNEL);
    if(!g_ck)
    {
        result = -ENOMEM;
        PRINT_OUT
        return result;
    }
	
	pdata = client->dev.platform_data;
	g_ck->gpio_irq = pdata->gpioirq;
    g_ck->gpio_rst = pdata->gpiorst;
	g_ck->irq = TEGRA_GPIO_TO_IRQ(g_ck->gpio_irq);
	g_ck->client_4_i2c = client;
    mutex_init(&g_ck->mutex);
	g_ck->i2c_addr = 0x68;
	
	
    result = capkey_poweron_device(g_ck,1);
    if(result)
	{
    	MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "capkey_probe: failed to power on device\n" );
		kfree(g_ck);
        PRINT_OUT
        return result;
    }
	
	
    result = capkey_setup_gpio(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to setup gpio\n" );
		
        kfree(g_ck);
        return result;
    }

	
    result = capkey_config_gpio(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config gpio\n" );
		
        kfree(g_ck);
        return result;
    }
	
	
    result = capkey_detect_ATA2538(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to detect\n" );
		
        kfree(g_ck);
        return result;
    }
	client->driver = &ata_capkey_driver;
    i2c_set_clientdata( client, g_ck );
	
	
    result = capkey_config_ATA2538(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
        kfree(g_ck);
        return result;
    }
	
	
	capkey_LED_power_switch(1);
	
	
    result = capkey_register_input( &g_ck->input_dev, NULL );
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to register input\n" );
        kfree(g_ck);
    	return result;
    }
	input_set_drvdata(g_ck->input_dev, g_ck);

	
	result = misc_register( &ck_misc_device );
    if( result )
    {
       	MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G" "failed register misc driver\n" );
        result = -EFAULT;
		kfree(g_ck);
    	PRINT_OUT
		return result;       
    }

	
	INIT_DELAYED_WORK( &g_ck->capkey_work, capkey_irqWorkHandler );
    g_ck->capkey_wqueue = create_singlethread_workqueue(CAPKEY_DRIVER_NAME); 
    if (!g_ck->capkey_wqueue)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to create singlethread workqueue\n" );
        result = -ESRCH; 
        
        kfree(g_ck); 
        return result;
    }
	
	
	result = request_irq( g_ck->irq, capkey_irqHandler, IRQF_TRIGGER_HIGH,"CAPKEY_IRQ", g_ck );
	if (result)
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:""irq %d requested failed\n", g_ck->irq);
		result = -EFAULT;
		kfree(g_ck);
    	PRINT_OUT
    	return result;
	}
    else
	{
        MY_INFO_PRINTK( 4, "INFO_LEVEL:""irq %d requested successfully\n", g_ck->irq);
	}
	
	
    g_ck->capkey_early_suspend.level = 150; 
    g_ck->capkey_early_suspend.suspend = capkey_suspend;
    g_ck->capkey_early_suspend.resume = capkey_resume;
    register_early_suspend(&g_ck->capkey_early_suspend);

    PRINT_OUT
    return 0;
}

extern int IsA02Version(void);
static int __init capkey_init(void)
{
    int rc = 0;

    PRINT_IN
    MY_INFO_PRINTK( 1,"INFO_LEVEL¡G""system_rev=0x%x\n",system_rev);
	A02Version = IsA02Version();
	printk("system_rev=0x%x A02=%d\n",system_rev,A02Version);
	ata_capkey_driver.driver.name = CAPKEY_DRIVER_NAME;
	rc = i2c_add_driver(&ata_capkey_driver);
	PRINT_OUT
    return rc;
}
module_init(capkey_init);

static void __exit capkey_exit(void)
{
    PRINT_IN
    i2c_del_driver(&ata_capkey_driver);
	input_unregister_device( g_ck->input_dev );
	input_free_device( g_ck->input_dev );
    destroy_workqueue( g_ck->capkey_wqueue );
    capkey_release_gpio( g_ck->gpio_irq, g_ck->gpio_rst );
    kfree(g_ck);
    PRINT_OUT
}
module_exit(capkey_exit);

MODULE_DESCRIPTION("ATA2538 capkey driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ata2538_capkey");
