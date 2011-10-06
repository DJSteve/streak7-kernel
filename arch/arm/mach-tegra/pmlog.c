#include <linux/module.h>
#include <linux/device.h>
#include <linux/android_alarm.h>
#include <linux/wakelock.h>
#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/syscalls.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <mach/pmlog.h>
#include <mach/luna_battery.h>

#define PMLOG_DEFAULT_TIMEOUT_SECONDS (1*60*60)
long pmlog_timeout_s = PMLOG_DEFAULT_TIMEOUT_SECONDS;

static int pmlog_set_pmlog_timeout(const char *val, struct kernel_param *kp);
static int pmlog_get_pmlog_timeout(char *buffer, struct kernel_param *kp);
module_param_call(pmlog_timeout_s, pmlog_set_pmlog_timeout, pmlog_get_pmlog_timeout,
					&pmlog_timeout_s, 0664);

struct wake_lock wake_lock_pmlog;
struct workqueue_struct *pmlog_wqueue;
struct work_struct pmlog_work;
struct work_struct pmlog_shutdown_work;
static struct list_head pmlog_list_head;
static DEFINE_MUTEX(pmlog_mutex);
static struct timespec system_time;
static struct timespec time_interval = { PMLOG_DEFAULT_TIMEOUT_SECONDS, 0 }; 
static struct timespec alarm_time = { 0, 0 };
static struct timespec time_24hr = { 24*60*60, 0 }; 
static struct timespec expire_time = { 0, 0 };
static struct alarm pmlog_alarm;
static char *buffer;
static char shutdown_cmd[80];
#if 0
static unsigned long long total_deepsleep_time = 0;
static unsigned long long deepsleep_time = 0;
static unsigned long wake_count[32];
static unsigned long wake_time_arr[32][2];
#else
extern u64 total_deepsleep_time;
extern u64 deepsleep_time;
extern unsigned long wake_count[32];
extern unsigned long wake_time_arr[32][2];
#endif
static unsigned int wake_source;

#define PMLOG_SAVED_PATH "/data/misc/pmlog"
#define	PMLOG_PATH "/data/misc/pmlog"
#define PMLOG_NAME "pmlog.log"
#define PMLOG_FILE PMLOG_PATH "/" PMLOG_NAME

static int pmlog_set_pmlog_timeout(const char *val, struct kernel_param *kp)
{
	int ret = 0;
	unsigned long tmp;

	ret = strict_strtoul(val, 10, &tmp);
	if (ret)
    {
        printk("%s => goout\n", __func__);
		goto out;
    }

	pmlog_timeout_s = tmp;
	time_interval.tv_sec = pmlog_timeout_s;

	wake_lock(&wake_lock_pmlog); 
	queue_work(pmlog_wqueue, &pmlog_work);

out:
	return ret;
}

static int pmlog_get_pmlog_timeout(char *buffer, struct kernel_param *kp)
{
	int ret;

	ret = sprintf(buffer, "%d", pmlog_timeout_s);

	return ret;
}


void pmlog_update_wakeup(unsigned long wake_time)
{
	printk(KERN_ERR "%s: Wakeup %lu Sec, at wake-source %d.\n", __func__, wake_time, wake_source);

    if (wake_source > 31)
    {
        printk("%s, invalid wake source : %d\n", __func__, wake_source);
        return;
    }
	wake_time_arr[wake_source][0] += wake_time;
    if (wake_time_arr[wake_source][1] < wake_time)
    {
	    wake_time_arr[wake_source][1] = wake_time;
    }
}

void pmlog_update_suspend(unsigned long suspended_time)
{
	printk(KERN_ERR "%s: Suspend %lu Sec.\n", __func__, suspended_time);

	deepsleep_time += suspended_time;
	total_deepsleep_time += suspended_time;
}

void pmlog_update_status(unsigned long wake_status)
{
	int i = 0;
	while (wake_status) {
		if (wake_status & 0x1) {
			wake_count[i]++;
            wake_source = i;
		}
		wake_status>>=1;
		i++;
	}	
}

int pmlog_device_on(struct pmlog_device *dev)
{
	if (!dev) {
		printk(KERN_ERR "%s: Error! Null Node.\n", __func__);
		return -EINVAL;
	}
	mutex_lock(&pmlog_mutex);
	if (dev->count++ == 0) {
		dev->start_time = ktime_to_timespec(alarm_get_elapsed_realtime());
		printk(KERN_INFO "%s: start time: %lld\n", __func__, timespec_to_ns(&dev->start_time));
	}
	mutex_unlock(&pmlog_mutex);
	return 0;
}
EXPORT_SYMBOL(pmlog_device_on);

int pmlog_device_off(struct pmlog_device *dev)
{
	struct timespec end_time;
	if (!dev) {
		printk(KERN_ERR "%s: Error! Null Node.\n", __func__);
		return -EINVAL;
	}
	mutex_lock(&pmlog_mutex);
	if (dev->count) {
		end_time = ktime_to_timespec(alarm_get_elapsed_realtime());
		dev->run_time = timespec_add_safe(dev->run_time, timespec_sub(end_time, dev->start_time));
		dev->count = 0;
	}
	mutex_unlock(&pmlog_mutex);
	return 0;	
}
EXPORT_SYMBOL(pmlog_device_off);

static void pmlog_force_update(void)
{
	struct pmlog_device *dev;
	struct list_head *dev_list;
	struct timespec end_time;

	mutex_lock(&pmlog_mutex);

	end_time = ktime_to_timespec(alarm_get_elapsed_realtime());

	list_for_each(dev_list, &pmlog_list_head) {
		dev = container_of(dev_list, struct pmlog_device, pmlog_list);
		if (dev->count) {
			dev->run_time = timespec_add_safe(dev->run_time, timespec_sub(end_time, dev->start_time));
		}
	}

	mutex_unlock(&pmlog_mutex);
}

struct pmlog_device* pmlog_register_device(struct device *dev)
{
	struct pmlog_device *new;
	struct list_head *dev_list;

	if (!dev) {
		printk(KERN_ERR "%s: Error! Null device\n", __func__);
		return NULL;
	}

	list_for_each(dev_list, &pmlog_list_head) {
		new = container_of(dev_list, struct pmlog_device, pmlog_list);
		if (new->dev == dev) return new;
	}

	new = kzalloc(sizeof(struct pmlog_device), GFP_KERNEL);
	if (!new) {
		printk(KERN_ERR "%s: not enough memory?\n", __func__);
		return NULL;
	}

	new->dev = dev;
	INIT_LIST_HEAD(&new->pmlog_list);

	mutex_lock(&pmlog_mutex);
	list_add(&new->pmlog_list, &pmlog_list_head);
	mutex_unlock(&pmlog_mutex);

	printk(KERN_ERR "%s: register device %s\n", __func__, dev_name(dev));

	return new;
}
EXPORT_SYMBOL(pmlog_register_device);

void pmlog_unregister_device(struct pmlog_device *dev )
{
	mutex_lock(&pmlog_mutex);
	list_del(&dev->pmlog_list);
	mutex_unlock(&pmlog_mutex);
	kfree(dev);	
}
EXPORT_SYMBOL(pmlog_unregister_device);

static int pmlog_file_exist(char *path)
{
	struct file *filp = NULL;
	filp = filp_open(path, O_RDWR, S_IRWXU);
	if (!IS_ERR(filp)) {
		filp_close(filp, NULL);
		return 1;
	}
	return 0;
}

static void pmlog_move_oldfile(void)
{
	char old_path[512];
	char new_path[512];
	mm_segment_t oldfs;
	int i;

	oldfs = get_fs();
	set_fs(get_ds());

	for (i=10; i>=0; i--) {	
		snprintf(old_path, 512, "%s/%s.%d", PMLOG_SAVED_PATH, PMLOG_NAME, i);
		snprintf(new_path, 512, "%s/%s.%d", PMLOG_SAVED_PATH, PMLOG_NAME, i+1);
		if (pmlog_file_exist(old_path)) {
			if (i==10) sys_unlink(old_path);
			else sys_rename(old_path, new_path);
		}
	}
	snprintf(old_path, 512, "%s/%s", PMLOG_PATH, PMLOG_NAME);
	snprintf(new_path, 512, "%s/%s.0", PMLOG_SAVED_PATH, PMLOG_NAME);
	if (pmlog_file_exist(old_path)) sys_rename(old_path, new_path);

	set_fs(oldfs);
}

typedef enum {
	ALIGN_LEFT = 0,
	ALIGN_CENTER,
	ALIGN_RIGHT,
} ALIGNMENT;

static int format_string(char *buffer, size_t length, 
		int column_size, ALIGNMENT align, 
		bool newline, const char *fmt, ...)
{
	char string_buffer[80];
	va_list args;
	int size = 0;
	int shift = 0;
	int min_length = (newline) ? (column_size+2) : (column_size+1);
	
	if (column_size >= 80) return 0;
	if (min_length >= length) return 0;

	va_start(args, fmt);
	size = vsnprintf(string_buffer, 80, fmt, args);
	va_end(args);

	memset(buffer, ' ', column_size);
	if (newline) buffer[column_size] = '\n';
	buffer[min_length-1] = 0;

	switch(align) {
		case ALIGN_CENTER: 
			shift = (column_size - size)/2;
			break;
		case ALIGN_RIGHT:
			shift = column_size - size;
			break;
		case ALIGN_LEFT:
		default:
			shift = 0;
			break;
	}

	memcpy((buffer + shift), string_buffer, size);
	return min_length-1;				
}

#define	FORMAT_COLUMN_R(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_RIGHT, false, format, ## arg);

#define FORMAT_COLUMN_RN(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_RIGHT, true, format, ## arg);

#define FORMAT_COLUMN_C(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_CENTER, false, format, ## arg);

#define	FORMAT_COLUMN_L(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_LEFT, false, format, ## arg);

#define FORMAT_COLUMN_N(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_RIGHT, true, format, ## arg);

#define	FORMAT_COLUMN_CN(buffer, length, format, arg...) \
	format_string(buffer, length, 20, ALIGN_CENTER, true, format, ## arg);

#define	FORMAT_COLUMN_SR(buffer, length, size, format, arg...) \
	format_string(buffer, length, size, ALIGN_RIGHT, false, format, ## arg);

#define FORMAT_COLUMN_SRN(buffer, length, size, format, arg...) \
	format_string(buffer, length, size, ALIGN_RIGHT, true, format, ## arg);

#define FORMAT_COLUMN_SC(buffer, length, size, format, arg...) \
	format_string(buffer, length, size, ALIGN_CENTER, false, format, ## arg);

#define	FORMAT_COLUMN_SCN(buffer, length, size, format, arg...) \
	format_string(buffer, length, size, ALIGN_CENTER, true, format, ## arg);

static void pmlog_flush_to_file(void)
{
	mm_segment_t oldfs;
	struct file *filp = NULL;
	struct pmlog_device *dev;
	struct list_head *dev_list;
	struct timespec now;
	struct rtc_time tm;
	unsigned long long offset = 0;
	int size = 0;
	struct luna_bat_info_data data;
	int i;
	u64 x = 0, y = 0;

	printk(KERN_INFO "Updating power management log.\n");

	oldfs = get_fs();
	set_fs(get_ds());

	filp = filp_open(PMLOG_FILE, O_RDWR|O_APPEND|O_CREAT, S_IRWXU);
	if (IS_ERR(filp)) {
		printk(KERN_ERR "%s: Can't open %s. %ld\n", __func__, 
			PMLOG_FILE, PTR_ERR(filp));
		set_fs(oldfs);
		return;
	}

	getnstimeofday(&now);
	rtc_time_to_tm(now.tv_sec, &tm);
	size = FORMAT_COLUMN_R(buffer, PAGE_SIZE, "Current Time: ");
	size += snprintf(buffer+size, PAGE_SIZE-size,
		"%d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, now.tv_nsec);
	vfs_write(filp, buffer, size, &offset);

	now = ktime_to_timespec(alarm_get_elapsed_realtime());
	size = FORMAT_COLUMN_R(buffer, PAGE_SIZE, "Up Time: ");
	size += snprintf(buffer+size, PAGE_SIZE-size,"%ld.%ld Sec.\n", 
		now.tv_sec, now.tv_nsec/NSEC_PER_MSEC);
	vfs_write(filp, buffer, size, &offset);

	x = deepsleep_time;
	y = do_div(x, 1000);

	size = FORMAT_COLUMN_R(buffer, PAGE_SIZE, "Deep Sleep Time: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "%llu.%llu Sec.\n", x, y);

	x = total_deepsleep_time;
	y = do_div(x, 1000);

	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Total Sleep Time: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "%llu.%llu Sec.\n", x, y);
	vfs_write(filp, buffer, size, &offset);

	deepsleep_time = 0;

	size = FORMAT_COLUMN_R(buffer, PAGE_SIZE, "Wakeup counter:");
	size += snprintf(buffer+size, PAGE_SIZE-size, "totaltime : longest time\n");

	for (i=0; i<32; i++)
	{
		if (wake_count[i] == 0)
		{
			continue;
		}
		size += snprintf(buffer+size, PAGE_SIZE-size, "=%2lu=:", i);
		size += snprintf(buffer+size, PAGE_SIZE-size, "%13lu :%5lu.%lu : %5lu.%lu\n", wake_count[i],
				wake_time_arr[i][0] / 1000, wake_time_arr[i][0] % 1000,
				wake_time_arr[i][1] / 1000, wake_time_arr[i][1] % 1000);
	}
	vfs_write(filp, buffer, size, &offset);
	memset(wake_count, 0, sizeof(wake_count));
	memset(wake_time_arr, 0, sizeof(wake_time_arr));

	luna_bat_get_info(&data);
	size = FORMAT_COLUMN_R(buffer, PAGE_SIZE, "Battery Info: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "\n");
	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Status: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "0x%x\n", data.bat_status);
	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Health: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "0x%x\n", data.bat_health);
	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Capacity: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "%d %%\n", data.bat_capacity);
	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Voltage: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "%d mV\n", data.bat_vol);
	size += FORMAT_COLUMN_R(buffer+size, PAGE_SIZE-size, "Temperature: ");
	size += snprintf(buffer+size, PAGE_SIZE-size, "%d\n", data.bat_temp);
	vfs_write(filp, buffer, size, &offset);

	size = snprintf(buffer, PAGE_SIZE, "\n");
	size += FORMAT_COLUMN_C(buffer+size, PAGE_SIZE-size, "Device Name");
	size += FORMAT_COLUMN_C(buffer+size, PAGE_SIZE-size, "Run Time");
	size += FORMAT_COLUMN_CN(buffer+size, PAGE_SIZE-size, "Last Start");
	size += snprintf(buffer+size, PAGE_SIZE-size, "===============================================================\n");
	vfs_write(filp, buffer, size, &offset);
	
	mutex_lock(&pmlog_mutex);

	list_for_each(dev_list, &pmlog_list_head) {
		dev = container_of(dev_list, struct pmlog_device, pmlog_list);
		size = FORMAT_COLUMN_C(buffer, PAGE_SIZE, "%s", dev_name(dev->dev));
		size += FORMAT_COLUMN_C(buffer+size, PAGE_SIZE-size, "%ld.%ld", 
			dev->run_time.tv_sec, dev->run_time.tv_nsec/NSEC_PER_MSEC);
		size += FORMAT_COLUMN_CN(buffer+size, PAGE_SIZE-size, "%ld.%ld",
			dev->start_time.tv_sec, dev->start_time.tv_nsec/NSEC_PER_MSEC);
		vfs_write(filp, buffer, size, &offset);
	}

	size = snprintf(buffer, PAGE_SIZE, "\n");
	vfs_write(filp, buffer, size, &offset);

	mutex_unlock(&pmlog_mutex);

	sys_sync();
	filp_close(filp, NULL);
	set_fs(oldfs);
}

static void pmlog_shutdown(void)
{
	mm_segment_t oldfs;
	struct file *filp = NULL;
	struct timespec now;
	unsigned long long offset = 0;
	int size = 0;

	oldfs = get_fs();
	set_fs(get_ds());

	filp = filp_open(PMLOG_FILE, O_RDWR|O_APPEND|O_CREAT, S_IRWXU);
	if (IS_ERR(filp)) {
		printk(KERN_ERR "%s: Can't open %s. %ld\n", __func__, PMLOG_FILE, PTR_ERR(filp));
		set_fs(oldfs);
		return;
	}

	now = ktime_to_timespec(alarm_get_elapsed_realtime());
	
	size = snprintf(buffer, PAGE_SIZE, "Shutdown Time: %ld.%ld sec after boot.\n", now.tv_sec, now.tv_nsec/NSEC_PER_MSEC); 
	vfs_write(filp, buffer, size, &offset);

	size = snprintf(buffer, PAGE_SIZE, "Command: %s\n", shutdown_cmd);
	vfs_write(filp, buffer, size, &offset);

#if 0
	size = save_stack(buffer, PAGE_SIZE, current, NULL);
	vfs_write(filp, buffer, size, &offset);
#endif

	sys_sync();
	filp_close(filp, NULL);
	set_fs(oldfs);


}

static void pmlog_work_func(struct work_struct *work)
{
	int expired = 1;
	int new_round = 1;

	system_time = ktime_to_timespec(alarm_get_elapsed_realtime());
	if (timespec_compare(&alarm_time, &system_time) > 0) expired = 0;
	if (timespec_compare(&expire_time, &system_time) > 0) new_round = 0;
	alarm_time = timespec_add_safe(time_interval, system_time);
	alarm_start_range(&pmlog_alarm, 
		timespec_to_ktime(alarm_time),
		timespec_to_ktime(alarm_time));
	if (expired) {
		pmlog_force_update();
		if (new_round) {
			pmlog_move_oldfile();
			expire_time = timespec_add_safe(time_24hr, system_time);
		}
		pmlog_flush_to_file();
	}
	wake_unlock(&wake_lock_pmlog);
}

static void pmlog_alarm_func(struct alarm *alarm)
{
	wake_lock(&wake_lock_pmlog); 
	queue_work(pmlog_wqueue, &pmlog_work);
}

static int __devinit pmlog_init(void)
{
	INIT_LIST_HEAD(&pmlog_list_head);
	mutex_init(&pmlog_mutex);
	return 0;
}
arch_initcall(pmlog_init);

static void pmlog_shutdown_func(struct work_struct *work)
{
	system_time = ktime_to_timespec(alarm_get_elapsed_realtime());
	if (timespec_compare(&expire_time, &system_time) < 0) {
		pmlog_move_oldfile();
	}
	pmlog_force_update();
	pmlog_flush_to_file();
	pmlog_shutdown();	
}

static int pmlog_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	char *cmd = unused;
	if (cmd)
	{
		strncpy(shutdown_cmd, cmd, 80);
		shutdown_cmd[79] = 0;
		printk(KERN_ERR "%s: Command:%s\n", __func__, cmd);
	}

	wake_lock(&wake_lock_pmlog);
	queue_work(pmlog_wqueue, &pmlog_shutdown_work);
	flush_workqueue(pmlog_wqueue);
	wake_unlock(&wake_lock_pmlog);
	return NOTIFY_DONE;
}

static struct notifier_block pmlog_notifier = {
	.notifier_call = pmlog_notify_sys,
	.priority = INT_MAX,
};

static int __devinit pmlogd_init(void)
{
	int ret = 0;

	pmlog_move_oldfile();

	pmlog_wqueue = alloc_workqueue("pmlogd", WQ_UNBOUND, 1);
	if (!pmlog_wqueue) {
		printk(KERN_ERR "%s: Create single thread real time workqueue failed.\n", __func__);
		goto fail;
	}

	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer) {
		printk(KERN_ERR "%s: Create debug buffer failed\n", __func__);
		goto err_destroy_workqueue;
	}

	ret = register_reboot_notifier(&pmlog_notifier);
	if (ret) {
		printk(KERN_ERR "%s: cannot register reboot notifier. (err=0x%x)\n", __func__, ret);
		goto err_free_page;
	}

	alarm_init(&pmlog_alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP, pmlog_alarm_func);
	wake_lock_init(&wake_lock_pmlog, WAKE_LOCK_SUSPEND, "pmlog_lock");
	INIT_WORK(&pmlog_work, pmlog_work_func);
	INIT_WORK(&pmlog_shutdown_work, pmlog_shutdown_func);
	
	system_time = ktime_to_timespec(alarm_get_elapsed_realtime());
	alarm_time = timespec_add_safe(time_interval, system_time);
	alarm_start_range(&pmlog_alarm, 
		timespec_to_ktime(alarm_time),
		timespec_to_ktime(alarm_time));

	memset(wake_count, 0 , sizeof(wake_count));

	return 0;

err_free_page:
	kfree(buffer);
err_destroy_workqueue:
	destroy_workqueue(pmlog_wqueue);
fail:
	return ret;
}
late_initcall_sync(pmlogd_init);

