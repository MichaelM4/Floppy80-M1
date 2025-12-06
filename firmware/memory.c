#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/structs/systick.h"

#include "defines.h"
#include "fdc.h"
#include "hdc.h"

#define NopDelay() __nop(); __nop(); __nop(); __nop(); __nop(); __nop();

extern BufferType g_bFdcRequest;
extern BufferType g_bFdcResponse;

static byte by_memory[0x8000];

volatile byte g_byFdcIntrActive;
volatile byte g_byRtcIntrActive;
volatile byte g_byResetActive;
volatile byte g_byEnableIntr;
volatile byte g_byEnableUpperMem;
volatile byte g_byEnableWaitStates;
volatile byte g_byEnableVhd = true;

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
    byte* pby;
    byte  data;

    addr -= FDC_RESPONSE_ADDR_START;

    if (addr < FDC_CMD_SIZE)
    {
        pby = &g_bFdcResponse.cmd[addr];
    }
    else
    {
        pby = &g_bFdcResponse.buf[addr-FDC_CMD_SIZE];
    }

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
void __not_in_flash_func(ServiceFdcRequestOperation)(word addr)
{
    byte  data;
    byte* pby;

    addr -= FDC_REQUEST_ADDR_START;

    if (addr < FDC_CMD_SIZE)
    {
        pby = &g_bFdcRequest.cmd[addr];
    }
    else
    {
        pby = &g_bFdcRequest.buf[addr-FDC_CMD_SIZE];
    }

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
        data = g_byDriveStatus;

        if (g_byRtcIntrActive)
        {
            data |= 0x80;
        }

        FinishReadOperation(data);

        if (g_byRtcIntrActive)
        {
            g_byRtcIntrActive = false;

            if (!g_byFdcIntrActive)
            {
                // deactivate intr
                clr_gpio(INT_PIN);
            }
        }

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
        FinishReadOperation(fdc_read_status());

        if (!g_byRtcIntrActive) // then caused by WD controller, so clear it
        {
            clr_gpio(INT_PIN);
        }

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
void __not_in_flash_func(ServicePortIn)(word addr)
{
    byte data;

    if (get_gpio(IN_PIN))
    {
        return;
    }

    addr = addr & 0xFF;

    if ((addr < 0xC0) || (addr > 0xCF) || (g_byEnableVhd == 0))
    {
        return;
    }

    FinishReadOperation(hdc_port_in(addr));
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(ServicePortOut)(word addr)
{
    byte data;

    if (get_gpio(OUT_PIN))
    {
        return;
    }

    addr = addr & 0xFF;

    if ((addr < 0xC0) || (addr > 0xCF) || (g_byEnableVhd == 0))
    {
        return;
    }

    clr_gpio(DATAB_OE_PIN);
    NopDelay();
    data = get_gpio_data_byte();
    set_gpio(DATAB_OE_PIN);

    hdc_port_out(addr, data);
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(service_memory)(void)
{
    register word bus;
    register word addr;

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

        // wait for MREQ, IN, RD, WR and OUT to go inactive
        do {
            bus = get_gpio_read_bus();
        } while ((bus & 0x1F) != 0x1F);

        // turn bus around
        set_gpio(DATAB_OE_PIN); // disable data bus transciever
        set_bus_as_input();     // reset data pins (D0-D7) inputs
        set_gpio(DIR_PIN);      // A to B direction

        // read low address byte, address is put on bus before MREQ goes low
        // so we can read it while waiting for MREQ to go active (low)
        // saves time after MREQ goes low
        clr_gpio(ADDRL_OE_PIN);
        NopDelay();

        // wait for MREQ, IN, RD, WR or OUT to go active, reading low address byte while waiting
        do {
            bus = get_gpio_read_bus();
        } while ((bus & 0x1F) == 0x1F);

        if (g_byEnableWaitStates)
        {
            set_gpio(WAIT_PIN);
        }

        addr = (bus >> (D0_PIN - IN_PIN)) & 0xFF;
        set_gpio(ADDRL_OE_PIN);
        clr_gpio(ADDRH_OE_PIN);

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
        addr = addr + ((get_gpio_data_byte() & 0xFF) << 8);
        set_gpio(ADDRH_OE_PIN);

        if (!(bus & 0x01))
        {
            ServicePortIn(addr);
        }
        else if (!(bus & 0x08))
        {
            ServicePortOut(addr);
        }
        else if (addr >= 0x8000)
        {
            if (g_byEnableUpperMem)
            {
                ServiceHighMemoryOperation(addr);
            }
        }
        else
        {
            switch (addr)
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
                    set_gpio(WAIT_PIN);
                    ServiceFdcDataOperation();
                    break;

                default:
                    if ((addr >= FDC_REQUEST_ADDR_START) && (addr <= FDC_REQUEST_ADDR_STOP))
                    {
                        ServiceFdcRequestOperation(addr);
                    }
                    else if ((addr >= FDC_RESPONSE_ADDR_START) && (addr <= FDC_RESPONSE_ADDR_STOP))
                    {
                        ServiceFdcResponseOperation(addr);
                    }

                    break;
            }
        }

        // end = systick_hw->cvr;
        // duration = (start & 0x00FFFFFF) - (end & 0x00FFFFFF);
    }
}
