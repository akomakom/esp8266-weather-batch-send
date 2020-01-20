# ESP8266 DHT11 Batching Remote Weather Sensor

## Goal
* Measure temperature and humidity and send data to some remote server via HTTP
* Conserve battery power as much as possible using **deep sleep**
* Conserve battery power further by **delaying send**

## Approach Summary
Instead of doing the typical "**deep sleep** then **send reading**" loop, this project batches multiple readings
 to save battery power prior to sending all.  
 
 Readings are stored in RTC clock memory between deep sleep cycles,
 not in flash (to prevent repeated write damage).  
 Main memory is lost during deep sleep and cannot be used.
 
 In case of network or server failures, pending readings will be resubmitted until success. A limited number of most recent readings are kept in the buffer. 
 When capacity is exceeded, oldest readings are lost.  The maximum capacity (using RTC memory) is about 60.
 
## Example Configuration

* Take a reading every **5 minutes** and store it.
* Send all pending readings every **30 minutes** (when 6 readings are pending).

Doing this reduces the power-on time considerably since waking up to take a reading takes about 0.2s while sending can take 6s or more,
and of course turning on WIFI takes more power.  With the example configuration above, the total on-time in an hour goes from 72s to 14s (worst case scenario).
 
## Hardware Requirements

* ESP8266 [wired for deep sleep](https://www.instructables.com/id/Enable-DeepSleep-on-an-ESP8266-01/) (GPIO16 to RST)
* DHT11/DHT22 properly connected (Note: plug-n-play DHT11 shields often have an LED that wastes about **4mAh**, removal is recommended)
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
