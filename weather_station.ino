#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>
 
#include <WiFiClient.h>

ESP8266WiFiMulti WiFiMulti;
HTTPClient http;

#include <DHTesp.h>

ESP8266WiFiMulti wifiMulti;
DHTesp dht;

char myId[] = "1234567890"; //overwritten in setup() below


/************ START CONFIGURATION ****************/
char ssid[] = "***REMOVED***";
char password[] = "***REMOVED***";
//char uploadIP[] = "***REMOVED***";
//char uploadHost[] = "***REMOVED***"; //for Host: header
char uploadUrlTemplate[] = "http://***REMOVED***/dtgraph/api/add/%s?temperature=%s&humidity=%s&delta_seconds=%d";
//int uploadPort = 80;  //not supporting SSL at this time
//String uploadURL = "/dtgraph/api/add/"; //relative URL base.  See printGET() for query string format.

/** 
 *  Power saving optimization: 
 *  1) Deep sleep between readings.  WIFI is off. (eg 5 minutes).
 *  2) Do not submit (and activate WIFI) every reading, instead save readings for later.
 *  3) Submit every SUBMIT_THRESHOLD readings (eg 3)
 *  4) If submit fails, continue to save readings up to BUFFER_SIZE, eg 30
 *  5) When full, allow oldest readings to drop off the end.
 *  6) Once submit is possible (server/network/wifi comes back), submit all that's queued.
 */

/**  BUFFER_SIZE:
 *  Keep up to this many readings. Normally this will not exceed SUBMIT_THRESHOLD,
 *  but if we can't submit for any reason, queue up to this many and submit when we can
 *  newer readings displace old ones in the buffer, so only the last BUFFER_SIZE readings
 *  are kept.
 */
#define BUFFER_SIZE  30
#define SUBMIT_THRESHOLD 3  //try to submit when we have this many readings
#define READING_INTERVAL 5 //deep sleep (s) between taking readings.

#define DHTPIN 2     // Digital pin connected to the DHT sensor
#define DHT_READ_RETRIES 3

/************ END CONFIGURATION ****************/

typedef struct {
  float temperature;
  float humidity;
  unsigned long time;
} reading;

// A fixed sized ringbuffer for readings.
reading readings[BUFFER_SIZE];

int dataIndex = 0;    // slot to store next reading into
int submitIndex = 0;  // slot to submit (send) next reading from

void incrementSubmitIndex() {
  submitIndex++;
  if (submitIndex >= BUFFER_SIZE) {
    submitIndex = 0;
  }
}
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
  dht.setup(DHTPIN, DHTesp::DHT11); // Connect DHT sensor to GPIO X

  itoa(ESP.getFlashChipId(), myId, 16); //convert int id to hex

  //TODO: move this to a method to run after 
    // Connect to Wi-Fi
  wifiMulti.addAP(ssid, password);
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(".");
  }

  // Print ESP8266 Local IP Address
  Serial.println(WiFi.localIP());


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
      char requestUrl[strlen(uploadUrlTemplate + 50)]; //should be enough for the values 
      char temperatureAsString[10];
      char humidityAsString[10];
      dtostrf(readings[submitIndex].temperature, 4, 2, temperatureAsString);
      dtostrf(readings[submitIndex].humidity, 4, 2, humidityAsString);
      
      Serial.print(F("Committing reading number: "));
      Serial.print(submitIndex);
      Serial.print(F(": "));
      Serial.println(readings[submitIndex].temperature);
      sprintf(
        requestUrl,
        uploadUrlTemplate, 
        myId, 
        temperatureAsString, 
        humidityAsString, 
        (millis() - readings[submitIndex].time) / 1000
      );
      
      http.begin(
        client, 
        requestUrl
      );
  
      int httpCode = http.GET();
      if (httpCode == 200) {
        //if success:
        incrementSubmitIndex();
      } else {
        Serial.print(F("Unable to submit data, got code "));
        Serial.println(httpCode);
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
