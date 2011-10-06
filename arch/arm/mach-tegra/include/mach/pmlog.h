#ifndef __PMLOG_H_
#define	__PMLOG_H_

#include <linux/list.h>
#include <linux/ktime.h>

struct pmlog_device {
	struct list_head pmlog_list;
	struct device *dev;
	struct timespec start_time;
	struct timespec run_time;
	unsigned long count;
};

#ifdef CONFIG_POWER_MANAGEMENT_LOG
void pmlog_update_suspend(unsigned long);
void pmlog_update_wakeup(unsigned long);
void pmlog_update_status(unsigned long);
int pmlog_device_on(struct pmlog_device *node);
int pmlog_device_off(struct pmlog_device *node);
struct pmlog_device* pmlog_register_device(struct device *dev);
void pmlog_unregister_device(struct pmlog_device *dev);
#else
static inline void pmlog_update_suspend(unsigned long){ }
static inline void pmlog_update_status(unsigned long){ }
static inline int pmlog_device_on(struct pmlog_device *dev) { return 0; }
static inline int pmlog_device_off(struct pmlog_device *dev) { return 0; }
static inline struct pmlog_device* pmlog_register_device(struct device *dev) { return NULL; }
static inline void pmlog_unregister_device(struct pmlog_device *dev) { }
#endif

#endif	
