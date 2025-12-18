# Floppy 80 (M1) Hardware

## Bill of Materials

A bill of materials [FDC.csv](/KiCAD/FDC.csv) is provided in the KiCAD folder

Notes:
* The 40 pin connector carries data signals, gold contacts should be chosen as a preference.

## Assembly Guide

The board has SMD components on both sides, care must be taken.

The order of assembly, while not definitive, following is advice
* U1, U3, U4, U5 - Logic IC's on the rear of the board
* SD Card Socket on the front of the board
* Small SMD components on the rear of the board
* The PI Pico on the rear of the board
* Any small SMD components on the front of the board
* Lastly solder through hole socket on the front of the board

Some Notes
* D2 is probably not required
* R, and R8, may also not be required

## Programming Firmware

The Pi must be programmed with the firmware. to do this:
* Hold down the reset button in PI, while inserting it into a USB port.
* A new USB drive will be mounted.
* Copy the firmware file to the new mounted drive
* The Drive will disconnect after the upload, this means programming is complete.

## Testing

See The [User Guide](USER-GUIDE.md#configuration) for creating an SD card
