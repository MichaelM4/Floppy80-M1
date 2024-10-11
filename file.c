#include "defines.h"
#include "file.h"
#include "system.h"
#include "string.h"

//-----------------------------------------------------------------------------

file g_fFiles[MAX_FILES];

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
	f_lseek(&fp->f, nOffset);
#endif
}

//-----------------------------------------------------------------------------
void FileFlush(file* fp)
{
#ifdef MFC
	fp->f.Flush();
#else
	f_sync(&fp->f);
#endif
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
	FRESULT fr;  /* Return value */
	DIR     dj;  /* Directory object */
	FILINFO fno; /* File information */
	
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
