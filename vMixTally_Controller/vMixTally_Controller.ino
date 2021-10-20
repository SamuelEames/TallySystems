// SETUP DEBUG MESSAGES
// #define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUG
	#define DPRINT(...)		Serial.print(__VA_ARGS__)		//DPRINT is a macro, debug print
	#define DPRINTLN(...)	Serial.println(__VA_ARGS__)	//DPRINTLN is a macro, debug print with new line
#else
	#define DPRINT(...)												//now defines a blank line
	#define DPRINTLN(...)											//now defines a blank line
#endif



#include "FastLED.h"
#include "MIDIUSB.h"
#include "RF24.h"
#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <ATEM.h>

#include "ENUMVars.h"


///////////////////////// IO /////////////////////////
#define LED_PIN 		6
#define RF_CSN_PIN		18
#define RF_CE_PIN 		19


//////////////////// Pixel Setup ////////////////////
#define NUM_LEDS 8
CRGB leds[NUM_LEDS]; // Define the array of leds

#define LED_BRIGHTNESS 10

// Colours!
#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000



//////////////////// RF Variables ////////////////////
#define MAX_TALLY_NODES 	9 				// Note: includes controller as a node (max nodes = 255)

RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t addrStartCode[] = "TALY";		// First 4 bytes of node addresses (5th byte on each is node ID - set later)
uint8_t nodeAddr[MAX_TALLY_NODES][5]; 	
bool nodePresent[MAX_TALLY_NODES]; 		// Used to indicate whether a receiving node responded to a message or not


#define RF_BUFF_LEN 1						// Number of bytes to transmit / receive -- Prog RGB, Prev RGB
uint8_t radioBuf_RX[RF_BUFF_LEN];
uint8_t radioBuf_TX[RF_BUFF_LEN];
bool newRFData = false;						// True if new data over radio just in

uint8_t myID = 0;						// master = 0


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

#define VMIX_ATEM_INUM 	0			// vMix Input number associated with ATEM switcher (actually refers to program midi number)

//////////////////// GLOBAL VARIABLES ////////////////////

bool vMixInputState[3][NUM_VMIX_INPUTS]; 	// 0: program on, 1: preview on, 3: audio on
bool ATEMInputState[2][NUM_ATEM_INPUTS];

#define tallyBrightness 7 					// Brightness of tally lights on nodes (range 0-15)
#define REFRESH_INTERVAL	1000 			// (ms) retransimits tally data to nodes when this time elapses from last transmit 
													// Note: A transmit is executed every time a change in tally data is detected
uint32_t lastTXTime = 0;					// Time of last transmit

bool frontTallyON = true;

uint8_t tallyState[NUM_ATEM_INPUTS];				// Holds current state of all tally lights
uint8_t tallyState_Last[NUM_ATEM_INPUTS];			// Last state of tally lights
	
#define ALL 0 									// Used when transmitting to 'ALL' tallies				


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
	#ifdef DEBUG
		Serial.begin(115200);
		while (!Serial) ; 
	#endif

	// Initialise Radio

	// Generate node addresses
	for (uint8_t i = 0; i < MAX_TALLY_NODES; ++i)
	{
		for (uint8_t j = 0; j < 4; ++j)	// Set first four bytes of address to same code
			nodeAddr[i][j] = addrStartCode[j];

		nodeAddr[i][4] = i;					// Unique 5th byte according to node address
	}

	radio.begin();
	radio.setChannel(108);					// Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
	radio.setPALevel(RF24_PA_HIGH);		// Let's make this powerful... later (RF24_PA_MAX)
	radio.setDataRate(RF24_2MBPS);		// Let's make this quick
	radio.setRetries(0,5); 					// Smallest time between retries (delay, count)
	// Note: writing pipe opened when we know who we want to talk to


	// Setup Pixel LEDs
	FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

	fill_solid(leds, NUM_LEDS, COL_BLACK); // Clear LEDs


	FastLED.setBrightness(LED_BRIGHTNESS);
	FastLED.show();

	for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)	// clear tally light array
		tallyState[i] = INPUTOFF;


	// Start the Ethernet, Serial (debugging) and UDP:
	Ethernet.begin(mac,ip);
	// Initialize a connection to the switcher:
	// AtemSwitcher.serialOutput(true); 			// Uncomment to dump serial status 
	AtemSwitcher.connect();

}




// void controlChange(byte channel, byte control, byte value) {
// 	midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
// 	MidiUSB.sendMIDI(event);
// }

void loop() 
{
	// GET TALLY INFO FROM ATEM & VMIX
	AtemSwitcher.runLoop();				// Check Atem state
	getATEMTallyState();					// Extract Atem tally data
	checkMIDI();							// Check vMix state & extract tally data
	checkVMixATEMTallyState(); 		// Check if ATEM input within vMix has changed state

	// UPDATE LOCAL TALLY ARRAYS
	UpdateEXTTallyState();	


	// IF IT'S CHANGED TRANSMIT TO EVERYONE! (ideally just to associated nodes, but eh.. maybe update this in future)

	// Check if tally data changed
	if (memcmp(tallyState_Last, tallyState, sizeof(tallyState)) != 0)
	{
		memcpy(tallyState_Last, tallyState, sizeof(tallyState)); // save data for next time
		TX_Tallies(ALL);					// Transmit data
		DPRINTLN(F("Tally data changed"));
	}


	// UPDATE NODES AT LEAST EVERY SECOND
	if (lastTXTime + REFRESH_INTERVAL < millis())
		TX_Tallies(ALL);

	if (millis() < lastTXTime) 						// Millis() wrapped around - restart timer
		lastTXTime = millis();

	#ifdef DEBUG 
		PrintTallyArray(); 				// Print to serial on receiving serial data
	#endif

	
	LightLEDs_EXTTally(); 		// light up local LEDs
}


bool checkMIDI()
{
	bool newMIDIData = false;

	// Get Midi Data
	midiEventPacket_t rx;
	do 
	{
		rx = MidiUSB.read();
		if (rx.header != 0) 
		{
			// #ifdef DEBUG
				// Serial.print(F("Received: "));
				// Serial.print(rx.header, HEX);	// event type (0x0B = control change).
				// Serial.print(F("-"));
				// Serial.print(rx.byte1, HEX);	// event type, combined with the channel.
				// Serial.print(F("-"));
				// Serial.print(rx.byte2, HEX);	// Note
				// Serial.print(F("-"));
				// Serial.println(rx.byte3, HEX); 	// Velocity
			// #endif


			// Write MIDI tally data to TallyArray
			if ((rx.byte1 & 0x0F) == MIDI_CHAN_NUM)
				getVMIXTallyState(rx.byte2, rx.byte3);

			newMIDIData = true;			
		}
	} while (rx.header != 0);

	return newMIDIData;
}




void LightLEDs_ATEM()
{
	// Lights local LEDs based off ATEM prog/Prev data
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

	for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
	{
		switch (tallyState[i]) 
		{
			case INPUTOFF:
				leds[i] = COL_BLACK;
				break;

			case PREVIEW:
				leds[i] = COL_GREEN;
				break;

			case PROGRAM:
				leds[i] = COL_RED;
				break;

			default:
				// statements
				break;
		}


		if (nodePresent[i+1]) 		// Low white glow for present nodes
			leds[i] += 0x101010; 
		else
			leds[i] %= 20; 			// Dim tally colours of absent nodes
	}	

	FastLED.show();

	return;
}


void TX_Tallies(uint8_t ID)
{
	// Transmits tally data to tallies that need it
	DPRINTLN(F("TX_Tallies"));

	if (ID == 0) // Update ALL tallies
	{
		for (uint8_t i = 1; i < MAX_TALLY_NODES; ++i)
		{
			radio.stopListening();
			radio.openWritingPipe(nodeAddr[i]);
			getTXBuf(i);

			if (radio.write( &radioBuf_TX, RF_BUFF_LEN )) 
				nodePresent[i] = true;
			else 
				nodePresent[i] = false;
		}
	}
	else   					// Only update given tally ID
	{
		radio.stopListening();
		radio.openWritingPipe(nodeAddr[ID]);
		getTXBuf(ID);

		if (radio.write( &radioBuf_TX, RF_BUFF_LEN )) 
			nodePresent[ID] = true;
		else 
			nodePresent[ID] = false;
	}

	lastTXTime = millis();

	return;
}


void getTXBuf(uint8_t tallyNum)
{
	// Generates data to transmit to given tally number
	/* TALLY BYTE DATA STRUCTURE
			Bit 1-2 --> tally state 
							0 = PROGRAM
							1 = PREVIEW
							2 = AUDIOON
							4 = INPUTOFF
			Bit 3   --> frontTallyON
							0 = off
							1 = on
			Bit 4   --> spare
			Bit 5-8 --> Tally Brightness (maybe front tally only & set back locally from light sensor?)
	*/

	radioBuf_TX[0] = tallyState[tallyNum-1];
	radioBuf_TX[0] |= frontTallyON << 2; 			// flag for whether tally nodes should have front light on or not
	radioBuf_TX[0] |= tallyBrightness << 4; 		// Brightness of tally lights (range 0-15)

	return;
}

#ifdef DEBUG
	void PrintTallyArray()
	{
		// Prints tally array to serial
		if (Serial.available() > 0) 
		{
			while(Serial.available()){Serial.read();} // clear serial buffer

			Serial.print(F("Tally Buffer = "));

			for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
			{
				Serial.print(tallyState[i], HEX);
				Serial.print(F(", \t"));
			}

			Serial.println();
		}

		return;
	}
#endif


void UpdateEXTTallyState()
{
	// Combines tally data from ATEM & vMIX to be shown on cameras

	// Check ATEM is in Prev/Prog within vmix

	if (vMixInputState[PROGRAM][VMIX_ATEM_INUM])
	{
		for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			if (ATEMInputState[PROGRAM][i])
				tallyState[i] = PROGRAM;
			else if (ATEMInputState[PREVIEW][i])
				tallyState[i] = PREVIEW;
			else
				tallyState[i] = INPUTOFF;
		}
	}
	else //if (vMixInputState[PREVIEW][VMIX_ATEM_INUM])
	{
		for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
		{
			if (ATEMInputState[PROGRAM][i])
				tallyState[i] = PREVIEW;
			else
				tallyState[i] = INPUTOFF;
		}
	}

	// else
	// {
	// 	for (int i = 0; i < NUM_ATEM_INPUTS; ++i)
	// 	{
	// 		tallyState[i] = INPUTOFF;
	// 		// if (ATEMInputState[PROGRAM][i])
	// 		// 	tallyState[i] = PREVIEW;
	// 		// else
	// 		// 	tallyState[i] = INPUTOFF;
	// 	}	
	// }


	// NOTE! - Uncomment above lines in function to turn off all camera tallies when ATEM input in vmix is neither prog nor prev

	return;
}


bool getATEMTallyState()
{
	// Extract Atem Tally data & note if it's changed since last time.
	bool newATEMData = false;

	for (uint8_t i = 0; i < NUM_ATEM_INPUTS; ++i)
	{
		if (AtemSwitcher.getProgramTally(i+1) != ATEMInputState[PROGRAM][i])
		{
			ATEMInputState[PROGRAM][i] = AtemSwitcher.getProgramTally(i+1);
			newATEMData = true;

			#ifdef DEBUG
				if (ATEMInputState[PROGRAM][i])
				{
					Serial.print(F("Program = "));
					Serial.println(i);
				}
			#endif
		}
		
		if (AtemSwitcher.getPreviewTally(i+1) != ATEMInputState[PREVIEW][i])
		{
			ATEMInputState[PREVIEW][i] = AtemSwitcher.getPreviewTally(i+1);
			newATEMData = true;

			#ifdef DEBUG
				if (ATEMInputState[PREVIEW][i])
				{
					Serial.print(F("Preview = "));
					Serial.println(i);
				}
			#endif
		}
	}

	return newATEMData;
}


void getVMIXTallyState(uint8_t MIDI_Note, uint8_t MIDI_Value)
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

	else if (MIDI_Note < NUM_VMIX_INPUTS + MIDI_PREV) 				// Preview
		vMixInputState[PREVIEW][MIDI_Note - MIDI_PREV] = state;

	else if (MIDI_Note < NUM_VMIX_INPUTS + MIDI_AUDI)				// Audio on
		vMixInputState[AUDIOON][MIDI_Note - MIDI_AUDI] = state;

	else
		return;

	return;
}


bool checkVMixATEMTallyState()
{
	// Checks if prev/prog state of ATEM input within vMix has changed
	static bool lastProg = 0;
	static bool lastPrev = 0;

	bool vMixATEMChange = false;

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


	return vMixATEMChange;
}
