welcome to the megamega2612 project
this was made for ECE:3360 Embedded Systems at the University of Iowa in Spring 2024

the project uses 2 ATmega328p ICs to turn a YM2612 FM synthesis IC into a 6-channel polyphonic synthesizer,
one of which is the main controller, connected to all peripheral devices except the YM2612 itself,
and one peripheral uC that receives data to be written to the YM2612 via SPI, sets the YM's control pins, 
and writes the data to the appropriate registers.

its interface consists of a rotary encoder, 2 push buttons (one on the encoder, but that is not essential for functioning),
a 16x2 HD44780 LCD, and MIDI in by way of a 6N138 optocoupler, which received by the main controller's RX pin.

the programs for both ATmegas are handled entirely by interrupts with no polling whatsoever.

inspiration for YM2612 code came from here:
https://github.com/Ryan-Marchildon/ym2612-arduino-test
and here:
https://github.com/AidanHockey5/MegaMIDI

inspiration for the MIDI code came from here (i can no longer find the github repository for this):
https://youtu.be/Zq1o3Phj4Wo?si=TP7ds2Czc5PGOIf0

the program uses Chaz Wilmot and Tristan Pawlenty's adaptation of Joerg Wensch's LCD library:
https://github.com/chazwilmot/LCD_library

sorry if i am forgetting anyone!
