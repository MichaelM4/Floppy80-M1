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

//-----------------------------------------------------------------------------
void __not_in_flash_func(hdc_port_out)(word addr, byte data)
{
#ifdef ENABLE_LOGGING
	fdc_log[log_head].type = port_out;
	fdc_log[log_head].val = data;
    fdc_log[log_head].op1 = addr & 0xFF;
	++log_head;
	log_head = log_head % LOG_SIZE;
#endif
}

//-----------------------------------------------------------------------------
byte __not_in_flash_func(hdc_port_in)(word addr)
{
    byte data = 0x55;

#ifdef ENABLE_LOGGING
	fdc_log[log_head].type = port_in;
	fdc_log[log_head].val = data;
    fdc_log[log_head].op1 = addr & 0xFF;
	++log_head;
	log_head = log_head % LOG_SIZE;
#endif

    return data;
}
