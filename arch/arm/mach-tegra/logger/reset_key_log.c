

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <asm/mach/map.h>

#include <linux/reset_key_log.h>

static struct reset_key_log_buf *reset_key_log_buf = NULL;
static DEFINE_SPINLOCK(resetkeylogbuf_lock);

void init_reset_key_logger(void)
{
#if 1
	struct map_desc hidden_mem_desc;
	hidden_mem_desc.virtual = IO_HIDDEN_MEM_VIRT;
	hidden_mem_desc.pfn = __phys_to_pfn(512*1024*1024-4*1024*1024);
	hidden_mem_desc.length = 4*1024*1024;
	hidden_mem_desc.type = MT_DEVICE;

	iotable_init(&hidden_mem_desc, 1);
	
	reset_key_log_buf = (struct reset_key_log_buf *)(IO_HIDDEN_MEM_VIRT);
#endif
}

#define logger_offset(n)	((n) & (log_len - 1))

static void do_write_log(struct reset_key_log_control_info *ci, uint32_t log_len, char *log_addr, const void *buf, size_t count)
{
	size_t len;
	int i;
	
	len = min(count, log_len - ci->e[0]);
	memcpy(log_addr + ci->e[0], buf, len);

	if (count != len)
		memcpy(log_addr, buf + len, count - len);

	for(i=0;i<MAX_DUP;i++)
		ci->e[i] = logger_offset(ci->e[i] + count);	
	
	if(count != len || ci->full[0])
	{
		for(i=0;i<MAX_DUP;i++)
		{
			ci->s[i] = ci->e[i];
			ci->full[i] = 0xFFFFFFFF;
		}
	}
}

static void write_entry(char* buf, uint32_t len, struct reset_key_log_control_info *ci, uint32_t log_len, char *log_addr)
{
	unsigned long flags;
	int i;
	
	spin_lock_irqsave(&resetkeylogbuf_lock,flags);
	
	for(i=0;i<MAX_DUP;i++)
	{
		ci->s_b[i] = ci->s[i];
		ci->e_b[i] = ci->e[i];
	}

	for(i=0;i<MAX_DUP;i++)
		ci->updating[i] = 0xFFFFFFFF;
	
	do_write_log(ci, log_len, log_addr, buf, len);

	for(i=0;i<MAX_DUP;i++)
		ci->updating[i] = 0x0;
	
	spin_unlock_irqrestore(&resetkeylogbuf_lock,flags);
}

void write_entry_to_reset_key_log(char* buf, uint32_t len,enum log_type t,__s32 sec, __s32 nsec, __s32 pid, __s32 tid)
{

#if 1
	size_t t_len;
	char tbuf[50];
	struct reset_key_log_control_info* ci;
	uint32_t log_len = MAIN_LOG_SIZE;
	char * log_addr;
	
	if(!reset_key_log_buf)
		return;
	
	ci = &reset_key_log_buf->control_info[0];
	log_addr = reset_key_log_buf->main_log;
	
	switch(t)
	{
		case RESET_KEY_LOG_MAIN_LOG:
			ci = &reset_key_log_buf->control_info[0];
			log_len = MAIN_LOG_SIZE;
			log_addr = reset_key_log_buf->main_log;
		break;
		case RESET_KEY_LOG_RADIO_LOG:
			ci = &reset_key_log_buf->control_info[1];
			log_len = RADIO_LOG_SIZE;
			log_addr = reset_key_log_buf->radio_log;		
		break;
		case RESET_KEY_LOG_SYSTEM_LOG:
			ci = &reset_key_log_buf->control_info[2];
			log_len = SYSTEM_LOG_SIZE;
			log_addr = reset_key_log_buf->system_log;		
		break;
		case RESET_KEY_LOG_OOB_LOG:
			ci = &reset_key_log_buf->control_info[3];
			log_len = OOB_LOG_SIZE;
			log_addr = reset_key_log_buf->oob_log;		
		break;
		case RESET_KEY_LOG_BL_LOG:
			ci = &reset_key_log_buf->control_info[4];
			log_len = BL_LOG_SIZE;
			log_addr = reset_key_log_buf->bootloader_log;		
		break;
		default:
			return;
		break;
	}
	t_len = snprintf(tbuf, sizeof(tbuf), "[%lu.%02lu](%lu)(%lu)",(unsigned long) sec,(unsigned long)nsec / 10000000,(unsigned long)pid,(unsigned long)tid);
	write_entry(tbuf, t_len, ci, log_len, log_addr);
	if(len >= 1)
	{
		if(buf[len-1] == 0)
		{
			len = len-1;
		}
	}
	write_entry(buf, len, ci, log_len, log_addr);
	if(len >= 1)
	{
		if(buf[len-1] != 0xa)
		{
			t_len = snprintf(tbuf, sizeof(tbuf), "\n");
			write_entry(tbuf, t_len, ci, log_len, log_addr);
		}
	}
#endif
}
