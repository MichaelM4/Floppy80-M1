
#ifndef _SYSTEM_H
#define _SYSTEM_H

#ifndef MAX_PATH
	#define MAX_PATH 64
#endif

// structures
//-----------------------------------------------------------------------------

// unions
//-----------------------------------------------------------------------------

// variables

// function definitions
//-----------------------------------------------------------------------------

void reset_system(void);

void OpenLogFile(void);
void CloseLogFile(void);
void WriteLogFile(char* psz);

void  StartStopWatch(void);
void  StopStopWatch(void);
float GetStopWatchDuration(void);

uint32_t CountDown(uint32_t nCount, uint32_t nAdjust);
uint32_t CountUp(uint32_t nCount, uint32_t nAdjust);

////////////////////////////////////////////////////////////////////////////////////

char* SkipBlanks(char* psz);
char* SkipToBlank(char* psz);
char* GetWord(char* psz, char* dest, int max_len);

char* CopyLabelName(char* pszSrc, char* pszDst, int nMaxLen);
void  CopyString(char* pszSrc, char* pszDst, int nMaxLen);
void  StrToUpper(char* psz);
char* stristr(char* psz, char* pszFind);

void  AddTrailingBackslash(char szFilePath[], int nMaxLen);

#ifndef MFC
    int  stricmp(char* psz1, char* psz2);
    void strcpy_s(char* pszDst, int nDstSize, char* pszSrc);
    void strcat_s(char* pszDst, int nDstSize, char* pszSrc);
    #define sprintf_s snprintf
#endif

#ifdef MFC
	UINT64 time_us_64(void);
#endif

void UpdateCounters(void);
void LoadIniFile(char* pszFileName);

#endif
