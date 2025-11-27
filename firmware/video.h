#ifndef _H_VIDEO_
#define _H_VIDEO_

#include "defines.h"

#define VIDEO_ADDR_START 0x3C00
#define VIDEO_ADDR_END 0x3FFF

#define VIDEO_BUFFER_SIZE (VIDEO_ADDR_END - VIDEO_ADDR_START + 1)
#define VIDEO_NUM_COLS 64
#define VIDEO_NUM_ROWS 16

#define MAX_VIDEO_LINES (VIDEO_BUFFER_SIZE / VIDEO_NUM_COLS)

// extern byte g_byVideoMemory[VIDEO_BUFFER_SIZE];

void InitVideo(void);
void VideoWrite(word addr, byte ch);
void ServiceVideo(void);
void PrintVideo(void);

void ServiceVGA(void);

#endif