/* include/linux/logger.h
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Author: Robert Love <rlove@android.com>
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

#ifndef _LINUX_LOGGER_H
#define _LINUX_LOGGER_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct logger_entry {
	__u16		len;	
	__u16		__pad;	
	__s32		pid;	
	__s32		tid;	
	__s32		sec;	
	__s32		nsec;	
	char		msg[0];	
};

#define LOGGER_LOG_RADIO	"log_radio"	
#define LOGGER_LOG_EVENTS	"log_events"	
#define LOGGER_LOG_SYSTEM	"log_system"	
#define LOGGER_LOG_MAIN		"log_main"	
#ifdef CONFIG_BG_OLD_FIQ_DEBUGGER
#define LOGGER_LOG_OOB		"log_oob"	
#endif


#define LOGGER_ENTRY_MAX_LEN		(4*1024) 
#define LOGGER_ENTRY_MAX_PAYLOAD	\
	(LOGGER_ENTRY_MAX_LEN - sizeof(struct logger_entry))

#define __LOGGERIO	0xAE

#define LOGGER_GET_LOG_BUF_SIZE		_IO(__LOGGERIO, 1) 
#define LOGGER_GET_LOG_LEN		_IO(__LOGGERIO, 2) 
#define LOGGER_GET_NEXT_ENTRY_LEN	_IO(__LOGGERIO, 3) 
#define LOGGER_FLUSH_LOG		_IO(__LOGGERIO, 4) 
#ifdef CONFIG_ROUTE_PRINTK_TO_MAINLOG
#define LOGGER_SET_GLOBAL_PRINTK2MAINLOG_LEVEL _IO(__LOGGERIO, 5) 
#endif
#ifdef __KERNEL__
enum {
    LOG_PRIORITY_UNKNOWN = 0,
    LOG_PRIORITY_DEFAULT,    
    LOG_PRIORITY_VERBOSE,
    LOG_PRIORITY_DEBUG,
    LOG_PRIORITY_INFO,
    LOG_PRIORITY_WARN,
    LOG_PRIORITY_ERROR,
    LOG_PRIORITY_FATAL,
    LOG_PRIORITY_SILENT,     
};

enum logidx {
	LOG_MAIN_IDX = 0,
	LOG_RADIO_IDX,
	LOG_INVALID_IDX,
};

int logger_write(const enum logidx index,
		const unsigned char priority,
		const char __kernel * const tag,
		const char __kernel * const fmt,
		...);

#endif 

#endif 
