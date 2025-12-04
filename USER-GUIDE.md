# Floppy 80 for the TRS-80 Model I

## Features

The Floppy80-M1 emulates the following features of the Expansion Interface
- 32k RAM expansion (optional)
- Up to 3 floppy drives
  - single and double sided
  - single and double density
  - up to 90 cylinders
  - DMK support, HFE?
- Up to 2 hard drives
  - Uses same image format as FreHD 
- 40Hz RTC interrupt generation

The Floppy-80 comprises these components
* A board (with SD card reader) that plugs into 40pin Expansion Interface
* Firmware running on Raspberry Pi which is on the Floppy-80 board
* FDC Utility (TRS DOS, and CPM) running on TRS-80 for emulator control
* Disk Images and configuration stored on the SD card

## Building the Hardware

The main components Required are
* Printed Circuit Board
* Raspberry Pi Pico 2 microcontroller
* SN74LVCC4245ADWR Buffer Chips (4x)
* SD Card Socket (SMD)
* 40 pin PCB Edge Connector

Building and testing the board is covered separately **TODO**

## Building the Software

### Floppy80 firmware

You need to install the C SDK for RPI 
https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#sdk-setup

CMake and Make are essential for this to work
* On Mac OSX - `brew install cmake`

```
# ensure you are in the firmware directory
cd firmware

# first run cmake - creating a separate build folder 
cmake -B build -S .

# then from the build folder
cd build

# run the make command
make Floppy80
```

this will create Floppy80.uf2 file which you burn to the RPI PICO

Alternately you can use VSCode and the RPI Pico extension.

#### Background Reading 

These are DEV resources:
* https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html
* https://admantium.medium.com/getting-started-with-raspberry-pico-and-cmake-f536e18512e6

### FDC (for TRSDOS)

zmac fdc.asm

### FDC (for CP/M)

**TODO**

## Configuration

SD Card (FAT formatted), to contain the disk images

Configuration of the Floppy80-M1 is performed with the placement of
files on the SD-Card inserted in its card reader. The files are as follows:

### boot.cfg
Specify the default ini file to load at reset of the Floppy80
when the floppy 80 boots or is reset it reads the contents of
the boot.cfg to determine the default configuration ini file.

This file contains (ONLY) the name of the current ININ file being used .e.g.

```
LD531.INI
```

### system.cfg

Contains global settings:
* MEM - Used to enable / disable 32KB RAM; 1 = enabled (default); 0 = disabled
* WAIT - Used to enable / disable wait states 1 = enabled; 0 = disabled (default)

```
MEM=1
WAIT=0
```

Wait states are used to slow the Z80 CPU, to get it to wait for all 
floppy disk controller actions to complete before continuing.
Thypically wait states should not be required. 

Wait states are useful for overclocked CPU's where the CPU outperforms 
the Floppy emulation, which is optimised for normal CPU speed.
The issue with wait states it they are known to disrupt 
critical timed operations, such as formatting a floppy disk.

### INI files
Specifies the disk images and options after reset. Options 
* Drive0 - specified the image to load for drive :0
* Drive1 - specified the image to load for drive :1
* Drive2 - specified the image to load for drive :2
* HD0 - specifies the image to load for the first hard drive
* HD1 - specifies the image to load for the second hard drive
* Doubler - 1 = doubler is enabled; 0 = doubler is disabled;

An example INI File
``` 
Drive0=LD531-0.dmk
Drive1=LD531-1.dmk
Drive2=LD531-2.dmk
HD0=hard0.hdv
Doubler=1
```

### DMK files
Virtual floppy disk images with a specific file format
that allows them to be generated and used with a number
of existing programs and simulators.

### Virtual Hard Drive Images
Virtual hard drive images with a specific file format.
They are the same format as is used by the FreHD and TRS80-GP.

### /FMT folder
A folder which should contain blank floppy images, these are used by the
`FDC FMT` command. See seperate section.

## Operation

### FDC Utility

Is a utility to interact with the Floppy80 from within the
TRS-80 Model I operating environment.  Versions of FDC exist
for the bothe CPM and TRS DOS based operating Systems.

    - CPM 1.4.1 (Lifeboat)
    - CPM 1.5 (FMG)
    - Double DOS 4.2.4
    - DOS Plus 3.50
    - LDOS 5.3.1
    - MultiDOS 4.01
    - NEWDOS 3.0
    - NEWDOS 80 V2.0
    - TRSDOS 2.3
    - UltraDOS 4.2.0
    - VTDOS 3.0.0
    - VTDOS 4.0.1

#### FDC STA

Displays a status, and the contents to the ini file specified by boot.cfg

#### FDC DIR [fiter]

Displays a list of files in the root folder of the SD-Card.
If filter is specified only files contain the filter character sequence are displayed.  
If filter is not specified all files are displayed.

#### FDC INI [filename.ini]

Switches between the different ini file on the SD-Card. 
If filename.ini is not specified a list of ini files on the SD-Card will be displayed allow you to select the one to write to boot.cfg

#### FDC DMK [filename.dmk] [0/1/2]

Allows the mounting of DMK disk images in the root folder of the SD-Card for a specified drive.
When filename.dmk is specified it will mount the dmk file names by filename.dmk into the drive specified
[0/1/2].  For example

`FDC DMK LDOS-DATA.DMK 2` will mount the dmk file LDOS-DATA.DMK into drive :2
`FDC DMK` - will list DMK files allowing you to select the file, and the drive to mount it into

#### FDC FOR

Format a Floppy Disk - Copies a DMK disk image from the `/FMT` folder of the SD-Card 
to one of the mounted disk images (0, 1 or 2)

List the files contained in the FMT folder of the SD-Card from which you can select one and specify the drive image
to replace with it.

#### FDC IMP [filename.ext] :n

imports the specified file from the root folder of the FAT32 formatted SD-Card to the disk image indicated by n.
imports a file from the root folder of the SD-Card into one of the mounted disk images (0, 1 or 2).

### Formatting Floppy Disks

There are three Options
* Native FORMAT command of the Operating system
* `FDC FOR` command is a useful backup if this fails
...


########################################################################

- FDC TRS (utility for the TRSDOS related utility)

    - use TRS80GP to import FDC/CMD onto a disk image

        - trs80gp -m1 -vs -frehd -frehd_dir zout -turbo -mem 48 -d0 dmk\ld531-0.dmk -d1 dmk\ld531-1.dmk -i "IMPORT2 FDC.CMD FDC/CMD:1\r"

- FDC CP/M (utility for the CP/M related utility)
    1. Compile program into a hex file.

       zmac fdc.asm

    2. Run TRS80GP loading the CP/M disk images.

       start trs80gp -m1 -vs -frehd -frehd_dir zout -turbo -mem 48 -d0 CPM141-0.dmk -d1 CPM141-1.dmk -d2 CPM141-2.dmk

    3. Select Load from TRS80GP File menu and select the hex file.

    4. Run "SAVE 20 B:FDC.COM" to save program memory contents to a .com file.
       As the program grows you will need to increase the value after SAVE.

       SAVE n ufn cr

       n   - number of 256-byte pages to be saved.

       ufn - unambiguous file name.

       cr  - carriage return.
