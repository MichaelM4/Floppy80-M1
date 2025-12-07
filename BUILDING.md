
# Floppy 80 (M1) Software Build

## Floppy80 firmware

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

### Background Reading

These are DEV resources:
* https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html
* https://admantium.medium.com/getting-started-with-raspberry-pico-and-cmake-f536e18512e6

## FDC (for TRSDOS)

FDC TRS (utility for the TRSDOS related utility)

```
cd FDC_TRS`
zmac fdc.asm
```

use TRS80GP to import FDC/CMD onto a disk image

```
trs80gp -m1 -vs -frehd -frehd_dir zout -turbo -mem 48 -d0 dmk\ld531-0.dmk -d1 dmk\ld531-1.dmk -i "IMPORT2 FDC.CMD FDC/CMD:1\r"
```

## FDC (for CP/M)

FDC CP/M (utility for the CP/M related utility)

Compile program into a hex file.

```
cd FDC_CPM`
zmac fdc.asm
```

Run TRS80GP loading the CP/M disk images.

```
trs80gp -m1 -vs -frehd -frehd_dir zout -turbo -mem 48 -d0 CPM141-0.dmk -d1 CPM141-1.dmk -d2 CPM141-2.dmk
```

Select Load from TRS80GP File menu and select the hex file.

Run the SAVE command to save program memory contents to a .COM file.
As the program grows you will need to increase the value after SAVE.
i.e. `SAVE n ufn` Where :
* n   - number of 256-byte pages to be saved.
* ufn - unambiguous file name.

e.g.

`SAVE 20 B:FDC.COM`
