#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef MFC
	#include "hardware/gpio.h"
	#include "pico/stdlib.h"
	#include "sd_core.h"
#endif

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

#define ERROR_MASK_DAM_NOT_FOUND      0x01
#define ERROR_MASK_TR000              0x02
#define ERROR_MASK_ABORTED_COMMAND    0x04
#define ERROR_MASK_UNDEFINED          0x08
#define ERROR_MASK_ID_NOT_FOUND       0x10
#define ERROR_MASK_CRC_ERROR_ID_FIELD 0x20
#define ERROR_MASK_UNCORRECTABLE      0x40
#define ERROR_MASK_BAD_BLOCK_DETECTED 0x80

#define STATUS_MASK_ERROR          0x01
#define STATUS_MASK_NOT_USED       0x02
#define STATUS_MASK_CORRECTED_DATA 0x04
#define STATUS_MASK_DATA_REQUEST   0x08
#define STATUS_MASK_SEEK_COMPLETE  0x10
#define STATUS_MASK_WRITE_FAULT    0x20
#define STATUS_MASK_DRIVE_READY    0x40
#define STATUS_MASK_BUSY           0x80

typedef struct {
	byte  byErrorRegister;
	byte  byWritePrecompRegister;
	byte  bySectorCountRegister;
	byte  bySectorNumberRegister;
	byte  byHighCylinderRegister;
	byte  byLowCylinderRegister;
	byte  bySDH_Register;
	byte  byStatusRegister;
	byte  byWriteProtectRegister; // also Interrupt Request flag (MS bit)
	byte  byCommandRegister;
	byte  byInterruptRequest;
	byte  bySectorBuffer[2048];
	int   nSectorSize;
	byte  byDriveSel;
	byte  byHeadSel;
	byte* pbyReadPtr;
	int   nReadCount;
	byte* pbyWritePtr;
	int   nWriteCount;
} HdcType;

HdcType Hdc;

//-----------------------------------------------------------------------------
void HdcInit(void)
{
	memset(&Hdc, 0, sizeof(Hdc));
	Hdc.byStatusRegister |= STATUS_MASK_DRIVE_READY;
}

//-----------------------------------------------------------------------------
void HdcServiceTestCommand(void)
{
	Hdc.byStatusRegister |= STATUS_MASK_SEEK_COMPLETE;
}

//-----------------------------------------------------------------------------
void HdcServiceRestoreCommand(void)
{
	Hdc.byStatusRegister |= STATUS_MASK_SEEK_COMPLETE;
}

//-----------------------------------------------------------------------------
void HdcServiceReadSectorCommand(void)
{
	int nSectorSizes[] = {256, 512, 1024, 128};

	Hdc.nSectorSize = nSectorSizes[(Hdc.bySDH_Register >> 5) & 0x03];
	Hdc.byDriveSel  = (Hdc.bySDH_Register >> 3) & 0x03;
	Hdc.byHeadSel   = Hdc.bySDH_Register & 0x07;

	Hdc.pbyReadPtr = Hdc.bySectorBuffer;
	Hdc.nReadCount = 0;

	Hdc.byStatusRegister |= STATUS_MASK_DATA_REQUEST;
}

//-----------------------------------------------------------------------------
void HdcServiceWriteSectorCommand(void)
{
	int nSectorSizes[] = {256, 512, 1024, 128};

	Hdc.nSectorSize = nSectorSizes[(Hdc.bySDH_Register >> 5) & 0x03];
	Hdc.byDriveSel  = (Hdc.bySDH_Register >> 3) & 0x03;
	Hdc.byHeadSel   = Hdc.bySDH_Register & 0x07;

	Hdc.pbyWritePtr = Hdc.bySectorBuffer;
	Hdc.nWriteCount = 0;

	Hdc.byStatusRegister |= STATUS_MASK_DATA_REQUEST;
}

//-----------------------------------------------------------------------------
void HdcServiceFormatCommand(void)
{

}

//-----------------------------------------------------------------------------
void HdcServiceSeekCommand(void)
{
	Hdc.byStatusRegister |= STATUS_MASK_SEEK_COMPLETE;
}

//-----------------------------------------------------------------------------
void HdcServiceStateMachine(void)
{
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
	}

	Hdc.byCommandRegister = 0;
	Hdc.byStatusRegister |= STATUS_MASK_DRIVE_READY;
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

			if (Hdc.nWriteCount < sizeof(Hdc.bySectorBuffer))
			{
				++Hdc.nReadCount;
				++Hdc.pbyWritePtr;
			}

			break;

		case 0xC9: // Hard Disk Write Pre-Comp Cyl.
			Hdc.byWritePrecompRegister = data;
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
			break;

		case 0xCF: // Commaand Register for WD1010 Winchester Disk Controller Chip.
			Hdc.byCommandRegister = data;
			Hdc.byStatusRegister &= ~STATUS_MASK_DRIVE_READY;
			Hdc.byInterruptRequest = 0;
			break;
	}
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(hdc_port_in)(word addr)
{
    byte data = 0x00;

	addr = addr & 0xFF;

#ifdef ENABLE_LOGGING
	fdc_log[log_head].type = port_in;
	fdc_log[log_head].val = data;
    fdc_log[log_head].op1 = addr;
	++log_head;
	log_head = log_head % LOG_SIZE;
#endif

	switch (addr)
	{
		case 0xC0: // 0xC0 - Write Protection.
			data = Hdc.byWriteProtectRegister;
			break;

		case 0xC8: // Data Register
			data = *Hdc.pbyReadPtr;

			if (Hdc.nReadCount < sizeof(Hdc.bySectorBuffer))
			{
				++Hdc.nReadCount;
				++Hdc.pbyReadPtr;
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

    return data;
}
