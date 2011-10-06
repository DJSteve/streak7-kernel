
#include <linux/wakelock.h>
#include <mach/suspend.h>


static int suspend_check = 2500;
struct wake_lock wwan_wake_lock;
struct wake_lock wwan_discon_lock;

#define WWAN_LOCK(lock)  \
{ \
	if (!wake_lock_active(&lock)) \
	{ \
		wake_lock(&lock); \
		if (debug) printk(KERN_INFO "%s: %s locked!\n", __func__, lock.name); \
	} \
} 

#define WWAN_LOCK_TIMEOUT(lock, sec)  \
{ \
	if (!wake_lock_active(&lock)) \
	{ \
		wake_lock_timeout(&lock, sec * HZ); \
		printk(KERN_INFO "%s: %s locked %ds!\n", __func__, lock.name, sec); \
	} \
} 

#define WWAN_UNLOCK(lock) \
{ \
	if (wake_lock_active(&lock)) \
	{ \
		wake_unlock(&lock); \
		if (debug) printk(KERN_INFO "%s: %s unlocked!\n", __func__, lock.name); \
	} \
}


static struct delayed_work *hw_suspend_wq = NULL; 
static struct usb_device   *hw_udev = NULL;

static void hw_wwan_suspend_check_work(struct work_struct *work);


void hw_wwan_suspend_check(struct usb_device *udev)
{
	
	{
		
		usb_mark_last_busy(udev);
	}
	

	
	if (hw_suspend_wq)
	{
		WWAN_LOCK(wwan_wake_lock);
		schedule_delayed_work(hw_suspend_wq, (suspend_check * HZ)/1000);
	}
}

void hw_wwan_disconnect(struct usb_serial *serial)
{
	WWAN_LOCK_TIMEOUT(wwan_discon_lock, 60);	

	
	if(hw_udev && hw_udev == serial->dev) {
		if (hw_suspend_wq) {
			cancel_delayed_work_sync(hw_suspend_wq); 
			WWAN_UNLOCK(wwan_wake_lock);
		}
		hw_udev = NULL;
	}
	
}

void hw_wwan_release(void)
{
	
	if (hw_suspend_wq) {
		cancel_delayed_work_sync(hw_suspend_wq); 
		WWAN_UNLOCK(wwan_wake_lock);	
		kfree(hw_suspend_wq);  
		hw_suspend_wq = NULL;
	}
	
}

void hw_wwan_init_termios(struct tty_struct *tty)
{
	dbg("%s", __func__);
	*(tty->termios) = tty_std_termios;
	tty->termios->c_cflag     &= ~(CSIZE | CSTOPB | PARENB | CLOCAL);
	tty->termios->c_cflag     |= CS8 | CREAD | HUPCL;
	tty->termios->c_iflag      = IGNBRK | IGNPAR;
	
	tty->termios->c_lflag      = 0;
	tty->termios->c_cc[VMIN]   = 1;
	tty->termios->c_cc[VTIME]  = 0;
	tty->termios->c_cflag ^= (CLOCAL | HUPCL);
}

#ifdef CONFIG_PM
void hw_wwan_suspend(struct usb_serial *serial)
{
	
	if (hw_udev && hw_udev == serial->dev && hw_suspend_wq) {
		cancel_delayed_work(hw_suspend_wq);
	}
	
}

void hw_wwan_resume(struct usb_serial *serial)
{
	
	if (hw_udev && hw_udev == serial->dev && hw_suspend_wq) {
		
		if (tegra_get_wake_status() & 0x8000) {
			WWAN_LOCK(wwan_wake_lock);
			schedule_delayed_work(hw_suspend_wq, (suspend_check * HZ)/1000);
		}
	}
	
}

int hw_wwan_reset_resume(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	dev_err(&serial->dev->dev, "%s\n", __func__);
	return usb_serial_resume(intf);
}
#endif


int __init hw_wwan_init(void)
{
	wake_lock_init(&wwan_wake_lock, WAKE_LOCK_SUSPEND, "wwan_wake_lock");
	wake_lock_init(&wwan_discon_lock, WAKE_LOCK_SUSPEND, "wwan_discon_lock");
	return 0;
}

void __exit hw_wwan_exit(void)
{
	wake_lock_destroy(&wwan_wake_lock);
	wake_lock_destroy(&wwan_discon_lock);
}

int hw_wwan_probe(struct usb_serial *serial)
{
	device_set_wakeup_enable(&serial->dev->dev, 1);

	
	
		if (!hw_udev) {
			hw_udev = interface_to_usbdev(serial->interface);
			
			
		}
		if (hw_udev == serial->dev && 
			!hw_suspend_wq) {
			hw_suspend_wq = kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
			if (!hw_suspend_wq) {
				 printk(KERN_ERR "fxz-%s:Alloc memroy failed for hw_suspend_wq!\n", __func__);
				 return -ENOMEM;
			}
			INIT_DELAYED_WORK(hw_suspend_wq, hw_wwan_suspend_check_work);
			
			WWAN_LOCK(wwan_wake_lock);	
			schedule_delayed_work(hw_suspend_wq, 10 * HZ);
		} else {
			if (hw_udev == serial->dev) {
				
				WWAN_LOCK(wwan_wake_lock);	
				schedule_delayed_work(hw_suspend_wq, 10 * HZ);
			}
		}
	
	
	return 0;
}


void hw_wwan_suspend_check_work(struct work_struct *work)
{
	
	
	
	unsigned long current_time, suspend_time, check_time;
	
	   
	if (NULL == hw_udev) {
		WWAN_UNLOCK(wwan_wake_lock);	
		return;
	}
	
	current_time = jiffies;
	check_time = (suspend_check * HZ)/1000;
	suspend_time = hw_udev->last_busy + check_time;
	
	if (current_time > suspend_time) {
#if 0
		if (hw_udev->actconfig) {
			for (; i < hw_udev->actconfig->desc.bNumInterfaces; i++) {
				intf = hw_udev->actconfig->interface[i];
				atomic_set(&intf->pm_usage_cnt,  0);
			}
			status = usb_autopm_set_interface(intf);
		}
		printk(KERN_ERR"fxz-%s:option_udev->last_busy = %ld, current_time=%ld, status[%d]\n", __func__,hw_udev->last_busy, current_time, status);
		if(status == 0) {
			cancel_delayed_work(hw_suspend_wq);
		} else {
			schedule_delayed_work(hw_suspend_wq, 1 * HZ);
		}
#endif
		WWAN_UNLOCK(wwan_wake_lock);	
	} else {
		if (likely((suspend_time - current_time) < check_time))
			check_time = suspend_time - current_time;

		schedule_delayed_work(hw_suspend_wq, check_time);
	}
}


module_param(suspend_check, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(suspend_check, "Next suspend check delay (ms)");

