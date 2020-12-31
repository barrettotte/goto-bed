# goto-bed
Making my melatonin bottle vibrate so I know its time to go to bed.

I just wanted a little Arduino project to work on between things, this
seemed perfect.

I also got to learn a couple more valuable concepts and practice designing
an enclosure to 3D print.


## General Concept
- Send NTP request to get current time and resync every 5 minutes
- If current time equals alarm time, trigger vibration motors
- Press function button to acknowledge alarm, stop vibration motors, and reset state
- Take melatonin, get tired, and go to bed at a reasonable time (ideally)


## Set Alarm Time
- Press function button to transition to edit state
- Use potentiometer to set alarm time
- Press function button again to reset back to normal state


## References
- ESP8266_SSD1306 - https://github.com/ThingPulse/esp8266-oled-ssd1306
- NTP packet format - https://tools.ietf.org/html/rfc5905#section-7.3
