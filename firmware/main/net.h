/* net - WiFi as a station and HTTPS GET. the WiFi keeps reconnecting by itself; a request is
 * blocking, one at a time, from the fetch task */
#pragma once
#include "store.h"

// how the board names itself to every server: OSM asks for a name it can tell apart
#define NET_USER_AGENT "OWLOS-Flight-Radar/" FLIGHT_VERSION " (+https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR)"

void net_start(const nets_t *l);             // nothing happens without a network
void net_reconnect(const nets_t *l);         // new WiFi settings, from the site or the screen: the first one first
int  net_up(void);                           // 1 = connected, has an address
int  net_why(void);                          // WHY_*: the network it is trying is not up because
void net_ssid(char *buf, int cap);           // the network it is on, or is trying
void net_ip(char *buf, int cap);
/* the time, for the radar's clock. nothing asks a time server until the clock is turned on */
void     net_time_start(void);               // again does nothing: it keeps itself right from then on
uint32_t net_utc(void);                      // seconds since 1970, 0: not known yet
/* the screen's WiFi page: look round for networks. request starts it, poll (each frame) gets
 * it going when the driver lets it, get copies the list and says SCAN_* */
void net_scan_request(uint32_t now);
void net_scan_poll(uint32_t now);
int  net_scan_get(ap_t *out, int *n);
/* GET url, the body goes to sink piece by piece as it arrives (only for status 200).
 * bearer = Authorization token or NULL. returns the bytes read, -1 if no connection or no
 * answer; *status = the HTTP status */
typedef void (*net_sink_fn)(void *ctx, const char *data, int len);
int  net_get(const char *url, const char *bearer, net_sink_fn sink, void *ctx, int *status);
/* POST a small JSON body, the answer's body is not read. 0 = it went and an answer came, -1 not;
 * *status = the HTTP status */
int  net_post_json(const char *url, const char *body, int *status);
