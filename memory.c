#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/structs/systick.h"

#include "defines.h"
#include "fdc.h"
#include "video.h"

#define NopDelay() __nop(); __nop(); __nop(); __nop(); __nop(); __nop();

static byte by_memory[0x8000];

volatile byte g_byRtcIntrActive;
volatile byte g_byResetActive;
volatile byte g_byEnableIntr;

//-----------------------------------------------------------------------------
void __not_in_flash_func(FinishReadOperation)(byte data)
{
    clr_gpio(DIR_PIN);      // B to A direction
    set_bus_as_output();    // make data pins (D0-D7) outputs
    clr_gpio(DATAB_OE_PIN); // enable data bus transciever

    // put byte on data bus
    put_byte_on_bus(data);

    clr_gpio(WAIT_PIN);
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcResponseOperation)(word addr)
{
    byte data;
    
    // wait for RD or WR to go active or MREQ to go inactive
    while (get_gpio(RD_PIN) && get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(RD_PIN))
    {
        addr -= FDC_RESPONSE_ADDR_START;
        data = fdc_get_response_byte(addr);
        FinishReadOperation(data);
    }
    else if (!get_gpio(WR_PIN))
    {
        addr -= FDC_RESPONSE_ADDR_START;
        fdc_put_response_byte(addr, data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcRequestOperation)(word addr)
{
    byte data;
    
    if (get_gpio(RD_PIN)) // assume to be a WR
    {
        clr_gpio(DATAB_OE_PIN);
        NopDelay();
        data = get_gpio_data_byte();
        set_gpio(DATAB_OE_PIN);
    }

    // wait for RD or WR to go active or MREQ to go inactive
    while (get_gpio(RD_PIN) && get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(RD_PIN))
    {
        addr -= FDC_REQUEST_ADDR_START;
        data = fdc_get_request_byte(addr);
        FinishReadOperation(data);
    }
    else if (!get_gpio(WR_PIN))
    {
        addr -= FDC_REQUEST_ADDR_START;
        fdc_put_request_byte(addr, data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceHighMemoryOperation)(word addr)
{
    byte data;
    byte* pby = &by_memory[addr-0x8000];
    
    if (!get_gpio(RD_PIN))
    {
        FinishReadOperation(*pby);
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for RD or WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        *pby = data;
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcDriveSelectOperation)(void)
{
    byte data;
    
    if (!get_gpio(RD_PIN))
    {
        data = fdc_read_drive_select();
        FinishReadOperation(data);
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        fdc_write_drive_select(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcCmdStatusOperation)(void)
{
    byte data;

    if (!get_gpio(RD_PIN))
    {
        // release INT line if the interrupt was cause by the WD controller
        if (!g_byRtcIntrActive)
        {
            clr_gpio(INT_PIN);
        }

        FinishReadOperation(fdc_read_status());
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        fdc_write_cmd(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcTrackOperation)(void)
{
    byte data;
    
    if (!get_gpio(RD_PIN))
    {
        FinishReadOperation(fdc_read_track());
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        fdc_write_track(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcSectorOperation)(void)
{
    byte data;
    
    if (!get_gpio(RD_PIN))
    {
        FinishReadOperation(fdc_read_sector());
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        fdc_write_sector(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcDataOperation)(void)
{
    byte data;

    if (!get_gpio(RD_PIN))
    {
        FinishReadOperation(fdc_read_data());
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        fdc_write_data(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceVideoMemoryOperation)(word addr)
{
    byte data;

    if (!get_gpio(RD_PIN))
    {
        return;
    }

//    addr -= VIDEO_ADDR_START;

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    // wait for WR to go active or MREQ to go inactive
    while (get_gpio(WR_PIN) && !get_gpio(MREQ_PIN));

    if (!get_gpio(WR_PIN))
    {
        VideoWrite(addr, data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(service_memory)(void)
{
    byte data;
    union {
        byte b[2];
        word w;
    } addr;

    // systick_hw->csr = 0x5;
    // systick_hw->rvr = 0x00FFFFFF;

    while (1)
    {
        clr_gpio(WAIT_PIN);

        while (!get_gpio(SYSRES_PIN))
        {
           	g_byResetActive = true;
        }

       	g_byResetActive = false;

        // wait for MREQ to go inactive
        while (!get_gpio(MREQ_PIN));

        // turn bus around
        set_gpio(DATAB_OE_PIN); // disable data bus transciever
        set_bus_as_input();     // reset data pins (D0-D7) inputs
        set_gpio(DIR_PIN);      // A to B direction

        // read low address byte, address is put on bus before MREQ goes low
        // so we can read it while waiting for MREQ to go active (low)
        // saves time after MREQ goes low
        clr_gpio(ADDRL_OE_PIN);
        NopDelay();

        // wait for MREQ to go active, reading low address byte while waiting
        do {
            addr.b[0] = get_gpio_data_byte();
        } while (get_gpio(MREQ_PIN));

        set_gpio(ADDRL_OE_PIN);

#ifdef PICO_RP2040
        set_gpio(WAIT_PIN);
#endif

        // start = systick_hw->cvr;

        if (g_byEnableIntr)
        {
            g_byEnableIntr = false;
        	set_gpio(INT_PIN); // activate intr
        }

        // read high address byte
        clr_gpio(ADDRH_OE_PIN);
        NopDelay();
        addr.b[1] = get_gpio_data_byte();
        set_gpio(ADDRH_OE_PIN);

        if ((addr.w >= VIDEO_ADDR_START) && (addr.w <= VIDEO_ADDR_END))
        {
            ServiceVideoMemoryOperation(addr.w);
        }
        else if (addr.w >= 0x8000)
        {
            ServiceHighMemoryOperation(addr.w);
        }
        else if ((addr.w >= 0x37E0) && (addr.w <= 0x37EF))
        {
            switch (addr.w)
            {
                case 0x37E0:
                case 0x37E1:
                case 0x37E2:
                case 0x37E3:
                    ServiceFdcDriveSelectOperation();
                    break;

                case 0x37EC:
                    ServiceFdcCmdStatusOperation();
                    break;

                case 0x37ED:
                    ServiceFdcTrackOperation();
                    break;

                case 0x37EE:
                    ServiceFdcSectorOperation();
                    break;

                case 0x37EF:
                    ServiceFdcDataOperation();
                    break;
            }
        }
        else if ((addr.w >= FDC_REQUEST_ADDR_START) && (addr.w <= FDC_REQUEST_ADDR_STOP))
        {
            set_gpio(WAIT_PIN);
            ServiceFdcRequestOperation(addr.w);
        }
        else if ((addr.w >= FDC_RESPONSE_ADDR_START) && (addr.w <= FDC_RESPONSE_ADDR_STOP))
        {
            set_gpio(WAIT_PIN);
            ServiceFdcResponseOperation(addr.w);
        }

    	// end = systick_hw->cvr;
        // duration = (start & 0x00FFFFFF) - (end & 0x00FFFFFF);
   }
}
