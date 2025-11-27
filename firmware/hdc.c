#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef MFC
	#include "hardware/gpio.h"
	#include "pico/stdlib.h"
	#include "sd_core.h"
#endif

#include "tusb.h"

#include "defines.h"
#include "file.h"
#include "system.h"
#include "crc.h"
#include "hdc.h"

// Model I ports
// 0xC0 - Write Protection.
//        Write/Output: Reserved.
//		  Read/Input: Hard disk write protect:
//
//			Bit 0 (INTRQ): Interrupt Request
//			Bit 1 (HWPL): If set, at least one hard drive is currently write protected
//			Bit 4 (WPD4): If set, hard drive 4 is currently write protected
//			Bit 5 (WPD3): If set, hard drive 3 is currently write protected
//			Bit 6 (WPD2): If set, hard drive 2 is currently write protected
//			Bit 7 (WPD1): If set, hard drive 1 is currently write protected
//
// 0xC1 - Hard disk controller board control register (Read/Write).
//
//			Bit 2: RUMORED to enable wait state support on a 8X300 Controller Board
//			Bit 3: If set, enable controller
//			Bit 4: If set, reset controller
//
// 0xC9 - Hard Disk Write Pre-Comp Cyl.
//		  Register 1 for WD1010 Winchester Disk Controller Chip.
//		  Write/Output: The RWC start cylinder number = The value stored here divide by 4.
//		  Read/Input: Error Register:
//
//			Bit 0: Per the WD1010-00 Spec Sheet, this is DAM Not Found. The Radio Shack 15M HD Service Mauals says that this bit is reserved and forced to 0
//			Bit 1: Track 0 Error (Restore Command)
//			Bit 2: Aborted Command
//			Bit 4: ID Not Found Error
//			Bit 5: CRC Error – ID Field
//			Bit 6: CRC Error – Data Field
//			Bit 7: Bad Block Detected
//
// 0xCB - Hard Disk Sector Number (Read/Write).
//		  Register 3 for WD1010 Winchester Disk Controller Chip.
//
// 0xCC - Hard Disk Cylinder LSB (Read/Write).
//		  Register 4 for WD1010 Winchester Disk Controller Chip.
//
// 0xCD - Hard Disk Cylinder MSB (Read/Write).
//		  Register 5 for WD1010 Winchester Disk Controller Chip.
//		  Since the maximum number of cylinders is 1024, only Bits 0 and 1 are used (1023 = 0000 0011 + 1111 1111).
//
// 0xCE - Hard Disk Sector Size / Drive # / Head # (Read/Write).
//		  Register 6 for WD1010 Winchester Disk Controller Chip.
//
//			Bits 0-2: Head Number (0-7)
//			Bits 3-4: Drive Number (00=DSEL1, 01=DSEL2, 10=DSEL3, 11=DSEL 4)
//			Bits 5-6: Sector Size (00=256, 01=512, 10=1024, 11=128)
//			Bit 7: Extension (if this is set, Error Checking and Correction codes are in use and the R/W data [sector length + 7 bytes] do not check or generate CRC)
//
// 0xCF - Register 7 for WD1010 Winchester Disk Controller Chip.
//		  Read = Status Register:
//
//			Bit 0: Error Exists (just an OR of Bits 1-7)
//			Bit 1: Command in Progress
//			Bit 2: Reserved (so forced to 0)
//			Bit 3: Data Request
//			Bit 4: Seek Complete
//			Bit 5: Write Fault
//			Bit 6: Drive Ready
//			Bit 7: Busy
//
//		  Write = Command Register.

HdcType Hdc;
VhdType Vhd[MAX_VHD_DRIVES];

static int g_nSectorSizes[] = {256, 512, 1024, 128};

//-----------------------------------------------------------------------------
void HdcInitFileName(int nDrive, char* pszFileName)
{
	if ((nDrive >= 0) && (nDrive < MAX_VHD_DRIVES))
	{
		strcpy_s(Vhd[nDrive].szFileName, sizeof(Vhd[nDrive].szFileName), pszFileName);
	}
}

//-----------------------------------------------------------------------------
void HdcInit(void)
{
	int nSectors, i;

	memset(&Hdc, 0, sizeof(Hdc));
	Hdc.byStatusRegister |= STATUS_MASK_DRIVE_READY;

	for (i = 0; i < MAX_VHD_DRIVES; ++i)
	{
		if (Vhd[i].szFileName[0] != 0)
		{
			Vhd[i].f = FileOpen(Vhd[i].szFileName, FA_OPEN_EXISTING | FA_READ | FA_WRITE);

			memset(Vhd[i].byHeader, 0, sizeof(Vhd[i].byHeader));

			if (Vhd[i].f != NULL)
			{
				FileRead(Vhd[i].f, Vhd[i].byHeader, sizeof(Vhd[i].byHeader));

				Vhd[i].nHeads     = Vhd[i].byHeader[26];
				Vhd[i].nCylinders = ((Vhd[i].byHeader[27] & 0x07) << 8) + Vhd[i].byHeader[28];

				nSectors = Vhd[i].byHeader[29];

				if (nSectors == 0)
				{
					nSectors = 256;
				}

				if (Vhd[i].nHeads == 0)
				{
					Vhd[i].nSectors = VHD_DEFAULT_SECTORS;
					Vhd[i].nHeads   = nSectors / Vhd[i].nSectors;
				}
				else
				{
					Vhd[i].nSectors = nSectors / Vhd[i].nHeads;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void HdcCreateVhd(char* pszFileName, int nHeads, int nCylinders, int nSectors)
{
	file* f;
	int i, j, k;

	memset(Hdc.bySectorBuffer, 0, sizeof(Hdc.bySectorBuffer));
	Hdc.bySectorBuffer[0]  = 0x56;	 // Magic number Byte 0
	Hdc.bySectorBuffer[1]  = 0xCB;	 // Magic number Byte 1
	Hdc.bySectorBuffer[2]  = 0x10;	 // Format version (0x10 = 1.0)
	Hdc.bySectorBuffer[3]  = 0;		 // Checksum (not used)
	Hdc.bySectorBuffer[4]  = 1;		 // Must me 1
	Hdc.bySectorBuffer[5]  = 4;		 // Must be 4
	Hdc.bySectorBuffer[6]  = 0;		 // Media type (0 = hard drive)
	Hdc.bySectorBuffer[7]  = 0;		 // Write protection (0x80 = write protected; 0 = not write protected)
	Hdc.bySectorBuffer[8]  = 0;		 // Flags: bit 0 = auto boot; bit 1 - 7 reserved;
	Hdc.bySectorBuffer[9]  = 0;		 // Reserved
	Hdc.bySectorBuffer[10] = 0;		 // Reserved
	Hdc.bySectorBuffer[11] = 1;		 // DOS type (only needed for auto boot)
									 //   0 = Model 4 LSDOS
									 //   1 = Model I/III (LSDOS)
									 //   2 = CP/M
									 //   3 = NEWDOS
	// bytes 12-25 reserved
	Hdc.bySectorBuffer[26] = nHeads; // If non-zero, number of heads per cylinder
									 // If zero, number of heads per cylinder is calculated as
									 // number of sectors per cylinder + (byte 29) divided by 32.

	Hdc.bySectorBuffer[27] = nCylinders >> 8;	// Number of cylinders per disk (high 3 bits)
	Hdc.bySectorBuffer[28] = nCylinders	& 0xFF; // Number of cylinders per disk (lower 8 bits)
												// This is the number of cylinders on the drive. which shouldn’t be higher than 1024.
												// To preserve backwards compatibility, values of 0 in both bytes 27 and 28 means 256.
	Hdc.bySectorBuffer[29] = nSectors;			// Number of sectors per cylinder

	// 30 		Number of granules per track (deprecated)
	// 31 		Directory cylinder (deprecated, should be 1)
	// 32–71 	Reserved
	// 72-103 	Reserved for storage of auto-boot data
	// 104-255 Reserved 	

	f = FileOpen(pszFileName, FA_CREATE_ALWAYS | FA_WRITE);

	if (f == NULL)
	{
		printf("Failed to create virtual hard disk file: %s\r\n", pszFileName);
		return;
	}

	FileWrite(f, Hdc.bySectorBuffer, VHD_HEADER_SIZE);
	FileClose(f);

	puts("Done");
}

//-----------------------------------------------------------------------------
uint32_t HdcGetSectorOffset(void)
{
	uint32_t nCylinder = (Hdc.byHighCylinderRegister << 8) + Hdc.byLowCylinderRegister;
	uint32_t nOffset = nCylinder * Vhd[Hdc.byDriveSel].nHeads * Vhd[Hdc.byDriveSel].nSectors * Hdc.nSectorSize +
	                   Hdc.byHeadSel * Vhd[Hdc.byDriveSel].nSectors * Hdc.nSectorSize +
					   Hdc.bySectorNumberRegister * Hdc.nSectorSize + 256;

	return nOffset;
}

//-----------------------------------------------------------------------------
void HdcDumpDisk(int nDrive)
{
	byte  byBuf[256];
	byte* pby;
	int   head = 0, cyl = 0, sec = 0;
	int   i = 0, j = 0, k = 0;

	if (nDrive >= MAX_VHD_DRIVES)
	{
		puts("Invalid drive index");
		return;
	}

	if (Vhd[nDrive].f == NULL)
	{
		puts("Drive not mounted");
		return;
	}

	FileSeek(Vhd[nDrive].f, 256);

	for (cyl = 0; cyl < Vhd[nDrive].nCylinders; ++cyl)
	{
		for (head = 0; head < Vhd[nDrive].nHeads; ++head)
		{
			for (sec = 0; sec < Vhd[nDrive].nSectors; ++sec)
			{
				FileRead(Vhd[Hdc.byDriveSel].f, byBuf, sizeof(byBuf));
			
                printf("HEAD: %02X CYL: %02X SEC: %02X\r\n", head, cyl, sec);
    	        while (tud_cdc_write_available() < 56);

				i = 0;

				for (j = 0; j < 16; ++j)
				{
					for (k = 0; k < 16; ++k)
					{
						printf("%02X ", byBuf[i]);
						++i;
					}

					printf("\r\n");
	    	        while (tud_cdc_write_available() < 56);
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void ProcessActiveCommand(void)
{
	uint32_t nOffset, i;

	if ((Hdc.byDriveSel >= MAX_VHD_DRIVES) || (Vhd[Hdc.byDriveSel].f == NULL))
	{
		Hdc.byActiveCommand = 0;
		Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
		return;
	}

	switch (Hdc.byActiveCommand >> 4)
	{
		case 0x03: // Write Sector
			if (Hdc.nWriteCount > 0)
			{
				break;
			}

			nOffset = HdcGetSectorOffset();

			FileSeek(Vhd[Hdc.byDriveSel].f, nOffset);
			FileWrite(Vhd[Hdc.byDriveSel].f, Hdc.bySectorBuffer, Hdc.nSectorSize);
			FileFlush(Vhd[Hdc.byDriveSel].f);

			Hdc.byActiveCommand = 0;
			Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
			break;

		case 0x05: // Format Track
			if (Hdc.nWriteCount > 0)
			{
				break;
			}

			nOffset = HdcGetSectorOffset();

			FileSeek(Vhd[Hdc.byDriveSel].f, nOffset);

			while (Hdc.bySectorCountRegister > 0)
			{
				FileWrite(Vhd[Hdc.byDriveSel].f, Hdc.bySectorBuffer, Hdc.nSectorSize);
				--Hdc.bySectorCountRegister;
			}

			FileFlush(Vhd[Hdc.byDriveSel].f);
			// FileTruncate(g_fVhd);

			Hdc.byActiveCommand = 0;
			Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
			break;

		default:
			Hdc.byActiveCommand = 0;
			break;
	}
}

//-----------------------------------------------------------------------------
void HdcServiceRestoreCommand(void)
{
	Hdc.byStatusRegister |= STATUS_MASK_SEEK_COMPLETE;
	Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
}

//-----------------------------------------------------------------------------
void HdcServiceReadSectorCommand(void)
{
	if ((Hdc.byDriveSel >= MAX_VHD_DRIVES) || (Vhd[Hdc.byDriveSel].f == NULL))
	{
		Hdc.byActiveCommand = 0;
		Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
		return;
	}

	Hdc.nSectorSize = g_nSectorSizes[(Hdc.bySDH_Register >> 5) & 0x03];
	Hdc.byDriveSel  = (Hdc.bySDH_Register >> 3) & 0x03;
	Hdc.byHeadSel   = Hdc.bySDH_Register & 0x07;

	Hdc.pbyReadPtr = Hdc.bySectorBuffer;
	Hdc.nReadCount = Hdc.nSectorSize;

	uint32_t nOffset = HdcGetSectorOffset();

	FileSeek(Vhd[Hdc.byDriveSel].f, nOffset);
	FileRead(Vhd[Hdc.byDriveSel].f, Hdc.bySectorBuffer, Hdc.nSectorSize);

	Hdc.byStatusRegister |= STATUS_MASK_DATA_REQUEST;
	Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
}

//-----------------------------------------------------------------------------
void HdcServiceWriteSectorCommand(void)
{
	Hdc.byActiveCommand = Hdc.byCommandRegister;
	// Hdc.byStatusRegister |= STATUS_MASK_DATA_REQUEST;
}

//-----------------------------------------------------------------------------
void HdcServiceFormatCommand(void)
{
	// Hdc.byActiveCommand = Hdc.byCommandRegister;
	Hdc.byStatusRegister |= STATUS_MASK_DATA_REQUEST;
	Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
}

//-----------------------------------------------------------------------------
void HdcServiceSeekCommand(void)
{
	Hdc.byStatusRegister |= STATUS_MASK_SEEK_COMPLETE;
	Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
}

//-----------------------------------------------------------------------------
void HdcServiceTestCommand(void)
{
	Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
}

//-----------------------------------------------------------------------------
void HdcServiceStateMachine(void)
{
	if (Hdc.byActiveCommand != 0)
	{
		ProcessActiveCommand();
		return;
	}

	if (Hdc.byCommandRegister == 0)
	{
		return;
	}

	switch (Hdc.byCommandRegister >> 4)
	{
		case 0x01: // Restore
			HdcServiceRestoreCommand();
			break;

		case 0x02: // Read Sector
			HdcServiceReadSectorCommand();
			break;

		case 0x03: // Write Sector
			HdcServiceWriteSectorCommand();
			break;

		case 0x05: // Format Track
			HdcServiceFormatCommand();
			break;

		case 0x07: // Seek
			HdcServiceSeekCommand();
			break;

		case 0x09: // Test
			HdcServiceTestCommand();
			break;

		default:
			Hdc.byStatusRegister &= ~STATUS_MASK_BUSY;
			break;
	}

	Hdc.byCommandRegister = 0;
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(hdc_port_out)(word addr, byte data)
{
    addr = addr & 0xFF;

#ifdef ENABLE_LOGGING
	fdc_log[log_head].type = port_out;
	fdc_log[log_head].val = data;
    fdc_log[log_head].op1 = addr;
	++log_head;
	log_head = log_head % LOG_SIZE;
#endif

	switch (addr)
	{
		case 0xC1: // Hard disk controller board control register (Read/Write).
			break;

		case 0xC8: // Data Register
			*Hdc.pbyWritePtr = data;

			if (Hdc.nWriteCount > 0)
			{
				--Hdc.nWriteCount;
				++Hdc.pbyWritePtr;

				if (Hdc.nWriteCount == 0)
				{
					Hdc.byStatusRegister &= ~STATUS_MASK_DATA_REQUEST;
					// Hdc.byStatusRegister |= STATUS_MASK_BUSY;
				}
			}

			break;

		case 0xC9: // Hard Disk Write Pre-Comp Cyl.
			Hdc.byWritePrecompRegister = data;
			break;

		case 0xCA: // Hard Disk Sector Count (Read/Write).
			Hdc.bySectorCountRegister = data;
			break;

		case 0xCB: // Hard Disk Sector Number (Read/Write).
			Hdc.bySectorNumberRegister = data;
			break;

		case 0xCC: // Hard Disk Cylinder LSB (Read/Write).
			Hdc.byLowCylinderRegister = data;
			break;

		case 0xCD: // Hard Disk Cylinder MSB (Read/Write).
			Hdc.byHighCylinderRegister = data;
			break;

		case 0xCE: // Hard Disk Sector Size / Drive # / Head # (Read/Write).
			Hdc.bySDH_Register = data;
			Hdc.nSectorSize = g_nSectorSizes[(Hdc.bySDH_Register >> 5) & 0x03];
			Hdc.byDriveSel  = (Hdc.bySDH_Register >> 3) & 0x03;
			Hdc.byHeadSel   = Hdc.bySDH_Register & 0x07;
			break;

		case 0xCF: // Command Register for WD1010 Winchester Disk Controller Chip.
			Hdc.byActiveCommand = 0;
			Hdc.byCommandRegister = data;
			Hdc.byInterruptRequest = 0;
			Hdc.byStatusRegister &= ~STATUS_MASK_DATA_REQUEST;
			Hdc.byStatusRegister |= STATUS_MASK_BUSY;

			data = data >> 4;

			if ((data == 0x05) || (data == 0x03))
			{
				Hdc.pbyWritePtr = Hdc.bySectorBuffer;
				Hdc.nWriteCount = Hdc.nSectorSize;
			}

			break;
	}
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(hdc_port_in)(word addr)
{
    byte data = 0x00;

	addr = addr & 0xFF;

	switch (addr)
	{
		case 0xC0: // 0xC0 - Write Protection.
			data = Hdc.byWriteProtectRegister;
			break;

		case 0xC8: // Data Register
			data = *Hdc.pbyReadPtr;

			if (Hdc.nReadCount > 0)
			{
				--Hdc.nReadCount;
				++Hdc.pbyReadPtr;

				if (Hdc.nReadCount == 0)
				{
					Hdc.byStatusRegister &= ~STATUS_MASK_DATA_REQUEST;
				}
			}

			break;

		case 0xC9: // Error Register
			data = Hdc.byErrorRegister;
			break;

		case 0xCA: // Sector Count
			data = Hdc.bySectorCountRegister;
			break;

		case 0xCB: // Sector Number
			data = Hdc.bySectorNumberRegister;
			break;

		case 0xCC: // Cylinder Low Byte
			data = Hdc.byLowCylinderRegister;
			break;

		case 0xCD: //Cylinder High Byte
			data = Hdc.byHighCylinderRegister;
			break;

		case 0xCE: // Size/Drive/Head
			data = Hdc.bySDH_Register;
			break;

		case 0xCF: // 0xCF - Hard Disk Error Status Register (Read Only).
			data = Hdc.byStatusRegister;
			break;
	}

#ifdef ENABLE_LOGGING
	fdc_log[log_head].type = port_in;
	fdc_log[log_head].val = data;
    fdc_log[log_head].op1 = addr;
	++log_head;
	log_head = log_head % LOG_SIZE;
#endif

	return data;
}
