#include "FastLED.h"
// #include <SPI.h>
#include "RF24.h"

//RF Variables
RF24 myRadio (19, 18); // CE, CSN
byte addresses[][6] = {"973126"};


const byte numBytes = 32;
uint8_t receivedData[numBytes];
// boolean newData = false;

#define TALLY_NUM 	3				// tally number to respond to 2, 4, 5, 6, 3, 7
// #define TALLY_ON		0x7F 		// 'on' value of tally light

// Values used in master tally array to indicate each state
#define TALLY_OFF	0
#define TALLY_AUDI	1
#define TALLY_PREV	2
#define TALLY_PROG	3

// Front LEDs - seen by people in front of camera
#define NUM_LEDS_F 	4 // or wire
#define LED_F_PIN 	20
CRGB ledsFront[NUM_LEDS_F];

// Back LEDs - seen by camera operator
#define NUM_LEDS_B 	2
#define LED_B_PIN 	21
CRGB ledsBack[NUM_LEDS_B];

#define LED_BRIGHTNESS 100

#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000

#define COL_TAL_RF	0xFF0000
#define COL_TAL_RB	0x3F0000
#define COL_TAL_GB	0x003F00


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
	FastLED.addLeds<WS2812B, LED_F_PIN, GRB>(ledsFront, NUM_LEDS_F);
	FastLED.addLeds<WS2812B, LED_B_PIN, GRB>(ledsBack, NUM_LEDS_B);
	FastLED.setBrightness(LED_BRIGHTNESS);
	// clearLEDs();
	for (int i = 0; i < (NUM_LEDS_F); ++i)
		ledsFront[i] = COL_YELLOW;

	for (int i = 0; i < (NUM_LEDS_B); ++i)
		ledsBack[i] = COL_GREEN;


	FastLED.show();
	delay(1000);

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
	for (int i = 0; i < (NUM_LEDS_F); ++i)
		ledsFront[i] = COL_BLACK;

	for (int i = 0; i < (NUM_LEDS_B); ++i)
		ledsBack[i] = COL_BLACK;

	return;
}

void LightLEDs()
{
	if (receivedData[TALLY_NUM] == TALLY_PROG)
	{
		for (int i = 0; i < NUM_LEDS_F; ++i)
			ledsFront[i] = COL_TAL_RF;

		for (int i = 0; i < NUM_LEDS_B; ++i)
			ledsBack[i] = COL_TAL_RB;
	}
	else if (receivedData[TALLY_NUM] == TALLY_PREV)
	{
		for (int i = 0; i < NUM_LEDS_F; ++i)
			ledsFront[i] = COL_BLACK;

		for (int i = 0; i < NUM_LEDS_B; ++i)
			ledsBack[i] = COL_TAL_GB;
	}
	else if (receivedData[TALLY_NUM] == TALLY_AUDI)
	{
		for (int i = 0; i < NUM_LEDS_F; ++i)
			ledsFront[i] = COL_BLACK;

		for (int i = 0; i < NUM_LEDS_B; ++i)
			ledsBack[i] = COL_YELLOW;
	}
	else
	{
		// all LEDs off
		clearLEDs();
	}

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
