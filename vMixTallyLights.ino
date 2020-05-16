/*
 * MIDIUSB_test.ino
 *
 * Created: 4/6/2015 10:47:08 AM
 * Author: gurbrinder grewal
 * Modified by Arduino LLC (2015)
 */ 

#include "FastLED.h"
#include "MIDIUSB.h"

// Pixel Setup
#define NUM_LEDS 12
#define DATA_PIN 21
CRGB leds[NUM_LEDS]; // Define the array of leds

#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000


// MIDI Setup
// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel can be anything between 0-15. Typically reported to the user as 1-16.
// Third parameter is the note number (48 = middle C).
// Fourth parameter is the velocity (64 = normal, 127 = fastest).

void noteOn(byte channel, byte pitch, byte velocity) {
	midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
	MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity) {
	midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
	MidiUSB.sendMIDI(noteOff);
}

void setup() {
	Serial.begin(115200);


	FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,120);

	for (int i = 0; i < NUM_LEDS; ++i)
		leds[i] = COL_BLACK;

	FastLED.show();

}

// First parameter is the event type (0x0B = control change).
// Second parameter is the event type, combined with the channel.
// Third parameter is the control number number (0-119).
// Fourth parameter is the control value (0-127).

#define CHAN_NUM 1			// Channel number to listen to. (Note: Starts at 0, but starts from 1 in other software)

uint8_t LastEvent = 0;
uint8_t LastChan	= 0;
uint8_t LastNote	= 0;
uint8_t LastValue = 0;

void controlChange(byte channel, byte control, byte value) {
	midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
	MidiUSB.sendMIDI(event);
}

void loop() 
{
	midiEventPacket_t rx;
	do 
	{
		rx = MidiUSB.read();
		if (rx.header != 0) 
		{
			Serial.print("Received: ");
			Serial.print(rx.header, HEX);
			Serial.print("-");
			Serial.print(rx.byte1, HEX);
			Serial.print("-");
			Serial.print(rx.byte2, HEX);
			Serial.print("-");
			Serial.println(rx.byte3, HEX);

			// Remember the things
			LastEvent = rx.byte1 >> 4;
			LastChan 	= rx.byte1 && 0x0F;
			LastNote	= rx.byte2;
			LastNote	= rx.byte3;
		}
	} while (rx.header != 0);

	if (LastChan == CHAN_NUM)
	{
		if (LastNote == 0x7F)
		{
			leds[LastValue] = COL_RED;
			// Serial.println(rx.byte3, DEC);
		}
		else
			leds[LastValue] = COL_GREEN;
	}



	// Serial.println(rx.byte3, DEC);

	FastLED.show();
}
