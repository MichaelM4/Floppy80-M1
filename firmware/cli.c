#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "tusb.h"
#include "stdio.h"
#include "defines.h"
#include "system.h"
#include "file.h"
#include "fdc.h"
#include "hdc.h"

void ServiceFdcLog(void);

extern FdcDriveType g_dtDives[MAX_DRIVES];
extern TrackType    g_tdTrack;

static uint64_t g_nCdcPrevTime;
static uint32_t g_nCdcConnectDuration;
static bool     g_bCdcConnected;
static bool     g_bCdcPromptSent;

static char     g_szCommandLine[64];
static int      g_nCommandLineIndex;

static DIR     dj;				// Directory object
static FILINFO fno;				// File information

static bool    g_bOutputLog = false;

static char szHelpText[] = {
                        "\n"
                        "help       - returns this message\n"
                        "status     - returns the current FDC status\n"
                        "dir filter - returns a directory listing of the root folder of the SD-Card\n"
                        "             optionally include a filter.  For example dir .ini\n"
                        "boot file  - selects an ini file to be specified in the boot.cfg\n"
                        "logon      - enable output of FDC interface logging output\n"
                        "logoff     - disable output of FDC interface logging output\n"
                        "disks      - returns the stats to the mounted diskettes\n"
                        "dump drive - returns sectors of each track on the indicate drive (0 - 2)\n"
                        "hdc        - creates a new vitual hard disk. Usage:\n"
                        "             hdc file.ext heads cylinders sectors\n"
                    };

void InitCli(void)
{
    g_bCdcPromptSent = false;
    g_bCdcConnected  = false;
    g_nCdcPrevTime   = time_us_64();
}

void ListFiles(char* pszFilter)
{
    FRESULT fr;  // Return value
    int nCol = 0;

    memset(&dj, 0, sizeof(dj));
    memset(&fno, 0, sizeof(fno));
    fr = f_findfirst(&dj, &fno, "0:", "*");

	while ((fr == FR_OK) && (fno.fname[0] != 0))
	{
		if ((fno.fattrib & AM_DIR) || (fno.fattrib & AM_SYS))
		{
			// pcAttrib = pcDirectory;
		}
		else
		{
			if ((pszFilter[0] == 0) || (stristr(fno.fname, pszFilter) != NULL))
			{
                ++nCol;

    			if (nCol < 5)
				{
                    printf("%30s %9d", fno.fname, fno.fsize);
                }
                else
                {
                    printf("%30s %9d\r\n", fno.fname, fno.fsize);
                    nCol = 0;
                }
            }
		}

		if (fno.fname[0] != 0)
		{
			fr = f_findnext(&dj, &fno); /* Search for next item */
		}
	}
}

void ProcessDisksRequest(void)
{
    int i;
    char* pszDensity[] = {"SD", "DD"};

    for (i = 0; i < MAX_DRIVES; ++i)
    {
        printf("File name  : %s\r\n", g_dtDives[i].szFileName);
        printf("Density    : %s\r\n", pszDensity[g_dtDives[i].dmk.byDensity]);
        printf("Num sides  : %d\r\n", g_dtDives[i].dmk.byNumSides);
        printf("Track size : %d\r\n", g_dtDives[i].dmk.wTrackLength);
        printf("Sector size: %d\r\n", g_dtDives[i].dmk.nSectorSize);
    }
}

void DumpSector(int nDrive, int nTrack, int nSector)
{
    int nOffset = g_tdTrack.nSectorIndexMarkOffset[nSector];

    if (nOffset < 0)
    {
        return;
    }

    BYTE* pby = g_tdTrack.byTrackData + nOffset - 3;
    int   i = 1;
    int   state = 0;
    int   size = 512;
    int   nDataSize = 1;

    while (i <= size)
    {
        printf("%02X ", *pby);

        switch (state)
        {
            case 0:
                if (*pby == 0xFE)
                {
                    if (*(pby+1) == 0xFE)
                    {
                        nDataSize = 2;
                    }

                    printf("%02X %02X %02X %02X %02X %02X\r\n"
                           "DRV: %02X TRK: %02X S: %02X SEC: %02X LEN: %02X CRC: %02X%02X",
                           *(pby+1*nDataSize), *(pby+2*nDataSize), *(pby+3*nDataSize),
                           *(pby+4*nDataSize), *(pby+5*nDataSize), *(pby+6*nDataSize),
                           nDrive, *(pby+1*nDataSize), *(pby+2*nDataSize), *(pby+3*nDataSize),
                           *(pby+4*nDataSize), *(pby+5*nDataSize), *(pby+6*nDataSize));
                    size = 128 << *(pby+4*nDataSize);
                    i = 0;
                    pby += 7*nDataSize;
                    ++state;
                }

                break;

            case 1:
                if ((*pby == 0xFB) || (*pby == 0xF8))
                {
                    printf("\r\nSector data");
                    i = 0;
                    ++state;
                }

                break;

            case 2:
                break;
        }

        if ((i % 16) == 0)
        {
            printf("\r\n");
            while (tud_cdc_write_available() < 56);
        }

        pby += nDataSize;
        ++i;
    }

    printf("CRC: %02X %02X\r\n\r\n", *pby, *(pby+1*nDataSize));
    sleep_ms(5);
}

void ProcessDumpRequest(int nDrive)
{
    if (nDrive == 4)
    {
        HdcDumpDisk(nDrive-4);
        return;
    }

    if ((nDrive < 0) || (nDrive >= MAX_DRIVES))
    {
        puts("Invalid drive index specified.");
        return;
    }

    int nTracks = g_dtDives[nDrive].dmk.byDmkDiskHeader[1];
    int i, j, k;
    int nSides = 2;

    if (g_dtDives[nDrive].dmk.byDmkDiskHeader[4] & 0x10)
    {
        nSides = 1;
    }

    for (i = 0; i < nTracks; ++i)
    {
        for (k = 0; k < nSides; ++k)
        {
            FdcReadTrack(nDrive, k, i);

            for (j = 0; j < 64; ++j)
            {
                DumpSector(nDrive, i, j);
            }
        }
    }
}

void CreateHdcFile(char* psz)
{
    char szParm1[16] = {""};
    char szFileName[32];
    int  nHeads, nCylinders, nSectors;

    psz = GetWord(psz, szFileName, sizeof(szFileName)-2);

    psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
    nHeads = atoi(szParm1);

    psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
    nCylinders = atoi(szParm1);

    psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
    nSectors = atoi(szParm1);

    printf("File name: %s\r\n"
           "Heads    : %d\r\n"
           "Cylinders: %d\r\n"
           "Sectors  : %d\r\n",
           szFileName, nHeads, nCylinders, nSectors);

    HdcCreateVhd(szFileName, nHeads, nCylinders, nSectors);
}

void ProcessCommand(char* psz)
{
    char szParm1[16] = {""};
    char szCmd[16] = {""};

    psz = GetWord(psz, szCmd, sizeof(szCmd)-2);

    if (stricmp(szCmd, "HELP") == 0)
    {
        puts(szHelpText);
        return;
    }

    if (stricmp(szCmd, "DIR") == 0)
    {
        psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
        ListFiles(szParm1);
        return;
    }

    if (stricmp(szCmd, "BOOT") == 0)
    {
        psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
		FdcSaveBootCfg(szParm1);
        FdcProcessStatusRequest(true);
        return;
    }

    if (stricmp(szCmd, "STATUS") == 0)
    {
        FdcProcessStatusRequest(true);
        return;
    }

    if (stricmp(szCmd, "LOGON") == 0)
    {
        g_bOutputLog = true;
        return;
    }

    if (stricmp(szCmd, "LOGOFF") == 0)
    {
        g_bOutputLog = false;
        return;
    }
    
    if (stricmp(szCmd, "DISKS") == 0)
    {
        ProcessDisksRequest();
        return;
    }

    if (stricmp(szCmd, "DUMP") == 0)
    {
        psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
        ProcessDumpRequest(atoi(szParm1));
        return;
    }

    if (stricmp(szCmd, "HDC") == 0)
    {
        CreateHdcFile(psz);
        return;
    }

    puts("Unknown command");
    puts(szHelpText);
}

void ServiceCli(void)
{
    uint64_t nTimeNow;
    char*    prompt = {"\nCMD>"};
    int      c;

    if (!tud_cdc_connected())
    {
        g_bCdcConnected = false;
        return;
    }

    if (g_bCdcConnected == false)
    {
        g_bCdcConnected = true;
       	g_nCdcPrevTime  = time_us_64();
        g_nCdcConnectDuration = 0;
        g_szCommandLine[0] = 0;
        g_nCommandLineIndex = 0;
        return;
    }

    if (g_bCdcPromptSent == false)
    {
        nTimeNow = time_us_64();
        g_nCdcConnectDuration += (g_nCdcPrevTime - nTimeNow);
        g_nCdcPrevTime = nTimeNow;

        if (g_nCdcConnectDuration < 2000000)
        {
            return;
        }

        printf("\nCMD> ");
        g_bCdcPromptSent = true;
    }

#ifdef ENABLE_LOGGING
    if (g_bOutputLog)
    {
        ServiceFdcLog();
    }
    else
    {
        log_tail = log_head;
    }
#endif

    c = getchar_timeout_us(0);

    if (c == PICO_ERROR_TIMEOUT) // no new characters
    {
        return;
    }

    if (c == '\r')
    {
        puts("");
        ProcessCommand(g_szCommandLine);
        printf("\nCMD> ");
        g_nCommandLineIndex = 0;
        g_szCommandLine[0] = 0;
    }
    else if (c == '\b') // backspace
    {
        if (g_nCommandLineIndex > 0)
        {
            --g_nCommandLineIndex;
            g_szCommandLine[g_nCommandLineIndex] = 0;
            printf("\b \b");
        }
    }
    else if (c < 32)
    {
        while (c != PICO_ERROR_TIMEOUT)
        {
            c = getchar_timeout_us(100);
        }
    }
    else if (g_nCommandLineIndex < sizeof(g_szCommandLine)-2)
    {
        if ((c != '\n') && (c != '\r'))
        {
            printf("%c", c);
            g_szCommandLine[g_nCommandLineIndex] = c;
            ++g_nCommandLineIndex;
            g_szCommandLine[g_nCommandLineIndex] = 0;
        }
    }
}
