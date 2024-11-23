#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "defines.h"
#include "file.h"
#include "system.h"
#include "crc.h"
#include "fdc.h"
#include "sd_core.h"

// #define ENABLE_LOGGING 1
// #pragma GCC optimize ("Og")

////////////////////////////////////////////////////////////////////////////////////
/*

For JV1 and JV3 format information see https://www.tim-mann.org/trs80/dskspec.html

For DMK format see http://cpmarchives.classiccmp.org/trs80/mirrors/www.discover-net.net/~dmkeil/coco/cocotech.htm#Technical-DMK-disks

*/
////////////////////////////////////////////////////////////////////////////////////
/*

DMK file format

Disk Header:

The first 16-bytes of the file is the header and defines the format of the virtual drive.

Byte  Description
0     Write Protect: 0xFF - drive is write protected; 0x00 - drive is not write protected;
1     Number of tracks
2&3   Track length = (Header[3] << 8) + Header[2];
4     Virtual disk options flags
      Bit-0: NA
      Bit-1: NA
      Bit-2: NA
      Bit-3: NA
      Bit-4: if set indicates it is a single sided diskette; if not set it is a double sided diskette;
      Bit-5: NA
      Bit-6: if set indicates it is a single density dikette; if not set it is a double density diskette;
      Bit-7: if set then the density of the disk it to be ignored.
5-11  Reserved for future use
12-15 0x00, 0x00, 0x00, 0x00 - for virtual diskette navive format
      0x12, 0x34, 0x56, 0x78 - if virtual disk is a REAL disk specification file

Track Data:

	Following the header is the data for each track.  The size of each track (in bytes) is
	specified by bytes 2 and 3 of the disk header.

	Each track has a 128 (0x80) byte header which contains an offset to each IDAM in the track.
	This is created during format and should NEVER require modification. The actual track data
	follows this header and can be viewed with a hex editor showing the raw data on the track.
	Modification should not be done as each IDAM and sector has a CRC, this is just like a real
	disk, and modifying the sector data without updating the CRC value will cause CRC errors when
	accessing the virtual disk within the emulator.  

	The actual track data follows the header and can be viewed with a hex editor showing the raw
	data on the track. If the virtual disk doesn't have bits 6 or 7 set of byte 4 of the disk header
	then each single density data byte is written twice, this includes IDAMs and CRCs (the CRCs are
	calculated as if only 1 byte was written however). The IDAM and sector data each have CRCs, this
	is just like on a real disk.

Track header:

	Each side of each track has a 128 (80H) byte header which contains an offset pointer to each
	IDAM in the track. This allows a maximum of 64 sector IDAMs/track. This is more than twice
	what an 8 inch disk would require and 3.5 times that of a normal TRS-80 5 inch DD disk. This
	should more than enough for any protected disk also.

The IDAM pointers MUST adhere to the following rules.

  Each pointer is a 2 byte offset to the 0xFE byte of the IDAM header.
  In double byte single density the pointer is to the first 0xFE.
	
  The IDAM value/offset includes the 128 byte header. For example, an IDAM 10h
  bytes into the track would have a pointer of 90h, 10h+80h=90h.
	
  The IDAM values/offsets MUST be in ascending order with no unused or bad pointers.
	
  If all the entries are not used the header is terminated with a 0x0000 entry.
  Unused IDAM entries must also be zero filled..
	
  Any IDAMs overwritten during a sector write command should have their entry
  removed from the header and all other pointer entries shifted to fill in.
	
  The IDAM pointers are created during the track write command (format). A completed
  track write MUST remove all previous IDAM pointers. A partial track write (aborted
  with the forced interrupt command) MUST have it's previous pointers that were not
  overwritten added to the new IDAM pointers.
	
  The IDAM value/offset bytes are stored in reverse order (LSB/MSB).

	Each IDAM pointer has two flags:

		- Bit 15 is set if the sector is double density.
		- Bit 14 is currently undefined.

	These bits must be masked to get the actual sector offset. For example, an offset to an
	IDAM at byte 0x90 would be 0x0090 if single density and 0x8090 if double density.

Track data:

	The actual track data follows the 128 byte IDAM header and can be viewed with a hex editor
	showing the raw data on the track. If the virtual disk doesn't have bits 6 or 7 set of byte
	4 of the disk header then each single density data byte is written twice, this includes
	IDAMs and CRCs (the CRCs are calculated as if only 1 byte was written however). The IDAM
	data block and sector data blocks each have CRCs, this is just like on a real disk.

	The IDAM starts with 0xFE, but that's preceded by three 0xA1 bytes (in double-density).
	The CRC starts at the first 0xA1. Again for the data part, the 0xFB is preceded by three
	0xA1, and the CRC starts at the first 0xA1.

	In single-density sectors (maybe only in mixed-density disks?) the three preceding 0xA1
	bytes are missing and should be left out of the CRC altogether. This applies to both
	IDAM and DAM (sector Data Address Mark).

	In single denisty sectors the preceding 0xA1 are not present. If the disk is marked
	as double denisty the bytes will be doubled.  Thus every other byte should be discarded.
	This include in the CRC calculations.

IDAM/Sector header:

	SectorOffset = IDAM & 3FFF;

	For Double denisty

		bySectorData[SectorOffset-3] should be 0xA1
		bySectorData[SectorOffset-2] should be 0xA1
		bySectorData[SectorOffset-1] should be 0xA1
		bySectorData[SectorOffset]   should be 0xFE
		bySectorData[SectorOffset+1] is track address (should be the same as the nTrack parameter)
		bySectorData[SectorOffset+2] side number      (should be the same as the nSide parameter)
		bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
		bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	For Single Density

		bySectorData[SectorOffset]   should be 0xFE
		bySectorData[SectorOffset+1] is track address (should be the same as the nTrack parameter)
		bySectorData[SectorOffset+2] side number      (should be the same as the nSide parameter)
		bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
		bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

DAM marker values:

			Single Density		Double Density
	--------------------------------------------
	0xFB	Normal data			Normal data
	0xFA	User-defined		Invalid
	0xF9	User-defines		Invalid
	0xF8	Deleted data		Deleted data

*/

/*
	RS Double Density Adapter

	Function selection is perfomed via writes to the sector register (0x37EE)

	Value		Function
	--------------------------------
    40-5F       Select side 0 (is not routed to drive, thus has no effect)
    60-7F       Select side 1
    80-9F       Set double-density mode
    A0-BF       Set single-density mode
    C0-DF       Disable precomp
    E0-FF       Enable precomp
*/

////////////////////////////////////////////////////////////////////////////////////

static char* g_pszVersion = {"0.1.0"};

static FdcType       g_FDC;
static FdcDriveType  g_dtDives[MAX_DRIVES];
static TrackType     g_tdTrack;
static SectorType    g_stSector;

static char          g_szBootConfig[80];

static BufferType    g_bFdcRequest;
static BufferType    g_bFdcResponse;

static DIR           g_dj;				// Directory object
static FILINFO       g_fno;				// File information

static char          g_szFindFilter[80];

#define FIND_MAX_SIZE 100

static FILINFO g_fiFindResults[FIND_MAX_SIZE];
static int     g_nFindIndex;
static int     g_nFindCount;

static BYTE    g_byTrackBuffer[MAX_TRACK_SIZE];

static uint8_t  g_byMotorWasOn;
static uint64_t g_nTimeNow;
static uint64_t g_nPrevTime;
static uint32_t g_dwRotationTime;
static uint32_t g_dwIndexTime;

static byte     byCommandTypes[] = {1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 3, 3};

//-----------------------------------------------------------------------------

volatile BYTE    g_byIntrRequest;		// controls the INTRQ output pin.  Which simulates an open drain output that when set indicates the completion
										// of any command and is reset when the computer reads or writes to/from the DR.
										//
										// when 1 => command has been completed;
										//      0 => command can be written or that a command is in progress;
										//
										// when enabled via the corresponding bit of byNmiMaskReg the NMI output is the inverted state of byIntrReq

volatile int32_t g_nRotationCount;
volatile DWORD   g_dwMotorOnTimer;
volatile uint8_t g_byBootConfigModified;

//-----------------------------------------------------------------------------
int __not_in_flash_func(FdcGetDriveIndex)(int nDriveSel)
{
	if (nDriveSel & 0x01)
	{
		return 0;
	}
	else if (nDriveSel & 0x02)
	{
		return 1;
	}
	else if (nDriveSel & 0x04)
	{
		return 2;
	}
	
	return -1;
}

//-----------------------------------------------------------------------------
int __not_in_flash_func(FdcGetSide)(byte byDriveSel)
{
	if (byDriveSel & 0x08) // Model I side select
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

////////////////////////////////////////////////////////////////////////////////////
// For Command Type I and IV
//  S7 - 1 = drive is not ready
//  S6 - 1 = media is write protected
//  S5 - 1 = head is loaded and engaged
//  S4 - 1 = seek error
//  S3 - 1 = CRC error
//  S2 - 1 = head is positioned over track zero
//  S1 - 1 = index mark detected (set once per rotation)
//  S0 - 1 = busy, command in progress
//
// For Command Type II and III
//  S7 - 1 = drive is not ready
//  S6 - x = not used on read; 1 on write and media is write protected;
//  S5 - x = on read indicates record type code, 1 = Deleted Mark; 0 = Data Mark;
//           on write it is set if there is a write fault;
//  S4 - 1 = record not found (desired track, sector or side not found)
//  S3 - 1 = CRC error
//  S2 - 1 = lost data, indicates computer did not respond to a DRQ in one byte time
//  S1 - x = copy of the DRQ output
//       1 = DR is full on read operation or empty on write operation
//  S0 - 1 = busy, command in progress
//
void __not_in_flash_func(FdcUpdateStatus)(void)
{
	BYTE byStatus = 0;
	int  nDrive;

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	if ((nDrive < 0) || (g_dtDives[nDrive].f == NULL))
	{
		byStatus = F_NOTREADY | F_HEADLOAD;
	}
	else if ((g_FDC.byCommandType == 1) || // Restore, Seek, Step, Step In, Step Out
           (g_FDC.byCommandType == 4))   // Force Interrupt
	{
		byStatus = 0;
		
		// S0 (BUSY)
		if (g_FDC.status.byBusy)
		{
			byStatus |= F_BUSY;
		}
		
		// S1 (INDEX) default to 0
		if (g_FDC.status.byIndex)
		{
			byStatus |= F_INDEX;
		}

		// S2 (TRACK 0) default to 0
		if (g_FDC.byTrack == 0)
		{
			byStatus |= F_TRACK0; // set TRACK 0 status flag
		}

		// S3 (CRC ERROR) default to 0
		if (g_FDC.status.byCrcError)
		{
			byStatus |= F_CRCERR;
		}

		// S4 (SEEK ERROR) default to 0
		if (g_FDC.status.bySeekError)
		{
			byStatus |= F_SEEKERR;
		}
		
		if (g_FDC.status.byHeadLoaded)
		{
			byStatus |= F_HEADLOAD;
		}

		// S6 (PROTECTED) default to 0
		if (g_FDC.status.byProtected || (g_dtDives[nDrive].nDriveFormat == eHFE))
		{
			byStatus |= 0x40;
		}
		
		// S7 (NOT READY) default to 0
		if (g_FDC.status.byNotReady)
		{
			byStatus |= F_NOTREADY;
		}
	}
	else if ((g_FDC.byCommandType == 2) ||	// Read Sector, Write Sector
			 (g_FDC.byCommandType == 3))	// Read Address, Read Track, Write Track
	{
		byStatus = 0;
		
		// S0 (BUSY)
		if (g_FDC.status.byBusy)
		{
			byStatus |= F_BUSY;
		}
	
		// S1 (DATA REQUEST)     default to 0
		if (g_FDC.status.byDataRequest)
		{
			byStatus |= F_DRQ;
		}

		// S2 (LOST DATA)        default to 0
		if (g_FDC.status.byDataLost)
		{
			byStatus |= F_LOSTDATA;
		}
		
		// S3 (CRC ERROR)        default to 0
		if (g_FDC.status.byCrcError)
		{
			byStatus |= F_CRCERR;
		}
		
		// S4 (RECORD NOT FOUND) default to 0
		if (g_FDC.status.byNotFound)
		{
			byStatus |= F_NOTFOUND;
		}
	
		// S5 (RECORD TYPE) default to 0
		// S6 (PROTECTED) default to 0
		switch (g_FDC.status.byRecordType)
		{
			case 0xFB:
				byStatus &= ~F_DELETED;
				byStatus &= ~F_PROTECT;
				break;

			case 0xFA:
				byStatus |= F_DELETED;
				byStatus &= ~F_PROTECT;
				break;

			case 0xF9:
				byStatus &= ~F_DELETED;
				byStatus |= F_PROTECT;
				break;

			case 0xF8:
				byStatus |= F_DELETED;
				byStatus |= F_PROTECT;
				break;
		}

		// S7 (NOT READY) default to 0
		if (g_FDC.status.byNotReady)
		{
			byStatus |= F_NOTREADY;
		}
	}
	else // Force Interrupt
	{
		byStatus = 0;
	}
	
	g_FDC.byStatus = byStatus;
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(FdcSetFlag)(byte flag)
{
	switch (flag)
	{
		case eBusy:
			g_FDC.status.byBusy = 1;
			break;

		case eIndex:
			g_FDC.status.byIndex = 1;
			break;

		case eDataLost:
			g_FDC.status.byDataLost = 1;
			break;

		case eCrcError:
			g_FDC.status.byCrcError = 1;
			break;

		case eSeekError:
			g_FDC.status.bySeekError = 1;
			break;

		case eNotFound:
			g_FDC.status.byNotFound = 1;
			break;

		case eProtected:
			g_FDC.status.byProtected = 1;
			break;

		case eNotReady:
			g_FDC.status.byNotReady = 1;
			break;

		case eRecordType:
			g_FDC.status.byRecordType = 1;
			break;

		case eDataRequest:
			g_FDC.status.byDataRequest = 1;
			break;

		case eHeadLoaded:
			g_FDC.status.byHeadLoaded = 1;
			break;
	}

	FdcUpdateStatus();
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(FdcClrFlag)(byte flag)
{
	switch (flag)
	{
		case eBusy:
			g_FDC.status.byBusy = 0;
			break;

		case eIndex:
			g_FDC.status.byIndex = 0;
			break;

		case eDataLost:
			g_FDC.status.byDataLost = 0;
			break;

		case eCrcError:
			g_FDC.status.byCrcError = 0;
			break;

		case eSeekError:
			g_FDC.status.bySeekError = 0;
			break;

		case eNotFound:
			g_FDC.status.byNotFound = 0;
			break;

		case eProtected:
			g_FDC.status.byProtected = 0;
			break;

		case eNotReady:
			g_FDC.status.byNotReady = 0;
			break;

		case eRecordType:
			g_FDC.status.byRecordType = 0;
			break;

		case eDataRequest:
			g_FDC.status.byDataRequest = 0;
			break;

		case eHeadLoaded:
			g_FDC.status.byHeadLoaded = 0;
			break;
	}

	FdcUpdateStatus();
}

//-----------------------------------------------------------------------------
void FdcSetRecordType(byte byType)
{
	g_FDC.status.byRecordType = byType;
	FdcUpdateStatus();
}

//-----------------------------------------------------------------------------
int FdcGetTrackOffset(int nDrive, int nSide, int nTrack)
{
	int nOffset;
	
	nOffset = (nTrack * g_dtDives[nDrive].dmk.byNumSides + nSide) * g_dtDives[nDrive].dmk.wTrackLength + 16;

	return nOffset;
}	

//-----------------------------------------------------------------------------
// calculates the index of the ID Address Mark for the specified physical sector.
//
// returns the index of the 0xFE byte in the sector byte sequence 0xA1, 0xA1, 0xA1, 0xFE
// in the g_tdTrack.byTrackData[] address
//
WORD FdcGetIDAM(int nSector)
{
	BYTE* pby;
	WORD  wIDAM;

	// get IDAM pointer for the specified track
	pby = g_tdTrack.byTrackData + nSector * 2;

	// get IDAM value for the specified track
	wIDAM = (*(pby+1) << 8) + *pby;

	return wIDAM;
}

//-----------------------------------------------------------------------------
// determines the index of the Data Address Mark for the specified logical sector.
//
// returns the index of the 0xFE byte in the sector byte sequence 0xA1, 0xA1, 0xA1, 0xFE
// in the g_tdTrack.byTrackData[] address for the specified sector.
//
int FdcGetSectorIDAM_Offset(int nSide, int nTrack, int nSector)
{
	BYTE* pby;
	WORD  wIDAM;
	int   i, nOffset;

	for (i = 0; i < 0x80; ++i)
	{
		wIDAM   = FdcGetIDAM(i);
		nOffset = wIDAM & 0x3FFF;

		// bySectorData[nOffset-3] should be 0xA1
		// bySectorData[nOffset-2] should be 0xA1
		// bySectorData[nOffset-1] should be 0xA1
		// bySectorData[nOffset]   should be 0xFE
		// bySectorData[nOffset+1] is track address (should be the same as the nTrack parameter)
		// bySectorData[nOffset+2] side number		(should be the same as the nSide parameter)
		// bySectorData[nOffset+3] sector number    (should be the same as the nSector parameter)
		// bySectorData[nOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

		pby = g_tdTrack.byTrackData + nOffset;

		if ((*pby == 0xFE) && (*(pby+1) == 0xFE)) // then double byte data
		{
			if ((*(pby+2) == nTrack) && (*(pby+4) == nSide) && (*(pby+6) == nSector))
			{
				return nOffset;
			}
		}
		else if (wIDAM & 0x8000)
		{
			if ((*(pby+1) == nTrack) && (*(pby+2) == nSide) && (*(pby+3) == nSector))
			{
				return nOffset;
			}
		}
		else
		{
			if ((*(pby+1) == nTrack) && (*(pby+2) == nSide) && (*(pby+3) == nSector))
			{
				return nOffset;
			}
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// returns TRUE if the byte sequence starting at pbt is one of the following
//					- 0xA1, 0xA1, 0xA1, 0xFB
//					- 0xA1, 0xA1, 0xA1, 0xF8
//		   FALSE is it is not
//
BYTE FdcIsDataStartPatern(BYTE* pby)
{
	if (*pby != 0xA1)
	{
		return FALSE;
	}
	
	++pby;
	
	if (*pby != 0xA1)
	{
		return FALSE;
	}
	
	++pby;
	
	if (*pby != 0xA1)
	{
		return FALSE;
	}
	
	++pby;

	if ((*pby == 0xFB) || (*pby == 0xF8))
	{
		return TRUE;
	}

	return FALSE;
}

//-----------------------------------------------------------------------------
int FdcGetDataSize(TrackType* ptdTrack, int nIDAM)
{
	BYTE* pby;
	int   nDataOffset = FdcGetIDAM_Offset(nIDAM);
	
	if (nIDAM < 0)
	{
		return 1;
	}

	pby = ptdTrack->byTrackData+nDataOffset;

	if ((*pby == 0xFE) && (*(pby+1) == 0xFE))
	{
		return 2;
	}

	return 1;
}

//-----------------------------------------------------------------------------
int FdcGetDAM_Offset(TrackType* ptdTrack, int nIDAM, int nDataSize)
{
	BYTE* pby;
	int   nDataOffset = FdcGetIDAM_Offset(nIDAM) + 7 * nDataSize;
	
	if (nIDAM < 0)
	{
		return -1;
	}

	if ((nDataSize == 1) && (ptdTrack->byDensity == eDD)) // double density
	{
		// locate the byte sequence 0xA1, 0xA1, 0xA1, 0xFB/0xF8
		while (nDataOffset < ptdTrack->nTrackSize)
		{
			if (FdcIsDataStartPatern(g_tdTrack.byTrackData+nDataOffset))
			{
				return nDataOffset;
			}
			else
			{
				++nDataOffset;
			}
		}
	}
	else // single density
	{
		while (nDataOffset < ptdTrack->nTrackSize)
		{
			pby = g_tdTrack.byTrackData+nDataOffset;

			if ((*pby == 0xFA) || (*pby == 0xFB) || (*pby == 0xF8) || (*pby == 0xF9))
			{
				return nDataOffset;
			}
			else
			{
				nDataOffset += nDataSize;
			}
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
void FdcFillSectorOffset(TrackType* ptdTrack)
{
	int i;
	
	for (i = 0; i < 0x80; ++i)
	{
		ptdTrack->nIDAM[i]     = FdcGetSectorIDAM_Offset(ptdTrack->nSide, ptdTrack->nTrack, i);
		ptdTrack->nDataSize[i] = FdcGetDataSize(ptdTrack, ptdTrack->nIDAM[i]);
		ptdTrack->nDAM[i]      = FdcGetDAM_Offset(ptdTrack, ptdTrack->nIDAM[i], ptdTrack->nDataSize[i]);
	}
}

//-----------------------------------------------------------------------------
void FdcReadDmkTrack(int nDrive, int nSide, int nTrack)
{
	int nTrackOffset;
	
	g_tdTrack.nType = eDMK;

	// check if specified track is already in memory
	if ((g_tdTrack.nDrive == nDrive) && (g_tdTrack.nSide == nSide) && (g_tdTrack.nTrack == nTrack))
	{
		return;
	}

	if ((nDrive < 0) || (nDrive >= MAX_DRIVES) || (g_dtDives[nDrive].f == NULL))
	{
		return;
	}

	nTrackOffset = FdcGetTrackOffset(nDrive, nSide, nTrack);

	FileSeek(g_dtDives[nDrive].f, nTrackOffset);
	FileRead(g_dtDives[nDrive].f, g_tdTrack.byTrackData, g_dtDives[nDrive].dmk.wTrackLength);

	g_tdTrack.byDensity  = g_dtDives[nDrive].dmk.byDensity;
	g_tdTrack.nDrive     = nDrive;
	g_tdTrack.nSide      = nSide;
	g_tdTrack.nTrack     = nTrack;
	g_tdTrack.nTrackSize = g_dtDives[nDrive].dmk.wTrackLength;

	FdcFillSectorOffset(&g_tdTrack);

	// For Double denisty
	// 	bySectorData[SectorOffset-3] should be 0xA1
	// 	bySectorData[SectorOffset-2] should be 0xA1
	// 	bySectorData[SectorOffset-1] should be 0xA1
	// 	bySectorData[SectorOffset]   should be 0xFE
	// 	bySectorData[SectorOffset+1] is track address (should be the same as the nTrack parameter)
	// 	bySectorData[SectorOffset+2] side number      (should be the same as the nSide parameter)
	// 	bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
	// 	bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	// For Single Density
	// 	bySectorData[SectorOffset]   should be 0xFE
	// 	bySectorData[SectorOffset+1] is track address (should be the same as the nTrack parameter)
	// 	bySectorData[SectorOffset+2] side number      (should be the same as the nSide parameter)
	// 	bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
	// 	bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	WORD  wIDAM   = FdcGetIDAM(0);
	int   nOffset = wIDAM & 0x3FFF;
	BYTE* pby = g_tdTrack.byTrackData + nOffset;

	if (*(pby-1) == 0xA1)
	{
		g_tdTrack.byDensity = eDD;
	}
	else
	{
		g_tdTrack.byDensity = eSD;
	}
}
/*
//-----------------------------------------------------------------------------
void FdcReadHfeTrack(int nDrive, int nSide, int nTrack)
{
	g_tdTrack.nType = eHFE;

	// check if specified track is already in memory
	if ((g_tdTrack.nDrive == nDrive) && (g_tdTrack.nSide == nSide) && (g_tdTrack.nTrack == nTrack))
	{
		return;
	}

	if ((nDrive < 0) || (nDrive >= MAX_DRIVES) || (g_dtDives[nDrive].f == NULL))
	{
		return;
	}

	LoadHfeTrack(g_dtDives[nDrive].f, nTrack, nSide, &g_dtDives[nDrive].hfe, &g_tdTrack, g_tdTrack.byTrackData, sizeof(g_tdTrack.byTrackData));

	g_tdTrack.nDrive = nDrive;
	g_tdTrack.nSide  = nSide;
	g_tdTrack.nTrack = nTrack;
}
*/
//-----------------------------------------------------------------------------
void FdcReadTrack(int nDrive, int nSide, int nTrack)
{
	switch (g_dtDives[nDrive].nDriveFormat)
	{
		case eDMK:
			FdcReadDmkTrack(nDrive, nSide, nTrack);
			break;

//		case eHFE:
//			FdcReadHfeTrack(nDrive, nSide, nTrack);
//			break;
	}
}

//-----------------------------------------------------------------------------
int FindSectorIndex(int nSector, TrackType* ptrack)
{
	int i, nOffset;

	// locate sector
	for (i = 0; i < MAX_SECTORS_PER_TRACK; ++i)
	{
		nOffset = FdcGetIDAM_Offset(ptrack->nIDAM[i])+6;

		if ((nOffset < sizeof(ptrack->byTrackData)) && (ptrack->byTrackData[nOffset] == nSector))
		{
			return i;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------
WORD FdcGetDmkSectorCRC(int nDrive, int nDataOffset, int nDensityAdjust, int nDataSize)
{
	WORD wCRC16;

	wCRC16  = g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+1*nDataSize] << 8;
	wCRC16 += g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+2*nDataSize];

	return wCRC16;
}

//-----------------------------------------------------------------------------
int FdcReadDmkSector1771(int nDriveSel, int nSide, int nTrack, int nSector)
{
	BYTE* pby;
	WORD  wCalcCRC16;
	int   nDrive, nDataOffset, nDensityAdjust;
	int   nDataSize = 1;
	int   ret = FDC_READ_SECTOR_SUCCESS;

	g_FDC.nDataSize = 1;

	nDrive = FdcGetDriveIndex(nDriveSel);
	
	if (nDrive < 0)
	{
		return FDC_INVALID_DRIVE;
	}

	FdcReadTrack(nDrive, nSide, nTrack);

	g_tdTrack.nFileOffset = FdcGetTrackOffset(nDrive, nSide, nTrack);

	int nOffset = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector]);

	// get pointer to start of sector data
	pby = g_tdTrack.byTrackData + nOffset;

	if ((*pby == 0xFE) && (*(pby+1) == 0xFE))
	{
		nDataSize = 2;
	}

	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-3] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-2] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-1] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+0] should be 0xFE
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+1] is track address (should be the same as the nTrack parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+2] side number		 (should be the same as the nSide parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+3] sector number    (should be the same as the nSector parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	g_FDC.nDataSize = nDataSize;

	if (g_FDC.byCurCommand & 0x08) // IBM format
	{
		g_stSector.nSectorSize = 128 << *(pby+4*nDataSize);
	}
	else // Non-IBM format
	{
		g_stSector.nSectorSize = 16; // 16 << *(pby+4*nDataSize);
	}

	g_dtDives[nDrive].dmk.nSectorSize = g_stSector.nSectorSize;

	// g_FDC.byTrackData[g_FDC.nSectorOffset+5..6] CRC (calculation starts with the three 0xA1/0xF5 bytes preceeding the 0xFE)
	FdcClrFlag(eCrcError);

	if (FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector]) <= 0)
	{
		g_stSector.nSectorDataOffset = 0; // then there is a problem and we will let the Z80 deal with it
		FdcClrFlag(eRecordType);
		FdcSetFlag(eNotFound);
		return FDC_SECTOR_NOT_FOUND;
	}

	if (nDataSize == 2)
	{
		nDensityAdjust = 0;
		wCalcCRC16 = Calculate_CRC_CCITT(pby, 5, nDataSize);
	}
	else if (g_tdTrack.byDensity == eDD) // double density
	{
		nDensityAdjust = 3;
		wCalcCRC16 = Calculate_CRC_CCITT(pby-3, 8, nDataSize);
	}
	else
	{
		nDensityAdjust = 0;
		wCalcCRC16 = Calculate_CRC_CCITT(pby, 5, nDataSize);
	}

	WORD wCRC16  = 0;
	int  nIndex1 = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+5*nDataSize;
	int  nIndex2 = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+6*nDataSize;

	if ((nIndex1 < sizeof(g_tdTrack.byTrackData)) && (nIndex2 < sizeof(g_tdTrack.byTrackData)))
	{
		wCRC16 = (g_tdTrack.byTrackData[nIndex1] << 8) + g_tdTrack.byTrackData[nIndex2];
	}

	if (wCalcCRC16 != wCRC16)
	{
		FdcSetFlag(eCrcError);
		ret = FDC_CRC_ERROR;
	}

	nDataOffset = g_tdTrack.nDAM[nSector];	// offset to first bytes of the sector data mark sequence (0xA1, 0xA1, 0xA1, 0xFB/0xF8)
											//  - 0xFB (regular data); or
											//  - 0xF8 (deleted data)
											// actual data starts after the 0xFB/0xF8 byte
	if (nDataOffset < 0)
	{
		g_stSector.nSectorDataOffset = 0; // then there is a problem and we will let the Z80 deal with it
		FdcClrFlag(eRecordType);
		FdcSetFlag(eNotFound);
		return FDC_SECTOR_NOT_FOUND;
	}

	// for double density drives nDataOffset is the index of the first 0xA1 byte in the 0xA1, 0xA1, 0xA1, 0xFB/0xF8 sequence
	//
	// for single density 0xA1, 0xA1 and 0xA1 are not present, CRC starts at the data mark (0xFB/0xF8)

	g_FDC.byRecordMark           = g_tdTrack.byTrackData[nDataOffset+nDensityAdjust*nDataSize];
	g_stSector.nSectorDataOffset = nDataOffset + (nDensityAdjust + 1) * nDataSize;
	FdcClrFlag(eNotFound);
	FdcSetRecordType(0xFB);	// will get set to g_FDC.byRecordMark after a few status reads

	// perform a CRC on the sector data (including preceeding 4 bytes) and validate
	wCalcCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+1, nDataSize);

	if (nDataSize == 2)
	{
		int nCrcOffset = g_dtDives[nDrive].dmk.nSectorSize * nDataSize;
		wCRC16  = g_tdTrack.byTrackData[nDataOffset+nCrcOffset+2] << 8;
		wCRC16 += g_tdTrack.byTrackData[nDataOffset+nCrcOffset+4];
	}
	else if (g_tdTrack.byDensity == eDD) // double density
	{
		wCRC16  = g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+1] << 8;
		wCRC16 += g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+2];
	}
	else
	{
		wCRC16  = g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+1] << 8;
		wCRC16 += g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+2];
	}

	if (wCalcCRC16 != wCRC16)
	{
		FdcSetFlag(eCrcError);
		return FDC_CRC_ERROR;
	}

	return ret;
}

//-----------------------------------------------------------------------------
void FdcReadDmkSector1791(int nDriveSel, int nSide, int nTrack, int nSector)
{
	BYTE* pby;
	WORD  wCalcCRC16;
	int   nDrive, nDataOffset;

	nDrive = FdcGetDriveIndex(nDriveSel);
	
	if (nDrive < 0)
	{
		return;
	}

	FdcReadTrack(nDrive, nSide, nTrack);

	g_tdTrack.nFileOffset = FdcGetTrackOffset(nDrive, nSide, nTrack);

	// get pointer to start of sector data
	int nOffset = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector]);

	pby = g_tdTrack.byTrackData + nOffset;

	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-3] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-2] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-1] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+0] should be 0xFE
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+1] is track address (should be the same as the nTrack parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+2] side number		 (should be the same as the nSide parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+3] sector number    (should be the same as the nSector parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	g_stSector.nSectorSize = 128 << *(pby+4);
	g_dtDives[nDrive].dmk.nSectorSize = g_stSector.nSectorSize;

	// g_FDC.byTrackData[g_FDC.nSectorOffset+5..6] CRC (calculation starts with the three 0xA1/0xF5 bytes preceeding the 0xFE)
	FdcClrFlag(eCrcError);

	if (FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector]) <= 0)
	{
		g_stSector.nSectorDataOffset = 0; // then there is a problem and we will let the Z80 deal with it
		FdcClrFlag(eRecordType);
		FdcSetFlag(eNotFound);
		return;
	}

	wCalcCRC16 = Calculate_CRC_CCITT(pby-3, 8, 1);
	
	WORD wCRC16  = 0;
	int  nIndex1 = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+5;
	int  nIndex2 = FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+6;

	if ((nIndex1 < sizeof(g_tdTrack.byTrackData)) && (nIndex2 < sizeof(g_tdTrack.byTrackData)))
	{
		wCRC16 = (g_tdTrack.byTrackData[nIndex1] << 8) + g_tdTrack.byTrackData[nIndex2];
	}

	if (wCalcCRC16 != wCRC16)
	{
		FdcSetFlag(eCrcError);
	}
	
	nDataOffset = g_tdTrack.nDAM[nSector];	// offset to first bytes of the sector data mark sequence (0xA1, 0xA1, 0xA1, 0xFB/0xF8)
											//  - 0xFB (regular data); or
											//  - 0xF8 (deleted data)
											// actual data starts after the 0xFB/0xF8 byte
	if (nDataOffset < 0)
	{
		g_stSector.nSectorDataOffset = 0; // then there is a problem and we will let the Z80 deal with it
		FdcClrFlag(eRecordType);
		FdcSetFlag(eNotFound);
		return;
	}

	// nDataOffset is the index of the first 0xA1 byte in the 0xA1, 0xA1, 0xA1, 0xFB/0xF8 sequence

	g_FDC.byRecordMark           = g_tdTrack.byTrackData[nDataOffset+3];
	g_stSector.nSectorDataOffset = nDataOffset + 4;
	FdcClrFlag(eNotFound);
	FdcSetRecordType(0xFB);	// will get set to g_FDC.byRecordMark after a few status reads

	// perform a CRC on the sector data (including preceeding 4 bytes) and validate
	wCalcCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], g_dtDives[nDrive].dmk.nSectorSize+4, 1);

	wCRC16  = g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+4] << 8;
	wCRC16 += g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+5];

	if (wCalcCRC16 != wCRC16)
	{
		FdcSetFlag(eCrcError);
	}
}

//-----------------------------------------------------------------------------
void FdcReadHfeSector(int nDriveSel, int nSide, int nTrack, int nSector)
{
	int i, nDrive;

	nDrive = FdcGetDriveIndex(nDriveSel);
	
	if (nDrive < 0)
	{
		return;
	}

	FdcReadTrack(nDrive, nSide, nTrack);

	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-3] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-2] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset-1] should be 0xA1 or 0xF5
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+0] should be 0xFE
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+1] is track address (should be the same as the nTrack parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+2] side number		 (should be the same as the nSide parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+3] sector number    (should be the same as the nSector parameter)
	// g_FDC.byTrackData[nSide][g_FDC.nTrackSectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.
	// g_FDC.byTrackData[nSide][g_FDC.nSectorOffset+5..6] CRC (calculation starts with the three 0xA1/0xF5 bytes preceeding the 0xFE)
	FdcClrFlag(eCrcError);

	i = FindSectorIndex(nSector, &g_tdTrack);

	g_FDC.byRecordMark           = g_tdTrack.byTrackData[g_tdTrack.nDAM[i] + 3];
	g_stSector.nSectorDataOffset = g_tdTrack.nDAM[FindSectorIndex(nSector, &g_tdTrack)] + 4;
	
	int nOffset = FdcGetIDAM_Offset(g_tdTrack.nIDAM[i]) + 7;

	if (nOffset < sizeof(g_tdTrack.byTrackData))
	{
		g_stSector.nSectorSize = 128 << g_tdTrack.byTrackData[nOffset];
	}
	else
	{
		g_stSector.nSectorSize = 128; // ?
	}

	FdcClrFlag(eNotFound);
	FdcSetRecordType(0xFB);	// will get set to g_FDC.byRecordMark after a few status reads
}

//-----------------------------------------------------------------------------
void FdcReadSector(int nDriveSel, int nSide, int nTrack, int nSector)
{
	int nDrive;

	nDrive = FdcGetDriveIndex(nDriveSel);

	switch (g_dtDives[nDrive].nDriveFormat)
	{
		case eDMK:
			if (g_FDC.byDoublerEnable && (g_tdTrack.byDensity == eDD))
			{
				FdcReadDmkSector1791(nDriveSel, nSide, nTrack, nSector);
			}
			else
			{
				FdcReadDmkSector1771(nDriveSel, nSide, nTrack, nSector);
			}

			break;

		case eHFE:
			FdcReadHfeSector(nDriveSel, nSide, nTrack, nSector);
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcMountDmkDrive(int nDrive)
{
	if (nDrive >= MAX_DRIVES)
	{
		return;
	}

	g_dtDives[nDrive].f = FileOpen(g_dtDives[nDrive].szFileName, FA_READ | FA_WRITE);

	if (g_dtDives[nDrive].f == NULL)
	{
		return;
	}

	g_dtDives[nDrive].nDriveFormat = eDMK;

	FileRead(g_dtDives[nDrive].f, g_dtDives[nDrive].dmk.byDmkDiskHeader, sizeof(g_dtDives[nDrive].dmk.byDmkDiskHeader));

	g_dtDives[nDrive].dmk.byWriteProtected = g_dtDives[nDrive].dmk.byDmkDiskHeader[0];
	g_dtDives[nDrive].byNumTracks          = g_dtDives[nDrive].dmk.byDmkDiskHeader[1];
	g_dtDives[nDrive].dmk.wTrackLength     = (g_dtDives[nDrive].dmk.byDmkDiskHeader[3] << 8) + g_dtDives[nDrive].dmk.byDmkDiskHeader[2];

	if (g_dtDives[nDrive].dmk.wTrackLength > MAX_TRACK_SIZE) // error (TODO: handle this gracefully)
	{
		g_dtDives[nDrive].dmk.wTrackLength = MAX_TRACK_SIZE - 1;
		return;
	}
	
	// determine number of sides for disk
	if ((g_dtDives[nDrive].dmk.byDmkDiskHeader[4] & 0x10) != 0)
	{
		g_dtDives[nDrive].dmk.byNumSides = 1;
	}
	else
	{
		g_dtDives[nDrive].dmk.byNumSides = 2;
	}

	// determine disk density
	if ((g_dtDives[nDrive].dmk.byDmkDiskHeader[4] & 0x40) != 0)
	{
		g_dtDives[nDrive].dmk.byDensity = eSD; // Single Density
	}
	else
	{
		g_dtDives[nDrive].dmk.byDensity = eDD; // Double Density
	}

	if ((g_dtDives[nDrive].dmk.byDmkDiskHeader[4] & 0x80) != 0) // then ignore denity setting and just use SD
	{
		g_dtDives[nDrive].dmk.byDensity = eSD; // Single Density
	}
	
	// bytes 0x05 - 0x0B are reserved
	
	// bytes 0x0B - 0x0F are zero for virtual disks; and 0x12345678 for real disks;

}

//-----------------------------------------------------------------------------
void FdcMountHfeDrive(int nDrive)
{
	if (nDrive >= MAX_DRIVES)
	{
		return;
	}

	g_dtDives[nDrive].f = FileOpen(g_dtDives[nDrive].szFileName, FA_READ | FA_WRITE);

	if (g_dtDives[nDrive].f == NULL)
	{
		return;
	}

	g_dtDives[nDrive].nDriveFormat = eHFE;

	FileRead(g_dtDives[nDrive].f, (BYTE*)&g_dtDives[nDrive].hfe.header, sizeof(g_dtDives[nDrive].hfe.header));
	FileSeek(g_dtDives[nDrive].f, g_dtDives[nDrive].hfe.header.track_list_offset*0x200);
	FileRead(g_dtDives[nDrive].f, (BYTE*)&g_dtDives[nDrive].hfe.trackLUT, sizeof(g_dtDives[nDrive].hfe.trackLUT));

	g_dtDives[nDrive].byNumTracks = g_dtDives[nDrive].hfe.header.number_of_tracks;
}

//-----------------------------------------------------------------------------
void FdcMountDrive(int nDrive)
{
	g_dtDives[nDrive].nDriveFormat = eUnknown;

	if (stristr(g_dtDives[nDrive].szFileName, (char*)".dmk") != NULL)
	{
		FdcMountDmkDrive(nDrive);
	}
	else if (stristr(g_dtDives[nDrive].szFileName, (char*)".hfe") != NULL)
	{
		FdcMountHfeDrive(nDrive);
	}
}

////////////////////////////////////////////////////////////////////////////////////
void FdcProcessConfigEntry(char szLabel[], char* psz)
{
	if ((strcmp(szLabel, "DRIVE0") == 0) && (MAX_DRIVES > 0))
	{
		CopyString(psz, g_dtDives[0].szFileName, sizeof(g_dtDives[0].szFileName)-2);
	}
	else if ((strcmp(szLabel, "DRIVE1") == 0) && (MAX_DRIVES > 1))
	{
		CopyString(psz, g_dtDives[1].szFileName, sizeof(g_dtDives[1].szFileName)-2);
	}
	else if ((strcmp(szLabel, "DRIVE2") == 0) && (MAX_DRIVES > 2))
	{
		CopyString(psz, g_dtDives[2].szFileName, sizeof(g_dtDives[2].szFileName)-2);
	}
	else if ((strcmp(szLabel, "DRIVE3") == 0) && (MAX_DRIVES > 3))
	{
		CopyString(psz, g_dtDives[3].szFileName, sizeof(g_dtDives[3].szFileName)-2);
	}
}

//-----------------------------------------------------------------------------
void FdcLoadIni(void)
{
	file* f;
	char  szLine[64];
	char  szSection[16];
	char  szLabel[32];
	char* psz;
	int   nLen;

	g_byBootConfigModified = FALSE;
    g_szBootConfig[0] = 0;

	// read the default ini file to load on init
	f = FileOpen("boot.cfg", FA_READ);
	
	if (f == NULL)
	{
		return;
	}

	// open the ini file specified in boot.cfg
	nLen = FileReadLine(f, (BYTE*)g_szBootConfig, sizeof(g_szBootConfig)-2);
	FileClose(f);

	f = FileOpen(g_szBootConfig, FA_READ);
	
	if (f == NULL)
	{
		return;
	}
	
	nLen = FileReadLine(f, (BYTE*)szLine, sizeof(szLine)-2);
	
	while (nLen >= 0)
	{
		psz = SkipBlanks(szLine);
		
		if ((*psz != 0) && (*psz != ';')) // blank line or a comment line
		{
			StrToUpper(psz);
			psz = CopyLabelName(psz, szLabel, sizeof(szLabel)-2);
			FdcProcessConfigEntry(szLabel, psz);
		}

		nLen = FileReadLine(f, (BYTE*)szLine, 126);
	}
	
	FileClose(f);
}

//-----------------------------------------------------------------------------
void FdcInit(void)
{
	int i;

    memset(&g_bFdcRequest, 0, sizeof(g_bFdcRequest));
    memset(&g_bFdcResponse, 0, sizeof(g_bFdcResponse));
	memset(&g_FDC, 0, sizeof(g_FDC));

	FdcSetFlag(eBusy);

	g_tdTrack.nDrive = -1;
	g_tdTrack.nSide  = -1;
	g_tdTrack.nTrack = -1;

	for (i = 0; i < MAX_DRIVES; ++i)
	{
		memset(&g_dtDives[i], 0, sizeof(FdcDriveType));
	}

	FileCloseAll();
	FdcLoadIni();

	for (i = 0; i < MAX_DRIVES; ++i)
	{
		if (g_dtDives[i].szFileName[0] != 0)
		{
			FdcMountDrive(i);
		}
	}

	g_FDC.byCommandReceived = 0;
	g_FDC.byCommandReg  = 255;
	g_FDC.byCurCommand  = 255;
	g_FDC.byDriveSel    = 0x01;
	g_FDC.byCommandType = 1;

	g_nTimeNow       = time_us_64();
	g_nPrevTime      = g_nTimeNow;
	g_byMotorWasOn   = 0;
	g_dwMotorOnTimer = 0;
	g_dwRotationTime = 200000;	// 200ms
	g_dwIndexTime    = 2800;	// 2.8ms
	g_nRotationCount = 0;
}

//-----------------------------------------------------------------------------	
void __not_in_flash_func(FdcGenerateIntr)(void)
{
	g_byIntrRequest   = 1;
	g_byFdcIntrActive = true;
	g_byEnableIntr    = true;
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(FdcGenerateDRQ)(void)
{
	FdcSetFlag(eDataRequest);
}	

//-----------------------------------------------------------------------------
void FdcCloseAllFiles(void)
{
	int i;
	
	for (i = 0; i < MAX_DRIVES; ++i)
	{
		if (g_dtDives[i].f != NULL)
		{
			FileClose(g_dtDives[i].f);
			g_dtDives[i].f = NULL;
		}

		memset(&g_dtDives[i], 0, sizeof(FdcDriveType));
	}
}

//-----------------------------------------------------------------------------
void FdcReset(void)
{
	FdcCloseAllFiles();
	FdcInit();
}

//-----------------------------------------------------------------------------
// Command code 0 0 0 0 h V r1 r0
//
// h = 1 - load head at begining; 0 - unload head at begining;
// V = 1 - verify on destination track
// r1/r0 - steppeing motor rate
//
void FdcProcessRestoreCommand(void)
{
	int nDrive;
	int nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byTrack = 255;
	g_FDC.byCommandType = 1;
	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	if (g_FDC.byCommandReg & 0x08) // h == 1?
	{
		FdcSetFlag(eHeadLoaded);
	}
	else
	{
		FdcClrFlag(eHeadLoaded);
	}

	FdcReadTrack(nDrive, nSide, 0);

	FdcClrFlag(eBusy);
	g_FDC.byTrack = 0;
	FdcGenerateIntr();
}

//-----------------------------------------------------------------------------
int GetStepRate(BYTE byCommandReg)
{
	int nStepRate = 3;

	switch (byCommandReg & 0x03)
	{
		case 0:
			nStepRate = 3;
			break;

		case 1:
			nStepRate = 6;
			break;

		case 2:
			nStepRate = 10;
			break;

		case 3:
			nStepRate = 15;
			break;
	}

	return nStepRate;
}

//-----------------------------------------------------------------------------
// Command code 0 0 0 1 h V r1 r0
//
// h = 1 - load head at begining; 0 - unload head at begining;
// V = 1 - verify on destination track
// r1/r0 - steppeing motor rate
//
// seek to the track specified in the data register
//
void FdcProcessSeekCommand(void)
{
	int nTimeOut;
	int nDrive;
	int nStepRate;
	int nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 1;

	if (g_FDC.byCommandReg & 0x08) // h == 1?
	{
		FdcSetFlag(eHeadLoaded);
	}
	else
	{
		FdcClrFlag(eHeadLoaded);
	}

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);
	
	if (nDrive != g_tdTrack.nDrive)
	{
		g_tdTrack.nDrive = -1;
	}

	if (g_FDC.byData >= g_dtDives[nDrive].byNumTracks)
	{
		FdcSetFlag(eSeekError);
		FdcClrFlag(eBusy);
		g_FDC.dwStateTimer = 0;
		g_FDC.nProcessFunction = psSeek;
		return;
	}

	nStepRate = GetStepRate(g_FDC.byCommandReg);

	if (g_FDC.byTrack > g_FDC.byData)
	{
		nTimeOut = nStepRate * (g_FDC.byTrack - g_FDC.byData);
	}
	else
	{
		nTimeOut = nStepRate * (g_FDC.byData - g_FDC.byTrack);
	}

	FdcReadTrack(nDrive, nSide, g_FDC.byData);

	g_FDC.byTrack = g_FDC.byData;
	FdcClrFlag(eSeekError);
	FdcSetFlag(eBusy);
	g_FDC.dwStateTimer = 0;
	g_FDC.nProcessFunction = psSeek;
}

//-----------------------------------------------------------------------------
// Command code 0 0 1 u h V r1 r0
//
// u = 1 - update track register; 0 - do not update track register;
// h = 1 - load head at begining; 0 - unload head at begining;
// V = 1 - verify on destination track
// r1/r0 - steppeing motor rate
//
void FdcProcessStepCommand(void)
{
	int nDrive;
	int nStepRate;
	int nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 1;

	if (g_FDC.byCommandReg & 0x08) // h == 1?
	{
		FdcSetFlag(eHeadLoaded);
	}
	else
	{
		FdcClrFlag(eHeadLoaded);
	}

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	if ((g_FDC.byCurCommand & 0x04) != 0) // perform verification
	{
		// TODO: peform what ever is needed
	}
	
	if ((g_FDC.byCurCommand & 0x10) != 0) // update flag set, then update track register
	{
		if ((g_FDC.nStepDir > 0) && (g_FDC.byTrack < 255))
		{
			++g_FDC.byTrack;
		}
		else if ((g_FDC.nStepDir < 0) && (g_FDC.byTrack > 0))
		{
			--g_FDC.byTrack;
		}
	}

	nStepRate   = GetStepRate(g_FDC.byCommandReg);

	FdcReadTrack(nDrive, nSide, g_FDC.byTrack);

	FdcClrFlag(eSeekError);
	FdcClrFlag(eBusy);
	FdcGenerateIntr();
}

//-----------------------------------------------------------------------------
// Command code 0 1 0 u h V r1 r0
//
// u = 1 - update track register; 0 - do not update track register;
// h = 1 - load head at begining; 0 - unload head at begining;
// V = 1 - verify on destination track
// r1/r0 - stepping motor rate
//
void FdcProcessStepInCommand(void)
{
	BYTE byData;
	int  nDrive;
	int  nStepRate;
	int  nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 1;

	if (g_FDC.byCommandReg & 0x08) // h == 1?
	{
		FdcSetFlag(eHeadLoaded);
	}
	else
	{
		FdcClrFlag(eHeadLoaded);
	}

	g_FDC.nStepDir = 1;

	if ((g_FDC.byCurCommand & 0x04) != 0) // perform verification
	{
		// TODO: peform what ever is needed
	}

	byData = g_FDC.byTrack;

	if (byData < 255)
	{
		++byData;
	}
	
	if ((g_FDC.byCurCommand & 0x10) != 0) // is u == 1 then update track register
	{
		g_FDC.byTrack = byData;
	}

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);
	
	if (nDrive != g_tdTrack.nDrive)
	{
		g_tdTrack.nDrive = -1;
	}

	nStepRate   = GetStepRate(g_FDC.byCommandReg);

	FdcReadTrack(nDrive, nSide, byData);

	g_FDC.byTrack = byData;
	FdcClrFlag(eSeekError);
	FdcClrFlag(eBusy);
	
	FdcGenerateIntr();
}

//-----------------------------------------------------------------------------
// Command code 0 1 1 u h V r1 r0
//
// u = 1 - update track register; 0 - do not update track register;
// h = 1 - load head at begining; 0 - unload head at begining;
// V = 1 - verify on destination track
// r1/r0 - steppeing motor rate
//
void FdcProcessStepOutCommand(void)
{
	BYTE byData;
	int  nDrive;
	int  nStepRate;
	int  nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 1;

	if (g_FDC.byCommandReg & 0x08) // h == 1?
	{
		FdcSetFlag(eHeadLoaded);
	}
	else
	{
		FdcClrFlag(eHeadLoaded);
	}

	g_FDC.nStepDir = -1;

	if ((g_FDC.byCurCommand & 0x04) != 0) // perform verification
	{
		// TODO: peform what ever is needed
	}

	byData = g_FDC.byTrack;

	if (byData > 0)
	{
		--byData;
	}
	
	if ((g_FDC.byCurCommand & 0x10) != 0) // is u == 1 then update track register
	{
		g_FDC.byTrack = byData;
	}

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);
	
	if (nDrive != g_tdTrack.nDrive)
	{
		g_tdTrack.nDrive = -1;
	}

	nStepRate = GetStepRate(g_FDC.byCommandReg);

	FdcReadTrack(nDrive, nSide, byData);

	FdcGenerateIntr();
}

//-----------------------------------------------------------------------------
// WD1771 Command code 1 0 0 m b E 0 0
//
//   m  = 0 - single record read; 1 - multiple record read;
//   b  = 0 - Non-IBM format (16 to 4096 bytes); 1 - IBM format (128 to 1024 bytes);
//   E  = 0 - no delay; 1 - 15 ms delay;
//
// WD1791 Command code 1 0 0 m S E C 0
//
//   m = 0 - single record read; 1 - multiple record read;
//   S = 0 - compare for side 0; 1 - compare for side 1;
//   E = 0 - no delay; 1 - 15 ms delay;
//   C = 0 - disable side select compare; 1 - enable side select compare;
//
void FdcProcessReadSectorCommand(void)
{
	int nSide  = FdcGetSide(g_FDC.byDriveSel);
	int nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	g_FDC.byCommandType = 2;

	FdcReadSector(g_FDC.byDriveSel, nSide, g_FDC.byTrack, g_FDC.bySector);

	if (g_FDC.status.byNotFound)
	{
		FdcClrFlag(eBusy);
		return;
	}		
	
	g_FDC.dwStateTimer = 0;

	FdcClrFlag(eDataRequest);
	FdcSetFlag(eHeadLoaded);

	if (g_FDC.byCurCommand & 0x10) // read multiple
	{
		g_FDC.byMultipleRecords = 1;
	}
	else
	{
		g_FDC.byMultipleRecords = 0;
	}

	// number of byte to be transfered to the computer before
	// setting the Data Address Mark status bit (1 if Deleted Data)
	g_tdTrack.nReadSize     = g_stSector.nSectorSize;
	g_tdTrack.pbyReadPtr    = g_tdTrack.byTrackData + g_stSector.nSectorDataOffset;
	g_tdTrack.nReadCount    = g_tdTrack.nReadSize;
	g_FDC.nServiceState     = 0;
	g_FDC.nProcessFunction  = psReadSector;
	
	// Note: computer now reads the data register for each of the sector data bytes
	//       once the last data byte is transfered status bit-5 is set if the
	//       Data Address Mark corresponds to a Deleted Data Mark.
	//
	//       Actual data transfer in handle in the FdcServiceRead() function.
}

//-----------------------------------------------------------------------------
// Command code 1 0 1 m b E a1 a0
//
// m  = 0 - single record read; 1 - multiple record read;
// b  = 0 - Non-IBM format; 1 - IDM format;
// E  = 0 - no delay; 1 - 15 ms delay;
// a1/a0 = 00 - 0xFB (Data Mark); 01 - 0xFA (user defined); 10 - 0xF9 (use defined); 11 - 0xF8 (Deleted Data Mark);
//
void FdcProcessWriteSectorCommand(void)
{
	int nSide  = FdcGetSide(g_FDC.byDriveSel);
	int nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	g_FDC.byCommandType = 2;
	FdcSetRecordType(0xFB);

	// read specified sector so that it can be modified
	FdcReadSector(g_FDC.byDriveSel, nSide, g_FDC.byTrack, g_FDC.bySector);

	FdcClrFlag(eDataRequest);
	FdcSetFlag(eHeadLoaded);

	g_stSector.nSector     = g_FDC.bySector;
	g_stSector.nSectorSize = g_dtDives[nDrive].dmk.nSectorSize;
	g_tdTrack.nFileOffset  = FdcGetTrackOffset(nDrive, nSide, g_FDC.byTrack);
	g_tdTrack.pbyWritePtr  = g_tdTrack.byTrackData + g_stSector.nSectorDataOffset;
	g_tdTrack.nWriteCount  = g_stSector.nSectorSize;
	g_tdTrack.nWriteSize   = g_stSector.nSectorSize;	// number of byte to be transfered to the computer before
															// setting the Data Address Mark status bit (1 if Deleted Data)
	g_FDC.nServiceState    = 0;

	if ((g_FDC.byCurCommand & 0x01) == 0)
	{
		g_stSector.bySectorDataAddressMark = 0xFB;
	}
	else
	{
		g_stSector.bySectorDataAddressMark = 0xF8;
	}

	g_FDC.nProcessFunction = psWriteSector;

	// Note: computer now writes the data register for each of the sector data bytes.
	//
	//       Actual data transfer is handled in the FdcServiceWrite() function.
}

//-----------------------------------------------------------------------------
// Command code 1 1 0 0 0 1 0 0
//
void FdcProcessReadAddressCommand(void)
{
	g_FDC.byCommandType = 3;
	
	// send the first ID field of the current track to the computer
	
	// Byte 1 : Track Address
	// Byte 2 : Side Number
	// Byte 3 : Sector Address
	// Byte 4 : Sector Length
	// Byte 5 : CRC1
	// Byte 6 : CRC2

	g_tdTrack.pbyReadPtr = &g_tdTrack.byTrackData[(FdcGetIDAM(0) & 0x3FFF) + 1];
	g_tdTrack.nReadSize  = 6;
	g_tdTrack.nReadCount = 6;

	g_FDC.dwStateTimer = 0;
	FdcClrFlag(eDataRequest);

	// number of byte to be transfered to the computer before
	// setting the Data Address Mark status bit (1 if Deleted Data)
	g_FDC.nServiceState     = 0;
	g_FDC.nProcessFunction  = psReadSector;

	// Note: CRC should be checked during transfer to the computer

}

//-----------------------------------------------------------------------------
// Command code 1 1 0 1 I3 I2 I1 I0
//
// Interrupt Condition flasg (Bits 3-1)
// ------------------------------------
// I0 = 1, Not-Ready to Ready Transition
// I1 = 1, Ready to Not-Ready Transition
// I2 = 1, Index Pulse
// I3 = 1, Immediate Interrupt
// I3-I0 = 0, Terminate with no Interrupt
//
void FdcProcessForceInterruptCommand(void)
{
	g_FDC.byCommandType  = 4;
	g_tdTrack.nReadSize  = 0;
	g_tdTrack.nReadCount = 0;
	g_tdTrack.nWriteSize = 0;
	g_FDC.byIntrEnable   = g_FDC.byCurCommand & 0x0F;
	memset(&g_FDC.status, 0, sizeof(g_FDC.status));

    g_FDC.byCurCommand      = g_FDC.byCommandReg;
    g_FDC.byCommandReceived = 0;
    g_FDC.nProcessFunction  = psIdle;
}

//-----------------------------------------------------------------------------
// Command code 1 1 1 0 0 1 0 S
//
// S = 1 - Do not synchronize to AM; 0 - Synchronize to AM;
void FdcProcessReadTrackCommand(void)
{
	int nSide  = FdcGetSide(g_FDC.byDriveSel);
	int nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	g_FDC.byCommandType = 3;
	FdcSetFlag(eHeadLoaded);

	g_tdTrack.nTrack = 255;

	FdcReadTrack(nDrive, nSide, g_FDC.byTrack);

	g_tdTrack.nDrive       = nDrive;
	g_tdTrack.nSide        = nSide;
	g_tdTrack.nTrack       = g_FDC.byTrack;
	g_tdTrack.pbyReadPtr   = g_tdTrack.byTrackData + 0x80;
	g_tdTrack.nReadSize    = g_dtDives[g_tdTrack.nDrive].dmk.wTrackLength;
	g_tdTrack.nReadCount   = g_tdTrack.nReadSize;
	g_FDC.nDataSize        = 1;
	g_FDC.nServiceState    = 0;

	g_FDC.nProcessFunction = psReadTrack;
}

//-----------------------------------------------------------------------------
// Command code 1 1 1 1 0 1 0 0
//
void FdcProcessWriteTrackCommand(void)
{
	int nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 3;
	FdcSetFlag(eHeadLoaded);
	
	memset(g_tdTrack.byTrackData+0x80, 0, sizeof(g_tdTrack.byTrackData)-0x80);

	if (g_FDC.byDoublerDensity)
	{
		g_tdTrack.byDensity = eDD;
	}
	else
	{
		g_tdTrack.byDensity = eSD;
	}

	g_tdTrack.nDrive       = FdcGetDriveIndex(g_FDC.byDriveSel);
	g_tdTrack.nSide        = nSide;
	g_tdTrack.nTrack       = g_FDC.byTrack;
	g_tdTrack.pbyWritePtr  = g_tdTrack.byTrackData + 0x80;
	g_tdTrack.nWriteSize   = g_dtDives[g_tdTrack.nDrive].dmk.wTrackLength;
	g_tdTrack.nWriteCount  = g_tdTrack.nWriteSize;
	g_FDC.nServiceState    = 0;
	g_FDC.nProcessFunction = psWriteTrack;
}

//-----------------------------------------------------------------------------
void FdcProcessMount(void)
{
	g_FDC.byCommandType    = 2;
	FdcClrFlag(eDataRequest);
	g_FDC.nServiceState    = 0;
	g_FDC.nProcessFunction = psMountImage;

	// Note: computer now writes the data register for each of the command data bytes.
	//
	//       Actual data transfer is handled in the FdcServiceMountImage() function.

}

//-----------------------------------------------------------------------------
void FdcProcessOpenFile(void)
{
	g_FDC.byCommandType    = 2;
	FdcClrFlag(eDataRequest);
	g_FDC.nServiceState    = 0;
	g_FDC.nProcessFunction = psOpenFile;

	// Note: computer now writes the data register for each of the command data bytes.
	//
	//       Actual data transfer is handled in the FdcServiceOpenFile() function.
	
}

//-----------------------------------------------------------------------------
void FdcProcessCommand(void)
{
	memset(&g_FDC.status, 0, sizeof(g_FDC.status));
	FdcSetFlag(eBusy);
	g_FDC.nServiceState     = 0;
	g_FDC.nProcessFunction  = psIdle;
	g_FDC.byCurCommand      = g_FDC.byCommandReg;
	g_FDC.byCommandReceived = 0;

	switch (g_FDC.byCurCommand >> 4)
	{
		case 0: // Restore									(Type 1 Command)
			FdcProcessRestoreCommand();
			break;

		case 1: // Seek										(Type 1 Command)
			FdcProcessSeekCommand();
			break;

		case 2: // Step (don't update track register)		(Type 1 Command)
		case 3: // Step (update track register)				(Type 1 Command)
			FdcProcessStepCommand();
			break;

		case 4: // Step In (don't update track register)	(Type 1 Command)
		case 5: // Step In (update track register)			(Type 1 Command)
			FdcProcessStepInCommand();
			break;

		case 6: // Step Out (don't update track register)	(Type 1 Command)
		case 7: // Step Out (update track register)			(Type 1 Command)
			FdcProcessStepOutCommand();
			break;

		case 8: // Read Sector (single record)				(Type 2 Command)
		case 9: // Read Sector (multiple record)			(Type 2 Command)
			FdcProcessReadSectorCommand();
			break;

		case 10: // Write Sector (single record)			(Type 2 Command)
		case 11: // Write Sector (multiple record)			(Type 2 Command)
			FdcProcessWriteSectorCommand();
			break;

		case 12: // Read Address							(Type 3 Command)
			if (g_FDC.byCommandReg == 0xC4)
			{
				FdcProcessReadAddressCommand();
			}

			break;

		case 13: // Force Interrupt							(Type 4 Command)
			FdcProcessForceInterruptCommand();
			break;

		case 14: // Read Track								(Type 3 Command)
			if ((g_FDC.byCommandReg == 0xE4) || (g_FDC.byCommandReg == 0xE5))
			{
				FdcProcessReadTrackCommand();
			}

			break;

		case 15: // Write Track								(Type 3 Command)
			if (g_FDC.byCommandReg == 0xF4)
			{
				FdcProcessWriteTrackCommand();
			}

			break;

		default:
			memset(&g_FDC.status, 0, sizeof(g_FDC.status));
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcServiceReadSector(void)
{
	switch (g_FDC.nServiceState)
	{
		case 0:
			g_FDC.dwStateTimer = 0;
			++g_FDC.nServiceState;
			break;

		case 1: // give host time to get ready for data
			if (g_FDC.dwStateTimer < 1000) // 1ms
			{
				break;
			}

			FdcSetRecordType(g_FDC.byRecordMark);
			FdcGenerateDRQ();
			g_FDC.dwStateTimer = 0;
			++g_FDC.nServiceState;
			break;

		case 2:
			if (g_tdTrack.nReadCount > 0)
			{
				break;
			}

			++g_FDC.nServiceState;
			FdcClrFlag(eBusy);
			FdcGenerateIntr();
			g_FDC.nProcessFunction = psIdle;
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcServiceReadTrack(void)
{
	switch (g_FDC.nServiceState)
	{
		case 0:
			g_FDC.dwStateTimer = 0;
			++g_FDC.nServiceState;
			break;

		case 1: // give host time to get ready for data
			if (g_FDC.dwStateTimer < 1000) // 1ms
			{
				break;
			}

			FdcSetRecordType(g_FDC.byRecordMark);
			FdcGenerateDRQ();
			g_FDC.dwStateTimer = 0;
			++g_FDC.nServiceState;
			break;

		case 2:
			if (g_tdTrack.nReadCount > 0)
			{
				break;
			}

			++g_FDC.nServiceState;
			FdcClrFlag(eBusy);
			FdcGenerateDRQ();
			FdcGenerateIntr();
			g_FDC.nProcessFunction = psIdle;
			break;
	}
}

//-----------------------------------------------------------------------------
void WriteDmkSectorData(int nSector)
{
	int  nDataOffset;

	if ((g_tdTrack.nDrive < 0) || (g_tdTrack.nDrive >= MAX_DRIVES))
	{
		return;
	}

	if (g_dtDives[g_tdTrack.nDrive].f == NULL)
	{
		return;
	}

	// TODO: check to see if disk image is read only

	nDataOffset = g_tdTrack.nDAM[nSector];

	if (nDataOffset < 0)
	{
		return;
	}
	
	FileSeek(g_dtDives[g_tdTrack.nDrive].f, g_tdTrack.nFileOffset+nDataOffset);

	if (g_tdTrack.byDensity == eDD) // double density
	{
		FileWrite(g_dtDives[g_tdTrack.nDrive].f, g_tdTrack.byTrackData+nDataOffset, g_stSector.nSectorSize+6);
	}
	else // single density
	{
		FileWrite(g_dtDives[g_tdTrack.nDrive].f, g_tdTrack.byTrackData+nDataOffset, g_stSector.nSectorSize+3);
	}

	FileFlush(g_dtDives[g_tdTrack.nDrive].f);
}

//-----------------------------------------------------------------------------
void WriteSectorData(int nSector)
{
	int nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	switch (g_dtDives[nDrive].nDriveFormat)
	{
		case eDMK:
			WriteDmkSectorData(nSector);
			break;

		case eHFE:
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcGenerateSectorCRC(int nSector, int nSectorSize)
{
	WORD wCRC16;
	int  nDataOffset;

	// now locate the 0xA1, 0xA1, 0xA1, 0xFB sequence that marks the start of sector data

	nDataOffset = g_tdTrack.nDAM[nSector];
	
	if (nDataOffset < 0)
	{
		return;
	}

	if (g_tdTrack.byDensity == eDD) // double density
	{
		// CRC consists of the 0xA1, 0xA1, 0xA1, 0xFB sequence and the sector data
		wCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], nSectorSize+4, 1);
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+4] = wCRC16 >> 8;
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+5] = wCRC16 & 0xFF;
	}
	else // single density
	{
		wCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], nSectorSize+1, 1);
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+1] = wCRC16 >> 8;
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+2] = wCRC16 & 0xFF;
	}
}

//-----------------------------------------------------------------------------
void FdcUpdateDataAddressMark(int nSector, int nSectorSize)
{
	int nDataOffset, i;

	// get offset of the 0xA1, 0xA1, 0xA1, 0xFB sequence that marks the start of sector data

	nDataOffset = g_tdTrack.nDAM[nSector];

	if (nDataOffset < 0)
	{
		return;
	}

	// nDataOffset is the index of the first 0xA1 byte in the 0xA1, 0xA1, 0xA1, 0xFB sequence

	// update sector data mark (0xFB/0xF8)

	if (g_tdTrack.byDensity == eDD) // double density
	{
		for (i = 0; i < 4; ++i)
		{
			if (g_tdTrack.byTrackData[nDataOffset+i] != 0xA1)
			{
				g_tdTrack.byTrackData[nDataOffset+i] = g_stSector.bySectorDataAddressMark;
				i = 4;
			}
		}
	}
	else // single density
	{
		g_tdTrack.byTrackData[nDataOffset] = g_stSector.bySectorDataAddressMark;
	}
}

//-----------------------------------------------------------------------------
void FdcServiceWriteSector(void)
{
	switch (g_FDC.nServiceState)
	{
		case 0:
			if (g_FDC.dwStateTimer < 1000)
			{
				break;
			}

			// indicate to the Z80 that we are ready for the first data byte
			FdcGenerateDRQ();
			++g_FDC.nServiceState;
			break;

		case 1:
			if (g_tdTrack.nWriteCount > 0)
			{
				break;
			}

			FdcUpdateDataAddressMark(g_stSector.nSector, g_stSector.nSectorSize);
			
			// perform a CRC on the sector data (including preceeding 4 bytes) and update sector CRC value
			FdcGenerateSectorCRC(g_stSector.nSector, g_stSector.nSectorSize);
			
			// flush sector to SD-Card
			WriteSectorData(g_stSector.nSector);
		
			++g_FDC.nServiceState;
			g_FDC.dwStateTimer = 0;
			break;
		
		case 2:
			if (g_FDC.dwStateTimer < 1000)
			{
				break;
			}

			FdcGenerateIntr();
			FdcClrFlag(eBusy);
			g_FDC.nServiceState    = 0;
			g_FDC.nProcessFunction = psIdle;
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcProcessTrackData1771(TrackType* ptdTrack)
{
	BYTE* pbyCrcStart = ptdTrack->byTrackData;
	BYTE* pbySrc = ptdTrack->byTrackData + 0x80;
	BYTE* pbyDst = g_byTrackBuffer;
	WORD  wCRC16;
	int   i, nSide = 0;

	for (i = 0x80; i < ptdTrack->nTrackSize; ++i)
	{
		*pbyDst = *pbySrc;

		switch (*pbySrc)
		{
			case 0xF5:
				pbyCrcStart = pbyDst;
				*pbyDst = 0xA1;
				break;
			
			case 0xF6:
				*pbyDst = 0xC2;
				break;

			case 0xF7:
				if (ptdTrack->byDensity == eDD)
				{
					wCRC16 = Calculate_CRC_CCITT(pbyCrcStart-2, (int)(pbySrc-pbyCrcStart+2), 1);
				}
				else
				{
					wCRC16 = Calculate_CRC_CCITT(pbyCrcStart, (int)(pbySrc-pbyCrcStart), 1);
				}

				*pbyDst = wCRC16 >> 8;
				++pbyDst;
				++i;
				*pbyDst = wCRC16 & 0xFF;
				break;

			case 0xFB:
				if (ptdTrack->byDensity == eSD) // single density
				{
					pbyCrcStart = pbySrc;
				}

				break;

			case 0xFE:
				if (ptdTrack->byDensity == eSD) // single density
				{
					pbyCrcStart = pbySrc;
					ptdTrack->nSide = *(pbySrc+2);
				}

				break;
		}
		
		++pbySrc;
		++pbyDst;
	}

	memcpy(ptdTrack->byTrackData+0x80, g_byTrackBuffer, sizeof(ptdTrack->byTrackData)-0x80);
}

//-----------------------------------------------------------------------------
void FdcProcessTrackData1791(TrackType* ptdTrack)
{
	WORD  wCRC16;
	int   i, j, nCrcStart, nSide = 0;

	j = nCrcStart = 0;

	for (i = 0x80; i < ptdTrack->nTrackSize; ++i)
	{
		g_byTrackBuffer[j] = ptdTrack->byTrackData[i];

		switch (ptdTrack->byTrackData[i])
		{
			case 0xF5:
				nCrcStart = j;
				g_byTrackBuffer[j] = 0xA1;
				break;
			
			case 0xF6:
				g_byTrackBuffer[j] = 0xC2;
				break;

			case 0xF7:
				wCRC16 = Calculate_CRC_CCITT(&g_byTrackBuffer[nCrcStart-2], (int)(j-nCrcStart+2), 1);

				g_byTrackBuffer[j] = wCRC16 >> 8;
				++j;
				g_byTrackBuffer[j] = wCRC16 & 0xFF;
				break;
		}
		
		++j;
	}

	memcpy(ptdTrack->byTrackData+0x80, g_byTrackBuffer, sizeof(ptdTrack->byTrackData)-0x80);
}

//-----------------------------------------------------------------------------
void FdcBuildIdamTable(TrackType* ptdTrack)
{
	BYTE* pbyTrackData = ptdTrack->byTrackData;
	BYTE  byDensity    = g_dtDives[ptdTrack->nDrive].dmk.byDensity;
	BYTE  byFound;
	int   nIndex, nIDAM;
	int   nTrackSize = ptdTrack->nTrackSize;

	// reset IDAM table to 0's
	memset(pbyTrackData, 0, 0x80);
	memset(ptdTrack->nIDAM, 0, sizeof(ptdTrack->nIDAM));

	// search track data for sectors (start at first byte after the last IDAM index)
	nIndex = 128;
	nIDAM  = 0;

	while (nIndex < nTrackSize)
	{
		byFound = 0;

		while ((byFound == 0) && (nIndex < nTrackSize))
		{
			if ((*(pbyTrackData+nIndex) == 0xA1) && (*(pbyTrackData+nIndex+1) == 0xA1) && (*(pbyTrackData+nIndex+2) == 0xA1) && (*(pbyTrackData+nIndex+3) == 0xFE))
			{
				byDensity = eDD;
				byFound = 1;
			}
			else if (*(pbyTrackData+nIndex) == 0xFE)
			{
				byDensity = eSD;
				byFound = 1;
			}
			else
			{
				++nIndex;
			}
		}

		if (byFound)
		{
			// at this point nIndex contains the location of the first 0xA1 byte for DD; or at 0xFE for SD;

			if (byDensity == eDD) // Double Density
			{
				// advance to the 0xFE byte. 
				nIndex += 3; // The IDAM pointer is the offset from the start of track data to the 0xFE of the associated sector.
			}

			ptdTrack->nIDAM[nIDAM] = nIndex;

			*(pbyTrackData + nIDAM * 2)     = nIndex & 0xFF;
			*(pbyTrackData + nIDAM * 2 + 1) = nIndex >> 8;

			++nIDAM;
			nIndex += 2;
		}
	}
}

//-----------------------------------------------------------------------------
void FdcBuildDataSizeTable(TrackType* ptdTrack)
{
	int i;
	
	for (i = 0; i < 0x80; ++i)
	{
		ptdTrack->nDataSize[i] = FdcGetDataSize(ptdTrack, ptdTrack->nIDAM[i]);
	}
}

//-----------------------------------------------------------------------------
void FdcBuildDamTable(TrackType* ptdTrack)
{
	int i;
	
	for (i = 0; i < 0x80; ++i)
	{
		ptdTrack->nDAM[i] = FdcGetDAM_Offset(ptdTrack, ptdTrack->nIDAM[i], ptdTrack->nDataSize[i]);
	}
}

//-----------------------------------------------------------------------------
void FdcWriteDmkTrack(TrackType* ptdTrack)
{
	if ((ptdTrack->nDrive < 0) || (ptdTrack->nDrive >= MAX_DRIVES))
	{
		return;
	}

	if (g_dtDives[ptdTrack->nDrive].f == NULL)
	{
		return;
	}

	// check if the disk header (number of sides) needs to be updated
	if (ptdTrack->nSide >= g_dtDives[ptdTrack->nDrive].dmk.byNumSides)
	{
		g_dtDives[ptdTrack->nDrive].dmk.byNumSides = ptdTrack->nSide + 1;
		g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader[4] &= 0xEF;
		FileSeek(g_dtDives[ptdTrack->nDrive].f, 0);
		FileWrite(g_dtDives[ptdTrack->nDrive].f, g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader, sizeof(g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader));
	}

	// check if the disk header (number of tracks) needs to be updated
	if (ptdTrack->nTrack >= g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader[1])
	{
		g_dtDives[ptdTrack->nDrive].byNumTracks = ptdTrack->nTrack + 1;
		g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader[1] = ptdTrack->nTrack + 1;
		FileSeek(g_dtDives[ptdTrack->nDrive].f, 0);
		FileWrite(g_dtDives[ptdTrack->nDrive].f, g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader, sizeof(g_dtDives[ptdTrack->nDrive].dmk.byDmkDiskHeader));
	}

	ptdTrack->nFileOffset = FdcGetTrackOffset(ptdTrack->nDrive, ptdTrack->nSide, ptdTrack->nTrack);

	FileSeek(g_dtDives[ptdTrack->nDrive].f, ptdTrack->nFileOffset);
	FileWrite(g_dtDives[ptdTrack->nDrive].f, ptdTrack->byTrackData, ptdTrack->nTrackSize);
	FileFlush(g_dtDives[ptdTrack->nDrive].f);
}

//-----------------------------------------------------------------------------
void FdcWriteTrack(TrackType* ptdTrack)
{
	switch (ptdTrack->nType)
	{
		case eDMK:
			FdcWriteDmkTrack(ptdTrack);
			break;

		case eHFE:
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcServiceWriteTrack(void)
{
	switch (g_FDC.nServiceState)
	{
		case 0:
			if (g_FDC.dwStateTimer < 1000)
			{
				break;
			}

			// indicate to the Z80 that we are ready for the first data byte
			FdcGenerateDRQ();
			++g_FDC.nServiceState;
			break;
		
		case 1:
			if (g_tdTrack.nWriteCount > 0)
			{
				break;
			}

			if (g_FDC.byDoublerDensity)
			{
				FdcProcessTrackData1791(&g_tdTrack);	// scan track data to generate CRC values
			}
			else
			{
				FdcProcessTrackData1771(&g_tdTrack);	// scan track data to generate CRC values
			}

			FdcBuildIdamTable(&g_tdTrack);		// scan track data to build the IDAM table
			FdcBuildDataSizeTable(&g_tdTrack);
			FdcBuildDamTable(&g_tdTrack);

			// flush track to SD-Card
			FdcWriteTrack(&g_tdTrack);
		
			g_FDC.dwStateTimer = 0;
			++g_FDC.nServiceState;
			break;

		case 2:
			if (g_FDC.dwStateTimer < 1000)
			{
				break;
			}

			g_FDC.nServiceState    = 0;
			g_FDC.nProcessFunction = psIdle;
			break;
	}
}

//-----------------------------------------------------------------------------
void FdcServiceSeek(void)
{
	if (g_FDC.dwStateTimer < 1000) // 1ms
	{
		return;
	}

	FdcClrFlag(eBusy);
	FdcGenerateIntr();
	g_FDC.nProcessFunction = psIdle;
}

//-----------------------------------------------------------------------------
void SetResponseLength(BufferType* bResponse)
{
	int i = strlen((char*)(bResponse->buf));

	bResponse->cmd[0] = i & 0xFF;
	bResponse->cmd[1] = (i >> 8) & 0xFF;
}

//-----------------------------------------------------------------------------
void FdcProcessStatusRequest(byte print)
{
	char szBuf[64];
	char szLineEnd[4];
	int  i;
	
	if (print)
	{
		strcpy(szLineEnd, "\r\n");
	}
	else
	{
		strcpy(szLineEnd, "\r");
	}

    memset(&g_bFdcResponse, 0, sizeof(g_bFdcResponse));

	strcpy((char*)(g_bFdcResponse.buf), "Pico FDC Version ");
	strcat((char*)(g_bFdcResponse.buf), g_pszVersion);
	strcat((char*)(g_bFdcResponse.buf), szLineEnd);
	strcat((char*)(g_bFdcResponse.buf), "BootIni=");
	strcat((char*)(g_bFdcResponse.buf), g_szBootConfig);
	strcat((char*)(g_bFdcResponse.buf), szLineEnd);

	if (g_byBootConfigModified)
	{
		file* f;
		char* psz;
		int   nLen;

		f = FileOpen(g_szBootConfig, FA_READ);
		
		if (f == NULL)
		{
			strcat((char*)(g_bFdcResponse.buf), "Unable to open specified ini file");
		}
		else
		{
			nLen = FileReadLine(f, (BYTE*)szBuf, sizeof(szBuf)-2);
			
			while (nLen >= 0)
			{
				if (nLen > 2)
				{
					strcat_s((char*)(g_bFdcResponse.buf),  sizeof(g_bFdcResponse.buf)-1, szBuf);
					strcat_s((char*)(g_bFdcResponse.buf),  sizeof(g_bFdcResponse.buf)-1, szLineEnd);
				}

				nLen = FileReadLine(f, (BYTE*)szBuf, sizeof(szBuf)-2);
			}
			
			FileClose(f);
		}
	}
	else
	{
		for (i = 0; i < MAX_DRIVES; ++i)
		{
			sprintf(szBuf, "%d: ", i);
			strcat((char*)(g_bFdcResponse.buf), szBuf);
			strcat((char*)(g_bFdcResponse.buf), g_dtDives[i].szFileName);
			strcat((char*)(g_bFdcResponse.buf), szLineEnd);
		}
	}

	if (print)
	{
		puts(g_bFdcResponse.buf);
	}
	else
	{
		SetResponseLength(&g_bFdcResponse);
	}
}

//-----------------------------------------------------------------------------
int FdcFileListCmp(const void * a, const void * b)
{
	FILINFO* f1 = (FILINFO*) a;
	FILINFO* f2 = (FILINFO*) b;

	return stricmp(f1->fname, f2->fname);
}

//-----------------------------------------------------------------------------
void FdcProcessFindFirst(char* pszFilter, char* pszFolder)
{
    FRESULT fr;  // Return value
	int     i;
	
	g_nFindIndex = 0;
	g_nFindCount = 0;

    memset(&g_bFdcResponse, 0, sizeof(g_bFdcResponse));

	strcpy((char*)(g_bFdcResponse.buf), "too soon");

    memset(&g_dj, 0, sizeof(g_dj));
    memset(&g_fno, 0, sizeof(g_fno));
	memset(g_fiFindResults, 0, sizeof(g_fiFindResults));

	strcpy(g_szFindFilter, pszFilter);

    fr = f_findfirst(&g_dj, &g_fno, pszFolder, "*");

    if (FR_OK != fr)
	{
		strcpy((char*)(g_bFdcResponse.buf), "No matching file found.");
        SetResponseLength(&g_bFdcResponse);
        return;
    }

	while ((fr == FR_OK) && (g_fno.fname[0] != 0) && (g_nFindCount < FIND_MAX_SIZE))
	{
		if ((g_fno.fattrib & AM_DIR) || (g_fno.fattrib & AM_SYS))
		{
			// pcAttrib = pcDirectory;
		}
		else
		{
			if ((g_szFindFilter[0] == '*') || (stristr(g_fno.fname, g_szFindFilter) != NULL))
			{
				memcpy(&g_fiFindResults[g_nFindCount], &g_fno, sizeof(FILINFO));
				++g_nFindCount;
			}
		}

		if (g_fno.fname[0] != 0)
		{
			fr = f_findnext(&g_dj, &g_fno); /* Search for next item */
		}
	}

	f_closedir(&g_dj);

	if (g_nFindCount > 0)
	{
		qsort(g_fiFindResults, g_nFindCount, sizeof(FILINFO), FdcFileListCmp);

		sprintf((char*)(g_bFdcResponse.buf), "%2d/%02d/%d %7d ",
				((g_fiFindResults[g_nFindIndex].fdate >> 5) & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate >> 9) + 1980,
				g_fiFindResults[g_nFindIndex].fsize);
        strcat((char*)(g_bFdcResponse.buf), (char*)g_fiFindResults[g_nFindIndex].fname);

		++g_nFindIndex;
	}

    SetResponseLength(&g_bFdcResponse);
}

//-----------------------------------------------------------------------------
void FdcProcessFindNext(void)
{
    FRESULT fr = FR_OK;  /* Return value */
	BYTE    bFound = FALSE;
	
    memset(&g_bFdcResponse, 0, sizeof(g_bFdcResponse));

	if (g_nFindIndex < g_nFindCount)
	{
		sprintf((char*)(g_bFdcResponse.buf), "%2d/%02d/%d %7d ",
				((g_fiFindResults[g_nFindIndex].fdate >> 5) & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate >> 9) + 1980,
				g_fiFindResults[g_nFindIndex].fsize);
        strcat((char*)(g_bFdcResponse.buf), (char*)g_fiFindResults[g_nFindIndex].fname);

		++g_nFindIndex;
	}
	
    SetResponseLength(&g_bFdcResponse);
}

//-----------------------------------------------------------------------------
void FdcSaveBootCfg(char* pszIniFile)
{
	char szNewIniFile[30];
	file* f;

	strncpy(szNewIniFile, pszIniFile, sizeof(szNewIniFile)-1);

	StrToUpper(szNewIniFile);

	if (strstr(szNewIniFile, ".INI") == NULL)
	{
		strncat(szNewIniFile, ".INI", sizeof(szNewIniFile)-1);
	}

	if (!FileExists(szNewIniFile))
	{
		printf("%s does not exist, boot.cfg not modified.\r\n", szNewIniFile);
		return;
	}

	f = FileOpen("boot.cfg", FA_WRITE | FA_CREATE_ALWAYS);
	
	if (f == NULL)
	{
		puts("Unable to open boot.cfg to write selected ini file.");
		return;
	}

	g_byBootConfigModified = TRUE;

	FileWrite(f, szNewIniFile, strlen(szNewIniFile));
	FileClose(f);
	strcpy(g_szBootConfig, szNewIniFile);
}

//-----------------------------------------------------------------------------
void FdcServiceMountImage(void)
{
	char* psz;
	int   nDrive;

	// locate the drive number
	psz = SkipBlanks((char*)g_bFdcRequest.buf);
	nDrive = atoi(psz);

	psz = SkipToBlank((char*)psz);

	if (*psz != ' ')
	{
		return;
	}

	psz = SkipBlanks((char*)psz);

	if ((nDrive < 0) || (nDrive >= MAX_DRIVES))
	{
		return;
	}

	// if test if it is an ini file
	if (stristr(g_bFdcRequest.buf, ".ini"))
	{
		FdcSaveBootCfg((char*)psz);
	}
	else if (FileExists((char*)psz))
	{
		strcpy(g_dtDives[nDrive].szFileName, (char*)psz);
		FileClose(g_dtDives[nDrive].f);
		g_dtDives[nDrive].f = NULL;
		FdcMountDrive(nDrive);
	}

    FdcProcessStatusRequest(false);
}

//-----------------------------------------------------------------------------
void FdcFormatDrive(void)
{
	file* f;
	char* psz;
	char  buf[256];
	int   drive = atoi(g_bFdcRequest.buf);
	int   size, read;

	if (!isdigit((char)g_bFdcRequest.buf[0]))
	{
		strcpy((char*)(g_bFdcResponse.buf), "Drive number not specified\n");
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	if ((drive < 0) || (drive > (sizeof(g_dtDives)/sizeof(g_dtDives[0]))))
	{
		sprintf(buf, "Invalid drive number %d\n", drive);
		strcpy((char*)(g_bFdcResponse.buf), buf);
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	if (g_dtDives[drive].f == NULL)
	{
		sprintf(buf, "Drive must be mounted to be formated\n");
		strcpy((char*)(g_bFdcResponse.buf), buf);
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	psz = g_bFdcRequest.buf;
	psz = SkipBlanks(psz);
	psz = SkipToBlank(psz);
	psz = SkipBlanks(psz);

	strcpy(buf, "0:\\FMT\\");
	strcat(buf, psz);

	if (FR_OK != f_stat(buf, &g_fno))
	{
		strcpy((char*)(g_bFdcResponse.buf), "Unable to formatted disk image: ");
		strcat((char*)(g_bFdcResponse.buf), buf);
		strcat((char*)(g_bFdcResponse.buf), "\n");
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	f = FileOpen(buf, FA_READ);

	if (f == NULL)
	{
		strcpy((char*)(g_bFdcResponse.buf), "Unable to open file: ");
		strcat((char*)(g_bFdcResponse.buf), buf);
		strcat((char*)(g_bFdcResponse.buf), "\n");
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	FileClose(g_dtDives[drive].f);
	g_dtDives[drive].f = NULL;

	g_dtDives[drive].f = FileOpen(g_dtDives[drive].szFileName, FA_WRITE | FA_CREATE_ALWAYS);

	if (g_dtDives[drive].f == NULL)
	{
		strcpy((char*)(g_bFdcResponse.buf), "Unable to create new DMK disk image.\n");
		SetResponseLength(&g_bFdcResponse);
		return;
	}

	size = g_fno.fsize;

	while (size > 0)
	{
		read = sizeof(g_byTrackBuffer);

		if (read > size)
		{
			read = size;
		}

		FileRead(f, g_byTrackBuffer, read);
		FileWrite(g_dtDives[drive].f, g_byTrackBuffer, read);
		size -= read;
	}

	FileClose(f);
	FileClose(g_dtDives[drive].f);
	g_dtDives[drive].f = NULL;

	FdcMountDrive(drive);

	sprintf((char*)(g_bFdcResponse.buf), "Formatting drive %d (%s), complete.\n", drive, g_dtDives[drive].szFileName);
	SetResponseLength(&g_bFdcResponse);
}

//-----------------------------------------------------------------------------
void FdcProcessRequest(void)
{
    switch (g_bFdcRequest.cmd[0])
    {
        case 0: // do nothing
            break;

        case 1: // put status in response buffer
            FdcProcessStatusRequest(false);
            break;

        case 2: // find first file
            FdcProcessFindFirst("*", "0:");
            break;

        case 3: // find next file
            FdcProcessFindNext();
            break;

		case 4: // Mount ini, dmk or hfe file
			FdcServiceMountImage();
			break;
			
		case 11: // format mounted drive
			FdcFormatDrive();
			break;

        case 0x80:
			FdcProcessFindFirst(".INI", "0:");
            break;

        case 0x81:
			FdcProcessFindFirst(".DMK", "0:");
            break;

        case 0x82:
			FdcProcessFindFirst(".HFE", "0:");
            break;

        case 0x83:
			FdcProcessFindFirst(".DMK", "0:\\FMT");
            break;
    }
}

//-----------------------------------------------------------------------------
void FdcUpdateCounters(void)
{
	uint64_t nDiff;

	g_nTimeNow  = time_us_64();
	nDiff       = g_nTimeNow - g_nPrevTime;
	g_nPrevTime = g_nTimeNow;

	if (g_dwMotorOnTimer != 0)
	{
		g_byMotorWasOn = 1;

		g_dwMotorOnTimer  = CountDown(g_dwMotorOnTimer, nDiff);
		g_nRotationCount += nDiff;

		// (g_dwTimerFrequency / 5) = count to make one full rotation of the diskette (200 ms at 300 RPM)
		if (g_nRotationCount >= g_dwRotationTime)
		{
			g_nRotationCount -= g_dwRotationTime;
		}

		if (g_nRotationCount < g_dwIndexTime)
		{
			FdcSetFlag(eIndex);
		 	gpio_put(LED_PIN, 1);
		}
		else
		{
			FdcClrFlag(eIndex);
		 	gpio_put(LED_PIN, 0);
		}
	}
	else
	{
        gpio_put(LED_PIN, 1);

		if (g_byMotorWasOn)
		{
			g_byMotorWasOn = 0;
		}
	}

	g_FDC.dwStateTimer = CountUp(g_FDC.dwStateTimer, nDiff);
}

//-----------------------------------------------------------------------------
void FdcServiceStateMachine(void)
{
	FdcUpdateCounters();
	TestSdCardInsertion();

    if (g_bFdcRequest.cmd[0] != 0)
    {
        FdcProcessRequest();
        g_bFdcRequest.cmd[0] = 0;
        return;
    }

	// check if we have a command to process
	if (g_FDC.byCommandReceived != 0)
	{
		g_FDC.byCommandType = byCommandTypes[g_FDC.byCommandReg>>4];
		FdcProcessCommand();
		return;
	}

	switch (g_FDC.nProcessFunction)
	{
		case psIdle:
			break;

		case psReadSector:
			FdcServiceReadSector();
			break;

		case psReadTrack:
			FdcServiceReadTrack();
			break;

		case psWriteSector:
			FdcServiceWriteSector();
			break;

		case psWriteTrack:
			FdcServiceWriteTrack();
			break;

		case psSeek:
			FdcServiceSeek();
			break;
	}
}

#ifdef ENABLE_LOGGING
//----------------------------------------------------------------------------
void GetCommandText(char* psz, int nMaxLen, BYTE byCmd)
{
	*psz = 0;

	if ((byCmd & 0xF0) == 0)         // 0000xxxx
	{
		sprintf(psz, "CMD: %02X Restore", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x10) // 0001xxxx
	{
		sprintf(psz, "CMD: %02X SEEK %02X, From %02X", byCmd, g_FDC.byData, g_FDC.byTrack);
	}
	else if ((byCmd & 0xF0) == 0x20) // 0010xxxx
	{
		sprintf(psz, "CMD: %02X Step, Do Not Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x30) // 0011xxxx
	{
		sprintf(psz, "CMD: %02X Step, Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x40) // 0100xxxx
	{
		sprintf(psz, "CMD: %02X Step In, Do Not Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x50) // 0101xxxx
	{
		sprintf(psz, "CMD: %02X Step In, Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x60) // 0110xxxx
	{
		sprintf(psz, "CMD: %02X Step Out, Do Not Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x70) // 0111xxxx
	{
		sprintf(psz, "CMD: %02X Step Out, Update Track Register", byCmd);
	}
	else if ((byCmd & 0xF0) == 0x80) // 1000xxxx
	{
		sprintf(psz, "CMD: %02X DRV: %02X TRK: %02X RSEC: %02X", byCmd, g_FDC.byDriveSel, g_FDC.byTrack, g_FDC.bySector);
	}
	else if ((byCmd & 0xF0) == 0x90) // 1001xxxx
	{
		sprintf(psz, "CMD: %02X RSEC: Multiple Record", byCmd);
	}
	else if ((byCmd & 0xF0) == 0xA0) // 1010xxxx
	{
		sprintf(psz, "CMD: %02X WSEC: %02X TRK: %02X", byCmd, g_FDC.bySector, g_FDC.byTrack);
	}
	else if ((byCmd & 0xF0) == 0xB0) // 1011xxxx
	{
		sprintf(psz, "CMD: %02X WSEC: Multiple Record", byCmd);
	}
	else if (byCmd == 0xC4) // 11000100
	{
		sprintf(psz, "CMD: %02X Read Address", byCmd);
	}
	else if ((byCmd & 0xF0) == 0xD0) // 1101xxxx
	{
		sprintf(psz, "CMD: %02X Force Interrupt", byCmd);
	}
	else if ((byCmd & 0xFE) == 0xE4) // 1110010x
	{
		sprintf(psz, "CMD: %02X RTRK: %02X", byCmd, g_FDC.byTrack);
	}
	else if (byCmd == 0xF4) // 11110100
	{
		sprintf(psz, "CMD: %02X WTRK: %02X", byCmd, g_FDC.byTrack);
	}
	else
	{
		sprintf(psz, "CMD: %02X Unknown", byCmd);
	}
}
#endif

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_write_cmd)(byte byData)
{
#ifdef ENABLE_LOGGING
    char szBuf[64];
#endif

	g_FDC.byCommandReg = byData;

	if (g_byIntrRequest)
	{
		g_byIntrRequest = 0;
		g_byFdcIntrActive = false;
	}

	g_FDC.status.byBusy = 1;
	g_FDC.byStatus     |= F_BUSY;
	g_FDC.byCommandReceived = 1;

#ifdef ENABLE_LOGGING
	GetCommandText(szBuf, sizeof(szBuf), byData);
	puts(szBuf);
#endif
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_write_track)(byte byData)
{
	g_FDC.byTrack = byData;

#ifdef ENABLE_LOGGING
    printf("WR TRACK %02X\r\n", byData);
#endif
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_write_sector)(byte byData)
{
	g_FDC.bySector = byData;

	if (byData >= 0xE0)
	{
		g_FDC.byDoublerPrecomp = 1;
	}
	else if (byData >= 0xC0)
	{
		g_FDC.byDoublerPrecomp = 0;
	}
	else if (byData >= 0xA0)
	{
		g_FDC.byDoublerEnable  = 0;
		g_FDC.byDoublerDensity = 0;
	}
	else if (byData >= 0x80)
	{
		g_FDC.byDoublerEnable  = 1;
		g_FDC.byDoublerDensity = 1;
	}
	else if (byData >= 0x60)
	{
		g_FDC.byDoublerSide = 1;
	}
	else if (byData >= 0x40)
	{
		g_FDC.byDoublerSide = 0;
	}

#ifdef ENABLE_LOGGING
    printf("WR SECTOR %02X\r\n", byData);
#endif
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_write_data)(byte byData)
{
#ifdef ENABLE_LOGGING
	static char buf[128];
	static int  i = 0;
	char t[8];
#endif

	g_FDC.byData = byData;
	g_FDC.status.byDataRequest = 0;
	g_FDC.byStatus &= ~F_DRQ;

	if (g_tdTrack.nWriteCount > 0)
	{
		*g_tdTrack.pbyWritePtr = byData;
		++g_tdTrack.pbyWritePtr;
		--g_tdTrack.nWriteCount;

		if (g_tdTrack.nWriteCount > 0)
		{
			g_FDC.status.byDataRequest = 1;
			g_FDC.byStatus |= F_DRQ;
		}
		else
		{
			// FdcGenerateIntr();
			g_byIntrRequest   = 1;
			g_byFdcIntrActive = true;
			g_byEnableIntr    = true;

			// FdcClrFlag(eBusy);
			g_FDC.status.byBusy = 0;
			g_FDC.byStatus &= ~F_BUSY;
		}
	}

#ifdef ENABLE_LOGGING
	if (i == 0)
	{
		sprintf(buf, "WR DATA %02X", byData);
		++i;
	}
	else if (i == 7)
	{
		sprintf(t, " %02X", byData);
		strcat(buf, t);
		puts(buf);
		i = 0;
	}
	else
	{
		sprintf(t, " %02X", byData);
		strcat(buf, t);
		++i;
	}
#endif
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_get_status_string)(char* buf, int nMaxLen, BYTE byStatus)
{
	int nDrive;

	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	buf[0] = '|';
	buf[1] = 0;

	if ((nDrive < 0) || (g_dtDives[nDrive].f == NULL))
	{
		strcat_s(buf, sizeof(buf)-1, "F_NOTREADY|F_HEADLOAD");
	}
	else if ((g_FDC.byCommandType == 1) || // Restore, Seek, Step, Step In, Step Out
             (g_FDC.byCommandType == 4))   // Force Interrupt
	{
		// S0 (BUSY)
		if (byStatus & F_BUSY)
		{
			strcat_s(buf, nMaxLen, "F_BUSY|");
		}
		
		// S1 (INDEX) default to 0
		if (byStatus & F_INDEX)
		{
			strcat_s(buf, nMaxLen, "F_INDEX|");
		}

		// S2 (TRACK 0) default to 0
		if (byStatus & F_TRACK0)
		{
			strcat_s(buf, nMaxLen, "F_TRACK0|");
		}

		// S3 (CRC ERROR) default to 0
		if (byStatus & F_CRCERR)
		{
			strcat_s(buf, nMaxLen, "F_CRCERR|");
		}

		// S4 (SEEK ERROR) default to 0
		if (byStatus & F_SEEKERR)
		{
			strcat_s(buf, nMaxLen, "F_SEEKERR|");
		}
		
		if (byStatus & F_HEADLOAD)
		{
			strcat_s(buf, nMaxLen, "F_HEADLOAD|");
		}

		// S6 (PROTECTED) default to 0
		if (byStatus & F_PROTECTED)
		{
			strcat_s(buf, nMaxLen, "F_PROTECTED|");
		}
		
		// S7 (NOT READY) default to 0
		if (byStatus & F_NOTREADY)
		{
			strcat_s(buf, nMaxLen, "F_NOTREADY|");
		}
	}
	else if ((g_FDC.byCommandType == 2) ||	// Read Sector, Write Sector
			 (g_FDC.byCommandType == 3))	// Read Address, Read Track, Write Track
	{
		// S0 (BUSY)
		if (byStatus & F_BUSY)
		{
			strcat_s(buf, nMaxLen, "F_BUSY|");
		}
	
		// S1 (DATA REQUEST)     default to 0
		if (byStatus & F_DRQ)
		{
			strcat_s(buf, nMaxLen, "F_DRQ|");
		}

		// S2 (LOST DATA)        default to 0
		if (byStatus & F_LOSTDATA)
		{
			strcat_s(buf, nMaxLen, "F_LOSTDATA|");
		}
		
		// S3 (CRC ERROR)        default to 0
		if (byStatus & F_CRCERR)
		{
			strcat_s(buf, nMaxLen, "F_CRCERR|");
		}
		
		// S4 (RECORD NOT FOUND) default to 0
		if (byStatus & F_NOTFOUND)
		{
			strcat_s(buf, nMaxLen, "F_NOTFOUND|");
		}
	
		// S5 (RECORD TYPE) default to 0
		if (byStatus & F_DELETED)
		{
			strcat_s(buf, nMaxLen, "F_DELETED|");
		}

		// S6 (PROTECTED) default to 0
		if (byStatus & F_PROTECT)
		{
			strcat_s(buf, nMaxLen, "F_PROTECT|");
		}

		// S7 (NOT READY) default to 0
		if (byStatus & F_NOTREADY)
		{
			strcat_s(buf, nMaxLen, "F_NOTREADY|");
		}
	}
	else // Force Interrupt
	{
	}
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_read_status)(void)
{
#ifdef ENABLE_LOGGING
	static BYTE byPrevStatus = 0;
#endif

#ifdef ENABLE_LOGGING
	if (byPrevStatus != g_FDC.byStatus)
	{
		char buf[64];
		fdc_get_status_string(buf, sizeof(buf)-1, g_FDC.byStatus);
		printf("RD STATUS %02X CMD TYPE %d (%s)\r\n", g_FDC.byStatus, g_FDC.byCommandType, buf);
		byPrevStatus = g_FDC.byStatus;
	}
#endif
	if (g_byIntrRequest)
	{
		g_byIntrRequest = 0;
		g_byFdcIntrActive = false;
	}

	return g_FDC.byStatus;
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_read_track)(void)
{
#ifdef ENABLE_LOGGING
	printf("RD TRACK %02X\r\n", g_FDC.byTrack);
#endif
	return g_FDC.byTrack;
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_read_sector)(void)
{
#ifdef ENABLE_LOGGING
	printf("RD SECTOR %02X\r\n", g_FDC.bySector);
#endif
	return g_FDC.bySector;
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_read_data)(void)
{
#ifdef ENABLE_LOGGING
	static char buf[128];
	static int  i = 0;
	char t[8];
#endif
	if (g_tdTrack.nReadCount > 0)
	{
		g_FDC.byData = *g_tdTrack.pbyReadPtr;
		
		g_FDC.status.byDataRequest = 0;
		g_FDC.byStatus &= ~F_DRQ;

		g_tdTrack.pbyReadPtr += g_FDC.nDataSize;
		--g_tdTrack.nReadCount;

		if (g_tdTrack.nReadCount == 0)
		{
			if (g_FDC.byMultipleRecords)
			{
				++g_FDC.bySector;
#ifdef ENABLE_LOGGING
				printf("RD NEXT SECTOR %02X\r\n", g_FDC.bySector);
#endif
				g_FDC.byCommandReg = 0x98;

				if (g_byIntrRequest)
				{
					g_byIntrRequest = 0;
					g_byFdcIntrActive = false;
				}

				g_FDC.byCommandReceived = 1;
			}
		}
		else
		{
			g_FDC.status.byDataRequest = 1;
			g_FDC.byStatus |= F_DRQ;
		}
	}
	else
	{
		g_FDC.status.byDataRequest = 0;
		g_FDC.byStatus |= F_DRQ;
	}

#ifdef ENABLE_LOGGING
	if (i == 0)
	{
		sprintf(buf, "RD DATA %02X", g_FDC.byData);
		++i;
	}
	else if (i == 7)
	{
		sprintf(t, " %02X", g_FDC.byData);
		strcat(buf, t);
		puts(buf);
		i = 0;
	}
	else
	{
		sprintf(t, " %02X", g_FDC.byData);
		strcat(buf, t);
		++i;
	}
#endif

	return g_FDC.byData;
}

//-----------------------------------------------------------------------------
// B0 - Drive select 1
// B1 - Drive select 2
// B2 - Drive select 3
// B3 - Drive select 4
// B4 - SDSEL   (0-selects side 0; 1-selects side 1)
// B5 - PRECOMP (0-no write precompensation; 1-write precompensation enabled)
// B6 - WSGEN   (0-disable wait state generation; 1-enable wait state generation)
// B7 - FM/MFM  (0-select single density; 1-select double denisty)
void __not_in_flash_func(fdc_write_drive_select)(byte byData)
{
#ifdef ENABLE_LOGGING
	if (g_FDC.byDriveSel != byData)
	{
	    printf("WR DRVSEL %02X\r\n", byData);
	}
#endif

	g_FDC.byDriveSel = byData;
	g_dwMotorOnTimer = 2000000;
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_put_response_byte)(word addr, byte data)
{
	if (addr < FDC_CMD_SIZE)
	{
		g_bFdcResponse.cmd[addr] = data;
	}
	else
	{
		g_bFdcResponse.buf[addr-FDC_CMD_SIZE] = data;
	}
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_get_response_byte)(word addr)
{
	if (addr < FDC_CMD_SIZE)
	{
		return g_bFdcResponse.cmd[addr];
	}
	else
	{
		return g_bFdcResponse.buf[addr-FDC_CMD_SIZE];
	}
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_put_request_byte)(word addr, byte data)
{
	if (addr < FDC_CMD_SIZE)
	{
		g_bFdcRequest.cmd[addr] = data;
	}
	else
	{
		g_bFdcRequest.buf[addr-FDC_CMD_SIZE] = data;
	}
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_get_request_byte)(word addr)
{
	if (addr < FDC_CMD_SIZE)
	{
		return g_bFdcRequest.cmd[addr];
	}
	else
	{
		return g_bFdcRequest.buf[addr-FDC_CMD_SIZE];
	}
}
