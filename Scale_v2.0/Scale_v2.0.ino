//Coffee Scale! 1.6
// Scale factor:
// 1Kg cell: 872 for reading in gms
//scale factor * reading / actual
//23600 RAW reading of plate only


#include "HX711.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define VERSION "V2.0"
#define LARGE_TEXT 2
#define SMALL_TEXT 1
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define TARE_TIMER_DURATION 30000 //the duration to start the timer for after a TARE in ms
#define POST_TARE_DELAY 500 //amount of time to minus from the timer after a tare in ms
#define SHOT_ADDR 0  //EPROM address to store shot counter (leave space between addresses in case of EPROM wear)
#define SCALE_ADDR 16 //EPROM address to store scale factor
#define CLEAN_ADDR 32 ////EPROM address to store clean counter
#define REF_WEIGHT 447.8
#define STD_SCALE_FACTOR 872
#define SHOTS_UNTIL_CLEAN 50
#define DESIRED_GRIND_MASS 18
#define DESIRED_GRIND_MASS_SINGLE 10
#define TARE_WEIGHT 600
#define SLEEP_TIME 600 //time to sleep display  in seconds
#define ACTIVE_TIMEOUT 5 //time in seconds to timeout display to normal
#define COOLING_TIMEOUT 40 //time in seconds qto keep the cooling shot timer going for 
#define ACTIVE_RATE 0.2 // g/s to denote extraction in progress
#define NO_COFFEE_RATE 5 // g/s to denote pure water in progress

#define PERFECT_TEMP 95


//Pin 1 = -
//Pin 2 = +
//Pin 3 = A5
//Pin 4 = A4
//Pin 5 = A3
//Pin 6 = +
//Pin 7 = -
//Pin 8 = D2
//Pin 9 = +
//Pin 10 = -
//Pin 11 = D3
//Pin 12 = NC

//3.5mm Outer = E+
//3.5mm Biggest = E-
//3.5mm Middle = A-
//3.5mm = A+

#define PIN_SPARE_WIRE A6
#define PIN_SPARE_DISPLAY A3
#define PIN_DISPLAY_SDA A4
#define PIN_DISPLAY_SCL A5
#define PIN_BEEP A7
#define PIN_PUMP 3
#define PIN_TEMP 2
#define PIN_HX711_SCK 5
#define PIN_HX711_DT 6




// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

HX711 scale; //the scale
OneWire oneWire(PIN_TEMP); //temp sensor
DallasTemperature sensors(&oneWire);

bool dbg = false;

//globals
long startTime = 0, endOfcoolingFlush = 0;
float timer = 0, previous = 0;
bool timerStarted = false, singleShot = false, doubleShot = false, activeShot = false, coolingFlush = false;
int shotCounter, lastCleaned;
bool cleanRequired = false;
long lastTareTime = 0, lastRateTime = 0, lastActiveTime = 0;
float lastRateReading = 0, extractionRate = 0, maxExtractionRate = 0, startMass = 0, massAt30 = 0;
float scaleFactor;
float currentTemp = 0;


bool blinkLEDON = false;




void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BEEP, OUTPUT);

  pinMode(PIN_PUMP, INPUT);           // set pin to input
  digitalWrite(PIN_PUMP, HIGH);  
  
  Serial.begin(38400);
  Serial.println((String)"Coffee Scale " + VERSION + " Feb 2020");
  sensors.begin();
  sensors.setWaitForConversion(false);
  
   //setup display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 alloc failed"));
    for (;;);
  }
  display.setRotation(2); //we have the display upside down!
  

  displayStuff("Hello", "AB", "Init", VERSION);

  EEPROM.get(SHOT_ADDR, shotCounter);
  EEPROM.get(CLEAN_ADDR, lastCleaned);
  if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
  {
    cleanRequired = true;
  }
  

  //setup scale
  Serial.println("Initializing the scale.. RAW READING");
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  Serial.println(scale.read_average(20));
  Serial.println("scale initialised");

  //scaleFactor = STD_SCALE_FACTOR;
  //EEPROM.put(SCALE_ADDR, scaleFactor); //USE ON FIRST RUN TO SET EPROM
  EEPROM.get(SCALE_ADDR, scaleFactor);
  Serial.println("Scale Factor:");
  Serial.println(scaleFactor);
  if (!(scaleFactor < 950) && !(scaleFactor > 700)) //sanity check the scale factor
  {
    Serial.println("Error in SF");
    scaleFactor = STD_SCALE_FACTOR;
    EEPROM.put(SCALE_ADDR, STD_SCALE_FACTOR);
  }
  scale.set_scale(scaleFactor);                 //scaleFactor = 872.f;
  scale.tare();
  Serial.println("Scale Offset:");
  Serial.println(scale.get_offset());


  //recal on startup if pump is set on.
  bool activeShot = !digitalRead(PIN_PUMP); 
  if (activeShot)
  {
    recal();
  }
  

}











void loop() {
  int n;
  float mean;
  bool coolingFlushActive = false;
  
  blinkLED();

  bool activeShotNew = !digitalRead(PIN_PUMP); 

  //if this is a new shot then reset all the timers etc
  if (activeShotNew && (activeShot != activeShotNew))
  {
    doubleShot = false;
    singleShot = false;
    maxExtractionRate = 0;
    massAt30 = 0;
    coolingFlush = false;
    timer = 0;
  }
  
  activeShot = activeShotNew;
  if (activeShot)
  {
    lastActiveTime = millis();
  }

  long timeSinceLastActive = ((long)millis() - lastActiveTime) / 1000;
  if (timeSinceLastActive > ACTIVE_TIMEOUT) //if it's been a while since we were last active then reset the shot counters.
  {
    doubleShot = false;
    singleShot = false;
    maxExtractionRate = 0;
    massAt30 = 0;
  }

  if (timeSinceLastActive > COOLING_TIMEOUT)
  {
    coolingFlush = false;
    timer = 0;
  }

  
  dbgPnt(1);


  //read the scale!!!!
  mean = readScaleDebounce();


  
  dbgPnt(3);




  
  
  long timeDiff = (millis() - lastRateTime) ;//time difference in mills between now and the last time around
  
  //calculate the flow rate and temp once per second
  if (timeDiff > 1000)
  {
     extractionRate = ((mean - lastRateReading) / timeDiff) * 1000; // g/second
     if (extractionRate < 0.2)
     {
        extractionRate = 0;
     }
     lastRateReading = mean;
     lastRateTime = millis();

     if (extractionRate > maxExtractionRate)
     {
        maxExtractionRate = extractionRate;
     }


     if (extractionRate > ACTIVE_RATE)
     {
        if (millis() > (lastActiveTime + 2000))
        {
          startMass = lastRateReading;
        }    
     }

     

     float newTemp = sensors.getTempCByIndex(0);
     if ((currentTemp >= PERFECT_TEMP) && (newTemp <= PERFECT_TEMP))
     {
      beep(300,150);
     }
     currentTemp = newTemp;
     sensors.requestTemperatures(); //this takes 750ms so put it last so as not to block.
  }
  

  dbgPnt(4);


 

  

  if (activeShot)
  {
    if (timerStarted == false)
    {
      
      startTime = millis();
      scale.tare();

      timer = millis() - startTime;
      timerStarted = true;
      Serial.print("Start Timer "); Serial.print(startTime);
    }
    else
    {
      //if it's been a second since the last weight was added then don't count that interval.
      long timeSinceLast = (millis() - startTime);
      if (timeSinceLast < 1000)
      {
        
        timer = timer + timeSinceLast;
      }
      startTime = millis();

      if ((timer > 30000) && (massAt30 ==0))
      {
        massAt30 = mean;
      }
      
    }

    if (extractionRate > NO_COFFEE_RATE)
     {
      coolingFlush = true; //this stick
      coolingFlushActive = true; //this gets reset each cycle
      endOfcoolingFlush = millis();
     }
    
  }
  else
  {
    timerStarted = false;
  }

  
  


dbgPnt(5);

  //Count the shots
  if ((extractionRate > ACTIVE_RATE) && (coolingFlushActive == false) && activeShot)
  {
    if ((mean > 15) && (mean < 16))
    {
      //if we haven't already counted a single shot then do so.
      if (singleShot == false)
      {
        singleShot = true;
        shotCounter = shotCounter + 1;
        beep(150,75);
        EEPROM.put(SHOT_ADDR, shotCounter);
        if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
        {
          cleanRequired = true;
        }
        
      }
    }
    else if ((mean > 25) && (mean < 30))
    {
      //if we haven't already counted a double shot then do so.
      if (doubleShot == false)
      {
        doubleShot = true;
        beep(300, 75);
        delay(50);
        beep(300, 75);
        
        shotCounter = shotCounter + 1;
        EEPROM.put(SHOT_ADDR, shotCounter);
        if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
        {
          cleanRequired = true;
        }
        
      }
    }
  }




  
  
  if ((timeSinceLastActive < SLEEP_TIME))
  {
    if ((timeSinceLastActive < ACTIVE_TIMEOUT) && (coolingFlush == false))
    {
      if (activeShot)
      {
        //if the pump is on display the extraction
        displayShot(mean, timer, extractionRate, currentTemp);  
      }
      else
      {
        displayShot(mean, timer, maxExtractionRate, currentTemp);  //if the pump is off show the max extraction rate and mass at 30s
      }
    }
    else
    {
      if (coolingFlush)
      {
        timer = millis() - endOfcoolingFlush;
        displayStuff(mean, shotCounter, timer, currentTemp, false);
      }
      else
      {
        displayStuff(mean, shotCounter, timer, currentTemp, false);
      }
    }
  }
  else
  {
    //sleep. Power down the scale, turn off the display and wait for the pump signal.
    display.clearDisplay();
    display.display();
    scale.power_down();     
    while (digitalRead(PIN_PUMP)) //while the pump isn't on..
    {
      delay(200);
    }
    scale.power_up();
    
  }



}

float readScaleDebounce()
{
  float mean;
  float mean1, mean2;
  mean2 = previous;
  mean = mean1 = scale.get_units(1);
  //Serial.println(mean);
  
  //compare new read to old, if they're miles off then something went wrong so go again
  while (((mean1 - mean2) > 3) || ((mean1 - mean2) < -2))
  {
    if (scale.wait_ready_timeout(500)) {
      mean1 = scale.get_units(1);
    }
    else
    {
      mean1 = -999;
    }
    if (scale.wait_ready_timeout(500)) {
      mean2 = scale.get_units(1);   
    }
    else
    {
      mean2 = -999;
    }
    if ((mean1 > TARE_WEIGHT) && (mean2 > TARE_WEIGHT))
    {
      //Hard push = tare
      tareMenu(mean1);
      mean1 = 0;
      mean2 = 0;
    }
    mean = (mean1 + mean2) / 2;
  }

  dbgPnt(2);
  
  float difference = mean - previous;
  //if the diffference is small then we average it with the previous. This prevents so much jitter. We do this after we've calcutaed the rate so we don't affect it.
  if ((difference > -0.2) && (difference < 0.2))
  {
    mean = ((previous * 2) + mean) / 3;
  }
  previous = mean;
  if ((mean < 1) && (mean > -1))
  {
    mean = 0;
  }
  return mean;
}


void beep(int freq, int duration)
{
  tone(PIN_BEEP, freq);
  delay(duration);
  noTone(PIN_BEEP);
}

void blinkLED()
{
   if (blinkLEDON)
  {
    digitalWrite(LED_BUILTIN, LOW);
    blinkLEDON = false;
  }
  else
  {
    digitalWrite(LED_BUILTIN, HIGH);  
    blinkLEDON = true;
  }
}

void dbgPnt(int spot)
{

  if (dbg && !activeShot)
  {
    display.setTextSize(SMALL_TEXT);
    display.setCursor(85, 55);
  
    if (spot == 1)
    {
      display.print(".");
    }
    else if (spot == 2)
    {
      display.print("..");
    }
    else if (spot == 3)
    {
      display.print("...");   
    }
    else if (spot == 4)
    {
      display.print("....");

    }
    else if (spot == 5)
    {
      display.print(".....");

    }
    else if (spot == 6)
    {
      display.print("......");

    }
    display.display();
  }
}

void displayShot(float extraction, float timer, float rate, float temp)
{

 
  char meanStr[7];
  dtostrf(extraction, 5, 1, meanStr);
  meanStr[5] = 'g';
  meanStr[6] = '\0';
  
  char timeStr[7];
  dtostrf((float(timer) / 1000), 5, 1, timeStr);
  timeStr[5] = 's';
  timeStr[6] = '\0';

  char rateStr[6];
  dtostrf(rate, 5, 1, rateStr);
  rateStr[5] = '\0';

  char tempStr[5];
  dtostrf(temp, 4, 1, tempStr);
  tempStr[4] = '\0';

  display.clearDisplay();
  display.setTextColor(WHITE);
  
  display.setTextSize(SMALL_TEXT);
  
  display.setCursor(30, 0);
  display.print("Extraction: ");
  
  display.setCursor(30, 28);
  display.print("Timer: ");

  display.setCursor(0, 55);
  display.print("AR: ");

  display.setCursor(15, 55);
  display.print(rateStr);

  display.setCursor(60, 55);
  display.print("Temp: ");

  display.setCursor(95, 55);
  display.print(tempStr);
  
  display.setTextSize(LARGE_TEXT);
  
  display.setCursor(30, 10);
  display.print(meanStr);
  
  display.setCursor(30, 38);
  display.print(timeStr);

  display.display();
  
}


void displayStuff(float extraction, int shots, float timer)
{
  displayStuff(extraction, shots, timer, false);
}

void displayStuff(float extraction, int shots, float timer, bool newGrind)
{
  //sensors.requestTemperatures(); 
  //float temp = sensors.getTempCByIndex(0);
  displayStuff(extraction, shots, timer, currentTemp, newGrind);
}

void displayStuff(float extraction, int shots, float timer, float temp, bool newGrind)
{
  char shotsStr[5];
  itoa(shots, shotsStr, 10);
  
  char meanStr[7];
  dtostrf(extraction, 5, 1, meanStr);
  if (extraction < -100)
  {
    //dirty shift right to fix alignment
    meanStr[4] = meanStr[3];
    meanStr[3] = meanStr[2];
    meanStr[2] = meanStr[1];
    meanStr[1] = meanStr[0];
    meanStr[0] = ' ';
  }
  meanStr[5] = 'g';
  meanStr[6] = '\0';
  
  char timeStr[6];
  dtostrf((float(timer) / 1000), 4, 0, timeStr);
  timeStr[4] = 's';
  timeStr[5] = '\0';
  
  char tempStr[5];
  dtostrf(temp, 4, 1, tempStr);
  
  displayStuff(meanStr, shotsStr, timeStr, tempStr, newGrind);
}

void displayStuff(char extraction[], char shots[], char timer[], char temp[])
{
  displayStuff(extraction, shots, timer, temp, false);
}


void displayStuff(char extraction[], char shots[], char timer[], char temp[], bool newGrind)
{
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  display.setTextSize(SMALL_TEXT);
  
  display.setCursor(0, 0);
  display.print("Extraction: ");
  
  display.setCursor(0, 35);
  display.print("Temp:");
  
  display.setCursor(85, 0);
  display.print("Shots:");
  
  display.setCursor(85, 20);
  display.print("Timer:");
  
  display.setCursor(85, 40);
  display.print("Status:");


  
  display.setTextSize(LARGE_TEXT);
  
  display.setCursor(0, 10);
  display.print(extraction);
  
  display.setCursor(0, 45);
  display.print(temp);


  display.setTextSize(SMALL_TEXT);
  
  display.setCursor(95, 10);
  display.print(shots);
  
  display.setCursor(95, 30);
  display.print(timer);
  
  
  display.setCursor(95, 50);
  if (newGrind == false)
  {
    if (cleanRequired)
    {
      display.print("CLEAN");
    }
    else
    {
      display.print("OK");
    }
  }
  else
  {
    display.print("GRIND");
  }

  
  display.display();
}

void displayStuff(char text[])
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(text);
  display.display();
}




void tareMenu(float mean)
{

  timer = 0;
  timerStarted = false;
  singleShot = false;
  doubleShot = false;

  displayStuff("Menu", "", "", "TARE");

  delay(1500);

  mean = scale.get_units(2);
  if ((mean > 685) && (mean < 720))
  {
    newGrind();
    return;
  }


  if (mean < 700)
  {
    //normal tare
    tareScale();
    return;
  }

  //check for clean
  displayStuff("Menu", "", "", "Clean?");
  delay(2000);
  mean = scale.get_units(2);
  if (mean > 700)
  {
    cleanRequired = false;
    lastCleaned = shotCounter;
    EEPROM.put(CLEAN_ADDR, lastCleaned);
    displayStuff("Menu", "", "Clean", "Done");
    return;
  }

  //check for recal
  displayStuff("Menu", "", "", "Recal?");
  delay(2000);
  mean = scale.get_units(2);
  if (mean > 700)
  {
    recal();
    return;
  }

  //check for recal
  displayStuff("Menu", "", "", "Debug?");
  delay(2000);
  mean = scale.get_units(2);
  if (mean > 700)
  {
    dbg = !dbg;
    return;
  }

  //check for shot reset
  displayStuff("Menu", "", "", "Reset SC?");
  delay(2000);

  mean = scale.get_units(2);
  if (mean > 700)
  {
    shotCounter = 0;
    return;
  }

}



void tareScale()
{
  displayStuff("Menu", "", "", "TARE");
  scale.tare();
  float mean = scale.get_units(2);
  int i = 0;
  while (((mean > 1) || (mean < -1)) && (i < 10))
  {
    char meanStr[7];
    dtostrf(mean, 5, 1, meanStr);
    meanStr[5] = 'g';
    meanStr[6] = '\0';
    displayStuff(meanStr, "10", "", "Re Tare");
    delay(1000);
    scale.tare();
    mean = scale.get_units(2);
    i++;
  }
  if (i == 10)
  {
    displayStuff("ERROR", "", "", "FAIL");
    delay(10000);
  }
  lastTareTime = millis() + POST_TARE_DELAY;

}




void recal()
{
  bool calibrationSuccess = false;
  while (calibrationSuccess == false)
  {
    //recalibrate the scale based on weight of tamp
    displayStuff("Recal", "", "", "TARE");
    delay(3000);
    
    scale.tare();;
    displayStuff("Place Tamp 447.8 ");
    
    delay(3000);
    float mean = scale.get_units(30);
    Serial.println("Resetting SF");
    //scale factor * reading / actual
    scaleFactor = (scaleFactor * mean) / REF_WEIGHT;
    if ((scaleFactor > 950) || (scaleFactor < 700))
    {
      Serial.println("Cal Error. New SF");
      Serial.println(scaleFactor);
      EEPROM.get(SCALE_ADDR, scaleFactor);
      displayStuff("SF ERROR!");
    }
    else
    {
      Serial.println("New SF");
      Serial.println(scaleFactor);
      scale.set_scale(scaleFactor);
      EEPROM.put(SCALE_ADDR, scaleFactor);
      char sfStr[5];
      dtostrf(scaleFactor, 5, 1, sfStr);
      displayStuff("Recal", sfStr, "", "Done");
      delay(3000);
      calibrationSuccess = true;
    }
  }
  tareScale();

}



void newGrind()
{

  displayStuff("New Grind", "", "", "TARE");
  delay(500);
  tareScale();
  displayStuff("New Grind", "", "", "Set 15S");
  delay(4000);

  float mean = scale.get_units(2);
  //activeShot = !digitalRead(PIN_PUMP);
  //while the pump isn't on...
  while ((mean < 400 ) && (digitalRead(PIN_PUMP)))
  {
    mean = scale.get_units(10);
    float grindTime = 0;
    float grindTimeSingle = 0;
    if (mean > 2)
    {
      grindTime = float(DESIRED_GRIND_MASS) / (mean / 15);
      grindTime = grindTime;
      grindTimeSingle = float(DESIRED_GRIND_MASS_SINGLE) / (mean / 15);
      
    }
    if (((mean < 1) && (mean > 0.2)) || ((mean > -1) && (mean < -0.2)))
    {
      displayStuff(mean, 2, grindTimeSingle, grindTime, true);
      scale.tare();
    }
    displayStuff(mean, 2, grindTimeSingle, grindTime, true);
  }


}
