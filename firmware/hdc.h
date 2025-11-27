
#define MAX_VHD_DRIVES 2
#define MAX_VHD_HEADS 4
#define MAX_VHD_CYLINDERS 1024

#define VHD_HEADER_SIZE 256
#define VHD_DEFAULT_SECTORS 32

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
	byte  byActiveCommand;
} HdcType;

typedef struct {
	file* f;
	char  szFileName[128];

	byte byHeader[VHD_HEADER_SIZE];
	int  nHeads;
	int  nCylinders;
	int  nSectors;
} VhdType;

extern HdcType Hdc;
extern VhdType Vhd[MAX_VHD_DRIVES];

void HdcInitFileName(int nDrive, char* pszFileName);
void HdcInit(void);
void HdcCreateVhd(char* pszFileName, int nHeads, int nCylinders, int nSectors);
void HdcServiceStateMachine(void);
void HdcDumpDisk(int nDrive);

void hdc_port_out(word addr, byte data);
byte hdc_port_in(word addr);
