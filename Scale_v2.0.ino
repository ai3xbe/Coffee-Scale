//Coffee Scale! 1.6
// Scale factor:
// 1Kg cell: 872 for reading in gms
//scale factor * reading / actual


#include "HX711.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define LARGE_TEXT 2
#define SMALL_TEXT 1
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define VERSION "V2.0"
#define TARE_TIMER_DURATION 30000 //the duration to start the timer for after a TARE
#define POST_TARE_DELAY 1000 //amount of time to minus from the timer after a tare
#define SCALE_ADDR 15
#define SHOT_ADDR 0  //address to store shot counter
#define CLEAN_ADDR 32
#define refWeight 447.8
#define SHOTS_UNTIL_CLEAN 50
#define DESIRED_GRIND_MASS 18
#define DESIRED_GRIND_MASS_SINGLE 10
#define TARE_WEIGHT 600
#define SLEEP_TIME 600 //time in seconds
#define ACTIVE_TIMEOUT 2000 //time in miliseconds
#define ACTIVE_RATE 0.5


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



//globals
long startTime = 0;
float timer = 0, previous = 0;
bool timerStarted = false, singleShot = false, doubleShot = false;
int shotCounter, lastCleaned;
bool cleanRequired = false;
long lastTareTime = 0, lastRateTime = 0, lastActiveTime = 0;
float lastRateReading = 0, extractionRate = 0;
float scaleFactor;
bool blinkLEDON = false;




void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BEEP, OUTPUT);
  
  Serial.begin(38400);
  Serial.println((String)"Coffee Scale " + VERSION + " Feb 2020");
  sensors.begin();
  
   //setup display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 alloc failed"));
    for (;;);
  }
  

  displayStuff("Hello", "AB", "Init", VERSION);

  EEPROM.get(SHOT_ADDR, shotCounter);
  EEPROM.get(CLEAN_ADDR, lastCleaned);
  if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
  {
    cleanRequired = true;
  }
  

  //setup scale
  Serial.println("Initializing the scale");
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  Serial.println("scale initialised");
  EEPROM.get(SCALE_ADDR, scaleFactor);
  scale.set_scale(scaleFactor);                 //scaleFactor = 872.f;
  scale.tare();



}

void loop() {
  
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
  
  int n;
  float mean;
  float mean1, mean2;
  bool activeShot = false;


  mean2 = previous;
  mean = mean1 = scale.get_units(1);
  
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
  
  
  
//calculate the flow rate once per second
  long timeDiff = (millis() - lastRateTime) ;//time difference in mills between now and the last time around
  if (timeDiff > 500)
  {
     extractionRate = ((mean - lastRateReading) / timeDiff) * 1000; // g/second
     if (extractionRate < 0.1)
     {
        extractionRate = 0;
     }
     lastRateReading = mean;
     lastRateTime = millis();


     if (extractionRate > ACTIVE_RATE)
     {
        lastActiveTime = millis();
     }
  }
  




  double difference = mean - previous;
  //if the diffference is small then we average it with the previous. This prevents so much jitter. We do this after we've calcutaed the rate so we don't affect it.
  if ((difference > -0.3) && (difference < 0.3))
  {
    mean = ((previous * 2) + mean) / 3;
  }

  

  //start timer and continue until weight stops being added. If it's been less than TARE_TIMER_DURATION seconds since last tare then start the timer as well.
  if ((mean >= 1) && (mean > (previous + 0.15)))
  {
    if (timerStarted == false)
    {
      
      //if we tared less than X seconds ago, count that as the start
      if ((millis() - lastTareTime ) < TARE_TIMER_DURATION)
      {
        startTime = lastTareTime;
      }
      else
      {
        startTime = millis();
      }
      timer = millis() - startTime;
      timerStarted = true;
      Serial.print("Start Timer "); Serial.print(startTime);
    }
    else
    {
      //if it's been a second since the last weight was added then don't count that interval.
      double timeSinceLast = (millis() - startTime);
      if (timeSinceLast < 1000)
      {
        
        timer = timer + timeSinceLast;
      }
      startTime = millis();
    }
  }
  else if ((mean < 1) && (mean > -50))
  {
    if (((long)millis() - lastTareTime ) < TARE_TIMER_DURATION)
    {
      timer = (long)millis() - lastTareTime;
      
    }
    else
    {
      timer = 0;
    }

  }




  //Count the shots
  if ((mean > 11) && (mean < 15))
  {
    //if we haven't already counted a single shot then do so.
    if (singleShot == false)
    {
      singleShot = true;
      shotCounter = shotCounter + 1;
      beep();
      EEPROM.put(SHOT_ADDR, shotCounter);
      if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
      {
        cleanRequired = true;
      }
    }
  }
  else if ((mean > 25) && (mean < 32))
  {
    //if we haven't already counted a double shot then do so.
    if (doubleShot == false)
    {
      doubleShot = true;
      beep();
      shotCounter = shotCounter + 1;
      EEPROM.put(SHOT_ADDR, shotCounter);
      if (shotCounter > (lastCleaned +SHOTS_UNTIL_CLEAN))
      {
        cleanRequired = true;
      }
    }
    digitalWrite(LED_BUILTIN, HIGH);
  }



  previous = mean;
  if ((mean < 1) && (mean > -1))
  {
    mean = 0;
  }

  long runningTime = ((long)millis() - lastTareTime) / 1000;
  if (runningTime < SLEEP_TIME)
  {
    if (millis() < (lastActiveTime + ACTIVE_TIMEOUT)) //if it's been longer than Active timeout since our last reading then normal display. Otherwise display shot
    {
      displayShot(mean, timer, extractionRate);
    }
    else
    {
      displayStuff(mean, shotCounter, timer);
      
    }

  }
  else
  {
    display.clearDisplay();
    display.display();
    scale.power_down();             // put the ADC in sleep mode
    delay(1000);
    scale.power_up();
    
  }



}

void beep()
{
  tone(PIN_BEEP, 150);
  delay(75);
  noTone(PIN_BEEP);
}

void displayShot(double extraction, float timer, float rate)
{
  char meanStr[7];
  dtostrf(extraction, 5, 1, meanStr);
  meanStr[5] = 'g';
  meanStr[6] = '\0';
  
  char timeStr[7];
  dtostrf((float(timer) / 1000), 5, 1, timeStr);
  timeStr[5] = 's';
  timeStr[6] = '\0';

  char rateStr[9];
  dtostrf(rate, 5, 1, rateStr);
  rateStr[5] = 'g';
  rateStr[6] = '/';
  rateStr[7] = 's';
  rateStr[8] = '\0';

  display.clearDisplay();
  display.setTextColor(WHITE);
  
  display.setTextSize(SMALL_TEXT);
  
  display.setCursor(30, 0);
  display.print("Extraction: ");
  
  display.setCursor(30, 28);
  display.print("Timer: ");

  display.setCursor(30, 55);
  display.print("Rate: ");

  display.setCursor(55, 55);
  display.print(rateStr);
  
  display.setTextSize(LARGE_TEXT);
  
  display.setCursor(30, 10);
  display.print(meanStr);
  
  display.setCursor(30, 38);
  display.print(timeStr);

  display.display();
  
}


void displayStuff(double extraction, int shots, float timer)
{
  displayStuff(extraction, shots, timer, false);
}

void displayStuff(double extraction, int shots, float timer, bool newGrind)
{
  sensors.requestTemperatures(); 
  float temp = sensors.getTempCByIndex(0);
  displayStuff(extraction, shots, timer, temp, newGrind);
}

void displayStuff(double extraction, int shots, float timer, float temp, bool newGrind)
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
  
  char timeStr[7];
  dtostrf((float(timer) / 1000), 5, 1, timeStr);
  timeStr[5] = 's';
  timeStr[6] = '\0';
  
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
  display.print("Timer:");
  
  display.setCursor(85, 0);
  display.print("Shots:");
  
  display.setCursor(85, 20);
  display.print("Temp:");
  
  display.setCursor(85, 40);
  display.print("Status:");


  
  display.setTextSize(LARGE_TEXT);
  
  display.setCursor(0, 10);
  display.print(extraction);
  
  display.setCursor(0, 45);
  display.print(timer);


  display.setTextSize(SMALL_TEXT);
  
  display.setCursor(95, 10);
  display.print(shots);
  
  display.setCursor(95, 30);
  display.print(temp);
  
  
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




void tareMenu(double mean)
{

  timer = 0;
  timerStarted = false;
  singleShot = false;
  doubleShot = false;

  displayStuff("Menu", "", "TARE", "");

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
  displayStuff("Menu", "", "Clean?", "");
  delay(2000);
  mean = scale.get_units(2);
  if (mean > 700)
  {
    cleanRequired = false;
    lastCleaned = shotCounter;
    EEPROM.put(CLEAN_ADDR, lastCleaned);
    displayStuff("Menu", "Done", "Clean", "");
    return;
  }

  //check for recal
  displayStuff("Menu", "", "Recal?", "");
  delay(2000);
  mean = scale.get_units(2);
  if (mean > 700)
  {
    recal();
    return;
  }

  //check for shot reset
  displayStuff("Menu", "", "Reset SC?", "");
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
  displayStuff("Menu", "", "TARE", "");
  scale.tare();
  double mean = scale.get_units(2);
  int i = 0;
  while (((mean > 1) || (mean < -1)) && (i < 10))
  {
    char meanStr[7];
    dtostrf(mean, 5, 1, meanStr);
    meanStr[5] = 'g';
    meanStr[6] = '\0';
    displayStuff(meanStr, "10", "Re Tare", "");
    delay(1000);
    scale.tare();
    mean = scale.get_units(2);
    i++;
  }
  if (i == 10)
  {
    displayStuff("ERROR", "", "FAIL", "");
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
    displayStuff("Recal", "", "TARE", "");
    delay(3000);
    
    scale.tare();;
    displayStuff("Place Tamp 447.8 ");
    
    delay(3000);
    double mean = scale.get_units(30);
    Serial.println("Resetting SF");
    //scale factor * reading / actual
    scaleFactor = (scaleFactor * mean) / refWeight;
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
      displayStuff("Recal", sfStr, "Done", "");
      delay(3000);
      calibrationSuccess = true;
    }
  }
  tareScale();

}

void newGrind()
{

  displayStuff("New Grind", "", "TARE", "");
  delay(1000);
  tareScale();
  displayStuff("New Grind", "", "Set 15S", "");
  delay(4000);

  double mean = scale.get_units(2);
  while (mean < 300)
  {
    mean = scale.get_units(10);
    float grindTime = 0;
    float grindTimeSingle = 0;
    if (mean > 2)
    {
      grindTime = float(DESIRED_GRIND_MASS) / (mean / 15);
      grindTime = grindTime * 1000;
      grindTimeSingle = float(DESIRED_GRIND_MASS_SINGLE) / (mean / 15);
      grindTimeSingle = grindTime * 1000;
    }
    if (((mean < 1) && (mean > 0.2)) || ((mean > -1) && (mean < -0.2)))
    {
      displayStuff(mean, 2, grindTime, grindTimeSingle, true);
      scale.tare();
    }
    displayStuff(mean, 2, grindTime, true);
  }
  displayStuff("New Grind", "", "Done", "");
  tareScale();

}
