########################################################################

Floppy 80 for the TRS-80 Model I

The Floppy80-M1 emulates the following features of the Expansion Interface
- 32k RAM expansion
- Up to 3 floppy drives
  - 1 and 2 sided
  - single and double density
  - up to 90 cylinders
- Up to 2 hard drives

Configuration of the Floppy80-M1 is performed with the placement of
files on the SD-Card inserted in its card reader.
The files are as follows:

system.cfg
- specifies system level options.  These are option that are not dependent
  of the ini file loaded.

  cfg options
  - Mem  - 0 = disable upper memory; 1 = enable upper memory;
           if not specified upper memory is enabled by default.
  - Wait - 0 = disable bus wait states; 1 = enable bus wait states.
           if not specified wait states are disabled by default.
  - Vhd  - 0 = disable hard disk emulation; 1 = enable hard disk emulation;
           if not specified hard disk emulation is enabled by default.

boot.cfg
- specified the default ini file to load at reset of the Floppy80
  when the floppy 80 boots or is reset it reads the contents of
  the boot.cfg to determine the default configuration ini file.

ini files
- specifies the disk images and options after reset.

  ini options
  - Drive0  - specified the image to load for drive :0
  - Drive1  - specified the image to load for drive :1
  - Drive2  - specified the image to load for drive :2
  - HD0     - specifies the image to load for tyhe first hard drive
  - HD1     - specifies the image to load for tyhe second hard drive
  - Doubler - 1 = doubler is enabled; 0 = doubler is disabled;

dmk files
- these are virtual disk images with a specific file format
  that allows them to be generated and used with a number
  of existing programs and simulators.

virtual hard drive images
- these are virtual hard drive images with a specific file format.
  They are used by the FreHD and TRS80-GP.

########################################################################

FDC utility
- Is a utility to interact with the Floppy80 from within the
  TRS-80 Model I operating environment.  Versions of FDC exist
  for the following operating systems

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

  FDC.COM is used with the two CP/M OSs and
  FDC/CMD is used with the rest.

  Usage

  FDC OPT PARM1:drive

  Where:
  - OPT is one of the following
    - STA - returns the status of the Floppy80.
    - DIR - displays a list of file in the root folder of the SD-Card.
    - INI - switches between tyhe differnt ini file on the SD-Card.
    - DMK - allows the mounting of DMK disk images in the root folder
            of the SD-Card for a specified drive (0, 1 or 2).
    - FOR - copies a DMK disk image from the FOR folder of the SD-Card
            to one of the mounted disk images (0, 1 or 2).
    - IMP - imports a file from the root folder of the SD-Card
            into one of the mounted disk images (0, 1 or 2).

  FDC STA
  - Displays the contents to the ini file specified by boot.cfg

  FDC DIR filter
  - Displays a list of files in the root folder of the SD-Card.
    If filter is specified only files contain the filter character
    sequence are displayed.  If filter is not specified all files
    are displayed.

  FDC INI filename.exe
  - filename.exe is optional.  If not specified and list of ini files
    on the SD-Card will be displayed allow you to select the one to
    write to boot.cfg.  When specified FDC will bypass the file
    selection process.

  FDC DMK filename.exe n
  - filename.exe:n is optional.  When specified it will mount
    the dmk file names by filename.exe into the drive specified
    by n.  For example

    FDC DMK LDOS-DATA.DMK 2

    will mount the dmk file LDOS-DATA.DMK into drive :2

    If filename.exe:n is not specfied a list of dmk files
    will be listed allowing you to select on and the drive
    to mount it into.

  FDC FOR
  - list the files contained in the FMT folder of the SD-Card
    from which you can select one and specify the drive image
    to replace with it.  This is useful to generate blank disk
    images.

  FDC IMP filename.exe:n
  - imports the specified file from the root folder of the
    FAT32 formatted SD-Card to the disk image indicated by n.

########################################################################

Building Source
- Floppy80 firmware
  - Uses VSCode and the Pi Pico extension.

- FDC TRS (utility for the TRSDOS related utility)
  
  - zmac fdc.asm
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
