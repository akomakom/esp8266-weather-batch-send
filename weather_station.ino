#include <Arduino.h>
extern "C" {
#include <user_interface.h>
}
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// For DHT11/DHT22
#include <DHTesp.h>

// Because << is prettier than lots of prints
#include <Streaming.h>

#include "config.h"
#include "secure.h"

ADC_MODE(ADC_VCC); //to read supply voltage

ESP8266WiFiMulti wifiMulti;
HTTPClient http;
DHTesp dht;

char myId[] = "1234567890"; //overwritten in setup() below

/************ Configuration is in config.h ****************/
// Since we're storing an array of these in RTC mem, size matters
// Currently it's 4+4, so 500/8 = 60 max items
typedef struct {
  float temperature;
  float humidity;
} reading;

// A fixed sized ringbuffer for readings.
reading readings[BUFFER_SIZE];
// ring buffer rotating positions:
uint32_t dataIndex = 0;    // slot to store next reading into
uint32_t submitIndex = 0;  // slot to submit (send) next reading from

uint32_t odometer = 0;     // just a statistic
// Recycled Globals used in sendData() to minimize heap allocation
char requestUrl[sizeof(uploadUrlTemplate) + sizeof(myId) + 30]; //should be enough for the values + template
char temperatureAsString[8];
char humidityAsString[8];
char voltageAsString[8];

int getPendingDataCount() {
  if (dataIndex >= submitIndex) {
    return dataIndex - submitIndex;
  } else {
    //wrapped around
    return BUFFER_SIZE - submitIndex + dataIndex;
  }
}
/** 
 *  Increment submit index and roll if we're past the end
 */
void incrementSubmitIndex() {
  submitIndex++;
  if (submitIndex >= BUFFER_SIZE) {
    submitIndex = 0;
  }
}
/** 
 *  Increment index to store the nextreading and roll if we're past the end
 *  TODO: if we wrap and pass submit index then we must advance submit index too...
 */
void incrementDataIndex() {
  dataIndex++;
  if (dataIndex >= BUFFER_SIZE) {
    dataIndex = 0;
  }
  if (dataIndex == submitIndex) {
    // we're falling behind on submitting, and dataIndex wrapped and got ahead
    // start losing data and keep submitIndex moving ahead of us.
    incrementSubmitIndex();
  }
  odometer++;
}


void loadStateFromRTC() {
    int index = RTC_STORE_START;
    ESP.rtcUserMemoryRead(index, &odometer, sizeof(dataIndex));
    index += sizeof(odometer)/RTC_BUCKET_SIZE;
    ESP.rtcUserMemoryRead(index, &dataIndex, sizeof(dataIndex));
    index += sizeof(dataIndex)/RTC_BUCKET_SIZE;
    ESP.rtcUserMemoryRead(index, &submitIndex, sizeof(submitIndex));
    index += sizeof(submitIndex)/RTC_BUCKET_SIZE;

    uint32_t checksum; //verify that we arent't getting garbage from memory
    ESP.rtcUserMemoryRead(index, &checksum, sizeof(checksum));
    index += sizeof(submitIndex)/RTC_BUCKET_SIZE;

    Serial << F("Read from RTC: indices: ") << dataIndex << F("/") << submitIndex << endl;
    if (checksum == dataIndex * 100 + submitIndex * 1000) {
        Serial << F("Checksum verified:") << checksum << endl;
    } else {
        dataIndex = 0; // undo changes, this is probably an initial startup
        submitIndex = 0; // and leave the array alone.
        odometer = 0;
        Serial << F("Checksum bad, ignoring data: ") << checksum << endl;
        return;
    }
    for(int i = 0 ; i < BUFFER_SIZE ; i++) {
      ESP.rtcUserMemoryRead(index, ((uint32_t*)(&readings[i])), sizeof(reading));
      Serial << F("Read from RTC: ") << readings[i].temperature << endl;
      index += sizeof(reading)/RTC_BUCKET_SIZE;
    }
}

void saveStateToRTC() {
    int index = RTC_STORE_START;
    ESP.rtcUserMemoryWrite(index, &odometer, sizeof(dataIndex));
    index += sizeof(odometer)/RTC_BUCKET_SIZE;
    ESP.rtcUserMemoryWrite(index, &dataIndex, sizeof(dataIndex));
    index += sizeof(dataIndex)/RTC_BUCKET_SIZE;
    ESP.rtcUserMemoryWrite(index, &submitIndex, sizeof(submitIndex));
    index += sizeof(submitIndex)/RTC_BUCKET_SIZE;
    Serial << F("Saved to RTC: indices: ") << dataIndex << F("/") << submitIndex << endl;

    //checksum.  On first startup, we may get garbage from RTC memory.
    uint32_t checksum = dataIndex * 100 + submitIndex * 1000;
    ESP.rtcUserMemoryWrite(index, &checksum, sizeof(checksum));
    index += sizeof(submitIndex)/RTC_BUCKET_SIZE;

    for(int i = 0 ; i < BUFFER_SIZE ; i++) {
      ESP.rtcUserMemoryWrite(index, ((uint32_t*)(&readings[i])), sizeof(reading));
      Serial << F("Saving to RTC: ") << readings[i].temperature << endl;
      index += sizeof(reading)/RTC_BUCKET_SIZE;
    }
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  int memFootprint = (3*sizeof(uint32_t) + BUFFER_SIZE * sizeof(reading));
  Serial << F("Expected RTC Memory Usage: ") << memFootprint << F("/512 bytes") << endl;
  if (memFootprint > 512) {
    Serial << F("Memory usage excessive!!!! Expect a crash") << endl;
  }

  dht.setup(DHT_PIN, DHT_TYPE); // Connect DHT sensor to GPIO X
  itoa(ESP.getFlashChipId(), myId, 16); //convert int id to hex

  for (int i = 0 ; i < (sizeof(wifi_logins) / sizeof(wifi_logins[0])) ; i++ ) {
    wifiMulti.addAP(wifi_logins[i].u, wifi_logins[i].p);
  }

  http.setReuse(true); //reasonable since we try to batch requests.

  loadStateFromRTC(); //no need to check if it's a deep sleep wake, it always is, even on first boot.
}

void readWeather() {
  int retries = DHT_READ_RETRIES; // try DHT reading this many times if bad
  while(retries > 0) {
    // take a reading from the sensor.
    // always put it directly into array
    // if it's a bad reading, we won't advance the array index
    readings[dataIndex].temperature = dht.getTemperature();
    readings[dataIndex].humidity = dht.getHumidity();
    if (dht.getStatus() != dht.ERROR_NONE || isnan(readings[dataIndex].temperature) || isnan(readings[dataIndex].humidity)) {
      if (FAKE_TEMP_WITHOUT_DHT) {
        Serial << F("Using fake temperature data, no DHT detected") << endl;
        readings[dataIndex].temperature = dataIndex + 55; //predictable consecutive fake temperature
        readings[dataIndex].humidity = random(1,100);
        incrementDataIndex(); //advance the array index for next time, only on success
        retries = 0;
      } else {
        Serial.println(F("Bad temp/humidity reading, retrying"));
        retries--;
      }
    } else {
      //good reading
      retries = 0; //end loop
      incrementDataIndex(); //advance the array index for next time, only on success
    }
  }
}

// Print directly: https://forum.arduino.cc/index.php?topic=44469
// instead of allocating strings.
//void printGET( Stream & s, float temperature, float humidity, unsigned long time)
//{
//  s.print( F("GET ") );
//  s.print( uploadURL );
//  s.print( myId );
//  s.print( F("?temperature=") );
//  s.print( temperature );
//  s.print( F("&humidity=") );
//  s.print( humidity );
//  s.print( F("&delta_seconds=") );
//  s.print( (millis() - time) / 1000);
//  s.println( F(" HTTP/1.1") );
//
//  s.print( F("Host: ") );
//  s.println( uploadHost );
//  s.println( F("User-Agent: esp8266") );
//  s.println( F("Connection: close") );  //TODO: can we keep-alive for multiple submits? or submit multiple in 1 request?
//}

void sendData() {
  WiFiClient client;
  int retries = WIFI_CONNECT_RETRIES;

  Serial << F("Wifi connecting, will send reading (first): ") << readings[submitIndex].temperature << endl;

  // get our supply voltage ready for the url
  dtostrf(((float)ESP.getVcc()) / 1000, 4, 3, voltageAsString);

  uint8_t wifiState;
  while((wifiState = wifiMulti.run()) != WL_CONNECTED && retries > 0) {
    Serial << F(" ... ");
    delay(500);
    retries--;
  }

  retries = HTTP_RETRIES;
  if (wifiState == WL_CONNECTED) {
    
    while(getPendingDataCount() > 0 && retries > 0) {
      // because arduino sprintf doesn't support %f
      dtostrf(readings[submitIndex].temperature, 4, 2, temperatureAsString);
      dtostrf(readings[submitIndex].humidity, 4, 2, humidityAsString);

      Serial << F("Committing reading number: ") << submitIndex << F(": ") << readings[submitIndex].temperature << F(" as string: ") << temperatureAsString << endl;
      sprintf(
        requestUrl,
        uploadUrlTemplate, 
        myId,
        temperatureAsString, 
        humidityAsString,
        (getPendingDataCount() - 1) * READING_INTERVAL,  //With deep sleep we can't rely on time measurement
        voltageAsString,
        odometer - getPendingDataCount() + 1
      );
      Serial << requestUrl << endl;
      
      if (http.begin(client, requestUrl)) {
        int httpCode = http.GET();
        if (httpCode >= 200 && httpCode < 300) {
          //if success:
          incrementSubmitIndex();
          retries = HTTP_RETRIES; // something's working, reset retries.
        } else {
          Serial << F("Unable to submit data, got code ") << httpCode << endl;
          Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          retries--;
        }
        http.end();
      }
    }
    if (getPendingDataCount() > 0) {
      Serial << F("Unable to submit all data, remaining: ") << getPendingDataCount() << endl;
    }
  } else {
    Serial << F("Unable to connect to WIFI: ") << wifiState;
  }
}

/**
 * Note: this loop() method is executed only once, since deep sleep causes a reset.
 * It's kept separate from setup() for clarity.
 * setup() contains initialization half, loop() contains real work.
 */
void loop() {
  Serial << F("Begin loop now.  Pending data count: ") << getPendingDataCount() << endl;
  readWeather();
  
  if (getPendingDataCount() >= SUBMIT_THRESHOLD) {
    sendData();
  }

  saveStateToRTC();
  ESP.deepSleep(READING_INTERVAL * 1000000);
}
