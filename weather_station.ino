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

bool fakeMode = false;

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
    //index += sizeof(readings);
}

// Attempt to determine whether a DHT is connected
void initDHT() {
  dht.setup(DHT_PIN, DHT_TYPE); // Connect DHT sensor to GPIO X
  uint32_t temperature =  dht.getTemperature();
  fakeMode = FAKE_TEMP_WITHOUT_DHT && (dht.getStatus() != dht.ERROR_NONE);
  Serial << F("Intializing.  Checking DHT: ") << temperature << endl;
  if (fakeMode) {
    Serial << F("Using fake temperature data, no DHT detected") << endl;
  }
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  initDHT();

  itoa(ESP.getFlashChipId(), myId, 16); //convert int id to hex

  for (int i = 0 ; i < (sizeof(wifi_logins) / sizeof(wifi_logins[0])) ; i++ ) {
    wifiMulti.addAP(wifi_logins[i].u, wifi_logins[i].p);
  }

//   wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
  http.setReuse(true); //reasonable since we try to batch requests.

//   rst_info *resetInfo;
//   resetInfo = ESP.getResetInfoPtr();
//   Serial.println((*resetInfo).reason);
//
//   // This is useless, it will always happen, ESP8266 wakes from deep sleep using reset
//   // so initial start is identical to deep sleep wake.
//   // see checksum in saveStateToRTC for a solution.
//   if (resetInfo->reason == REASON_DEEP_SLEEP_AWAKE) {
//     Serial << F("Detected deep sleep wake, loading state: ") << resetInfo->reason << ESP.getResetReason() << endl;
//     loadStateFromRTC();
//   }

  loadStateFromRTC(); //no need to check if it's a deep sleep wake, it always is.
}

void readWeather() {
  if (fakeMode) {
    readings[dataIndex].temperature = dataIndex + 77;
    readings[dataIndex].humidity = random(1,100);
    incrementDataIndex(); //advance the array index for next time, only on success
    return;
  }

  int retries = DHT_READ_RETRIES; // try DHT reading this many times if bad
  while(retries > 0) {
    // take a reading from the sensor.
    // always put it directly into array
    // if it's a bad reading, we won't advance the array index
    readings[dataIndex].temperature = dht.getTemperature();
    readings[dataIndex].humidity = dht.getHumidity();
    if (dht.getStatus() != dht.ERROR_NONE || isnan(readings[dataIndex].temperature) || isnan(readings[dataIndex].humidity)) {
      Serial.println(F("Bad temp/humidity reading"));
      retries--;
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

  Serial << F("Will send reading: ") << readings[submitIndex].temperature << endl;

  dtostrf(((float)ESP.getVcc()) / 1000, 4, 3, voltageAsString);

  while(wifiMulti.run() != WL_CONNECTED && retries > 0) {
    Serial << F("Not connected, waiting ... ");
    delay(5000);
    retries--;
  }

  retries = HTTP_RETRIES;
  uint8_t wifiState = wifiMulti.run();
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
          retries++; // something's working, we can afford an extra retry if things go bad later.
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

void loop() {
  Serial << F("Begin loop now.  Pending data count: ") << getPendingDataCount() << endl;
  readWeather();
  
  if (getPendingDataCount() >= SUBMIT_THRESHOLD) {
    sendData();
  }

  saveStateToRTC();
  Serial << "Invoking deep sleep now for " << READING_INTERVAL << endl; //this print seems to prevent panic crashes???
  ESP.deepSleep(READING_INTERVAL * 1000000);
//   delay(READING_INTERVAL * 1000);
}
