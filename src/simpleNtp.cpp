#include "simpleNtp.hpp"

SimpleNTP::SimpleNTP(const char *ntpServerDns){
    this->ntpServerDns = ntpServerDns;
}

SimpleNTP::~SimpleNTP(){ }

// start listening on a UDP port
uint8_t SimpleNTP::initUDP(){
    Serial.println("Starting UDP.");
    udp.begin(NTP_UDP);
    Serial.printf("Local UDP port: %d\n", udp.localPort());

    // DNS lookup for NTP
    if(!WiFi.hostByName(ntpServerDns, timeServerIP)){
        Serial.println("DNS lookup failed. Rebooting.");
        Serial.flush();
        return -1;
    }
    Serial.printf("Time server IP:  %s\n", timeServerIP.toString().c_str());
    return 0;
}

// get unix time if NTP request completed, else return 0
uint32_t SimpleNTP::getUnixTime(){
    
    if(udp.parsePacket() == 0){
        return 0; // response not completed yet
    }
    udp.read(ntpBuffer, NTP_PACKET_SIZE);

    // use four timestamp bytes to make a 32-bit int as unix time
    uint32_t ntpTime = (ntpBuffer[40] << 24) | (ntpBuffer[41] << 16)
                     | (ntpBuffer[42] << 8)  | ntpBuffer[43];

    // unix time starts 01-01-1970, also account timezone offset
    return ntpTime - 2208988800UL;
}

// send NTP packet to time server asking for current time
void SimpleNTP::sendPacketNTP(){
    Serial.println("Sending NTP packet...");
    memset(ntpBuffer, 0, NTP_PACKET_SIZE);

    // 48 bytes for request, only need to populate first one.
    ntpBuffer[0] = 0b11100011; // LI, version, mode

    // make request for time
    udp.beginPacket(timeServerIP, NTP_UDP);
    udp.write(ntpBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}
