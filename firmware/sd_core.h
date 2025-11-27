#ifndef __SDHC_C_
#define __SDHC_C_

#include "ff.h"

/* type definitions ==========================================*/

/* global variable declarations ==========================================*/

extern volatile BYTE  sd_byCardInialized;
extern volatile DWORD g_dwSdCardPresenceCount;
extern volatile DWORD g_dwSdCardMaxPresenceCount;

/* function prototypes ==========================================*/

unsigned char get_cd(void);
unsigned char get_wp(void);

BYTE IsSdCardInserted(void);
BYTE IsSdCardWriteProtected(void);
void MountSdCard(void);
void TestSdCardInsertion(void);
void SDHC_Init(void);

void InitTraceCapture(void);

#endif


