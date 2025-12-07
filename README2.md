# Floppy 80 for the TRS-80 Model I

## Features

The Floppy-80 is a Hardware and software solution that emulates 
the following features of the Expansion Interface
- A Floppy Disc controller
  - supports 3 floppy drives
  - single and double-density
  - single and double-sided
  - up to 90 cylinders
  - DMK disk format supported
- 32k RAM expansion (optional)
- 40Hz RTC interrupt generation

Additionally, the following features are provided:
- A Hard Disk Controller (optional)
  - supports up to 2 hard drives
  - Uses same image format as FreHD
- Wait State Generation (optional)
- Software Debugging Interface

An SD card is required to store disk images and configuration data

## Hardware

The Floppy-80 consists of a board that connects to the Model-1 expansion edge connector.
The board contains an SD card socket to provide disk images and a
Raspberry Pi Pico 2 microcontroller which does the emulation

[Building and testing](HARDWARE.md) the board is covered separately.

## Software

The software provided includes 2 main components
* Firmware running on Raspberry Pi, that provides the emulation
* FDC Utility (TRS DOS, and CPM) running on TRS-80 for emulator control

See the separate [Software Build Guide](BUILDING.md) for details on 
building the Floppy-80 software

## User Guide

For User Instructions please see the separate [User Guide](USER-GUIDE.md)

## Credits

## Resources


