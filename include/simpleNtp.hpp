#include <ESP8266WiFi.h>
#include <HardwareSerial.h>
#include <IPAddress.h>
#include <WiFiUdp.h>

#define NTP_PACKET_SIZE  48
#define NTP_UDP          123

class SimpleNTP{

    public:
        SimpleNTP(const char *ntpServerDns);
        ~SimpleNTP();

        uint8_t initUDP();
        uint32_t getUnixTime();
        void sendPacketNTP();

    private:
        WiFiUDP udp;
        uint8_t ntpBuffer[NTP_PACKET_SIZE];
        const char *ntpServerDns;
        IPAddress timeServerIP;
};
