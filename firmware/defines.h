
#ifndef _H_DEFINES_
#define _H_DEFINES_

#define ENABLE_DOUBLER 1
#define ENABLE_LOGGING 1

//#pragma GCC optimize ("O0")
#pragma GCC optimize ("O3")

#include <stdio.h>

#define CDC_ITF     0           /* USB CDC interface no */

typedef signed char        	int8_t;
typedef unsigned char		uint8_t;
typedef short              	int16_t;
typedef unsigned short     	uint16_t;
typedef long               	int32_t;
typedef unsigned long      	uint32_t;
typedef long long          	int64_t;
typedef unsigned long long 	uint64_t;

typedef unsigned char		byte;
typedef unsigned short     	word;
typedef unsigned long      	dword;

#define SizeOfArray(x) (sizeof(x) / sizeof(x[0]))

// set to 1 to enable capture of bus activity logging to sd-card
#define ENABLE_TRACE_LOG 0

#define INT_PIN       0
#define WAIT_PIN      1
#define CLK_SCLK      2
#define CMD_MOSI      3

#define DAT0_MISO     4
#define DAT3_CS       5
#define LED_PIN       6
#define ADDRL_OE_PIN  7

#define DATAB_OE_PIN  8
#define ADDRH_OE_PIN  9
#define IN_PIN        10
#define RD_PIN        11

#define WR_PIN        12
#define OUT_PIN       13
#define MREQ_PIN      14
#define D0_PIN        15

#define D1_PIN        16
#define D2_PIN        17
#define D3_PIN        18
#define D4_PIN        19

#define D5_PIN        20
#define D6_PIN        21
#define D7_PIN        22

#define DIR_PIN       26
#define SYSRES_PIN    27
#define CD_PIN        28

// Bit  Net      Input    Hex
//             (1=input)
// 31    -         0
// 30    -         0
// 29    -         0
// 28    CD        1       1     0
//
// 27    SYSRES    1
// 26    DIR       0
// 25    -         0
// 24    -         0       8     0
//
// 23    -         0
// 22    D7        1
// 21    D6        1
// 20    D5        1       7     7
//
// 19    D4        1
// 18    D3        1
// 17    D2        1
// 16    D1        1       F     F
//
// 15    D0        1
// 14    RAS       1
// 13    OUT       1
// 12    WR        1       F     8
//
// 11    RD        1
// 10    IN        1
//  9    ADDRH     0
//  8    DATAB     0       C     0
//
//  7    ADDRL     0
//  6    LED       0
//  5    DAT3_CS   0
//  4    DAT0_MISO 0       0     0
//
//  3    CMD_MOSI  0
//  2    CLK_SCLK  0
//  1    WAIT      0
//  0    INT       0       0     0

#define GPIO_IN_MASK  0x187FFC00
#define DATA_BUS_MASK 0x007F8000

#define A_TO_B_DIR 1
#define B_TO_A_DIR 0

///////////////////////////////////////////////////////////////////////////////////////////////////

#define get_gpio_data_byte() (sio_hw->gpio_in >> D0_PIN)
#define get_gpio_read_bus()  (sio_hw->gpio_in >> IN_PIN)
#define get_gpio(gpio)       (sio_hw->gpio_in & (1u << gpio))
#define set_gpio(gpio)        sio_hw->gpio_set = 1u << gpio
#define clr_gpio(gpio)        sio_hw->gpio_clr = 1u << gpio
#define set_bus_as_output()   sio_hw->gpio_oe_set = 0xFF << D0_PIN
#define set_bus_as_input()    sio_hw->gpio_oe_clr = 0xFF << D0_PIN
#define put_byte_on_bus(data) sio_hw->gpio_togl = (sio_hw->gpio_out ^ (data << D0_PIN)) & (0xFF << D0_PIN)

// #define get_gpio_data_byte()  gpio_get_all() >> D0_PIN
// #define get_gpio(gpio)        gpio_get(gpio)
// #define set_gpio(gpio)        gpio_put(gpio, 1)
// #define clr_gpio(gpio)        gpio_put(gpio, 0)
// #define set_bus_as_output()   gpio_set_dir_out_masked(0xFF << D0_PIN)
// #define set_bus_as_input()    gpio_set_dir_in_masked(0xFF << D0_PIN)
// #define put_byte_on_bus(data) gpio_put_masked(0xFF << D0_PIN, data << D0_PIN)

///////////////////////////////////////////////////////////////////////////////////////////////////

// Low Power Timer Overflow Rates in Hz
#define TIMER_OVERFLOW_RATE 2000

///////////////////////////////////////////////////////////////////////////////////////////////////
// generic defines/equates

#ifndef TRUE
	#define FALSE	0
	#define TRUE	1
#endif

#ifndef true
	#define false	0
	#define true	1
#endif

#ifndef MAX_PATH
  #define MAX_PATH 64
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// FDC logging

typedef struct {
	uint8_t type;
	uint8_t val;
	uint8_t op1;
} LogType;

#define LOG_SIZE 4096

extern LogType fdc_log[LOG_SIZE];
extern int log_head;
extern int log_tail;

enum {
	write_drive_select = 0,
	write_data,
	write_sector,
	write_track,
	write_cmd,
	read_drive_select,
	read_data,
	read_sector,
	read_track,
	read_status,
	port_out,
	port_in
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// typedefs

typedef struct {
   uint64_t nSectorsTotal;
   uint64_t nSectorsFree;
   uint64_t nSectorSize;

   uint32_t nClustersTotal;
   uint32_t nClustersFree;
   uint32_t nClustersUsed;
   uint32_t nClustersBad;
   uint32_t nClusterSize;
} F_SPACE;

///////////////////////////////////////////////////////////////////////////////////////////////////
// enumerations

enum {
  opRead = 0,
  opWrite,
  opNop
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// global variables

//-----------------------------------------------------------------------------
// counter for real time clock (RTC).

extern volatile uint8_t sd_byCardInialized;
extern volatile byte    g_byFdcIntrActive;
extern volatile byte    g_byRtcIntrActive;
extern volatile byte    g_byResetActive;
extern volatile byte    g_byEnableIntr;
extern volatile int32_t g_nRotationCount;
extern volatile byte    g_byEnableUpperMem;
extern volatile byte    g_byEnableWaitStates;

///////////////////////////////////////////////////////////////////////////////////////////////////

void InitVars(void);
void ReleaseWait(void);

byte* TD230_GetPtr(void);
int   TD230_Size(void);

///////////////////////////////////////////////////////////////////////////////////////////////////

#endif
