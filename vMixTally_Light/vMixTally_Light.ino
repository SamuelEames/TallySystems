#include "FastLED.h"
// #include <SPI.h>
#include "RF24.h"

//RF Variables
RF24 myRadio (7, 8); // CE, CSN
byte addresses[][6] = {"973126"};


const byte numBytes = 32;
uint8_t receivedData[numBytes];
// boolean newData = false;

#define TALLY_NUM 	1				// tally number to respond to 
#define TALLY_ON		0x7F 		// 'on' value of tally light

// Pixel Setup
#define NUM_LEDS 		13
#define DATA_PIN 		21
CRGB leds[NUM_LEDS]; // Define the array of leds

#define LED_BRIGHTNESS 50

#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000


void setup() 
{
	Serial.begin(115200);

	//Initialise Reciever
	myRadio.begin(); 
	myRadio.setChannel(115); 
	myRadio.setPALevel(RF24_PA_MAX);
	myRadio.setDataRate( RF24_250KBPS ) ; 
	myRadio.openReadingPipe(1, addresses[0]);
	myRadio.startListening();

	// Setup Pixel LEDs
	FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
	FastLED.setBrightness(LED_BRIGHTNESS);
	clearLEDs();
	FastLED.show();

}


void loop() 
{

	//Check if data is recieved over RF
	if ( myRadio.available()) 
	{
		//Extract data from RF Reciever
		while (myRadio.available())
			myRadio.read( &receivedData, sizeof(receivedData) );

		LightLEDs();
	}
	

	PrintRecDataArray();
	
}


void clearLEDs()
{
	for (int i = 0; i < (NUM_LEDS); ++i)
		leds[i] = CRGB::Black;

	return;
}

void LightLEDs()
{
	if (receivedData[TALLY_NUM] == TALLY_ON)
	{
		for (int i = 0; i < NUM_LEDS; ++i)
			leds[i] = COL_RED;
	}
	else
		clearLEDs();

	FastLED.show();
	return;
}


void PrintRecDataArray()
{
	if (Serial.available() > 0) 
	{
		while(Serial.available()){Serial.read();} // clear serial buffer

		Serial.print("DataIn Buffer = ");

		for (int i = 0; i < numBytes; ++i)
		{
			Serial.print(receivedData[i], HEX);
			Serial.print(",\t");
		}

		Serial.println();
	}

	return;
}

