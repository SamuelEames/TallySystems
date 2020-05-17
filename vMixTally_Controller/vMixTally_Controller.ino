/*
 * MIDIUSB_test.ino
 *
 * Created: 4/6/2015 10:47:08 AM
 * Author: gurbrinder grewal
 * Modified by Arduino LLC (2015)
 */ 

#include "FastLED.h"
#include "MIDIUSB.h"
// #include <SPI.h>
#include "RF24.h"

//RF Variables
RF24 myRadio (7, 8); // CE, CSN
byte addresses[][6] = {"973126"};

// byte Scores[8] = {0,0,0,0,0,0,0,0};
const byte numChars = 32;
char receivedChars[numChars];
boolean newData = false;

// Pixel Setup
#define NUM_LEDS 12
#define DATA_PIN 21
CRGB leds[NUM_LEDS]; // Define the array of leds

#define NUM_TALLY NUM_LEDS
#define LED_BRIGHTNESS 5

uint8_t TallyStatus[NUM_TALLY]; // Holds current status of tally lights

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

void setup() 
{
	Serial.begin(115200);

	//Setup Radio Transmitter
	myRadio.begin();  
	myRadio.setChannel(115);
	myRadio.setPALevel(RF24_PA_MAX);
	myRadio.setDataRate( RF24_250KBPS );
	myRadio.openWritingPipe( addresses[0]);

	// Setup Pixel LEDs
	FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
	// FastLED.setMaxPowerInVoltsAndMilliamps(5,120);

	for (int i = 0; i < NUM_LEDS; ++i)	// Clear LEDs
		leds[i] = COL_BLACK;

	FastLED.setBrightness(LED_BRIGHTNESS);

	FastLED.show();

	for (int i = 0; i < NUM_TALLY; ++i)	// clear tally light array
		TallyStatus[i] = 0;

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


			// Write MIDI tally data to TallyArray
			if ((rx.byte1 & 0x0F) == CHAN_NUM)
			{
				if (rx.byte2 < NUM_TALLY)
					TallyStatus[rx.byte2] = rx.byte3;
			}
			newData = true;			
		}
	} while (rx.header != 0);

	PrintTallyArray();
	LightLEDs();
	sendData();
	
}




void LightLEDs()
{

	for (int i = 0; i < NUM_LEDS; ++i)
	{
		if (TallyStatus[i] == 0x7F)
			leds[i] = COL_RED;
		else
			leds[i] = COL_GREEN;
	}

	// TODO - Make LED yellow if mic live but not on program
	FastLED.show();
	
}


void sendData() 
{
	if (newData == true) 
	{
		//Print Sent Data to Serial Port
		Serial.print("Sent Data: ");
		for (int i = 0; i < NUM_TALLY; ++i)
		{
			Serial.print(TallyStatus[i], HEX);
			Serial.print(", \t");
		}

		Serial.println();
		// Serial.println(TallyStatus);
		// Serial.println(sizeof(TallyStatus));

		//Sent the Data over RF
		myRadio.write(&TallyStatus, sizeof(TallyStatus));
		newData = false;  
	}
	return;
}

void PrintTallyArray()
{
	if (Serial.available() > 0) 
	{
		while(Serial.available()){Serial.read();} // clear serial buffer

		Serial.print("Tally Buffer = ");

		for (int i = 0; i < NUM_TALLY; ++i)
		{
			Serial.print(TallyStatus[i], HEX);
			Serial.print(", \t");
		}

		Serial.println();
	}
}

