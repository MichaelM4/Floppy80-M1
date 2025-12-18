# Floppy 80 (M1) User Guide

## Configuration

An SD Card (FAT formatted) is required to contain the disk images, and configuration.

Configuration of the Floppy80-M1 is performed with the placement of
files on the SD-Card inserted in its card reader. 
Files are placed in the Root folder (unless noted), subfolders are 
not generally supported. The files are as follows:

### system.cfg

This file contains global configuration settings. The file and settings are
optional, and all have meaningful defaults The settings are:
* MEM - Used to enable / disable 32KB RAM; 1 = enabled (default); 0 = disabled
* VHD - Used to enable / disable Hard Disk; 1 = enabled (default); 0 = disabled
* WAIT - Used to enable / disable wait states 1 = enabled; 0 = disabled (default)

e.g.
```
MEM=1
VHD=0
WAIT=0
```

Wait states are used to slow the Z80 CPU, to get it to wait for all
floppy disk controller actions to complete before continuing.
Typically, wait states should not be required.

Wait states are useful for overclocked CPU's where the CPU outperforms
the Floppy emulation, which is optimised for normal CPU speed.
The issue with wait states is they are known to disrupt
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
If filter is specified only files containing the filter character sequence are displayed.  
If filter is not specified all files are displayed.

#### FDC INI [filename.ini]

Switches between the different INI file on the SD-Card. 
If filename.ini is not specified a list of INI files on the SD-Card will be displayed 
and you can select the one to write to boot.cfg

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

## Operating Systems

### CPM 1.4.1 (Lifeboat)

Format works.

### CPM 1.5 (FGM)

Format works.

### Double DOS 4.2.4

Format works.

### DOS Plus 3.50

Format works, provided you start with a double sides dmk file.

Backup works as long as disk doubler is disabled

### LDOS 5.3.1

Format works, supports only single density at this time, and requires a reboot after formatting drive.

### MultiDOS 4.01

Format works.

Backup works, as long as disk doublers are disabled.

### NEWDOS 3.0

Format works.

### NEWDOS 80 V2.0

Format works.

### TRSDOS 2.3

Format works.

### UltraDOS 4.2.0

Format works.

### VTDOS 3.0.0

Format works.

BACKUP - Different Pack IDs

### VTDOS 4.0.1

Format works.

Backup works, as long as disk doublers are disabled.

## Debugging

The Firmware provides a debugging interface for logging and diagnosing issues.

While running the Floppy 80 provides a Command Line interface via serial
over the USB port on the Pi PICO. Simply connect a USB can to a host computer and
connect a Serial Communications program to the serial port

While the port may appear differently on different operating systems on a Mac
it appears something like `dev/cu.usbmodem000000`. The port is not a true serial port
and has no explicit Baud Rate, etc. 

Pressing enter should display a help menu explaining the basic commands.
A few of the commands have matching `FDC` commands

| COMMAND | FDC     | DESCRIPTION                             |
|---------|---------|-----------------------------------------|
| boot f  | FDC INI | Set INI file (f) to use for boot        |
| disks   |         | Display information about mounted disks |
| dir f   | FDC DIR | Display a Directory (optional filter)   |
| dump n  |         | Dump Drive (n) contents                 |
| hdc     |         | Create a Virtual Hard Disk              |
| help    |         | Display CLI Help screen                 |
| logon   |         | Enable FDC Debug Output                 |
| logoff  |         | Disable FDC Debug Output                |
| status  | FDC STA | Display Status                          |

Some of the more important commands are described below

### Create Virtual Hard Disks

**t.b.d.**

### Dump Drive Contents

`dump n` - where n is the drive number. Display a complete sector-by-sector list
of the chosen (mounted) disk to the console

### Logon / Logoff

Enables or disables the output to the console of FDC debugging activity information

