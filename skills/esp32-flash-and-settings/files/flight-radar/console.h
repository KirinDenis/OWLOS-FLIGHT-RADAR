/* console - the site talks to the board over the USB serial line (115200), one line at a time:
 *   board  OWLOS READY flight-radar <version> <build> <mac>   at power on, and as the answer to OWLOS HELLO
 *   site   OWLOS SET {"ssid":"..","pass":"..","src":"free","key":"..","lat":51.5,"lon":-0.1,"place":"..","country":"..",
 *                     "up":90,"flip":1,"theme":2,"look":1,"map":1}   (how the screen shows it, for a board without touch)
 *          a new home without "place" and "country": the board asks OpenStreetMap what it is called
 *          a network goes first in the five it remembers, {"forget":".."} takes one out
 *   board  OWLOS OK                               or OWLOS ERR <why>
 *   site   OWLOS GET
 *   board  OWLOS CFG {"ssid":"..","nets":[".."],"lat":..,"lon":..,"place":"..","country":"..","src":"free","key":true,"up":0,"flip":0,
 *                     "theme":0,"look":0,"map":1}
 *   board  OWLOS ALIGN 1                          its screen waits for the touch alignment, 0: no more
 *   site   OWLOS SKIP                             leave the touch alignment, as holding BOOT does
 * every field of SET may be left out. the password and the key go in, never out: GET only
 * says whether a key is stored. the log goes on the same line, the site skips what is not OWLOS */
#pragma once

typedef void (*console_line_fn)(const char *line);
void console_start(console_line_fn fn);

/* flat JSON fields, for what the site sends. 1 = found */
int json_str(const char *json, const char *key, char *out, int cap);
int json_num(const char *json, const char *key, double *out);
