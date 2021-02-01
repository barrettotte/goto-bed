#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SSD1306Wire.h>
#include <Timezone.h>
#include <WiFiClientSecure.h> 
#include <Wire.h>

#include "secrets.h"
#include "icons.h"
#include "simpleNtp.hpp"

// Pins
const uint8_t DSPLY_SDA = D3;
const uint8_t DSPLY_SCL = D4;
const uint8_t VIBR_1    = D2;
const uint8_t EN_SWITCH = D7;
const uint8_t FUNC_BTN  = D8;
const uint8_t ALARM_POT = A0;

// Program states
const uint8_t STATE_NORMAL = 0;
const uint8_t STATE_ALARM  = 1;
const uint8_t STATE_EDIT   = 2;

// Type definitions
typedef struct alarm_t{
    uint8_t incPerHour;
    uint16_t val;
    uint16_t step;
    uint8_t hour;
    uint8_t min;
} Alarm_t;

// Misc Config
const char* NTP_SERVER = "time.nist.gov";
const uint32_t WAIT_NTP_REQ = 300000; // every 5 mins NTP sync
const uint16_t WAIT_STATE_CHK = 500;  // check state change every 0.5 secs
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240}; // EDT = UTC-4h
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};  // EST = UTC-5h
const uint8_t DSPLY_I2C_ADDR = 0x3c;

// Globals
Alarm_t *alarm;
SimpleNTP simpleNtp(NTP_SERVER);
SSD1306Wire display(DSPLY_I2C_ADDR, DSPLY_SDA, DSPLY_SCL);
TimeChangeRule* tcr;
Timezone myTZ(myDST, mySTD);

uint8_t pgmState = STATE_NORMAL;
uint8_t vibrate = 0;
uint32_t unixTime = 0;
ulong previousMillis = 0;
ulong prevNtp = 0;
ulong lastNtpResp = millis();
ulong prevActualTime = 0;


// initialize serial
void initSerial(){
    Serial.begin(115200);
    Serial.println();

    for(uint8_t t = 3; t > 0; t--){
        Serial.printf("WAIT %d...\n", t);
        Serial.flush();
        delay(500);
    }
    Serial.println("\n* * * START * * *");
}

// initialize SSD1306 display
void initDisplay(){
    display.init();
    display.clear();
    display.display();
    // display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);
    display.setFont(ArialMT_Plain_10);
}

// connect to WiFi using secrets
void connectWifi(){
    uint32_t counter = 0;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    Serial.print("Attempting to connect to WiFi");

    while(WiFi.status() != WL_CONNECTED){
        delay(500);
        Serial.print(".");
        display.clear();
        display.drawString(64, 14, "Connecting to WiFi");
        display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbol : inactiveSymbol);
        display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbol : inactiveSymbol);
        display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbol : inactiveSymbol);
        display.display();
        counter++;
    }
    Serial.printf("\nSuccessfully connected to %s as %s\n", 
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// round number n to nearest multiple i
int roundToNearest(uint32_t n, uint32_t i){
    uint32_t a = (n / i) * i;
    uint32_t b = a + i;
    return (n - a > b - n) ? b : a;
}

// read analog potentiometer value
void readAlarmPot(){
    alarm->val = roundToNearest(analogRead(ALARM_POT) - alarm->step, alarm->step) / alarm->step;
    uint8_t hour = alarm->val / alarm->incPerHour;
    uint8_t min = (alarm->val % alarm->incPerHour) * (60 / alarm->incPerHour);
    
    // clamp time 00:00 to 23:30
    if(hour < 0){
        hour = 0;
        min = 0;
    } else if(hour > 23){
        hour = 23;
        min = 60 - (60 / alarm->incPerHour);
    }
    alarm->hour = hour;
    alarm->min = min;
}

void setup(){
    pinMode(VIBR_1, OUTPUT);
    pinMode(EN_SWITCH, INPUT);
    pinMode(FUNC_BTN, INPUT);

    initSerial();
    initDisplay();
    connectWifi();
    delay(250);

    if(simpleNtp.initUDP() != 0){
        ESP.reset(); // UDP init failed, reset ESP module
    }
    simpleNtp.sendPacketNTP();
    display.clear();

    alarm = (Alarm_t *) malloc(sizeof(Alarm_t));
    alarm->incPerHour = 2;
    alarm->step = (uint16_t) round(1000 / (23 * alarm->incPerHour));
    readAlarmPot();
}

// display alarm to screen
void drawAlarm(){
    char buffer[16];
    sprintf(buffer, "%02d:%02d", alarm->hour, alarm->min);

    if(pgmState == STATE_EDIT){
        display.clear();
        display.setFont(ArialMT_Plain_24);
        display.drawString(64, 20, buffer);
        display.display();
    } else {
        display.setFont(ArialMT_Plain_10);
        display.drawString(16, 4, buffer);
    } 
}

// request time from NTP and trigger alarm if its time to go to bed
void processTime(ulong currentMillis){
    if(currentMillis - prevNtp > WAIT_NTP_REQ && pgmState == STATE_NORMAL){
        prevNtp = currentMillis;
        simpleNtp.sendPacketNTP();
    }
    uint32_t tempTime = simpleNtp.getUnixTime();

    if(tempTime){
        unixTime = myTZ.toLocal(tempTime, &tcr); // convert unix time to local timezone
        Serial.printf("NTP response:  %d\n", unixTime);
        lastNtpResp = currentMillis;
    } else if((currentMillis - lastNtpResp) > 3600000){
        Serial.println("More than an hour since last NTP request. Rebooting...");
        Serial.flush();
        ESP.reset();
    }
    uint32_t actualTime = unixTime + (currentMillis - lastNtpResp) / 1000;

    if(actualTime != prevActualTime && unixTime != 0){
        char buffer[16];
        uint8_t actualHour = actualTime / 3600 % 24;
        uint8_t actualMin = actualTime / 60 % 60;
        uint8_t actualSec = actualTime % 60;

        // trigger alarm if its time
        if(pgmState == STATE_NORMAL && actualHour == alarm->hour 
          && actualMin == alarm->min && actualSec == 0){
            pgmState = STATE_ALARM;
        }
        prevActualTime = actualTime;
        sprintf(buffer, "%02d:%02d:%02d", actualHour, actualMin, actualSec);

        display.clear();
        display.setFont(ArialMT_Plain_24);
        display.drawString(64, 24, buffer);
        drawAlarm();
        display.display();
    }
}

// handle program state change when function button pressed
void funcBtnPressed(){
    if(pgmState == STATE_ALARM){
        pgmState = STATE_NORMAL;
        vibrate = 0;
        digitalWrite(VIBR_1, vibrate);
    } else if(pgmState == STATE_NORMAL){
        pgmState = STATE_EDIT;
    } else if(pgmState == STATE_EDIT){
        pgmState = STATE_NORMAL;
    }
}

// trigger vibration motors and display message
void handleAlarm(){
    vibrate = !vibrate; // toggle vibration every other tick
    digitalWrite(VIBR_1, vibrate);

    display.setFont(ArialMT_Plain_16);
    display.clear();
    display.drawString(64, 14, "GO TO BED!");
    display.display();
}

// perform action(s) based on current state
void performAction(ulong currentMillis){
    if(pgmState == STATE_NORMAL){
        display.displayOn();
        processTime(currentMillis);
    } else if(pgmState == STATE_ALARM){
        handleAlarm();
    } else if(pgmState == STATE_EDIT){
        readAlarmPot();
        drawAlarm();
    }
}

void loop(){
    ulong currentMillis = millis();

    if(currentMillis - previousMillis >= WAIT_STATE_CHK){
        previousMillis = currentMillis;

        // transition to different state if needed
        if(digitalRead(FUNC_BTN)){
            funcBtnPressed();
        }
        performAction(currentMillis);
    }
}
