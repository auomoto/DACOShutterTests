/*
 * DACOShutterTests.c
 * Created: 12/30/2014 11:37:38 AM
 *  Author: Alan Uomoto
 * 16 MHz crystal clock
 * set fuses:
 * avrdude -c usbtiny -p atmega168 -U lfuse:w:0xff:m -U hfuse:w:0xdd:m
 
 
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <stdlib.h>

#define LEDDDR	DDRB
#define LEDPORT	PORTB
#define LEDPINS	PINB
#define LEDPIN	PB5

#define SHUTTERDDR	DDRC
#define SHUTTERPORT	PORTC
#define SHUTTERPINS	PINC
#define SHUTTERTRIG	PC0
#define SHUTTERPIN1	PC1
#define SHUTTERPIN2	PC2
#define SHUTTERPIN3 PC3

#define INPUTCAPTUREDDR		DDRB
#define INPUTCAPTUREPORT	PORTB
#define INPUTCAPTUREPINS	PINB
#define INPUTCAPTUREPIN		PB0

#define BAUDRATE	9600
#define MYUBRR		((F_CPU / 16 / BAUDRATE) - 1)
#define CHARSENT	(UCSR0A & (1<<RXC0))
#define TXREADY		(UCSR0A & (1<<UDRE0))
#define PRINTLINE(STRING)	serialSendStr(strcpy(strbuf, STRING))

// Function prototypes
int main(void);
void exposureStart(uint8_t expTime, uint8_t pattern);
void exposureStop(void);
void initialize(void);
void ledToggle(void);
int16_t serialRecvNum(void);
void serialSendByte(uint8_t);
void serialSendBin(uint8_t);
void serialSendCRLF(void);
void serialSendNum(uint16_t number);
void serialSendStr(char *);
void shutterDemo(void);
void shutterSet(uint8_t pattern);

// Globals
volatile uint16_t pulseCount, pulseWidth;
volatile uint16_t inputCaptureStart, inputCaptureEnd;
volatile uint16_t timeToOpen, timeToClose;
volatile uint8_t shutterOld, shutterCurrent, shutterNew;
volatile uint8_t exposing, expTimeElapsed, expTimeRequested;

const char string01[] PROGMEM = "Enter pulse width in ms: ";
const char string02[] PROGMEM = "Pulse width is ";
const char string03[] PROGMEM = "Requested exposure time: ";
const char string04[] PROGMEM = " secs";
const char string05[] PROGMEM = " ms";
const char string06[] PROGMEM = "Exposure time elapsed: ";
const char string07[] PROGMEM = "Shutter-power pulse width: ";
const char string08[] PROGMEM = "Command list:\n\r";
const char string09[] PROGMEM = "Enter exposure time in seconds (0 to 255): ";
const char string10[] PROGMEM = "Enter shutter pattern code: ";
const char string11[] PROGMEM = "Shutters set to: ";
const char string12[] PROGMEM = "Shutters will be: ";
const char string13[] PROGMEM = "Exposure time remaining: ";
const char string14[] PROGMEM = "<SPACE> - print status\n\r";
const char string15[] PROGMEM = "e - start exposure\n\r";
const char string16[] PROGMEM = "p - change power pulse width\n\r";
const char string17[] PROGMEM = "s - shutter pattern for next exposure\n\r";
const char string18[] PROGMEM = "S - drive shutters to new position\n\r";
const char string19[] PROGMEM = "t - set exposure time\n\r";
const char string20[] PROGMEM = "x - stop a running exposure\n\r";
const char string21[] PROGMEM = "Already exposing; use x to stop\n\r";
const char string22[] PROGMEM = "Status: ";
const char string23[] PROGMEM = "exposing";
const char string24[] PROGMEM = "idle";
const char string25[] PROGMEM = "time to open shutter: ";
const char string26[] PROGMEM = "D - run startup exercise\n\r";
const char string27[] PROGMEM = "time to close shutter: ";
const char string28[] PROGMEM = "input capture pin: ";

int main()
{

	char strbuf[48], cmd;

	initialize();

	for (;;) {
		if (CHARSENT) {
			cmd = UDR0;
			serialSendByte(cmd);
			serialSendCRLF();
			switch (cmd) {

				case (' '):
					PRINTLINE(string22);
					if (exposing) {
						PRINTLINE(string23);
					} else {
						PRINTLINE(string24);
					}
					serialSendCRLF();
					PRINTLINE(string03);
					serialSendNum(expTimeRequested);
					PRINTLINE(string04);
					serialSendCRLF();
					if (exposing) {									// Elapsed exposure time
						PRINTLINE(string06);
						serialSendNum(expTimeElapsed);
						PRINTLINE(string04);
						serialSendCRLF();
						PRINTLINE(string13);
						serialSendNum(expTimeRequested - expTimeElapsed);
						serialSendCRLF();
					}
					PRINTLINE(string25);
					serialSendNum((uint16_t) timeToOpen);
					PRINTLINE(string05);
					serialSendCRLF();
					PRINTLINE(string27);
					serialSendNum((uint16_t) timeToClose);
					PRINTLINE(string05);
					serialSendCRLF();
					PRINTLINE(string07);
					serialSendNum(pulseWidth * 10);
					PRINTLINE(string05);
					serialSendCRLF();
					PRINTLINE(string11);							// Current shutter pattern
					serialSendNum(shutterCurrent);
					serialSendCRLF();
					PRINTLINE(string12);							// Next shutter pattern
					exposing ? serialSendNum(shutterOld) : serialSendNum(shutterNew);
					serialSendCRLF();
					PRINTLINE(string28);
					serialSendNum(INPUTCAPTUREPINS & (1 << INPUTCAPTUREPIN));
					serialSendCRLF();
					break;

				case ('D'):
					shutterDemo();
					break;

				case ('e'):
					if (exposing) {
						PRINTLINE(string21);
					} else {
						exposureStart(expTimeRequested, shutterNew);
					}
					break;

				case ('p'):
					PRINTLINE(string01);
					pulseWidth = serialRecvNum() / 10;
					if (10 * pulseWidth >= 1000) {		// SHouldn't be longer than an exp time tick
							pulseWidth = 99;
					}
					PRINTLINE(string02);
					serialSendNum(pulseWidth * 10);
					PRINTLINE(string05);
					serialSendCRLF();
					break;

				case ('s'):
					PRINTLINE(string10);
					shutterNew = (uint8_t) (serialRecvNum() % 8);
					PRINTLINE(string12);
					serialSendNum(shutterNew);
					serialSendCRLF();
					break;

				case ('S'):
					PRINTLINE(string10);
					shutterNew = (uint8_t) (serialRecvNum() % 8);
					shutterSet(shutterNew);
					PRINTLINE(string11);
					serialSendNum(shutterCurrent);
					serialSendCRLF();
					break;

				case ('t'):
					PRINTLINE(string09);
					expTimeRequested = serialRecvNum();
					PRINTLINE(string03);
					serialSendNum(expTimeRequested);
					PRINTLINE(string04);
					serialSendCRLF();
					break;

				case ('x'):
					if (exposing) {
						exposureStop();
					}
					break;

				default:
					PRINTLINE(string08);
					PRINTLINE(string14);
					PRINTLINE(string26);
					PRINTLINE(string15);
					PRINTLINE(string16);
					PRINTLINE(string17);
					PRINTLINE(string18);
					PRINTLINE(string19);
					break;

			}
			serialSendByte('>');
		}
	}
}

void exposureStart(uint8_t expTime, uint8_t pattern)
{

	expTimeElapsed = 0;
	expTimeRequested = expTime;
	timeToOpen = 0;

	TCCR1B = 0b00001100;	// Start timer/clock1, CTC mode on OCR1A, 256 prescaler
	shutterSet(pattern);	// Open the shutter
	sei();
	timeToOpen = 1000 * (uint32_t) (inputCaptureEnd - inputCaptureStart)/62500;
	exposing = 1;

}

void exposureStop(void)
{

	timeToClose = 0;

	shutterSet(shutterOld);
	TCCR1B = 0b00000000;	// Stop timer/clock1
	cli();
	timeToClose = 1000 * (uint32_t) (inputCaptureEnd - inputCaptureStart)/62500;	// ms
	exposing = 0;

}

void initialize()
{

	// On-board LED
	LEDDDR = _BV(LEDPIN);
	
	// USART setup
	UBRR0H = (uint8_t) (MYUBRR >> 8);		// Baud rate
	UBRR0L = (uint8_t) MYUBRR;
	UCSR0B = (1 << RXEN0) | (1 << TXEN0);	// Enable transmit & receive
	UCSR0C = (3 << UCSZ00);					// 8 bits, no parity, one stop bit
	
	// Input capture pin
	INPUTCAPTUREDDR &= ~(1 << INPUTCAPTUREPIN);	// ICP1 is input

	// Shutter pins
	SHUTTERDDR = 0x0F;		// Low four bits are shutter controls

	// Shutter pulse width timer
	pulseWidth = 25;		// Shutter current-on time in increments of 10.048 ms
	TIMSK0 = 0b00000010;	// Interrupt on OCR0A match
	TCCR0A = 0b00000010;	// CTC mode on OCR0A value

	// Exposure timer
	OCR1A = 62499;			// This count gives 1-second ticks with a 256 prescaler
	TIMSK1 = 0b00100010;	// Output compare A match and input capture interrupt enable
	TCCR1A = 0b00000000;

	shutterSet(0);			// Close all the shutters
	shutterNew = 7;
	expTimeRequested = 5;
	serialSendStr("Shutter Testing 2015-01-25");
	serialSendCRLF();
	serialSendByte('>');

}

void ledToggle(void)
{

	// Writing a "1" to a pin (not a port) toggles it
	LEDPINS = _BV(LEDPIN);

}

/*
	serialRecvNum() reads characters from the serial port and returns the twos complement
	16-bit integer. It echos characters as they're typed. Processing happens when a carriage
	return is entered.
	
	Errors: If more than 5 characters without a sign are entered, or if more than 6 characters
	with a leading + or - sign are entered, it writes a question mark (?) and CRLF to the
	serial port and returns zero (0).
*/

int16_t serialRecvNum(void)
{
	char strBuf[7];
	uint8_t i;

	i = 0;
	for (;;) {
		while (!CHARSENT) {
			asm("nop");
		}
		strBuf[i] = UDR0;
		serialSendByte(strBuf[i]);
		if (strBuf[i] == '\r') {
			strBuf[i] = '\0';
			serialSendByte('\n');
			break;
		}
		if (i == 5) {
			if ((strBuf[0] != '+') && (strBuf[0] != '-')) {
				serialSendCRLF();
				serialSendByte('?');
				serialSendCRLF();
				return(0);
			}
		}
		if (i == 6) {
			serialSendCRLF();
			serialSendByte('?');
			serialSendCRLF();
			return(0);
		}
		i++;
	}
	return(atoi(strBuf));
}

void serialSendBin(uint8_t number)
{

	char strBuf[7];

	serialSendStr(itoa(number, strBuf, 2));

}
void serialSendByte(uint8_t c)
{

	while (!TXREADY) {
		asm("nop");
	}
	UDR0 = c;

}

void serialSendCRLF(void)
{

	serialSendStr("\n\r");

}

void serialSendNum(uint16_t number)
{

	char strBuf[7];

	serialSendStr(ltoa((int32_t) number, strBuf, 10));

}

void serialSendStr(char *str)
{

	uint8_t i;

	i = 0;
	while (str[i]) {
		serialSendByte(str[i++]);
	}

}

void shutterDemo()
{

	uint8_t i;

	shutterSet(0);
	_delay_ms(500);
	shutterSet(7);
	_delay_ms(250);
	for (i = 0; i < 8; i++) {
		shutterSet(i);
		_delay_ms(500);
	}

	shutterSet(0);
	_delay_ms(500);
	shutterSet(1);
	_delay_ms(500);
	shutterSet(3);
	_delay_ms(500);
	shutterSet(7);
	_delay_ms(500);
	shutterSet(6);
	_delay_ms(500);
	shutterSet(4);
	_delay_ms(500);
	shutterSet(0);
	_delay_ms(500);
	shutterSet(1);
	_delay_ms(250);
	shutterSet(3);
	_delay_ms(250);
	shutterSet(7);
	_delay_ms(250);
	shutterSet(6);
	_delay_ms(250);
	shutterSet(4);
	_delay_ms(250);
	shutterSet(0);

}

void shutterSet(uint8_t pattern)
{

	cli();

	TCCR1B |= (1 << ICNC1);									// Enable noise canceler
	if (INPUTCAPTUREPINS & (1 << INPUTCAPTUREPIN)) {		// if ICP1 is high
		TCCR1B &= ~(1 << ICES1);							// select falling edge
		} else {											//     otherwise
		TCCR1B |= (1 << ICES1);								// select rising edge
	}

	shutterOld = shutterCurrent;
	OCR0A = 155;					// Output compare timer/counter0 at 155 gives 9.984 ms increments
	pulseCount = 0;
	SHUTTERPORT = (pattern << 1);
//	TCNT0 = 0;				// Reset timer/counter0
//	TCNT1 = 0;				// Reset timer/counter1
	sei();
	TCCR0B = 0b00000101;	// Start the pulse width clock with 1024 prescaler
	inputCaptureStart = TCNT0 = TCNT1 = 0;	// Capture current exposure time within the second
	SHUTTERPINS = _BV(SHUTTERTRIG);		// Fire the trigger
//	inputCaptureStart = ICR1;	// Capture current exposure time within the second
	while (pulseCount < pulseWidth) {
		// wait for shutter to move
	}
	TCCR0B = 0b00000000;	// Stop the pulse width clock
	cli();
	SHUTTERPINS = _BV(SHUTTERTRIG);		// Release the trigger
	shutterCurrent = pattern;

}

/* Shutter activation pulse counter */
ISR(TIMER0_COMPA_vect)
{

	pulseCount++;

}

/* Input Capture Interrupt */
ISR(TIMER1_CAPT_vect)
{

	inputCaptureEnd = ICR1;

}

/* Exposure time counter */
ISR(TIMER1_COMPA_vect)
{

	if (++expTimeElapsed >= expTimeRequested) {
		exposureStop();
	}
	ledToggle();

}


/*====================================================================

Setting the DACO shutter trigger pulse width using TIMER0

TCCR0A - Timer/Counter0 Control Register A (page 101)
=================================================================
|   7   |   6   |   5   |   4   |   3   |    2  |   1   |   0   |
| COM0A1| COM0A0| COM0B1| COM0B0|   -   |    -  | WGM01 | WGM00 |
=================================================================

7-6	COM0A1:0 Compare output mode for channel A (output compare pin)
5-4	COM0B1:0 Compare output mode for channel B (output compare pin)
1-0	WGM01:0	- Waveform generation mode bits (see TCCR0B)

TCCR0A = 0b00000010;	// CTC mode on OCRA value

TCCR0B - Timer/Counter0 Control Register B (page 104)
=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
| FOC0A | FOC0B |   -   |   -   | WGM02 |  CS02 |  CS01 |  CS00 |
=================================================================

7:	FOC0A	- Force Output Compare A
6:	FOC0B	- Force OUtput Compare B
3:	WGM02	- Waveform generation mode (see TCCR0A)
2-0	CS02:0	- Clock select (we will use 101 for 1024 prescaler

TCCR0B = 0b00000101;	// 1024 prescaler

TIMSK0 - Timer/Counter0 Interrupt Mask Register (page 106)
=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
|   -   |   -   |   -   |   -   |   -   | OCIE0B| OCIE0A| TOIE0 |
=================================================================

2: OCIE0B	- Output compare B match interrupt enable
1: OCIE0A	- Output compare A match interrupt enable
0: TOIE0	- Overflow interrupt enable

TIMSK0 = 0b00000010;	// Interrupt on match with OCR0A

Timing the exposure

We want to interrupt on 1-second ticks from TIMER1.
Set timer scaling to 1/256 (CS12:0 = 100)
Load OCR1A with 62499.
Use "clear on timer compare match" CTC mode.
Interrupt on output compare A match.

TCCR1A - Timer/Counter1 Control Register A (page 130)
=================================================================
|   7   |   6   |   5   |   4   |   3   |    2  |   1   |   0   |
| COM1A1| COM1A0| COM1B1| COM1B0|   -   |    -  | WGM11 | WGM10 |
=================================================================

7-6	COM1A1:0 Compare output mode for channel A
5-4	COM1B1:0 Compare output mode for channel B
1-0	WGM11:0	- Waveform generation mode bits (see TCCR1B)

TCCR1A = 0b00000000;


TCCR1B - Timer/Counter1 Control Register B (page 132)
=================================================================
|   7   |   6   |   5   |   4   |   3   |    2  |   1   |   0   |
| ICNC1 | ICES1 |   -   | WGM13 | WGM12 |  CS12 |  CS11 |  CS10 |
=================================================================

7:	ICNC1	- Input capture noise canceler. Does 4X sampling of ICP1
6:	ICES1	- Input capture edge select. 0->falling, 1->rising
5:	Reserved
4-3	WGM13:2	- More bits of the Waveform generation mode (see TCCR1A)
2-0	CS12:0	- Clock Select (page 133)

TCCR1B = 0b00001100;	// use CTC on OCR1A, prescale 256

TIMSK1 - Timer/Counter1 Interrupt Mask Register (page 135)
=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
|   -   |   -   | ICIE1 |   -   |   -   | OCIE1B| OCIE1A| TOIE1 |  
=================================================================

5: ICIE1	- Input capture interrupt enable
2: OCIE1B	- Output compare B match interrupt enable
1: OCIE1A	- Output compare A match interrupt enable
0: TOIE1	- Overflow interrupt enable

TMSK1 = 0b00010010;	(Output compare A match and input capture interrupt enable)

UBRR0L and UBRR0H - USART baud rate registers (page 194)

=================================================================
|   15  |   14  |   13  |   12  |   11  |   10  |   9   |   8   |
|   -   |   -   |   -   |   -   |            UBRR0[11:8]        |
|----------------------------------------------------------------
|                            UBRR0[7:0]                         |
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
=================================================================

UCSR0A - USART control and status register A (page 190)

=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
|  RXC0 | TXC0  | UDRE0 |  FE0  |  DOR0 |  UPE0 |  U2X0 | MPCM0 |
=================================================================

7: RXC0		- USART receive complete (cleared when buffer is empty)
6: TXC0		- USART transmit complete
5: UDRE0	- USART data register empty
4: FE0		- Frame error
3: DOR0		- Data OverRun
2: UPE0		- Parity error
1: U2X0		- Double USART transmission speed
0: MPCM0	- Multiprocessor communication mode

UCSR0B - USART control and status register B (page 191)

=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
| RXCIE0| TXCIE0| UDRIE0| RXEN0 | TXEN0 | UCSZ02| RXB80 | TXB80 |
=================================================================

7: RXCIE0	- RX complete interrupt enable
6: TXCIE0	- TX complete interrupt enable
5: UDRIE0	- Data register empty interrupt enable
4: RXEN0	- Receiver enable
3: TXEN0	- Transmitter enable
2: UCSZ02	- Character size
1: RXB80	- Receive data bit 8
1: TXB80	- Transmit data bit 8

UCSR0C - USART control and status register C (page 192)

=================================================================
|   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
|UMSEL01|UMSEL00| UPM01 | UPM00 | USBS0 | UCSZ01| UCSZ00| UCPOL0|
=================================================================

7-6: UMSEL01:0	- Operation mode
5-4: UPM01:0	- Parity
3: USBS0		- Stop bit select
2-1: UCSZ01:0	- Character size
0: UCPOL0		- Clock polarity

*/