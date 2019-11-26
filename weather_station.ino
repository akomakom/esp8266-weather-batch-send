#include <WiFi.h>
#include <WiFiClient.h>

#include <DHTesp.h>

#include <dummy.h>

#define DHTPIN 2     // Digital pin connected to the DHT sensor
#define DHT_READ_RETRIES 3

DHTesp dht;

char myId[] = "1234567890"; //overwritten in setup() below


/************ START CONFIGURATION ****************/
char ssid[] = "***REMOVED***";
char password[] = "***REMOVED***";
char uploadIP[] = "***REMOVED***";
char uploadHost[] = "***REMOVED***"; //for Host: header
int uploadPort = 80;
String uploadURL = "/dtgraph/api/add/";

/** 
 *  Keep up to this many readings. Normally this will not exceed SUBMIT_THRESHOLD,
 *  but if we can't submit for any reason, queue up to this many and submit when we can
 *  newer readings displace old ones in the buffer, so only the last BUFFER_SIZE readings
 *  are kept.
 */
#define BUFFER_SIZE  30
#define SUBMIT_THRESHOLD 3  //try to submit when we have this many
#define READING_INTERVAL 5 //deep sleep (s) between taking readings.

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
  WiFi.begin(ssid, password);
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
void printGET( Stream & s, float temperature, float humidity, unsigned long time)
{
  s.print( F("GET ") );
  s.print( uploadURL );
  s.print( myId );
  s.print( F("?temperature=") );
  s.print( temperature );
  s.print( F("&humidity=") );
  s.print( humidity );
  s.print( F("&delta_seconds=") );
  s.print( (millis() - time) / 1000);
  s.println( F(" HTTP/1.1") );

  s.print( F("Host: ") );
  s.println( uploadHost );
  s.println( F("User-Agent: esp8266") );
  s.println( F("Connection: close") );  //TODO: can we keep-alive for multiple submits? or submit multiple in 1 request?
}

void sendData() {
  while(getPendingDataCount() > 0) {
    
    Serial.print(F("Committing reading number: "));
    Serial.print(submitIndex);
    Serial.print(F(": "));
    Serial.println(readings[submitIndex].temperature);

    // Print directly: https://forum.arduino.cc/index.php?topic=444696.0
    //String url = uploadURL + myId + "?temperature=" + temperatures[i] + "&humidity=" + humidities[i] + "&delta_seconds=" + ((BUFFER_SIZE - i - 1) * READING_INTERVAL);
    printGET(Serial, readings[submitIndex].temperature, readings[submitIndex].humidity, readings[submitIndex].time);

    //TODO: printGET to WIFI also
    //if success:
    incrementSubmitIndex();
  }
}

void loop() {

  readWeather();
  
  if (getPendingDataCount() >= SUBMIT_THRESHOLD) {
    sendData();
  }

  ESP.deepSleep(READING_INTERVAL * 1000000);
  
}
