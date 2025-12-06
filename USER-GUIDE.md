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
A Hard Disk Controller
  - supports up to 2 hard drives
  - Uses same image format as FreHD 
- 32k RAM expansion (optional)
- 40Hz RTC interrupt generation

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

See the [Separate Guide](BUILDING.md) for building the Floppy-80 software

## Configuration

An SD Card (FAT formatted), is required to contain the disk images, and configuration

Configuration of the Floppy80-M1 is performed with the placement of
files on the SD-Card inserted in its card reader. 
Files are placed in the Root folder (unless noted), sub folders are 
not generally supported. The files are as follows:

### system.cfg

This file contains global configuraton settings. The file and settings are
optional and all have meaninful defaults The settings are:
* MEM - Used to enable / disable 32KB RAM; 1 = enabled (default); 0 = disabled
* WAIT - Used to enable / disable wait states 1 = enabled; 0 = disabled (default)

e.g.
```
MEM=1
WAIT=0
```

Wait states are used to slow the Z80 CPU, to get it to wait for all
floppy disk controller actions to complete before continuing.
Typically, wait states should not be required.

Wait states are useful for overclocked CPU's where the CPU outperforms
the Floppy emulation, which is optimised for normal CPU speed.
The issue with wait states it they are known to disrupt
critical timed operations, such as formatting a floppy disk.

### boot.cfg
Specify the default INI file to load at reset of the Floppy80
when the floppy 80 boots or is reset it reads the contents of
the boot.cfg to determine the default configuration INI file.

This file contains (ONLY) the name of the current INI file being used e.g.

e.g.
```
LD531.INI
```

### INI files
Specifies the disk images and options after reset. Options 
* Drive0 - specified the image to load for drive :0
* Drive1 - specified the image to load for drive :1
* Drive2 - specified the image to load for drive :2
* HD0 - specifies the image to load for the first hard drive
* HD1 - specifies the image to load for the second hard drive
* Doubler - 1 = doubler is enabled; 0 = doubler is disabled;

e.g.
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
A folder which can contain blank floppy images, these are not mountable.
Instead, they used by the `FDC FMT` command. See [FDC FOR](#fdc-for) section below

## Operation

The Floppy80 should be powered on before the Main computer is powered.
Power is provided by the USB port on the Pi Pico microcontroller.

The Floppy-80 has an LED which shows disk activity.

### Startup

When Floppy80 first starts it uses `boot.cfg` to load the default 
disk images.

### FDC Utility

Is a utility to interact with the Floppy80 from within the
TRS-80 Model I operating environment.  Versions of FDC exist
for both the CPM (Lifeboard and FMG) and most TRS DOS based operating Systems.

#### FDC STA

Displays a status, and the contents to the INI file specified by boot.cfg

#### FDC DIR [fiter]

Displays a list of files in the root folder of the SD-Card.
If filter is specified only files contain the filter character sequence are displayed.  
If filter is not specified all files are displayed.

#### FDC INI [filename.ini]

Switches between the different INI file on the SD-Card. 
If filename.ini is not specified a list of INI files on the SD-Card will be displayed allow you to select the one to write to boot.cfg

#### FDC DMK [filename.dmk] [0/1/2]

Allows the mounting of DMK disk images in the root folder of the SD-Card for a specified drive.
When filename.dmk is specified it will mount the DMK file names by filename.dmk into the drive specified
[0/1/2].  For example

`FDC DMK LDOS-DATA.DMK 2` will mount the DMK file LDOS-DATA.DMK into drive :2
`FDC DMK` - will list DMK files allowing you to select the file, and the drive to mount it into

#### FDC FOR

Format a Floppy Disk - Copies a DMK disk image from the `/FMT` folder of the SD-Card 
to one of the mounted disk images (0, 1 or 2)

List the files contained in the `/FMT` folder of the SD-Card from which you can select one 
and specify the drive image to replace with it.

This utility is a backup if the native format function does not work.

#### FDC IMP [filename.ext] :n

imports the specified file from the root folder of the FAT32 formatted SD-Card to the disk image indicated by n.
imports a file from the root folder of the SD-Card into one of the mounted disk images (0, 1 or 2).

