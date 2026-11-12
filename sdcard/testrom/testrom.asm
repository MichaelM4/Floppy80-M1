;
; testrom
;
	org	$3000
start:
	call	01C9H		;cls
	ld	hl,msg		;start of message
lp0:	ld	a,(hl)
	or	a
	rz			;return if zero
	call	0033H		;send each char
	inc	hl
	jr	lp0		;keep going until end of txt 0
done:	ret
;
msg:		ascii	'Hello World!',13,0
	end	start
