#include <WiFi.h>
#include <WiFiClient.h>

#include <DHTesp.h>

#include <dummy.h>

#define DHTPIN 5     // Digital pin connected to the DHT sensor

DHTesp dht;


// Replace with your network credentials
char ssid[] = "***REMOVED***";
char password[] = "***REMOVED***";
char uploadIP[] = "***REMOVED***";
char uploadHost[] = "***REMOVED***";
int uploadPort = 80;
String uploadURL = "/dtgraph/api/add/";
char myId[] = "1234567890"; //set in setup() below


const int bufferSize = 3;  //submit once we have this many readings
const int sleepSeconds = 5; //sleep (s) between readings.

float temperatures[bufferSize];
float humidities[bufferSize];
int currentIndex = 0;


void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  dht.setup(DHTPIN, DHTesp::DHT11); // Connect DHT sensor to GPIO X

  itoa(ESP.getFlashChipId(), myId, 16); //convert int id to hex
  
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
  int retries = 3;

  while(retries > 0) {
    // take a reading from the sensor.
    // always put it directly into array
    // if it's a bad reading, we'll skip it on upload
    temperatures[currentIndex] = dht.getTemperature(); 
    humidities[currentIndex] = dht.getHumidity();
  
    if (isnan(temperatures[currentIndex]) || isnan(humidities[currentIndex])) {
      Serial.println(F("Bad temp/humidity reading"));
      retries--;
    } else {
      //good reading
      retries = 0;
    }
  }  
}

// Print directly: https://forum.arduino.cc/index.php?topic=44469
// instead of allocating strings.
void printGET( Stream & s, float temperature, float humidity, int delta )
{
  s.print( F("GET ") );
  s.print( uploadURL );
  s.print( myId );
  s.print( F("?temperature=") );
  s.print( temperature );
  s.print( F("&humidity=") );
  s.print( humidity );
  s.print( F("&delta_seconds=") );
  s.print( delta);
  s.println( F(" HTTP/1.1") );

  s.println( F("Host: ") );
  s.println( uploadHost );
  s.println( F("User-Agent: esp8266") );
  s.println( F("Connection: close") );
}

void sendData() {
  for(int i = 0 ; i < bufferSize ; i++) {
    if (!isnan(temperatures[i]) && !isnan(humidities[i])) {
      Serial.print(F("Committing reading: "));
      Serial.println(temperatures[i]);

      // Print directly: https://forum.arduino.cc/index.php?topic=444696.0
      //String url = uploadURL + myId + "?temperature=" + temperatures[i] + "&humidity=" + humidities[i] + "&delta_seconds=" + ((bufferSize - i - 1) * sleepSeconds);
      printGET(Serial, temperatures[i], humidities[i], (bufferSize - i - 1) * sleepSeconds);

      //TODO: printGET to WIFI also
    }
  }
}

void loop() {

  readWeather();
  currentIndex++;
  
  if (currentIndex == bufferSize) {
    sendData();
    currentIndex = 0;
  }
  
}
