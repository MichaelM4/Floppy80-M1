;
; fdc.asm -- Floppy-80 Utility
;

LLEN:         equ 80		; file buffer line length
NLINES:       equ 10		; number of lines in file/text buffer
PARMSIZE:     equ 64
STACKSIZE:    equ 256
CMDLINE_SIZE: equ 80

; Floppy-80 command codes
GETSTAUS_CMD:  equ 1
FINDALL_CMD:   equ 2
FINDNEXT_CMD:  equ 3
MOUNT_CMD:     equ 4
OPENFILE_CMD:  equ 5
READFILE_CMD:  equ 6
WRITEFILE_CMD: equ 7
CLOSEFILE_CMD: equ 8
SETTIME_CMD:   equ 9
GETTIME_CMD:   equ 10

FINDINI_CMD:   equ 80h
FINDDMK_CMD:   equ 81h
FINDHFE_CMD:   equ 82h

REQUEST_ADDR:  equ 3400h
RESPONSE_ADDR: equ 3510h

BDOS_CMDLINE1:  equ 0xE607	; Lifeboat CP/M 1.14
BDOS_CMDLINE2:  equ 0xE807	; FMG CP/M 1.5

; to detect equality of a value in A
;	cp	<whatever>
;	jr	c,testFailed   ; A was less than <whatever>.
;	jr	z,testFailed   ; A was exactly equal to <whatever>.

BDOS:	equ	0x4205
CONS:	equ	1
TYPEF:	equ	2
PRINTF:	equ	9	;BUFFER PRINT ENTRY

	org	0x4300
start:
	ld	hl,0
	add	hl,sp
	ld	sp,mystack
	push	hl

	; copy BDOS cmd line to program cmd line buffer
	call	get_cmdline

	; parse cmd line
	ld	hl,cmdline
	call	getparms	; HP - points to the command line

	ld	a,0		; set opcode = 0 just in case
	ld	(opcode),a

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for STA command line parmameter
	ld	hl,parm1
	ld	de,STAstr
	call	striequ
	jr	nz,gotid1
	jp	getsta

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for INI command line parmameter
gotid1:
	ld	hl,parm1
	ld	de,INIstr
	call	striequ
	jr	nz,gotid2
	ld	a,FINDINI_CMD	; find .ini files
	ld	(opcode),a
	jp	getlist

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for DMK command line parmameter
gotid2:
	ld	hl,parm1
	ld	de,DMKstr
	call	striequ
	jr	nz,gotid3
	ld	a,FINDDMK_CMD	; find .dmk files
	ld	(opcode),a
	jp	getlist

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for HFE command line parmameter
gotid3:
	ld	hl,parm1
	ld	de,HFEstr
	call	striequ
	jr	nz,gotid4
	ld	a,FINDHFE_CMD	; find .hfe files
	ld	(opcode),a
	jp	getlist

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for IMP command line parmameter
gotid4:
	ld	hl,parm1
	ld	de,IMPstr
	call	striequ
	jr	nz,gotid5
	jp	import

gotid5:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; display FDC usage (help)
info:	call	clrscr
	ld	hl,intro
	call	print
	jp	exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of buffer to copy the command line string too
get_cmdline:
	; look in command line buffer 1 for 'FDC'.
	; if found use cmd line 1; else use cmd line 2;
	ld	hl,BDOS_CMDLINE1
	call	strFDC
	cp	0
	jp	z,get_cmdline0
	jp	get_cmdline1

get_cmdline0:
	ld	hl,BDOS_CMDLINE2

get_cmdline1:
	ld	de,cmdline
	ld	b,(hl)

get_cmdline2:
	inc	hl
	ld	a,(hl)
	ld	(de),a
	inc	de
	djnz	get_cmdline2

	ld	a,0
	ld	(de),a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; request and display the status string of the Floppy-80
getsta:
	call	clrscr

	; set response length to 0
	ld	a,0
	ld	hl,RESPONSE_ADDR
	ld	(hl),a
	ld	hl,RESPONSE_ADDR+1
	ld	(hl),a

	; issue status request
	ld	a,GETSTAUS_CMD
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	call	wait_for_ready

	; display status response
	ld	hl,RESPONSE_ADDR+2
	call	print

	ld	a,(opcode)	; if mounting an ini file then hang until reset pressed
	cp	FINDINI_CMD
	jr	nz,getsta_exit

	ld	hl,prompt_reset
	call	print
getsta_loop:
	jp	getsta_loop

getsta_exit:
	jp	exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; parm2 - points to command line option 2 (the file name)
import:
	; replace . with / in the file specification
	ld	hl,parm2
	ld	b,'.'
	ld	c,'/'
	call	strchr_replace

	; initialze FCB
	ld	de,fcb
	ld	hl,parm2
	call	fspec

	; check for an error
	jp	z,import1
	ld	hl,imperr1
	call	print
	jp	exit

import1:
	; open the file
	ld	hl,fcbbuf
	ld	de,fcb
	call	finit

	; check for an error
	jr	z,import2
	ld	hl,imperr2
	call	print
	ret

import2:
	; if here then the file should be ready to receive data

	; build and send open file request to Floppy-80
	ld	de,xferbuf
	ld	hl,parm2
	call	strcpy		; HL - source; DE - destination;

	ld	hl,openr	; append ', r'
	ld	de,xferbuf
	call	strcat		; HL - source; DE - destination;

	; for debuging display string to be sent to Floppy-80
	ld	hl,xferbuf
	call	print
	ld	a,13
	call	putc

	; drive select for host
	ld	a,0FH
	out	(0F4H),a

	; send file specification and open mode (r/w)
	ld	hl,xferbuf
	call	strlen
	inc     b
	call	writedata	; hl - points to the data to be written
				; b  - contains the number of bytes to be written

	; open file command (mode r/w is specified in string)
	ld	a,OPENFILE_CMD
	out	(0F0H),a

	call	wait_for_ready	; wait until Floppy-80 has completed the request

imploop:
	; read data from Floppy-80
	ld	a,0FH		; host drive select
	out	(0F4H),a
	ld	a,READFILE_CMD	; read file data
	out	(0F0H),a
	
	ld	hl,xferbuf
	call	readdata	; contains the address of the buffer to receive the bytes
				; since up to 255 bytes can be read the buffer must be at
				; least 255 bytes
        			;
		        	; returns: a - contains the number of bytes read
	or	a
	jr	z,impdone

	; if here we have data in xferbuf to be written (byte count in a)

	ld	b,a
	ld	hl,xferbuf
	ld	de,fcb
	call	fwrite

	; display . for each block received
	ld	a,'.'
	call	putc

	jp	imploop

impdone:
	; all done, close file and exit
	ld	de,fcb
	call	fclose
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of the File Control Block (FCB) to be initialized
; lh - address of file specification
fspec:
;	ld	a,(model)
;	or	a
;	jr	z,fspec3	; 0 => model 3
;	dec	a
;	jr	z,fspec4	; 1 => model 4
;	dec	a
;	jp	nz,$40
;
;fspec3:
;	call	441ch
;	ret
;
;fspec4:
;	; for model 4
;	ld	a,@fspec
;	rst	$28
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of File Control Block (FCB) for the file
; hl - address of buffer to be used when accessing the file
finit:
;	ld	a,(model)
;	or	a
;	jr	z,finit3	; 0 => model 3
;	dec	a
;	jr	z,finit4	; 1 => model 4
;	dec	a
;	jp	nz,$40
;
;finit3:
;	ld	b,1
;	call	4420h
;	ret
;
;finit4:
;	; for model 4
;	ld	a,@init
;	ld	b,1		; 1-byte record size
;	rst	$28
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of File Control Block (FCB) of file to close.
fclose:
;	ld	a,(model)
;	or	a
;	jr	z,fclose3	; 0 => model 3
;	dec	a
;	jr	z,fclose4	; 1 => model 4
;	dec	a
;	jp	nz,$40
;
;fclose3:
;	call	4428h
;	ret
;
;fclose4:
;	; for model 4
;	ld	a,@close
;	rst	$28
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; b  - contains the number of bytes to write
; de - contains the address of the fcb
; hl - contains the address of the bytes to write
fwrite:
;	ld	a,(model)
;	or	a
;	jr	z,fwrite3	; 0 => model 3
;	dec	a
;	jr	z,fwrite4	; 1 => model 4
;	dec	a
;	jp	nz,$40
;
;fwrite3:
;	ld	c,(hl)
;	call	4439h
;	inc	hl
;	djnz	fwrite3
;
;	ret
;
;fwrite4:
;	; for model 4
;	ld	a,@put
;	ld	c,(hl)
;	rst	$28
;	inc	hl
;	djnz	fwrite4

	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - holds the next entry in lnbuf to copy the file name
; c  - counts the number of files found
;
; (opcode) - specifies the code for findfirst
; 		0x02 - all files
;               0x80 - .INI files
;               0x81 - .DMK files
;               0x82 - .HFE files
;
getlist:
	call	clrscr

	; check if parm2 (the file was specified on the cmd line)
	ld	a,(parm2)
	cp	0
	jr	z,getlist10

	; if ((opcode) == 0x80) then set parm3 = '0' and call mountfile
	ld	a,(opcode)
	cp	FINDINI_CMD
	jr	nz,getlist1

	; parm3[0] = '0'
	; parm3[1] = 0
	ld	a,'0'
	ld	(parm3),a
	ld	a,0
	ld	(parm3+1),a

	call	mountfile
	jp	getsta

getlist1:
	; make sure drive index was specified
	ld	a,(parm3)
	cp	0
	jr	nz,oktomount	; drive index was not specified
	ld	hl,error1
	call	print
	jp	info

oktomount:
	call	mountfile
	jp	getsta

getlist10:
	ld	de,lnbuf
	ld	a,0		; init file counter (found = 0)
	ld	(found),a
	ld	a,(opcode)
	ld	b,a
	call	findfirst

getlist20:
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for empty string
	ld	hl,xferbuf
	ld	a,(hl)
	cp	0
	jr	z,getlist30

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; then we found another one (found += 1)
	ld	a,(found)
	inc	a
	ld	(found),a

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; skip file date and file size
	ld	hl,xferbuf
	call	skipblanks
	call	skiptoblank
	call	skipblanks
	call	skiptoblank
	call	skipblanks

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; at this point hl contains the address of the first
	; character of the file name

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; save de on the stack
	push	de

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; copy file name to lnbuf
	push	hl
	call	strcpy		; HL - source; DE - destination;
	pop	hl

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; display file name
	push	bc

	ld	a,(found)
	add	a,'0'
	call	putc

	ld	a,' '
	call	putc

	; display file name
	call	print

	ld	a,13
	call	putc

	pop	bc
	
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; restore de and add LLEN
	pop	de

	; de += LLEN
	ld	a,e
	add	a,LLEN
	ld	e,a
	ld	a,d
	adc	a,0
	ld	d,a

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test if we already have 9 entries

	; if (c == 8) then show prompt
	ld	a,(found)
	cp	8
	jr	z,getlist30	; then a == 8

getnext:
	call	findnext
	jp	getlist20

getnextset:
	call	clrscr
	ld	de,lnbuf
	ld	a,0		; init file counter (found = 0)
	ld	(found),a
	jp	getnext

getlist30:
	; test if any entries were found
	ld	a,(found)
	or	a
	jr	z,getlist40

	; if here then we have at least one entry

	; initialize drive selection to '0'
	ld	a,'0'
	ld	(drive),a

	; prompt user for selection
	ld	hl,prompt_part1
	call	print

	ld	a,(found)
	add	a,'0'
	call	putc

	ld	hl,prompt_part2
	call	print

	call	getchar
	ld	(select),a	; save copy of selection

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test if a valid selection

	; if a < '1' then go around again
	cp	'1'
	jr	c,getnextset

	; if a > (found) then go around again
	ld	a,(found)
	add	a,'0'
	ld	b,a		; b = highest available selection
	ld	a,(select)	; a = user input
	cp	b
	jr	c,mount
	jr	z,mount
	jp	getnextset

	; else try to mount selected image
mount:
	; if (opcode == FINDINI_CMD) then don't ask user for drive index
	ld	a,(opcode)
	cp	FINDINI_CMD
	jr	z,mount2

	; if here then we need to ask user for drive index
	ld	hl,prompt_drive
	call	print

mount1:	call	getchar
	ld	(drive),a	; save copy of drive

	; validate drive selection
	; if (a < '0') then try again
	cp	'0'
	jr	c,mount1

	; if (a > '2') then try again
	cp	'2'
	jr	c,mount2	; A was less than '2'
	jr	z,mount2	; A was exactly equal to '2'
	jp	mount1

mount2:	ld	(parm3),a
	ld	a,0
	ld	(parm3+1),a

	call	setparms
	call	mountfile
	jp	getsta

getlist40:
	jp	exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; initializes parm1, parm2 and parm3 based on user selection to be
; passed to mountfile.
;
; (select) - memory location contains the index of the desired selection.
;
; (drive)  - memory location contains the index of the drive selection ('0'-'3')
;
setparms:
	push	af
	push	hl
	push	de

	; parm3[0] = (drive);
	; parm3[1] = 0;
	ld	a,(drive)
	ld	(parm3),a
	ld	a,0
	ld	(parm3+1),a

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; calculate file buffer pointer (hl = ((select) - '1') * LLEN + lnbuf)
	ld	a,(select)
	sub	'1'
	ld	de,LLEN
	call	Mul8		; HL = DE * A

	ld	de,lnbuf
	add	hl,de

	ld	de,parm2
	call	strcpy

	pop	de
	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; build the mount command string from the contents of parm1 and parm2
;
; then sends the mount command code and command text to the
; Floppy-80 controller.
;
; parm3 - ASCII character indicating the desired drive ('0'-'2')
;
; parm2 - contains the filename.ext
;
mountfile:
	push	af
	push	hl
	push	de

	; build command string in xferbuf
	; '0 filename.ext' (replace 0 with desired drive number)
	ld	hl,parm3
	ld	de,xferbuf
	call	strcpy

	; add space
	ld	hl,space
	ld	de,xferbuf
	call	strcat

	; add parm2
	ld	hl,parm2
	ld	de,xferbuf
	call	strcat

	ld	hl,xferbuf
	call	strlen
	call	writedata	; hl - points to the data to be written
				; b  - contains the number of bytes to be written

	ld	a,MOUNT_CMD	; mount command
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	call	wait_for_ready

	pop	de
	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; return HL = DE * A
Mul8:	push	bc
	ld	hl,0		; HL is used to accumulate the result
	ld	b,8		; the multiplier (A) is 8 bits wide
Mul8Loop:
	rrca			; putting the next bit into the carry
	jp	nc,Mul8Skip	; if zero, we skip the addition (jp is used for speed)
	add	hl,de		; adding to the product if necessary
Mul8Skip:
	sla	e		; calculating the next auxiliary product by shifting
	rl	d		; DE one bit leftwards (refer to the shift instructions!)
	djnz	Mul8Loop
	pop	bc
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; increments HL until the memory location pointed to by HL is not
; a space (32)
;
; HL - points to the string
;
skipblanks:
	push	af
skipblanks1:
	ld	a,(hl)
	cp	32		; space
	jr	nz,skipblanks2
	inc	hl
	jr	skipblanks1

skipblanks2:
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; increments HL until the memory location pointed to by HL is a
; space (32) or a null (0)
;
; HL - points to the string
;
skiptoblank:
	push	af
skiptoblank1:
	ld	a,(hl)

	cp	32		; space
	jr	z,skiptoblank2
	
	cp	0
	jr	z,skiptoblank2

	inc	hl
	jr	skiptoblank1

skiptoblank2:
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; searches a string for the occurance of 'FDC'
;
; Parameters:
;	HL - pointer to string to search
;
; Returns:
;	a = 0 if string not found
;         = 1 if string was found
;
strFDC:
	push	hl
	ld	b,8	; maximum number of characters to scan

	; copy string until null is detected
strFDC1:
	inc	hl
	ld	a,(hl)
	call	toupper
	cp	'F'
	jr	z,strFDC2
	djnz	strFDC1
	ld	a,0
	jp	strFDC_Exit

strFDC2:
	inc	hl
	ld	a,(hl)
	call	toupper
	cp	'D'
	jr	z,strFDC3
	ld	a,0
	jp	strFDC_Exit

strFDC3:
	inc	hl
	ld	a,(hl)
	call	toupper
	cp	'C'
	jr	z,strFDC4
	ld	a,0
	jp	strFDC_Exit

strFDC4:
	ld	a,1

strFDC_Exit:
	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; copies the string start at location indicated by hl to the location
; indicated by de. copy stops when the null termination of the hl
; string is detected. the de string is terminated with a null.
;
; Parameters:
;	HL - source
;	DE - destination
;
; Returns:
;	HL - last location of source accessed (points to the null)
;	DE - last location of destingation accessed (points to the null)
;
strcpy:	push	af
	push	hl
	push	de

	; copy string until null is detected
strcpy1:
	ld	a,(hl)
	inc	hl
	ld	(de),a
	inc	de
	cp	0
	jr	nz,strcpy1

	pop	de
	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Parameters:
;	HL - source
;	DE - destination
;
strcat:	push	af
	push	hl
	push	de

	; locate null byte of destination string
strcat1:
	ld	a,(de)
	cp	0
	jr	z,strcat2
	inc	de
	jp	strcat1

	; copy string until null is detected
strcat2:
	ld	a,(hl)
	inc	hl
	ld	(de),a
	inc	de
	cp	0
	jr	nz,strcat2

	pop	de
	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; counts the number of characters in the null terminated string
; pointed to by hl. the maximum string length is 255.
;
; HL - address of the string
;
; return: b - contains the length of the string
;
strlen:	push	af
	push	hl
	ld	b,255

	; locate null terminator
strlen1:
	inc	b
	ld	a,(hl)
	inc	hl
	cp	0
	jr	nz,strlen1

	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; compares the two strings pointed to by HL and DE are the same.
; comparison is case independent ('a' is considered the same as 'A').
; sets z flag if thy are the same.
striequ:
	; Load next chars of each string
	ld	a, (de)
	call	toupper
	ld	b, a

	ld	a, (hl)
	call	toupper

	; Compare
	cp	b

	; Return non-zero if chars don't match
	ret	nz

	; Check for end of both strings
	cp	0

	; Return if strings have ended
	ret	z

	; Otherwise, advance to next chars
	inc	hl
	inc	de
	jr	striequ

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; hl - points to the string to scan for
; b  - contains the character to be replaced
; c  - contains the character to replace with
strchr_replace:
	ld	a,(hl)
	cp	b
	jr	nz,replace1	; not the same as b

	; if here (hl) is the same as b, then replace with c
	ld	a,c
	ld	(hl),a

replace1:

	; test for null terminator
	ld	a,(hl)
	cp	0

	; Return if strings have ended
	ret	z

	; repeat
	inc	hl
	jp	strchr_replace

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; copys the command line options to the parm1, parm2 and parm3 arrays
;
; HL - points to the command line
;
getparms:
	push	hl

	; initialize parm1 and parm2 arrays to zeros
	ld	hl,parm1
	ld	a,0
	ld	b,PARMSIZE
	call	memset		; HL - destination
				; a  - value to fill memory with
				; b  - the number of bytes in memory to set
	ld	hl,parm2
	ld	a,0
	ld	b,PARMSIZE
	call	memset		; HL - destination
				; a  - value to fill memory with
				; b  - the number of bytes in memory to set
	ld	hl,parm3
	ld	a,0
	ld	b,PARMSIZE
	call	memset		; HL - destination
				; a  - value to fill memory with
				; b  - the number of bytes in memory to set

	pop	hl

	; (hl) points to the command line options
	ld	de,parm0
	call	copyparm
	cp	0		; if CR then no more paramters
	jr	z,getparms1

	ld	de,parm1
	call	copyparm
	cp	0		; if CR then no more paramters
	jr	z,getparms1

	ld	de,parm2
	call	copyparm
	cp	0		; if CR then no more paramters
	jr	z,getparms1

	ld	de,parm3
	call	copyparm

getparms1:
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; sets the specified bytes of memory to the specified value
;
; HL - destination
; a  - value to fill memory with
; b  - the number of bytes in memory to set
;
memset:
	push	hl
memset1:
	ld	(hl),a
	inc	hl
	djnz	memset1

	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; copy string until null, space or CR (13) is detected
;
; parameters:
;	HL - source
;	DE - destination
;
; returns:
;	a - contains the parameter termination charcter
;
copyparm:
	push	de
copyparm1:
	ld	a,(hl)
	inc	hl
	ld	(de),a
	inc	de
	cp	0
	jr	z,copyparm2
	cp	13
	jr	z,copyparm2
	cp	' '
	jr	z,copyparm2
	jr	copyparm1

copyparm2:
	push	af

	; null terminate (space to null or CR to null)
	dec	de
	ld	a,0
	ld	(de),a

	pop	af
	pop	de
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; converts the character in a to uppercase. if the character is not
; between 'a' and 'z' inclusive it is left as is.
;
toupper:
	; if (a < 'a') then return a
	cp	'a'
	jr	c,toupper2	; then a < 'a'

	; if (a > 'z') then return a
	cp	'z'
	jr	c,toupper1	; then a < 'z'
	jp	toupper2

toupper1:
	and	0DFH		; make uppercase (by clearing bit 5)

toupper2:
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; on entry: b = 0x02 - all files
;               0x80 - .INI files
;               0x81 - .DMK files
;               0x82 - .HFE files
;
; returns:
;   a = contains the number of bytes read into xferbuf
;   a == 0 indicates no file found
;   a != 0 indicates the file information in xferbuf
;
findfirst:
	push	hl

	; set response length to 0
	ld	a,0
	ld	hl,RESPONSE_ADDR
	ld	(hl),a
	ld	hl,RESPONSE_ADDR+1
	ld	(hl),a

	; find first command
	ld	a,b
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	; ReadData(psz, nMaxLen);
	ld	hl,xferbuf
	call	readdata

	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; on exit a is the number of bytes read
;   a == 0 indicates no file found
;   a != 0 indicates the file information in xferbuf
;
findnext:
	push	hl

	; set response length to 0
	ld	a,0
	ld	hl,RESPONSE_ADDR
	ld	(hl),a
	ld	hl,RESPONSE_ADDR+1
	ld	(hl),a

	; find next command
	ld	a,FINDNEXT_CMD
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	; ReadData(psz, nMaxLen);
	ld	hl,xferbuf
	call	readdata

	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; read a block of bytes from Floppy-80
;
; parameters:
;	hl - contains the address of the buffer to receive the bytes
;            since up to 255 bytes can be read the buffer must be at
;            least 255 bytes
;
; returns:
;	a - contains the number of bytes read
;
readdata:
	push	hl
	push	de

	ld	a,0
	ld	(hl),a

	call	wait_for_ready

	ld	de,RESPONSE_ADDR
	ld	a,(de)

	push	af
	ld	b,a

	; test for zero length data/string
	cp	0
	jr	z,readdata2

	ld	b,a         ; b will hold the loop counter

	ld	de,RESPONSE_ADDR+2

readdata1:
	ld	a,(de)
	inc	de
	ld	(hl),a
	inc	hl
	djnz	readdata1

	; null terminate str
	ld	a,0
	ld	(hl),a

readdata2:
	pop	af
	pop	de
	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; sends bytes to the Floppy-80
;
; parameters:
;	hl - points to the data to be written
;	b  - contains the number of bytes to be written
;
writedata:
	push	af
	push	hl
	push	de

	ld  de,REQUEST_ADDR+2

writedata1:
	ld	a,(hl)
	ld	(de),a
	inc	de
	inc	hl
	djnz	writedata1

	pop	de
	pop	hl
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; just waste some time
delay:	push	bc
	ld	b,0
dly:	djnz	dly
	pop	bc
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; wait for the Floppy80-M1 request to complete (mem(REQUEST_ADDR) == 0)
; times out if mem(REQUEST_ADDR) != 0 after 256 times through loop
wait_for_ready:
	push	af
	push	bc
	push    hl

	ld	b,0

wnb1:	ld	hl,REQUEST_ADDR
	ld	a,(hl)
	jr	nz,wnb3

wnb2:	call	delay		; give Floppy-80 time to do its thing
	djnz	wnb1		; time out after 256 loops

wnb3:	pop	hl
	pop	bc
	pop	af
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
clrscr:	push	hl
	ld	hl,cscr
	call	print
	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; A - character to display.
putc:
	push	de
	push	hl
	push	bc

	ld	c,TYPEF
	ld	e,a
	call	BDOS

	pop	bc
	pop	hl
	pop	de
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; HL - points to the null terminated string to display.
print:	push	hl
print1:	ld	a,(hl)
	inc	hl
	or	a
	jr	z,print_end

	call	putc

	jr	print1
print_end:
	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; get a character from the system.
;
; returns:
;	a - contains the character associated with the user key press.
;
getchar:
	ld	c,CONS
	call	BDOS
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
exit:
	pop	hl
	ld	sp,hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
intro:		db	'Model I FDC utility version 0.1.0',13,10
		db	'Command line options:',13,10
		db	'STA - get status (firmware version, mounted disks, etc.).',13,10
;		ascii	'SET - set FDC date and time to the TRS-80 date and time.',13,10
;		ascii	'GET - set TRS-80 date and time to the FDC date and time.',13,10
;		ascii	'DIR - get a directory listing of the FDC SD-Card root folder.',13,10
		db	'INI - select the default ini file.    FDC INI filename.ext',13,10
		db	'DMK - mount a DMK disk image.         FDC DMK filename.ext n',13,10
;		db	'HFE - mount a HFE disk image.         FDC HFE filename.ext n',13,10
;		db      'IMP - import a file from the SD-Card. FDC IMP filename/ext:n',13,10
;		ascii	'EXP - export a file to the SD-card.   FDC EXP filename/ext:n',13,10
		db	' ',13,10
		db	'      filename.ext - is the filename and extension.',13,10
		db	'      n - is the drive number (0-2).',13,10,0

error1:		db	'Error: drive index not specified on command line',13,13,0
imperr1:	db	'Error: invalid file specification',13,13,0
imperr2:	db	'Error: unable to open the specified file',13,13,0

cscr:		db   ' ', 28, 31, 15, 0
space:		db	' ', 0
openr:		db	', r', 0
openw:		db	', w', 0

STAstr:		db	'STA',0
INIstr:		db	'INI',0
DMKstr:		db	'DMK',0
HFEstr:		db	'HFE',0
DIRstr:		db	'DIR',0
IMPstr:		db	'IMP',0
EXPstr:		db	'EXP',0

prompt_part1:	db	'Press 1-',0
prompt_part2:	db	' to select the desired file.',13
		db	'Press any other key for next set of files.',13,0
prompt_drive:	db	'Specify drive to mount to (0-2).',13,0
prompt_reset:	db	'Power OFF and back ON to continue.',13,0

opcode:		ds	1		; command line operation requested (0=STA; 1=INI; 2=MNT;)
found:		ds	1
select:		ds	1
drive:		ds	1

fcb:		ds	48		; 48 for Model III TRSDOS 1.3   
fcbbuf:		ds	256

xferbuf:	ds	260		; transfer buffer
lnbuf:		ds	NLINES*LLEN	; text buffer of NLINES lines of LLEN characters each

cmdline:	ds	CMDLINE_SIZE
parm0:		ds	PARMSIZE
parm1:		ds	PARMSIZE
parm2:		ds	PARMSIZE
parm3:		ds	PARMSIZE

		ds	STACKSIZE
mystack:

	end
 