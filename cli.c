#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "stdio.h"

#include "defines.h"
#include "system.h"
#include "file.h"
#include "fdc.h"
#include "video.h"
#include "cli.h"

uint64_t g_nCdcPrevTime;
uint32_t g_nCdcConnectDuration;
bool     g_bCdcConnected;
bool     g_bCdcPromptSent;

char     g_szCommandLine[64];
int      g_nCommandLineIndex;

static DIR     dj;				// Directory object
static FILINFO fno;				// File information

char szHelpText[] = {
                        "\n"
                        "help   - returns this message\n"
                        "status - returns the current FDC status\n"
                        "dir    - returns a directory listing of the root folder of the SD-Card\n"
                        "         optionally include a filter.  For example dir .ini\n"
                        "boot   - selects an ini file to be specified in the boot.cfg\n"
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
				puts(fno.fname);
			}
		}

		if (fno.fname[0] != 0)
		{
			fr = f_findnext(&dj, &fno); /* Search for next item */
		}
	}
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

    puts("Unknown command");
    puts(szHelpText);
}

void ServiceCli(void)
{
    uint64_t nTimeNow;
    char*    prompt = {"\nCMD>"};
    int c;

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
        g_nCdcConnectDuration += GetTimeDiff(g_nCdcPrevTime, nTimeNow);
        g_nCdcPrevTime = nTimeNow;

        if (g_nCdcConnectDuration < 2000000)
        {
            return;
        }

        printf("\nCMD> ");
        g_bCdcPromptSent = true;
    }

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
