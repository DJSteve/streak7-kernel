/*
 *  drivers/switch/usimplug.c
 *
 * Copyright (C) 2010 Qisda Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/delay.h>


#define USIM_ERROR   0
#define USIM_DEBUG   1
#define USIM_VERBOSE 2
#define USIM_PRINT(level, format, arg...) \
   if(usim_level >= level ) printk(KERN_INFO "usimplug: "format,  ##arg)

#define USIMPLUG_NAME "usimplug"

static uint usim_level = USIM_DEBUG;  

struct usimplug_driver_data_t
{
	struct switch_dev sdev;
	unsigned gpio;
	const char *name_on;
	const char *name_off;
	const char *state_on;
	const char *state_off;
	int irq;
	struct work_struct work;
};

static struct usimplug_driver_data_t usimplug_driver_data;

static void usimplug_switch_work(struct work_struct *work)
{
	int state;
	int temp_state;
	struct usimplug_driver_data_t *data =
		container_of(work, struct usimplug_driver_data_t, work);

	msleep(50);
	temp_state = gpio_get_value(data->gpio);
	msleep(50);
	state = gpio_get_value(data->gpio);
	USIM_PRINT(USIM_VERBOSE, "state check %d -> %d\n", temp_state, state);
	if( state != temp_state)
	{
		msleep(50);
		state = gpio_get_value(data->gpio);
	}
	USIM_PRINT(USIM_VERBOSE, "confirmed %d\n", state);

	switch_set_state(&data->sdev, state);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct usimplug_driver_data_t *switch_data =
	    (struct usimplug_driver_data_t *)dev_id;

	USIM_PRINT(USIM_DEBUG, "%s!!!\n", 
		gpio_get_value(switch_data->gpio)? "plugged in": "unplugged");

	schedule_work(&switch_data->work);
	return IRQ_HANDLED;
}

static int usimplug_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
	struct usimplug_driver_data_t *switch_data = &usimplug_driver_data;
	int ret = 0;
	int state;

	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);

	if (!pdata)
		return -EBUSY;

	memset(&usimplug_driver_data, 0, sizeof(struct usimplug_driver_data_t));

	switch_data->sdev.name = pdata->name;
	switch_data->gpio = pdata->gpio;
	switch_data->name_on = pdata->name_on;
	switch_data->name_off = pdata->name_off;
	switch_data->state_on = pdata->state_on;
	switch_data->state_off = pdata->state_off;
	switch_data->sdev.print_state = NULL;
	platform_set_drvdata(pdev, switch_data);

	ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0) 
		goto err_switch_dev_register;

	ret = gpio_request(switch_data->gpio, pdev->name);
	if (ret < 0)
		goto err_request_gpio;

	ret = gpio_direction_input(switch_data->gpio);
	if (ret < 0)
		goto err_set_gpio_input;

	INIT_WORK(&switch_data->work, usimplug_switch_work);

	switch_data->irq = gpio_to_irq(switch_data->gpio);
	if (switch_data->irq < 0) {
		ret = switch_data->irq;
		goto err_detect_irq_num_failed;
	}

	
	state = gpio_get_value(switch_data->gpio);
	USIM_PRINT(USIM_DEBUG, "default state %d\n", state);
	switch_set_state(&switch_data->sdev, state);

	ret = request_irq(switch_data->irq, gpio_irq_handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, pdev->name, switch_data);
	if (ret < 0)
		goto err_request_irq;

	USIM_PRINT(USIM_DEBUG, "%s(), ret=%d\n", __func__, ret);
	return 0;

err_request_irq:
err_detect_irq_num_failed:
err_set_gpio_input:
	gpio_free(switch_data->gpio);
err_request_gpio:
	switch_dev_unregister(&switch_data->sdev);
err_switch_dev_register:
	
	platform_set_drvdata(pdev, NULL);

	USIM_PRINT(USIM_ERROR, "%s(), ret=%d\n", __func__, ret);
	return ret;
}

static int __devexit usimplug_remove(struct platform_device *pdev)
{
	struct usimplug_driver_data_t *switch_data = platform_get_drvdata(pdev);

	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);
	cancel_work_sync(&switch_data->work);
	gpio_free(switch_data->gpio);
	switch_dev_unregister(&switch_data->sdev);
	

	return 0;
}

#ifdef CONFIG_PM
static int usimplug_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usimplug_driver_data_t *switch_data = platform_get_drvdata(pdev);

	USIM_PRINT(USIM_VERBOSE, "%s()\n", __func__);
	cancel_work_sync(&switch_data->work);
	
	return 0;
}

static int usimplug_resume(struct platform_device *pdev)
{
	struct usimplug_driver_data_t *switch_data = platform_get_drvdata(pdev);
	int state;

	state = gpio_get_value(switch_data->gpio);
	USIM_PRINT(USIM_VERBOSE, "%s(), state %d\n", __func__, state);
	switch_set_state(&switch_data->sdev, state);

	return 0;
}
#endif

static struct platform_driver usimplug_platform_driver = {
	.probe = usimplug_probe,
	.remove = __devexit_p(usimplug_remove),
	.driver		= {
		.name	= USIMPLUG_NAME,
		.owner	= THIS_MODULE,
	},
#ifdef CONFIG_PM
	.suspend = usimplug_suspend,
	.resume  = usimplug_resume,
#endif
};

static int __init usimplug_init(void)
{
	int ret = 0;

	printk("BootLog, +%s+\n", __func__);

	ret = platform_driver_register(&usimplug_platform_driver);
	if (ret)
	{
		printk("BootLog, -%s-, register failed, ret=%d\n", __func__, ret);
		return ret;
	}

	printk("BootLog, -%s-, ret=%d\n", __func__, ret);
	return ret;
}

static void __exit usimplug_exit(void)
{
	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);
	platform_driver_unregister(&usimplug_platform_driver);
}

module_init(usimplug_init);
module_exit(usimplug_exit);

module_param(usim_level, uint, 0644);
MODULE_DESCRIPTION("USIM plug debug level");

MODULE_LICENSE("GPL");

