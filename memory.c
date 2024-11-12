#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "defines.h"
#include "fdc.h"

#define NopDelay() __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();

static byte by_memory[0x8000];

volatile byte g_byRtcIntrActive;
volatile byte g_byFdcIntrActive;
volatile byte g_byResetActive;
volatile byte g_byEnableIntr;

extern FdcType g_FDC;

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceMemoryRead)(byte data)
{
    clr_gpio(DIR_PIN);      // B to A direction
    set_bus_as_output();    // make data pins (D0-D7) outputs
    clr_gpio(DATAB_OE_PIN); // enable data bus transciever

    // put byte on data bus
    put_byte_on_bus(data);

    clr_gpio(WAIT_PIN);
    while (get_gpio(MREQ_PIN) == 0);

    // turn bus around
    set_gpio(DATAB_OE_PIN); // disable data bus transciever
    set_bus_as_input();     // reset data pins (D0-D7) inputs
    set_gpio(DIR_PIN);      // A to B direction
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcResponseOperation)(word addr)
{
    byte data;

    // wait for RD or WR to go active or MREQ to go inactive
    while ((get_gpio(RD_PIN) != 0) && (get_gpio(WR_PIN) != 0) && (get_gpio(MREQ_PIN) == 0));

    if (get_gpio(RD_PIN) == 0)
    {
        addr -= FDC_RESPONSE_ADDR_START;
        data = fdc_get_response_byte(addr);
        ServiceMemoryRead(data);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcRequestOperation)(word addr)
{
    byte data;

    // wait for RD or WR to go active or MREQ to go inactive
    while ((get_gpio(RD_PIN) != 0) && (get_gpio(WR_PIN) != 0) && (get_gpio(MREQ_PIN) == 0));

    if (get_gpio(WR_PIN) == 0)
    {
        // get data byte
        clr_gpio(DATAB_OE_PIN);
        NopDelay();
        data = get_gpio_data_byte();
        set_gpio(DATAB_OE_PIN);

        addr -= FDC_REQUEST_ADDR_START;
        fdc_put_request_byte(addr, data);

    }

    clr_gpio(WAIT_PIN);
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceHighMemoryOperation)(word addr)
{
    // wait for RD or WR to go active or MREQ to go inactive
    while ((get_gpio(RD_PIN) != 0) && (get_gpio(WR_PIN) != 0) && (get_gpio(MREQ_PIN) == 0));

    if (get_gpio(RD_PIN) == 0)
    {
        ServiceMemoryRead(by_memory[addr-0x8000]);
    }
    else if (get_gpio(WR_PIN) == 0)
    {
        // get data byte
        clr_gpio(DATAB_OE_PIN);
        NopDelay();
        by_memory[addr-0x8000] = get_gpio_data_byte();
        set_gpio(DATAB_OE_PIN);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcReadOperation)(word addr)
{
    byte data;

    switch (addr)
    {
        case 0x37E0: // RTC
        case 0x37E1:
        case 0x37E2:
        case 0x37E3:
            data = 0x3F;

            if (g_byIntrRequest)
            {
                data |= 0x40;
            }

            if (g_byRtcIntrActive)
            {
                data |= 0x80;
                g_byRtcIntrActive = false;

                if (!g_byFdcIntrActive)
                {
                    // deactivate intr
                    clr_gpio(INT_PIN);
                }
            }

            break;

        case 0x37EC: // Cmd/Status register
            if (!g_byRtcIntrActive)
            {
                clr_gpio(INT_PIN);
            }

        case 0x37ED: // Track register
        case 0x37EE: // Sector register
        case 0x37EF: // Data register
            data = fdc_read(addr);
            break;

        default:
            return;
    }

    ServiceMemoryRead(data);
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcWriteOperation)(word addr)
{
    byte data;

    switch (addr)
    {
        case 0x37E0:
        case 0x37E1:
        case 0x37E2:
        case 0x37E3: // drive select
            // get data byte
            clr_gpio(DATAB_OE_PIN);
            NopDelay();
            data = get_gpio_data_byte();
            set_gpio(DATAB_OE_PIN);

            fdc_write_drive_select(data);
            break;

        case 0x37EC: // Cmd/Status register
        case 0x37ED: // Track register
        case 0x37EE: // Sector register
        case 0x37EF: // Data register
            // get data byte
            clr_gpio(DATAB_OE_PIN);
            NopDelay();
            data = get_gpio_data_byte();
            set_gpio(DATAB_OE_PIN);

            fdc_write(addr, data);
            break;
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServiceFdcMemoryOperation)(word addr)
{
    // wait for RD or WR to go active or MREQ to go inactive
    while ((get_gpio(RD_PIN) != 0) && (get_gpio(WR_PIN) != 0) && (get_gpio(MREQ_PIN) == 0));

    if (get_gpio(RD_PIN) == 0)
    {
        ServiceFdcReadOperation(addr);
    }
    else if (get_gpio(WR_PIN) == 0)
    {
        ServiceFdcWriteOperation(addr);
    }
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(service_memory)(void)
{
    union {
        byte b[2];
        word w;
    } addr;

    while (1)
    {
        clr_gpio(WAIT_PIN);

        while (!get_gpio(SYSRES_PIN))
        {
           	g_byResetActive = true;
        }

       	g_byResetActive = false;

        // wait for MREQ to go inactive
        while (get_gpio(MREQ_PIN) == 0);

        // wait for MREQ to go active
        while (get_gpio(MREQ_PIN) != 0);

#ifdef PICO_RP2040
        set_gpio(WAIT_PIN);
#endif

        if (g_byEnableIntr)
        {
            g_byEnableIntr = false;
        	set_gpio(INT_PIN); // activate intr
        }

        // read low address byte
        clr_gpio(ADDRL_OE_PIN);
        NopDelay();
        addr.b[0] = get_gpio_data_byte();
        set_gpio(ADDRL_OE_PIN);

        // read high address byte
        clr_gpio(ADDRH_OE_PIN);
        NopDelay();
        addr.b[1] = get_gpio_data_byte();
        set_gpio(ADDRH_OE_PIN);

        if (addr.w >= 0x8000)
        {
            ServiceHighMemoryOperation(addr.w);
        }
        else if ((addr.w >= 0x37E0) && (addr.w <= 0x37EF))
        {
            if ((get_gpio(RD_PIN) == 0) && (addr.w == 0x37EC)) // reading status register
            {
                if (!g_byRtcIntrActive)
                {
                    clr_gpio(INT_PIN);
                }

                if (g_byIntrRequest)
                {
                    g_byIntrRequest = 0;
                    g_byFdcIntrActive = false;
                }

                ServiceMemoryRead(g_FDC.byStatus);
            }
            else
            {
                set_gpio(WAIT_PIN);
                ServiceFdcMemoryOperation(addr.w);
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
   }
}
