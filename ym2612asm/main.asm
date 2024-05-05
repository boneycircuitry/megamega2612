;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; ym2612.asm
;
; ECE:3360 term project
; Author : Izaak Thompson (Boney Circuitry)
;
; this didn't end up being used, but it was supposed to receive
; data over SPI and load it into the Y register, then write that
; to the YM2612.  i couldn't figure out how to set up an interrupt
; in assembly and the whole SPI thing just didn't pan out so i scrapped it
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.include "m328Pdef.inc"

.cseg 

; ???????????
;.org SPIaddr ; jump to SPI_SCT_ISR when SPI STC (Serial Transfer Complete) interrupt occurs
;	jmp SPI_STC_ISR

.org 0x0033 ; jump over IVT

; register definitions
.def _sreg=r14
.def tmp=r16		; for assignments
.def ctr=r17		; counter
.def ym_ld=r18
.def spi_reg=r19
.def lp1=r23		; counter for display loop
.def lp2=r24		; "
.def lp3=r25		; "

; YM definitions

; pins
.equ IC=5
.equ CS=4
.equ WR=3
.equ RD=2
.equ A0=1
.equ A1=0
.equ CLK=1
; DDR
.equ clk_spi_DDR=DDRB
.equ ctrlDDR=DDRC
.equ dataDDR=DDRD
; PORT
.equ clkPORT=PORTB
.equ ctrlPORT=PORTC
.equ dataPORT=PORTD

; SPI definitions
.equ SS=2
.equ MOSI=3
.equ MISO=4
.equ SCK=5

; 100u: 1,1,200
; 250u: 1,5,100
; 5m: 10,100,100
; 100m: 3,255,255
; ~1s: 50,255,255
.MACRO delay
	ldi lp1,@0		; outer loop
d1: ldi lp2,@1		; middle loop
d2: ldi lp3,@2		; inner loop
d3: dec lp3;
	nop
	brne d3
	dec lp2
	brne d2
	dec lp1
	brne d1
.ENDMACRO

 ; write a register (i.e. YH or YL)
.MACRO writeR
	cbi ctrlPORT,CS	; clear CS
	out dataPORT,@0 ; write data
	delay 1,1,3
	cbi ctrlPORT,WR
	delay 1,1,10
	sbi ctrlPORT,WR ; strobe wr low-high

;	cbi ctrlPORT,RD ; strobe RD low-high
;	sbi ctrlPORT,WR ; strobe WR high-low
;	delay 1,1,10
;	cbi ctrlPORT,WR 
;	sbi ctrlPORT,RD ; can play with placement of RD pulse or not depending on sound

	delay 1,1,10
	sbi ctrlPORT,CS ; set cs
.ENDMACRO

; set YM2612 register to ATmega register (YH or YL)
.MACRO setregR
	cbi ctrlPORT,A0 ; reg select 1-3
	writeR @0	; reg number
	sbi ctrlPORT,A0 ; data write 1-3
	writeR @1	; data

	sbi ctrlPORT,A1 ; reg select ch 4-6
	writeR @0	; reg number
	sbi ctrlPORT,A0 ; data write 4-6
	sbi ctrlPORT,A1 ; (both high to write data to 4-6)
	writeR @1
	; clear both
	cbi ctrlPORT,A0
	cbi ctrlPORT,A1
.ENDMACRO

; for writing IMMEDIATE (i.e. a number)
.MACRO writeI
	push ym_ld
	cbi ctrlPORT,CS	; clear CS
	ldi ym_ld,@0
	out dataPORT,ym_ld ; write data
	delay 1,1,3

;	cbi ctrlPORT,RD
;	cbi ctrlPORT,WR
;	delay 1,1,10
;	sbi ctrlPORT,RD ; strobe rd low-high
;	sbi ctrlPORT,WR ; strobe wr high-low
;	delay 1,1,10

	cbi ctrlPORT,WR ; strobe wr low-high
	delay 1,1,10
	sbi ctrlPORT,WR
	delay 1,1,10

	sbi ctrlPORT,CS ; set CS
	pop ym_ld
.ENDMACRO

; set reg to immediate
.MACRO setregI
	cbi ctrlPORT,A0 ; reg select 1-3
	writeI @0	; reg number
	sbi ctrlPORT,A0 ; data write 1-3
	writeI @1	; data
	sbi ctrlPORT,A1 ; reg select ch 4-6
	writeI @0	; reg number
	sbi ctrlPORT,A0 ; data write 4-6
	sbi ctrlPORT,A1 ; (both high to write data to 4-6)
	writeI @1
	; clear both
	cbi ctrlPORT,A0
	cbi ctrlPORT,A1
.ENDMACRO

.MACRO spiTransfer
	lds @0,SPDR
	sts SPDR,@0
.ENDMACRO

setup:
	; DDR assignment
	ldi tmp,0x3F	; PC0-PC5 output
	out ctrlDDR,tmp
	ldi tmp,0xFF	; PD0-PD8 output
	out dataDDR,tmp
	ldi tmp,0x12	; PB1 clock, PB2-5 SPI (MISO output; SS, SCK, MOSI input)
	out clk_spi_DDR,tmp
	delay 20,100,100 ; necessary?

	; PORTC assignment
	ldi tmp,0x3C	; a0, a1 low; rd, wr, cs, ic high
	out ctrlPORT,tmp

	; initialize SPI
	ldi tmp,0	; clear port initially
	sts SPCR,tmp
	ldi tmp,(1<<SPE) ; SPE high; SPIE, DORD, MSTR, CPOL, CPHA, SPR1, SPR0 low
	sts SPCR,tmp

	; clock assignment
	ldi tmp,(1<<COM1A0)
	sts TCCR1A,tmp	; set COM1A0 & WGM12 (toggle OC1A on compare match) 
	ldi tmp,(1<<CS10)|(1<<WGM12)
	sts TCCR1B,tmp	; no prescaling
	ldi tmp,0		; reset count etc
	sts TCCR1C,tmp
	sts TCNT1H,tmp
	sts TCNT1L,tmp

	; strobe IC pin before getting started to initialize internal registers
	cbi ctrlPORT,ic
	delay 30,100,100
	sbi ctrlPORT,ic
	delay 30,100,100

soundsetup:
	setregI 0x22, 0x00; // LFO off
	setregI 0x27, 0x00; // ch 3 mode/timer
	setregI 0x28, 0x00; // Note off (channel 0)
	setregI 0x28, 0x01; // Note off (channel 1)
	setregI 0x28, 0x02; // Note off (channel 2)
	setregI 0x28, 0x04; // Note off (channel 3)
	setregI 0x28, 0x05; // Note off (channel 4)
	setregI 0x28, 0x06; // Note off (channel 5)
	setregI 0x2B, 0x00; // DAC off
	setregI 0x30, 0x71; //
	setregI 0x34, 0x0D; //
	setregI 0x38, 0x33; //
	setregI 0x3C, 0x08; // DT1/MUL ch 1
	setregI 0x31, 0x71; //
	setregI 0x35, 0x0D; //
	setregI 0x39, 0x33; //
	setregI 0x3D, 0x08; // DT1/MUL ch 2
	setregI 0x40, 0x23; //
	setregI 0x44, 0x2D; //
	setregI 0x48, 0x26; //
	setregI 0x4C, 0x00; // Total level ch 1
	setregI 0x41, 0x23; //
	setregI 0x45, 0x2D; //
	setregI 0x49, 0x26; //
	setregI 0x4D, 0x00; // Total level ch 2
	setregI 0x50, 0x5F; //
	setregI 0x54, 0x99; //
	setregI 0x58, 0x5F; //
	setregI 0x5D, 0x94; // RS/AR ch1
	setregI 0x51, 0x5F; //
	setregI 0x55, 0x99; //
	setregI 0x59, 0x5F; //
	setregI 0x5C, 0x94; // RS/AR ch2
	setregI 0x60, 0x05; //
	setregI 0x64, 0x05; //
	setregI 0x68, 0x05; //
	setregI 0x6C, 0x07; // AM/D1R ch1
	setregI 0x61, 0x05; //
	setregI 0x65, 0x05; //
	setregI 0x69, 0x05; //
	setregI 0x6D, 0x07; // AM/D1R ch 2
	setregI 0x70, 0x02; //
	setregI 0x74, 0x02; //
	setregI 0x78, 0x02; //
	setregI 0x7C, 0x02; // D2R ch 1
	setregI 0x71, 0x02; //
	setregI 0x75, 0x02; //
	setregI 0x79, 0x02; //
	setregI 0x7D, 0x02; // D2R ch 2
	setregI 0x80, 0x11; //
	setregI 0x84, 0x11; //
	setregI 0x88, 0x11; //
	setregI 0x8C, 0xA6; // D1L/RR ch1
	setregI 0x81, 0x11; //
	setregI 0x85, 0x11; //
	setregI 0x89, 0x11; //
	setregI 0x8D, 0xA6; // D1L/RR ch2
	setregI 0x90, 0x00; //
	setregI 0x94, 0x00; //
	setregI 0x98, 0x00; //
	setregI 0x9C, 0x00; // SSGEG ch1
	setregI 0x91, 0x00; //
	setregI 0x95, 0x00; //
	setregI 0x99, 0x00; //
	setregI 0x9D, 0x00; // SSGEG ch1
	setregI 0xB0, 0x45; // Feedback/algorithm ch1
	setregI 0xB1, 0x45; // Feedback/algorithm ch2
	setregI 0xB4, 0xC0; // Both speakers on
	setregI 0x28, 0x00; // Key off
	setregI 0xA4, 0x12; // 
	setregI 0xA0, 0x69; // Set frequency

loop:
	;sei
	setregI 0xB0,0x45
;	lds spi_reg,SPSR
;	sbrc spi_reg,SPIF
;	rcall SPI_STC
	
	;
	setregI 0x28,0xF0
	delay 70,255,255
;	setregI 0xB1, 0x72
;	setregI 0xA5, 0x1F; // 
;	setregI 0xA1, 0x00; // Set frequency
	setregI 0x28,0x00
	delay 70,255,255
;	setregI 0x28,0x00
;	delay 70,255,255
;	setregI 0x28,0x01
;	setregI 0x28,0x02 ; FOR TEST
;	delay 30,255,255
	rjmp loop

;.global SPI_STC_vect

; called when SPIF flag is set in SPSR, signifying data transmission complete
SPI_STC:
	setregI 0xB0, 0x00
	setregI 0xA4, 0x20; // 
	setregI 0xA0, 0x40; // Set frequency

	cpi YH,0		; see if YH is already loaded
	brne i0			; if so, go to i0
	spiTransfer YH	; if not, load it
	rjmp i1			; and then leave
i0: spiTransfer YL	; if YH is already loaded, load YL
	setregR YH,YL	; then write the data from Y register to the YM2612
	ldi YH,0		; clear Y register
	ldi YL,0
i1:	ret				; back to business

; couldn't get this to work
SPI_STC_vect:
	push _sreg
	lds	_sreg,SREG	; save SREG status
	cli				; stop interrupts
	cpi YH,0		; see if YH is already loaded
	brne v0			; if so, go to i0
	spiTransfer YH	; if not, load it
	rjmp v1			; and then leave
v0: spiTransfer YL	; if YH is already loaded, load YL
	setregR YH,YL	; then write the data from Y register to the YM2612
	ldi YH,0		; clear Y register
	ldi YL,0
v1:	sts SREG,_sreg	; turn interrupts on, restore SREG
	pop _sreg		; restore _sreg register
	reti			; back to business

.exit