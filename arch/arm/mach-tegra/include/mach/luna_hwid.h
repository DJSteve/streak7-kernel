#ifndef __LUNA_HWID_H_
#define __LUNA_HWID_H_
typedef enum {
	
	EVT1A = 0x00000000,
	EVT1B,
	EVT1_2 = EVT1B,
	EVT1_3,
	EVT2,
	EVT2_2,
	EVT2_3,		
	EVT2_4,
	EVT3,
	EVT3_1_B,
	DVT1,
	DVT1_A02,	
	DVT1_A02_3G_WG,
	DVT1_A01_3G_WG,
	DVT1_S08_RW,
	HWID_UNKNOWN = 0xFFFFFFFF,
} HWID;

typedef struct _HWID_STRING_MAPPING_ {
    HWID    hwid;
    char    hwstring[16];
} HWID_STRING_MAPPING;

static const HWID_STRING_MAPPING hwid_string_mapping[] = {
    { EVT1A,  "EVT1A" }, 
    { EVT1_2, "EVT1_2" }, 
    { EVT1_3, "EVT1_3" }, 
    { EVT2,   "EVT2" }, 
    { EVT2_2, "EVT2_2" }, 
    { EVT2_3, "EVT2_3" }, 
    { EVT2_4, "EVT2_4" }, 
    { EVT3,   "EVT3" }, 
    { EVT3_1_B,   "EVT3_1_B" }, 
    { DVT1,   "DVT1" }, 
	{ DVT1_A02,	"DVT1_A02" },
	{ DVT1_A02_3G_WG, "DVT1_A02_3G_WG" },
	{ DVT1_A01_3G_WG, "DVT1_A01_3G_WG" },
	{ DVT1_S08_RW, "DVT1_S08_RW" },
    { HWID_UNKNOWN, "UNKNOWN" }
};

#endif
