# 2048rgb
2048 RGB Led Matrix

This is an embedded systems project of a recreation of the 2048 game onto an RGB led matrix.

The main components include: 3 x TLC5940 (daisy-chained pwm driver), 16 x RGB leds, atmega1284 microcontroller, button inputs.
![alt tag](http://i.imgur.com/zLBHbQn.jpg)
![alt tag](http://i.imgur.com/tpVZL1e.jpg)
![alt tag](http://i.imgur.com/QmSIqod.jpg)
![alt tag](http://i.imgur.com/7PBmgdt.jpg)
![alt tag](http://i.imgur.com/rG1GHR2.jpg)


 
Walkthrough of Code
```
Initializing ports on the atmega1284 to be able to use with the TLC5940
If not using the atmega1284, use this datasheet of TLC5940: http://www.ti.com/lit/ds/symlink/tlc5940.pdf to match pins for your avr
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

```


Initialize empty grid and blank led matrix
```
int grid[4][4];
int wonGame = 0;

//initialize the grid to all 0
void initializeGrid() {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			grid[i][j] = 0;
		}
	}
}
```
Generate 2 random ‘2’ tiles for the start of the game. First call matrix_to_rgb to set the the 2 ‘2’ tiles to red on the grid
```
	initializeGrid();
	generateRandom();
	generateRandom();
	matrix_to_rgb(2);
```
matrix_to_rgb() takes an int (0 for loss, 1 for win, anything else is play on)
Now the led matrix should have 2 random leds illuminated as red
Set up user input via buttons
```
while(1) {		
		_delay_ms(50);
		buttonUp = ~PINA & 0x01;
		buttonDown = ~PINA & 0x02;
		buttonLeft = ~PINA & 0x04;
		buttonRight = ~PINA & 0x08;
		buttonReset = ~PINA & 0x10;
}
```
When button press is detected and it is not reset
```
if (buttonUp && !buttonDown && !buttonRight && !buttonLeft && !buttonReset) {
			pressUp();
check if it is a valid move, if it is, generate random and update the grid
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
```
Before running another turn, check if the game is won
```
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

```
