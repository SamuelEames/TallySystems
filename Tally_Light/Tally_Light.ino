/* Tally Light


TODO 
  * Add pixel output for optional tally light up number block on top of camera
      * Or it could just use the same output and override the original one, like how headphone ports switch inhibit speakers when headphones are used
      * Be careful not to short out PSU with whatever connection is used though.

*/







// SETUP DEBUG MESSAGES
// #define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
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

uint8_t RF_address[] = "TALY1";

// #define RF_BUFF_LEN 7           // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
// uint8_t radioBuf_RX[RF_BUFF_LEN];
uint8_t myID = 3;               // controller/transmitter = 0 - TODO update this to get value from BCD switch - MY TALLY NUMBER (1 = cam 1, etc)

uint16_t tallyState_RAW[3];


// Front LEDs - seen by people in front of camera
#define NUM_LEDS_F  4 // or wire
CRGB ledsFront[NUM_LEDS_F];

// Back LEDs - seen by camera operator
#define NUM_LEDS_B  2
CRGB ledsBack[NUM_LEDS_B];

#define LED_BRIGHTNESS 8

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

/*  // Initialise Radio
  radio.begin();
  // radio.setChannel(108);                // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  // radio.setPALevel(RF24_PA_MAX);        // Let's make this powerful... later (RF24_PA_MAX)
  // radio.setDataRate(RF24_250KBPS);      // Let's make this quick
  // radio.setAutoAck(true);
  // radio.setAutoAck(0, false);              // Don't respond to messages
  // radio.enableDynamicPayloads() ;
  // radio.openReadingPipe(0, RF_address); // My address to read messages in on
  // radio.startListening();               // Start listening now!


  radio.setDataRate( RF24_250KBPS );
  radio.openReadingPipe(0, RF_address);
  // radio.setAutoAck(false);
  // radio.enableDynamicPayloads() ;
  radio.startListening();*/

  radio.begin();
  radio.setChannel(108);              // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  radio.setPALevel(RF24_PA_MAX);      // Let's make this powerful... later (RF24_PA_MAX)
  radio.setDataRate( RF24_250KBPS );  // Use low bitrate to be more reliable.. I think??
  radio.setAutoAck(false);            // Don't acknowledge messages
  radio.openReadingPipe(0, RF_address); // All messages read on port 0
  radio.startListening();             



  // Setup Pixel LEDs
  FastLED.addLeds<WS2812B, LED_F_PIN, GRB>(ledsFront, NUM_LEDS_F);
  FastLED.addLeds<WS2812B, LED_B_PIN, GRB>(ledsBack, NUM_LEDS_B);
  FastLED.setBrightness(LED_BRIGHTNESS);

  fill_solid(ledsFront, NUM_LEDS_F, 0x000020);
  fill_solid(ledsBack, NUM_LEDS_B, 0x200020);

  FastLED.show();

  #ifdef DEBUG
    Serial.print("MyAddress = ");
    Serial.println(myID);
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
  // Lights up LEDs on tally box according to tally data
  
  // Clear LED colours
  fill_solid(ledsFront, NUM_LEDS_F, COL_BLACK);
  fill_solid(ledsBack, NUM_LEDS_B, COL_BLACK);


  if ( tallyState_RAW[0] & (1 << myID) ) // IF PREVIEW
    fill_solid(ledsBack, NUM_LEDS_B, COL_GREEN);


  if ( tallyState_RAW[1] & (1 << myID) )    // IF PROGRAM
  {
    fill_solid(ledsFront, NUM_LEDS_F, COL_RED);
    fill_solid(ledsBack, NUM_LEDS_B, COL_RED);

    // if ( tallyState_RAW[2] & (1 << myID) )  // IF ISO AS WELL
    // {
    //   for (uint8_t i = 0; i < NUM_LEDS_B/2; ++i)
    //     ledsBack[i] = COL_RED;
    // }
    // else // Just program
    // {
    //   fill_solid(ledsBack, NUM_LEDS_B, COL_RED);
    // }
  }

  if ( tallyState_RAW[2] & (1 << myID) )  // IF ISO
  {
    // If also prev or program, only light half the back LEDs yellow
    if ( (tallyState_RAW[0] & (1 << myID)) || (tallyState_RAW[1] & (1 << myID)) )
    {
      for (uint8_t i = 0; i < NUM_LEDS_B/2; ++i)
        ledsBack[i] = COL_YELLOW;
    }
    else
      fill_solid(ledsBack, NUM_LEDS_B, COL_YELLOW);
  }


  FastLED.show();
  return;
}


bool CheckRF()
{
  // Checks for updates from controller & updates state accordingly
  bool newMessage = false;


  if (radio.available())    // Read in message
  {
    DPRINT(".");
    radio.read(&tallyState_RAW, sizeof(tallyState_RAW));
    newMessage = true;
  }

  #ifdef DEBUG
    if (newMessage)
    {
      Serial.print("Message Received! = ");
      for (uint8_t i = 0; i < sizeof(tallyState_RAW)/sizeof(tallyState_RAW[0]); ++i)
      {
        Serial.print(tallyState_RAW[i], BIN);
        Serial.print("\t");
      }
      Serial.println();
    }
  #endif

  return newMessage;
}
