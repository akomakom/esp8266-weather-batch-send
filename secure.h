// This file contains sensitive parts of the configuration that should not be
// checked into to git

/** Send-to server GET url printf string:  ($n is omitted if used in order)
 * This is the URL of your server that accepts data via a GET request.
 *
 *  $1: sensorId (our unique id)
 *  $2: temperature (as string)
 *  $3: humidity (as string)
 *  $4: delta_seconds (time since reading was taken, as int)
 *  $5: battery voltage
 *  $6: odometer (readings count since boot)
 */
char uploadUrlTemplate[] = "http://SOME.IP/some/path/%s?unit=C&temperature=%s&humidity=%s&delta_seconds=%d&voltage=%s&odometer=%d";

#define WLAN_SSID     "YOUR_SSID"
#define WLAN_PASSWD   "YOUR_PASSWORD"

// optional static IP configuration
IPAddress staticIP(192,168,1,222);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
#define STATIC_IP  //comment out this line to use DHCP

