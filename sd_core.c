#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "defines.h"
#include "sd_core.h"
#include "system.h"
#include "fdc.h"

#include "ff.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"

////////////////////////////////////////////////////////////////////////////////////

F_SPACE sd_Size;
BYTE    sd_byCurrentCdState;
BYTE    sd_byCurrentWpState;
BYTE    sd_byCardRemoved;
BYTE    sd_byCardInialized;
BYTE    sd_byPreviousCdState;
BYTE    sd_byPreviousWpState;
WORD    sd_wCardInitTries;
DWORD   g_dwSdCardPresenceCount;
DWORD   g_dwSdCardMaxPresenceCount;

////////////////////////////////////////////////////////////////////////////////////
static FATFS *sd_get_fs_by_name(const char *name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
	{
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
		{
			return &sd_get_by_num(i)->fatfs;
		}
	}

    return NULL;
}

//-----------------------------------------------------------------------------
// returns: 0 => card removed; 1 => card inserted;
unsigned char get_cd(void)
{
	if (gpio_get(CD_PIN) == 0)
	{
		return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// returns: 1 => write protected; 0 => not write protected
unsigned char get_wp(void)
{
	return 0;
}

//-----------------------------------------------------------------------------
BYTE IsSdCardInserted(void)
{
	if (sd_byCurrentCdState == 0)	// card is not present
	{
		return FALSE;
	}
	
	return TRUE;
}

//-----------------------------------------------------------------------------
BYTE IsSdCardWriteProtected(void)
{
	if (sd_byCurrentWpState == 1)	// card is write protected
	{
		return TRUE;
	}
	
	return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////
BYTE sd_getfreespace(void)
{
	char*    pszDrive = (char*)"0:";
    DWORD    fre_clust, fre_sect, tot_sect;
	uint64_t nSecPerCluster, nFree;

    /* Get volume information and free clusters of drive */
    FATFS* pfs = sd_get_fs_by_name(pszDrive);

    if (!pfs)
	{
        return 1;
    }

    FRESULT fr = f_getfree("0:", &fre_clust, &pfs);

    if (FR_OK != fr)
	{
        return 1;
    }

    /* Get total sectors and free sectors */
    tot_sect = (pfs->n_fatent - 2) * pfs->csize;
    fre_sect = fre_clust * pfs->csize;

   	sd_Size.nSectorsTotal  = tot_sect;
   	sd_Size.nSectorsFree   = fre_sect;

    /* assuming 512 bytes/sector */
   	sd_Size.nSectorSize    = 512;

	sd_Size.nClustersTotal = sd_Size.nSectorsTotal / pfs->csize;
	sd_Size.nClustersFree  = sd_Size.nSectorsFree / pfs->csize;
	sd_Size.nClustersUsed  = sd_Size.nClustersTotal - sd_Size.nClustersFree;
	sd_Size.nClustersBad   = 0;
	sd_Size.nClusterSize   = pfs->csize;

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////
void IdentifySdCard(void)
{
	FRESULT fr;
	BYTE    nCD;
	int     nRet;

	nCD = get_cd();

	sd_Size.nSectorsTotal  = 0;
	sd_Size.nSectorsFree   = 0;
	sd_Size.nSectorSize    = 0;
	sd_Size.nClustersTotal = 0;
	sd_Size.nClustersFree  = 0;
	sd_Size.nClustersUsed  = 0;
	sd_Size.nClustersBad   = 0;
	sd_Size.nClusterSize   = 0;

	if (nCD != 0)
	{
		if (sd_wCardInitTries < 5)
		{
			++sd_wCardInitTries;
			sd_byCardInialized = FALSE;

			if (!sd_init_driver())
			{
				return;
			}

			char*  pszDrive = (char*)"0:";
			FATFS* pfs = sd_get_fs_by_name(pszDrive);

			if (!pfs)
			{
				sd_byCardInialized = FALSE;
				return;
			}

			fr = f_mount(pfs, "0:", 1);

			if (fr == FR_OK)
			{
				nRet = sd_getfreespace();

				if (nRet == 0)
				{
					sd_byCardInialized = TRUE;
				}
				else
				{
					sd_byCardInialized = FALSE;
				}
			}
			else
			{
				sd_byCardInialized = FALSE;
			}
		}
		else
		{
			sd_byCardInialized = FALSE;
		}

		sd_byCardRemoved = FALSE;
	}
	else
	{
		sd_byCardRemoved   = TRUE;
		sd_byCardInialized = FALSE;
		sd_wCardInitTries  = 0;
	}
}

////////////////////////////////////////////////////////////////////////////////////
void TestSdCardInsertion(void)
{
	sd_byCurrentCdState = get_cd();		// 0 => card removed; 1 => card inserted;
	sd_byCurrentWpState = get_wp();		// 0 => not write protected; 1 => write protected;

	if (sd_byCurrentCdState != sd_byPreviousCdState)
	{
		if (sd_byCurrentCdState != 0)	// card has been inserted
		{
			if (g_dwSdCardPresenceCount < g_dwSdCardMaxPresenceCount) // wait for Sd-Card insertion to debounce
			{
				return;
			}
			
			sd_byCardRemoved  = TRUE;
			sd_wCardInitTries = 0;
			IdentifySdCard();
		}
		else // card was removed
		{
		}

		if (sd_byCurrentCdState == 0)		// card is not present
		{
			sd_byCardInialized = FALSE;
		}
	}

	if (sd_byCurrentWpState != sd_byPreviousWpState)
	{
		if ((sd_byCurrentCdState == 1) && (sd_byCurrentWpState == 1))	// card is write protected
		{
		}
	}

	sd_byPreviousCdState = sd_byCurrentCdState;
	sd_byPreviousWpState = sd_byCurrentWpState;
}

////////////////////////////////////////////////////////////////////////////////////
void SDHC_Init(void)
{
	sd_byCardRemoved           = FALSE;
	sd_byCardInialized         = FALSE;
	sd_byCurrentCdState        = get_cd();				// 0 => card removed; 1 => card inserted;
	sd_byCurrentWpState        = get_wp();				// 1 => write protected; 0 => not write protected
	sd_byPreviousCdState       = sd_byCurrentCdState;
	sd_byPreviousWpState       = sd_byCurrentWpState;
	sd_wCardInitTries          = 0;
	g_dwSdCardMaxPresenceCount = 10000;
	g_dwSdCardPresenceCount    = 0;
	
	IdentifySdCard();
}
