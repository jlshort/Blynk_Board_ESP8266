/******************************************************************************
BlynkBoard_BlynkMode.ino
BlynkBoard Firmware: Blynk Demo Source
Jim Lindblom @ SparkFun Electronics
February 24, 2016
https://github.com/sparkfun/Blynk_Board_ESP8266/Firmware

This file, part of the BlynkBoard Firmware, implements all "Blynk Mode"
functions of the BlynkBoard Core Firmware. That includes managing the Blynk
connection and ~10 example experiments, which can be conducted without
reprogramming the Blynk Board.

 Supported Experiment list:
 0: RGB LED - ZeRGBa (V0, V1, V2)
 2: 5 LED - Button (D5)
 3: 0 Button - Virtual LED (V3)
      2~: 0 Button - tweet, would need to modify button interrupt handler
 4: Humidity and temperature to <strike>gauge</strike> value displays
 5: RGB control from <strike>Zebra widet</strike> Sliders
 6. Incoming serial on hardware port sends to terminal in Blynk
 7. ADC hooked to VIN pin on graph
 8. TODO: Alligator clips connected to GPIO 12(?) send an email
 9. TODO: Timer triggers relay
 10. TODO: IMU graphs to phone 

Resources:
Blynk Arduino Library: https://github.com/blynkkk/blynk-library/releases/tag/v0.3.1
SparkFun HTU21D Library: https://github.com/sparkfun/SparkFun_HTU21D_Breakout_Arduino_Library/releases/tag/V_1.1.1

License:
This is released under the MIT license (http://opensource.org/licenses/MIT).
Please see the included LICENSE.md for more information.

Development environment specifics:
Arduino IDE 1.6.7
SparkFun BlynkBoard - ESP8266
******************************************************************************/

#include <Wire.h>
#include "SparkFunHTU21D.h"
#include <SparkFunTSL2561.h>
#include "Servo.h"
HTU21D thSense;

#define BLYNK_BOARD_SDA 2
#define BLYNK_BOARD_SCL 14
bool scanI2C(uint8_t address);
void luxInit(void);
void luxUpdate(void);
void twitterUpdate(void);
void emailUpdate(void);

/****************************************** 
 *********************************************/
#define RGB_VIRTUAL V0
#define BUTTON_VIRTUAL V1
#define RED_VIRTUAL V2
#define GREEN_VIRTUAL V3
#define BLUE_VIRTUAL V4
#define TEMPERATURE_F_VIRTUAL V5
#define TEMPERATURE_C_VIRTUAL V6
#define HUMIDITY_VIRTUAL V7
#define ADC_VOLTAGE_VIRTUAL V8
#define RGB_RAINBOW_VIRTUAL V9
#define LCD_VIRTUAL V10
#define LCD_TEMPHUMID_VIRTUAL V11
#define LCD_STATS_VIRTUAL V12
#define LCD_RUNTIME_VIRTUAL V13
#define SERVO_X_VIRTUAL V14
#define SERVO_Y_VIRTUAL V15
#define SERVO_MAX_VIRTUAL V16
#define SERVO_ANGLE_VIRUTAL V17
#define LUX_VIRTUAL V18
#define LUX_RATE_VIRTUAL V19
#define ADC_BATT_VIRTUAL V20
#define SERIAL_VIRTUAL V21
#define TWEET_ENABLE_VIRTUAL V22
#define TWITTER_THRESHOLD_VIRTUAL V23
#define TWITTER_RATE_VIRTUAL V24
#define DOOR_STATE_VIRTUAL V25
#define PUSH_ENABLE_VIRTUAL V26
#define EMAIL_ENABLED_VIRTUAL V27
#define TEMP_OFFSET_VIRTUAL V28
#define RGB_STRIP_NUM_VIRTUAL V29
#define RUNTIME_VIRTUAL V30
#define RESET_VIRTUAL V31

WidgetTerminal terminal(SERIAL_VIRTUAL);

/*#define TH_UPDATE_RATE 2000
unsigned long lastTHUpdate = 0;
void thUpdate(void);*/

#define BATT_UPDATE_RATE 1000
unsigned long lastBattUpdate = 0;
void battUpdate(void);

#define SERVO_PIN 15
#define SERVO_MINIMUM 5
unsigned int servoMax = 180;
int servoX = 0;
int servoY = 0;
Servo myServo;

#define LUX_ADDRESS 0x39
bool luxPresent = false;
bool luxInitialized = false;
unsigned int luxUpdateRate = 1000;
unsigned int ms = 1000;  // Integration ("shutter") time in milliseconds
unsigned long lastLuxUpdate = 0;
SFE_TSL2561 light;
boolean gain = 0;

bool tweetEnabled = false;
unsigned long tweetUpdateRate = 60000;
unsigned long lastTweetUpdate = 0;
unsigned int moistureThreshold = 0;

String emailAddress = "";
#define EMAIL_UPDATE_RATE 60000
unsigned long lastEmailUpdate = 0;

#define RUNTIME_UPDATE_RATE 1000
unsigned long lastRunTimeUpdate = 0;

BLYNK_CONNECTED() 
{
  //! If first connect, print a message to the LCD
  Blynk.syncAll(); // Sync all virtual variables
}

//////////////////////////
// Experiment 0: zeRGBa //
// Widget(s):           //
//  - zeRGBa: Merge, V0 //
//////////////////////////
bool firstRGBWrite = true; // On startup

BLYNK_WRITE(RGB_VIRTUAL)
{
  int redParam = param[0].asInt();
  int greenParam = param[1].asInt();
  int blueParam = param[2].asInt();
  BB_DEBUG("Blynk Write RGB: " + String(redParam) + ", " + 
          String(greenParam) + ", " + String(blueParam));
  if (!firstRGBWrite)
  {
    if (!rgbSetByProject)
    {
      blinker.detach();
      rgbSetByProject = true;
    }
    for (int i=0; i<rgb.numPixels(); i++)
      rgb.setPixelColor(i, rgb.Color(redParam, greenParam, blueParam));
    rgb.show();
  }
  else
  {
    firstRGBWrite = false;
  }
}

////////////////////////////////////
// Experiment 1: Button           //
// Widget(s):                     //
//  - Button: GP5, Push or Switch //
////////////////////////////////////
// No variables required - Use GP5 directly

///////////////////////
// Experiment 2: LED //
// Widget(s):        //
//  - Button: V1     //
///////////////////////
// To update the buttonLED widget, buttonUpdate() is called through
// a pin-change interrupt.
WidgetLED buttonLED(BUTTON_VIRTUAL); // LED widget in Blynk App
void buttonUpdate(void)
{
  pinMode(BUTTON_PIN, INPUT);
  if (digitalRead(BUTTON_PIN)) // Read the physical button status
    buttonLED.off(); // Set the Blynk button widget to off
  else
    buttonLED.on(); // Set the Blynk button widget to on
}

/////////////////////////////////////
// Experiment 3: Sliders           //
// Widget(s):                      //
//  - Large Slider: GP5, D5, 0-255 //
//  - Slider: Red, V2, 0-255       //
//  - Slider: Green, V3, 0-255     //
//  - Slider: Blue, V4, 0-255      //
/////////////////////////////////////
// GP5 variables not necessary - directly controlled
//! RGB values are synced on connect, but GP5 is not.
//  Is there any way to fix that?
byte blynkRed = 0; // Keeps track of red value
byte blynkGreen = 0; // Keeps track of green value
byte blynkBlue = 0; // Keeps track of blue value

void updateBlynkRGB(void)
{
  if (!rgbSetByProject) // If the setByProject flag isn't set
  { // The LED should still be pulsing
    blinker.detach(); // Detach the rgbBlink timer
    rgbSetByProject = true; // Set the flag
  }
  // Show the new LED color:
  setRGB(rgb.Color(blynkRed, blynkGreen, blynkBlue));  
}

BLYNK_WRITE(RED_VIRTUAL)
{
  int redIn = param.asInt(); // Read the value in
  BB_DEBUG("Blynk Write red: " + String(redIn));
  redIn = constrain(redIn, 0, 255); // Keep it between 0-255
  blynkRed = redIn; // Update the global variable
  updateBlynkRGB(); // Show the color on the LED
}

BLYNK_WRITE(GREEN_VIRTUAL)
{
  int greenIn = param.asInt(); // Read the value in
  BB_DEBUG("Blynk Write green: " + String(greenIn));
  greenIn = constrain(greenIn, 0, 255); // Keep it between 0-255
  blynkGreen = greenIn; // Update the global variable
  updateBlynkRGB(); // Show the color on the LED
}

BLYNK_WRITE(BLUE_VIRTUAL)
{
  int blueIn = param.asInt(); // Read the value in
  BB_DEBUG("Blynk Write blue: " + String(blueIn));
  blueIn = constrain(blueIn, 0, 255); // Keep it between 0-255
  blynkBlue = blueIn; // Update the global variable
  updateBlynkRGB(); // Show the color on the LED
}

///////////////////////////////////////////
// Experiment 4: Values                  //
// Widget(s):                            //
//  - Value: TempF, V5, 0-1023, 1 sec    //
//  - Value: TempC, V6, 0-1023, 1 sec    //
//  - Value: Humidity, V7, 0-1023, 1 sec //
///////////////////////////////////////////
// Value ranges are ignored once values are written
// Board runs hot, subtract an offset to try to compensate:
float tempCOffset = 0; //-8.33;
BLYNK_READ(TEMPERATURE_F_VIRTUAL)
{
  float tempC = thSense.readTemperature(); // Read from the temperature sensor
  tempC += tempCOffset; // Add any offset
  float tempF = tempC * 9.0 / 5.0 + 32.0; // Convert to farenheit
  // Create a formatted string with 1 decimal point:
  Blynk.virtualWrite(TEMPERATURE_F_VIRTUAL, tempF); // Update Blynk virtual value
  BB_DEBUG("Blynk Read TempF: " + String(tempF));
}

BLYNK_READ(TEMPERATURE_C_VIRTUAL)
{
  float tempC = thSense.readTemperature(); // Read from the temperature sensor
  tempC += tempCOffset; // Add any offset
  Blynk.virtualWrite(TEMPERATURE_C_VIRTUAL, tempC); // Update Blynk virtual value
  BB_DEBUG("Blynk Read TempC: " + String(tempC));
}

BLYNK_READ(HUMIDITY_VIRTUAL)
{
  float humidity = thSense.readHumidity(); // Read from humidity sensor
  Blynk.virtualWrite(HUMIDITY_VIRTUAL, humidity); // Update Blynk virtual value
  BB_DEBUG("Blynk Read Humidity: " + String(humidity));
}

BLYNK_WRITE(TEMP_OFFSET_VIRTUAL) // Very optional virtual to set the tempC offset
{
  tempCOffset = param.asInt();
  BB_DEBUG("Blynk TempC Offset: " + String(tempCOffset));
}

/* 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5
 5 Experiment 5: Gauge           5
 5 Widget(s):                    5
 5  - Gauge: ADC0, 0-1023, 1 sec 5
 5  - Gauge: V8, 0-4, 1 sec      5
 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 */
// ADC is read directly to a gauge
// Voltage is read to V8
BLYNK_READ(ADC_VOLTAGE_VIRTUAL)
{
  float adcRaw = analogRead(A0); // Read in A0
  // Divide by 1024, then multiply by the hard-wired voltage divider max (3.2)
  float voltage = ((float)adcRaw / 1024.0) * ADC_VOLTAGE_DIVIDER;
  Blynk.virtualWrite(ADC_VOLTAGE_VIRTUAL, voltage); // Output value to Blynk
  BB_DEBUG("Blynk DC Voltage: " + String(voltage));
}

/* 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6
 6 Experiment 6: zeRGBa                6
 6 Widget(s):                          6
 6  - zeRGBa: Merge, V0                6
 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 6 */
Ticker rgbRainbowTicker; // Timer to set RGB rainbow mode

void rgbRainbow(void)
{
  static byte rainbowCounter = 0; // cycles from 0-255
  // From Wheel function in Adafruit_Neopixel strand example:
  uint32_t rainbowColor;
  for (int i=0; i<rgb.numPixels(); i++) // numPixels may just be 1
  {
    byte colorPos = i + rainbowCounter; 
    if (colorPos < 85)
    {
      rainbowColor = rgb.Color(255 - colorPos * 3, 0, colorPos * 3);
    }
    else if (colorPos < 170)
    {
      colorPos -= 85;
      rainbowColor = rgb.Color(0, colorPos * 3, 255 - colorPos * 3);
    }
    else
    {
      colorPos -= 170;
      rainbowColor = rgb.Color(colorPos * 3, 255 - colorPos * 3, 0);
    }
    
    rgb.setPixelColor(i, rainbowColor); // Set the pixel color
  }
  rgb.show(); // Actually set the LED
  rainbowCounter++; // IncremenBUTTONt counter, may roll over to 0
}

BLYNK_WRITE(RGB_RAINBOW_VIRTUAL)
{
  int rainbowState = param.asInt();
  BB_DEBUG("Rainbow Write: " + String(rainbowState));
  if (rainbowState) // If parameter is 1
  {
    blinker.detach(); // Detactch the status LED 
    rgbRainbowTicker.attach_ms(20, rgbRainbow);
    rgbSetByProject = true;
  }
  else
  {
    rgbRainbowTicker.detach();
    for (int i=0; i<rgb.numPixels(); i++)
      rgb.setPixelColor(i, 0);
    rgb.show();
    blinker.attach_ms(1, blinkRGBTimer);
    rgbSetByProject = false;
  }
}

BLYNK_WRITE(RGB_STRIP_NUM_VIRTUAL)
{
  int ledCount = param.asInt();
  if (ledCount <= 0) ledCount = 1;
  BB_DEBUG("RGB Strip length: " + String(ledCount));
  rgb.updateLength(ledCount);
}

/* 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7
 7 Experiment 7: Timer                 7
 7 Widget(s):                          7
 7  - Timer: Relay, GP12, Start, Stop  7
 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 7 */
// No functions or variables required

/* 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8
 8 Experiment 8: LCD                   8
 8 Widget(s):                          8
 8  - LCD: Advanced, V10               8
 8  - Button: TempHumid, V11           8
 8  - Button: Stats, V12               8
 8  - Button: Runtime, V13             8
 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 */
 //! This experiment may be causing random exceptions
WidgetLCD thLCD(LCD_VIRTUAL); // LCD widget, updated in blynkLoop

BLYNK_WRITE(LCD_TEMPHUMID_VIRTUAL)
{
  if (param.asInt() > 0)
  {
    float humd = thSense.readHumidity(); // Read humidity
    float tempC = thSense.readTemperature(); // Read temperature
    float tempF = tempC * 9.0 / 5.0 + 32.0; // Calculate farenheit
    String tempLine = String(tempF, 2) + "F / "+ String(tempC, 2) + "C";
    String humidityLine = "Humidity: " + String(humd, 1) + "%";
    thLCD.clear(); // Clear the LCD
    thLCD.print(0, 0, tempLine.c_str());
    thLCD.print(0, 1, humidityLine.c_str());
  }  
}
BLYNK_WRITE(LCD_STATS_VIRTUAL)
{
  if (param.asInt() > 0)
  {
    String firstLine = "GP0: ";
    if (digitalRead(BUTTON_PIN))
      firstLine += "HIGH";
    else
      firstLine += "LOW";
    String secondLine = "ADC0: " + String(analogRead(A0));
    thLCD.clear(); // Clear the LCD
    thLCD.print(0, 0, firstLine.c_str());
    thLCD.print(0, 1, secondLine.c_str());
  }
}

BLYNK_WRITE(LCD_RUNTIME_VIRTUAL)
{
  if (param.asInt() > 0)
  {
    String topLine = "RunTime-HH:MM:SS";
    String botLine = "     ";
    float seconds, minutes, hours;
    seconds = (float)millis() / 1000;
    minutes = seconds / 60;
    hours = minutes / 60;
    seconds = (int)seconds % 60;
    minutes = (int)minutes % 60;
    botLine += String((int)hours) + ":" + String((int)minutes) + ":" + String((int)seconds);
    thLCD.clear(); // Clear the LCD
    thLCD.print(0, 0, topLine.c_str());
    thLCD.print(0, 1, botLine.c_str());    
  }
}

/* 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9
 9 Experiment 9: Joystick              9
 9 Widget(s):                          9
 9  - Joystick: ServoPos, V14, 0-255,  9
 9     V15, 0-255, Off, Off            9
 9  - Slider: ServoMax, V16, 0-360     9
 9  - Gauge: ServoAngle, V17, 0-360    9
 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 */

BLYNK_WRITE(SERVO_X_VIRTUAL)
{
  int servoXIn = param.asInt();
  BB_DEBUG("Servo X: " + String(servoXIn));
  servoX = servoXIn - 128;
  float pos = atan2(servoY, servoX) * 180.0 / PI;
  if (pos < 0)
    pos = 360.0 + pos;
  Blynk.virtualWrite(SERVO_ANGLE_VIRUTAL, pos);
  int servoPos = map(pos, 0, 360, SERVO_MINIMUM, servoMax);
  myServo.write(servoPos);
  Serial.println("Pos: " + String(pos));
  Serial.println("ServoPos: " + String(servoPos));
}

BLYNK_WRITE(SERVO_Y_VIRTUAL)
{
  int servoYIn = param.asInt();
  BB_DEBUG("Servo Y: " + String(servoYIn));
  servoY = servoYIn - 128;
  float pos = atan2(servoY, servoX) * 180.0 / PI;
  if (pos < 0)
    pos = 360.0 + pos;
  Blynk.virtualWrite(SERVO_ANGLE_VIRUTAL, pos);
  int servoPos = map(pos, 0, 360, SERVO_MINIMUM, servoMax);
  myServo.write(servoPos);
  Serial.println("Pos: " + String(pos));
  Serial.println("ServoPos: " + String(servoPos));
}

BLYNK_WRITE(SERVO_MAX_VIRTUAL)
{
  int sMax = param.asInt();
  BB_DEBUG("Servo Max: " + String(sMax));
  servoMax = constrain(sMax, SERVO_MINIMUM + 1, 360);
  Serial.println("ServoMax: " + String(servoMax));
}

/* 10 10 10 10 10 10 10 10 10 10 10 10 10
 10 Experiment 10: Graph               10
 10 Widget(s):                         10
 10  - Graph: Lux, V18, 0-1023,        10
 10    5s, Line                        10
 10  - Slider: UpdateRate, V19, 0-???  10
 10 10 10 10 10 10 10 10 10 10 10 10 10 */

void luxInit(void)
{
  // Initialize the SFE_TSL2561 library
  // You can pass nothing to light.begin() for the default I2C address (0x39)
  light.begin();
  
  // If gain = false (0), device is set to low gain (1X)
  gain = 0;
  
  unsigned char time = 0;
  // setTiming() will set the third parameter (ms) to the 13.7ms
  light.setTiming(gain,time,ms);

  // To start taking measurements, power up the sensor:
  light.setPowerUp();
  
  luxInitialized = true;
}

void luxUpdate(void)
{
  // This sketch uses the TSL2561's built-in integration timer.
  delay(ms);
  
  // Once integration is complete, we'll retrieve the data.
  unsigned int data0, data1;
  
  if (light.getData(data0,data1))
  {
    double lux;    // Resulting lux value
    boolean good;  // True if neither sensor is saturated
    good = light.getLux(gain,ms,data0,data1,lux);
    BB_DEBUG("Lux: " + String(lux));
    Blynk.virtualWrite(LUX_VIRTUAL, lux);
  }
}

BLYNK_READ(LUX_VIRTUAL)
{
  BB_DEBUG("Blynk read Lux");
  if (luxInitialized)
    luxUpdate();
  else
    Blynk.virtualWrite(LUX_VIRTUAL, analogRead(A0));
  lastLuxUpdate = millis();  
}
//! Not implemented, leaving the virtual claimed for expansion of project
BLYNK_WRITE(LUX_RATE_VIRTUAL)
{
  int luxUpdateIn = param.asInt();
  BB_DEBUG("Lux update rate: " + String(luxUpdateIn));
  luxUpdateRate = luxUpdateIn * 1000;
  if (luxUpdateRate < 1000) luxUpdateRate = 1000;
}

/* 11 11 11 11 11 11 11 11 11 11 11 11 11
 11 Experiment 11: History Graph       11
 11 Widget(s):                         11
 11  - History Graph: V20, Voltage     11
 11  - Value: Voltage, V20, 1sec       11
 11 11 11 11 11 11 11 11 11 11 11 11 11 */
BLYNK_READ(ADC_BATT_VIRTUAL)
{
  int rawADC = analogRead(A0);
  float voltage = ((float) rawADC / 1024.0) * ADC_VOLTAGE_DIVIDER;
  voltage *= 2.0; // Assume dividing VIN by two with another divider

  Blynk.virtualWrite(ADC_BATT_VIRTUAL, voltage);
}

/* 12 12 12 12 12 12 12 12 12 12 12 12 12
 12 Experiment 12: Terminal            12
 12 Widget(s):                         12
 12  - Terminal: V21, On, On           12
 12 12 12 12 12 12 12 12 12 12 12 12 12 */
BLYNK_WRITE(SERIAL_VIRTUAL)
{
  String incoming = param.asStr();
  Serial.println(incoming);
  
  if (incoming.charAt(0) == '!')
  {
    String emailAdd = incoming.substring(1, incoming.length());
    for (int i=0; i<emailAdd.length(); i++)
    {
      if (emailAdd.charAt(i) == ' ') //! TODO check for ANY invalid character
        emailAdd.remove(i, 1);
    }
    //! TODO: Check if valid email - look for @, etc.
    terminal.println("Your email is:" + emailAdd + ".");
    emailAddress = emailAdd;
    terminal.flush();
  }
}

/* 13 13 13 13 13 13 13 13 13 13 13 13 13
 13 Experiment 13: Twitter             13
 13 Widget(s):                         13
 13  - Twitter: Connect account        13
 13  - Button: TweeEn, V22, Switch     13
 13  - Slider: Threshold, V23, 1023    13
 13  - Slider: Rate, V24, 0-60         13
 13 13 13 13 13 13 13 13 13 13 13 13 13 */
BLYNK_WRITE(TWEET_ENABLE_VIRTUAL)
{
  uint8_t state = param.asInt();
  BB_DEBUG("Tweet enable: " + String(state));
  if (state)
  {
    tweetEnabled = true;
    BB_DEBUG("Tweet enabled.");
  }
  else
  {
    tweetEnabled = false;
    BB_DEBUG("Tweet disabled.");    
  }
}

BLYNK_WRITE(TWITTER_THRESHOLD_VIRTUAL)
{
  moistureThreshold = param.asInt();
  BB_DEBUG("Tweet threshold set to: " + String(moistureThreshold));
}

BLYNK_WRITE(TWITTER_RATE_VIRTUAL)
{
  int tweetRate = param.asInt();
  if (tweetRate <= 0) tweetRate = 1;
  BB_DEBUG("Setting tweet rate to " + String(tweetRate) + " minutes");
  tweetUpdateRate = tweetRate * 60 * 1000;
}

void twitterUpdate(void)
{
  unsigned int moisture = analogRead(A0);
  String msg = "~~MyBoard~~\r\nSoil Moisture reading: " + String(moisture) + "\r\n";
  if (moisture < moistureThreshold)
  {
    msg += "FEED ME!\r\n";
  }
  msg += "[" + String(millis()) + "]";
  BB_DEBUG("Tweeting: " + msg);
  Blynk.tweet(msg);  
}

/* 14 14 14 14 14 14 14 14 14 14 14 14 14
 14 Experiment 14: Push                14
 14 Widget(s):                         14
 14  - Push: Off, On (iPhone)          14
 14  - Value: DoorState, V25, 1sec     14
 14  - Button: PushEnable, V26, Switch 14
 14 14 14 14 14 14 14 14 14 14 14 14 14 */
#define DOOR_SWITCH_PIN 16
#define DOOR_SWITCH_UPDATE_RATE 1000
unsigned long lastDoorSwitchUpdate = 0;
bool pushEnabled = false;
uint8_t lastSwitchState = 255;

BLYNK_READ(DOOR_STATE_VIRTUAL)
{
  BB_DEBUG("Blynk read door state");
  uint8_t switchState = digitalRead(DOOR_SWITCH_PIN);
  if (lastSwitchState != switchState)
  {
    if (switchState)
    {
      BB_DEBUG("Door switch closed.");  
      Blynk.virtualWrite(DOOR_STATE_VIRTUAL, "Close");
      if (pushEnabled)
      {
        BB_DEBUG("Notified closed.");  
        Blynk.notify("[" + String(millis()) + "]: Door closed");
      }
    }
    else
    {
      BB_DEBUG("Door switch opened.");    
      Blynk.virtualWrite(DOOR_STATE_VIRTUAL, "Open");
      if (pushEnabled)
      {
        BB_DEBUG("Notified opened.");  
        Blynk.notify("[" + String(millis()) + "]: Door opened");          
      }
    }
    lastSwitchState = switchState;
  }  
}

BLYNK_WRITE(PUSH_ENABLE_VIRTUAL)
{
  uint8_t state = param.asInt();
  BB_DEBUG("Push enable: " + String(state));
  if (state)
  {
    pushEnabled = true;
    BB_DEBUG("Push enabled.");
  }
  else
  {
    pushEnabled = false;
    BB_DEBUG("Push disabled.");    
  }
}

/* 15 15 15 15 15 15 15 15 15 15 15 15 15
 15 Experiment 15: Email               15
 15 Widget(s):                         15
 15  - Email: Off, On (iPhone)         15
 15  - Terminal: V21, On, On           15
 15  - Button: Send, V27, Push         15
 15 15 15 15 15 15 15 15 15 15 15 15 15 */

void emailUpdate(void)
{
  String emailSubject = "My BlynkBoard Statistics";
  String emailMessage = "";
  emailMessage += "D0: " + String(digitalRead(0)) + "\r\n";
  emailMessage += "D16: " + String(digitalRead(16)) + "\r\n";
  emailMessage += "\r\n";
  emailMessage += "A0: " + String(analogRead(A0)) + "\r\n";
  emailMessage += "\r\n";
  emailMessage += "Temp: " + String(thSense.readTemperature()) + "C\r\n";
  emailMessage += "Humidity: " + String(thSense.readHumidity()) + "%\r\n";
  emailMessage += "\r\n";
  emailMessage += "Runtime: " + String(millis() / 1000) + "s\r\n";

  BB_DEBUG("email: " + emailAddress);
  BB_DEBUG("subject: " + emailSubject);
  BB_DEBUG("message: " + emailMessage);
  Blynk.email(emailAddress.c_str(), emailSubject.c_str(), emailMessage.c_str());
  terminal.println("Sent an email to " + emailAddress);
  terminal.flush();
}

BLYNK_WRITE(EMAIL_ENABLED_VIRTUAL)
{
  int emailEnableIn = param.asInt();
  BB_DEBUG("Email enabled: " + String(emailEnableIn))
  if (emailEnableIn)
  {
    if (emailAddress != "")
    {
      if ((lastEmailUpdate == 0) || (lastEmailUpdate + EMAIL_UPDATE_RATE < millis()))
      {
        emailUpdate();
        lastEmailUpdate = millis();
      }
      else
      {
        int waitTime = (lastEmailUpdate + EMAIL_UPDATE_RATE) - millis();
        waitTime /= 1000;
        terminal.println("Please wait " + String(waitTime) + " seconds");
        terminal.flush();
      }
    }
    else
    {
      terminal.println("Type !email@address.com to set the email address");
      terminal.flush();
    }
  }
}

BLYNK_WRITE(RESET_VIRTUAL)
{
  int resetIn = param.asInt();
  BB_DEBUG("Blynk Reset: " + String(resetIn));
  if (resetIn)
  {
    BB_DEBUG("Factory resetting");
    resetEEPROM();
    ESP.reset();
  }
}

void blynkSetup(void)
{
  BB_DEBUG("Initializing Blynk Demo");
  WiFi.enableAP(false);
  runMode = MODE_BLYNK_RUN;
  //detachInterrupt(BUTTON_PIN);
  attachInterrupt(BUTTON_PIN, buttonUpdate, CHANGE);
  thSense.begin();

  //lcdTHTicker.attach(LCD_TICKER_UPDATE_RATE, updateLCDTH);
  
  myServo.attach(SERVO_PIN);
  myServo.write(15);

  pinMode(DOOR_SWITCH_PIN, INPUT_PULLDOWN_16);
  lastSwitchState = digitalRead(DOOR_SWITCH_PIN);

  if (scanI2C(LUX_ADDRESS))
  {
    BB_DEBUG("Luminosity sensor connected.");
    luxInit();
  }
  else
  {
    BB_DEBUG("No lux sensor.");
  }
  
}

void blynkLoop(void)
{
  if (tweetEnabled && ((lastTweetUpdate == 0) || (lastTweetUpdate + tweetUpdateRate < millis())))
  {
    twitterUpdate();
    lastTweetUpdate = millis();
  }

  if (lastRunTimeUpdate + RUNTIME_UPDATE_RATE < millis())
  {
    float runTime = (float) millis() / 1000.0; // Convert millis to seconds
    // Assume we can only show 4 digits
    if (runTime >= 1000) // 1000 seconds = 16.67 minutes
    {
      runTime /= 60.0; // Convert to minutes
      if (runTime >= 1000) // 1000 minutes = 16.67 hours
      {
        runTime /= 60.0; // Convert to hours
        if (runTime >= 1000) // 1000 hours = 41.67 days
          runTime /= 24.0;
      }
    }
    Blynk.virtualWrite(RUNTIME_VIRTUAL, runTime);
    lastRunTimeUpdate = millis();
  }

  if (Serial.available())
  {
    String toSend;
    while (Serial.available())
      toSend += (char)Serial.read();
    terminal.print(toSend);
    terminal.flush();
  }
}

bool scanI2C(uint8_t address)
{
  Wire.beginTransmission(address);
  Wire.write( (byte)0x00 );
  if (Wire.endTransmission() == 0)
    return true;
  return false;
}
