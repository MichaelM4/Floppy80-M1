# Floppy 80 (M1) Hardware

## Bill of Materials

The components Required are:

| Reference | Qty | Value	             | Footprint                                                   |
|-----------|-----|---------------------|-------------------------------------------------------------|
| C1-C9     | 9   | 100nF               | Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm              |
| D1        | 1   | LED                 | LED_SMD:LED_2816_7142Metric_Pad3.20x4.45mm_HandSolder       |
| D2        | 1   | PMEG4010CEJ         | Diode_SMD:D_SOD-323F                                        |
| J2        | 1   | Conn_02x20_Odd_Even | Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical  |
| J3        | 1   | SD_Card             | Connector_Card:SD_Kyocera_145638009511859+                  |
| Q1-Q2     | 2   | SSM3K324RLFCT-ND    | Package_TO_SOT_SMD:SOT-23_Handsoldering                     |
| R1	     | 1   | 100R                | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm               |
| R2,R6,R8	 | 3   | 1K                  | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm               |
| U1,U3-U5  | 4   | SN74LVCC4245ADWR    | Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm                      |
| U2        | 1   | Pico2               | MCU_RaspberryPi_and_Boards:RPi_Pico_SMD_TH                  |

The 40 pin connection carries data signals, and gold contact pins should be chosen as a preference.

## Assembly Guide

The board is dual sided, and has SMD components care must be taken

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
