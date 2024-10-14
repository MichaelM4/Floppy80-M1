#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "hardware/gpio.h"

#include "defines.h"
#include "datetime.h"
#include "file.h"
#include "system.h"
#include "crc.h"
#include "fdc.h"
#include "sd_core.h"

//#define ENABLE_LOGGING 1

////////////////////////////////////////////////////////////////////////////////////
/*

For JV1 and JV3 format information see https://www.tim-mann.org/trs80/dskspec.html

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
		bySectorData[SectorOffset+2] side number			(should be the same as the nSide parameter)
		bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
		bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

	For Single Density

		bySectorData[SectorOffset]   should be 0xFE
		bySectorData[SectorOffset+1] is track address (should be the same as the nTrack parameter)
		bySectorData[SectorOffset+2] side number			(should be the same as the nSide parameter)
		bySectorData[SectorOffset+3] sector number    (should be the same as the nSector parameter)
		bySectorData[SectorOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

DAM marker values:

						Single Density			Double Density
	--------------------------------------------
	0xFB			Normal data					Normal data
	0xFA			User-defined				Invalid
	0xF9			User-defines				Invalid
	0xF8			Deleted data				Deleted data

*/

////////////////////////////////////////////////////////////////////////////////////

char* g_pszVersion = {"0.0.2"};

FdcType       g_FDC;
FdcDriveType  g_dtDives[MAX_DRIVES];
TrackType     g_tdTrack;
SectorType    g_stSector;

uint64_t g_nMaxSeekTime;
uint32_t g_dwPrevTraceCycleCount = 0;
char     g_szBootConfig[80];
BYTE     g_byBootConfigModified;

byte g_byFdcRequestBuffer[FDC_REQUEST_SIZE];
byte g_byFdcResponseBuffer[FDC_RESPONSE_SIZE];

file*   g_fOpenFile;
DIR     g_dj;				// Directory object
FILINFO g_fno;				// File information
char    g_szBootConfig[80];
BYTE    g_byBootConfigModified;

char    g_szFindFilter[80];

#define FIND_MAX_SIZE 100

FILINFO g_fiFindResults[FIND_MAX_SIZE];
int     g_nFindIndex;
int     g_nFindCount;

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

//----------------------------------------------------------------------------
BYTE __not_in_flash_func(FdcGetCommandType)(BYTE byCommand)
{
	BYTE byType = 0;
	
	if (g_FDC.byDriveSel == 0x0F)
	{
		return 2;
	}

	switch (byCommand >> 4)
	{
		case 0: // Restore
			byType = 1;
			break;

		case 1: // Seek
			byType = 1;
			break;

		case 2: // Step (don't update track register)
		case 3: // Step (update track register)
			byType = 1;
			break;

		case 4: // Step In (don't update track register)
		case 5: // Step In (update track register)
			byType = 1;
			break;

		case 6: // Step Out (don't update track register)
		case 7: // Step Out (update track register)
			byType = 1;
			break;

		case 8: // Read Sector (single record)
		case 9: // Read Sector (multiple record)
			byType = 2;
			break;

		case 10: // Write Sector (single record)
		case 11: // Write Sector (multiple record)
			byType = 2;
			break;

		case 12: // Read Address
			byType = 3;
			break;

		case 13: // Force Interrupt
			byType = 4;
			break;

		case 14: // Read Track
			byType = 3;
			break;

		case 15: // Write Track
			byType = 3;
			break;
	}	
	
	return byType;
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
		
		byStatus |= F_HEADLOAD;

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
			byStatus |= F_BADDATA;
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

		case eIntrRequest:
			g_FDC.status.byIntrRequest = 1;
			g_byFdcIntrActive = true;
		    gpio_put(INT_PIN, 1); // activate intr
 			break;

	}

	FdcUpdateStatus();
}

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

		case eIntrRequest:
			g_FDC.status.byIntrRequest = 0;
			g_byFdcIntrActive = false;
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
int FdcGetIDAM_Index(int nSector)
{
	BYTE* pby;
	WORD  wIDAM;
	int   nSectorOffset;

	// get IDAM pointer for the specified track
	pby = g_tdTrack.byTrackData + nSector * 2;

	// get IDAM value for the specified track
	wIDAM = (*(pby+1) << 8) + *pby;

	// get offset from start of track to specified sector
	nSectorOffset = (wIDAM & 0x3FFF);

	return nSectorOffset;
}

//-----------------------------------------------------------------------------
// determines the index of the Data Address Mark for the specified locical sector.
//
// returns the index of the 0xFE byte in the sector byte sequence 0xA1, 0xA1, 0xA1, 0xFE
// in the g_tdTrack.byTrackData[] address for the specified sector.
//
int FdcGetSectorIDAM_Offset(int nSide, int nTrack, int nSector)
{
	BYTE* pby;
	int   i, nOffset;

	for (i = 0; i < 0x80; ++i)
	{
		nOffset = FdcGetIDAM_Index(i);

		// bySectorData[nOffset-3] should be 0xA1
		// bySectorData[nOffset-2] should be 0xA1
		// bySectorData[nOffset-1] should be 0xA1
		// bySectorData[nOffset]   should be 0xFE
		// bySectorData[nOffset+1] is track address (should be the same as the nTrack parameter)
		// bySectorData[nOffset+2] side number		(should be the same as the nSide parameter)
		// bySectorData[nOffset+3] sector number    (should be the same as the nSector parameter)
		// bySectorData[nOffset+4] byte length (log 2, minus seven), 0 => 128 bytes; 1 => 256 bytes; etc.

		pby = g_tdTrack.byTrackData + nOffset;

		if ((*(pby+1) == nTrack) && (*(pby+2) == nSide) && (*(pby+3) == nSector))
		{
			return nOffset;
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
int FdcGetDAM_Offset(TrackType* ptdTrack, int nIDAM)
{
	BYTE* pby;
	int   nDataOffset = FdcGetIDAM_Offset(nIDAM) + 7;
	
	if (nIDAM < 0)
	{
		return -1;
	}

	if (ptdTrack->byDensity == eDD) // double density
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
				++nDataOffset;
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
		ptdTrack->nIDAM[i] = FdcGetSectorIDAM_Offset(ptdTrack->nSide, ptdTrack->nTrack, i);
		ptdTrack->nDAM[i]  = FdcGetDAM_Offset(ptdTrack, ptdTrack->nIDAM[i]);
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
	int i;

	// locate sector
	for (i = 0; i < MAX_SECTORS_PER_TRACK; ++i)
	{
		if (ptrack->byTrackData[FdcGetIDAM_Offset(ptrack->nIDAM[i])+6] == nSector)
		{
			return i;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------
WORD FdcGetDmkSectorCRC(int nDrive, int nDataOffset, int nDensityAdjust)
{
	WORD wCRC16;

	wCRC16  = g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+1] << 8;
	wCRC16 += g_tdTrack.byTrackData[nDataOffset+g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+2];

	return wCRC16;
}

//-----------------------------------------------------------------------------
void FdcReadDmkSector(int nDriveSel, int nSide, int nTrack, int nSector)
{
	BYTE* pby;
	WORD  wCRC16;
	int   nDrive, nDataOffset, nDensityAdjust;

	nDrive = FdcGetDriveIndex(nDriveSel);
	
	if (nDrive < 0)
	{
		return;
	}

	FdcReadTrack(nDrive, nSide, nTrack);

	g_tdTrack.nFileOffset = FdcGetTrackOffset(nDrive, nSide, nTrack);

	// get pointer to start of sector data
	pby = g_tdTrack.byTrackData + FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector]);

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

	if (g_tdTrack.byDensity == eDD) // double density
	{
		nDensityAdjust = 3;
		wCRC16 = Calculate_CRC_CCITT(pby-3, 8);
	}
	else
	{
		nDensityAdjust = 0;
		wCRC16 = Calculate_CRC_CCITT(pby, 5);
	}
	
	if (wCRC16 != ((g_tdTrack.byTrackData[FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+5] << 8) + g_tdTrack.byTrackData[FdcGetIDAM_Offset(g_tdTrack.nIDAM[nSector])+6]))
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

	// for double denisty drives nDataOffset is the index of the first 0xA1 byte in the 0xA1, 0xA1, 0xA1, 0xFB/0xF8 sequence
	//
	// for single denisty 0xA1, 0xA1 and 0xA1 are not present, CRC starts st the data mark (0xFB/0xF8)

	g_FDC.byRecordMark           = g_tdTrack.byTrackData[nDataOffset+nDensityAdjust];
	g_stSector.nSectorDataOffset = nDataOffset + nDensityAdjust + 1;
	FdcClrFlag(eNotFound);
	FdcSetRecordType(0xFB);	// will get set to g_FDC.byRecordMark after a few status reads

	// perform a CRC on the sector data (including preceeding 4 bytes) and validate
	wCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], g_dtDives[nDrive].dmk.nSectorSize+nDensityAdjust+1);

	if (wCRC16 != FdcGetDmkSectorCRC(nDrive, nDataOffset, nDensityAdjust))
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
	g_stSector.nSectorSize       = 128 << g_tdTrack.byTrackData[FdcGetIDAM_Offset(g_tdTrack.nIDAM[i]) + 7];
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
			FdcReadDmkSector(nDriveSel, nSide, nTrack, nSector);
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
	if (strcmp(szLabel, "DRIVE0") == 0)
	{
		CopyString(psz, g_dtDives[0].szFileName, sizeof(g_dtDives[0].szFileName)-2);
	}
	else if (strcmp(szLabel, "DRIVE1") == 0)
	{
		CopyString(psz, g_dtDives[1].szFileName, sizeof(g_dtDives[1].szFileName)-2);
	}
	else if (strcmp(szLabel, "DRIVE2") == 0)
	{
		CopyString(psz, g_dtDives[2].szFileName, sizeof(g_dtDives[2].szFileName)-2);
	}
	else if (strcmp(szLabel, "DRIVE3") == 0)
	{
		CopyString(psz, g_dtDives[3].szFileName, sizeof(g_dtDives[3].szFileName)-2);
	}
}

//-----------------------------------------------------------------------------
void FdcLoadIni(void)
{
	file* f;
	char  szLine[256];
	char  szSection[16];
	char  szLabel[128];
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
	
	nLen = FileReadLine(f, (BYTE*)szLine, 126);
	
	while (nLen >= 0)
	{
		psz = SkipBlanks(szLine);
		
		if (*psz == '[')
		{
			CopySectionName(psz, szSection, sizeof(szSection)-1);
		}
		else if ((*psz != 0) && (*psz != ';')) // blank line or a comment line
		{
			StrToUpper(psz);
			psz = CopyLabelName(psz, szLabel, sizeof(szLabel)-1);
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

    memset(g_byFdcRequestBuffer, 0, sizeof(g_byFdcRequestBuffer));
    memset(g_byFdcResponseBuffer, 0, sizeof(g_byFdcResponseBuffer));
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

	g_nMaxSeekTime = 0;
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(FdcGenerateIntr)(void)
{
	BYTE byNmiMaskReg = g_FDC.byNmiMaskReg;

	g_FDC.byNmiStatusReg = 0x7F; // inverted state of all bits low except INTRQ

	FdcSetFlag(eIntrRequest);
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
	nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);
	
	if (nDrive != g_tdTrack.nDrive)
	{
		g_tdTrack.nDrive = -1;
	}

	if (g_FDC.byData >= g_dtDives[nDrive].byNumTracks)
	{
		FdcSetFlag(eSeekError);
		FdcClrFlag(eBusy);
		g_FDC.dwStateCounter   = 1;
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
	FdcClrFlag(eBusy);
	g_FDC.dwStateCounter   = 10;
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
// r1/r0 - steppeing motor rate
//
void FdcProcessStepInCommand(void)
{
	BYTE byData;
	int  nDrive;
	int  nStepRate;
	int  nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 1;
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
// Command code 1 0 0 m F2 E F1 0
//
// m  = 0 - single record read; 1 - multiple record read;
// F2 = 0 - compare for side 0; 1 - compare for side 1;
// E  = 0 - no delay; 1 - 15 ms delay;
// F1 = 0 - disable side select compare; 1 - enable side select compare;
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
	
	g_FDC.nReadStatusCount       = 0;
	g_FDC.dwStateCounter         = 1000;
	FdcClrFlag(eDataRequest);

	// number of byte to be transfered to the computer before
	// setting the Data Address Mark status bit (1 if Deleted Data)
	g_tdTrack.nReadSize     = g_stSector.nSectorSize;
	g_tdTrack.pbyReadPtr    = g_tdTrack.byTrackData + g_stSector.nSectorDataOffset;
	g_tdTrack.nReadCount    = g_tdTrack.nReadSize;
	g_FDC.nServiceState     = 0;
	g_FDC.nDataRegReadCount = 0;
	g_FDC.nProcessFunction  = psReadSector;

	// Note: computer now reads the data register for each of the sector data bytes
	//       once the last data byte is transfered status bit-5 is set if the
	//       Data Address Mark corresponds to a Deleted Data Mark.
	//
	//       Actual data transfer in handle in the FdcServiceRead() function.
}

//-----------------------------------------------------------------------------
// Command code 1 0 0 m F2 E F1 a0
//
// m  = 0 - single record read; 1 - multiple record read;
// F2 = 0 - compare for side 0; 1 - compare for side 1;
// E  = 0 - no delay; 1 - 15 ms delay;
// F1 = 0 - disable side select compare; 1 - enable side select compare;
// a0 = 0 - 0xFB (Data Mark); 1 - 0xF8 (Deleted Data Mark);
//
void FdcProcessWriteSectorCommand(void)
{
	int nSide  = FdcGetSide(g_FDC.byDriveSel);
	int nDrive = FdcGetDriveIndex(g_FDC.byDriveSel);

	g_FDC.byCommandType = 2;
	FdcSetRecordType(0xFB);
	g_FDC.nReadStatusCount = 0;

	// read specified sector so that it can be modified
	FdcReadSector(g_FDC.byDriveSel, nSide, g_FDC.byTrack, g_FDC.bySector);

	FdcClrFlag(eDataRequest);
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
// Command code 1 1 0 0 0 E 0 0
//
// E = 1 - 15ms delay; 0 - no 15ms delay;
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

	g_tdTrack.pbyReadPtr = &g_tdTrack.byTrackData[FdcGetIDAM_Index(0) + 1];
	g_tdTrack.nReadSize  = 6;
	g_tdTrack.nReadCount = 6;

	g_FDC.nReadStatusCount = 0;
	g_FDC.dwStateCounter   = 1000;
	FdcClrFlag(eDataRequest);

	// number of byte to be transfered to the computer before
	// setting the Data Address Mark status bit (1 if Deleted Data)
	g_FDC.nServiceState     = 0;
	g_FDC.nDataRegReadCount = 0;
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
// Command code 1 1 1 0 0 E 0 0
//
// E = 1 - 15ms delay; 0 - no 15ms delay;
//
void FdcProcessReadTrackCommand(void)
{
	g_FDC.byCommandType = 3;
}

//-----------------------------------------------------------------------------
// Command code 1 1 1 1 0 E 0 0
//
// E = 1 - 15ms delay; 0 - no 15ms delay;
//
void FdcProcessWriteTrackCommand(void)
{
	int nSide = FdcGetSide(g_FDC.byDriveSel);

	g_FDC.byCommandType = 3;
	
	memset(g_tdTrack.byTrackData+0x80, 0, sizeof(g_tdTrack.byTrackData)-0x80);
	
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
	g_FDC.nReadStatusCount = 0;
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
	g_FDC.nReadStatusCount = 0;
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
			FdcProcessReadAddressCommand();
			break;

		case 13: // Force Interrupt							(Type 4 Command)
			FdcProcessForceInterruptCommand();
			break;

		case 14: // Read Track								(Type 3 Command)
			FdcProcessReadTrackCommand();
			break;

		case 15: // Write Track								(Type 3 Command)
			FdcProcessWriteTrackCommand();
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
			g_FDC.nReadStatusCount = 0;
			g_FDC.dwStateCounter = 5;
			++g_FDC.nServiceState;
			break;

		case 1: // give host time to get ready for data
			if (g_FDC.dwStateCounter > 0)
			{
				--g_FDC.dwStateCounter;
				break;
			}

			FdcSetRecordType(g_FDC.byRecordMark);
			FdcGenerateDRQ();
			g_FDC.dwStateCounter = 5;
			++g_FDC.nServiceState;
			break;

		case 2:
			if (g_tdTrack.nReadCount > 0)
			{
				break;
			}

			g_FDC.nReadStatusCount = 0;
			++g_FDC.nServiceState;
			FdcClrFlag(eBusy);
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
		wCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], nSectorSize+4);
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+4] = wCRC16 >> 8;
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+5] = wCRC16 & 0xFF;
	}
	else // single density
	{
		wCRC16 = Calculate_CRC_CCITT(&g_tdTrack.byTrackData[nDataOffset], nSectorSize+1);
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+1] = wCRC16 >> 8;
		g_tdTrack.byTrackData[nDataOffset+nSectorSize+2] = wCRC16 & 0xFF;
	}
}

//-----------------------------------------------------------------------------
void FdcUpdateDataAddressMark(int nSector, int nSectorSize)
{
	int  nDataOffset, i;

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
			if ((g_FDC.nReadStatusCount < 25) && (g_FDC.dwStateCounter > 0))
			{
				--g_FDC.dwStateCounter;
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
			g_FDC.dwStateCounter = 5;
			break;
		
		case 2:
			if (g_FDC.dwStateCounter > 0)
			{
    			--g_FDC.dwStateCounter;
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
// returns the side the track is specified to be on
void FdcProcessTrackData(TrackType* ptdTrack)
{
	BYTE* pbyCrcStart = ptdTrack->byTrackData;
	BYTE* pby = ptdTrack->byTrackData + 0x80;
	WORD  wCRC16;
	int   i, nSide = 0;

	for (i = 0x80; i < ptdTrack->nTrackSize; ++i)
	{
		switch (*pby)
		{
			case 0xF5:
				pbyCrcStart = pby;
				*pby = 0xA1;
				break;
			
			case 0xF6:
				*pby = 0xC2;
				break;

			case 0xF7:
				if (ptdTrack->byDensity == eDD)
				{
					wCRC16 = Calculate_CRC_CCITT(pbyCrcStart-2, (int)(pby-pbyCrcStart+2));
				}
				else
				{
					wCRC16 = Calculate_CRC_CCITT(pbyCrcStart, (int)(pby-pbyCrcStart));
				}

				*pby = wCRC16 >> 8;
				++pby;
				++i;
				*pby = wCRC16 & 0xFF;
				break;

			case 0xFB:
				if (ptdTrack->byDensity == eSD) // single density
				{
					pbyCrcStart = pby;
				}

				break;

			case 0xFE:
				if (ptdTrack->byDensity == eSD) // single density
				{
					pbyCrcStart = pby;
					ptdTrack->nSide = *(pby+2);
				}

				break;
		}
		
		++pby;
	}
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
void FdcBuildDamTable(TrackType* ptdTrack)
{
	int i;
	
	for (i = 0; i < 0x80; ++i)
	{
		ptdTrack->nDAM[i] = FdcGetDAM_Offset(ptdTrack, ptdTrack->nIDAM[i]);
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
			if ((g_FDC.nReadStatusCount < 25) && (g_FDC.dwStateCounter > 0))
			{
				--g_FDC.dwStateCounter;
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

			FdcProcessTrackData(&g_tdTrack);	// scan track data to generate CRC values
			FdcBuildIdamTable(&g_tdTrack);		// scan track data to build the IDAM table
			FdcBuildDamTable(&g_tdTrack);

			// flush track to SD-Card
			FdcWriteTrack(&g_tdTrack);
		
			g_FDC.dwStateCounter = 5;
			++g_FDC.nServiceState;
			break;

		case 2:
			if (g_FDC.dwStateCounter > 0)
			{
				--g_FDC.dwStateCounter;
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
void FdcServiceSeeek(void)
{
	if (g_FDC.dwStateCounter > 0)
	{
		--g_FDC.dwStateCounter;
		return;
	}

	FdcGenerateIntr();
	g_FDC.nProcessFunction = psIdle;
}

//-----------------------------------------------------------------------------
// primary data transfer is handled in fdc_isr()
void FdcServiceSendData(void)
{
	if (g_FDC.dwStateCounter > 0) // don't wait forever
	{
	--g_FDC.dwStateCounter;
		return;
	}

	FdcClrFlag(eDataRequest);
	FdcClrFlag(eBusy);
	g_FDC.nProcessFunction = psIdle;
}

//-----------------------------------------------------------------------------
void SetResponseLength(byte* byResponse)
{
	int i = strlen((char*)(byResponse+2));
	byResponse[0] = i & 0xFF;
	byResponse[1] = (i >> 8) & 0xFF;
}

//-----------------------------------------------------------------------------
void FdcProcessStatusRequest(void)
{
	char szBuf[64];
	int  i;
	
    memset(g_byFdcResponseBuffer, 0, sizeof(g_byFdcResponseBuffer));

	g_byFdcResponseBuffer[0] = 0;
	g_byFdcResponseBuffer[1] = 0;

	strcpy((char*)(g_byFdcResponseBuffer+2), "Pico FDC Version ");
	strcat((char*)(g_byFdcResponseBuffer+2), g_pszVersion);
	strcat((char*)(g_byFdcResponseBuffer+2), "\r");
	strcat((char*)(g_byFdcResponseBuffer+2), "BootIni=");
	strcat((char*)(g_byFdcResponseBuffer+2), g_szBootConfig);
	strcat((char*)(g_byFdcResponseBuffer+2), "\r");

	if (g_byBootConfigModified)
	{
		file* f;
		char  szLine[256];
		char  szSection[16];
		char  szLabel[128];
		char* psz;
		int   nLen, nLeft, nRead;

		f = FileOpen(g_szBootConfig, FA_READ);
		
		if (f == NULL)
		{
			strcat((char*)(g_byFdcResponseBuffer+2), "Unable to open specified ini file");
		}
		else
		{
			nLen = FileReadLine(f, (BYTE*)szLine, nRead);
			
			while (nLen >= 0)
			{
				if (nLen > 2)
				{
					strcat_s((char*)(g_byFdcResponseBuffer+2),  sizeof(g_byFdcResponseBuffer)-2, szLine);
					strcat_s((char*)(g_byFdcResponseBuffer+2),  sizeof(g_byFdcResponseBuffer)-2, "\r");
				}

				nLen = FileReadLine(f, (BYTE*)szLine, 126);
			}
			
			FileClose(f);
		}
	}
	else
	{
		for (i = 0; i < MAX_DRIVES; ++i)
		{
			sprintf(szBuf, "%d: ", i);
			strcat((char*)(g_byFdcResponseBuffer+2), szBuf);
			strcat((char*)(g_byFdcResponseBuffer+2), g_dtDives[i].szFileName);
			strcat((char*)(g_byFdcResponseBuffer+2), "\r");
		}
	}

	SetResponseLength(g_byFdcResponseBuffer);
}

//-----------------------------------------------------------------------------
int FdcFileListCmp(const void * a, const void * b)
{
	FILINFO* f1 = (FILINFO*) a;
	FILINFO* f2 = (FILINFO*) b;

	return stricmp(f1->fname, f2->fname);
}

//-----------------------------------------------------------------------------
void FdcProcessFindFirst(char* pszFilter)
{
    FRESULT fr;  // Return value
	int i;
	
	g_nFindIndex = 0;
	g_nFindCount = 0;

    memset(g_byFdcResponseBuffer, 0, sizeof(g_byFdcResponseBuffer));

	strcpy((char*)(g_byFdcResponseBuffer+2), "too soon");

    memset(&g_dj, 0, sizeof(g_dj));
    memset(&g_fno, 0, sizeof(g_fno));
	memset(g_fiFindResults, 0, sizeof(g_fiFindResults));

	strcpy(g_szFindFilter, pszFilter);

    fr = f_findfirst(&g_dj, &g_fno, "0:", "*");

    if (FR_OK != fr)
	{
		strcpy((char*)(g_byFdcResponseBuffer+2), "No matching file found.");
        SetResponseLength(g_byFdcResponseBuffer);
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

		sprintf((char*)(g_byFdcResponseBuffer+2), "%2d/%02d/%d %7d ",
				((g_fiFindResults[g_nFindIndex].fdate >> 5) & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate >> 9) + 1980,
				g_fiFindResults[g_nFindIndex].fsize);
        strcat((char*)(g_byFdcResponseBuffer+2), (char*)g_fiFindResults[g_nFindIndex].fname);

		++g_nFindIndex;
	}

    SetResponseLength(g_byFdcResponseBuffer);
}

//-----------------------------------------------------------------------------
void FdcProcessFindNext(void)
{
    FRESULT fr = FR_OK;  /* Return value */
	BYTE    bFound = FALSE;
	
    memset(g_byFdcResponseBuffer, 0, sizeof(g_byFdcResponseBuffer));

	if (g_nFindIndex < g_nFindCount)
	{
		sprintf((char*)(g_byFdcResponseBuffer+2), "%2d/%02d/%d %7d ",
				((g_fiFindResults[g_nFindIndex].fdate >> 5) & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate & 0xF) + 1,
				(g_fiFindResults[g_nFindIndex].fdate >> 9) + 1980,
				g_fiFindResults[g_nFindIndex].fsize);
        strcat((char*)(g_byFdcResponseBuffer+2), (char*)g_fiFindResults[g_nFindIndex].fname);

		++g_nFindIndex;
	}
	
    SetResponseLength(g_byFdcResponseBuffer);
}

//-----------------------------------------------------------------------------
void FdcProcessRequest(void)
{
    switch (g_byFdcRequestBuffer[0])
    {
        case 0: // do nothing
            break;

        case 1: // put status in response buffer
            FdcProcessStatusRequest();
            break;

        case 3: // find next file
            FdcProcessFindNext();
            break;

        case 0x80:
			FdcProcessFindFirst(".INI");
            break;

        case 0x81:
			FdcProcessFindFirst(".DMK");
            break;

        case 0x82:
			FdcProcessFindFirst(".HFE");
            break;
    }
}

//-----------------------------------------------------------------------------
void FdcServiceStateMachine(void)
{
	TestSdCardInsertion();

    if (g_byFdcRequestBuffer[0] != 0)
    {
        FdcProcessRequest();
        g_byFdcRequestBuffer[0] = 0;
        return;
    }

	// check if we have a command to process
	if (g_FDC.byCommandReceived != 0)
	{
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
		
		case psWriteSector:
			FdcServiceWriteSector();
			break;
		
		case psWriteTrack:
			FdcServiceWriteTrack();
			break;
		
		case psSendData:
			FdcServiceSendData();
			break;

		case psSeek:
			FdcServiceSeeek();
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
		strcpy(psz, "    FDC CMD: Restore\r\n");
	}
	else if ((byCmd & 0xF0) == 0x10) // 0001xxxx
	{
		sprintf(psz, "    FDC CMD: SEEK 0x%02X, From 0x%02X\r\n", g_FDC.byData, g_FDC.byTrack);
	}
	else if ((byCmd & 0xF0) == 0x20) // 0010xxxx
	{
		strcpy(psz, "    FDC CMD: Step, Do Not Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x30) // 0011xxxx
	{
		strcpy(psz, "    FDC CMD: Step, Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x40) // 0100xxxx
	{
		strcpy(psz, "    FDC CMD: Step In, Do Not Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x50) // 0101xxxx
	{
		strcpy(psz, "    FDC CMD: Step In, Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x60) // 0110xxxx
	{
		strcpy(psz, "    FDC CMD: Step Out, Do Not Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x70) // 0111xxxx
	{
		strcpy(psz, "    FDC CMD: Step Out, Update Track Register\r\n");
	}
	else if ((byCmd & 0xF0) == 0x80) // 1000xxxx
	{
		sprintf(psz, "    FDC CMD: DRV: 0x%02X TRK: 0x%02X RSEC: 0x%02X\r\n", g_FDC.byDriveSel, g_FDC.byTrack, g_FDC.bySector);
	}
	else if ((byCmd & 0xF0) == 0x90) // 1001xxxx
	{
		strcpy(psz, "    FDC CMD: RSEC: Multiple Record\r\n");
	}
	else if ((byCmd & 0xF0) == 0xA0) // 1010xxxx
	{
		sprintf(psz, "    FDC CMD: WSEC: 0x%02X TRK: 0x%02X\r\n", g_FDC.bySector, g_FDC.byTrack);
	}
	else if ((byCmd & 0xF0) == 0xB0) // 1011xxxx
	{
		strcpy(psz, "    FDC CMD: WSEC: Multiple Record\r\n");
	}
	else if ((byCmd & 0xF0) == 0xC0) // 1100xxxx
	{
		strcpy(psz, "    FDC CMD: Read Address\r\n");
	}
	else if ((byCmd & 0xF0) == 0xD0) // 1101xxxx
	{
		sprintf(psz, "    FDC CMD: Force Interrupt (0x%02X)\r\n", byCmd);
	}
	else if ((byCmd & 0xF0) == 0xE0) // 1110xxxx
	{
		sprintf(psz, "    FDC CMD: RTRK: 0x%02X\r\n", g_FDC.byTrack);
	}
	else if ((byCmd & 0xF0) == 0xF0) // 1110xxxx
	{
		sprintf(psz, "    FDC CMD: WTRK: 0x%02X\r\n", g_FDC.byTrack);
	}
	else
	{
		strcpy(psz, "    FDC CMD: Unknown\r\n");
	}
}
#endif

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_write)(word addr, byte byData)
{
	WORD wReg, wCom;
#ifdef ENABLE_LOGGING
    char szBuf[128];
#endif
	wReg = addr & 0x03;

	switch (wReg)
	{
		case 0: // command register
			g_FDC.byCommandReg   = byData;
			g_FDC.byCommandType  = FdcGetCommandType(byData);
			g_FDC.byNmiStatusReg = 0xFF;

			if (g_FDC.status.byIntrRequest)
			{
				g_FDC.byNmiStatusReg = 0xFF; // inverted state of all bits low except INTRQ
				FdcClrFlag(eIntrRequest);
			}

    		g_FDC.byCommandReceived = 1;

            wCom = 0;

            while ((g_FDC.byCommandReceived == 1) && (wCom < 1000)) // wait for main task to recognize reception of command (don't wait forever)
            {
                ++wCom;
            }

#ifdef ENABLE_LOGGING
			GetCommandText(szBuf, sizeof(szBuf), byData);
#endif

			break;

		case 1: // track register
			g_FDC.byTrack = byData;

#ifdef ENABLE_LOGGING
		    sprintf(szBuf, "    FDC WRITE TRACK %02X\r\n", byData);
#endif

			break;

		case 2: // sector register
			g_FDC.bySector = byData;

#ifdef ENABLE_LOGGING
		    sprintf(szBuf, "    FDC WRITE SECTOR %02X\r\n", byData);
#endif

			break;

		case 3: // data register
			g_FDC.byData = byData;
			FdcClrFlag(eDataRequest);

			if (g_tdTrack.nWriteCount > 0)
			{
				*g_tdTrack.pbyWritePtr = byData;
				++g_tdTrack.pbyWritePtr;
				--g_tdTrack.nWriteCount;

				if (g_tdTrack.nWriteCount > 0)
				{
					FdcGenerateDRQ();
				}
			}

#ifdef ENABLE_LOGGING
		    sprintf(szBuf, "    FDC WRITE DATA %02X\r\n", byData);
#endif

			break;
	}

#ifdef ENABLE_LOGGING
	puts(szBuf);
#endif
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_read)(word wAddr)
{
	WORD wReg;
	BYTE byData = 0;
#ifdef ENABLE_LOGGING
    char szBuf[128];
#endif

	wReg = wAddr & 0x03;

	switch (wReg)
	{
		case 0:
			byData = g_FDC.byStatus;

#ifdef ENABLE_LOGGING
			printf("    FDC READ STATUS %02X\r\n", byData);
#endif

			++g_FDC.nReadStatusCount;

			if (g_FDC.status.byIntrRequest)
			{
				g_FDC.byNmiStatusReg = 0xFF; // inverted state of all bits low except INTRQ
				FdcClrFlag(eIntrRequest);
			}

			break;

		case 1:
			byData = g_FDC.byTrack;

#ifdef ENABLE_LOGGING
    		printf("    FDC READ TRACK %02X\r\n", byData);
#endif
			break;

		case 2:
			byData = g_FDC.bySector;

#ifdef ENABLE_LOGGING
			printf("    FDC READ SECTOR %02X\r\n", byData);
#endif

			break;

		case 3:
			if (g_tdTrack.nReadCount > 0)
			{
				byData = g_FDC.byData = *g_tdTrack.pbyReadPtr;
				FdcClrFlag(eDataRequest);
				++g_FDC.nDataRegReadCount;

				++g_tdTrack.pbyReadPtr;
				--g_tdTrack.nReadCount;

				if (g_tdTrack.nReadCount == 0)
				{
					g_FDC.nDataRegReadCount = 0;
					FdcGenerateIntr();
				}
				else
				{
					FdcGenerateDRQ();
				}
			}
			else
			{
				byData = g_FDC.byData;
				FdcClrFlag(eDataRequest);
				++g_FDC.nDataRegReadCount;
			}

#ifdef ENABLE_LOGGING
		  	printf("    FDC READ DATA %02X\r\n", byData);
#endif

			break;
	}

	return byData;
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
    printf("    FDC  WR DRVSEL %02X\r\n", byData);
#endif

	g_FDC.byDriveSel = byData;

	// do not allow a wait output if WAITTIMEOUT, INTRQ or DRQ are active
	if (g_FDC.status.byIntrRequest == 0)
	{
		// in "real" hardware the wait is activated on rising edge of the DRV_SEL input
		// here it is already activated by the PIO code on every FDC read/write operation
		if ((byData & 0x40) != 0) // activate WAIT
		{
			g_FDC.dwWaitTimeoutCount = 2000;
			g_FDC.byWaitOutput = 1;
		}
	}

	FdcGetDriveIndex(byData);
	g_FDC.dwMotorOnTimer = 2000000;
}

//-----------------------------------------------------------------------------
// g_byFdcRequestBuffer[0]
//   0x01 - get status
void __not_in_flash_func(fdc_request)(word addr, byte data)
{
    if ((addr < FDC_REQUEST_ADDR_START) || (addr > FDC_REQUEST_ADDR_STOP)) // fdc.cmd request area
    {
        return;
    }

    addr -= FDC_REQUEST_ADDR_START;
    g_byFdcRequestBuffer[addr] = data;
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(fdc_response)(word addr)
{
    if ((addr < FDC_RESPONSE_ADDR_START) || (addr > FDC_RESPONSE_ADDR_STOP)) // fdc.cmd request area
    {
        return 0;
    }

    addr -= FDC_RESPONSE_ADDR_START;
    return g_byFdcResponseBuffer[addr];
}
