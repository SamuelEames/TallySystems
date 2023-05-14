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


TSL TALLY DATA FORMAT (TSL 3.1)

| HEADER | CONTROL BYTE | DISPLAY DATA |
----------------------------------------------------------------
  Header (1 Byte) = Display address (0-126) + 80 hex (control byte and display data will be sent )

Control (1 byte)
  bit 0 = tally 1 ( 1=on, 0=off )
  bit 1 = tally 2 ( 1=on, 0=off )
  bit 2 = tally 3 ( 1=on, 0=off )
  bit 3 = tally 4 ( 1=on, 0=off )
  
  bits 4-5 = brightness data

    bit 4 = 0, bit 5 = 0 (0 brightness)
    bit 4 = 0, bit 5 = 1 (1/7 brightness)
    bit 4 = 1, bit 5 = 0 (1/2 brightness)
    bit 4 = 1, bit 5 = 1 (full brightness)
    
    bit 6 = reserved (clear to 0)
    bit 7 = cleared to 0

Display Data (16 bytes) = 16 displayable ASCII characters in the range 20 hex to 7E hex. All 16 characters must be sent.


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

// #include "ENUMVars.h"

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
// #define MAX_TALLY_NODES   8               // Note: includes controller as a node (max nodes = 255)

RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t RF_address[] = "TALY0";

// #define RF_BUFF_LEN 6                     // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
// uint8_t radioBuf_RX[RF_BUFF_LEN];
// uint8_t radioBuf_TX[RF_BUFF_LEN];
bool newRFData = false;                   // True if new data over radio just in


//////////////////// ETHERNET SETUP ////////////////////
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};    // MAC address of this Ethernet Shield.
EthernetUDP Udp;
IPAddress ip(10, 1, 100, 15);                   // static IP address that Arduino uses to connect to LAN.
IPAddress multicastip(224, 0, 168, 168);        // the Multicast IP to listen on.

uint16_t multicastport = 8903;                  // the Multicast Port to listen on.
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];      // buffer to hold incoming packet.
uint8_t packetSize;                             // Holds size of last UDP message

uint32_t lastEthTime;                           // Time ethernet data was last received
#define TX_ETH_DELAY  20                        // (ms) delay between receving eth data and transmitting radio data 
                                                //  to give ethernet more time for receiving

//////////////////// GLOBAL VARIABLES ////////////////////


#define TALLY_QTY 16                      // Number of tally devices (counting up from 1);
bool newTallyData = false;

uint16_t tallyState_RAW[3];                   // Holds current state of all tally lights

uint8_t tallyText_RAW[16][TALLY_QTY];   // Holds label names associated with each tally on range 1-32


#define tallyBrightness 7                 // Brightness of tally lights on nodes (range 0-15)
#define REFRESH_INTERVAL  100            // (ms) retransimits tally data to nodes when this time elapses from last transmit 
                                            // Note: A transmit is executed every time a change in tally data is detected
uint32_t lastTXTime = 0;                  // Time of last transmit

bool frontTallyON = true;  

bool getTalFromName = true;           // When true, tally data is extracted from looking at the names on inputs going to the selected 'Prev' and 'Prog' busses
                                      // NOTE
                                      //      * names must be of format "CAM X" where 'X' can be a number of 1-3 digits
                                      //      * yellow tally light only works in this mode.
                                      // When false, tally data is taken from input tally data flags (the more common way to get tallies).


bool ISO_enabled = true;
uint8_t broadcastBus = 0x19;  // (25 - ME P/P BKGD) ID of broadcast output (for prog tallies - match to prev number)
uint8_t previewBus   = 0x1A;
// uint8_t broadcastBus = 0x40;    // (64 - MiniMe 1 Bkgnd)
uint8_t ISO_Bus = 0x77;       // (119) Aux MiniMe ID of alternate bus to show tallies on



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


  // for (uint8_t i = 0; i < TALLY_QTY; ++i) // clear tally light array
  //   tallyState_RAW[i] = INPUTOFF;


  // INITIALISE ETHERNET
  Ethernet.begin(mac, ip);
  Udp.beginMulticast(multicastip, multicastport);

  DPRINT(F("Starting to Listen for UDP Multicast Traffic on IP: "));
  DPRINT(multicastip);
  DPRINT(F(" Port: "));
  DPRINTLN(multicastport);


  for (uint8_t i = 0; i < TALLY_QTY; ++i)  // Initialise to 0
    tallyState_RAW[i] = 0;

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
    if (lastEthTime + TX_ETH_DELAY < millis())
    {
      TX_Tallies();         // Transmit data
      newTallyData = false;                 // Clear 'newData' flag
    }
  }


  // UPDATE NODES AT LEAST EVERY SECOND
  if (lastTXTime + REFRESH_INTERVAL < millis())
   TX_Tallies();

  if (millis() < lastTXTime)            // Millis() wrapped around - restart timer
    lastTXTime = millis();

  if (millis() < lastEthTime)
    lastEthTime = millis();

  
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

    if (tallyState_RAW[2] & (1 << i))         // Check ISO (override preview)
    {
      if (tallyState_RAW[1] & (1 << i))       // Check for ISO & Prog state
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
  uint8_t TallyNum_lastRX;                          // Holds number of last tally data received (counting up from 0)
  uint16_t bitMask;


  if (packetSize == 18)                             // Check it's the length we're expecting
  {
    // Get Tally Number
    TallyNum_lastRX = packetBuffer[0] - 0x80;       // Note starting input count from 0

    if (getTalFromName)
    {
      BUS_InputNum = getCamNumber();                // Converts plain text from output tally bus text data. Assumes format is "CAM X" (where X can be 1-3 digits)

      if (BUS_InputNum < TALLY_QTY)
      {
        //Note: tallyState_RAW rows --> [0] prev, [1] prog, [2] iso
        if (TallyNum_lastRX == previewBus)
        {
          DPRINT("Broadcast_Prev Input = ");      
          DPRINTLN(BUS_InputNum);

          tallyState_RAW[0] = 1 << BUS_InputNum;
          newTallyData = true;     
        }

        else if (TallyNum_lastRX == broadcastBus)
        {
          DPRINT("Broadcast_Prog Input = ");
          DPRINTLN(BUS_InputNum);

          tallyState_RAW[1] = 1 << BUS_InputNum;
          newTallyData = true;
        }

        else if (ISO_enabled && TallyNum_lastRX == ISO_Bus)
        {
          DPRINT("PGM I = ");
          DPRINTLN(BUS_InputNum);

          tallyState_RAW[2] = 1 << BUS_InputNum;
          newTallyData = true;
        }
      }
    }
    else
    {
      if (TallyNum_lastRX < TALLY_QTY)                // Only record data for tallies in our system
      {
        //Note: tallyState_RAW rows --> [0] prev, [1] prog, [2] iso

        bitMask = 1 << TallyNum_lastRX; 
        
        // Using actual tally data bits
        tallyState_RAW[0] = ((tallyState_RAW[0] & ~bitMask) | ((packetBuffer[1] & 0x01) << TallyNum_lastRX));          // Preview
        tallyState_RAW[1] = ((tallyState_RAW[1] & ~bitMask) | (((packetBuffer[1] >> 1) & 0x01) << TallyNum_lastRX));   // Program

        // Save Label Text
        // memcpy(tallyText_RAW[TallyNum_lastRX], packetBuffer[2], 16); // < This didn't work

        for (uint8_t i = 0; i < 16; ++i)
          tallyText_RAW[TallyNum_lastRX][i] = packetBuffer[2+i];

        newTallyData = true;
      }
    }

    lastEthTime = millis();
  }

  return;
}



uint8_t getCamNumber()
{
  // Converts cam number plain text from cam buffer to int
  // Works up to three digits
  // Returns 0 if input was formatted wrong

  uint8_t camNumber = 0;

  // Confirm format is "CAM X" for name
  if (packetBuffer[2] == 'C' && packetBuffer[3] == 'A' && packetBuffer[4] == 'M' && packetBuffer[5] == ' ')
  {
    if (packetBuffer[7] <= '9' && packetBuffer[7] >= '0')
    {
      if (packetBuffer[8] <= '9' && packetBuffer[8] >= '0') // 3 digit cam number
        camNumber = (packetBuffer[6]-0x30) * 100 + (packetBuffer[7]-0x30) * 10 + (packetBuffer[8]-0x30);
      else                                                // 2 Digit cam number
        camNumber = (packetBuffer[6]-0x30) * 10 + (packetBuffer[7]-0x30);
    }
    else                                                  // single digit cam number
      camNumber = packetBuffer[6]-0x30; // Using plain text input name
  }
  else
  {
    DPRINTLN(F("invalid cam input text format"));
  }


  return camNumber;
}

