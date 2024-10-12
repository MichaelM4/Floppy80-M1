#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "video.h"

#define VIDEO_BUFFER_SIZE 0x800
#define VIDEO_NUM_COLS 64
#define VIDEO_NUM_ROWS 16

byte g_byVideoMemory[VIDEO_BUFFER_SIZE];
int  g_nVideoModified;

byte g_byLineBuffer[128];

void InitVideo(void)
{
	memset(g_byVideoMemory, 0x20, sizeof(g_byVideoMemory));
    g_nVideoModified = 0;
}

void VideoWrite(word addr, byte ch)
{
    if ((addr < 0x3C00) || (addr > 0x3FFF))
    {
        return;
    }

    g_byVideoMemory[addr-0x3C00] = ch;
    ++g_nVideoModified;
}

byte TranslateVideoChar(byte by)
{
    if ((by & 0xA0) == 0)
    {
        by |= 0x40;
    }

	switch (by)
	{
		case 128:
		case 130:
		case 160:
			by = ' ';
			break;

		case 138:
			by = 'X';
			break;

		case 133:
		case 149:
			by = 'X';
			break;

		case 131:
		case 135:
		case 143:
		case 159:
		case 175:
			by = 'X';
			break;

		case 136:
		case 140:
		case 180:
			by = 'X';
			break;

		case 189:
		case 190:
		case 191:
			by = 'X';
			break;

		case 188:
			by = 'X';
			break;

		default:
			if (by > 128)
			{
				by = 'X';
			}

			break;
	}

	return by;
}

void GetVideoLine(int nLine, byte* pby, int nMaxLen)
{
    byte* pbyVid = g_byVideoMemory+(nLine * VIDEO_NUM_COLS);
    int   i;

    for (i = 0; i < VIDEO_NUM_COLS; ++i)
    {
        *pby = TranslateVideoChar(*pbyVid);
        ++pby;
        ++pbyVid;
    }

    *pby = 0;
}

void ServiceVideo(void)
{
    static int nPreviousVideoModified;
    int i;

    if (nPreviousVideoModified == g_nVideoModified)
    {
        return;
    }

    nPreviousVideoModified = g_nVideoModified;

    puts("\033[?25l\033[2J\033[H");

    for (i = 0; i < 16; ++i)
    {
        printf("\033[%dH", i+1);
        GetVideoLine(i, g_byLineBuffer, sizeof(g_byLineBuffer));
        printf(g_byLineBuffer);
    }
}