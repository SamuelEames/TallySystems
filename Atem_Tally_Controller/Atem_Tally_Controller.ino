/* Tally Controller - reads ATEM tally data and broadcasts to tally lights via NRF24L01




RADIO DATA FORMAT
  * Let's transmit the first 16 cameras
  * Each needs 
    * PROG                  = 2x bytes  (bit flags for cameras 0-15)
    * ISO                   = 2x bytes
    * PREV                  = 2x bytes
    * Brightness Front/back = 1x byte   (0-15 Front brightness, 0-15 Back brightness)

  * = 7 bytes total to transmit each time


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


// INCLUDE LIBRARIES
#include <SPI.h>
#include <Ethernet.h>
#include <Streaming.h>
#include <MemoryFree.h>
#include <SkaarhojPgmspace.h>

#include "FastLED.h"
#include "RF24.h"


///////////////////////// IO /////////////////////////
#define LED_PIN       6
#define RF_CSN_PIN    18
#define RF_CE_PIN     19


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
RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t RF_address[] = "TALY1";
bool newRFData = false;                   // True if new data over radio just in

//////////////////// ETHERNET SETUP ////////////////////
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0D, 0x6B, 0xB9 };    // MAC address of this Arduino Ethernet Shield.

IPAddress clientIp(192, 168, 10, 245);          // IP address of the Arduino
IPAddress switcherIp(192, 168, 10, 240);        // IP address of the ATEM Switcher

// Include ATEMbase library and make an instance:
// The port number is chosen randomly among high numbers.
#include <ATEMbase.h>
#include <ATEMstd.h>
ATEMstd AtemSwitcher;


//////////////////// GLOBAL VARIABLES ////////////////////
#define TALLY_QTY 16                      // Number of tally devices (counting up from 1);
bool newTallyData = false;

uint16_t tallyState_RAW[2];               // Holds current state of all tally lights --> //[0] prev, [1] prog

#define REFRESH_INTERVAL  100            // (ms) retransimits tally data to nodes when this time elapses from last transmit 
                                            // Note: A transmit is executed every time a change in tally data is detected
uint32_t lastTXTime = 0;                  // Time of last transmit


void setup() 
{
  // INITIALISE PIXEL LEDS
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, COL_BLUE); // Clear LEDs
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();


  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) ; 
  #endif

  // INITIALISE RADIO
  radio.begin();
  radio.setChannel(108);              // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  radio.setPALevel(RF24_PA_MAX);      // Let's make this powerful... later (RF24_PA_MAX)
  radio.setDataRate( RF24_250KBPS );  // Use low bitrate to be more reliable.. I think??
  radio.setAutoAck(false);            // Don't acknowledge messages
  radio.openWritingPipe(RF_address);  // All messages sent to same address


  // INITIALISE ETHERNET
  randomSeed(analogRead(5));  // For random port selection
  
  // Start the Ethernet, Serial (debugging) and UDP:
  Ethernet.begin(mac,clientIp);

  // Initialize a connection to the switcher:
  AtemSwitcher.begin(switcherIp);
  // AtemSwitcher.serialOutput(0x80); // Uncomment to have the switcher dump heaps oACK!
  AtemSwitcher.connect();


}



void loop() 
{
  AtemSwitcher.runLoop();       // Check Atem state

  // UPDATE NODES AT LEAST EVERY SECOND
  if (lastTXTime + REFRESH_INTERVAL < millis())
  {
    getATEMTallyState();
    TX_Tallies();
  }

  if (millis() < lastTXTime)            // Millis() wrapped around - restart timer
    lastTXTime = millis();
  
  LightLEDs_EXTTally();     // light up local LEDs
}


void TX_Tallies()
{
  // Broadcasts tally data to tally lights
  radio.write( &tallyState_RAW, sizeof(tallyState_RAW), true );
  lastTXTime = millis();

  // DPRINTLN("Radio TX");

  return;
}


void LightLEDs_EXTTally()
{
  // Lights local LEDs to match external tally lights

  for (uint8_t i = 1; i < NUM_LEDS; ++i)    // Tally ID 0 is master station -- skip this one
  {
    leds[i-1] = COL_BLACK;

    if (tallyState_RAW[1] & (1 << i))         // Check program
      leds[i-1] = COL_RED;
    else if (tallyState_RAW[0] & (1 << i))    // Check preview
      leds[i-1] = COL_GREEN;
  } 

  FastLED.show();

  return;
}


bool getATEMTallyState()
{
  // Extract Atem Tally data & note if it's changed since last time.
  bool newATEMData = false;
  static uint16_t lastProgState = 0;
  static uint16_t lastPrevState = 0;

  for (uint8_t i = 0; i < TALLY_QTY; ++i)
  {
    // Record Prev State
    if (AtemSwitcher.getPreviewTally(i))
    {
      tallyState_RAW[0] |= 1 << i;    // set bit
      DPRINT("Prev = ");
      DPRINTLN(i);
    }
    else
      tallyState_RAW[0] &= ~(1 << i); // clear bit

    // Record Prog State
    if (AtemSwitcher.getProgramTally(i))
    {
      tallyState_RAW[1] |= 1 << i;    // set bit
      DPRINT("Prog = ");
      DPRINTLN(i);
    }
    else
      tallyState_RAW[1] &= ~(1 << i); // clear bit
  }


  // Check for changes
  if (lastProgState != tallyState_RAW[0])
    newATEMData = true;

  else if (lastPrevState != tallyState_RAW[1])
    newATEMData = true;

  // Save data to check again next time
  lastProgState = tallyState_RAW[0];
  lastPrevState = tallyState_RAW[1];

  return newATEMData;
}
