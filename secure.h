/** WIFI **/
typedef struct {
  const char *u;
  const char *p;
} wifi_login;


// Enter your SSID(s) info here
// multiple are supported for failover.
// If you only need one, comment out the second line.

const wifi_login wifi_logins[] = {
  {"FIRSTSSID", "FIRSTPASSWORD"},
  {"SECONDSSID, "SECONDPASSWORD"}
  // and so on...
};