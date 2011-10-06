#ifndef __RESET_KEY_LOG_H
#define __RESET_KEY_LOG_H

#define MAIN_LOG_SIZE 1*1024*1024
#define RADIO_LOG_SIZE 512*1024
#define SYSTEM_LOG_SIZE 64*1024
#define OOB_LOG_SIZE 128*1024
#define BL_LOG_SIZE 64*1024
#define RESET_KEY_LOG_RESERVED_SIZE 128*1024

#define MAX_RESET_KEY_LOG_ITEM 16
#define USED_RESET_KEY_LOG_ITEM 5
#define MAX_DUP 5

enum log_type {
	RESET_KEY_LOG_MAIN_LOG = 0,
	RESET_KEY_LOG_RADIO_LOG,
	RESET_KEY_LOG_SYSTEM_LOG,
	RESET_KEY_LOG_OOB_LOG,
	RESET_KEY_LOG_BL_LOG,
	RESET_KEY_LOG_NOT_A_LOG
};

struct reset_key_log_control_info
{
	uint32_t s[MAX_DUP];
	uint32_t e[MAX_DUP];
	uint32_t updating[MAX_DUP];
	uint32_t s_b[MAX_DUP];
	uint32_t e_b[MAX_DUP];
	uint32_t full[MAX_DUP];
};

struct reset_key_log_buf {
	struct reset_key_log_control_info control_info[MAX_RESET_KEY_LOG_ITEM];
	char main_log[MAIN_LOG_SIZE];
	char radio_log[RADIO_LOG_SIZE];
	char system_log[SYSTEM_LOG_SIZE];
	char oob_log[OOB_LOG_SIZE];
	char bootloader_log[BL_LOG_SIZE];
	char reserved[RESET_KEY_LOG_RESERVED_SIZE];
};

#endif
