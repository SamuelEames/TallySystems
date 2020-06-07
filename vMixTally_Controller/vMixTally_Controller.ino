#include "FastLED.h"
#include "MIDIUSB.h"
#include "RF24.h"
#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <ATEM.h>

//////////////////// RF Variables ////////////////////
RF24 myRadio (7, 8); // CE, CSN
byte addresses[][6] = {"973126"};

const byte numChars = 32;
char receivedChars[numChars];
boolean newMIDIData = false;

//////////////////// Pixel Setup ////////////////////
#define NUM_LEDS 12
#define DATA_PIN 21
CRGB leds[NUM_LEDS]; // Define the array of leds

#define LED_BRIGHTNESS 5

// Colours!
#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000

//////////////////// ATEM SETUP ////////////////////

// MAC address and IP address for this *particular* Ethernet Shield!
// MAC address is printed on the shield
// IP address is an available address you choose on your subnet where the switcher is also present:
byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0xE8, 0xE9 };		// <= SETUP
IPAddress ip(10, 10, 201, 107);        // ARDUINO IP ADDRESS


// Include ATEM library and make an instance:
#include <ATEM.h>

// Connect to an ATEM switcher on this address and using this local port:
// The port number is chosen randomly among high numbers.
ATEM AtemSwitcher(IPAddress(10, 10, 201, 101), 56417); // ATEM Switcher IP Address


//////////////////// MISC VARIABLES ////////////////////
// Used with vMix & ATEM tally data arrays
#define PROGRAM 				0
#define PREVIEW 				1
#define AUDIOON 				2

// Values used in master tally array to indicate each state
#define TALLY_OFF				0
#define TALLY_AUDI			1
#define TALLY_PREV			2
#define TALLY_PROG			3


//////////////////// PARAMETERS (EDIT AS REQUIRED) ////////////////////
#define MIDI_CHAN_NUM 	1		// Channel number to listen to. (Note: Counts from 0 here, but starts from 1 in other software)

#define NUM_VMIX_INPUTS 8		// Number of vmix inputs to allow tally for 
#define NUM_ATEM_INPUTS 8		// Number of ATEM inputs to allow tally for

// Midi starting notes
// * indicates note asociated with 'input 1' to listen to 
// * currently allows 10 inputs max (increase interval below to allow more)
#define MIDI_PROG 			0
#define MIDI_PREV 			10
#define MIDI_AUDI 			20

#define MIDI_ON					0x7F 	// 'On' value of received midi note
#define MIDI_OFF				0x00  // 'Off' value of received midi note

#define VMIX_ATEM_INUM 	1			// Input number associated with ATEM switcher (actually refers to program midi number)

//////////////////// GLOBAL VARIABLES ////////////////////

bool vMixInputState[3][NUM_VMIX_INPUTS]; 	// 0: program on, 1: preview on, 3: audio on
bool ATEMInputState[2][NUM_ATEM_INPUTS];

bool newATEMData = true;									// Data is transmitted to tally receivers on a change
bool vMixATEMChange = true; 							// Indicates change in Prev/Prog state of ATEM input within vMix

bool newExtTally = true;									// Indicates when external tally status has changed


uint8_t tallyState[NUM_ATEM_INPUTS];	// Holds current state of all tally lights



//////////////////// MIDI Setup ////////////////////
// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel can be anything between 0-15. Typically reported to the user as 1-16.
// Third parameter is the note number (48 = middle C).
// Fourth parameter is the velocity (64 = normal, 127 = fastest).

// void noteOn(byte channel, byte pitch, byte velocity) {
// 	midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
// 	MidiUSB.sendMIDI(noteOn);
// }

// void noteOff(byte channel, byte pitch, byte velocity) {
// 	midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
// 	MidiUSB.sendMIDI(noteOff);
// }

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

	for (int i = 0; i < NUM_LEDS; ++i)	// Clear LEDs
		leds[i] = COL_BLACK;

	FastLED.setBrightness(LED_BRIGHTNESS);

	FastLED.show();

	for (int i = 0; i < NUM_ATEM_INPUTS; ++i)	// clear tally light array
		tallyState[i] = TALLY_OFF;


	// Start the Ethernet, Serial (debugging) and UDP:
	Ethernet.begin(mac,ip);
	// Initialize a connection to the switcher:
	// AtemSwitcher.serialOutput(true);
	AtemSwitcher.connect();

}




// void controlChange(byte channel, byte control, byte value) {
// 	midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
// 	MidiUSB.sendMIDI(event);
// }

void loop() 
{
	AtemSwitcher.runLoop();
	GetATEMTallyState();

	midiEventPacket_t rx;
	do 
	{
		rx = MidiUSB.read();
		if (rx.header != 0) 
		{
			Serial.print("Received: ");
			Serial.print(rx.header, HEX);	// event type (0x0B = control change).
			Serial.print("-");
			Serial.print(rx.byte1, HEX);	// event type, combined with the channel.
			Serial.print("-");
			Serial.print(rx.byte2, HEX);	// Note
			Serial.print("-");
			Serial.println(rx.byte3, HEX); 	// Velocity


			// Write MIDI tally data to TallyArray
			if ((rx.byte1 & 0x0F) == MIDI_CHAN_NUM)
			{
				SetVMIXTallyState(rx.byte2, rx.byte3);
			}
			newMIDIData = true;			
		}
	} while (rx.header != 0);

	CheckvMixATEMChange();		// Check if we need to transmit tally data via radio
	UpdateEXTTallyState();		// Update tally array
	PrintTallyArray(); 				// to serial
	LightLEDs_EXTTally(); 		// light up local LEDs
	sendData();								// Send data to tally lights via radio
	
}




void LightLEDs_ATEM()
{
	// Lights local LEDs based off ATEM prog/Prev data

	// for (int i = 0; i < NUM_LEDS; ++i)
	// {
	// 	if (TallyStatus[i] == 0x7F)
	// 		leds[i] = COL_RED;
	// 	else
	// 		leds[i] = COL_BLUE;
	// }

	for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
	{
		if (ATEMInputState[PROGRAM][i])
			leds[i] = COL_RED;
		else if (ATEMInputState[PREVIEW][i])
			leds[i] = COL_GREEN;
		else
			leds[i] = COL_BLACK;

	}

	// TODO - Make LED yellow if mic live but not on program
	FastLED.show();
	
}


void LightLEDs_EXTTally()
{
	// Lights local LEDs to match external tally lights

	for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
	{
		switch (tallyState[i]) 
		{
			case TALLY_OFF:
				leds[i] = COL_BLACK;
				break;

			case TALLY_PREV:
				leds[i] = COL_GREEN;
				break;

			case TALLY_PROG:
				leds[i] = COL_RED;
				break;

			default:
				// statements
				break;
		}
	}	

	FastLED.show();

	return;
}


void sendData() 
{
	if (newExtTally == true) 
	{
		//Print Sent Data to Serial Port
		Serial.print("Sent Data: ");
		for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			Serial.print(tallyState[i], HEX);
			Serial.print(", \t");
		}

		Serial.println();

		//Sent the Data over RF
		myRadio.write(&tallyState, sizeof(tallyState));
		newExtTally = false;  
	}

	return;
}

void PrintTallyArray()
{
	if (Serial.available() > 0) 
	{
		while(Serial.available()){Serial.read();} // clear serial buffer

		Serial.print("Tally Buffer = ");

		for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			Serial.print(tallyState[i], HEX);
			Serial.print(", \t");
		}

		Serial.println();
	}

	return;
}


void UpdateEXTTallyState()
{
	// Combines tally data from ATEM & vMIX to be shown on cameras

	// Check ATEM is in Prev/Prog within vmix

	if (vMixInputState[PROGRAM][VMIX_ATEM_INUM])
	{
		for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			if (ATEMInputState[PROGRAM][i])
				tallyState[i] = TALLY_PROG;
			else if (ATEMInputState[PREVIEW][i])
				tallyState[i] = TALLY_PREV;
			else
				tallyState[i] = TALLY_OFF;
		}
	}
	else //if (vMixInputState[PREVIEW][VMIX_ATEM_INUM])
	{
		for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			if (ATEMInputState[PROGRAM][i])
				tallyState[i] = TALLY_PREV;
			else
				tallyState[i] = TALLY_OFF;
		}
	}

	// else
	// {
	// 	for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
	// 	{
	// 		tallyState[i] = TALLY_OFF;
	// 		// if (ATEMInputState[PROGRAM][i])
	// 		// 	tallyState[i] = TALLY_PREV;
	// 		// else
	// 		// 	tallyState[i] = TALLY_OFF;
	// 	}	
	// }


	// NOTE! - Uncomment above lines in function to turn off all camera tallies when ATEM input in vmix is neither prog nor prev

	return;
}


void GetATEMTallyState()
{
	// Get Tally data & note if it's changed since last time.

	for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
	{
		if (AtemSwitcher.getProgramTally(i+1) != ATEMInputState[PROGRAM][i])
		{
			ATEMInputState[PROGRAM][i] = AtemSwitcher.getProgramTally(i+1);
			newATEMData = true;

			if (ATEMInputState[PROGRAM][i])
			{
				Serial.print("Program = ");
				Serial.println(i);
			}
		}
		
		if (AtemSwitcher.getPreviewTally(i+1) != ATEMInputState[PREVIEW][i])
		{
			ATEMInputState[PREVIEW][i] = AtemSwitcher.getPreviewTally(i+1);
			newATEMData = true;

			if (ATEMInputState[PREVIEW][i])
			{
				Serial.print("Preview = ");
				Serial.println(i);
			}
		}
	}

	return;
}


void SetVMIXTallyState(uint8_t MIDI_Note, uint8_t MIDI_Value)
{
	// Writes midi data to vMix tally array

	// Also checks midi data is what we want to hear!
	bool state = false;

	// convert midi not value to bool & return if invalid value
	if (MIDI_Value == MIDI_ON)
		state = true;
	else if (MIDI_Value == MIDI_OFF)
		state = false;
	else
		return;

	
	if (MIDI_Note < NUM_VMIX_INPUTS)										// Program
		vMixInputState[PROGRAM][MIDI_Note] = state;

	else if (MIDI_Note < NUM_VMIX_INPUTS + MIDI_PREV) 	// Preview
		vMixInputState[PREVIEW][MIDI_Note - MIDI_PREV] = state;

	else if (MIDI_Note < NUM_VMIX_INPUTS + MIDI_AUDI)		// Audio on
		vMixInputState[AUDIOON][MIDI_Note - MIDI_AUDI] = state;

	else
		return;


	CheckvMixATEMChange();

	return;
}


void CheckvMixATEMChange()
{
	// Checks if prev/prog state of ATEM input within vMix has changed
	static bool lastProg = 0;
	static bool lastPrev = 0;

	if (vMixInputState[PROGRAM][VMIX_ATEM_INUM] != lastProg)
	{
		vMixATEMChange = true;
		lastProg = vMixInputState[PROGRAM][VMIX_ATEM_INUM];
	}

	if (vMixInputState[PREVIEW][VMIX_ATEM_INUM] != lastPrev)
	{
		vMixATEMChange = true;
		lastPrev = vMixInputState[PREVIEW][VMIX_ATEM_INUM];
	}

	// If on vMix prog/prev & newATEM data || vMixAtemChange
	if (vMixATEMChange || ((lastProg || lastPrev) && newATEMData))
	{
		newATEMData = false;
		vMixATEMChange = false;
		newExtTally = true;
	}

	return;
}
