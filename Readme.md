# ESP8266 DHT11 Batching Remote Sensor

## Goal
* Measure temperature and humidity and send data to some remote server via http.
* Conserve battery power as much as possible using deep sleep
* Conserve battery power further by delaying send.

## Approach Summary
Instead of doing the typical **deep sleep**, **send reading** loop, this project batches multiple readings
 to save battery power prior to sending all.  
 
 Readings are stored in RTC clock memory between deep sleep cycles,
 not in flash (to prevent repeated write damage).  
 Main memory is lost during deep sleep and cannot be used.
 
 In case of network or server failures, pending readings will be resubmitted until success. A limited number of most recent readings are kept in the buffer. 
 When capacity is exceeded, oldest readings are lost.  The maximum capacity (using RTC memory) is about 60.
 
## Example Configuration

* Take a reading every **5 minutes** and store it.
* Send all pending readings every **30 minutes** (when 6 readings are pending).

Doing this reduces the power-on time considerably since waking up to take a reading takes about 0.2ms while sending can take 6+ seconds,
and of course turning on WIFI takes more power.
 
## Hardware Requirements

* ESP8266 wired for deep sleep (GPIO16 to RST)
* DHT11/DHT22 properly connected (Note: pre-made DHT11 shields often have an LED that wastes about **4mAh**, removal is recommended)
* A way to program the ESP8266.
* A remote server of some sort that can accept the data via http

## Software Requirements

* Arduino IDE (tested with 1.8.10)
* Installed in Arduino IDE:
    * ESP8266 support 
    * DHTEsp library 
    * Streaming library
    

## TODO

* Use flash memory if storage capacity is exceeded due to network failure.