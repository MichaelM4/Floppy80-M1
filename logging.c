#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "stdio.h"

#include "defines.h"
#include "system.h"
#include "file.h"
#include "fdc.h"
#include "cli.h"

#ifdef ENABLE_LOGGING

extern FdcDriveType g_dtDives[MAX_DRIVES];

static uint8_t  g_byRwIndex = 0;
static char     g_szRwBuf[256];
static int      g_nDriveSel = -1;
static int      g_nCommand = -1;
static int      g_nCommandType = -1;

LogType fdc_log[LOG_SIZE];
int log_head = 0;
int log_tail = 0;

//----------------------------------------------------------------------------
void PurgeRwBuffer(void)
{
	if (g_szRwBuf[0] != 0)
	{
		#ifdef MFC
			strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "\r\n");
			WriteLogFile(g_szRwBuf);
		#else
			puts(g_szRwBuf);
		#endif
	}

	g_byRwIndex  = 0;
	g_szRwBuf[0] = 0;
}

//----------------------------------------------------------------------------
void GetCommandText(char* psz, int nMaxLen, BYTE byCmd, BYTE op1, BYTE op2)
{
	*psz = 0;

	if (byCmd == 0xFE)
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X PERCOM DOUBLER FM", byCmd);
	}
	else if (byCmd == 0xFF)
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X PERCOM DOUBLER MFM", byCmd);
	}
	else if ((byCmd & 0xF0) == 0)         // 0000xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Restore", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x10) // 0001xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X SEEK %02X, From %02X", byCmd, op1, op2);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x20) // 0010xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step, Do Not Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x30) // 0011xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step, Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x40) // 0100xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step In, Do Not Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x50) // 0101xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step In, Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x60) // 0110xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step Out, Do Not Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x70) // 0111xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Step Out, Update Track Register", byCmd);
		g_nCommandType = 1;
	}
	else if ((byCmd & 0xF0) == 0x80) // 1000xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X DRV: %02X S: %d TRK: %02X RSEC: %02X",
				  byCmd, FdcGetDriveIndex(g_nDriveSel), FdcGetSide(g_nDriveSel), op1, op2);
		g_nCommandType = 2;
	}
	else if ((byCmd & 0xF0) == 0x90) // 1001xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X RSEC: Multiple Record", byCmd);
		g_nCommandType = 2;
	}
	else if ((byCmd & 0xF0) == 0xA0) // 1010xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X DRV: %02X S: %d TRK: %02X WSEC: %02X ",
				  byCmd, FdcGetDriveIndex(g_nDriveSel), FdcGetSide(g_nDriveSel), op1, op2);
		g_nCommandType = 2;
	}
	else if ((byCmd & 0xF0) == 0xB0) // 1011xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X WSEC: Multiple Record", byCmd);
		g_nCommandType = 2;
	}
	else if (byCmd == 0xC4) // 11000100
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Read Address", byCmd);
		g_nCommandType = 3;
	}
	else if ((byCmd & 0xF0) == 0xD0) // 1101xxxx
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Force Interrupt", byCmd);
		g_nCommandType = 4;
	}
	else if ((byCmd & 0xFE) == 0xE4) // 1110010x
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X RTRK: %02X", byCmd, op1);
		g_nCommandType = 3;
	}
	else if (byCmd == 0xF0) // 11110000
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X DSEL: %02X WTRK: %02X", byCmd, op1, op2);
		g_nCommandType = 3;
	}
	else if (byCmd == 0xF4) // 11110100
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X DSEL: %02X WTRK: %02X", byCmd, op1, op2);
		g_nCommandType = 3;
	}
	else
	{
		sprintf_s(psz, nMaxLen, "CMD: %02X Unknown", byCmd);
	}
}

//-----------------------------------------------------------------------------
extern int __not_in_flash_func(FdcGetDriveIndex)(int nDriveSel);

//-----------------------------------------------------------------------------
void __not_in_flash_func(fdc_get_status_string)(char* buf, int nMaxLen, BYTE byStatus)
{
	int nDrive = FdcGetDriveIndex(g_nDriveSel);

	buf[0] = '|';
	buf[1] = 0;

	if ((nDrive < 0) || (g_dtDives[nDrive].f == NULL))
	{
		strcat_s(buf, nMaxLen, (char*)"F_NOTREADY|F_HEADLOAD");
	}
	else if ((g_nCommandType == 1) || // Restore, Seek, Step, Step In, Step Out
             (g_nCommandType == 4))   // Force Interrupt
	{
		// S0 (BUSY)
		if (byStatus & F_BUSY)
		{
			strcat_s(buf, nMaxLen, (char*)"F_BUSY|");
		}
		
		// S1 (INDEX) default to 0
		if (byStatus & F_INDEX)
		{
			strcat_s(buf, nMaxLen, (char*)"F_INDEX|");
		}

		// S2 (TRACK 0) default to 0
		if (byStatus & F_TRACK0)
		{
			strcat_s(buf, nMaxLen, (char*)"F_TRACK0|");
		}

		// S3 (CRC ERROR) default to 0
		if (byStatus & F_CRCERR)
		{
			strcat_s(buf, nMaxLen, (char*)"F_CRCERR|");
		}

		// S4 (SEEK ERROR) default to 0
		if (byStatus & F_SEEKERR)
		{
			strcat_s(buf, nMaxLen, (char*)"F_SEEKERR|");
		}
		
		if (byStatus & F_HEADLOAD)
		{
			strcat_s(buf, nMaxLen, (char*)"F_HEADLOAD|");
		}

		// S6 (PROTECTED) default to 0
		if (byStatus & F_PROTECTED)
		{
			strcat_s(buf, nMaxLen, (char*)"F_PROTECTED|");
		}
		
		// S7 (NOT READY) default to 0
		if (byStatus & F_NOTREADY)
		{
			strcat_s(buf, nMaxLen, (char*)"F_NOTREADY|");
		}
	}
	else if ((g_nCommandType == 2) ||	// Read Sector, Write Sector
			 (g_nCommandType == 3))	// Read Address, Read Track, Write Track
	{
		// S0 (BUSY)
		if (byStatus & F_BUSY)
		{
			strcat_s(buf, nMaxLen, (char*)"F_BUSY|");
		}
	
		// S1 (DATA REQUEST)     default to 0
		if (byStatus & F_DRQ)
		{
			strcat_s(buf, nMaxLen, (char*)"F_DRQ|");
		}

		// S2 (LOST DATA)        default to 0
		if (byStatus & F_LOSTDATA)
		{
			strcat_s(buf, nMaxLen, (char*)"F_LOSTDATA|");
		}
		
		// S3 (CRC ERROR)        default to 0
		if (byStatus & F_CRCERR)
		{
			strcat_s(buf, nMaxLen, (char*)"F_CRCERR|");
		}
		
		// S4 (RECORD NOT FOUND) default to 0
		if (byStatus & F_NOTFOUND)
		{
			strcat_s(buf, nMaxLen, (char*)"F_NOTFOUND|");
		}
	
		// S5 (RECORD TYPE) default to 0
		if (byStatus & F_DELETED)
		{
			strcat_s(buf, nMaxLen, (char*)"F_DELETED|");
		}

		// S6 (PROTECTED) default to 0
		if (byStatus & F_PROTECT)
		{
			strcat_s(buf, nMaxLen, (char*)"F_PROTECT|");
		}

		// S7 (NOT READY) default to 0
		if (byStatus & F_NOTREADY)
		{
			strcat_s(buf, nMaxLen, (char*)"F_NOTREADY|");
		}
	}
	else // Force Interrupt
	{
	}
}

void ServicePortOutLog(void)
{
    char buf[64];
	char t[8];

	switch (fdc_log[log_tail].op1)
	{
		case 0xC1: // Hard disk controller board control register (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X ", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xC8: // Data Register
            if (g_byRwIndex == 0)
            {
                sprintf_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "OUT DATA %02X", fdc_log[log_tail].val);
                ++g_byRwIndex;
            }
            else if (g_byRwIndex == 15)
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                g_byRwIndex = 0;

                #ifdef MFC
                    strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "\r\n");
                    WriteLogFile(g_szRwBuf);
                #else
                    puts(g_szRwBuf);
                #endif

                g_szRwBuf[0] = 0;
            }
            else
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                ++g_byRwIndex;
            }

			break;

		case 0xC9: // Hard Disk Write Pre-Comp Cyl.
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (Write Pre-Comp)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCB: // Hard Disk Sector Number (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (Sector Number)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCC: // Hard Disk Cylinder LSB (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (Cylinder LSB)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCD: // Hard Disk Cylinder MSB (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (Cylinder MSB)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCE: // Hard Disk Sector Size / Drive # / Head # (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (SDH)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCF: // Command Register for WD1010 Winchester Disk Controller Chip.
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X (CMD Reg)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		default:
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "OUT %02X %02X ", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;
	}
}


void ServicePortInLog(void)
{
    char buf[64];
	char t[8];

	switch (fdc_log[log_tail].op1)
	{
		case 0xC1: // Hard disk controller board control register (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X ", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xC8: // Data Register
            if (g_byRwIndex == 0)
            {
                sprintf_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "INP DATA %02X", fdc_log[log_tail].val);
                ++g_byRwIndex;
            }
            else if (g_byRwIndex == 15)
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                g_byRwIndex = 0;

                #ifdef MFC
                    strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "\r\n");
                    WriteLogFile(g_szRwBuf);
                #else
                    puts(g_szRwBuf);
                #endif

                g_szRwBuf[0] = 0;
            }
            else
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                ++g_byRwIndex;
            }

			break;

		case 0xC9: // Hard Disk Write Pre-Comp Cyl.
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (Error Register)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCB: // Hard Disk Sector Number (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (Sector Number)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCC: // Hard Disk Cylinder LSB (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (Cylinder LSB)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCD: // Hard Disk Cylinder MSB (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (Cylinder MSB)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCE: // Hard Disk Sector Size / Drive # / Head # (Read/Write).
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (SDH)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		case 0xCF: // Command Register for WD1010 Winchester Disk Controller Chip.
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X (Status Reg)", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;

		default:
            PurgeRwBuffer();
			sprintf_s(buf, sizeof(buf)-1, "INP %02X %02X ", fdc_log[log_tail].op1, fdc_log[log_tail].val);
			#ifdef MFC
				strcat_s(buf2, sizeof(buf2)-1, "\r\n");
				WriteLogFile(buf2);
			#else
				puts(buf);
			#endif
			break;
	}
}

//----------------------------------------------------------------------------
void ServiceFdcLog(void)
{
	static BYTE byPrevStatus = 0;
	static BYTE byPrevDrvSelRd = 0;
	static BYTE byDrvSelRdCount = 0;
    char buf[64];
	char t[8];

    if (log_head == log_tail)
    {
        return;
    }

    if (tud_cdc_write_available() < 56)
	{
		return;
	}

    switch (fdc_log[log_tail].type)
    {
        case write_drive_select:
            if (g_nDriveSel != fdc_log[log_tail].val)
            {
                sprintf_s(buf, sizeof(buf), "WR DRVSEL %02X", fdc_log[log_tail].val);

                #ifdef MFC
                    strcat_s(buf, sizeof(buf)-1, "\r\n");
                    WriteLogFile(buf);
                #else
                    puts(buf);
                #endif

				g_nDriveSel = fdc_log[log_tail].val;
            }

			break;

		case read_drive_select:
			++byDrvSelRdCount;

			if ((fdc_log[log_tail].val != byPrevDrvSelRd) || (byDrvSelRdCount >= 40))
			{
				sprintf_s(buf, sizeof(buf), "RD DRVSEL %02X x %d", fdc_log[log_tail].val, byDrvSelRdCount);

				#ifdef MFC
					strcat_s(buf, sizeof(buf)-1, "\r\n");
					WriteLogFile(buf);
				#else
					puts(buf);
				#endif

				byDrvSelRdCount = 0;
				byPrevDrvSelRd = fdc_log[log_tail].val;
			}

			break;

        case write_data:
            if (g_byRwIndex == 0)
            {
                sprintf_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "WR DATA %02X", fdc_log[log_tail].val);
                ++g_byRwIndex;
            }
            else if (g_byRwIndex == 15)
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                g_byRwIndex = 0;

                #ifdef MFC
                    strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "\r\n");
                    WriteLogFile(g_szRwBuf);
                #else
                    puts(g_szRwBuf);
                #endif

                g_szRwBuf[0] = 0;
            }
            else
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                ++g_byRwIndex;
            }

			break;

        case write_sector:
        	PurgeRwBuffer();

			sprintf_s(buf, sizeof(buf)-1, "WR SECTOR %02X ", fdc_log[log_tail].val);

			if (fdc_log[log_tail].val >= 0xE0)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Precomp = 1");
			}
			else if (fdc_log[log_tail].val >= 0xC0)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Precomp = 0");
			}
			else if (fdc_log[log_tail].val >= 0xA0)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Enable = 0, Doubler Density = 0");
			}
			else if (fdc_log[log_tail].val >= 0x80)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Enable = 1, Doubler Density = 1");
			}
			else if (fdc_log[log_tail].val >= 0x60)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Side = 1");
			}
			else if (fdc_log[log_tail].val >= 0x40)
			{
                strcat_s(buf, sizeof(buf)-1, "Doubler Side = 0");
			}

            #ifdef MFC
                strcat_s(buf, sizeof(buf)-1, "\r\n");
                WriteLogFile(buf);
            #else
                puts(buf);
            #endif
            break;

        case write_track:
            PurgeRwBuffer();

            sprintf_s(buf, sizeof(buf)-1, "WR TRACK %02X", fdc_log[log_tail].val);

            #ifdef MFC
                strcat_s(buf, sizeof(buf)-1, "\r\n");
                WriteLogFile(buf);
            #else
                puts(buf);
            #endif
            break;

        case write_cmd:
        	PurgeRwBuffer();

			g_nCommand = fdc_log[log_tail].val;

            GetCommandText(buf, sizeof(buf), fdc_log[log_tail].val, fdc_log[log_tail].op1, fdc_log[log_tail].op2);

            #ifdef MFC
                strcat_s(buf, sizeof(buf)-1, "\r\n");
                WriteLogFile(buf);
            #else
                puts(buf);
            #endif
            break;

        case read_data:
            if (g_byRwIndex == 0)
            {
                sprintf_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "RD DATA %02X", fdc_log[log_tail].val);
                ++g_byRwIndex;
            }
            else if (g_byRwIndex == 15)
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);

                #ifdef MFC
                    strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, "\r\n");
                    WriteLogFile(g_szRwBuf);
                #else
                    puts(g_szRwBuf);
                #endif

                g_byRwIndex  = 0;
                g_szRwBuf[0] = 0;
            }
            else
            {
                sprintf_s(t, sizeof(t)-1, " %02X", fdc_log[log_tail].val);
                strcat_s(g_szRwBuf, sizeof(g_szRwBuf)-1, t);
                ++g_byRwIndex;
            }

			break;

        case read_sector:
            PurgeRwBuffer();
            sprintf_s(buf, sizeof(buf)-1, "RD SECTOR %02X", fdc_log[log_tail].val);

            #ifdef MFC
                strcat_s(buf, sizeof(buf)-1, "\r\n");
                WriteLogFile(buf);
            #else
                puts(buf);
            #endif
            break;

        case read_track:
            PurgeRwBuffer();
            sprintf_s(buf, sizeof(buf)-1, "RD TRACK %02X", fdc_log[log_tail].val);

            #ifdef MFC
                strcat_s(buf, sizeof(buf)-1, "\r\n");
                WriteLogFile(buf);
            #else
                puts(buf);
            #endif
            break;

        case read_status:
            if (byPrevStatus != fdc_log[log_tail].val)
            {
                char buf[64];
                char buf2[128];

                PurgeRwBuffer();

                fdc_get_status_string(buf, sizeof(buf)-1, fdc_log[log_tail].val);
                sprintf_s(buf2, sizeof(buf2)-1, "RD STATUS %02X CMD TYPE %d (%s)", fdc_log[log_tail].val, g_nCommandType, buf);

                #ifdef MFC
                    strcat_s(buf2, sizeof(buf2)-1, "\r\n");
                    WriteLogFile(buf2);
                #else
                    puts(buf2);
                #endif

                byPrevStatus = fdc_log[log_tail].val;
            }

			break;

		case port_out:
			ServicePortOutLog();
			break;

		case port_in:
			ServicePortInLog();
			break;
    }

    ++log_tail;
	log_tail = log_tail % LOG_SIZE;
}

#endif
