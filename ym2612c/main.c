/////////////////////////////////////////////////////////////////////////////
/* ym2612c.c
 *
 * Created: 4/21/2024 1:46:49 PM
 * Author : birdlabs
 * 
 * this program first initializes the YM2612 by setting the control pins
 * to appropriate values, and writing an initial test patch, the code for which
 * came from here: https://github.com/Ryan-Marchildon/ym2612-arduino-test
 * it's worth noting that i originally translated that test code to assembly,
 * and then for the C version i translated back my own assembly code.
 * i admit it does look very similar!
 *
 * following the initialization, the program just waits for an SPI transmission
 * and writes the data to the appropriate register in the YM2612 after each 3rd
 * byte.  the first signifies which channels will be written to, the second
 * is the register, and the 3rd is the data to write
 */
///////////////////////////////////////////////////////////////////////////// 

#ifndef F_CPU
#define F_CPU 16000000UL // define system clock
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/power.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// ym control pins
#define CS PC0
#define WR PC1
#define RD PC2
#define A0 PC3
#define A1 PC4
#define IC PB0
#define CLK PB1

//FOR BREADBOARD VERSION
/*
#define IC PC5
#define CS PC4
#define WR PC3
#define RD PC2
#define A0 PC1
#define A1 PC0
*/

// SPI pins
#define SS PB2
#define MOSI PB3
#define MISO PB4
#define SCK PB5

// DDR
#define YM_DATA_DDR DDRD
#define YM_CTRL_DDR DDRC
#define SPI_DDR DDRB

// ports
#define YM_IC_PORT PORTB
#define YM_CTRL_PORT PORTC
#define YM_DATA_PORT PORTD

struct Global {
	uint8_t sreg;
	
	uint8_t YM_reg;
	uint8_t YM_data;
	uint8_t writeFlag;
	
	uint8_t inCnt;
};
	
struct Global glb;

void setreg(uint8_t reg, uint8_t data, uint8_t chan);
void ymWrite(uint8_t data);
void setreg123(uint8_t reg, uint8_t data);
void setreg456(uint8_t reg, uint8_t data);

ISR(SPI_STC_vect);

int main(void){		
	// DDR setup
	
	SPI_DDR |= (1<<CLK) | (1<<MISO) | (1<<IC); // clock, MISO, IC out
	SPI_DDR &= ~((1<<SS) | (1<<MOSI) | (1<<SCK)); // SS, MOSI, SCK in
	
	YM_CTRL_DDR |= (1<<A1) | (1<<A0) | (1<<RD) | (1<<WR) | (1<<CS);// | (1<<IC); for breadboard version (original design)
	
	YM_DATA_DDR = 0xFF; // port D output
	
	// initialize SPI
	SPCR = (1<<SPIE) | (1<<SPE);
	
	// initialize timer1
	TCCR1A = (1<<COM1A0);
	TCCR1B = (1<<CS10) | (1<<WGM12);
	TCCR1C = 0;
	OCR1A = 0;
	
	TCNT1 = 0;
	
	// initialize global vars
	glb.inCnt = 0;
		
	// initialize YM ctrl pins
	YM_IC_PORT |= (1<<IC);
	YM_CTRL_PORT |= (1<<RD) | (1<<WR) | (1<<CS) | (1<<IC) | (0<<A0) | (0<<A1);
	
	// strobe IC to reset YM
	YM_IC_PORT &= ~(1<<IC);
	_delay_ms(10);
	YM_IC_PORT |= (1<<IC);
	_delay_ms(10);
	
	// init register setup
	setreg123(0x22, 0x00); // LFO off
	setreg123(0x27, 0x00); // Note off (channel 0)
	setreg123(0x28, 0x01); // Note off (channel 1)
	setreg123(0x28, 0x02); // Note off (channel 2)
	setreg123(0x28, 0x04); // Note off (channel 3)
	setreg123(0x28, 0x05); // Note off (channel 4)
	setreg123(0x28, 0x06); // Note off (channel 5)
	setreg123(0x2B, 0x00); // DAC off
	setreg123(0x30, 0x71); //
	setreg123(0x34, 0x0D); //
	setreg123(0x38, 0x33); //
	setreg123(0x3C, 0x01); // DT1/MUL
	setreg123(0x40, 0x23); //
	setreg123(0x44, 0x2D); //
	setreg123(0x48, 0x26); //
	setreg123(0x4C, 0x00); // Total level
	setreg123(0x50, 0x5F); //
	setreg123(0x54, 0x99); //
	setreg123(0x58, 0x5F); //
	setreg123(0x5C, 0x94); // RS/AR
	setreg123(0x60, 0x05); //
	setreg123(0x64, 0x05); //
	setreg123(0x68, 0x05); //
	setreg123(0x6C, 0x07); // AM/D1R
	setreg123(0x70, 0x02); //
	setreg123(0x74, 0x02); //
	setreg123(0x78, 0x02); //
	setreg123(0x7C, 0x02); // D2R
	setreg123(0x80, 0x11); //
	setreg123(0x84, 0x11); //
	setreg123(0x88, 0x11); //
	setreg123(0x8C, 0xA6); // D1L/RR
	setreg123(0x90, 0x00); //
	setreg123(0x94, 0x00); //
	setreg123(0x98, 0x00); //
	setreg123(0x9C, 0x00); // SSGEG
	setreg123(0xB0, 0x32); // Feedback/algorithm
	setreg123(0xB4, 0xC0); // Both speakers on
	setreg123(0x28, 0x00); // Key off
	setreg123(0xA4, 0x22); //
	setreg123(0xA0, 0x69); // Set frequency
	
	sei();
	  
    while (1){
		// program is handled by SPI interrupt
    }
}

// set registers in YM2612
void setreg(uint8_t reg, uint8_t data, uint8_t chan){
	cli();
	
	switch(chan){
		case 0: // just set channels 1-3
		setreg123(reg, data);
		break;
		case 1: // just channels 4-6
		setreg456(reg, data);
		break;
		default: // set all 6
		setreg123(reg, data);
		setreg456(reg, data);
		break;
	}
	
	sei();
}

// to write to chan 1-3: A0,A1 = 0,0 for reg select; 1,0 for data write
void setreg123(uint8_t reg, uint8_t data){
	YM_CTRL_PORT &= ~(1<<A0); // channels 1-3
	ymWrite(reg);
	YM_CTRL_PORT |= (1<<A0);
	ymWrite(data);
	YM_CTRL_PORT &= ~(1<<A0);
}

// to write to chan 4-6: A0,A1 = 0,1 for reg select, 1,1 for data write
void setreg456(uint8_t reg, uint8_t data){
	YM_CTRL_PORT |= (1<<A1); // channels 4-6
	ymWrite(reg);
	YM_CTRL_PORT |= (1<<A0);
	ymWrite(data);
	YM_CTRL_PORT &= ~((1<<A0) | (1<<A1));
}

void ymWrite(uint8_t data){
	YM_CTRL_PORT &= ~(1<<CS);
	YM_DATA_PORT = data;
	_delay_us(1);	
	YM_CTRL_PORT &= ~(1<<WR);
	//YM_CTRL_PORT |= (1<<RD); // ???
	_delay_us(5);
	YM_CTRL_PORT |= (1<<WR);
	//YM_CTRL_PORT &= ~(1<<RD); // ????
	_delay_us(5);
	YM_CTRL_PORT |= (1<<CS);
	
	// TODO: maybe play with setting RD
}

// receive data over SPI
ISR(SPI_STC_vect){
	glb.sreg = SREG;
	cli();
	
	switch(glb.inCnt){
		case 0: // first value written determines which channels will be written to
			glb.writeFlag = SPDR;
			break;
		case 1: // next value is the register
			glb.YM_reg = SPDR;
			break;
		case 2: // last is the data to write
			glb.YM_data = SPDR;
			setreg(glb.YM_reg,glb.YM_data,glb.writeFlag); // write everything, writeFlag is which channels will be written to
			break;
	}
	
	++glb.inCnt; // next value sent over SPI has a different purpose
	
	if(glb.inCnt > 2) glb.inCnt = 0; // every 3rd value sent is the end of a complete transmission
	
	SPDR = 0; //glb.inCnt + (glb.writeFlag<<4); for testing
	SREG = glb.sreg;
};