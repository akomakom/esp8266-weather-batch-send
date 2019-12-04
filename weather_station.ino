#include <Arduino.h>


#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// For DHT11/DHT22
#include <DHTesp.h>

// Because << is prettier than lots of prints
#include <Streaming.h>

#include "config.h"

ESP8266WiFiMulti wifiMulti;
HTTPClient http;
DHTesp dht;

char myId[] = "1234567890"; //overwritten in setup() below

/************ Configuration is in config.h ****************/

typedef struct {
  float temperature;
  float humidity;
  unsigned long time;
} reading;

// A fixed sized ringbuffer for readings.
reading readings[BUFFER_SIZE];

// ring buffer rotating positions:
int dataIndex = 0;    // slot to store next reading into
int submitIndex = 0;  // slot to submit (send) next reading from

// Recycled Globals used in sendData() to minimize heap allocation
char requestUrl[sizeof(uploadUrlTemplate) + sizeof(myId) + 30]; //should be enough for the values + template
char temperatureAsString[6];  //TODO: check if 6 is enough with 4+2
char humidityAsString[6];
    
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
}

int getPendingDataCount() {
  return abs(dataIndex - submitIndex);
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHT_TYPE); // Connect DHT sensor to GPIO X

  itoa(ESP.getFlashChipId(), myId, 16); //convert int id to hex

  //TODO: move this to a method to run after 
    // Connect to Wi-Fi
  wifiMulti.addAP(ssid, password);
  http.setReuse(true); //reasonable since we try to batch requests.
}

void readWeather() {
  int retries = DHT_READ_RETRIES; // try DHT reading this many times if bad

  while(retries > 0) {
    // take a reading from the sensor.
    // always put it directly into array
    // if it's a bad reading, we won't advance the array index
    readings[dataIndex].temperature = dht.getTemperature(); 
    readings[dataIndex].humidity = dht.getHumidity();
    readings[dataIndex].time = millis(); 
    
    if (isnan(readings[dataIndex].temperature) || isnan(readings[dataIndex].humidity)) {
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
  if (wifiMulti.run() == WL_CONNECTED) {
    
    while(getPendingDataCount() > 0) {
      // because arduino sprintf doesn't support %f
      dtostrf(readings[submitIndex].temperature, 4, 2, temperatureAsString);
      dtostrf(readings[submitIndex].humidity, 4, 2, humidityAsString);
      
      Serial << F("Committing reading number: ") << submitIndex << F(": ") << readings[submitIndex].temperature << endl;
      sprintf(
        requestUrl,
        uploadUrlTemplate, 
        myId, 
        temperatureAsString, 
        humidityAsString, 
        (millis() - readings[submitIndex].time) / 1000
      );
      
      http.begin(client, requestUrl);
      int httpCode = http.GET();
      if (httpCode >= 200 && httpCode < 300) {
        //if success:
        incrementSubmitIndex();
      } else {
        Serial << F("Unable to submit data, got code ") << httpCode << endl;
      }
    }
  } else {
    Serial.println(F("Unable to connect to WIFI"));
  }
}

void loop() {

  readWeather();
  
  if (getPendingDataCount() >= SUBMIT_THRESHOLD) {
    sendData();
  }

  ESP.deepSleep(READING_INTERVAL * 1000000);
  
}
