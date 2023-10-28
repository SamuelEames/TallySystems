/* Tally Light

11/10/2023 - started major updates!
  * TODO
    * DONE! - Hex switch to select tally number
    * Light sensor to set brightness automatically
    * Button to set channel
      * Hold button while powering on, then use hex switch to select channel.
      * Channel is saved to EEPROM 10s after no changes
      * Either power cycle to get out of settings mode, OR automatically restart after the 10s
    * When on tally '0' show pattern on LEDs each time signal is received

  * Changed MCU to save costs on units - using Atmega328 instead of Atmega32u4


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
// #define LED_F_PIN     7
// #define LED_B_PIN     8
#define LED_PIN       8


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

#define MY_TALLY_ID ~PINC & 0x0F    // Returns current tally number according to BCD switch

uint16_t tallyState_RAW[3];         // Tally flags (1-15, 0 is unused), [0] Prev, [1] Prog, [3] ISO

#define RF_TIMEOUT  5000       // (ms) timeout period for receiving data

#define NUM_LEDS    8
CRGB leds[NUM_LEDS];

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

//////////////////// LUX SENSOR ////////////////////
Adafruit_VEML7700 veml = Adafruit_VEML7700();

#define MAX_LUX     1500          // Lux Level at which LEDs are set to their brightest
#define MIN_LED_BRIGHTNESS  5     // Minimum brightness LEDs are set to 
#define LUX_REFRESH 200           // (ms) Interval which brightness is adjusted at




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


  if (!veml.begin()) 
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
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(MIN_LED_BRIGHTNESS);

  fill_solid(leds, NUM_LEDS, 0x000020);

  FastLED.show();

  #ifdef DEBUG
    Serial.print("MyAddress = ");
    Serial.println(MY_TALLY_ID);
  #endif

}


void loop() 
{
  static uint8_t myTallyID_last = MY_TALLY_ID;
  static uint32_t lastRFTime = millis();

  if (CheckRF())      // Check for new messages and update LEDs accordingly
  {
    LightLEDs();
    lastRFTime = millis();
  }

  // Update LEDs instantly if MY_TALLY_ID has changed
  if (MY_TALLY_ID != myTallyID_last)
  {
    LightLEDs();
    myTallyID_last = MY_TALLY_ID;
  }

  // Set brightness from light sensor
  checkRoomLux();


  if (millis() < lastRFTime)               // Millis() wrapped around - restart timer
    lastRFTime = millis();

  if (lastRFTime + RF_TIMEOUT < millis())   // Show LED timeout pattern if we haven't received RF data in a while
    timeoutLEDS();
   


  // TODO - Indicate on LEDs if we haven't received a message for 10 seconds or so
      // Alternate flash back LEDs
      // Turn off front LEDs
  
}


void clearLEDs()
{
  fill_solid(leds, NUM_LEDS, COL_BLACK);

  return;
}

void LightLEDs()
{
  // Lights up LEDs on tally box according to tally data
  
  // Clear LED colours
  fill_solid(leds, NUM_LEDS, COL_BLACK);


  if ( tallyState_RAW[0] & (1 << MY_TALLY_ID) ) // IF PREVIEW
    fill_solid(leds, NUM_LEDS/2, COL_GREEN);


  if ( tallyState_RAW[1] & (1 << MY_TALLY_ID) )    // IF PROGRAM
    fill_solid(leds, NUM_LEDS, COL_RED);

  if ( tallyState_RAW[2] & (1 << MY_TALLY_ID) )  // IF ISO
  {
    // If also prev or program, only light half the back LEDs yellow
    if ( (tallyState_RAW[0] & (1 << MY_TALLY_ID)) || (tallyState_RAW[1] & (1 << MY_TALLY_ID)) )
      fill_solid(leds, NUM_LEDS/4, COL_YELLOW);
    else
      fill_solid(leds, NUM_LEDS/2, COL_YELLOW);
  }


  FastLED.show();
  return;
}

void timeoutLEDS()
{
  // Steps blue across back LEDs 
  static uint32_t lastStepTime = millis();
  static uint8_t stepNum = 0;
  const uint16_t stepTime = 500;            // time 


  if (millis() < lastStepTime)               // Millis() wrapped around - restart timer
    lastStepTime = millis();

  if (lastStepTime + stepTime < millis()) // Return if timer hasn't elapsed
   lastStepTime = millis();
  else
    return;

  clearLEDs();

  // Set one LED to blue
  leds[stepNum++] = COL_BLUE;

  if (stepNum >= NUM_LEDS/2)
    stepNum = 0;

  FastLED.show();

  return;
}

void checkRoomLux()
{
  // Sets brightness of LEDs based off Lux from sensor
  uint8_t scaledLux;
  static uint32_t lastLuxTime = millis();

  if (millis() < lastLuxTime)               // Millis() wrapped around - restart timer
    lastLuxTime = millis();

  if (lastLuxTime + LUX_REFRESH < millis()) // Return if timer hasn't elapsed
   lastLuxTime = millis();
  else
    return;

  float luxLevel = veml.readLux();

  DPRINT("RAW LUX = ");
  DPRINT(luxLevel);

  if (luxLevel >= MAX_LUX)   // Cap upper end of lux
    luxLevel = MAX_LUX;

  scaledLux = (luxLevel/(float) MAX_LUX) * 255;

  if (scaledLux < MIN_LED_BRIGHTNESS)
    scaledLux = MIN_LED_BRIGHTNESS;

  DPRINT("\tScaled LUX = ");
  DPRINTLN(scaledLux);

  FastLED.setBrightness(scaledLux);
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
