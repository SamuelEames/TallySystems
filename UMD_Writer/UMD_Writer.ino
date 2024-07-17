/* Writes UMD text on quad - grabs text from TSL3.1 UDP Multicast data




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
//////////////////// DEBUG SETUP ////////////////////
#define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUG
  #define DPRINT(...)   Serial.print(__VA_ARGS__)   //DPRINT is a macro, debug print
  #define DPRINTF(...)   Serial.printf(__VA_ARGS__)   //DPRINT is a macro, debug print
  #define DPRINTLN(...) Serial.println(__VA_ARGS__) //DPRINTLN is a macro, debug print with new line
#else
  #define DPRINT(...)                       //now defines a blank line
  #define DPRINTF(...)                       //now defines a blank line
  #define DPRINTLN(...)                     //now defines a blank line
#endif




// INCLUDE LIBRARIES
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>


//////////////////// ETHERNET SETUP ////////////////////
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};    // MAC address of this Ethernet Shield.
EthernetUDP Udp;
IPAddress ip(10, 1, 100, 200);                   // static IP address that Arduino uses to connect to LAN.
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

// uint16_t tallyBrightness[NUM_TALLY_OUTPUTS]; // Holds brightnesses of front & back of each tally light


#define REFRESH_INTERVAL  5000            // (ms) Longest time pixels aren't updated on connected lights

// bool frontTallyON = true;               // Sets whether front tally is used or not


uint32_t lastTXTime = 0;

bool ISO_enabled = true;
uint8_t broadcastBus = 0x19;  // ID of broadcast output (for prog tallies - match to prev number)
uint8_t key1Vid = 0x1B;       // MSG Fill
uint8_t key2Vid = 0x1D;       // LYR Fill

uint8_t previewBus   = 0x1A;
// uint8_t broadcastBus = 0x40;    // (64 - MiniMe 1 Bkgnd)
uint8_t ISO_Bus = 0x77;       // ID of alternate bus to show tallies on

#define TSL_TEXT_LEN    16
#define TSL_PACKET_LEN  18

#define TALLY_OFFSET    0x80


uint8_t tslBuffer[4][16];
// uint8_t tslBuffer[] = {0x81, 0x00, 0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4c, 0x44, 0x20, 0x20, 0x20, 0x20, 0x20};


void setup() 
{
  // Initialise Serial debug
  #ifdef DEBUG
    Serial.begin(115200);         // Open comms line
    while (!Serial) ;           // Wait for serial port to be available

   DPRINTLN(F("10-4, ready for more!"));
  #endif



  Serial1.begin(38400, SERIAL_8E1);       // TSL Serial output setup (38K4, 8bit, even parity, one stop bit)


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
    // updateTallies();
    newTallyData = false;                 // Clear 'newData' flag
  }


  // // Retransmit tally pixel data if we haven't in a while
  // if (lastTXTime + REFRESH_INTERVAL < millis())
  //  updateTallies();

  if (millis() < lastTXTime)            // Millis() wrapped around - restart timer
    lastTXTime = millis();

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


  if (packetSize == TSL_PACKET_LEN)                             // Check it's the length we're expecting
  {
    // Get Tally Number
    TallyNum_lastRX = packetBuffer[0] - 0x80;       // Note starting input count from 0

    if (TallyNum_lastRX == broadcastBus)
    {
      recordUMD(0);
      writeUMD();

      // Get input number that is going to that bus
      // BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);
      BUS_InputNum = getCamNumber();

      DPRINT("Broadcast Input = ");
      DPRINTLN(BUS_InputNum);
    }

    else if (TallyNum_lastRX == key1Vid)            // Writes first three characters of layer name at end of UMD label
    {
      if (packetBuffer[1] == 0x13)          // ON
      {
        DPRINTLN("Key 1 ON");
        for (uint8_t i = 0; i < 3; ++i)
          tslBuffer[0][11+i] = packetBuffer[2+i];
      }
      else if (packetBuffer[1] == 0x10)        // OFF
      {
        DPRINTLN("Key 1 OFF");
        for (uint8_t i = 0; i < 3; ++i)
          tslBuffer[0][11+i] = 0x20;
      }

      writeUMD();
    }

    else if (TallyNum_lastRX == key2Vid)            // Writes first three characters of layer name at end of UMD label
    {
      if (packetBuffer[1] == 0x13)          // ON
      {
        DPRINTLN("Key 2 ON");
        for (uint8_t i = 0; i < 3; ++i)
          tslBuffer[0][15+i] = packetBuffer[2+i];
      }
      else if (packetBuffer[1] == 0x10)    // OFF
      {
        DPRINTLN("Key 2 OFF");
        for (uint8_t i = 0; i < 3; ++i)
          tslBuffer[0][15+i] = 0x20;
      }

      writeUMD();
    }

    // else if (ISO_enabled && TallyNum_lastRX == ISO_Bus)
    // {
    //   // Get input number that is going to that bus
    //   // BUS_InputNum = (packetBuffer[2]-0x30) * 100 + (packetBuffer[3]-0x30) * 10 + (packetBuffer[4]-0x30);
    //   BUS_InputNum = getCamNumber();

    //   DPRINT("PGM I = ");
    //   DPRINTLN(BUS_InputNum);

    //   if (BUS_InputNum < TALLY_QTY)
    //   {
    //     tallyState_RAW[2] = 1 << BUS_InputNum;

    //     // // Check if there's new data for local tallies
    //     // for (uint8_t i = 0; i < sizeof(tallyOutputs); ++i)
    //     // {
    //     //   if (BUS_InputNum == tallyOutputs[i])
    //     //   newTallyData = true;  
    //     // }
    //   }
    // }
  }

  return;
}



uint8_t getCamNumber()
{
  // Converts cam number plain text from cam buffer to int
  // Works up to three digits
  // Returns 0 if input was formatted wrong

  uint8_t camNumber = 0;

  for (uint8_t i = 0; i < 12; ++i)
  {

    // Confirm format is "CAM X" for name
    if (packetBuffer[2+i] == 'C' && packetBuffer[3+i] == 'A' && packetBuffer[4+i] == 'M')
    {
      if (packetBuffer[5+i] == ' ')     // If there is a space after 'CAM', increment where we're looking
      {
        i++;
        // DPRINTLN(F("Space detected"));
      }

      if (packetBuffer[6+i] <= '9' && packetBuffer[6+i] >= '0')
      {
        if (packetBuffer[7+i] <= '9' && packetBuffer[7+i] >= '0') // 3 digit cam number
          camNumber = (packetBuffer[5+i]-0x30) * 100 + (packetBuffer[6+i]-0x30) * 10 + (packetBuffer[7+i]-0x30);
        else                                                // 2 Digit cam number
          camNumber = (packetBuffer[5+i]-0x30) * 10 + (packetBuffer[6+i]-0x30);
      }
      else                                                  // single digit cam number
        camNumber = packetBuffer[5+i]-0x30; // Using plain text input name

      DPRINT(F("CAM NUMBER = "));
      DPRINTLN(camNumber);
      return camNumber;
    }
    else
    {
      DPRINTLN(F("invalid cam input text format"));
    }
  }

  return camNumber;
}

void recordUMD(uint8_t tallyNum)
{
  // Records current tally text into given UMD Tally number

  for (uint8_t j = 2; j < TSL_PACKET_LEN; ++j)
    tslBuffer[tallyNum][j] = packetBuffer[j];

  return;
}


void writeUMD()
{
  // Writes text to UMD
  for (uint8_t i = 0; i < 4; ++i)
  {
    tslBuffer[i][0] = TALLY_OFFSET + i;       // Set tally number in first byte
    for (uint8_t j = 0; j < TSL_PACKET_LEN; ++j)
      Serial1.write(tslBuffer[i][j]); 
  }

  return;
}
