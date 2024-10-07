
#ifndef _H_DEFINES_
#define _H_DEFINES_

#include <stdio.h>

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
  #define MAX_PATH 256
#endif

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

///////////////////////////////////////////////////////////////////////////////////////////////////
// global variables

//-----------------------------------------------------------------------------
// counter for real time clock (RTC).

extern uint32_t g_dwForegroundRtc;
extern uint32_t g_dwBackgroundRtc;
extern uint16_t g_wWatchdogRefreshCounter;
extern uint32_t g_dwRTC;

extern uint8_t  g_byCaptureBusActivity;

extern uint32_t g_dwRotationTime;
extern uint32_t g_dwIndexTime;
extern uint32_t g_dwResetTime;

extern uint8_t  g_byMonitorReset;

extern uint64_t g_nWaitTime;
extern uint64_t g_nTimeStart;
extern uint64_t g_nTimeEnd;
extern uint64_t g_nTimeDiff;

extern uint8_t  sd_byCardInialized;

///////////////////////////////////////////////////////////////////////////////////////////////////

void InitVars(void);
void ReleaseWait(void);

///////////////////////////////////////////////////////////////////////////////////////////////////

#endif
