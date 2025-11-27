#include <stdio.h>
#include <string.h>
#include "tusb.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "defines.h"
#include "video.h"

byte g_byVideoMemory[VIDEO_BUFFER_SIZE];
byte g_byLineBuffer[128];
byte g_byModified[VIDEO_BUFFER_SIZE/VIDEO_NUM_COLS];

void InitVideo(void)
{
	memset(g_byVideoMemory, 0x20, sizeof(g_byVideoMemory));
}

void VideoWrite(word addr, byte ch)
{
    if ((addr < 0x3C00) || (addr > 0x3FFF))
    {
        return;
    }

    addr -= 0x3C00;
    g_byVideoMemory[addr] = ch;
    g_byModified[addr / VIDEO_NUM_COLS] = true;
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
        // *pby = TranslateVideoChar(*pbyVid);
        *pby = *pbyVid;
        ++pby;
        ++pbyVid;
    }

    *pby = 0;
}

int GetModifiedLine(void)
{
    int i;

    for (i = 0; i < sizeof(g_byModified); ++i)
    {
        if (g_byModified[i])
        {
            g_byModified[i] = false;
            return i;
        }
    }

    return -1;
}

void ServiceVideo(void)
{
    static int nCurrentLine = 0;
    static int state = 0;
    static int offset = 0;
	static uint64_t nLastSend;
    int line;

    switch (state)
    {
        case 0:
            line = GetModifiedLine();

            if (line >= 0)
            {
                nCurrentLine = line;
            }
            else
            {
                ++nCurrentLine;
            }

            if (nCurrentLine >= VIDEO_NUM_ROWS)
            {
                nCurrentLine = 0;
            }

            offset = 0;
            g_byLineBuffer[0] = 1; // puts at x/y command code
            g_byLineBuffer[1] = 0; // X pos
            g_byLineBuffer[2] = nCurrentLine; // Y pos
            GetVideoLine(nCurrentLine, g_byLineBuffer+3, sizeof(g_byLineBuffer)-3);

            ++state;
            break;

        case 1:
            if (!spi_is_writable(spi0))
            {
                break;
            }

            gpio_put(LED_PIN, 0);
            spi_write_blocking(spi0, g_byLineBuffer+offset, 16);
         	gpio_put(LED_PIN, 1);
        	nLastSend = time_us_64();
            ++state;
            break;

        case 2:
            if ((time_us_64() - nLastSend) < 1000)
            {
                break;
            }

            offset += 8;

            if (offset < VIDEO_NUM_COLS) // then setup to send next 8 characters
            {
                g_byLineBuffer[offset] = 1;
                g_byLineBuffer[offset+1] = offset;
                g_byLineBuffer[offset+2] = nCurrentLine;
                --state;
            }
            else // go get next line
            {
                ++state;
            }

            break;

        default:
            state = 0;
            break;
    }
}
