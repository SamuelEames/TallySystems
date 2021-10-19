#include "FastLED.h"
// #include <SPI.h>
#include "RF24.h"


///////////////////////// IO /////////////////////////
#define RF_CSN_PIN		18
#define RF_CE_PIN 		19
#define LED_F_PIN 		20
#define LED_B_PIN 		21




//////////////////// RF Variables ////////////////////
#define MAX_TALLY_NODES 	9 				// Note: includes contrroller as a node
RF24 radio(RF_CE_PIN, RF_CSN_PIN);
// byte addresses[][6] = {"973126"};


uint8_t addrStartCode[] = "TALY";		// First 4 bytes of node addresses (5th byte on each is node ID - set later)
uint8_t nodeAddr[MAX_TALLY_NODES][5]; 	


#define RF_BUFF_LEN 1						// Number of bytes to transmit / receive -- Prog RGB, Prev RGB
uint8_t radioBuf_RX[RF_BUFF_LEN];
// uint8_t radioBuf_TX[RF_BUFF_LEN];
bool newRFData = false;						// True if new data over radio just in
uint8_t myID = 7;						// master = 0 - TODO update this to get value from BCD switch

// #define TALLY_NUM 	3				// tally number to respond to 2, 4, 5, 6, 3, 7
// #define TALLY_ON		0x7F 		// 'on' value of tally light

// Values used in master tally array to indicate each state
#define TALLY_OFF	0
#define TALLY_AUDI	1
#define TALLY_PREV	2
#define TALLY_PROG	3

// Front LEDs - seen by people in front of camera
#define NUM_LEDS_F 	4 // or wire
CRGB ledsFront[NUM_LEDS_F];

// Back LEDs - seen by camera operator
#define NUM_LEDS_B 	2
CRGB ledsBack[NUM_LEDS_B];

#define LED_BRIGHTNESS 10

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
	radio.openReadingPipe(1, nodeAddr[myID]); // My address to read messages in on
	radio.startListening();					// Start listening now!

	// Setup Pixel LEDs
	FastLED.addLeds<WS2812B, LED_F_PIN, GRB>(ledsFront, NUM_LEDS_F);
	FastLED.addLeds<WS2812B, LED_B_PIN, GRB>(ledsBack, NUM_LEDS_B);
	FastLED.setBrightness(LED_BRIGHTNESS);


	fill_solid(ledsFront, NUM_LEDS_F, COL_BLUE);
	fill_solid(ledsBack, NUM_LEDS_B, COL_PURPLE);

	FastLED.show();

	while (!Serial) ; 
	Serial.print("MyAddress = ");
	for (uint8_t i = 0; i < 4; ++i)
		Serial.write(nodeAddr[myID][i]);
	Serial.println(nodeAddr[myID][4]);

	// delay(1000);

}


void loop() 
{

	if (CheckRF())
		LightLEDs();

/*	// CheckRF();

	// //Check if data is recieved over RF
	// if ( myRadio.available()) 
	// {
	// 	//Extract data from RF Reciever
	// 	while (myRadio.available())
	// 		myRadio.read( &receivedData, sizeof(receivedData) );

	// 	LightLEDs();
	// }

	if ( radio.available()) 
	{
		//Extract data from RF Reciever
		// if (radio.available())
		radio.read( &radioBuf_RX, RF_BUFF_LEN );

		Serial.println("Got em!");

		// LightLEDs();
	}
	
	// if ( radio.available() ) 
	// {
	// 	radio.read( &dataReceived, sizeof(dataReceived) );
	// }*/


	// PrintRecDataArray();


	// static uint32_t startTime = millis();

	// if (startTime + 1000 < millis())
	// {
	// 	startTime = millis();
	// 	Serial.println("Still running");
	// }
	
}


void clearLEDs()
{
	fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
	fill_solid(ledsBack, NUM_LEDS_B, COL_BLACK);

	return;
}

// void LightLEDs()
// {
// 	if (receivedData[TALLY_NUM] == TALLY_PROG)
// 	{
// 		fill_solid(ledsFront, NUM_LEDS_F, COL_TAL_RF);
// 		fill_solid(ledsBack, NUM_LEDS_B, COL_TAL_RB);
// 	}
// 	else if (receivedData[TALLY_NUM] == TALLY_PREV)
// 	{
// 		fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
// 		fill_solid(ledsBack, NUM_LEDS_B, COL_TAL_GB);
// 	}
// 	else if (receivedData[TALLY_NUM] == TALLY_AUDI)
// 	{
// 		fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
// 		fill_solid(ledsBack, NUM_LEDS_B, COL_YELLOW);
// 	}
// 	else
// 		clearLEDs(); // all LEDs off

// 	FastLED.show();
// 	return;
// }

void LightLEDs()
{
	// Lights LEDs based off given values
	// TODO - add local brightness override for back LEDs (based off light sensor)
	fill_solid( ledsFront, NUM_LEDS_F, CRGB( radioBuf_RX[0], radioBuf_RX[1], radioBuf_RX[2]) );
	fill_solid( ledsBack,  NUM_LEDS_B, CRGB( radioBuf_RX[3], radioBuf_RX[4], radioBuf_RX[5]) );

	FastLED.show();
	return;
}




// void PrintRecDataArray()
// {
// 	if (Serial.available() > 0) 
// 	{
// 		while(Serial.available()){Serial.read();} // clear serial buffer

// 		Serial.print("DataIn Buffer = ");

// 		for (uint8_t i = 0; i < numBytes; ++i)
// 		{
// 			Serial.print(receivedData[i], HEX);
// 			Serial.print(",\t");
// 		}

// 		Serial.println();
// 	}

// 	return;
// }


bool CheckRF()
{
	// Checks for updates from controller & updates state accordingly
	bool newMessage = false;

	if (radio.available())
	{
		// Read in message
		radio.read(&radioBuf_RX, RF_BUFF_LEN);
		// radio.writeAckPayload(1, &radioBuf_TX, RF_BUFF_LEN ); 	//Note: need to re-write ack payload after each use
		newMessage = true;

		// radioBuf_TX[0] += 1;

		Serial.println("Got em!");

		// while (radio.available())
		// 	radio.read(radioBuf_RX, RF_BUFF_LEN); 				// flush ack responses

	}

	if (newMessage)
	{
		Serial.print("Message Received! = ");
		for (uint8_t i = 0; i < RF_BUFF_LEN; ++i)
		{
			Serial.print(radioBuf_RX[i], DEC);
			Serial.print("\t");
		}
		Serial.println();
	}

	return newMessage;
}
