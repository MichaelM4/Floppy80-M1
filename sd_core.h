#ifndef __SDHC_C_
#define __SDHC_C_

#include "ff.h"

/* type definitions ==========================================*/

/* global variable declarations ==========================================*/

extern BYTE    sd_byCardInialized;
extern DWORD   g_dwSdCardPresenceCount;
extern DWORD   g_dwSdCardMaxPresenceCount;

/* function prototypes ==========================================*/

unsigned char get_cd(void);
unsigned char get_wp(void);

BYTE IsSdCardInserted(void);
BYTE IsSdCardWriteProtected(void);
void IdentifySdCard(void);
void TestSdCardInsertion(void);
void SDHC_Init(void);

void InitTraceCapture(void);

#endif


