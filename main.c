#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/regs/intctrl.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"

#include "fdc.pio.h"
#include "defines.h"
#include "sd_core.h"
#include "fdc.h"
#include "system.h"

#if (ENABLE_TRACE_LOG == 1)
	void RecordBusHistory(DWORD dwBus, BYTE byData);
#endif

byte by_memory[0x8000];

byte g_byRtcIntrActive;
byte g_byFdcIntrActive;

///////////////////////////////////////////////////////////////////////////////
// API documentions is located at
// https://raspberrypi.github.io/pico-sdk-doxygen/
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// to get memory usage of the compilied program enter
//
//   size build/Floppy80.elf
//
// in a bash terminal window
//
// Output is
// - text gives you the size of the text segment (or code segment).
// - data gives you the size of the data segment.
// - bss gives you the size of the block started by symbol segment.
// - dec is the size of the text, data and bss size added together in decimal, and hex is the same number in hexadecimal.
///////////////////////////////////////////////////////////////////////////////

pio_sm_config g_pio_config;
PIO           g_pio;
uint          g_sm, g_offset;

///////////////////////////////////////////////////////////////////////////////
// GPIO NET        IN   OUT
//-----------------------------------------------------------------------------
// 0	  DEBUG_TXT			   * (Serial transmit for debug messages)
// 1    WAIT             * (drives a transistor to pull the Z80 Wait line low)
// 2    CLK_SCLK         * (SD-Card)
// 3    CMD_MOSI         * (SD-Card)
//
// 4    DAT0_MISO   *      (SD-Card)
// 5    DAT3_CS          * (SD-Card)
// 6    CD               * (SD-Card, Card Detect)
// 7    ADDRL_OE         * (Enable the low (A0-A7) byte address buffer for reading)
//
// 8    DATAB_OE         * (Enable the data bus for reading)
// 9    IN          *      (Z80 bus for port I/O, input)
// 10   ADDRH_OE         * (Enable the high (A8-A15) byte address buffer for reading)
// 11   RD          *      (Z80 bus for memory read)
//
// 12   WR          *      (Z80 bus for memory write)
// 13   OUT         *      (Z80 bus for port I/O, output)
// 14   LED              * (active low enable for the activity LED)
// 15   INT              * (drives a transistor to pull the Z80 interrupt line low)
//
// 16   D0          *    * (bit 0 of the data bus to read A0-A7, A8-A15 and D0-D7, and wite D0-D7)
// 17   D1          *    * (bit 1 of the data bus)
// 18   D2          *    * (bit 2 of the data bus)
// 19   D3          *    * (bit 3 of the data bus)
//
// 20   D4          *    * (bit 4 of the data bus)
// 21   D5          *    * (bit 5 of the data bus)
// 22   D6          *    * (bit 6 of the data bus)
// 23   NA
//
// 24   NA
// 25   NA
// 26   D7          *    * (bit 7 of the data bus)
// 27   DIR              * (controls the direction of the data bus buffer)
//
// 28   SYSRES      *      (Z80 bus reset state)
// 29	-
// 30	-
// 31	-
//
//-----------------------------------------------------------------------------
void InitGPIO(void)
{
  gpio_init(INT_PIN);
  gpio_set_dir(INT_PIN, GPIO_OUT);
  gpio_set_slew_rate(INT_PIN, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(INT_PIN, GPIO_DRIVE_STRENGTH_12MA);
  gpio_put(INT_PIN, 0); // deactivate intr

  gpio_init(WAIT_PIN);
  gpio_set_dir(WAIT_PIN, GPIO_OUT);
  gpio_set_slew_rate(WAIT_PIN, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(WAIT_PIN, GPIO_DRIVE_STRENGTH_12MA);
  gpio_put(WAIT_PIN, 0); // deactivate wait

  // CLK_SCLK - initailized by the SD-Card library
  // CMD_MOSI
  // DAT0_MISO
  // DAT3_CS

  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  gpio_set_slew_rate(LED_PIN, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(LED_PIN, GPIO_DRIVE_STRENGTH_12MA);
  gpio_put(LED_PIN, 1); // LED off

  gpio_init(ADDRL_OE_PIN);
  gpio_set_dir(ADDRL_OE_PIN, GPIO_OUT);
  gpio_set_slew_rate(ADDRL_OE_PIN, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(ADDRL_OE_PIN, GPIO_DRIVE_STRENGTH_12MA);
  gpio_put(ADDRL_OE_PIN, 1); // deactivate low address bus input buffer

  gpio_init(DATAB_OE_PIN);
  gpio_set_dir(DATAB_OE_PIN, GPIO_OUT);
  gpio_put(DATAB_OE_PIN, 1); // deactivate data bus I/O buffer

  gpio_init(ADDRH_OE_PIN);
  gpio_set_dir(ADDRH_OE_PIN, GPIO_OUT);
  gpio_put(ADDRH_OE_PIN, 1); // deactivate high address bus input buffer

  gpio_init(IN_PIN);
  gpio_set_dir(IN_PIN, GPIO_IN);

  gpio_init(RD_PIN);
  gpio_set_dir(RD_PIN, GPIO_IN);

  gpio_init(WR_PIN);
  gpio_set_dir(WR_PIN, GPIO_IN);

  gpio_init(OUT_PIN);
  gpio_set_dir(OUT_PIN, GPIO_IN);

  gpio_init(MREQ_PIN);
  gpio_set_dir(MREQ_PIN, GPIO_IN);
 
  gpio_init(D0_PIN);
  gpio_set_dir(D0_PIN, GPIO_IN);

  gpio_init(D1_PIN);
  gpio_set_dir(D1_PIN, GPIO_IN);

  gpio_init(D2_PIN);
  gpio_set_dir(D2_PIN, GPIO_IN);

  gpio_init(D3_PIN);
  gpio_set_dir(D3_PIN, GPIO_IN);

  gpio_init(D4_PIN);
  gpio_set_dir(D4_PIN, GPIO_IN);

  gpio_init(D5_PIN);
  gpio_set_dir(D5_PIN, GPIO_IN);

  gpio_init(D6_PIN);
  gpio_set_dir(D6_PIN, GPIO_IN);

  gpio_init(D7_PIN);
  gpio_set_dir(D7_PIN, GPIO_IN);

  gpio_init(DIR_PIN);
  gpio_set_dir(DIR_PIN, GPIO_OUT);
  gpio_put(DIR_PIN, A_TO_B_DIR); // A to B direction

  gpio_init(SYSRES_PIN);
  gpio_set_dir(SYSRES_PIN, GPIO_IN);

  gpio_init(CD_PIN);
  gpio_set_dir(CD_PIN, GPIO_IN);
}

//-----------------------------------------------------------------------------
void InitPIO(void)
{
  g_pio    = pio0;
  g_sm     = pio_claim_unused_sm(g_pio, true);
  g_offset = pio_add_program(g_pio, &fdc_program);

  fdc_program_init(g_pio, g_sm, g_offset, &g_pio_config);

  // Start running the PIO program in the state machine
  pio_sm_set_enabled(g_pio, g_sm, true);
}

///////////////////////////////////////////////////////////////////////////////
// restart pio code to release WAIT state
void __not_in_flash_func(ReleaseWait)(void)
{
   // empty fifo
   pio_sm_clear_fifos(g_pio, g_sm);

  /////////////////////////////////////////////////////////////////////////////////////////////
	// restart the pio state machine at its first instruction (the address for which is g_offset)
  /////////////////////////////////////////////////////////////////////////////////////////////

	// set x, g_offset
	//   [3 bit opcode]      = 111
	//   [5 bits = delay]    = 00000
	//   [3 bits detination] = 001 = x
	//   [5 bits data]       = g_offset
	//   -------------------------------
	//   1110  0000 0010 0000 + [offset]
	pio_sm_exec(g_pio, g_sm, 0xE020 + g_offset);

	// mov pc, x
	//   [3 bit opcode]      = 101
	//   [5 bits = delay]    = 00000
	//   [3 bits detination] = 101 = pc
	//   [2 bits Op]         = 00  = None
	//   [3 bits Source]     = 001 = x
	//   -------------------------------
	//   1010  0000 1010 0001
	pio_sm_exec(g_pio, g_sm, 0xA0A1);

  // remove the first push of the PIO SM after IN, RD, WR, OUT and MREQ are all high
  // indicating the end of a memory access cycle
  pio_sm_get_blocking(g_pio, g_sm);
}

//-----------------------------------------------------------------------------
void __not_in_flash_func(service_memory)(void)
{
  byte rdwr;
  byte byHoldCount;
  union {
    byte b[2];
    word w;
  } addr;

  while (1)
  {
    rdwr = 31;

    // rdwr = g_pio->rxf[g_sm];
    //   1 => WR
    //   2 => RD
    //   31 => IN, RD, WR, OUT and MREQ are all high
    do
    {
      byHoldCount = 0;

      do
      {
        ++byHoldCount;

        if (byHoldCount > 250) // then wait might be stuck on
        {
          // pio_sm_exec(g_pio, g_sm, 0xB042); // nop side 0
          g_pio->sm[g_sm].instr = 0xB042; // nop side 0
        }
      } while (pio_sm_is_rx_fifo_empty(g_pio, g_sm));

      // rdwr = pio_sm_get(g_pio, g_sm);
      rdwr = g_pio->rxf[g_sm];
    } while (rdwr == 31); // wait while IN, RD, WR, OUT and MREQ are all high
  
    // read low address byte
    sio_hw->gpio_clr = 1 << ADDRL_OE_PIN;
    asm(
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
    );
    addr.b[0] = sio_hw->gpio_in >> D0_PIN;
    sio_hw->gpio_set = 1 << ADDRL_OE_PIN;

    // read high address byte
    sio_hw->gpio_clr = 1 << ADDRH_OE_PIN;
    asm(
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
    );
    addr.b[1] = sio_hw->gpio_in >> D0_PIN;
    sio_hw->gpio_set = 1 << ADDRH_OE_PIN;

    if (rdwr == 2) // RD
    {
      if (addr.w < 0x8000) // RD from lower 32k memory
      {
        switch (addr.w)
        {
          case 0x37E0: // RTC
          case 0x37E1:
          case 0x37E2:
          case 0x37E3:
            rdwr = 0x3F;

            if (g_FDC.status.byIntrRequest)
            {
              rdwr |= 0x40;
            }

            if (g_byRtcIntrActive)
            {
              rdwr |= 0x80;
              g_byRtcIntrActive = false;

              if (!g_byFdcIntrActive)
              {
                gpio_put(INT_PIN, 0); // deactivate intr
              }
            }

            sio_hw->gpio_clr = 1 << DIR_PIN;        // B to A direction
            sio_hw->gpio_oe_set = 0xFF << D0_PIN;   // make data pins (D0-D7) outputs
            sio_hw->gpio_clr = 1 << DATAB_OE_PIN;   // enable data bus transciever

            // put byte on data bus
            sio_hw->gpio_togl = (sio_hw->gpio_out ^ (rdwr << D0_PIN)) & (0xFF << D0_PIN);

            ReleaseWait();

            // turn bus around
            sio_hw->gpio_set = 1 << DATAB_OE_PIN; // disable data bus transciever
            sio_hw->gpio_oe_clr = 0xFF << D0_PIN; // reset data pins (D0-D7) inputs
            sio_hw->gpio_set = 1 << DIR_PIN;      // A to B direction
            break;

          case 0x37EC: // Cmd/Status register
              if (!g_byRtcIntrActive)
              {
                gpio_put(INT_PIN, 0); // deactivate intr
              }

          case 0x37ED: // Track register
          case 0x37EE: // Sector register
          case 0x37EF: // Data register
            rdwr = fdc_read(addr.w);

            sio_hw->gpio_clr = 1 << DIR_PIN;        // B to A direction
            sio_hw->gpio_oe_set = 0xFF << D0_PIN;   // make data pins (D0-D7) outputs
            sio_hw->gpio_clr = 1 << DATAB_OE_PIN;   // enable data bus transciever

            // put byte on data bus
            sio_hw->gpio_togl = (sio_hw->gpio_out ^ (rdwr << D0_PIN)) & (0xFF << D0_PIN);

            ReleaseWait();

            // turn bus around
            sio_hw->gpio_set = 1 << DATAB_OE_PIN; // disable data bus transciever
            sio_hw->gpio_oe_clr = 0xFF << D0_PIN; // reset data pins (D0-D7) inputs
            sio_hw->gpio_set = 1 << DIR_PIN;      // A to B direction
            break;

          default:
            ReleaseWait();
            break;
        }
      }
      else // RD from upper 32k memory
      {
        sio_hw->gpio_clr = 1 << DIR_PIN;        // B to A direction
        sio_hw->gpio_oe_set = 0xFF << D0_PIN;   // make data pins (D0-D7) outputs
        sio_hw->gpio_clr = 1 << DATAB_OE_PIN;   // enable data bus transciever

        // put byte on data bus
        sio_hw->gpio_togl = (sio_hw->gpio_out ^ (by_memory[addr.w-0x8000] << D0_PIN)) & (0xFF << D0_PIN);

        ReleaseWait();

        // turn bus around
        sio_hw->gpio_set = 1 << DATAB_OE_PIN; // disable data bus transciever
        sio_hw->gpio_oe_clr = 0xFF << D0_PIN; // reset data pins (D0-D7) inputs
        sio_hw->gpio_set = 1 << DIR_PIN;      // A to B direction
      }
    }
    else if (rdwr == 1) // WR
    {
      if (addr.w < 0x8000) // WR to lower 32k memory
      {
        switch (addr.w)
        {
          case 0x37E0:
          case 0x37E1:
          case 0x37E2:
          case 0x37E3: // drive select
            // get data byte
            sio_hw->gpio_clr = 1 << DATAB_OE_PIN;
            asm(
              "nop\n\t"
              "nop\n\t"
              "nop\n\t"
            );
            rdwr = sio_hw->gpio_in >> D0_PIN;
            sio_hw->gpio_set = 1 << DATAB_OE_PIN;

            fdc_write_drive_select(rdwr);

            ReleaseWait();
            break;

          case 0x37EC: // Cmd/Status register
          case 0x37ED: // Track register
          case 0x37EE: // Sector register
          case 0x37EF: // Data register
            // get data byte
            sio_hw->gpio_clr = 1 << DATAB_OE_PIN;
            asm(
              "nop\n\t"
              "nop\n\t"
              "nop\n\t"
            );
            rdwr = sio_hw->gpio_in >> D0_PIN;
            sio_hw->gpio_set = 1 << DATAB_OE_PIN;

            fdc_write(addr.w, rdwr);

            ReleaseWait();
            break;
 
          default:
            ReleaseWait();
            break;
        }
      }
      else // WR to upper 32k memory
      {
        // get data byte
        sio_hw->gpio_clr = 1 << DATAB_OE_PIN;
        asm(
          "nop\n\t"
          "nop\n\t"
          "nop\n\t"
        );
        by_memory[addr.w-0x8000] = sio_hw->gpio_in >> D0_PIN;
        sio_hw->gpio_set = 1 << DATAB_OE_PIN;

        ReleaseWait();
      }
    }
    else
    {
      ReleaseWait();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
int main()
 {
  stdio_init_all();

  systick_hw->csr = 0x5;
  systick_hw->rvr = 0x00FFFFFF;

  InitGPIO();
  InitVars();
  SDHC_Init();
  FileSystemInit();
  FdcInit();

  // wait for reset to be released
  while (!gpio_get(SYSRES_PIN));

  InitPIO();
  multicore_launch_core1(service_memory);

  while (true)
  {
    UpdateCounters();
    FdcServiceStateMachine();
  }   
}
