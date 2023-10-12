/* Tally Light

11/10/2023 - started major updates!
  * TODO
    * Hex switch to select tally number
    * Light sensor to set brightness automatically
    * Button to set channel
      * Hold button while powering on, then use hex switch to select channel.
      * Channel is saved to EEPROM 10s after no changes
      * Either power cycle to get out of settings mode, OR automatically restart after the 10s

  * Changed MCU to save costs on units - using Atmega328 instead of Atmega32u4


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
#include "Adafruit_VEML7700.h"


///////////////////////// IO /////////////////////////

// ///////////////////////// Atmega32u4 
// #define RF_CSN_PIN    18
// #define RF_CE_PIN     19

// #define LED_F_PIN     20
// #define LED_B_PIN     21


///////////////////////// Atmega328 
// Radio
#define RF_CE_PIN     9     // 13 - PB1
#define RF_CSN_PIN    10    // 14 - PB2
// SCK  - 17 PB5
// MOSI - 15 PB3
// MISO - 16 PB4

// Pixels
#define LED_F_PIN     7
#define LED_B_PIN     8

// BCD Switch
#define BCD_1         14    // 26 - PC0
#define BCD_2         15    // 25 - PC1
#define BCD_4         16    // 24 - PC2
#define BCD_8         17    // 23 - PC3

// Light Sensor??
// SCL -- 19 - 28 PC5
// SDA -- 18 - 27 PC4




//////////////////// RF Variables ////////////////////
RF24 radio(RF_CE_PIN, RF_CSN_PIN);

uint8_t RF_address[] = "TALY0";

// #define RF_BUFF_LEN 7           // Number of bytes to transmit / receive -- Prog RGB, Prev RGB
// uint8_t radioBuf_RX[RF_BUFF_LEN];
// uint8_t MY_TALLY_ID = 3;               // controller/transmitter = 0 - TODO update this to get value from BCD switch - MY TALLY NUMBER (1 = cam 1, etc)
#define MY_TALLY_ID ~PINC & 0x0F    // Returns current tally number according to BCD switch


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

// Initialise Light Sensor
Adafruit_VEML7700 luxSensor = Adafruit_VEML7700();


void setup() 
{
  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) ; 
  #endif

  pinMode(BCD_1, INPUT_PULLUP);
  pinMode(BCD_2, INPUT_PULLUP);
  pinMode(BCD_4, INPUT_PULLUP);
  pinMode(BCD_8, INPUT_PULLUP);


  if (!luxSensor.begin()) 
  {
    DPRINTLN("Light sensor not found");
  }    


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
    Serial.println(MY_TALLY_ID);
  #endif

}


void loop() 
{
  static uint8_t myTallyID_last = MY_TALLY_ID;

  if (CheckRF())      // Check for new messages and update LEDs accordingly
    LightLEDs();

  // Update LEDs instantly if MY_TALLY_ID has changed
  if (MY_TALLY_ID != myTallyID_last)
  {
    LightLEDs();
    myTallyID_last = MY_TALLY_ID;
  }


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


  if ( tallyState_RAW[0] & (1 << MY_TALLY_ID) ) // IF PREVIEW
    fill_solid(ledsBack, NUM_LEDS_B, COL_GREEN);


  if ( tallyState_RAW[1] & (1 << MY_TALLY_ID) )    // IF PROGRAM
  {
    fill_solid(ledsFront, NUM_LEDS_F, COL_RED);
    fill_solid(ledsBack, NUM_LEDS_B, COL_RED);
  }

  if ( tallyState_RAW[2] & (1 << MY_TALLY_ID) )  // IF ISO
  {
    // If also prev or program, only light half the back LEDs yellow
    if ( (tallyState_RAW[0] & (1 << MY_TALLY_ID)) || (tallyState_RAW[1] & (1 << MY_TALLY_ID)) )
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
