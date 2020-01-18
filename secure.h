
/** Send-to server GET url printf string:  ($n is omitted if used in order)
 *  $1: sensorId (our unique id)
 *  $2: temperature (as string)
 *  $3: humidity (as string)
 *  $4: delta_seconds (time since reading was taken, as int)
 *  $5: battery voltage
 *  $6: odometer (readings count since boot)
 */
char uploadUrlTemplate[] = "http://YOUR.HOST.OR.IP/some/uri/%s?unit=C&temperature=%s&humidity=%s&delta_seconds=%d&voltage=%s&odometer=%d";


/** WIFI Auth info **/
typedef struct {
  const char *u;
  const char *p;
} wifi_login;


// Enter your SSID(s) info here
// multiple are supported for failover.
// If you only need one, comment out the second line.

const wifi_login wifi_logins[] = {
  {"FIRSTSSID", "FIRSTPASSWORD"},
  {"SECONDSSID", "SECONDPASSWORD"}
  // and so on...
};



