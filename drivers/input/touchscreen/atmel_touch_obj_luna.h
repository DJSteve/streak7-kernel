




#ifndef _ATMEL_TOUCH_OBJ_H_
#define _ATMEL_TOUCH_OBJ_H_

static int T6_msg_handler ( uint8_t *value );
static int T9_msg_handler ( uint8_t *value );
static int T20_msg_handler ( uint8_t *value ); 
static int T22_msg_handler ( uint8_t *value ); 
static int T25_msg_handler ( uint8_t *value ); 
                                   

uint8_t maxTouchCfg_T38[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T7[] =
{
	0x1E,
	0xA,
	0x19,
};


uint8_t maxTouchCfg_T8[] =
{
	0x8,
	0x0,
	0x14,
	0xA,
	0x0,
	0x0,
	0x14,
	0x0,
	0x12,
	0xF0
};


uint8_t maxTouchCfg_T9[] =
{
	 0x3,
	 0x0,
	 0x0,
	 0x13,
	 0xB,
	 0x0,
	 0x0,
	 0x2D,
	 0x2,
	 0x4,
	 0x0,
	 0x5,
	 0x2,
	 0x0,
	 0x2,
	 0x5,
	 0x5,
	 0x5,
	 0x0,
	 0x0,
	 0x0,
	 0x0,
	 0x14,
	 0x14,
	 0x28,
	 0x23,
	 0x8A,
	 0x32,
	 0x94,
	 0x4B,
	 0x3C,
	 0x0,
};


uint8_t maxTouchCfg_T15[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T18[] =
{
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T19[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T20[] =
{
	0x7,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x14,
	0xf,
	0x4,
	0xf,
	0x1,
};


uint8_t maxTouchCfg_T22[] =
{
	0x7,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x14,
	0x0,
	0x0,
	0x0,
	0x7,
	0x12,
	0xFF,
	0xFF,
	0x0,
};


uint8_t maxTouchCfg_T23[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T24[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
    0x0,
    0x0,
    0x0,
    0x0,
};


uint8_t maxTouchCfg_T25[] =            
{                                           
	0x3,
	0x0,
	0xBC,
	0x34,
	0x4C,
	0x1D,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};                                          


uint8_t maxTouchCfg_T27[] =
{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


uint8_t maxTouchCfg_T28[] =
{
	0x0,
	0x0,
	0x3,
	0x4,
	0x8,
	0xf,
};

struct obj_t maxTouchCfg_T5_obj =
{
	.size = 0,
	.obj_addr = 0,
	.value_array = NULL,
	.msg_handler = NULL,
	.config_crc = 0,
};

struct obj_t maxTouchCfg_T6_obj =
{                                 
	.value_array = NULL,
	.msg_handler = T6_msg_handler,
	.config_crc = 0,
};

struct obj_t maxTouchCfg_T38_obj =
{                                 	
	.value_array = maxTouchCfg_T38,
	.msg_handler = NULL,
	.config_crc = 0,
};

struct obj_t maxTouchCfg_T7_obj =
{                                 
	.value_array = maxTouchCfg_T7,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T8_obj =
{                                 
	.value_array = maxTouchCfg_T8,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T9_obj =
{                                 
	.value_array = maxTouchCfg_T9,
	.msg_handler = T9_msg_handler,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T15_obj =
{                                 
	.value_array = maxTouchCfg_T15,
	
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T18_obj =
{                                 
	.value_array = maxTouchCfg_T18,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T19_obj =
{                                 
	.value_array = maxTouchCfg_T19,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T20_obj =
{                                 
	.value_array = maxTouchCfg_T20,
	.msg_handler = T20_msg_handler,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T22_obj =
{                                 
	.value_array = maxTouchCfg_T22,
	.msg_handler = T22_msg_handler,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T23_obj =
{                                 
	.value_array = maxTouchCfg_T23,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T24_obj =
{                                 
	.value_array = maxTouchCfg_T24,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T25_obj =
{                                 
	.value_array = maxTouchCfg_T25,
	.msg_handler = T25_msg_handler,
	.config_crc = 1,
	
};

struct obj_t maxTouchCfg_T27_obj =
{                                 
	.value_array = maxTouchCfg_T27,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T28_obj =
{                                 
	.value_array = maxTouchCfg_T28,
	.msg_handler = NULL,
	.config_crc = 1,
};

struct obj_t maxTouchCfg_T37_obj =
{                                 
	.value_array = NULL,
	.msg_handler = NULL,
	.config_crc = 0,
};

struct obj_t maxTouchCfg_T44_obj =
{                                 
	.value_array = NULL,
	.msg_handler = NULL,
	.config_crc = 0,
};
#endif
