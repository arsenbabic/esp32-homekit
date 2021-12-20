/*
 * ESP2866_Homekit.ino
 *
 *  Created on: 2021-11-30
 *      Author: ababic
 *  Thanks to all the other helpful people commenting here.
 *
 * This example allows to change brightness of a connected WS2812B strip 
 * and reads additional temperature sensor data. (currently dummy readings)
 */


#include <Arduino.h>
#include <arduino_homekit_server.h>
#include <DHT.h>
#include "wifi_info.h"

#define LOG_D(fmt, ...)   printf_P(PSTR(fmt "\n") , ##__VA_ARGS__);
bool is_on = false;

#include <FastLED.h>

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define DHTPIN 18
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

#define DATA_PIN    4
//#define CLK_PIN   4
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    120 //added more LEDs
CRGB leds[NUM_LEDS];

#define BRIGHTNESS          20
#define FRAMES_PER_SECOND  120

float current_brightness =  BRIGHTNESS;

extern "C" homekit_characteristic_t cha_bright;

void setup() {
  //homekit_server_reset(); // needed when paired accessory has been unpaired and cannot be detected by iOS again
  Serial.begin(115200);
  wifi_connect(); // in wifi_info.h
  
  delay(3000); // 3 second delay for recovery
  my_homekit_setup();
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  // limit my draw to 450mA at 5v of power draw // most of the development boards have 500mA fuse on USB, so if powered through USB and 5V pin, this avoids fuse going off
   FastLED.setMaxPowerInVoltsAndMilliamps(5,250);

   dht.begin();
}


// List of patterns to cycle through.  Each is defined as a separate function below.
typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = { rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm };

uint8_t gCurrentPatternNumber = 0; // Index number of which pattern is current
uint8_t gHue = 0; // rotating "base color" used by many of the patterns
  
void loop()
{
    my_homekit_loop();
  delay(5);
}

void my_led_strip()
{
// Call the current pattern function once, updating the 'leds' array
  gPatterns[gCurrentPatternNumber]();
  
  // send the 'leds' array out to the actual LED strip
  FastLED.show();  
  // insert a delay to keep the framerate modest
  FastLED.delay(1000/FRAMES_PER_SECOND); 

  // do some periodic updates
  EVERY_N_MILLISECONDS( 20 ) { gHue++; } // slowly cycle the "base color" through the rainbow
  EVERY_N_SECONDS( 10 ) { nextPattern(); } // change patterns periodically
}

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

void nextPattern()
{
  // add one to the current pattern number, and wrap around at the end
  gCurrentPatternNumber = (gCurrentPatternNumber + 1) % ARRAY_SIZE( gPatterns);
}

void rainbow() 
{
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}

void rainbowWithGlitter() 
{
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(80);
}

void addGlitter( fract8 chanceOfGlitter) 
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::White;
  }
}

void confetti() 
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy( leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV( gHue + random8(64), 200, 255);
}

void sinelon()
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy( leds, NUM_LEDS, 20);
  int pos = beatsin16( 13, 0, NUM_LEDS-1 );
  leds[pos] += CHSV( gHue, 255, 192);
}

void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
  for( int i = 0; i < NUM_LEDS; i++) { //9948
    leds[i] = ColorFromPalette(palette, gHue+(i*2), beat-gHue+(i*10));
  }
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( leds, NUM_LEDS, 20);
  byte dothue = 0;
  for( int i = 0; i < 8; i++) {
    leds[beatsin16( i+7, 0, NUM_LEDS-1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}



//==============================
// HomeKit setup and loop
//==============================

// access your HomeKit characteristics defined in my_accessory.c

extern "C" homekit_server_config_t accessory_config;
extern "C" homekit_characteristic_t cha_on;
extern "C" homekit_characteristic_t cha_bright;
extern "C" homekit_characteristic_t cha_current_temperature;
extern "C" homekit_characteristic_t cha_current_humidity;

static uint32_t next_heap_millis = 0;
static uint32_t next_report_millis = 0;

void my_homekit_setup() {
  cha_on.setter = set_on;
  cha_bright.setter = set_bright;
  updateBrightness();
   
  arduino_homekit_setup(&accessory_config);

}

void my_homekit_loop() {
  arduino_homekit_loop(); 
  if(is_on)
  {
      my_led_strip();
    }
  else if(!is_on) //lamp - switch to off
  {
    
  }
 
 const uint32_t t = millis();
 if (t > next_report_millis) {
    // report sensor values every 5 seconds
    next_report_millis = t + 5 * 1000;
    my_homekit_report();
  }
  if (t > next_heap_millis) {
    // show heap info every 10 seconds
    next_heap_millis = t + 10 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
        ESP.getFreeHeap(), arduino_homekit_connected_clients_count());

  }
}

void my_homekit_report() {
    // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);
  
  float temperature_value = hic; // show readings from DHT22
  float humidity_value = h;
  cha_current_temperature.value.float_value = temperature_value;
  LOG_D("Current temperature: %.1f", temperature_value);
  cha_current_humidity.value.float_value = humidity_value;
  LOG_D("Current humidity: %.1f", humidity_value);
  homekit_characteristic_notify(&cha_current_temperature, cha_current_temperature.value);
  homekit_characteristic_notify(&cha_current_humidity, cha_current_humidity.value);
}

int random_value(int min, int max) {
  return min + random(max - min);
}

void set_on(const homekit_value_t v) {
    bool on = v.bool_value;
    cha_on.value.bool_value = on; //sync the value

    if(on) {
        is_on = true;
        Serial.println("On");
        updateBrightness();
        my_led_strip();
    } else  {
        is_on = false;
        Serial.println("Off");
        updateBrightness();
        my_led_strip();
    }
}

void set_bright(const homekit_value_t v) {
    Serial.println("set_bright");
    int bright = v.int_value;
    cha_bright.value.int_value = bright; //sync the value

    current_brightness = bright;
    updateBrightness();
}

void updateBrightness()
{
  if(is_on)
  {
      int b = current_brightness;
      Serial.println(b);
      FastLED.setBrightness(b);

    }
  else if(!is_on) //lamp - switch to off
  {
      Serial.println("is_on == false");
      FastLED.setBrightness(0);
  }
}
