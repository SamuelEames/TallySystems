/* Tally Controller - reads TSL3.1 UDP Multicast data and broadcasts to tally lights via NRF24L01


TODO
 * Change NRF24L01 to do broadcast rather than unicast
    * to improve speed of update
    * Make infinitely epandable 
 * Change to lower bitrate - to try and improve reliabality



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
#define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
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
// uint8_t addrStartCode[] = "TALY";          // First 4 bytes of node addresses (5th byte on each is node ID - set later)
// uint8_t nodeAddr[MAX_TALLY_NODES][5];  
// bool nodePresent[MAX_TALLY_NODES];         // Used to indicate whether a receiving node responded to a message or not


#define RF_BUFF_LEN 7                     // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
// uint8_t radioBuf_RX[RF_BUFF_LEN];
uint8_t radioBuf_TX[RF_BUFF_LEN];
bool newRFData = false;                   // True if new data over radio just in

// uint8_t myID = 0;                          // master = 0



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
#define REFRESH_INTERVAL  1000            // (ms) retransimits tally data to nodes when this time elapses from last transmit 
                                            // Note: A transmit is executed every time a change in tally data is detected
uint32_t lastTXTime = 0;                  // Time of last transmit

bool frontTallyON = true;    




void setup() 
{
  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) ; 
  #endif

  
  // INITIALISE RADIO

  // // Generate node addresses
  // for (uint8_t i = 0; i < MAX_TALLY_NODES; ++i)
  // {
  //  for (uint8_t j = 0; j < 4; ++j) // Set first four bytes of address to same code
  //    nodeAddr[i][j] = addrStartCode[j];

  //  nodeAddr[i][4] = i;         // Unique 5th byte according to node address
  // }

/*  radio.begin();
  // radio.setChannel(108);         // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  radio.setDataRate(RF24_250KBPS);    // Let's make this quick
  // radio.enableDynamicPayloads();
  // radio.stopListening();             // Probably don't need this line, but eh
  // radio.setAutoAck(0, false); 
  radio.setRetries(0,5);          // Smallest time between retries (delay, count)
  radio.openWritingPipe(RF_address);  // Broadcast to ALLL tallies
  // Note: writing pipe opened when we know who we want to talk to*/

  radio.begin();
  radio.setChannel(108);              // Keep out of way of common wifi frequencies = 2.4GHz + 0.108 GHz = 2.508GHz
  radio.setPALevel(RF24_PA_MAX);      // Let's make this powerful... later (RF24_PA_MAX)
  radio.setDataRate( RF24_250KBPS );  // Use low bitrate to be more reliable.. I think??
  radio.setAutoAck(false);            // Don't acknowledge messages
  // radio.setRetries(3,5); // delay, count
  radio.openWritingPipe(RF_address);  // All messages sent to same address


  // INITIALISE PIXEL LEDS
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, COL_BLACK); // Clear LEDs
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.show();

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

  // #ifdef DEBUG 
  //  PrintTallyArray();        // Print to serial on receiving serial data
  // #endif

  
  LightLEDs_EXTTally();     // light up local LEDs




}


void TX_Tallies()
{
  // Broadcasts tally data to tally lights
  if (radio.write( &tallyState_ALL, RF_BUFF_LEN, true ))
    DPRINTLN(F("TX Success\n"));
  else
    DPRINTLN(F("TX FAIL\n"));

  lastTXTime = millis();


  return;
}



// void TX_Tallies_V1(uint8_t ID)
// {
//  // Transmits tally data to tallies that need it

//  if (ID == 0) // Update ALL tallies
//  {
//    DPRINTLN(F("TX TO ALL Tallies\n"));
//    for (uint8_t i = 1; i < MAX_TALLY_NODES; ++i)
//    {
//      // if ((i == 1) || (i == 5) || (i == 6))
//      //  continue;
      
//      radio.stopListening();
//      radio.openWritingPipe(nodeAddr[i]);
//      getTXBuf(i);

//      if (radio.write( &radioBuf_TX, RF_BUFF_LEN )) 
//        nodePresent[i] = true;
//      else 
//        nodePresent[i] = false;
//    }

//    lastTXTime = millis();

//  }
//  else            // Only update given tally ID
//  {
//    DPRINT(F("TX TO TALLY #"));
//    DPRINTLN(ID, DEC);
//    radio.stopListening();
//    radio.openWritingPipe(nodeAddr[ID]);
//    getTXBuf(ID);

//    if (radio.write( &radioBuf_TX, RF_BUFF_LEN )) 
//      nodePresent[ID] = true;
//    else 
//      nodePresent[ID] = false;
//  }


//  return;
// }


void LightLEDs_EXTTally()
{
  // Lights local LEDs to match external tally lights

  for (uint8_t i = 1; i < NUM_LEDS; ++i)    // Tally ID 0 is master station -- skip this one
  {

    if (tallyState_ALL[1] & (1 << i)) // Check program
      leds[i-1] = COL_RED;
    else if (tallyState_ALL[2] & (1 << i)) // Check ISO
      leds[i-1] = COL_YELLOW;
    else if (tallyState_ALL[0] & (1 << i)) // Check preview
      leds[i-1] = COL_GREEN;
    else
      leds[i-1] = COL_BLACK;
  } 

  FastLED.show();

  return;
}




// void getTXBuf(uint8_t ID)
// {
//  // Generates data to transmit to given tally number
//  /* TALLY BYTE DATA STRUCTURE
//      Bit 1-2 --> tally state 
//              0 = PROGRAM
//              1 = PREVIEW
//              2 = AUDIOON
//              4 = INPUTOFF
//      Bit 3   --> frontTallyON
//              0 = off
//              1 = on
//      Bit 4   --> spare
//      Bit 5-8 --> Tally Brightness (maybe front tally only & set back locally from light sensor?)
//  */

//  uint8_t tempVal = 0;

//  // Fix this at some point later.. .but will need to fix light code to match
//  if (tallyState_ALL[ID] & 0x02)      // Check program
//    tempVal = 0;
//  else if (tallyState_ALL[ID] & 0x01) // Check preview
//    tempVal = 1;
//  else
//    tempVal = 3;

//  radioBuf_TX[0] = tempVal;
//  radioBuf_TX[0] |= frontTallyON << 2;      // flag for whether tally nodes should have front light on or not
//  radioBuf_TX[0] |= tallyBrightness << 4;     // Brightness of tally lights (range 0-15)

//  return;
// }


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

  void UDP_TallyInfoDump()
  {
    // Prints tally info to console


    uint8_t TallyNum = 0;
    bool TallyPrev = false;
    bool TallyProg = false;

    if (packetSize == 18)
    {
      // Get Tally Number
      TallyNum = packetBuffer[0] - 0x80;

      Serial.print("Tally Number ");
      Serial.print(TallyNum, DEC);



      // Get Preview State
      TallyPrev = packetBuffer[1] & 0x01;

      // Get Program State
      TallyProg = (packetBuffer[1] >> 1) & 0x01;

      if (TallyPrev)
        Serial.print(" PREVIEW");

      if (TallyProg)
        Serial.print(" PROGRAM");

      Serial.println();

      // Print name
      Serial.print("DisplayData = ");
      for (uint8_t i = 2; i < 18; ++i)
        Serial.write(packetBuffer[i]);

      Serial.println();
      
    }


    return;
  }

#endif




void unpackTSLTally()
{
  // Gets tally data from UDP packet and writes it into RAW tally array

  // uint8_t dataTemp = 0;

  if (packetSize == 18)                             // Check it's the length we're expecting
  {
    // Get Tally Number
    TallyNum_lastRX = packetBuffer[0] - 0x80;         // Note starting input count from 0
    // dataTemp = packetBuffer[1] & 0x03;             // Single out Prev & Program bits


    if (TallyNum_lastRX < TALLY_QTY)                // Only record data for tallies in our system
    {
      //prev, prog, iso, brightness

      uint16_t mask = 1 << TallyNum_lastRX;
      

      tallyState_ALL[0] = ((tallyState_ALL[0] & ~mask) | ((packetBuffer[1] & 0x01) << TallyNum_lastRX));
      tallyState_ALL[1] = ((tallyState_ALL[1] & ~mask) | (((packetBuffer[1] >> 1) & 0x01) << TallyNum_lastRX));

      // tallyState_ALL[TallyNum_lastRX] = packetBuffer[1];
      Serial.print("Received Tally ");
      Serial.println(TallyNum_lastRX);
      Serial.println();

      newTallyData = true;  
    }
  }

  Serial.println("tallyState_ALL");
  for (uint8_t i = 0; i < 4; ++i)
  {
    Serial.println(tallyState_ALL[i], BIN);
  }

  return;
}


