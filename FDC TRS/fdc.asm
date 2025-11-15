;
; fdc.asm -- M1 Floppy-80 Utility
;

LLEN	 equ 80		; file buffer line length
NLINES	 equ 10		; number of lines in file/text buffer
PARMSIZE equ 64

; Floppy-80 command codes
GETSTAUS_CMD  equ 1
FINDALL_CMD   equ 2
FINDNEXT_CMD  equ 3
MOUNT_CMD     equ 4
OPENFILE_CMD  equ 5
READFILE_CMD  equ 6
WRITEFILE_CMD equ 7
CLOSEFILE_CMD equ 8
SETTIME_CMD   equ 9
GETTIME_CMD   equ 10
FORMAT_CMD    equ 11

FINDINI_CMD   equ 80h
FINDDMK_CMD   equ 81h
FINDHFE_CMD   equ 82h
FINDFMT_CMD   equ 83h

REQUEST_ADDR  equ 3400h
RESPONSE_ADDR equ 3510h

; to detect equality of a value in A
;	cp	<whatever>
;	jr	c,testFailed   ; A was less than <whatever>.
;	jr	z,testFailed   ; A was exactly equal to <whatever>.

	org	$5200

start:
	ld	a,0
	ld	(RESPONSE_ADDR),a
	ld	(RESPONSE_ADDR+1),a
	ld	(REQUEST_ADDR),a
	ld	(REQUEST_ADDR+1),a
	ld	(hidefsel),a
	ld	(hidedsel),a

	call	getparms

	ld	a,0		; set opcode = 0 just in case
	ld	(opcode),a

	call	clrscr

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
	ld	a,1
	ld	(hidedsel),a
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

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for FOR command line parmameter
gotid5:
	ld	hl,parm1
	ld	de,FORstr
	call	striequ
	jr	nz,gotid6
	ld	a,FINDFMT_CMD	; find .dmk files in FMT folder
	ld	(opcode),a
	jp	getlist

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; test for FOR command line parmameter
gotid6:
	ld	hl,parm1
	ld	de,DIRstr
	call	striequ
	jr	nz,gotid7
	ld	a,FINDALL_CMD	; find .*
	ld	(opcode),a
	ld	a,1
	ld	(hidefsel),a
	ld	(hidedsel),a
	jp	getlist

gotid7:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; display FDC usage (help)
info:	call	clrscr
	ld	hl,intro
	call	print
	jp	exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; request and display the status string of the Floppy-80
getsta:
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
	jz	import1
	ld	hl,imperr1
	call	print
	jmp	exit

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

	; send file specification and open mode (r/w)
	ld	hl,xferbuf
	call	strlen
	inc     b
	call	writedata	; hl - points to the data to be written
				; b  - contains the number of bytes to be written

	; open file command (mode r/w is specified in string)
	ld	a,OPENFILE_CMD
	ld	hl,REQUEST_ADDR
	ld	(hl),a

imploop:
	call	wait_for_ready	; wait until Floppy-80 has completed the request

	ld	hl,RESPONSE_ADDR
	ld	a,(hl)
	or	a
	jr	z,impdone	; if byte count is zero then we are done

	; data is available in memory at address RESPONSE_ADDR
	; (RESPONSE_ADDR) is the number of the byte read
	; (RESPONSE_ADDR+2) is the start of the bytes read

	ld	b,a
	ld	hl,RESPONSE_ADDR+2
	ld	de,fcb
	call	fwrite

	; display . for each block received
	ld	a,'.'
	call	putc

	; request next block of data from file
	ld	a,READFILE_CMD
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	jp	imploop

impdone:
	; close file on controller
	ld	a,CLOSEFILE_CMD
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	; all done, close file and exit
	ld	de,fcb
	call	fclose
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of the File Control Block (FCB) to be initialized
; hl - address of file specification
fspec:
	call	441ch
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of File Control Block (FCB) for the file
; hl - address of buffer to be used when accessing the file
finit:
	ld	b,1
	call	4420h
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; de - address of File Control Block (FCB) of file to close.
fclose:
	call	4428h
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; b  - contains the number of bytes to write
; de - contains the address of the fcb
; hl - contains the address of the bytes to write
fwrite:
	ld	c,(hl)
	call	4439h
	inc	hl
	djnz	fwrite

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

	; if (hidefsel) then don't display the file select character
	ld	a,(hidefsel)
	cp	0
	jr	nz,getlist21

	ld	a,(found)
	add	a,'0'
	call	putc

	ld	a,' '
	call	putc

getlist21:
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

getlistexit:
	jp	exit

getlist30:
	; test if any entries were found
	ld	a,(found)
	or	a
	jr	z,getlistexit

	; if here then we have at least one entry

	; if (hidefsel) then display press any key for next set of files
	ld	a,(hidefsel)
	cp	0
	jr	nz,getlist31

	; else get selection from user
	jp	sel_item
getlist31:

	ld	hl,prompt_next
	call	print
	call	getchar
	jp	getnextset

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; now that we have a list if files, get file selection from user
sel_item:
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
	; test if acc contains a valid selection

	; if a < '1' then go around again
	cp	a,'1'
	jr	c,getnextset

	; if a > (found) then go around again
	ld	a,(found)
	add	a,'0'
	ld	b,a		; b = highest available selection
	ld	a,(select)	; a = user input
	cp	a,b
	jr	c,sel_item10
	jr	z,sel_item10
	jp	getnextset

sel_item10:
	; if (hidedsel) then don't ask user for drive index
	ld	a,(hidedsel)
	cp	0
	jr	nz,sel_item30

	; if here then we need to ask user for drive index
	ld	hl,prompt_drive
	call	print

sel_item20:
	call	getchar
	ld	(drive),a	; save copy of drive

	; validate drive selection
	; if (a < '0') then try again
	cp	a,'0'
	jr	c,sel_item20

	; if (a > '2') then try again
	cp	'2'
	jr	c,sel_item30	; A was less than '2'
	jr	z,sel_item30	; A was exactly equal to '2'
	jp	sel_item20

sel_item30:
	ld	(parm3),a
	ld	a,0
	ld	(parm3+1),a

	call	setparms

	ld	a,(opcode)
	cp	FINDFMT_CMD
	jr	z,do_format

	ld	a,(hidefsel)
	cp	0
	jr	nz,sel_item40

	; if here we assume it is a mount file request
	call	mountfile
	jp	getsta

do_format:
	call	format

sel_item40:
	jp	exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; parm2 - points to command line option 2 (drive number)
format:
	push	a
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

	ld	a,FORMAT_CMD	; format command
	ld	hl,REQUEST_ADDR
	ld	(hl),a

	call	wait_for_ready

	; display status response
	ld	hl,RESPONSE_ADDR+2
	call	print

	pop	de
	pop	hl
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; initializes parm1, parm2 and parm3 based on user selection to be
; passed to mountfile.
;
; (select) - memory location contains the index of the desired selection.
;
; (drive)  - memory location contains the index of the drive selection ('0'-'3')
;
setparms:
	push	a
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
	pop	a
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
	push	a
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
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; return HL = DE * A
Mul8:	push	b
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
	pop	b
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; increments HL until the memory location pointed to by HL is not
; a space (32)
;
; HL - points to the string
;
skipblanks:
	push	a
skipblanks1:
	ld	a,(hl)
	cp	32		; space
	jr	nz,skipblanks2
	inc	hl
	jr	skipblanks1

skipblanks2:
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; increments HL until the memory location pointed to by HL is a
; space (32) or a null (0)
;
; HL - points to the string
;
skiptoblank:
	push	a
skiptoblank1:
	ld	a,(hl)

	cp	32		; space
	jr	z,skiptoblank2
	
	cp	0
	jr	z,skiptoblank2

	inc	hl
	jr	skiptoblank1

skiptoblank2:
	pop	a
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
strcpy:	push	a
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
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Parameters:
;	HL - source
;	DE - destination
;
strcat:	push	a
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
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; counts the number of characters in the null terminated string
; pointed to by hl. the maximum string length is 255.
;
; HL - address of the string
;
; return: b - contains the length of the string
;
strlen:	push	a
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
	pop	a
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
	ld	de,parm1
	call	copyparm
	cp	a,13		; if CR then no more paramters
	jr	z,getparms1

	ld	de,parm2
	call	copyparm
	cp	a,13		; if CR then no more paramters
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
	push	a

	; null terminate (space to null or CR to null)
	dec	de
	ld	a,0
	ld	(de),a

	pop	a
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

	push	a
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
	pop	a
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
	push	a
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
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; just waste some time
delay:	push	b
	ld	b,0
dly:	djnz	dly
	pop	b
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; just waste some time
delay2:	push	b
	ld	b,20
dly2:	call	delay
	djnz	dly2
	pop	b
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; wait for the Floppy80-M1 request to complete (mem(REQUEST_ADDR) == 0)
; times out if mem(REQUEST_ADDR) != 0 after 256 times through loop
wait_for_ready:
	push	a
	push	b
	push    hl

	ld	b,0

wnb1:	ld	a,(REQUEST_ADDR)
	cp	0
	jr	z,wnb3

wnb2:	call	delay2		; give Floppy-80 time to do its thing
	djnz	wnb1		; time out after 256 loops

wnb3:	pop	hl
	pop	b
	pop	a
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
clrscr:	push	hl
	ld	hl,cscr
	call	print
	pop	hl
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; A - character to display.
putc:	push	de
	call	$33
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
	push	de
getchar3:
	ld	de,4015H
	call	13H
	or	a
	jz	getchar3

	pop	de
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
exit:
	ld	hl,0
	call	402dh
	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
intro:
		ascii	'Model I FDC utility version 0.2.0',13
		ascii	'Command line options:',13
		ascii	'STA - get status (firmware version, mounted disks, etc.).',13
;		ascii	'SET - set FDC date and time to the TRS-80 date and time.',13
;		ascii	'GET - set TRS-80 date and time to the FDC date and time.',13
		ascii	'DIR - get a directory listing of the FDC SD-Card root folder.',13
		ascii	'INI - select the default ini file.    FDC INI filename.ext',13
		ascii	'DMK - mount a DMK disk image.         FDC DMK filename.ext n',13
		ascii	'FOR - format DMK disk image.',13
;		ascii	'HFE - mount a HFE disk image.         FDC HFE filename.ext n',13
		ascii   'IMP - import a file from the SD-Card. FDC IMP filename.ext:n',13
;		ascii	'EXP - export a file to the SD-card.   FDC EXP filename/ext:n',13
		ascii	' ',13
		ascii	'      filename.ext - is the filename and extension.',13
		ascii	'      n - is the drive number (0-2).',13,0

error1:		ascii	'Error: drive index not specified on command line',13,13,0
imperr1:	ascii	'Error: invalid file specification',13
		ascii	'Usage: FDC IMP filename.ext:n',13
		ascii	'Where:',13
		ascii   ' - filename.ext is the file name and extension of',13
		ascii   '   the file to be imported from the SD-Card.',13
		ascii   ' - n is the logical drive to save file on',13,13,0
imperr2:	ascii	'Error: unable to open the specified file',13,13,0

cscr:		ascii   ' ', 28, 31, 15, 0
space:		ascii	' ', 0
openr:		ascii	', r', 0
openw:		ascii	', w', 0

STAstr:		ascii	'STA',0
INIstr:		ascii	'INI',0
DMKstr:		ascii	'DMK',0
HFEstr:		ascii	'HFE',0
DIRstr:		ascii	'DIR',0
IMPstr:		ascii	'IMP',0
EXPstr:		ascii	'EXP',0
FORstr:		ascii	'FOR',0

prompt_part1:	ascii	'Press 1-',0
prompt_part2:	ascii	' to select the desired file.',13
		ascii	'Press any other key for next set of files.',13,0
prompt_drive:	ascii	'Specify drive to mount to (0-2).',13,0
prompt_reset:	ascii	'Power OFF and back ON to continue.',13,0

prompt_next:	ascii	'Press any key for next set of files.',13,0

opcode:		defs	1		; command line operation requested (0=STA; 1=INI; 2=MNT;)
hidefsel:	defs	1		; if not zero then hide file select index and prompt
hidedsel:	defs	1		; if not zero hide drive select to mount prompt
found:		defs	1
select:		defs	1
drive:		defs	1

fcb:		defs	48		; 48 for Model III TRSDOS 1.3   
fcbbuf:		defs	256

xferbuf:	defs	260		; transfer buffer
lnbuf:		defs	(NLINES*LLEN)	; text buffer of NLINES lines of LLEN characters each

parm1:		defs	PARMSIZE
parm2:		defs	PARMSIZE
parm3:		defs	PARMSIZE

	end	start
 