welcome to the megamega2612 project

this was made for ECE:3360 Embedded Systems at the University of Iowa in Spring 2024

the project uses 2 ATmega328p ICs to turn a YM2612 FM synthesis IC into a 6-channel polyphonic synthesizer. 
one of these ATmegas is the main controller, connected to all peripheral devices except the YM2612 itself,
and the other is a peripheral uC that receives data to be written to the YM2612 via SPI, sets the YM's control pins, 
and writes the data to the appropriate registers using 8 data lines and 7 control lines including an OCR pin for the YM's clock.

its interface consists of a rotary encoder, 2 push buttons (one on the encoder, but that is not essential for functioning),
a 16x2 HD44780 LCD, and MIDI in received by the main controller's RX pin by way of a 6N138 optocoupler.

the programs for both ATmegas are handled entirely by interrupts with no polling whatsoever.

if anyone implements this, you might find some noise on the output when SPI data is being sent.  if you're able to resolve this please let me know!

on the schematic:

- most resistors are 10k except for the following:
  - 470R: at LCD anode pin and across 6N138's pins 8 and 6
  - 220R: at YM2612 audio output and MIDI input
- all electrolytic capacitors are 10uF, all others are 0.1uF
- the diode across the 6N138's pins 2 & 3 is a 1N4148
- i used 16MHz ceramic resonators for the ATmega's clocks, but 16MHz xtals with 22pF caps could be used instead
- the LCD's contrast trimmer is 1k
- i used a 100k dual pot for volume but most values are probably fine
- IMPORTANT: audio is routed through a TDA1308 prefabricated headphone preamp module like this one between the volume pot and output jack:
  https://ifuturetech.org/product/tda1308-stereo-earphone-audio-driver-module/
- also not pictured: a LM7805 to power the system, with 0.1uF capacitors

acknowledgements:
- inspiration for YM2612 code came from here: https://github.com/Ryan-Marchildon/ym2612-arduino-test
- and here: https://github.com/AidanHockey5/MegaMIDI
- inspiration for the MIDI code came from here (i can no longer find the github repository for this): 
https://youtu.be/Zq1o3Phj4Wo?si=TP7ds2Czc5PGOIf0
- the program uses Chaz Wilmot and Tristan Pawlenty's adaptation of Joerg Wensch's LCD library: 
https://github.com/chazwilmot/LCD_library

sorry if i am forgetting anyone!
