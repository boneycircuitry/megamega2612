/////////////////////////////////////////////////////////////////////////////
/*
 * megamega1.c
 * ECE:3360 spring 2024 term project
 * Created: 4/10/2024 4:00:26 PM
 * Author : Izaak Thompson (Boney Circuitry)
 *
 * this program is used in tandem with ym2612c.c to control the YM2612 for the
 * megamega2612 project
 *
 * the program uses SPI, pin change interrupts, USART RX interrupts, and timer1
 * overflow interrupts to change parameters and turn notes on in a YM2612 IC,
 * which is controlled by the second ATmega328p, which receives the data over SPI
 * from the main controller
 * 
 * the physical interface consists of a rotary encoder with an attached push button,
 * a second push button, an HD44780 16x2 LCD screen, a MIDI jack which is connected
 * to the ATmega328p's USART RX pin through a 6N138 optocoupler, a second
 * ATmega328p, connected via SPI, and of course the YM2612, which is not directly
 * connected to the ATmega for which this program is written
 *
 * parameters are arranged into 4 groups:
 *   group 1: preset patches and (in the future) extra options such as routing
 * of MIDI input (modulation, aftertouch, etc), drone mode, random mode, etc.
 *   group 2: algorithm, feedback, frequency multiple*, detune*, total level*
 *   group 3: envelope parameters*
 *   group 4: LFO parameters: frequency, vibrato, amplitude modulation sensitivity,
 * AM on/off*
 *
 * *per operator parameters
 *
 * when input is received through the encoder and buttons (via PCINT on D1-D4),
 * the screen is updated and data is sent to the YM2612 over SPI
 * 
 * parameters are accessed and modified as follows, handled in the PCINT2 ISR:
 *  - to cycle through groups, press and RELEASE encoder (LEFT) button (move back) or standalone (RIGHT) button (move forward)
 *  - to cycle through parameters within groups, HOLD AND TURN the LEFT button
 *  - to change OPERATOR (4 available), HOLD the RIGHT button and TURN the encoder
 *  - to change the value of the currently selected parameter, just TURN the encoder left (decrement) or right (increment)
 * 
 * MIDI data handled by the program consists either of note on/off data or modulation data
 * (mod wheel, aftertouch, pitch bend, sustain).  routing of MIDI data is handled by the USART_RX ISR.
 * for note on/off MIDI data, the note() function is called, which does the following:
 *  - note on: find a channel that is not "on" or "scheduled" to be turned on, translate the incoming note
 * to a frequency and octave to be sent to the YM2612, "schedule" the selected channel to be turned on, and
 * assign the note on velocity to an array.  the velocity assigned can not be lower than minVel, which is defined by the user.
 *  - note off: find a channel that matches the note that is supposed to be turned off; schedule it to be turned off.
 * if MIDI sustain data comes in while notes are on, notes will be scheduled to be turned off, but won't be until sustain is released
 *
 * the TIMER1_OVF ISR is enabled so that when timer1 overflows, channels that are "scheduled" to be
 * turned off or on are made to be so, and their respective flags are changed to reflect that fact.
 * this function also handles the velocity by assigning a weighted average between incoming velocity
 * and the 'total level' of each operator as defined by the preset patch.  the weight is determined
 * by the global variable velSens
 *
 * possible future developments are listed within the main while loop in lieu of any polling or other such business
 */  
/////////////////////////////////////////////////////////////////////////////

#ifndef F_CPU
#define F_CPU 16000000UL // define system clock
#endif

// universal MIDI baud rate - used by UBRR0
#define MIDI_BAUD 31250UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/power.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "defines.h" // library for LCD
#include "lcd.h"

FILE lcd_str = FDEV_SETUP_STREAM(lcd_putchar, NULL, _FDEV_SETUP_WRITE); // for LCD

// these are defined in the LCD 'defines.h' file
// it might be redundant to define them here but i do it anyway for the sake of clarity
#define LCD_RS PB1
#define LCD_E  PB2
#define LCD_D4 PC0
#define LCD_D5 PC1
#define LCD_D6 PC2
#define LCD_D7 PC3

// interface pins
#define MIDI_IN PD0
#define BTN_L	PD1
#define BTN_R	PD2
#define ENC_A	PD3
#define ENC_B	PD4

// for future use - an indicator that a note is being pressed
#define LED PB0 

// SPI pins
#define MOSI PB3
#define MISO PB4
#define SCK	 PB5
#define SS   PC4 // SS output is PC4 for convenience
	
#define MAX_PRESET 20
	
////////////// STRINGS USED BY THE LCD //////////////

const char* patchNames[] = {
	"ding dong piano",
	"toxic sludge",
	"wooden steel",
	"steel drum pad",
	"(un)naturhythm",
	"reedy ripper",
	"lately who?",
	"tuned bounce",
	"morph metal",
	"get(s) nasty",
	"flarp wobble",
	"pan flute",
	"deceptive bass",
	"jagged EP",
	"all consuming",
	"one operator",
	"squelchy",
	"ugly bell",
	"moving electric",
	"wurly slow dance",
	"ambient banjo"
};

const char* params[4][7] = {
	{
		"preset patch","velocity sens","min velocity","polyphony"
	},{
		"algorithm","feedback","freq mult","detune","level"
	},{
		"attack","decay","sust level","sust rate","release","rate scale","SSGEG"
	},{
		"LFO frequency","vibrato","AM sensitivity","AM"
	}
};

const char* algorithms[] = {
	"1 > 2 > 3 > 4~", "1 & 2 > 3 > 4~",
	"(2 > 3) & 1 > 4~", "(1 > 2) & 3 > 4~",
	"1 > 2~, 3 > 4~", "1 > (2 & 3 & 4)~",
	"1 > 3~, 2~, 4~", "1~, 2~, 3~, 4~"
};

const char* egTypes[] = {
	"OFF","forward loop", "one shot + low",
	"forward+rev loop", "one shot + high",
	"reverse loop", "reverse + high",
	"rev+forward loop", "reverse + low"
};

const char* lfoFreqs[] = {
	"OFF", "3.82 Hz", "5.33 Hz", "5.77 Hz", "6.11 Hz",
	"6.60 Hz", "9.23 Hz", "46.11 Hz", "69.22 Hz"
};

const char* onOff[] = { "OFF", "ON" };
	
const char* playModes[] = { "polyphonic","mono retrig","mono legato" };
	
/////////////////////////////////////////////////////

// idiosyncrasies of writing to the YM2612's registers
// see https://www.plutiedev.com/ym2612-registers for more info
const uint8_t chan[] = {0, 1, 2, 4, 5, 6};
const uint8_t opOffset[] = {0, 0x08, 0x04, 0x0C};
	
// these will be used by random mode
#define NUM_REGS 98

// ALL registers used for programming YM2612
const uint8_t regs[] = {
  	0x22,				0xA0, 0xA4,	0xB0, 0xB4,	0x30, 0x38, 0x34, 0x3C,
	0x28,				0xA1, 0xA5,	0xB1, 0xB5,	0x31, 0x39, 0x35, 0x3D,
					0xA2, 0xA6,	0xB2, 0xB6,	0x32, 0x3A, 0x36, 0x3E,
	// ^ global regs       		   ^ per channel regs ^          rest are per operator
	0x40, 0x48, 0x44, 0x4C,		0x50, 0x58, 0x54, 0x5C,		0x60, 0x68, 0x64, 0x6C,
	0x41, 0x49, 0x45, 0x4D,		0x51, 0x59, 0x55, 0x5D,		0x61, 0x69, 0x65, 0x6D,
	0x42, 0x4A, 0x46, 0x4E,		0x52, 0x5A, 0x56, 0x5E,		0x62, 0x6A, 0x66, 0x6E,

	0x70, 0x78, 0x74, 0x7C,		0x80, 0x88, 0x84, 0x8C,		0x90, 0x98, 0x94, 0x9C,		
	0x71, 0x79, 0x75, 0x7D,		0x81, 0x89, 0x85, 0x8D,		0x91, 0x99, 0x95, 0x9D,		
	0x72, 0x7A, 0x76, 0x7E,		0x82, 0x8A, 0x86, 0x8E,		0x92, 0x9A, 0x96, 0x9E,		
};
	
// global variables
struct GlobalVars {
	// interface - stored values of previous states for comparison
	volatile uint8_t RPGold[2];
	volatile uint8_t RPGpinOld;
	volatile uint8_t BTN_L_old;
	volatile uint8_t BTN_R_old;
	
	volatile bool chgGrp;
	
	// midi
	volatile int midiBuf[128];
	int midiIndex;
	volatile uint8_t msgStart;
	volatile uint8_t midiStatus;
	
	// other
	volatile uint8_t sreg;
};

struct GlobalVars glb;

struct Parameters {
	int op; // current operator value
	int group; // current group value
	int current; // current parameter selected within current group
	int* value; // value of currently selected parameter
	
	// pitch etc (updated from USART_RX ISR)
	volatile uint8_t freq[6][2]; // freqs to be loaded into high (A4) and low (A0) registers for all 6 channels
	volatile uint8_t notesOn[6][3]; // note value for each channel, whether it *should* be on, and whether it *is* on
	volatile int timeOn[6]; // count how long each note has been held, turn off note that has been held the longest when reaching 6 notes
	volatile int vel[6];
	bool sustain;
	
	// group 0
	int patchNum;
	char* patchName;
	int velSens;
	int minVel;
	int polyphony;
	
	// for later
	int* modWheel;
	int* aftertouch;
	int modIndex;
	int atIndex;
	
	// group 1
	int algorithm;
	int feedback;
	int detune[4];
	int multiple[4];
	int totalLvl[4];
	
	// group 2
	int attack[4];
	int decay[4];
	int susLvl[4];
	int susRate[4];
	int release[4];
	int rateScl[4];
	int ssgeg[4];
	
	// group 3
	int lfoFreq;
	int vibrato;
	int tremolo;
	int amOn[4];
};	

struct Parameters ym;

unsigned char SPIsend(unsigned char data); // send SPI data

void sendreg(uint8_t flag, uint8_t reg, uint8_t data); // send YM2612 data over SPI

// update YM2612 parameter registers for all 6 channels
void writeToYM(uint8_t op, uint8_t val1, uint8_t val2, uint8_t baseReg, uint8_t bitShift1, uint8_t bitShift2,
bool multiOp, bool multiChannel, uint8_t options, uint8_t subValue1, uint8_t subValue2);

//void printToLCD(char param[17],int options); // update LCD
void printToLCD(int options); // update LCD

void minMaxValue(int* var, int min, int max); // restrict passed parameter within min/max values

// update all YM parameters at once
void changeAllParams(int alg, int fb, int lfo, int vib, int trem,
int mult0, int  mult1, int mult2, int mult3, int det0, int det1, int det2, int det3,
int tl0, int tl1, int tl2, int tl3, int atk0, int atk1, int atk2, int atk3,
int dcy0, int dcy1, int dcy2, int dcy3, int sl0, int sl1, int sl2, int sl3,
int sr0, int sr1, int sr2, int sr3, int rel0, int rel1, int rel2, int rel3,
int rs0, int rs1, int rs2, int rs3, int eg0, int eg1, int eg2, int eg3,
int am0, int am1, int am2, int am3);

void preset(); // load presets

void changeGroup(); // select between 4 groups

void changeCurrent(); // select parameter within groups

void changeValue(); // update value of parameter on LCD and in YM2612

int max(int val1, int val2);

void note(uint8_t noteIn, uint8_t velocity, bool on); // schedule notes to be turned off/on

ISR(USART_RX_vect); // midi data received

ISR(TIMER1_OVF_vect); // turn on/off scheduled notes

ISR(PCINT2_vect); // pin change ISR for interface (encoder/buttons - D1-D4)

int main(void) {	
	
	stdout = &lcd_str; // printf prints to LCD

	lcd_init(); // initialize lcd
	
	// DDRs for SPI (B & C), LCD (B & C), and interface (D)
	// note that PB2 (LCD_E) is the uC's SS pin - since it's set as output it shouldn't affect the SPI's functioning
	DDRB |= (1<< LCD_RS) | (1<<LCD_E) | (1<<MOSI) | (1<<SCK) | (0<<MISO);
	
	// SS is PC4
	DDRC |= (1<<LCD_D4) | (1<<LCD_D5) | (1<<LCD_D6) | (1<<LCD_D7) | (1<<SS);
	
	// interface + MIDI pins input
	DDRD &= ~((1<<MIDI_IN) | (1<<BTN_L) | (1<<BTN_R) | (1<<ENC_A) | (1<<ENC_B)); 
	
	// initialize SPI for sending messages to mega2 to be transmitted to YM2612
	SPCR = (1<<SPE) | (1<<MSTR) | (1<<SPR1); // SPI enable, master mode, divide clock by 64
	
	// initialize USART for MIDI
	UCSR0B = (1 << RXCIE0) | (1 << RXEN0) | (0 << UCSZ02); // enable RX interrupt, enable RX
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8 bits per character
	UBRR0 = (F_CPU / 16 / MIDI_BAUD) - 1; // midi baud rate 31250
	
	// enable pin change interrupts for D1 - D4 (PCINT17 - 20)
	PCMSK2 = (1<<PCINT17) | (1<<PCINT18) | (1<<PCINT19) | (1<<PCINT20);
	PCICR = (1<<PCIE2); // enable pin change interrupts for PORTD
	
	PORTC |= (1<<SS); // SS pin high by default, low when transmitting data
	
	// initialize timer1
	TCCR1A = 0; // no PWM, no compare, just counting up to 0xFFFF in "normal" mode
	TCCR1B = (1<<CS10); // on, no prescale
	TIMSK1 |= (1<<TOIE1); // enable timer1 overflow interrupt
	
	TCNT1 = 0;
	
	// initialize stored interface pin values
	glb.RPGpinOld = PIND & ((1<<ENC_A) | (1<<ENC_B));
	glb.RPGold[0] = (glb.RPGpinOld & (1<<ENC_A))>>ENC_A;
	glb.RPGold[1] = (glb.RPGpinOld & (1<<ENC_B))>>ENC_B;
	glb.BTN_L_old = (PIND & (1<<BTN_L))>>BTN_L;
	glb.BTN_R_old = (PIND & (1<<BTN_R))>>BTN_R;
	
	// initialize ym values
	ym.op = 0;
	ym.group = 0;
	ym.current = 0;
	ym.patchNum = 15; // "one operator" patch
	ym.value = &ym.patchNum;
	
	ym.sustain = false;
	
	ym.velSens = 2;
	ym.minVel = 50;
	
	// initialize MIDI buffer situation
	glb.midiIndex = 0;
	glb.msgStart = 0;
	
	// i'm cool
	printf("Boney Circuitry\n  megamega2612");
	_delay_ms(1000);
	
	// show preset patch on startup
	preset();
	changeGroup();
	
	sei(); // lets goooooooooo
	
    while (1) 
    {		
		/*
		TODO:
			- set up way to change parameter selection for mod wheel & AT
			- midi channel
			- random sounds
			- turn off oldest note when playing new note (past 6 channels)
			- dont play notes lower than lowest oct/higher than highest
			- velocity!!
				- CHANGE MIN VELOCITY
			- monophonic mode (legato/retrig)
				- part of patch
			- MIDI in LED
			- pitch bend?
			- poly AT?
		*/
	}
}

////////////////////////////// FUNCTIONS ///////////////////////////////////

// transmit data over the SPI lines
unsigned char SPIsend(unsigned char data){
	uint8_t readData;
	
	glb.sreg = SREG;
	cli();
	PORTC &= ~(1<<SS); // clear SS to signify sending data
	_delay_us(3);
	SPDR = data; // send byte
	while(!(SPSR & (1<<SPIF))); // wait for SPI data transmission complete flag SPIF in SPSR
	readData = SPDR;
	PORTC |= (1<<SS); // set SS
	glb.sreg = SREG;
	return readData; // only used for testing
}

// send first a flag signifying which set of 3 channels (1-3 (0), 4-6 (1), or all 6 (default)) will be written to in the slave ATmega
// followed by the register to be written to, followed by the data to be written to that register
void sendreg(uint8_t flag, uint8_t reg, uint8_t data){
	unsigned char rData;
	
	rData = SPIsend(flag);
	_delay_us(10);
	rData = SPIsend(reg);
	_delay_us(10);
	rData = SPIsend(data);
	_delay_us(10);
}

// send parameter data to slave ATmega to be written to YM2612
// all 6 channels will be written to at once, but only the currently selected operator
void writeToYM(uint8_t op, uint8_t val1, uint8_t val2, uint8_t baseReg, uint8_t bitShift1, uint8_t bitShift2,
bool multiOp, bool multiChannel, uint8_t options, uint8_t subValue1, uint8_t subValue2){
	
	uint8_t regToWrite;
	uint8_t dataToWrite;
	
	// add offset (0x00, 0x08, 0x04, 0x0C for op 0, 1, 2, 3) to base register, to write to the register associated with a specific operator
	if(multiOp){
		regToWrite = baseReg + opOffset[op];
	} else {
		regToWrite = baseReg;
	}
	
	// most registers are written to on a channel-by-channel basis
	// so first send flag 0 to write to channel 1, 2, 3 (0xX0, 0xX1, 0xX2, 0xX3 + op offset),
	// then send flag 1 to write to channels 4, 5, 6 using the same values
	if(multiChannel){
		for(int j = 0; j < 2; j++){
			for(int i = 0; i < 3; i++){ // write same value to all 6 channels
				switch(options){
					case 0: // shared, neither register is reversed
						dataToWrite = (val1 << bitShift1) + (val2 << bitShift2);
						break;
					case 1: // val 1 is reversed, val 2 is not
						dataToWrite = ((subValue1 - val1) << bitShift1) + (val2 << bitShift2);
						break;
					case 2: // val 1 is not reverse, val 2 is
						dataToWrite = (val1 << bitShift1) + ((subValue2 - val2) << bitShift2);
						break;
					case 3: // both vals reversed
						dataToWrite = ((subValue1 - val1) << bitShift1) + ((subValue2 - val2) << bitShift2);
						break;
					case 4: // not shared, not reverse (SSGEG if 0, frequency A0)
						dataToWrite = val1;
						break;
					case 5: // for detune & oct, which have a a min value of -3 (val1)
						dataToWrite = ((val1 + 3) << bitShift1) + (val2 << bitShift2);
						break;
					case 6: // for detune & oct, which have a a min value of -3 (val2)
						dataToWrite = (val1 << bitShift1) + ((val2 + 3) << bitShift2);
						break;
					case 7: // only sustain rate, which is both not shared AND reversed
						dataToWrite = subValue1 - val1;
						break;
					case 8: // only for reg B4+ which has pan (always both L and R true)
						dataToWrite = 0xC0 + (val1 << bitShift1) + (val2 << bitShift2);
						break;
					case 9: // not shared, but reverse (total lvl)
						dataToWrite = subValue1 - val1;
						break;
					default:
						dataToWrite = val1;
						break;
				}
				
				sendreg(j, regToWrite + i, dataToWrite); // write
			}
		}
		
	} else { // only LFO register 0x22 is global (among the registers being written to in this function anyway)
		sendreg(0, baseReg, val1 + (val2 << bitShift2));
	}
}

// format and print text to LCD based on currently selected parameter
void printToLCD(int options){
	int val = *ym.value;
	
	uint8_t op = ym.op;
	const char* param = params[ym.group][ym.current];
	
	home(); // line 1, spot 1
	printf("                \n                "); // clear screen
	home();

	switch(options){
		case 0: // not operator, no strings
			printf("%s:\n%d",param,val);
			break;
		case 1: // regular operator params
			printf("op %d %s:\n%d",op+1,param,val);
			break;
		case 2:  // just mult (0 = 0.5, everything else is normal)
			if(val == 0){
				printf("op %d %s:\n0.5",op+1,param);
			} else {
				printf("op %d %s:\n%d",op+1,param,val);
			}
			break;
		case 3: // algorithm
			printf("%s %d:\n%s",param,val+1,algorithms[val]);
			break;
		case 4: // on or off params (AM and... i think thats it for now)
			printf("op %d %s:\n%s",op+1,param,onOff[val]);
			break;
		case 5: // LFO
			printf("%s:\n%s",param,lfoFreqs[val]);
			break;
		case 6: // SSGEG
			printf("op %d %s:\n%s",op+1,param,egTypes[val]);
			break;
		case 7: // preset patch
			printf("%s:\n%s",param,patchNames[val]);
			break;
		case 8: // polyphony
			printf("%s:\n%s",param,playModes[val]);
			break;
		case 9: // anything just on/off (non-operator)
			printf("%s:\n%s",param,onOff[val]);
			break;
	}
}

// restrict passed value within min/max values
void minMaxValue(int* var, int min, int max){
	int varVal = *var;
	
	if(varVal < min){
		*var = max;
	} else if(varVal > max){
		*var = min;
	}
}

// change all parameters at once (for preset()) within program as well as writing to YM2612
// TODO: include polyphony in params
void changeAllParams(int alg, int fb, int lfo, int vib, int trem,
int mult0, int  mult1, int mult2, int mult3, int det0, int det1, int det2, int det3,
int tl0, int tl1, int tl2, int tl3, int atk0, int atk1, int atk2, int atk3,
int dcy0, int dcy1, int dcy2, int dcy3, int sl0, int sl1, int sl2, int sl3,
int sr0, int sr1, int sr2, int sr3, int rel0, int rel1, int rel2, int rel3,
int rs0, int rs1, int rs2, int rs3, int eg0, int eg1, int eg2, int eg3,
int am0, int am1, int am2, int am3){
	
	// change param values inside program:
	
	// non-operator params:
	ym.algorithm = alg;
	ym.feedback = fb;
	ym.lfoFreq = lfo;
	ym.vibrato = vib;
	ym.tremolo = trem;
	
	// reconstruct operator arrays:
	
	// group 1
	int mult[] = {mult0, mult1, mult2, mult3};
	int det[] = {det0, det1, det2, det3};
	int tl[] = {tl0, tl1, tl2, tl3};
	
	// group 2
	int atk[] = {atk0, atk1, atk2, atk3};
	int dcy[] = {dcy0, dcy1, dcy2, dcy3};
	int sl[] = {sl0, sl1, sl2, sl3};
	int sr[] = {sr0, sr1, sr2, sr3};
	int rel[] = {rel0, rel1, rel2, rel3};
	int rs[] = {rs0, rs1, rs2, rs3};
	int eg[] = {eg0, eg1, eg2, eg3};
	
	// group 3
	int am[] = {am0, am1, am2, am3};
	
	// write non-operator parameters to YM:
	writeToYM(0,alg,fb,0xB0,0,3,0,1,0,0,0); // algorithm & feedback share one register (group 1)
	
	if(lfo == 0){ // special case for LFO (group 3)
		writeToYM(0,0,0,0x22,0,3,0,0,0,0,0);
	} else {
		writeToYM(0,lfo-1,1,0x22,0,3,0,0,0,0,0);
	}
	
	writeToYM(0,vib,trem,0xB4,0,4,0,1,8,0,0); // vib & trem share one register (group 3)
	
	// operator params:
	for(int i = 0; i < 4; i++){
		// change all operator params in program for each operator
		ym.multiple[i] = mult[i];
		ym.detune[i] = det[i];
		ym.totalLvl[i] = tl[i];
		ym.attack[i] = atk[i];
		ym.decay[i] = dcy[i];
		ym.susLvl[i] = sl[i];
		ym.susRate[i] = sr[i];
		ym.rateScl[i] = rs[i];
		ym.ssgeg[i] = eg[i];
		ym.amOn[i] = am[i];
		
		// group 1
		writeToYM(i,mult[i],det[i],0x30,0,4,1,1,6,0,0);
		writeToYM(i,tl[i],0,0x40,0,0,1,1,9,127,0); // total level has its own reg
		
		// group 2
		writeToYM(i,atk[i],rs[i],0x50,0,6,1,1,1,31,0);
		writeToYM(i,dcy[i],am[i],0x60,0,7,1,1,1,31,0); // AM is in group 4
		writeToYM(i,sl[i],rel[i],0x80,4,0,1,1,3,15,15);
		writeToYM(i,sr[i],0,0x70,0,0,1,1,7,31,0); // sustain rate has its own reg
		// finally, SSGEG
		if(eg[i] == 0){
			writeToYM(i,0,0,0x90,0,3,1,1,4,0,0);
		} else {
			writeToYM(i,eg[i]-1,1,0x90,0,3,1,1,0,0,0);
		}
	}
}

// for each preset patch, format name to be printed and change values of params, both within program and within YM2612
void preset(){		
	switch(ym.patchNum){
		case 0: // ding dong piano
			// params are grouped according to whether they're global, or groups of 4 for operator
			// i.e. alg,fb,lfo,vib,trem, mult[0-3], det[0-3], tl[0-3], atk[0-3], dec[0-3], sl[0-3], sr[0-3], rel[0-3], rs[0-3], eg[0-3], am[0-3]
			changeAllParams(7,0,0,0,0, 10,8,4,2, -3,1,3,0, 63,117,117,127, 0,0,0,0, 23,23,23,23, 0,0,0,0, 29,29,29,29, 1,1,1,1, 1,2,1,2, 0,0,0,0, 0,0,0,0);
			break;
		case 1: // toxic sludge
			changeAllParams(3,4,2,4,0, 1,10,2,6, 0,0,0,0, 127,127,127,127, 0,2,12,7, 4,0,23,31, 14,5,0,13, 29,16,0,29, 7,5,8,7, 1,1,1,1, 0,0,0,0, 0,0,0,0);
			break;
		case 2: // wooden steel
			changeAllParams(4,0,0,0,0, 10,8,4,2, -3,1,3,0, 27,112,112,127, 0,0,9,0, 16,16,16,21, 0,0,0,0, 29,29,29,29, 7,7,7,10, 1,2,1,2, 0,0,0,0, 0,0,0,0);
			break;
		case 3: // steel drum pad
			changeAllParams(5,3,3,0,3, 10,8,6,2, -3,1,3,0, 100,117,117,127, 10,26,25,0, 15,23,16,21, 13,7,12,0, 29,29,29,29, 9,1,19,11, 1,2,1,2, 0,0,0,0, 0,0,1,0);
			break;
		case 4: // (un)naturhythm
			changeAllParams(0,6,1,6,2, 10,8,1,2, -3,1,3,0, 88,112,112,127, 14,17,14,8, 18,19,19,22, 0,0,0,15, 29,29,29,29, 6,6,6,8, 2,1,2,1, 3,1,3,0, 1,1,1,0);
			break;
		case 5: // reedy ripper
			changeAllParams(2,5,0,0,0, 1,2,7,2, 3,-3,3,0, 126,97,106,127, 16,19,27,10, 27,22,26,21, 13,10,12,12, 31,31,31,27, 8,8,8,8, 1,1,1,1, 0,0,0,0, 0,0,0,0);
			break;
		case 6: // lately who?
			changeAllParams(7,4,0,0,0, 4,2,1,2, -2,2,1,-1, 124,117,120,127, 0,0,0,0, 16,23,31,12, 0,0,0,0, 29,29,0,18, 1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0);
			break;
		case 7: // tuned bounce
			changeAllParams(3,0,0,0,0, 4,6,3,4, -3,2,3,-1, 111,79,118,127, 11,14,2,1, 15,20,10,17, 0,0,0,0, 29,29,29,29, 9,1,10,9, 2,2,1,2, 0,0,0,0, 0,0,0,0);
			break;
		case 8: // morph metal
			changeAllParams(3,4,0,0,0, 4,6,7,4, -1,2,3,-1, 111,117,118,127, 10,22,27,1, 15,20,17,21, 0,0,0,0, 29,29,31,29, 9,1,10,9, 2,2,1,2, 0,0,0,0, 0,0,0,0);
			break;
		case 9: // get(s) nasty
			changeAllParams(3,5,0,0,0, 2,3,2,1, -2,-2,1,0, 116,118,119,127, 25,23,0,0, 25,27,19,24, 9,10,11,13, 31,31,31,31, 4,4,4,4, 1,1,1,1, 0,0,0,0, 0,0,0,0);
			break;
		case 10: // flarp wobble
			changeAllParams(5,5,2,5,2, 2,2,2,2, -1,1,3,0, 108,117,124,127, 8,6,12,7, 25,16,27,26, 4,0,0,0, 29,29,29,29, 4,1,3,2, 1,2,1,2, 0,0,0,0, 0,0,1,0);
			break;
		case 11: // pan flute
			changeAllParams(4,6,3,2,3, 4,5,4,4, -3,3,-2,0, 117,114,117,127, 3,22,29,18, 16,28,23,20, 0,0,0,0, 29,29,29,29, 7,7,8,7, 1,1,1,1, 0,0,0,0, 0,1,0,0);
			break;
		case 12: // deceptive bass
			changeAllParams(5,2,5,0,1, 2,2,10,6, 0,0,0,0, 127,104,118,127, 27,16,0,0, 25,19,19,21, 5,0,12,0, 31,31,31,31, 9,8,8,8, 2,2,1,1, 0,1,0,0, 0,0,1,0);
			break;
		case 13: // jagged EP
			changeAllParams(6,5,2,0,2, 7,3,14,3, -3,-1,3,1, 113,120,125,118, 0,0,25,0, 22,23,22,23, 11,11,11,11, 31,31,31,31, 10,8,8,8, 1,1,1,1, 0,0,0,0, 0,0,1,0);
			break;
		case 14: // all consuming
			changeAllParams(5,5,3,0,2, 1,1,4,2, 0,0,0,0, 120,120,120,127, 27,28,20,24, 30,26,8,28, 0,3,0,10, 7,31,31,31, 9,7,7,7, 1,1,1,1, 0,0,1,0, 0,1,0,0);
			break;
		case 15: // one operator
			changeAllParams(7,0,0,0,0, 2,2,2,2, 0,0,0,0, 0,0,0,127, 0,0,0,0, 31,31,31,31, 15,15,15,15, 31,31,31,31, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);
			break;
		case 16: // squelchy
			changeAllParams(1,0,0,0,0, 10,8,4,2, -3,1,3,0, 27,112,112,127, 18,10,20,0, 16,0,29,25, 0,0,0,0, 29,29,29,29, 7,7,7,10, 1,2,1,2, 0,0,0,0, 0,0,0,0);
			break;
		case 17: // ugly bell
			changeAllParams(6,4,0,0,0, 10,1,1,1, 0,0,0,0, 120,120,120,127, 0,0,0,0, 24,19,25,13, 0,0,0,0, 31,31,31,31, 8,8,6,9, 1,1,1,1, 0,0,0,0, 0,0,0,0);
			break;
		case 18: // moving electric
			changeAllParams(2,4,1,0,1, 2,6,8,4, -3,0,3,0, 120,111,105,125, 14,23,0,14, 24,23,22,31, 0,0,8,12, 24,23,27,31, 9,8,8,9, 1,2,2,0, 0,0,7,0, 0,1,1,0);
			break;
		case 19: // wurly slow dance
			changeAllParams(5,5,1,0,2, 4,2,10,2, 2,-1,1,0, 113,114,109,127, 0,23,21,0, 23,24,26,27, 0,0,0,0, 31,31,31,31, 7,7,9,9, 0,2,1,0, 0,0,3,0, 0,1,0,0);
			break;
		case 20: // ambient banjo
			changeAllParams(4,4,0,0,0, 4,3,7,2, 0,-1,1,0, 105,116,102,127, 18,0,14,0, 20,21,17,19, 7,0,0,0, 24,23,23,21, 9,9,9,9, 0,0,0,0, 8,0,0,0, 0,0,0,0);
			break;
	}
	
}

// this and changeCurrent() are responsible for the structure of the interface
// if group is changed: restrict group number within 0-3, change currently selected param, print
// only needed for first element of each group
void changeGroup(){
	minMaxValue(&ym.group,0,3);
	
	ym.current = 0; // display is at FIRST element of group when group is changed (possible TODO: have it go back to previously selected value)
	
	switch(ym.group){
		case 0:
			ym.value = &ym.patchNum;
			printToLCD(7);
			break;
		case 1:
			ym.value = &ym.algorithm;
			printToLCD(3);
			break;
		case 2:
			ym.value = &ym.attack[ym.op];
			printToLCD(1);
			break;
		case 3:
			ym.value = &ym.lfoFreq;
			printToLCD(5);
			break;
	}
}

// this and changeGroup() are responsible for the structure of the interface
// if currently selected param is changed: restrict op within 0-4, restrict value of 'current' within the values allowed within each group,
// change 'value' to the parameter at that slot within group, print
void changeCurrent(){
	minMaxValue(&ym.op,0,3); // this is called when op is changed, so restrict op from 0-3 (ops 1-4)
	
	uint8_t op = ym.op;
	
	if(ym.group == 0){ // more will be added here eventually
		minMaxValue(&ym.current,0,3);
		
		switch(ym.current){
			case 0:
				ym.value = &ym.patchNum;
				printToLCD(7);
				break;
			case 1:
				ym.value = &ym.velSens;
				printToLCD(0);
				break;
			case 2:
				ym.value = &ym.minVel;
				printToLCD(0);
				break;
			case 3:
				ym.value = &ym.polyphony;
				printToLCD(8);
				break;
		}
		
	} else if(ym.group == 1){
		minMaxValue(&ym.current,0,4);
		switch(ym.current){
			case 0: // algorithm
				ym.value = &ym.algorithm;
				printToLCD(3);
				break;
			case 1: // feedback
				ym.value = &ym.feedback;
				printToLCD(0);
				break;
			case 2: // multiple
				ym.value = &ym.multiple[op];
				printToLCD(2);
				break;
			case 3: // detune
				ym.value = &ym.detune[op];
				printToLCD(1);
				break;
			case 4: // total level
				ym.value = &ym.totalLvl[op];
				printToLCD(1);
				break;
		}
	} else if(ym.group == 2){
		minMaxValue(&ym.current,0,6);
		switch(ym.current){
			case 0: // attack
				ym.value = &ym.attack[op];
				printToLCD(1);
				break;
			case 1: // decay
				ym.value = &ym.decay[op];
				printToLCD(1);
				break;
			case 2: // susLvl
				ym.value = &ym.susLvl[op];
				printToLCD(1);
				break;
			case 3: // susRate
				ym.value = &ym.susRate[op];
				printToLCD(1);
				break;
			case 4: // release
				ym.value = &ym.release[op];
				printToLCD(1);
				break;
			case 5: // rateScl
				ym.value = &ym.rateScl[op];
				printToLCD(1);
				break;
			case 6: // ssgeg
				ym.value = &ym.ssgeg[op];
				printToLCD(6);
				break;
		}
	} else if(ym.group == 3){
		minMaxValue(&ym.current,0,3);
		switch(ym.current){
			case 0: // lfoFreq
				ym.value = &ym.lfoFreq;
				printToLCD(5);
				break;
			case 1: // vibrato
				ym.value = &ym.vibrato;
				printToLCD(0);
				break;
			case 2: // tremolo
				ym.value = &ym.tremolo;
				printToLCD(0);
				break;
			case 3: // am
				ym.value = &ym.amOn[op];
				printToLCD(4);
				break;
		}
	}
}

// value of currently selected param has been changed: restrict parameter value within max/min limits, write to YM2612, and update LCD
void changeValue(){
	int* val = ym.value;
	int op = ym.op;
	
	// group 0
	if(val == &ym.patchNum){
		minMaxValue(&ym.patchNum,0,MAX_PRESET);
		preset();
		printToLCD(7);
	
	} else if(val == &ym.velSens){
		minMaxValue(&ym.velSens,0,10);
		printToLCD(0);
	
	} else if(val == &ym.minVel){
		minMaxValue(&ym.minVel,0,127);
		printToLCD(0);
		
	} else if(val == &ym.polyphony){
		minMaxValue(&ym.polyphony,0,2);
		printToLCD(8);
		
	// group 1
	} else if(val == &ym.algorithm){
		minMaxValue(&ym.algorithm,0,7);
		writeToYM(op,ym.algorithm,ym.feedback,0xB0,0,3,0,1,0,0,0);
		printToLCD(3);
		
	} else if(val == &ym.feedback){
		minMaxValue(&ym.feedback,0,7);
		writeToYM(op,ym.feedback,ym.algorithm,0xB0,3,0,0,1,0,0,0);
		printToLCD(0);
				
	} else if(val == &ym.multiple[op]){
		minMaxValue(&ym.multiple[op],0,15);
		writeToYM(op,ym.multiple[op],ym.detune[op],0x30,0,4,1,1,6,0,0);
		printToLCD(2);
		
	} else if(val == &ym.detune[op]){
		minMaxValue(&ym.detune[op],-3,3);
		writeToYM(op,ym.detune[op],ym.multiple[op],0x30,4,0,1,1,5,0,0);
		printToLCD(1);
		
	} else if(val == &ym.totalLvl[op]){
		minMaxValue(&ym.totalLvl[op],0,127);
		writeToYM(op,ym.totalLvl[op],0,0x40,0,0,1,1,9,127,0);
		printToLCD(1);
		
	// group 2
	} else if(val == &ym.attack[op]){
		minMaxValue(&ym.attack[op],0,31);
		writeToYM(op,ym.attack[op],ym.rateScl[op],0x50,0,6,1,1,1,31,0);
		printToLCD(1);
		
	} else if(val == &ym.decay[op]){
		minMaxValue(&ym.decay[op],0,31);
		writeToYM(op,ym.decay[op],ym.amOn[op],0x60,0,7,1,1,1,31,0);
		printToLCD(1);
		
	} else if(val == &ym.susLvl[op]){
		minMaxValue(&ym.susLvl[op],0,15);
		writeToYM(op,ym.susLvl[op],ym.release[op],0x80,4,0,1,1,3,15,15);
		printToLCD(1);
		
	} else if(val == &ym.susRate[op]){
		minMaxValue(&ym.susRate[op],0,31);
		writeToYM(op,ym.susRate[op],0,0x70,0,0,1,1,7,31,0); // case 7
		printToLCD(1);
		
	} else if(val == &ym.release[op]){
		minMaxValue(&ym.release[op],0,15);
		writeToYM(op,ym.release[op],ym.susLvl[op],0x80,0,4,1,1,3,15,15); // case 3
		printToLCD(1);
		
	} else if(val == &ym.rateScl[op]){
		minMaxValue(&ym.rateScl[op],0,3);
		writeToYM(op,ym.rateScl[op],ym.attack[op],0x50,6,0,1,1,2,0,31); // case 2
		printToLCD(1);
		
	} else if(val == &ym.ssgeg[op]){ // special case
		minMaxValue(&ym.ssgeg[op],0,8);
		if(ym.ssgeg[op] == 0){
			writeToYM(op,0,0,0x90,0,3,1,1,4,0,0);
		} else {
			writeToYM(op,ym.ssgeg[op]-1,1,0x90,0,3,1,1,0,0,0);
		}
		printToLCD(6);
		
	// group 3
	} else if(val == &ym.lfoFreq){ // special case
		minMaxValue(&ym.lfoFreq,0,8);
		if(ym.lfoFreq == 0){
			writeToYM(op,0,0,0x22,0,3,0,0,0,0,0);
		} else {
			writeToYM(op,ym.lfoFreq-1,1,0x22,0,3,0,0,0,0,0);
		}
		printToLCD(5);
		
	} else if(val == &ym.vibrato){
		minMaxValue(&ym.vibrato,0,7);
		writeToYM(op,ym.vibrato,ym.tremolo,0xB4,0,4,0,1,8,0,0); // case 8
		printToLCD(0);
		
	} else if(val == &ym.tremolo){
		minMaxValue(&ym.tremolo,0,3);
		writeToYM(op,ym.tremolo,ym.vibrato,0xB4,4,0,0,1,8,0,0); // case 8
		printToLCD(0);
		
	} else if(val == &ym.amOn[op]){
		minMaxValue(&ym.amOn[op],0,1);
		writeToYM(op,ym.amOn[op],ym.decay[op],0x60,7,0,1,1,2,0,31); // case 2
		printToLCD(4);

	}
}

// return maximum of two values
int max(int	val1, int val2){
	if(val1 > val2){
		return val1;
	} else {
		return val2;
	}
}

// schedule notes to be turned off or on, format data to be written to YM2612 registers A0 (freq low) and A4 (freq high + block (octave))
void note(uint8_t noteIn, uint8_t velocity, bool on){
	cli();
	
	uint16_t noteOut;
	int oct;
	
	oct = noteIn / 12 - 1; // middle C is octave 5 -> make it octave 4
	
	minMaxValue(&oct, 0, 7); // 'block' can be 0-7 (8 octaves)
	
	// note frequencies within 1 octave
	uint16_t notes[] = {311, 329, 349, 370, 392, 415, 440, 466, 493, 523, 554, 586};
	
	noteOut = notes[noteIn % 12]; // ex. 63 is D# -> 63 % 12 = 3 -> notes[3] = 370 (Hz)
		
	if(on){ // note on message has been received
		for(int i = 0; i < 6; i++){
			
			// channel is not on and is not waiting to be turned on
			if(!ym.notesOn[i][1] && !ym.notesOn[i][2]){
				
				ym.vel[i] = max(velocity, ym.minVel); // velocity can't be lower than minVel or else it'll all be too quiet maybe or something
				
				// format pitch to be written to YM2612
				ym.freq[i][0] = (oct<<3) | ((noteOut & 0x0700)>>8); // top 3 bits of freq, next 3 bits are octave
				ym.freq[i][1] = (noteOut & 0x00FF); // lower 8 bits of freq
				
				ym.notesOn[i][0] = noteIn; 
				ym.notesOn[i][1] = 1; // schedule note to be turned on
				
				break; // only loop until finding first available channel
				
			// if it is already on but the note is the same value, turn channel off and then on again
			} else if(ym.notesOn[i][1] && ym.notesOn[i][2] && ym.notesOn[i][0] == noteIn){
				uint8_t chanGrp = (i > 2); // 0 or 1, depending on value of i
				
				sendreg(0, 0x28, 0x00+chan[i]);
				
				sendreg(chanGrp, 0xA4+i%3, ym.freq[i][0]);
				sendreg(chanGrp, 0xA0+i%3, ym.freq[i][1]);
				
				sendreg(0, 0x28, 0xF0+chan[i]);
				
				break;
			}
		}
	} else { // key is released
		for(uint8_t i = 0; i < 6; ++i){
			// find matching key in notesOn, only turn off if it is not waiting to be turned off
			if(ym.notesOn[i][1] && ym.notesOn[i][2] && ym.notesOn[i][0] == noteIn){
				
				ym.notesOn[i][1] = 0; // schedule corresponding channel to be turned off
				
				break;
			}
		}
	}
	
	sei();
}

// receive MIDI messages and translate (note on, note off, pitch, modulation, etc)
ISR(USART_RX_vect){
	glb.sreg = SREG;
	cli();
	
	int data = UDR0; // data coming into RX pin
	
	// the 2 data bytes after the status message (sometimes only 1 is used)
	int data1;
	int data2;
	
	if(data < 0xF8){ // >= 0xF8 - system control messages, not used
		glb.midiBuf[glb.midiIndex] = data;
		
		// status messages start 1XXX XXXX, all others 0XXX XXXX
		if((data & 0xF0) >= 0x80) glb.msgStart = glb.midiIndex;
		
		glb.midiIndex++; // move forward in midi data buffer
		
		minMaxValue(&glb.midiIndex, 0, 127);
	}
		
	bool case1 = 0;
	bool case2 = 0;
	
	// my way of creating a ring buffer, since midi messages are up to 3 bytes long
	if(glb.msgStart == 127){
		data1 = glb.midiBuf[0];
		data2 = glb.midiBuf[1];
		case1 = 1;
	} else if(glb.msgStart == 126){
		data1 = glb.midiBuf[127];
		data2 = glb.midiBuf[0];
		case2 = 1;
	} else {
		data1 = glb.midiBuf[glb.msgStart+1];
		data2 = glb.midiBuf[glb.msgStart+2];
	}
	
	// if message is 3 bytes long (note on/off, controller, etc)
	if((glb.midiIndex - glb.msgStart == 3) || (glb.midiIndex == 2 && case1) || (glb.midiIndex == 1 && case2)){
		switch(glb.midiBuf[glb.msgStart] & 0xF0){ // upper nibble of status byte is status message, lower nibble is midi channel (not currently used)
			case 0x90: // note on: status byte - note number - velocity	
				note(data1,data2,1);
				break;
			case 0x80: // note off: status byte - note number - note off velocity	
				note(data1,data2,0);
				break;
			case 0xA0: // poly AT
				// unused
				break;
			case 0xB0: // controllers (mod wheel etc): status byte - controller number - data
				if(data1 == 1){ // mod wheel is controller 1, currently only used for LFO frequency
					if(data2 == 0){ // reset to original LFO value when mod wheel is not in use
						if(ym.lfoFreq == 0){
							sendreg(0,0x22,0);
						} else {
							sendreg(0,0x22,0x08+ym.lfoFreq-1);
						}
					} else { // 127 / 8 = 18
						sendreg(0,0x22,0x08+(data2/18));
					}
				
				} else if(data1 == 64){ // sustain
					if(data2 == 0){
						ym.sustain = true;
					} else {
						ym.sustain = false;
					} 
				}
				
				break;
			case 0xC0: // program change (probably won't use)
				//
				break;
		}
		
	// message is 2 bytes (aftertouch, pitch bend)
	} else if((glb.midiIndex - glb.msgStart == 2) || (glb.midiIndex == 1 && case1) || (glb.midiIndex == 0 && case2)){
		switch(glb.midiBuf[glb.msgStart] & 0xF0){
			case 0xD0: // aftertouch, currently only used for vibrato
				if(data1 == 0){ // if 0, restore old values
					for(int i = 0; i < 3; i++){ // write to all 6 channels
						for(int j = 0; j < 2; j++){
							sendreg(j, 0xB4+i, 0xC0 + (ym.tremolo<<4) + ym.vibrato); // register is shared with tremolo and panning 
						}
					}
				} else {
					for(int i = 0; i < 3; i++){
						for(int j = 0; j < 2; j++){
							sendreg(j, 0xB4+i, 0xC0 + (ym.tremolo<<4) + data1/18);
						}
					}
				}
				break;
				
			case 0xE0: // pitch bend (kinda wonky, use it to turn all notes off instead for now)
				for(int i = 0; i < 6; i++){
					sendreg(0, 0x28, 0x00+chan[i]);
					ym.notesOn[i][0] = 0;
					ym.notesOn[i][1] = 0;
					ym.notesOn[i][2] = 0;
				}
					/*
					if(data1 == 0){
						for(int i = 0; i < 6; i++){
							sendreg((i > 2), 0xA0 + i%3, ym.freq[1][i]);
						}
					} else {
						for(int i = 0; i < 6; i++){
							sendreg((i > 2), 0xA0 + i%3, ym.freq[1][i] + data1);
						}
					}
					*/
				break;
		}
	}
	
	SREG = glb.sreg;
}

// when the timer overflows, turn on or off any notes (channels) that are waiting to be turned off or on
ISR(TIMER1_OVF_vect){
	glb.sreg = SREG;
	cli();
	
	uint8_t chanGrp;
	int opLvl;
	
	for(int i = 0; i < 6; i++){
		chanGrp = (i > 2);
		
		// turn notes on 
		if(ym.notesOn[i][1] && !ym.notesOn[i][2]){ // note is currently off but should be on
			
			// FOR VELOCITY
			
			for(int o = 0; o < 4; o++){
				// velocity sensitivity: more sensitivity means output leans more and more toward velocity when averaging
				opLvl = ((ym.velSens)*ym.vel[i] + (10-ym.velSens)*ym.totalLvl[o]) / 10;
				
				// each operator's level becomes the average of the selected level and velocity input
				sendreg(chanGrp, 0x40+i%3+opOffset[o], 127 - opLvl);
			}
			
			
			// set frequency high/low registers
			sendreg(chanGrp, 0xA4+i%3, ym.freq[i][0]);
			sendreg(chanGrp, 0xA0+i%3, ym.freq[i][1]);
			
			// turn note on
			sendreg(0, 0x28, 0xF0+chan[i]);
			
			// flag to indicate that note is indeed on
			ym.notesOn[i][2] = 1;
			
		// turn notes off
		} else if(!ym.notesOn[i][1] && ym.notesOn[i][2]){ // note is currently on but should be off
			
			if(!ym.sustain){ // see how this goes
				// turn note off
				sendreg(0, 0x28, 0x00+chan[i]);
				
				// clear channel's note number, set both flags to 0 to indicate that note is off
				ym.notesOn[i][0] = 0;
				ym.notesOn[i][1] = 0;
				ym.notesOn[i][2] = 0;
			}
		}
	}
	
	SREG = glb.sreg;
}

// everything related to encoder and buttons happens here
ISR(PCINT2_vect){	
	glb.sreg = SREG;
	cli();
	
	// get current pin values
	uint8_t RPG[] = {(PIND & (1<<ENC_A))>>ENC_A, (PIND & (1<<ENC_B))>>ENC_B}; // current pin values for individual encoder pins
	uint8_t RPGpin = PIND & ((1<<ENC_A) | (1<<ENC_B)); // current overall encoder status
	
	uint8_t BTN_L_status = (PIND & (1<<BTN_L))>>BTN_L; // current value for left button pin
	uint8_t BTN_R_status = (PIND & (1<<BTN_R))>>BTN_R; // right button pin

	
	// if the thing that caused the interrupt was either button going low, cleared to change group
	if(!BTN_L_status || !BTN_R_status) glb.chgGrp = true;
	
	if(glb.RPGpinOld == RPGpin){
		// if interrupt was caused by button change and not RPG:
		if(glb.chgGrp){
			if(BTN_L_status && !glb.BTN_L_old){ // button has just been released
				--ym.group;
				changeGroup();
			}
			if(BTN_R_status && !glb.BTN_R_old){
				++ym.group;
				changeGroup();
			}
		}
		// store pin values
		glb.BTN_L_old = BTN_L_status;
		glb.BTN_R_old = BTN_R_status;
		
	} else {
		glb.chgGrp = false; // if the interrupt was caused by a change in the RPG (while a button was held), if a button is released the group won't be changed
		if(RPG[1] && !RPG[0]){ // limit number of cases so that value only changes once per turn (as opposed to 4 times)
			if ((glb.RPGold[1] == RPG[0]) && (glb.RPGold[0] != RPG[1])){ // encoder turned counterclockwise
				if(!BTN_L_status){ // if encoder button is held
					--ym.current; // change currently selected parameter (decrement)
					changeCurrent();
				} else if(!BTN_R_status){ // other button is held
					--ym.op; // change currently selected operator
					changeCurrent();
				} else if(BTN_L_status && BTN_R_status){ // neither button is held
					--*ym.value; // change value of currently selected parameter
					changeValue();
				}
			} else if ((glb.RPGold[0] == RPG[1]) && (glb.RPGold[1] != RPG[0])){ // encoder turned clockwise
				if(!BTN_L_status){
					++ym.current;
					changeCurrent();
				} else if(!BTN_R_status){
					++ym.op;
					changeCurrent();
				} else if(BTN_L_status && BTN_R_status){
					++*ym.value;
					changeValue();
				}
			}
		}
		// set stored values for all interface pins
		glb.RPGold[0] = RPG[0];
		glb.RPGold[1] = RPG[1];
		glb.RPGpinOld = RPGpin;
		
		glb.BTN_L_old = BTN_L_status;
		glb.BTN_R_old = BTN_R_status;
	}
	
	SREG = glb.sreg;
}
