# vMixTally
Tally light system for vMix using Arduino USB MIDI controller, transmitting tally light data to receiver modules on cameras using NRF24L01 radios


## MIDI Map

Notes
* Use midi note value to set colour of LEDs on tally lights

* Channel = 2


Data from vMix
* Preview <Input Number> Note 000-031
* Program <Input Number> Note 100-131
* Streaming


Data from BMD
 * Preview <Input Number>
 * Program <Input Number>
 * AUX 	   <Input Number>

BMD Inputs
1-8
MP 1-2
Col 1-2
FTB


Tally Light Colours
|			| 			| BMD		|			|			|
| --------- | --------- | --------- | --------- | --------- |
|			|			| No Signal	| Program	| Preview	| AUX
| vMix 		| No Signal	| NA		| Yellow	| Blue 		|
|			| Program	| Yellow	| Red		| Green 	|
|			| Preview	| Blue		| Green		| Off 		|
	


# Resources
BMD-Arduino interface
https://github.com/mathijsk/Arduino-Library-for-ATEM-Switchers 

Insanely cool company
https://www.skaarhoj.com/various/atem-arduino-case-stories/
https://www.skaarhoj.com/support/manual/