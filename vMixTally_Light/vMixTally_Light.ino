/* Tally Light


TODO 
  * Add pixel output for optional tally light up number block on top of camera
      * Or it could just use the same output and override the original one, like how headphone ports switch inhibit speakers when headphones are used
      * Be careful not to short out PSU with whatever connection is used though.

*/







// SETUP DEBUG MESSAGES
#define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUG
  #define DPRINT(...)   Serial.print(__VA_ARGS__)   //DPRINT is a macro, debug print
  #define DPRINTLN(...) Serial.println(__VA_ARGS__) //DPRINTLN is a macro, debug print with new line
#else
  #define DPRINT(...)                       //now defines a blank line
  #define DPRINTLN(...)                     //now defines a blank line
#endif


#include "FastLED.h"
#include "RF24.h"
#include "ENUMVars.h"


///////////////////////// IO /////////////////////////
#define RF_CSN_PIN    18
#define RF_CE_PIN     19
#define LED_F_PIN     20
#define LED_B_PIN     21


//////////////////// RF Variables ////////////////////
RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t RF_address[] = "TALY0";
// uint8_t addrStartCode[] = "TALY";   // First 4 bytes of node addresses (5th byte on each is node ID - set later)
// uint8_t nodeAddr[5];  

#define RF_BUFF_LEN 7           // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
uint8_t radioBuf_RX[RF_BUFF_LEN];
uint8_t myID = 3;               // controller/transmitter = 0 - TODO update this to get value from BCD switch - MY TALLY NUMBER (1 = cam 1, etc)


// Front LEDs - seen by people in front of camera
#define NUM_LEDS_F  4 // or wire
CRGB ledsFront[NUM_LEDS_F];

// Back LEDs - seen by camera operator
#define NUM_LEDS_B  2
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

#define COL_TAL_RF  0xFF0000
#define COL_TAL_RB  0x3F0000
#define COL_TAL_GB  0x003F00


void setup() 
{
  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) ; 
  #endif

  // GET MY ID ... from BCD switch - TODO
  // memcpy(nodeAddr, addrStartCode, 4); // Generate my node address
  // nodeAddr[4] = myID; 

  // Initialise Radio
  radio.begin();
  radio.setChannel(108);                // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  radio.setPALevel(RF24_PA_MAX);        // Let's make this powerful... later (RF24_PA_MAX)
  // radio.setDataRate(RF24_250KBPS);      // Let's make this quick
  // radio.setAutoAck(true);
  // radio.setAutoAck(0, false);              // Don't respond to messages
  // radio.enableDynamicPayloads() ;
  // radio.openReadingPipe(0, RF_address); // My address to read messages in on
  // radio.startListening();               // Start listening now!


  radio.setDataRate( RF24_250KBPS );
  radio.openReadingPipe(0, RF_address);
  radio.setAutoAck(false);
  radio.enableDynamicPayloads() ;
  radio.startListening();



  // Setup Pixel LEDs
  FastLED.addLeds<WS2812B, LED_F_PIN, GRB>(ledsFront, NUM_LEDS_F);
  FastLED.addLeds<WS2812B, LED_B_PIN, GRB>(ledsBack, NUM_LEDS_B);
  FastLED.setBrightness(LED_BRIGHTNESS);

  fill_solid(ledsFront, NUM_LEDS_F, 0x000020);
  // ledsFront[myID-1] = COL_YELLOW;
  fill_solid(ledsBack, NUM_LEDS_B, 0x200020);

  FastLED.show();

  #ifdef DEBUG
    Serial.print("MyAddress = ");
    Serial.println(myID);
    // for (uint8_t i = 0; i < 4; ++i)
    //   Serial.write(nodeAddr[i]);
    // Serial.println(nodeAddr[4]);
  #endif

}


void loop() 
{
  if (CheckRF())      // Check for new messages and update LEDs accordingly
    LightLEDs();


  // TODO - Indicate on LEDs if we haven't received a message for 10 seconds or so
      // Alternate flash back LEDs
      // Turn off front LEDs
  
}


void clearLEDs()
{
  fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
  fill_solid(ledsBack, NUM_LEDS_B, COL_BLACK);

  return;
}

void LightLEDs()
{
  uint8_t tallyState = (radioBuf_RX[0] & 0b00000011);
  bool frontTallyON  = (radioBuf_RX[0] & 0b00000100);
  uint8_t tallyBrightness = radioBuf_RX[0] >> 4;

  DPRINT(F("TallyState = "));
  DPRINTLN(tallyState);

  DPRINT(F("frontTallyON = "));
  DPRINTLN(frontTallyON);

  DPRINT(F("Brightness = "));
  DPRINTLN(tallyBrightness);

  // Light back LEDs
  if (tallyState == PROGRAM)
    fill_solid(ledsBack, NUM_LEDS_B, COL_TAL_RB);
  else if (tallyState == PREVIEW)
    fill_solid(ledsBack, NUM_LEDS_B, COL_TAL_GB);
  else if (tallyState == AUDIOON)
    fill_solid(ledsBack, NUM_LEDS_B, COL_YELLOW);
  else
    fill_solid(ledsBack, NUM_LEDS_B, COL_BLACK);


  if (frontTallyON)
  {
    if (tallyState == PROGRAM)
      fill_solid(ledsFront, NUM_LEDS_F, COL_TAL_RF);
    else
      fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
  }


  FastLED.show();
  return;
}


// void PrintRecDataArray()
// {
//  if (Serial.available() > 0) 
//  {
//    while(Serial.available()){Serial.read();} // clear serial buffer

//    Serial.print("DataIn Buffer = ");

//    for (uint8_t i = 0; i < numBytes; ++i)
//    {
//      Serial.print(receivedData[i], HEX);
//      Serial.print(",\t");
//    }

//    Serial.println();
//  }

//  return;
// }


bool CheckRF()
{
  // Checks for updates from controller & updates state accordingly
  bool newMessage = false;

  if (radio.available())    // Read in message
  {
    radio.read(&radioBuf_RX, RF_BUFF_LEN);
    newMessage = true;
  }

  #ifdef DEBUG
    if (newMessage)
    {
      Serial.print("Message Received! = ");
      for (uint8_t i = 0; i < RF_BUFF_LEN; ++i)
      {
        Serial.print(radioBuf_RX[i], BIN);
        Serial.print("\t");
      }
      Serial.println();
    }
  #endif

  return newMessage;
}
