# ESP8266 DHT11/DS18B20 Batching Remote Weather Sensor

Monitors temperature/humidity and periodically sends data (in batches) to a server

## Goal
* Measure temperature and humidity and send data to some remote server via HTTP
* Conserve battery power as much as possible using **deep sleep**
* Conserve battery power further by **batching**, sending all accumulated data periodically. 

## Approach Summary
Instead of doing the typical "**deep sleep** then **send reading**" loop, this project batches multiple readings
 to save battery power prior to sending all.  
 
 Readings are stored in RTC clock memory between deep sleep cycles,
 not in flash (to prevent repeated write damage).  
 Main memory is lost during deep sleep and cannot be used.
 
 In case of network or server failures, pending readings will be resubmitted until success. A limited number of most recent readings are kept in the buffer. 
 When capacity is exceeded, oldest readings are lost.  The maximum capacity (using RTC memory) is about 60 (this can theoretically be doubled if humidity is not needed)
 
## Example Configuration

* Take a reading every **5 minutes** and store it.
* Send all pending readings every **30 minutes** (when 6 readings are pending).

Doing this reduces the power-on time considerably since waking up to take a reading takes about 0.2s while sending can take 6s or more,
and of course turning on WIFI takes more power.  With the example configuration above, the total on-time in an hour goes from 72s to 14s (worst case scenario, less with a static IP).
 
## Hardware Requirements

* ESP8266 [wired for deep sleep](https://www.instructables.com/id/Enable-DeepSleep-on-an-ESP8266-01/) (GPIO16 to RST)
* DHT11/DHT22/DS18B20 properly connected 

    Note: plug-n-play DHT11 shields often have an LED that wastes about **4mAh**, removal is recommended.
    In fact, directly connecting the sensor to the ESP is even better if you don't need a regulator.
* A way to program the ESP8266.
* A remote server of some sort that can accept the data via http.

    This project uses a GET request, but POST/PUT would be a minor code change.

## Software Requirements

* Arduino IDE (tested with 1.8.10)
* Installed in Arduino IDE:
    * ESP8266 support 
    * **DHTEsp** library (if using a DHT)
    * **[DS18B20](https://github.com/matmunk/DS18B2)** library (if using a DS18B20) 
    * **Streaming** library
    

## TODO

* Use flash memory if storage capacity is exceeded due to network failure.


## What actually happens

With the default configuration, the ESP will store 5 readings, then wake up the 6th time, take one more reading, 
and submit all 6.  If successful, it will go to sleep with 0 pending requests (nothing left to submit).

Webserver logs to illustrate how the ESP sends data using the default URL format are below.
Note the batches with **delta_seconds** (time offset) value to indicate age of reading. 
The ESP need not waste its power on querying NTP and knowing what time it is, age is all that's needed.

```
192.168.1.204 - - [22/Jan/2020:08:31:02 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.90&humidity=55.00&delta_seconds=1500&voltage=3.117&odometer=511 HTTP/1.1" 200 250 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:08:31:02 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.20&humidity=39.00&delta_seconds=1200&voltage=3.117&odometer=512 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:08:31:02 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.10&humidity=40.00&delta_seconds=900&voltage=3.117&odometer=513 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:08:31:02 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.10&humidity=40.00&delta_seconds=600&voltage=3.117&odometer=514 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:08:31:03 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.10&humidity=40.00&delta_seconds=300&voltage=3.117&odometer=515 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:08:31:03 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.10&humidity=40.00&delta_seconds=0&voltage=3.117&odometer=516 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"

192.168.1.204 - - [22/Jan/2020:09:00:52 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.50&humidity=55.00&delta_seconds=1500&voltage=3.117&odometer=517 HTTP/1.1" 200 250 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:09:00:53 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.20&humidity=40.00&delta_seconds=1200&voltage=3.117&odometer=518 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:09:00:53 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.20&humidity=40.00&delta_seconds=900&voltage=3.117&odometer=519 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:09:00:53 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.20&humidity=40.00&delta_seconds=600&voltage=3.117&odometer=520 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:09:00:53 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.20&humidity=40.00&delta_seconds=300&voltage=3.117&odometer=521 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"
192.168.1.204 - - [22/Jan/2020:09:00:53 -0500] "GET /dtgraph/api/add/5C:CF:7F:01:02:03?unit=C&temperature=17.30&humidity=40.00&delta_seconds=0&voltage=3.117&odometer=522 HTTP/1.1" 200 249 "-" "ESP8266HTTPClient"

```