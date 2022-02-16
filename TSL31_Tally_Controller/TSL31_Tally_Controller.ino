/* Tally Controller - reads TSL3.1 UDP Multicast data and broadcasts to tally lights via NRF24L01


TODO
 * Try to improve speed
      * Controller seems to be receiving everything, but tally lights aren't getting every update, so lag behind sometimes
      * Implement brightness

 * Boss level
    * Implement screen and encoder/button knob for setting paremeters
          * IP Address
          * Port Number
          * Brightness (controller LEDs, Front tally lights, back tally lights)
          * ISO Bus number to respond to



RADIO DATA FORMAT
  * Let's transmit the first 16 cameras
  * Each needs 
    * PROG                  = 2x bytes  (bit flags for cameras 0-15)
    * ISO                   = 2x bytes
    * PREV                  = 2x bytes
    * Brightness Front/back = 1x byte   (0-15 Front brightness, 0-15 Back brightness)

  * = 7 bytes total to transmit each time


  Switcher Tally Setup - notes just for me because I keep forgetting them
   * 1-tslumd_1.0
   * Multicast IP 224.0.168.168

   * 1SecUpdate   On      -- Updates one tally every (when idle) rather than dump of everything once a minute
   * ShowUMDIf    On      -- Essential for ISO feature to work
   * ShowBusName  On      -- Not essential, but handy for debugging
   * FSLBL        On      -- I have no idea what this means - it shows '(FS)' on inputs 
   * Center       Off     -- Not sure if this is critical anymore -- centers text, otherwise text is left justified
   * Transport    UDP     -- Important 
   * Port         8903    -- Important



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
#include <EthernetUdp.h>
#include "FastLED.h"
#include "RF24.h"

#include "ENUMVars.h"

///////////////////////// IO /////////////////////////
#define LED_PIN     6
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
#define MAX_TALLY_NODES   8               // Note: includes controller as a node (max nodes = 255)

RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t RF_address[] = "TALY0";

#define RF_BUFF_LEN 7                     // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
// uint8_t radioBuf_RX[RF_BUFF_LEN];
uint8_t radioBuf_TX[RF_BUFF_LEN];
bool newRFData = false;                   // True if new data over radio just in


//////////////////// ETHERNET SETUP ////////////////////
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};    // MAC address of this Ethernet Shield.
EthernetUDP Udp;
IPAddress ip(10, 1, 100, 15);                   // static IP address that Arduino uses to connect to LAN.
IPAddress multicastip(224, 0, 168, 168);        // the Multicast IP to listen on.

uint16_t multicastport = 8903;                  // the Multicast Port to listen on.
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];      // buffer to hold incoming packet.
uint8_t packetSize;                             // Holds size of last UDP message

//////////////////// GLOBAL VARIABLES ////////////////////


#define TALLY_QTY 16                      // Number of tally devices (counting up from 1);
uint8_t TallyNum_lastRX;                  // Holds number of last tally data received (counting up from 0)
bool newTallyData = false;

uint16_t tallyState_ALL[4];                   // Holds current state of all tally lights


#define tallyBrightness 7                 // Brightness of tally lights on nodes (range 0-15)
#define REFRESH_INTERVAL  200            // (ms) retransimits tally data to nodes when this time elapses from last transmit 
                                            // Note: A transmit is executed every time a change in tally data is detected
uint32_t lastTXTime = 0;                  // Time of last transmit

bool frontTallyON = true;    




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


  for (uint8_t i = 0; i < TALLY_QTY; ++i) // clear tally light array
    tallyState_ALL[i] = INPUTOFF;


  // INITIALISE ETHERNET
  Ethernet.begin(mac, ip);
  Udp.beginMulticast(multicastip, multicastport);

  DPRINT(F("Starting to Listen for UDP Multicast Traffic on IP: "));
  DPRINT(multicastip);
  DPRINT(F(" Port: "));
  DPRINTLN(multicastport);


  for (uint8_t i = 0; i < TALLY_QTY; ++i)  // Initialise to 0
    tallyState_ALL[i] = 0;

}



void loop() 
{
  // Check for UDP message
  packetSize = Udp.parsePacket();

  if(packetSize)
  {
    // UDP_InfoDump();
    Udp.read(packetBuffer,UDP_TX_PACKET_MAX_SIZE);    // read the packet into packetBufffer
    #ifdef DEBUG
      UDP_PacketDump();
    #endif

    unpackTSLTally();
  }

  if (newTallyData)
  {
    TX_Tallies();         // Transmit data
    newTallyData = false;                 // Clear 'newData' flag
  }


  // UPDATE NODES AT LEAST EVERY SECOND
  if (lastTXTime + REFRESH_INTERVAL < millis())
   TX_Tallies();

  if (millis() < lastTXTime)            // Millis() wrapped around - restart timer
    lastTXTime = millis();

  
  LightLEDs_EXTTally();     // light up local LEDs

}


void TX_Tallies()
{
  // Broadcasts tally data to tally lights
  radio.write( &tallyState_ALL, RF_BUFF_LEN, true );
  lastTXTime = millis();

  return;
}


void LightLEDs_EXTTally()
{
  // Lights local LEDs to match external tally lights

  for (uint8_t i = 1; i < NUM_LEDS; ++i)    // Tally ID 0 is master station -- skip this one
  {
    leds[i-1] = COL_BLACK;

    if (tallyState_ALL[1] & (1 << i))         // Check program
      leds[i-1] = COL_RED;
    else if (tallyState_ALL[0] & (1 << i))    // Check preview
      leds[i-1] = COL_GREEN;

    if (tallyState_ALL[2] & (1 << i))         // Check ISO (override preview)
    {
      if (tallyState_ALL[1] & (1 << i))       // Check for ISO & Prog state
        leds[i-1] = COL_ORANGE;
      else
        leds[i-1] = COL_YELLOW;               // ISO Only
    }
  } 

  FastLED.show();

  return;
}



#ifdef DEBUG
  void UDP_InfoDump()
  {
    // Prints message info to console
    Serial.println();
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remote = Udp.remoteIP();
    for (uint8_t i =0; i < 4; i++)
    {
      Serial.print(remote[i], DEC);
      if (i < 3)
        Serial.print(".");
    }
    Serial.print(", port ");
    Serial.println(Udp.remotePort());

    return;
  }


  void UDP_PacketDump()
  {
    // Prints received UDP data to console

    Serial.print("Contents:");
    Serial.println(packetBuffer);

    Serial.print("HEX = ");
    for (uint8_t i = 0; i < packetSize; ++i)
    {
      Serial.print(packetBuffer[i], HEX);
      Serial.print(" ");
    }
    
    Serial.println();

    return;
  }

#endif


void unpackTSLTally()
{
  // Gets tally data from UDP packet and writes it into RAW tally array
  uint8_t BUS_InputNum = 0;                         // Holds input number being sent to PGM I bus
  uint16_t mask;


  if (packetSize == 18)                             // Check it's the length we're expecting
  {
    // Get Tally Number
    TallyNum_lastRX = packetBuffer[0] - 0x80;       // Note starting input count from 0

    if (TallyNum_lastRX < TALLY_QTY)                // Only record data for tallies in our system
    {
      //prev, prog, iso, brightness

      mask = 1 << TallyNum_lastRX;
      
      tallyState_ALL[0] = ((tallyState_ALL[0] & ~mask) | ((packetBuffer[1] & 0x01) << TallyNum_lastRX));          // Preview
      tallyState_ALL[1] = ((tallyState_ALL[1] & ~mask) | (((packetBuffer[1] >> 1) & 0x01) << TallyNum_lastRX));   // Program

      newTallyData = true;  
    }
    else if (TallyNum_lastRX == 0x77 /*|| TallyNum_lastRX == 0x19*/)  // PGM ISO Bus () OR 'ME1 BKGD' (25)
    {
      // Get input number that is going to that bus
      BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);

      DPRINT("PGM I = ");
      DPRINTLN(BUS_InputNum);

      if (BUS_InputNum < TALLY_QTY)
      {
        if (TallyNum_lastRX == 0x77) // ISO
          tallyState_ALL[2] = 1 << BUS_InputNum;
        // else // Program
        // {
        //   tallyState_ALL[1] = 1 << BUS_InputNum;
        // }
        newTallyData = true;
      }
    }
    // else if (TallyNum_lastRX == 0x19)               // ME 1 BKGD
    // {
    //   // Get input number that is going to that bus
    //   BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);

    //   DPRINT("PGM I = ");
    //   DPRINTLN(BUS_InputNum);
    // }
  }

  return;
}


