#include <avr/io.h>
#include <stdlib.h>
#include <stdio.h>
#include "avr/interrupt.h"
#include "util/delay.h"
//pinouts for atmega1284
//OCR1A = PD5
#ifndef TLC5940_GS_PORT
#define TLC5940_GS_PORT PORTD
#endif
#ifndef TLC5940_GS_PIN
#define TLC5940_GS_PIN 5
#endif
// serial clock -OC0B PB4
#ifndef TLC5940_SCK_PORT
#define TLC5940_SCK_PORT PORTB
#endif
#ifndef TLC5940_SCK_PIN
#define TLC5940_SCK_PIN 4
#endif
// latch - PC4
#ifndef TLC5940_XLAT_PORT
#define TLC5940_XLAT_PORT PORTC
#endif
#ifndef TLC5940_XLAT_PIN
#define TLC5940_XLAT_PIN 4
#endif
// programming select - PC7
#ifndef TLC5940_VPRG_PORT
#define TLC5940_VPRG_PORT PORTC
#endif
#ifndef TLC5940_VPRG_PIN
#define TLC5940_VPRG_PIN  7
#endif
// blank outputs - OC2B pullup resistor + PD6
#ifndef TLC5940_BLANK_PORT
#define TLC5940_BLANK_PORT PORTD
#endif
#ifndef TLC5940_BLANK_PIN
#define TLC5940_BLANK_PIN 6
#endif
// serial data master out slave in OC0A- PB3
#ifndef TLC5940_MOSI_PORT
#define TLC5940_MOSI_PORT PORTB
#endif
#ifndef TLC5940_MOSI_PIN
#define TLC5940_MOSI_PIN 3
#endif

#ifndef TLC5940_N
#define TLC5940_N 3
#endif

#define TLC5940_LED_N 16 * TLC5940_N

class TLC5940 {
	public:
	// constructor (don't need a destructor)
	TLC5940();
	// initilize the chip by filling the dc data
	void init(void);
	// set the dot correction array
	void setDC(uint8_t led, uint8_t val);
	// set the brightness of an led in the array
	void setGS(uint8_t led, uint16_t val);
	// tell the chip to clock in the data from the GS array
	void update(void);
	// gs refresh function (lives in an ISR that fires ever 4096 greyscale clocks)
	void refreshGS(void);

	private:
	// serial data function - returns if a latch is needed or not
	bool serialCycle(void);
	// dc array
	uint8_t dc[TLC5940_LED_N];
	// gs array
	uint16_t gs[TLC5940_LED_N];
	// new gs data flag
	volatile bool newData;
};

#define DDR(port) (*(&port-1))


// give the variables some default values
TLC5940::TLC5940(void) {
	// initialize variables at all leds off for safety and dot correction to full brightness
	for (uint8_t i=0; i<(16 * TLC5940_N); i++) {
		setDC(i, 63);
	}
	for (uint8_t i=0; i<(16 * TLC5940_N); i++) {
		setGS(i, 0);
	}

	newData = false;
}

// initialize the pins and set dot correction
void TLC5940::init(void) {
	// initialize pins
	// gsclk - output set low initially
	DDR(TLC5940_GS_PORT) |= (1 << TLC5940_GS_PIN);
	TLC5940_GS_PORT &= ~(1 << TLC5940_GS_PIN);
	// sclk - output set low
	DDR(TLC5940_SCK_PORT) |= (1 << TLC5940_SCK_PIN);
	TLC5940_SCK_PORT &= ~(1 << TLC5940_SCK_PIN);
	// xlat - output set low
	DDR(TLC5940_XLAT_PORT) |= (1 << TLC5940_XLAT_PIN);
	TLC5940_XLAT_PORT &= ~(1 << TLC5940_XLAT_PIN);
	// blank - output set high (active high pin blanks output when high)
	DDR(TLC5940_BLANK_PORT) |= (1 << TLC5940_BLANK_PIN);
	TLC5940_BLANK_PORT &= ~(1 << TLC5940_BLANK_PIN);
	// serial data MOSI - output set low
	DDR(TLC5940_MOSI_PORT) |= (1 << TLC5940_MOSI_PIN);
	TLC5940_MOSI_PORT &= ~(1 << TLC5940_MOSI_PIN);
	// programming select - output set high
	DDR(TLC5940_VPRG_PORT) |= (1 << TLC5940_VPRG_PIN);
	TLC5940_VPRG_PORT |= (1 << TLC5940_VPRG_PIN);

	// set vprg to 1 (program dc data)
	TLC5940_VPRG_PORT |= (1 << TLC5940_VPRG_PIN);
	// set serial data to high (setting dc to 1)
	TLC5940_MOSI_PORT |= (1 << TLC5940_MOSI_PIN);

	// pulse the serial clock (96 * number-of-drivers) times to write in dc data
	for (uint16_t i=0; i<(96 * TLC5940_N); i++) {
		// get the bit the tlc5940 is expecting from the gs array (tlc expects msb first)
		uint8_t data = (dc[((96 * TLC5940_N) - 1 - i)/6]) & (1 << ((96 * TLC5940_N) - 1 - i)%6);
		// set mosi if bit is high, clear if bit is low
		if (data) {
			TLC5940_MOSI_PORT |= (1 << TLC5940_MOSI_PIN);
		}
		else {
			TLC5940_MOSI_PORT &= ~(1 << TLC5940_MOSI_PIN);
		}
		TLC5940_SCK_PORT |= (1 << TLC5940_SCK_PIN);
		TLC5940_SCK_PORT &= ~(1 << TLC5940_SCK_PIN);
	}

	// pulse xlat to latch the data
	TLC5940_XLAT_PORT |= (1 << TLC5940_XLAT_PIN);
	TLC5940_XLAT_PORT &= ~(1 << TLC5940_XLAT_PIN);

	// enable leds
	TLC5940_BLANK_PORT &= ~(1 << TLC5940_BLANK_PIN);
}

// refresh the led display and data
void TLC5940::refreshGS(void) {
	bool gsFirstCycle = false;
	static bool needLatch = false;

	// disable leds before latching in new data
	TLC5940_BLANK_PORT |= (1 << TLC5940_BLANK_PIN);

	// check if vprg is still high
	if ( TLC5940_VPRG_PORT & (1 << TLC5940_VPRG_PIN) ) {
		// pull VPRG low and set the first cycle flag
		TLC5940_VPRG_PORT &= ~(1 << TLC5940_VPRG_PIN);
		gsFirstCycle = true;
	}

	// check if we need a latch
	if (needLatch) {
		needLatch = false;
		TLC5940_XLAT_PORT |= (1 << TLC5940_XLAT_PIN);
		TLC5940_XLAT_PORT &= ~(1 << TLC5940_XLAT_PIN);
	}

	// check if this was the first gs cycle after a dc cycle
	if (gsFirstCycle) {
		gsFirstCycle = false;
		// pulse serial clock once if it is (because the datasheet tells us to)
		TLC5940_SCK_PORT |= (1 << TLC5940_SCK_PIN);
		TLC5940_SCK_PORT &= ~(1 << TLC5940_SCK_PIN);
	}

	// enable leds
	TLC5940_BLANK_PORT &= ~(1 << TLC5940_BLANK_PIN);
	
	// clock in new gs data
	needLatch = serialCycle();
}


bool TLC5940::serialCycle(void) {
	// if there's data to clock in
	if (newData) {
		newData = false;
		for (uint16_t dataCount=0; dataCount<192*TLC5940_N; dataCount++) {
			// get the bit the tlc5940 is expecting from the gs array (tlc expects msb first)
			uint16_t data = (gs[((192 * TLC5940_N) - 1 - dataCount)/12]) & (1 << ((192 * TLC5940_N) - 1 - dataCount)%12);
			// set mosi if bit is high, clear if bit is low
			if (data) {
				TLC5940_MOSI_PORT |= (1 << TLC5940_MOSI_PIN);
			}
			else {
				TLC5940_MOSI_PORT &= ~(1 << TLC5940_MOSI_PIN);
			}
			// pulse the serial clock
			TLC5940_SCK_PORT |= (1 << TLC5940_SCK_PIN);
			TLC5940_SCK_PORT &= ~(1 << TLC5940_SCK_PIN);
		}
		return true;
	}
	return false;
}

// set the new data flag
void TLC5940::update(void) {
	newData = true;
}

// set the brightness of an individual led
void TLC5940::setDC(uint8_t led, uint8_t val) {
	// basic parameter checking
	// check if led is inbounds
	if (led < (16 * TLC5940_N)) {
		// if value is out of bounds, set to max
		if (val < 64) {
			dc[led] = val;
		}
		else {
			dc[led] = 63;
		}
	}
}

// set the brightness of an individual led
void TLC5940::setGS(uint8_t led, uint16_t val) {
	// basic parameter checking
	// check if led is inbounds
	if (led < (16 * TLC5940_N)) {
		// if value is out of bounds, set to max
		if (val < 4096) {
			gs[led] = val;
		}
		else {
			gs[led] = 4095;
		}
	}
}
// tlc object
TLC5940 tlc;
// loop counter
uint16_t count;
int8_t dir;
void setup(void) {
	dir = 1;

	// set DC to full
	for (uint8_t i=0; i<TLC5940_LED_N; i++) {
		tlc.setDC(i, 63);
	}

	// initialize the led driver
	tlc.init();

	cli();

	// user timer 1 to toggle the gs clock pin
	TCCR1A = 0;
	TCCR1B = 0;
	TCCR1C = 0;
	TIMSK1 = 0;
	// toggle OC1A (pin B1) on compare match event
	TCCR1A |= (1 << COM1A0);
	// set the top of the timer
	// PS = 1, F_CPU = 16 MHz, F_OC = F_CPU/(2 * PS * (OCR1A+1)
	// gs edge gets sent every 32*2=64 clock ticks
	OCR1A = 31;
	// put the timer in CTC mode and start timer with no prescaler
	TCCR1B |= ( (1 << WGM12) | (1 << CS10));

	// set up an isr for the serial cycle to live in
	// let it live in timer 0
	TCCR0A = 0;
	TCCR0B = 0;
	TIMSK0 = 0;
	// set waveform generation bit to put the timer into CTC mode
	TCCR0A |= (1 << WGM01);
	// set the top of the timer - want this to happen every 4096 * gs clocks = every 8192 clock ticks
	// set top to 255 for an interrupt every 256 * 1024 = 64 * 4096 clock ticks
	OCR0A = 255;
	// start the timer with a 1024 prescaler
	TCCR0B |= ( (1 << CS02) | (1 << CS00) );
	// enable the interrupt of output compare A match
	TIMSK0 |= (1 << OCIE0A);

	sei();
}

// ISR for serial data input into TLC5940
ISR(TIMER0_COMPA_vect) {
	tlc.refreshGS();
}


/* colors list:
1.full red: red(5000)
2.orange: red(3500) green (5000)
11.peach: red(3000) green(5000) blue(5000)
3.yellow: red(2500) green(4000)
4.lime green: red(2000) green(5000)
5.full green: green(5000)
6.cyan: green(5000) blue(2000)
7.light blue: green(3000) blue(5000)
8.full blue: blue(5000)
9.light violet: red(1000) green(3000) blue(5000)
10.purple: red(2000) green(2000) blue(5000)
*/
//initiate grid. change size if needed
int grid[4][4];
int grid_prev[4][4];
int wonGame = 0;

//color controls for individual leds. Pass in matrix location [i][j]
//adjust pos if matrix size isn't 4x4
void set_led_off(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,0);
	tlc.setGS(pos+1,0);
	tlc.setGS(pos+2,0);
}
void set_led_purple(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,2000);
	tlc.setGS(pos+1,2000);
	tlc.setGS(pos+2,5000);
}
void set_led_light_violet(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,1000);
	tlc.setGS(pos+1,3000);
	tlc.setGS(pos+2,5000);
}
void set_led_blue(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,0);
	tlc.setGS(pos+1,0);
	tlc.setGS(pos+2,5000);
}
void set_led_light_blue(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,0);
	tlc.setGS(pos+1,3000);
	tlc.setGS(pos+2,5000);
}
void set_led_cyan(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,0);
	tlc.setGS(pos+1,5000);
	tlc.setGS(pos+2,2000);
}
void set_led_green(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,0);
	tlc.setGS(pos+1,5000);
	tlc.setGS(pos+2,0);
}
void set_led_lime_green(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,2000);
	tlc.setGS(pos+1,5000);
	tlc.setGS(pos+2,0);
}
void set_led_yellow(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,2000);
	tlc.setGS(pos+1,4000);
	tlc.setGS(pos+2,0);
}
void set_led_peach(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,3000);
	tlc.setGS(pos+1,5000);
	tlc.setGS(pos+2,5000);
}
void set_led_orange(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,3500);
	tlc.setGS(pos+1,5000);
	tlc.setGS(pos+2,0);
}
void set_led_red(int i, int j)
{
	int pos = (i*3) + (12*j);
	tlc.setGS(pos,5000);
	tlc.setGS(pos+1,0);
	tlc.setGS(pos+2,0);
}

/* MAIN LOOP for turning var GRID into specific colors onto the physical rgb led grid.
If game is lost, pass in X as 0, 1 if game is won, anything else to continue normal round
*/
void matrix_to_rgb(bool x) {
	//if grid is full, game is lost. Turn off all leds
	if (x == 0) {
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				set_led_off(i,j);
			}
		}
		return;
	}
	//if any number on the matrix is 2048, game is won, set everything to full red
	if (x == 1) {
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				set_led_red(i,j);
			}
		}
		return;
	}
	//run through the grid to assign colors to grid locations
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (grid[i][j] == 0) {
				set_led_off(i,j);
			}
			else if (grid[i][j] == 2) {
				set_led_red(i,j);
			}
			else if (grid[i][j] == 4) {
				set_led_orange(i,j);
			}
			else if (grid[i][j] == 8) {
				set_led_peach(i,j);
			}
			else if (grid[i][j] == 16) {
				set_led_yellow(i,j);
			}
			else if (grid[i][j] == 32) {
				set_led_lime_green(i,j);
			}
			else if (grid[i][j] == 64) {
				set_led_green(i,j);
			}
			else if (grid[i][j] == 128) {
				set_led_cyan(i,j);
			}
			else if (grid[i][j] == 256) {
				set_led_light_blue(i,j);
			}
			else if (grid[i][j] == 512) {
				set_led_blue(i,j);
			}
			else if (grid[i][j] == 1024) {
				set_led_light_violet(i,j);
			}
			else if (grid[i][j] == 2048) {
				set_led_purple(i,j);
			}
		}
	}
	tlc.update();
}
int validmove = 0;
//checks if any value in the grid is 2048
//set flag validmove to 1 if true
void checkWin() {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (grid[i][j] == 2048) {
				wonGame = 1;
				return;
			}
		}
	}
}

//initialize the grid to all 0
void initializeGrid() {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			grid[i][j] = 0;
		}
	}
}

//action of pressing up/down/left/right on the grid. 
void shiftGridRight() {
	unsigned int trigger = 0;
	do {
		trigger = 0;
		for (unsigned int i = 0; i < 4; i++) {
			for (unsigned int j = 0; j < 3; j++) {
				if ((grid[i][j] != 0) && (grid[i][j+1] == 0)) {
					grid[i][j+1] = grid[i][j];
					grid[i][j] = 0;
					trigger = 1;
				}
			}
		}
	}
	while (trigger != 0);
}

void combineGridRight() {
	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 0; j < 3; j++) {
			if ((grid[i][j] == grid[i][j+1]) && (grid[i][j] != 0)) {
				grid[i][j+1] = grid[i][j+1] * 2;
				grid[i][j] = 0;
			}
		}
	}
}

void shiftGridLeft() {
	unsigned int trigger = 0;
	do {
		trigger = 0;
		for (unsigned int i = 0; i < 4; i++) {
			for (unsigned int j = 3; j > 0; j--) {
				if ((grid[i][j] != 0) && (grid[i][j-1] == 0)) {
					grid[i][j-1] = grid[i][j];
					grid[i][j] = 0;
					trigger = 1;
				}
			}
		}
	}
	while (trigger != 0);
}

void combineGridLeft() {
	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 0; j < 3; j++) {
			if ((grid[i][j] == grid[i][j+1]) && (grid[i][j] != 0)) {
				grid[i][j] = grid[i][j] * 2;
				grid[i][j+1] = 0;
			}
		}
	}
}


void shiftGridDown() {
	unsigned int trigger = 0;
	do {
		trigger = 0;
		for (unsigned int i = 0; i < 4; i++) {
			for (unsigned int j = 0; j < 3; j++) {
				if ((grid[j][i] != 0) && (grid[j+1][i] == 0)) {
					grid[j+1][i] = grid[j][i];
					grid[j][i] = 0;
					trigger = 1;
				}
			}
		}
	}
	while (trigger != 0);
}

void combineGridDown() {
	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 3; j > 0; j--) {
			if ((grid[j][i] == grid[j-1][i]) && (grid[j][i] != 0)) {
				grid[j-1][i] = grid[j-1][i] * 2;
				grid[j][i] = 0;
			}
		}
	}
}

void shiftGridUp() {
	unsigned int trigger = 0;
	do {
		trigger = 0;
		for (unsigned int i = 0; i < 4; i++) {
			for (unsigned int j = 3; j > 0; j--) {
				if ((grid[j][i] != 0) && (grid[j-1][i] == 0)) {
					grid[j-1][i] = grid[j][i];
					grid[j][i] = 0;
					trigger = 1;
				}
			}
		}
	}
	while (trigger != 0);
}

void combineGridUp() {
	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 0; j < 3; j++) {
			if ((grid[j][i] == grid[j+1][i]) && (grid[j][i] != 0)) {
				grid[j][i] = grid[j][i] * 2;
				grid[j+1][i] = 0;
			}
		}
	}
}

void pressLeft() {
	shiftGridLeft();
	combineGridLeft();
	shiftGridLeft();
}

void pressUp() {
	shiftGridUp();
	combineGridUp();
	shiftGridUp();
}

void pressDown() {
	shiftGridDown();
	combineGridDown();
	shiftGridDown();
}

void pressRight() {
	shiftGridRight();
	combineGridRight();
	shiftGridRight();
}

//random seed for generating a '2' tile. will update grid at specific location to '2'
void generateRandom() {
	int size = 0;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (grid[i][j] == 0) {
				size++;
			}
		}
	}
	int arr[size];
	size = 0;
	int location = 0;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (grid[i][j] == 0) {
				arr[size] = location;
				size++;
			}
			location++;
		}
	}
	int read = rand() % size;
	size = arr[read];
	int i = 0;
	int j = 0;
	while (size > 4) {
		size = size - 4;
		i++;
	}
	j = size;
	grid[i][j] = 2;
}

int main()
{
	//pinouts for up/down/left/right/reset controls
	DDRA = 0x00; PORTA = 0xFF;
	unsigned char buttonUp, buttonDown, buttonLeft, buttonRight, buttonReset;
	buttonUp = ~PINA & 0x01;
	buttonDown = ~PINA & 0x02;
	buttonLeft = ~PINA & 0x04;
	buttonRight = ~PINA & 0x08;
	buttonReset = ~PINA & 0x10;
	while (!buttonUp && !buttonDown && !buttonRight && !buttonLeft && !buttonReset)
	{
		buttonUp = ~PINA & 0x01;
		buttonDown = ~PINA & 0x02;
		buttonLeft = ~PINA & 0x04;
		buttonRight = ~PINA & 0x08;
		buttonReset = ~PINA & 0x10;
	}
	_delay_ms(1000);
	setup();
	//set up grid with 2 random '2's, display onto led grid
	initializeGrid();
	generateRandom();
	generateRandom();
	matrix_to_rgb(2);
	while(1) {		
		_delay_ms(50);
		buttonUp = ~PINA & 0x01;
		buttonDown = ~PINA & 0x02;
		buttonLeft = ~PINA & 0x04;
		buttonRight = ~PINA & 0x08;
		buttonReset = ~PINA & 0x10;
		_delay_ms(50);
		int size = 0;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if (grid[i][j] == 0) {
					size++;
				}
			}
		}
		if (size == 0) {
			while(!buttonReset) {
				matrix_to_rgb(0);
				buttonReset = ~PINA & 0x10;		
			}
		}
		if (buttonUp && !buttonDown && !buttonRight && !buttonLeft && !buttonReset) {
			pressUp();
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if (grid[i][j] != grid_prev[i][j]) {
						validmove = 1;
					}
				}
			}
			if (validmove == 1) {
				generateRandom();
				validmove = 0;
			}
			matrix_to_rgb(2);
		}
		else if (!buttonUp && buttonDown && !buttonRight && !buttonLeft && !buttonReset) {
			pressDown();
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if (grid[i][j] != grid_prev[i][j]) {
						validmove = 1;
					}
				}
			}
			if (validmove == 1) {
				generateRandom();
				validmove = 0;
			}
			matrix_to_rgb(2);
		}
		else if (!buttonUp && !buttonDown && buttonRight && !buttonLeft && !buttonReset) {
			pressRight();
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if (grid[i][j] != grid_prev[i][j])
					{
						validmove = 1;
					}
				}
			}
			if (validmove == 1) {
				generateRandom();
				validmove = 0;
			}
			matrix_to_rgb(2);
		}
		else if (!buttonUp && !buttonDown && !buttonRight && buttonLeft && !buttonReset) {
			pressLeft();
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if (grid[i][j] != grid_prev[i][j]) {
						validmove = 1;
					}
				}
			}
			if (validmove == 1) {
				generateRandom();
				validmove = 0;
			}
			matrix_to_rgb(2);
		}
		else if (!buttonUp && !buttonDown && !buttonRight && !buttonLeft && buttonReset) {
			initializeGrid();
			generateRandom();
			generateRandom();
			matrix_to_rgb(2);
			_delay_ms(50);
		}
		_delay_ms(50);
		while (buttonUp || buttonLeft || buttonRight || buttonDown || buttonReset) {
			buttonUp = ~PINA & 0x01;
			buttonDown = ~PINA & 0x02;
			buttonLeft = ~PINA & 0x04;
			buttonRight = ~PINA & 0x08;
			buttonReset = ~PINA & 0x10;
		}
		checkWin();
		if (wonGame == 1) {
			wonGame = 0;
			while (!buttonReset) {
				matrix_to_rgb(1);
				_delay_ms(3000);
				buttonReset = ~PINA & 0x10;
			}
		}
		_delay_ms(50);
	}
	
	return 0;
}