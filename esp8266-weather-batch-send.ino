#include <Arduino.h>
extern "C" {
#include <user_interface.h>
}
#include <ESP8266WiFi.h>
// #include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>


// Because << is prettier than lots of prints
#include <Streaming.h>

#include "config.h"
#include "secure.h"

// Include only the required library:
#if SENSOR == SENSOR_DHT
// For DHT11/DHT22
  #include <DHTesp.h>
  DHTesp dht;
#elif SENSOR == SENSOR_DS18B20
  #include <DS18B20.h>
  DS18B20 ds(SENSOR_PIN);
#endif



ADC_MODE(ADC_VCC); //to read supply voltage

// ESP8266WiFiMulti wifiMulti;
HTTPClient http;


/************ Configuration is in config.h ****************/
// Since we're storing an array of these in RTC mem, size matters
// Currently it's 4+4 bytes, and we have 4 other uint32_t in there as well,
// so (512 - 4*4)/8 = 62 max items (BUFFER_SIZE)
// If  you don't want humidity, remove it from the code (everywhere)
// and you should be able to double the BUFFER_SIZE
typedef struct {
  float temperature;
  float humidity;
} reading;

// A fixed sized ringbuffer for readings.
reading readings[BUFFER_SIZE];
// ring buffer rotating positions:
uint32_t dataIndex = 0;    // slot to store next reading into
uint32_t submitIndex = 0;  // slot to submit (send) next reading from

uint32_t odometer = 0;     // just a statistic for fun

// Recycled Globals used in sendData() to minimize heap allocation

// preallocate max string length, should be enough for the values + template:
char requestUrl[sizeof(uploadUrlTemplate) + sizeof(WiFi.macAddress()) + 30];
// these are for float->string conversion later:
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

/**
 * This method is kept in sync with saveStateToRTC()
 */
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

/**
 * This method is kept in sync with loadStateFromRTC()
 */
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
    // so write a checksum so that loadStateFromRTC() can confirm on wake
    uint32_t checksum = dataIndex * 100 + submitIndex * 1000;
    ESP.rtcUserMemoryWrite(index, &checksum, sizeof(checksum));
    index += sizeof(submitIndex)/RTC_BUCKET_SIZE;

    for(int i = 0 ; i < BUFFER_SIZE ; i++) {
      ESP.rtcUserMemoryWrite(index, ((uint32_t*)(&readings[i])), sizeof(reading));
      Serial << F("Saving to RTC: ") << readings[i].temperature << endl;
      index += sizeof(reading)/RTC_BUCKET_SIZE;
    }
}

void wifiOff() {
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();
  delay( 1 );
}

void wifiOn() {
  WiFi.forceSleepWake(); //radio back on at the last second
  delay( 1 );
  // Disable the WiFi persistence.  The ESP8266 will not load and save WiFi settings in the flash memory.
//   WiFi.persistent( false );

#ifdef STATIC_IP
  Serial << F("Setting a static IP: ") << staticIP << endl;
  WiFi.config(staticIP, gateway, subnet);
#endif
//   WiFi.mode( WIFI_STA );

  WiFi.begin( WLAN_SSID, WLAN_PASSWD );

  int retries = WIFI_CONNECT_RETRIES;
  while(WiFi.status() != WL_CONNECTED && retries > 0) {
    Serial << F(" ... ");
    delay(WIFI_CONNECT_DELAY);
    retries--;
  }
}

void setup() {
  wifiOff();  // save power while we do non-network work.

  // Serial port for debugging purposes
  Serial.begin(115200);
  int memFootprint = (3*sizeof(uint32_t) + BUFFER_SIZE * sizeof(reading));
  Serial << F("Expected RTC Memory Usage: ") << memFootprint << F("/512 bytes") << endl;
  if (memFootprint > 512) {
    Serial << F("Memory usage excessive!!!! Expect a crash") << endl;
  }

  #if SENSOR == SENSOR_DHT
  dht.setup(SENSOR_PIN, DHT_TYPE); // Connect DHT sensor to GPIO X
  #endif

  Serial << F("Using MAC as unique id: ") << WiFi.macAddress() << endl;

  http.setReuse(true); //reasonable since we try to batch requests.

  loadStateFromRTC(); //no need to check if it's a deep sleep wake, it always is, even on first boot.
}


boolean isVoltageOK() {
  return (ESP.getVcc() >= MIN_VOLTAGE);
}


/**
 * take a reading from the sensor via the appropriate library.
 * always put it directly into array
 * if it's a bad reading, we won't advance the array index
 */
void readWeather() {
  int retries = SENSOR_READ_RETRIES; // try DHT reading this many times if bad
  if (!isVoltageOK()) {
    Serial << F("Voltage too low, not reading temperature");
    return;
  }
  while(retries > 0) {

    bool readingOK = false;

    #if SENSOR == SENSOR_DHT
      readings[dataIndex].temperature = dht.getTemperature();
      readings[dataIndex].humidity = dht.getHumidity();
      readingOK = dht.getStatus() == dht.ERROR_NONE && !isnan(readings[dataIndex].temperature) && !isnan(readings[dataIndex].humidity);
    #elif SENSOR == SENSOR_DS18B20
      if (ds.selectNext()) {
        readings[dataIndex].temperature = ds.getTempC();
        readings[dataIndex].humidity = 0.0;
        readingOK = !isnan(readings[dataIndex].temperature);
      } //else readingOK is false
    #endif
    if (!readingOK) {
      if (FAKE_TEMP_WITHOUT_SENSOR) {
        Serial << F("Using fake temperature data, no sensors detected") << endl;
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

/**
 * Send all pending data, one item per request
 */
void sendData() {
  WiFiClient client;
  Serial << F("Wifi connecting, will send reading (first): ") << readings[submitIndex].temperature << endl;

  // get our supply voltage ready for the url
  dtostrf(((float)ESP.getVcc()) / 1000, 4, 3, voltageAsString);

  wifiOn();
  int retries = HTTP_RETRIES;
  if (WiFi.status() == WL_CONNECTED) {
    
    while(getPendingDataCount() > 0 && retries > 0) {
      // because arduino sprintf doesn't support %f
      dtostrf(readings[submitIndex].temperature, 4, 2, temperatureAsString);
      dtostrf(readings[submitIndex].humidity, 4, 2, humidityAsString);

      Serial << F("Committing reading number: ") << submitIndex << F(": ") << readings[submitIndex].temperature << F(" as string: ") << temperatureAsString << endl;
      sprintf(
        requestUrl,
        uploadUrlTemplate, 
        WiFi.macAddress().c_str(),
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
    Serial << F("Unable to connect to WIFI: ") << WiFi.status();
  }
   wifiOff();
}

/**
 * Note: this loop() method is executed only once, since deep sleep causes a reset,
 * and execution starts over from the top on wake.
 * It's kept separate from setup() for clarity:
 *   setup() contains initialization half
 *   loop() contains real work.
 */
void loop() {
  Serial << F("Begin loop now.  Pending data count: ") << getPendingDataCount() << endl;
  readWeather();
  
  if (getPendingDataCount() >= SUBMIT_THRESHOLD) {
    sendData();
  }

  saveStateToRTC();
  ESP.deepSleep(READING_INTERVAL * 1000000);

  // WAKE_RF_DISABLED doesn't seem to help.  deep sleep with WAKE_RF_DISABLED
  // means that radio is completely disabled, unless we deep sleep again
  // to re-enable it, but the point is to wake, take a reading and only then enable WiFi
  // making things complicated.  For ultimate power saving we could wake, store readings,
  // sleep again, wake with radio on and submit.  But meh.
//   ESP.deepSleep(READING_INTERVAL * 1000000, WAKE_RF_DISABLED );
}
