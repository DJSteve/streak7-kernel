#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <mach/current_driving.h>
#include <mach/pinmux.h>

#define CURRENT_DRIVING_DEBUG_LOG_ENABLE 1
#define CURRENT_DRIVING_PRINTKE_ENABLE 1
#define CURRENT_DRIVING_PRINTKD_ENABLE 0
#define CURRENT_DRIVING_PRINTKW_ENABLE 1
#define CURRENT_DRIVING_PRINTKI_ENABLE 0

#define HIGHSPEED 1
#define SCHMITT 1

#if CURRENT_DRIVING_DEBUG_LOG_ENABLE
	#if CURRENT_DRIVING_PRINTKE_ENABLE
		#define CURRENT_DRIVING_PRINTKE printk
	#else
		#define CURRENT_DRIVING_PRINTKE(a...)
	#endif

	#if CURRENT_DRIVING_PRINTKD_ENABLE
		#define CURRENT_DRIVING_PRINTKD printk
	#else
		#define CURRENT_DRIVING_PRINTKD(a...)
	#endif

	#if CURRENT_DRIVING_PRINTKW_ENABLE
		#define CURRENT_DRIVING_PRINTKW printk
	#else
		#define CURRENT_DRIVING_PRINTKW(a...)
	#endif

	#if CURRENT_DRIVING_PRINTKI_ENABLE
		#define CURRENT_DRIVING_PRINTKI printk
	#else
		#define CURRENT_DRIVING_PRINTKI(a...)
	#endif
#else
	#define CURRENT_DRIVING_PRINTKE(a...)
	#define CURRENT_DRIVING_PRINTKD(a...)
	#define CURRENT_DRIVING_PRINTKW(a...)
	#define CURRENT_DRIVING_PRINTKI(a...)
#endif

#define CURRENT_DRIVING_NAME "current_driving"

#define DRIVE_CONFIG(_config, _pad, _drive)					\
	{							\
		_config.pingroup = _pad;	\
		_config.hsm = TEGRA_HSM_DISABLE;			\
		_config.schmitt = TEGRA_SCHMITT_ENABLE;		\
		_config.drive = _drive;			\
		_config.pull_down = TEGRA_PULL_31;			\
		_config.pull_up = TEGRA_PULL_31;			\
		_config.slew_rising = TEGRA_SLEW_SLOWEST;		\
		_config.slew_falling = TEGRA_SLEW_SLOWEST;		\
	}

struct current_driving_driver_data_t
{
	atomic_t	open_count;
};

static struct current_driving_driver_data_t current_driving_driver_data;

static int misc_current_driving_open(struct inode *inode_p, struct file *fp)
{
	int ret = 0;

	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s+\n", __func__);

	fp->private_data = &current_driving_driver_data;

	if (atomic_read(&(current_driving_driver_data.open_count)) == 0)
	{
	}
	atomic_inc(&(current_driving_driver_data.open_count));

	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s-ret=%d\n", __func__, ret);
	return ret;
}

static int misc_current_driving_release(struct inode *inode_p, struct file *fp)
{
	int ret = 0;
	struct current_driving_driver_data_t *driver_data = fp->private_data;

	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s+\n", __func__);

	if (driver_data == NULL)
		return -EFAULT;

	if (atomic_read(&driver_data->open_count) == 1)
	{
	}

	atomic_dec(&driver_data->open_count);
	if (0 > atomic_read(&driver_data->open_count))
	{
		CURRENT_DRIVING_PRINTKW(KERN_WARNING "[CURRENT DRIVING] %s:: open_count=%d\n", __func__, atomic_read(&driver_data->open_count));
		atomic_set(&driver_data->open_count, 0);
	}

	fp->private_data = NULL;
	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s-ret=%d\n", __func__, ret);
	return ret;
}

static long misc_current_driving_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct current_driving_driver_data_t *driver_data = fp->private_data;
	struct current_driving_ctrl_t current_driving_ctrl;
	struct tegra_drive_pingroup_config config;

	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s+\n", __func__);

	if (driver_data == NULL)
		return -EFAULT;

	if (_IOC_TYPE(cmd) != CURRENT_DRIVING_IOC_MAGIC)
	{
		CURRENT_DRIVING_PRINTKE(KERN_ERR "[CURRENT DRIVING] %s::Not CURRENT_DRIVING_IOC_MAGIC\n", __func__);
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
	{
		ret = !access_ok(VERIFY_WRITE, (void __user*)arg, _IOC_SIZE(cmd));
		if (ret)
		{
			CURRENT_DRIVING_PRINTKE(KERN_ERR "[CURRENT DRIVING] %s::access_ok check err\n", __func__);
			return -EFAULT;
		}
	}

	if (_IOC_DIR(cmd) & _IOC_WRITE)
	{
		ret = !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
		if (ret)
		{
			CURRENT_DRIVING_PRINTKE(KERN_ERR "[CURRENT DRIVING] %s::access_ok check err\n", __func__);
			return -EFAULT;
		}
	}

	switch(cmd)
	{
		case CURRENT_DRIVING_IOC_WRITE:
			CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] CURRENT_DRIVING_IOC_WRITE\n");
			if (copy_from_user(&current_driving_ctrl, (void __user*) arg, _IOC_SIZE(cmd)))
			{
				CURRENT_DRIVING_PRINTKD(KERN_ERR "[CURRENT DRIVING] %s::CURRENT_DRIVING_IOC_WRITE:copy_from_user fail-\n", __func__);
				ret = -EFAULT;
				break;
			}

			DRIVE_CONFIG(config, current_driving_ctrl.padctrl, current_driving_ctrl.ohm);
			tegra_drive_pinmux_config_table(&config, 1);
			break;

		default:
			CURRENT_DRIVING_PRINTKI(KERN_INFO "[CURRENT DRIVING] %s::deafult\n", __func__);
			break;
	}

	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s-ret=%d\n", __func__, ret);
	return ret;
}

static struct file_operations misc_current_driving_fops = {
	.owner 	= THIS_MODULE,
	.open 	= misc_current_driving_open,
	.release = misc_current_driving_release,
	.unlocked_ioctl = misc_current_driving_ioctl,
};

static struct miscdevice misc_current_driving = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= CURRENT_DRIVING_NAME,
	.fops 	= &misc_current_driving_fops,
};

static int __init current_driving_init(void)
{
	int ret = 0;

	printk("BootLog, +%s+\n", __func__);

	
	ret = misc_register(&misc_current_driving);
	if (ret)
	{
		printk("BootLog, -%s-, misc_register error, ret=%d\n", __func__, ret);
		return ret;
	}

	printk("BootLog, -%s-, ret=%d\n", __func__, ret);
	return ret;
}

static void __exit current_driving_exit(void)
{
	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s+\n", __func__);
	misc_deregister(&misc_current_driving);
	CURRENT_DRIVING_PRINTKD(KERN_DEBUG "[CURRENT DRIVING] %s-\n", __func__);
}

module_init(current_driving_init);
module_exit(current_driving_exit);

MODULE_DESCRIPTION("Current Driving");
MODULE_LICENSE("GPL");

