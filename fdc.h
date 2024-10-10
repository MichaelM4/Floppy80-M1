#ifndef __FDC_C_
#define __FDC_C_

#ifdef __cplusplus
extern "C" {
#endif

#include "file.h"

/* global defines ========================================================*/

#define MAX_DRIVES 4
#define DMK_HEADER_SIZE 16

#define BLOCK_SIZE 512
#define NUM_BLOCKS 32
#define MAX_TRACK_SIZE (BLOCK_SIZE*NUM_BLOCKS)

                           /* Common status bits:               */
#define F_BUSY     0x01    /* Controller is executing a command */
#define F_READONLY 0x40    /* The disk is write-protected       */
#define F_NOTREADY 0x80    /* The drive is not ready            */

                           /* Type-1 command status:            */
#define F_INDEX    0x02    /* Index mark detected               */
#define F_TRACK0   0x04    /* Head positioned at track #0       */
#define F_CRCERR   0x08    /* CRC error in ID field             */
#define F_SEEKERR  0x10    /* Seek error, track not verified    */
#define F_HEADLOAD 0x20    /* Head loaded                       */
#define F_NOTREADY 0x80    /* Drive not ready, drive missing    */

                           /* Type-2 and Type-3 command status: */
#define F_DRQ      0x02    /* Data request pending              */
#define F_LOSTDATA 0x04    /* Data has been lost (missed DRQ)   */
#define F_ERRCODE  0x18    /* Error code bits:                  */
#define F_BADDATA  0x08    /* 1 = bad data CRC                  */
#define F_NOTFOUND 0x10    /* 2 = sector not found              */
#define F_BADID    0x18    /* 3 = bad ID field CRC              */
#define F_DELETED  0x20    /* Deleted data mark (when reading)  */
#define F_PROTECT  0x40
#define F_WRFAULT  0x20    /* Write fault (when writing)        */

/* global HFE defines ========================================================*/

#define IBMPC_DD_FLOPPYMODE            0x00 
#define IBMPC_HD_FLOPPYMODE            0x01
#define ATARIST_DD_FLOPPYMODE          0x02 
#define ATARIST_HD_FLOPPYMODE          0x03 
#define AMIGA_DD_FLOPPYMODE            0x04 
#define AMIGA_HD_FLOPPYMODE            0x05 
#define CPC_DD_FLOPPYMODE              0x06 
#define GENERIC_SHUGGART_DD_FLOPPYMODE 0x07 
#define IBMPC_ED_FLOPPYMODE            0x08 
#define MSX2_DD_FLOPPYMODE             0x09 
#define C64_DD_FLOPPYMODE              0x0A 
#define EMU_SHUGART_FLOPPYMODE         0x0B 
#define S950_DD_FLOPPYMODE             0x0C 
#define S950_HD_FLOPPYMODE             0x0D 
#define DISABLE_FLOPPYMODE             0xFE 
#define ISOIBM_MFM_ENCODING            0x00 
#define AMIGA_MFM_ENCODING             0x01 
#define ISOIBM_FM_ENCODING             0x02 
#define EMU_FM_ENCODING                0x03 
#define UNKNOWN_ENCODING               0xFF 

#define HFE_Encoding_MFM (0)
#define HFE_Encoding_FM  (2)

#define MFM_MARK_A1 (0x4489)

#define FM_CLOCK    (1)	// 0C
#define FM_DATA_ONE (1)	// 0D
#define FM_MARK_FE  (0x55111554)
#define FM_MARK_FB  (0x55111455)
#define FM_MARK_FA  (0x55111454)
#define FM_MARK_F8  (0x55111444)
#define FM_MARK_F9  (0x55111445)

#define MAX_TRACKS    80
#define MAX_SECTORS_PER_TRACK 32
#define MAX_TRACK_LEN 0x4000

#define FdcGetIDAM_Offset(x) (0x3FFF & x)
//#define FdcGetIDAM_Density(x) (0x8000 & x)

#define CPM_BLOCK_SIZE 0x200
#define CPM_READ_BLOCK_CMD  0x10
#define CPM_WRITE_BLOCK_CMD 0x11
#define CPM_PARTITION_BASE  0x800

/* global variable declarations ==========================================*/

enum {
	psIdle = 0,
	psReadSector = 1,
	psWriteSector,
	psWriteTrack,
	psSeek,
	psSendData,
	psMountImage,
	psOpenFile,
	psWriteFile,
	psSetTime,
};

enum {
	eSD = 0,
	eDD,
	eHD,
};

enum {
	eUnknown = 0,
	eDMK,
	eHFE,
	eIMG
};

typedef struct pictrack_
{ 
	unsigned short offset; // Offset of the track data in block of 512 bytes (Ex: 2=0x400) 
	unsigned short track_len; // Length of the track data in byte. 
} pictrack;

typedef struct picfileformatheader_ 
{
	unsigned char HEADERSIGNATURE[8];	// “HXCPICFE” 
	unsigned char formatrevision;		// Revision 0 
	unsigned char number_of_tracks;		// Number of track in the file 
	unsigned char number_of_sides;		// Number of valid side (Not used by the emulator) 
	unsigned char track_encoding;		// Track Encoding mode

	// (Used for the write support - Please see the list above) 
	unsigned short bitRate;				// Bitrate in Kbit/s. Ex : 250=250000bits/s 

	// Max value : 500 
	unsigned short floppyRPM;			// Rotation per minute (Not used by the emulator) 
	unsigned char  floppyinterfacemode;	// Floppy interface mode. (Please see the list above.) 
	unsigned char  dnu;					// Free 
	unsigned short track_list_offset;	// Offset of the track list LUT in block of 512 bytes 

	// (Ex: 1=0x200) 
	unsigned char write_allowed;		// The Floppy image is write protected ? 
	unsigned char single_step;			// 0xFF : Single Step – 0x00 Double Step mode 
	unsigned char track0s0_altencoding;	// 0x00 : Use an alternate track_encoding for track 0 Side 0 
	unsigned char track0s0_encoding;	// alternate track_encoding for track 0 Side 0 
	unsigned char track0s1_altencoding;	// 0x00 : Use an alternate track_encoding for track 0 Side 1 
	unsigned char track0s1_encoding;	// alternate track_encoding for track 0 Side 1 
} picfileformatheader; 

enum {
	eBusy,
	eIndex,
	eDataLost,
	eCrcError,
	eSeekError,
	eNotFound,
	eProtected,
	eNotReady,
	eRecordType,
	eDataRequest,
	eIntrRequest
};

typedef struct {
	BYTE byBusy;
	BYTE byIndex;
	BYTE byDataLost;
	BYTE byCrcError;
	BYTE bySeekError;
	BYTE byNotFound;
	BYTE byProtected;
	BYTE byNotReady;
	BYTE byRecordType;		// 0xFB => regular data; or 0xF8 => deleted data;
	
	BYTE byDataRequest;		// controls the DRQ output pin. Which simulates an open drain output that indicates that the DR (Data Register)
							// contains assembled data in read operations, or the DR is empty in write operation.  This signal is reset when
							// serviced by the computer through reading or writing the DR in Read and Write operations, respectively.
							// basically indicates that a byte can be read from the DR or written to the DR.
							//
							// when 1 => data can be read/written;
							//      0 => data is not available to be read/written;
							//
							// when enabled via the corresponding bit of byNmiMaskReg the WAIT output is the inverted state of byDataReq
	
	BYTE byIntrRequest;		// controls the INTRQ output pin.  Which simulates an open drain output that when set indicates the completion
							// of any command and is reset when the computer reads or writes to/from the DR.
							//
							// when 1 => command has been completed;
							//      0 => command can be written or that a command is in progress;
							//
							// when enabled via the corresponding bit of byNmiMaskReg the NMI output is the inverted state of byIntrReq
} FDC_StatusType;

typedef struct {
	picfileformatheader header;
	pictrack            trackLUT[MAX_TRACKS];
} HfeDriveType;

typedef struct {
	// disk header
	BYTE   byDmkDiskHeader[DMK_HEADER_SIZE];

	BYTE   byWriteProtected;
	WORD   wTrackLength;
	BYTE   byNumSides;
	BYTE   byDensity;

	// sector data
	int    nSectorSize;
} DmkDriveType;

typedef struct {
	file* f;
	char  szFileName[128];
	int   nDriveFormat;
	BYTE  byNumTracks;

	union {
		DmkDriveType dmk;
		HfeDriveType hfe;
	};
} FdcDriveType;

typedef struct {
	int  nType;
	byte byDensity;

	int nDrive;
	int nSide;
	int nTrack;

	int nReadCount;
	int nReadSize;

	int nWriteCount;
	int nWriteSize;

	int nFileOffset;              // byte offset from the start of the file to the start of this track

	int  nIDAM[0x80];             // byte offset from start of track buffer for each ID Address Mark
	int  nDAM[0x80];              // byte offset from start of track buffer for each Data Address Mark

	int nSectorIDAM_BitPos[0x80];	// bit offset from start of track buffer for each ID Address Mark
	int nSectorDAM_BitPos[0x80];	// bit offset from start of track buffer for each Data Address Mark

	BYTE* pbyReadPtr;
	BYTE* pbyWritePtr;

	int   nTrackSize;
	BYTE  byTrackData[MAX_TRACK_SIZE];
} TrackType;

#define MAX_SECTOR_SIZE 256

typedef struct {
	int   nSector;
	int   nSectorSize;
	int   nSectorDataOffset;		// offset from the start of the track buffer of the first data byte of the sector specified by nSector
	BYTE  bySectorDataAddressMark;
} SectorType;

typedef struct {
	// RD when DISK_IN is low
	// WR when DISK_OUT is low
	// address sepecified by A0 and A1 inputs
	BYTE  byCommandReg;				// WR address 0

	BYTE  byTrack;						// RD/WR address 1
	BYTE  bySector;						// RD/WR address 2
	BYTE  byData;							// RD/WR address 3

	FDC_StatusType status;
	BYTE  byStatus;

	BYTE  byCommandReceived;		// contains the value of the last received command
	BYTE  byCurCommand;
	BYTE  byCommandType;			// 1, 2, 3 or 4

	short nStepDir;					// 1 = increase track reg on each step; -1 = decrease track reg on each step;
	
	// RD/WR when DRIVESEL is low
	BYTE  byDriveSel;				// Bit 0 = drive select 1;
									// Bit 1 = drive select 2;
									// Bit 2 = drive select 3;
									// Bit 3 = drive select 4;
									// Bit 4 = 0 => side 0 select; 1 => side 1 select;
									// Bit 5 = Write Precom (ignored);
									// Bit 6 = 0 => do not generate wait; 1 => generate wait;
									// Bit 7 = 0 => select FM mode; 1 => select MFM mode; (ignored)

	BYTE  byBackupDriveSel;
	
	// RD when RDNMISTATUS is low
	BYTE  byNmiStatusReg;

	// WR when WRNMIMASKREG is low
	BYTE  byNmiMaskReg;				// Bit 0 = host select; (see host interface communication protocol document for details)
									// Bit 1..5 = not used;
									// Bit 6 = the WAIT output is the inverted state of this bit.  This bit is cleared by activation of RESET, WAITTIMEOUT, INTRQ and DRQ
									// Bit 7 = 0 => disable INTRQ; 1 => enable INTRQ;

	BYTE  byRecordMark;
							
	BYTE  byIntrEnable;		// set by a Force Interrupt command
							// bit-0 = generate interrupt on Not-Ready to Ready transition;
							// bit-1 = generate interrupt on Ready to Not-Ready transition;
							// bit-2 = at every index pulse;
							// bit-3 = Imediate interrupt;

	BYTE  byHoldWaitOnDataWrite;
	BYTE  byReleaseWait;
	BYTE  byWaitOutput;		// when 1 => wait line is being held low;
							//      0 => wait line is released;

	DWORD dwWaitTimeoutCount;
	DWORD dwRotationCount;

	DWORD dwMotorOnTimer;	// when not zero the drive motor is considered to be ON.
							// when it reaches zero (after approximately 2 seconds) the motor is considered OFF.
							// the 2 second count is reloated each time the DRV_SEL latch is written too.

	DWORD dwResetCount;		// increments when the FDC RESET input is low, is set to zero when FDC RESET is high
							// this can be used to determine if the FDC has received a RESET pulse
	BYTE  byResetFDC;

	int   nProcessFunction;
	int   nServiceState;
	DWORD dwStateCounter;

	int   nReadStatusCount;

	BYTE  bySdCardPresent;
	
	int   nDataRegReadCount;

	BYTE  byTransferBuffer[256];
	int   nTransferSize;
	int   nTrasferIndex;
} FdcType;

/* ==============================================================*/

extern FdcType   g_FDC;
extern TrackType g_tdTrack;

/* function prototypes ==========================================*/

void LoadHfeTrack(file* pFile, int nTrack, int nSide, HfeDriveType* pdisk, TrackType* ptrack, BYTE* pbyTrackData, int nMaxLen);

void FdcSetFlag(byte flag);
void FdcClrFlag(byte flag);
BYTE FdcGetCommandType(BYTE byCommand);
void FdcGenerateIntr(void);
void FdcStartCapture(void);
void FdcInit(void);
void FdcReset(void);
void FdcProcessCommand(void);
void FdcServiceStateMachine(void);
void FdcProcessConfigEntry(char szLabel[], char* psz);
void FdcCloseAllFiles(void);

void fdc_write(uint16_t addr, byte byData);
byte fdc_read(uint16_t wAddr);
void fdc_write_drive_select(byte byData);
byte fdc_read_nmi(void);
void fdc_write_nmi(byte byData);
void fdc_process_command_request(byte by);

#ifdef __cplusplus
}
#endif

#endif
