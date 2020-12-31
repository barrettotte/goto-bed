#include <Arduino.h>
#include <Timezone.h>

#include <Wire.h>
#include "SSD1306Wire.h"

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 
#include <WiFiUdp.h>

#include "secrets.h"
#include "icons.h"

// Pins
const uint8_t DSPLY_SDA = D3;
const uint8_t DSPLY_SCL = D4;
const uint8_t VIBR_1    = D2;
const uint8_t EN_SWITCH = D7;
const uint8_t FUNC_BTN   = D8;
const uint8_t ALARM_POT = A0;

// Program states
const uint16_t WAIT_STATE_CHK = 500;
const uint8_t STATE_NORMAL = 0;
const uint8_t STATE_SLEEP = 1;
const uint8_t STATE_ALARM = 2;
const uint8_t STATE_EDIT = 3;

// NTP and display configuration
const uint8_t NTP_UDP = 123;
const char* NTP_SERVER = "time.nist.gov";
const uint8_t NTP_PACKET_SIZE = 48;
const uint32_t WAIT_NTP_REQ = 300000; // every 5 mins NTP sync

const int DSPLY_I2C_ADDR = 0x3c;

// Timezone configuration
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240}; // EDT = UTC-4h
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};  // EST = UTC-5h
Timezone myTZ(myDST, mySTD);

// Globals
SSD1306Wire display(DSPLY_I2C_ADDR, DSPLY_SDA, DSPLY_SCL);
WiFiUDP UDP;
IPAddress timeServerIP;

uint8_t pgmState;
byte NTPBuffer[NTP_PACKET_SIZE];

// alarm pot
uint8_t alarmIncPerHour = 4;
uint16_t alarmPotVal = 0;
uint16_t alarmPotStep = (uint16_t) round(1000 / (23 * alarmIncPerHour));
uint8_t alarmHour = 0;
uint8_t alarmMin = 0;

// time
unsigned long previousMillis = 0;
uint8_t vibrate = 0;
uint8_t prevState = 0;


unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t unixTime = 0;
unsigned long prevActualTime = 0;

TimeChangeRule* tcr;
char displayBuffer[16];


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

    display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);
    display.setFont(ArialMT_Plain_10);

    Serial.println("Display initialized.");
}

// connect to WiFi using secrets
void connectWifi(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PWD);

    int counter = 0;
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

    display.setFont(ArialMT_Plain_24);
    delay(500);
}

// start listening on a UDP port
void initUDP(int port){
    Serial.println("Starting UDP.");
    UDP.begin(port);
    Serial.printf("Local UDP port: %d\n", UDP.localPort());
    delay(500);
}

// get unix time if NTP request completed, else return 0
uint32_t getUnixTime(){
    if(UDP.parsePacket() == 0){
        return 0; // response not completed yet
    }
    UDP.read(NTPBuffer, NTP_PACKET_SIZE);

    // use four timestamp bytes to make a 32-bit int as unix time
    uint32_t NTPTime = (NTPBuffer[40] << 24) 
                     | (NTPBuffer[41] << 16)
                     | (NTPBuffer[42] << 8) 
                     | NTPBuffer[43];

    // unix time starts 01-01-1970, also account timezone offset
    return NTPTime - 2208988800UL;
}

// send NTP packet to time server asking for current time
void sendPacketNTP(IPAddress &addr){
    Serial.println("Sending NTP packet...");
    memset(NTPBuffer, 0, NTP_PACKET_SIZE);

    // 48 bytes for request, only need to populate first one.
    NTPBuffer[0] = 0b11100011; // LI, version, mode

    // make request for time
    UDP.beginPacket(addr, NTP_UDP);
    UDP.write(NTPBuffer, NTP_PACKET_SIZE);
    UDP.endPacket();
}

// round number n to nearest multiple i
int roundToNearest(int n, int i){
    int a = (n / i) * i;
    int b = a + i;
    return (n - a > b - n) ? b : a;
}

// read analog potentiometer value
void readAlarmPot(){
    alarmPotVal = roundToNearest(analogRead(ALARM_POT) - alarmPotStep, alarmPotStep) / alarmPotStep;
    alarmHour = alarmPotVal / alarmIncPerHour;
    alarmMin = (alarmPotVal % alarmIncPerHour) * (60 / alarmIncPerHour);
    
    // clamp time 00:00 to 23:30
    if(alarmHour < 0){
        alarmHour = 0;
        alarmMin = 0;
    } else if(alarmHour > 23){
        alarmHour = 23;
        alarmMin = 60 - (60 / alarmIncPerHour);
    }
}

void setup(){
    pgmState = STATE_NORMAL;
    pinMode(VIBR_1, OUTPUT);
    pinMode(EN_SWITCH, INPUT);
    pinMode(FUNC_BTN, INPUT);

    initSerial();
    initDisplay();
    connectWifi();
    initUDP(NTP_UDP);

    // DNS lookup for NTP
    if(!WiFi.hostByName(NTP_SERVER, timeServerIP)){
        Serial.println("DNS lookup failed. Rebooting.");
        Serial.flush();
        ESP.reset();
    }
    Serial.printf("Time server IP:  %s\n", timeServerIP.toString().c_str());
    sendPacketNTP(timeServerIP);
    
    display.clear();
    readAlarmPot(); // set alarm initial value
    delay(500);
}

// display alarm to screen
void drawAlarm(){
    sprintf(displayBuffer, "%02d:%02d", alarmHour, alarmMin);

    if(pgmState == STATE_EDIT){
        display.clear();
        display.setFont(ArialMT_Plain_24);
        display.drawString(64, 20, displayBuffer);
        display.display();
    } else {
        display.setFont(ArialMT_Plain_10);
        display.drawString(16, 4, displayBuffer);
    } 
}

// request time from NTP and trigger alarm if its time to go to bed
void updateTime(unsigned long currentMillis){

    // wait a bit before requesting time again
    if(currentMillis - prevNTP > WAIT_NTP_REQ && pgmState == STATE_NORMAL){
        prevNTP = currentMillis;
        sendPacketNTP(timeServerIP);
    }

    uint32_t tempTime = getUnixTime();

    if(tempTime){
        unixTime = myTZ.toLocal(tempTime, &tcr); // convert unix time to local timezone
        Serial.printf("NTP response:  %d\n", unixTime);
        lastNTPResponse = currentMillis;
    } else if((currentMillis - lastNTPResponse) > 3600000){
        // something has gone wrong, need to reset
        Serial.println("More than an hour since last NTP request. Rebooting...");
        Serial.flush();
        ESP.reset();
    }

    uint32_t actualTime = unixTime + (currentMillis - lastNTPResponse) / 1000;

    if(actualTime != prevActualTime && unixTime != 0){
        uint8_t actualHour = actualTime / 3600 % 24;
        uint8_t actualMin = actualTime / 60 % 60;
        uint8_t actualSec = actualTime % 60;

        if(pgmState == STATE_NORMAL && actualHour == alarmHour && actualMin == alarmMin && actualSec == 0){
            pgmState = STATE_ALARM;
        }
        prevActualTime = actualTime;
        sprintf(displayBuffer, "%02d:%02d:%02d", actualHour, actualMin, actualSec);

        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.drawString(64, 20, displayBuffer);
        drawAlarm();
        display.display();
    }
}

// TODO: test
int alarmCounter = 0;

// handle program state when function button pressed
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

void loop(){
    unsigned long currentMillis = millis();

    // apply state changes if enough time has passed
    if(currentMillis - previousMillis >= WAIT_STATE_CHK){
        prevState = pgmState;
        previousMillis = currentMillis;

        // update states if needed
        if(!digitalRead(EN_SWITCH)){
            pgmState = STATE_SLEEP;
        } else if(prevState == STATE_SLEEP){
            pgmState = STATE_NORMAL;
        } else if(digitalRead(FUNC_BTN)){
            funcBtnPressed();
        }

        // perform action(s) based on current state
        if(pgmState == STATE_NORMAL){
            display.displayOn();
            updateTime(currentMillis);
        } else if(pgmState == STATE_ALARM){
            vibrate = !vibrate; // toggle vibration every other tick
            digitalWrite(VIBR_1, vibrate);
            display.setFont(ArialMT_Plain_16);
            display.clear();
            display.drawString(64, 14, "GO TO BED!");
            display.display();
        } else if(pgmState == STATE_SLEEP){
            display.displayOff();
            vibrate = 0;
            digitalWrite(VIBR_1, vibrate);
        } else if(pgmState == STATE_EDIT){
            readAlarmPot();
            drawAlarm();
        }
    }
}
