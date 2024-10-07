
#ifndef _SYSTEM_H
#define _SYSTEM_H

#define MAX_DRIVES 4

#ifndef MAX_PATH
	#define MAX_PATH 256
#endif

// structures
//-----------------------------------------------------------------------------

typedef struct {
	int  nModel;
	char szIniFolder[256];
	char szDrivePath[MAX_DRIVES][MAX_PATH];
	char szRomPath[MAX_PATH];
	int  nRomAddr;
} SystemType;

// unions
//-----------------------------------------------------------------------------

// variables

extern bool g_bLogOpen;
extern SystemType sysdef;

// function definitions
//-----------------------------------------------------------------------------

void OpenLogFile(void);
void CloseLogFile(void);
void WriteLogFile(char* psz);

uint32_t GetCycDuration(uint32_t dwStart, uint32_t dwEnd);
void  StartStopWatch(void);
void  StopStopWatch(void);
float GetStopWatchDuration(void);

uint32_t CountDown(uint32_t nCount, uint32_t nAdjust);
uint32_t CountUp(uint32_t nCount, uint32_t nAdjust);

////////////////////////////////////////////////////////////////////////////////////

char* SkipBlanks(char* psz);
char* SkipToBlank(char* psz);
void  CopySectionName(char* pszSrc, char* pszDst, int nMaxLen);
char* CopyLabelName(char* pszSrc, char* pszDst, int nMaxLen);
void  CopyString(char* pszSrc, char* pszDst, int nMaxLen);
void  StrToUpper(char* psz);
char* stristr(char* psz, char* pszFind);

void  AddTrailingBackslash(char szFilePath[], int nMaxLen);

#ifndef MFC
  int   stricmp(char* psz1, char* psz2);
  void  strcat_s(char* pszDst, int nDstSize, char* pszSrc);
#endif

#ifdef MFC
	UINT64 time_us_64(void);
#endif

void   UpdateCounters(void);
void   LoadIniFile(char* pszFileName);

#endif

/* END OF FILE */
