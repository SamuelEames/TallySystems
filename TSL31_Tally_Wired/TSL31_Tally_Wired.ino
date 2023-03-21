/* Wired Tally Lights - reads TSL3.1 UDP Multicast data and uses it for to 4 connected tally lights


TODO
 * ALL THE THINGS

 * Boss level
    * Implement screen and encoder/button knob for setting paremeters
          * IP Address
          * Port Number
          * Brightness (controller LEDs, Front tally lights, back tally lights)
          * ISO Bus number to respond to


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



TALLY PACKET FORMAT

0 - Input Number (+0x80)
1 - Tally Data
2-17 - Text



Display Address
Control
  bit 0 - Tally 1 - Preview
  bit 1 - Tally 2 - Program
  bit 2 - Tally 3 - Unused
  bit 3 - Tally 4 - Unused
  bit 4-5 - Brightness - Unused
  bit 6 - reserved
  bit 7 - cleared to 0
Display Data (16 bytes)


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

///////////////////////// IO /////////////////////////
#define TALLY_LED_1_PIN   18    // Not sure of a better way to format this that will work
#define TALLY_LED_2_PIN   19
#define TALLY_LED_3_PIN   20
#define TALLY_LED_4_PIN   21


/* Note - these pins are also used
 9 - CS for Ethernet

 Mosi, Miso, SCK for ethernet
 SCL SDA - display (not yet implemented)

*/ 



//////////////////// Pixel Setup ////////////////////
#define NUM_TALLY_OUTPUTS 4       // Maximum number of tally lights this unit can control
#define NUM_LEDS 8                // Number of LEDs per tally light (4 on front, 4 on back)

CRGB tallyLeds[NUM_TALLY_OUTPUTS][NUM_LEDS]; // Array of all LEDs

#define LED_BRIGHTNESS 10         // range: 0-255

// Colours!
#define COL_RED     0xFF0000
#define COL_ORANGE  0xFF2800
#define COL_YELLOW  0xFF8F00
#define COL_GREEN   0x00FF00
#define COL_BLUE    0x0000FF
#define COL_PURPLE  0xB600FF
#define COL_WHITE   0xFFFF7F
#define COL_BLACK   0x000000


uint8_t tallyOutputs[4] = {1,2,3,4};     // Tallies to display on each output 




//////////////////// ETHERNET SETUP ////////////////////
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};    // MAC address of this Ethernet Shield.
EthernetUDP Udp;
IPAddress ip(10, 1, 100, 199);                   // static IP address that Arduino uses to connect to LAN.
// IPAddress ip(192, 168, 1, 15);
IPAddress multicastip(224, 0, 168, 168);        // the Multicast IP to listen on.

uint16_t multicastport = 8903;                  // the Multicast Port to listen on.
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];      // buffer to hold incoming packet.
uint8_t packetSize;                             // Holds size of last UDP message

//////////////////// GLOBAL VARIABLES ////////////////////


#define TALLY_QTY 16                      // Number of tally devices (counting up from 1);
bool newTallyData = false;

uint16_t tallyState_RAW[3];              // Holds current state of tally lights for devices 1-32
                                            // [0] Preview, [1] Program, [2] ISO
uint8_t tallyText_RAW[16][TALLY_QTY];   // Holds label names associated with each tally on range 1-32

uint16_t tallyBrightness[NUM_TALLY_OUTPUTS]; // Holds brightnesses of front & back of each tally light


#define REFRESH_INTERVAL  5000            // (ms) Longest time pixels aren't updated on connected lights

bool frontTallyON = true;               // Sets whether front tally is used or not


uint32_t lastTXTime = 0;

bool ISO_enabled = true;
uint8_t broadcastBus = 0x19;  // ID of broadcast output (for prog tallies - match to prev number)
// uint8_t broadcastBus = 0x40;    // (64 - MiniMe 1 Bkgnd)
uint8_t ISO_Bus = 0x77;       // ID of alternate bus to show tallies on




void setup() 
{
  // INITIALISE PIXEL LEDS
  // Tally Pixel Setup - again, not sure how to make this tidier :(
  FastLED.addLeds<NEOPIXEL, TALLY_LED_1_PIN>(tallyLeds[0], NUM_LEDS);
  FastLED.addLeds<NEOPIXEL, TALLY_LED_2_PIN>(tallyLeds[1], NUM_LEDS);
  FastLED.addLeds<NEOPIXEL, TALLY_LED_3_PIN>(tallyLeds[2], NUM_LEDS);
  FastLED.addLeds<NEOPIXEL, TALLY_LED_4_PIN>(tallyLeds[3], NUM_LEDS);

  fillLeds(COL_BLUE);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();


  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) ; 

    DPRINTLN("Hello!");
  #endif


  // INITIALISE ETHERNET
  // Ethernet.begin(mac, ip);       // Static IP

  if (Ethernet.begin(mac) == 0)     // Try DHCP
  {
    DPRINTLN(F("Failed to configure Ethernet using DHCP")); 

    if (Ethernet.hardwareStatus() == EthernetNoHardware) 
      DPRINTLN(F("Ethernet module not connected"));

    else if (Ethernet.linkStatus() == LinkOFF)
      DPRINT(F("Ethernet cable not connected"));
  }
  else
  {
    DPRINT(F("Successfully got DHCP IP: "));
    DPRINTLN(Ethernet.localIP());
  }


  Udp.beginMulticast(multicastip, multicastport);

  DPRINT(F("Starting to Listen for UDP Multicast Traffic on IP: "));
  DPRINT(multicastip);
  DPRINT(F(" Port: "));
  DPRINTLN(multicastport);


  for (uint8_t i = 0; i < TALLY_QTY; ++i)  // Initialise tally data to 0
    tallyState_RAW[i] = 0x0000;

  // for (uint8_t i = 0; i < sizeof(tallyText_RAW); ++i)         // Honestly not sure if this works.... it did not
  //   tallyText_RAW[i] = 0;

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
    updateTallies();
    newTallyData = false;                 // Clear 'newData' flag
  }


  // Retransmit tally pixel data if we haven't in a while
  if (lastTXTime + REFRESH_INTERVAL < millis())
   updateTallies();

  if (millis() < lastTXTime)            // Millis() wrapped around - restart timer
    lastTXTime = millis();

}

void updateTallies()
{
  // Updates connected tally lights
  lastTXTime = millis();

  // Clear LED arrays
  fillLeds(COL_BLACK);

  for (uint8_t i = 0; i < sizeof(tallyOutputs); ++i)    // Tally ID 0 is master station -- skip this one
  {


    // Check program
    if (tallyState_RAW[1] & (1 << tallyOutputs[i]))
    {
      if (frontTallyON)
        fill_solid(tallyLeds[i], NUM_LEDS, COL_RED);
      else
        fill_solid(tallyLeds[i]+NUM_LEDS/2, NUM_LEDS/2, COL_RED);
    }

    // Check Preview
    else if (tallyState_RAW[0] & (1 << tallyOutputs[i]))
      fill_solid(tallyLeds[i]+NUM_LEDS/2, NUM_LEDS/2, COL_GREEN); // Only show prev on back

    // Check ISO
    if (ISO_enabled && tallyState_RAW[2] & (1 << tallyOutputs[i]))         // Check ISO (override preview)
    {
      if (tallyState_RAW[1] & (1 << tallyOutputs[i]) || tallyState_RAW[0] & (1 << tallyOutputs[i]))       // Check for ISO & Prog/Prev state
        fill_solid(tallyLeds[i]+NUM_LEDS/2, NUM_LEDS/4, COL_YELLOW);
      else
        fill_solid(tallyLeds[i]+NUM_LEDS/2, NUM_LEDS/2, COL_YELLOW);   // ISO Only
    }
  } 

  FastLED.show();

  return;
}

void fillLeds(uint32_t colour)
{
   // Fills all connected pixel LEDs to given colour
   for (uint8_t i = 0; i < NUM_TALLY_OUTPUTS; ++i)
      fill_solid(tallyLeds[i], NUM_LEDS, colour);

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
  uint8_t TallyNum_lastRX;                          // Holds number of last tally data received (counting up from 0)

  uint16_t bitMask;


  if (packetSize == 18)                             // Check it's the length we're expecting
  {
    // Get Tally Number
    TallyNum_lastRX = packetBuffer[0] - 0x80;       // Note starting input count from 0

    if (TallyNum_lastRX < TALLY_QTY)                // Only record data for tallies in our system
    {
      //[0] prev, [1] prog, [2] iso

      bitMask = 1 << TallyNum_lastRX; 
      
      tallyState_RAW[0] = ((tallyState_RAW[0] & ~bitMask) | ((packetBuffer[1] & 0x01) << TallyNum_lastRX));          // Preview
      // tallyState_RAW[1] = ((tallyState_RAW[1] & ~bitMask) | (((packetBuffer[1] >> 1) & 0x01) << TallyNum_lastRX));   // Program

      // Save Label Text
      // memcpy(tallyText_RAW[TallyNum_lastRX], packetBuffer[2], 16); // < This didn't work

      for (uint8_t i = 0; i < 16; ++i)
        tallyText_RAW[TallyNum_lastRX][i] = packetBuffer[2+i];

      // Check if there's new data for local tallies
      for (uint8_t i = 0; i < sizeof(tallyOutputs); ++i)
      {
        if (TallyNum_lastRX == tallyOutputs[i])
        newTallyData = true;  
      }
    }


    else if (TallyNum_lastRX == broadcastBus)
    {
      // Get input number that is going to that bus
      BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);

      DPRINT("Broadcast Input = ");
      DPRINTLN(BUS_InputNum);

      if (BUS_InputNum < TALLY_QTY)
      {
        tallyState_RAW[1] = 1 << BUS_InputNum;

        // Check if there's new data for local tallies
        for (uint8_t i = 0; i < sizeof(tallyOutputs); ++i)
        {
          if (BUS_InputNum == tallyOutputs[i])
          newTallyData = true;  
        }
      }
    }


    else if (ISO_enabled && TallyNum_lastRX == ISO_Bus)
    {
      // Get input number that is going to that bus
      BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);

      DPRINT("PGM I = ");
      DPRINTLN(BUS_InputNum);

      if (BUS_InputNum < TALLY_QTY)
      {
        tallyState_RAW[2] = 1 << BUS_InputNum;

        // Check if there's new data for local tallies
        for (uint8_t i = 0; i < sizeof(tallyOutputs); ++i)
        {
          if (BUS_InputNum == tallyOutputs[i])
          newTallyData = true;  
        }
      }
    }
  }

  return;
}


