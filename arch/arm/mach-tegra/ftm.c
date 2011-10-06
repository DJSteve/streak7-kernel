
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/usb/android_composite.h>
#include <linux/i2c.h>

#include <linux/types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/sysdev.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/gpio.h>
#include "gpio-names.h"

#include <mach/iomap.h>
#include <mach/irqs.h>

#include "board.h"

#include <linux/delay.h>

int ftm_mode = 0;
int bridge_enabled = 0;

static struct sysdev_class ftm_sysdev_class = {
    .name = "ftm",
};

static struct sys_device ftm_sys_device = {
    .id = 0,
    .cls = &ftm_sysdev_class,
};

static ssize_t ftm_show_ftm_mode(struct sys_device *dev,
        struct sysdev_attribute *attr,
        char *buf)
{
    printk(KERN_INFO "%s: ftm_mode=%d\n", __func__, ftm_mode);
    return snprintf(buf, PAGE_SIZE, "%u\n", ftm_mode);
}

static struct sysdev_attribute ftm_info[] = {
    _SYSDEV_ATTR(ftm, 0444, ftm_show_ftm_mode, NULL),
};

static int __init bridge_mode_setup(char *options)
{
    if (!options || !*options)
        return 0;

    bridge_enabled = simple_strtoul(options, NULL, 0);
    printk(KERN_INFO "%s: bridge_enabled=%d\n", __func__, bridge_enabled);

    return 0;
}
__setup("bridgemode=", bridge_mode_setup);

static int __init ftm_mode_setup(char *options)
{
    if (!options || !*options)
        return 0;

    ftm_mode = simple_strtoul(options, NULL, 0);
    printk(KERN_INFO "%s: ftm_mode=%d\n", __func__, ftm_mode);

    return 0;
}
__setup("ftm_mode=", ftm_mode_setup);

static int __init_or_module ftm_probe(struct platform_device *pdev)
{
    return 0;
}

static int ftm_remove(struct platform_device *pdev)
{
    return 0;
}

static struct platform_driver ftm_driver =
{
    .probe = ftm_probe,
    .remove = ftm_remove,
    .driver = {
        .name = "ftm",
        .owner = THIS_MODULE,
    },
};

static int __init ftm_init(void)
{
    int ret;

    printk(KERN_INFO "BootLog, +%s\n", __func__);
    ret = platform_driver_register(&ftm_driver);
    printk(KERN_INFO "%s platform_driver_register ret: %d\n", __func__, ret);

    ret = sysdev_class_register(&ftm_sysdev_class);
    printk(KERN_INFO "%s sysdev_class_register ret: %d\n", __func__, ret);

    ret = sysdev_register(&ftm_sys_device);
    printk(KERN_INFO "%s sysdev_register ret: %d\n", __func__, ret);

    ret = sysdev_create_file(&ftm_sys_device, ftm_info); 
    printk(KERN_INFO "%s sysdev_create_file ret: %d\n", __func__, ret);

    printk(KERN_INFO "BootLog, -%s, ret=%d\n", __func__, ret);
    return ret;
}


static void __exit ftm_exit(void)
{
    printk("Poweroff, +");
    platform_driver_unregister(&ftm_driver);
}

module_init(ftm_init);
module_exit(ftm_exit);

EXPORT_SYMBOL(ftm_mode);
