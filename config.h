

/** Send-to server GET url printf string:  ($n is omitted if used in order)
 *  $1: temperature (as string)
 *  $2: humidity (as string)
 *  $3: delta_seconds (time since reading was taken, as int)
 *  $4: battery voltage
 */
char uploadUrlTemplate[] = "http://***REMOVED***.***REMOVED***.com/dtgraph/api/add/%s?unit=C&temperature=%s&humidity=%s&delta_seconds=%d&voltage=%s&odometer=%d";

/** 
 *  Power saving optimization: 
 *  1) Deep sleep between readings.  WIFI is off. (eg 5 minutes).
 *  2) Do not submit (and activate WIFI) every reading, instead save readings for later.
 *  3) Submit every SUBMIT_THRESHOLD readings (eg 3)
 *  4) If submit fails, continue to save readings up to BUFFER_SIZE, eg 30
 *  5) When full, allow oldest readings to drop off the end.
 *  6) Once submit is possible (server/network/wifi comes back), submit all that's queued.
 */

/**  
 *  Ring Buffer: stores pending (unsubmitted) readings.
 *      
 *      This buffer is a fixed-sized array of structs that looks like this:
 *  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 *     ^Submitted index           ^Data index
 *     
 *      We keep on writing data to this array at "Data Index" position, 
 *      incrementing it in round-robin fashion.
 *      We try to submit all pending data periodically, incrementing "Submitted Index" in round-robin fashion.
 *      When the two indices match, there is nothing left to submit.
 *   
 *  BUFFER_SIZE:
 *  Keep up to this many readings. Normally this will not exceed SUBMIT_THRESHOLD,
 *  but if we can't submit for any reason, queue up to this many and submit when we can
 *  newer readings displace old ones in the buffer, so only the last BUFFER_SIZE readings
 *  are kept.
 */
#define BUFFER_SIZE           10 // 60 is about as much as will fit into RTC memory during deep sleep
#define SUBMIT_THRESHOLD      3  // try to submit when we have this many readings
#define READING_INTERVAL      30  // deep sleep (s) between taking readings.  Deep sleep may require board mods.
#define DHT_PIN               2  // Digital pin connected to the DHT sensor
#define DHT_READ_RETRIES      3
#define DHT_TYPE              DHTesp::DHT11 // https://github.com/beegee-tokyo/DHTesp/blob/master/DHTesp.h
#define RTC_STORE_START       0
#define RTC_BUCKET_SIZE       4 // for index calculations
#define WIFI_CONNECT_RETRIES  30  // try this many times before giving up
#define WIFI_CONNECT_DELAY    500 // ms to wait between tries
#define HTTP_RETRIES          3   // if we don't get a 2XX status code, retry request this many times - 1.
#define FAKE_TEMP_WITHOUT_DHT   1 //when Serial is open, don't use a DHT since it's not connected, fake the temp/humidity

//TODO: change to static IP and no DNS