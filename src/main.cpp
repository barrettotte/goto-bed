
#include <Wire.h>
#include "SSD1306Wire.h"

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 
#include <WiFiUdp.h>

#include "secrets.h"
#include "icons.h"

// SSD1306 configuration
const int DSPLY_I2C_ADDR = 0x3c;
const int DSPLY_SDA = D3;
const int DSPLY_SCL = D4;

// NTP configuration
const char* NTP_SERVER = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;


SSD1306Wire display(DSPLY_I2C_ADDR, DSPLY_SDA, DSPLY_SCL);

WiFiUDP UDP;
IPAddress timeServerIP;

byte NTPBuffer[NTP_PACKET_SIZE];


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
        display.drawString(64, 10, "Connecting to WiFi");
        display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbol : inactiveSymbol);
        display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbol : inactiveSymbol);
        display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbol : inactiveSymbol);
        display.display();

        counter++;
    }
    display.clear();                            // TODO: temp
    display.drawString(64, 10, "Connected :)"); // TODO: temp
    display.display();                          // TODO: temp

    Serial.printf("\nSuccessfully connected to %s", WiFi.SSID().c_str());
    Serial.printf(" as %s\n", WiFi.localIP().toString().c_str());
}

// connect and start listening to UDP port
void connectUDP(){
    Serial.println("Starting UDP.");
    UDP.begin(123);
    Serial.printf("Local UDP port: %d\n", UDP.localPort());
}

void setup(){
    initSerial();
    initDisplay();
    connectWifi();
    connectUDP();

    
}

void loop(){

    delay(1000);
}