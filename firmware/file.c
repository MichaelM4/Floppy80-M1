#include "defines.h"
#include "file.h"
#include "system.h"
#include "string.h"

#ifndef MFC
#include "pico/stdlib.h"
#endif

// Buffered writes are synced to the SD card after the file has been idle for SD_FLUSH_DELAY_MS,
// and never later than SD_FLUSH_MAX_MS after the first unsynced write.
// SD_FLUSH_DELAY_MS = 0 gives the original behaviour (f_sync after every write).
#ifndef SD_FLUSH_DELAY_MS
#define SD_FLUSH_DELAY_MS 200
#endif
#ifndef SD_FLUSH_MAX_MS
#define SD_FLUSH_MAX_MS 2000
#endif

// Number of 32-bit entries in each file's cluster link map (2 per fragment + 1).
#ifndef LINKMAP_ENTRIES
#define LINKMAP_ENTRIES 128
#endif

//-----------------------------------------------------------------------------

static file    g_fFiles[MAX_FILES];
static FRESULT fr;  /* Return value */
static DIR     dj;  /* Directory object */
static FILINFO fno; /* File information */

#ifndef MFC
static DWORD g_dwLinkMap[MAX_FILES][LINKMAP_ENTRIES];
static byte  g_byAnyDirty = FALSE;
#endif

//-----------------------------------------------------------------------------
file* FileOpen(char* pszFileName, BYTE byMode)
{
	FRESULT fr = FR_NO_FILE;
	UINT    nMode;
	int     i;

#ifndef MFC
	if (sd_byCardInialized == 0)
	{
		return NULL;
	}
#endif

	// find the first unused file buffer
	i = 0;

	while ((i < MAX_FILES) && (g_fFiles[i].byIsOpen))
	{
		++i;
	}

	if (i >= MAX_FILES)
	{
		return NULL;
	}

#ifdef MFC
	CString str;
	
	str   = pszFileName;
	nMode = 0;

	if ((byMode & FA_READ) && (byMode & FA_WRITE))
	{
		nMode = CFile::modeReadWrite;
	}
	else if (byMode & FA_WRITE)
	{
		nMode = CFile::modeWrite;
	}
	else if (byMode & FA_READ)
	{
		nMode = CFile::modeRead;
	}

	if (g_fFiles[i].f.Open(str, nMode | CFile::typeBinary))
	{
		fr = FR_OK;
	}
#else
	fr = f_open(&g_fFiles[i].f, pszFileName, byMode);
#endif

	if (fr == FR_OK)
	{
		g_fFiles[i].byIsOpen = TRUE;
		g_fFiles[i].byDirty  = FALSE;
		return &g_fFiles[i];
	}
	
	return NULL;
}

//-----------------------------------------------------------------------------
void FileClose(file* fp)
{
	if ((fp == NULL) || (fp->byIsOpen == FALSE))
	{
		return;
	}

#ifdef MFC
	fp->f.Close();
#else
	f_close(&fp->f);
#endif

	fp->byIsOpen = FALSE;
	fp->byDirty  = FALSE;	// f_close() syncs
}

//-----------------------------------------------------------------------------
BYTE FileIsOpen(file *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}

	return fp->byIsOpen;
}

//-----------------------------------------------------------------------------
uint32_t FileRead(file* fp, BYTE* pby, uint32_t nSize)
{
	UINT br;

	if ((fp == NULL) || (fp->byIsOpen == 0))
	{
		return 0;
	}

#ifdef MFC
	br = fp->f.Read(pby, nSize);
#else
	f_read(&fp->f, pby, nSize, &br);
#endif

	return br;
}

//-----------------------------------------------------------------------------
uint32_t FileWrite(file* fp, BYTE* pby, uint32_t nSize)
{
	UINT bw;

	if (fp == NULL)
	{
		return 0;
	}

#ifdef MFC
	fp->f.Write(pby, nSize);
	bw = nSize;
#else
	// a file with a link map cannot grow; drop the map if this write would extend the file
	if ((fp->f.cltbl != NULL) && ((f_tell(&fp->f) + nSize) > f_size(&fp->f)))
	{
		fp->f.cltbl = NULL;
	}

	f_write(&fp->f, pby, nSize, &bw);
#endif

	return bw;
}

//-----------------------------------------------------------------------------
void FileSeek(file* fp, int nOffset)
{
	if (fp == NULL)
	{
		return;
	}

#ifdef MFC
	int n = (int)fp->f.Seek(nOffset, CFile::begin);
#else
	// with a link map, seeking to or past the end of the file would be clipped; drop the map instead
	if ((fp->f.cltbl != NULL) && ((FSIZE_t)nOffset >= f_size(&fp->f)))
	{
		fp->f.cltbl = NULL;
	}

	f_lseek(&fp->f, nOffset);
#endif
}

//-----------------------------------------------------------------------------
void FileFlush(file* fp)
{
	if ((fp == NULL) || (fp->byIsOpen == FALSE))
	{
		return;
	}

#ifdef MFC
	fp->f.Flush();
#elif (SD_FLUSH_DELAY_MS > 0)
	// Deferred: f_write() has already pushed full sectors to the card. The remaining partial
	// sector and the directory entry are written by FileServiceFlush() once the file is idle.
	uint32_t now = to_ms_since_boot(get_absolute_time());

	if (fp->byDirty == FALSE)
	{
		fp->byDirty = TRUE;
		fp->dwFirstDirtyMs = now;
	}

	fp->dwLastWriteMs = now;
	g_byAnyDirty = TRUE;
#else
	f_sync(&fp->f);
#endif
}

//-----------------------------------------------------------------------------
// Syncs files whose last write is older than SD_FLUSH_DELAY_MS (or whose oldest unsynced
// write is older than SD_FLUSH_MAX_MS). Call from the main loop on core 0.
void FileServiceFlush(void)
{
#if !defined(MFC) && (SD_FLUSH_DELAY_MS > 0)
	uint32_t now;
	int      i;
	byte     byStillDirty = FALSE;

	if (g_byAnyDirty == FALSE)
	{
		return;
	}

	now = to_ms_since_boot(get_absolute_time());

	for (i = 0; i < MAX_FILES; ++i)
	{
		file* fp = &g_fFiles[i];

		if ((fp->byIsOpen == FALSE) || (fp->byDirty == FALSE))
		{
			continue;
		}

		if (((now - fp->dwLastWriteMs) >= SD_FLUSH_DELAY_MS) || ((now - fp->dwFirstDirtyMs) >= SD_FLUSH_MAX_MS))
		{
			f_sync(&fp->f);
			fp->byDirty = FALSE;
		}
		else
		{
			byStillDirty = TRUE;
		}
	}

	g_byAnyDirty = byStillDirty;
#endif
}

//-----------------------------------------------------------------------------
// Syncs every file that has unsynced writes, right now.
void FileFlushAll(void)
{
#if !defined(MFC) && (SD_FLUSH_DELAY_MS > 0)
	int i;

	for (i = 0; i < MAX_FILES; ++i)
	{
		file* fp = &g_fFiles[i];

		if (fp->byIsOpen && fp->byDirty)
		{
			f_sync(&fp->f);
			fp->byDirty = FALSE;
		}
	}

	g_byAnyDirty = FALSE;
#endif
}

//-----------------------------------------------------------------------------
// Builds a cluster link map so that f_lseek() jumps straight to the right cluster instead of
// walking the FAT chain from the start of the file. Use on disk images that are normally
// opened read/write and are not growing. If a later write or seek needs the file to grow,
// FileWrite()/FileSeek() quietly drop the map and carry on with normal seeking.
// Returns TRUE if the map was built (it fails if the file has too many fragments).
BYTE FileEnableFastSeek(file* fp)
{
#ifdef MFC
	return FALSE;
#else
	int i;

	if ((fp == NULL) || (fp->byIsOpen == FALSE))
	{
		return FALSE;
	}

	i = (int)(fp - g_fFiles);

	if ((i < 0) || (i >= MAX_FILES))
	{
		return FALSE;
	}

	g_dwLinkMap[i][0] = LINKMAP_ENTRIES;
	fp->f.cltbl = g_dwLinkMap[i];

	if (f_lseek(&fp->f, CREATE_LINKMAP) != FR_OK)
	{
		fp->f.cltbl = NULL;
		return FALSE;
	}

	f_lseek(&fp->f, 0);
	return TRUE;
#endif
}

//-----------------------------------------------------------------------------
void FileTruncate(file* fp)
{
#ifndef MFC
	fp->f.cltbl = NULL;	// truncating changes the cluster chain
#endif
	f_truncate(&fp->f);
}

//-----------------------------------------------------------------------------
void FileCloseAll(void)
{
	int i;

	for (i = 0; i < MAX_FILES; ++i)
	{
		FileClose(&g_fFiles[i]);
	}
}

//-----------------------------------------------------------------------------
void FileSystemInit(void)
{
	int i;

	for (i = 0; i < MAX_FILES; ++i)
	{
		g_fFiles[i].byIsOpen = false;
	}
}

//-----------------------------------------------------------------------------
BYTE IsEOF(file* fp)
{
	if (fp == NULL)
	{
		return TRUE;
	}

#ifdef MFC
	int nSize = (int)fp->f.GetLength();
	if (fp->f.GetPosition() < nSize)
	{
		return FALSE;
	}
	else
	{
		return TRUE;
	}
#else
	return f_eof(&fp->f);
#endif
}

//-----------------------------------------------------------------------------
int FileReadLine(file* fp, char szLine[], int nMaxLen)
{
	char* psz;
	int   nLen = 0;
	
	if ((fp == NULL) || (IsEOF(fp)))
	{
		return -1;
	}

	if (nMaxLen == 0)
	{
		return -1;
	}

	--nMaxLen;
	szLine[0] = 0;

#ifdef MFC
	char  ch = 0;
	int   i = 0;
	int   nSize = (int)fp->f.GetLength();
	int   nPos  = (int)fp->f.GetPosition();

	while ((ch != '\r') && (ch != '\n') && (i < nMaxLen) && (nPos < nSize))
	{
		fp->f.Read(&ch, 1);
		++nPos;

		if ((ch != '\r') && (ch != '\n'))
		{
			szLine[i] = ch;
			++i;
		}
	}

	szLine[i] = 0;
#else
	f_gets(szLine, nMaxLen, &fp->f);						/* Get a string from the file */
#endif

	// remove CR
	psz = strchr(szLine, '\r');

	if (psz != NULL)
	{
		*psz = 0;
	}

	// remove LF
	psz = strchr(szLine, '\n');

	if (psz != NULL)
	{
		*psz = 0;
	}

	nLen = (int)strlen(szLine);

	return nLen;
}

////////////////////////////////////////////////////////////////////////////////////
BYTE FileExists(char* pszFileName)
{
#ifdef MFC
	CFile f;
	CString str;
	
	str = pszFileName;

	if (!f.Open(str, CFile::modeRead))
	{
		return FALSE;
	}

	f.Close();
	return TRUE;
#else
	memset(&dj, 0, sizeof(dj));
	memset(&fno, 0, sizeof(fno));

	fr = f_findfirst(&dj, &fno, "0:", "*");

	if (FR_OK != fr)
	{
		return FALSE;
	}

	while ((fr == FR_OK) && (fno.fname[0] != 0)) /* Repeat until a file is found */
	{
		if ((fno.fattrib & AM_DIR) || (fno.fattrib & AM_SYS))
		{
		// pcAttrib = pcDirectory;
		}
		else if (stricmp(fno.fname, pszFileName) == 0)
		{
			return TRUE;
		}

		fr = f_findnext(&dj, &fno); /* Search for next item */
	}

	return FALSE;
#endif
}
