#pragma once
#include "nets.h"
#include "ui.h"

/* kept in NVS: what the screen shows (fr_cfg_t), the secrets the site sent, the touch alignment
 * and which screen start-up the board took. the secrets never go to the screen or to the log */
typedef struct {
    char ssid[33];
    char pass[65];
    char key[600];           /* the Flightradar24 API token, they are long */
} secrets_t;

int  store_load_cfg(fr_cfg_t *c);              // 1 if there was a saved one
void store_save_cfg(const fr_cfg_t *c);
void store_load_secrets(secrets_t *s);         // empty strings where nothing is saved
void store_save_secrets(const secrets_t *s);
/* the networks it remembers: the first is the one in the secrets, the others are kept here */
void store_load_nets(const secrets_t *s, nets_t *l);
void store_save_nets(const nets_t *l);
int  store_load_cal(touch_cal_t *cal);
void store_save_cal(const touch_cal_t *cal);
int  store_load_screen(void);                  // -1: never picked
void store_save_screen(int profile);
void store_load_watch(char *text, int cap);    // the watch list as typed, "" none
void store_save_watch(const char *text);
void store_load_light(light_cfg_t *c);         // what lights the LED, the defaults if none was kept
void store_save_light(const light_cfg_t *c);
void store_load_opt(scope_opt_t *o);           // the radar's options, all off if none were kept
void store_save_opt(const scope_opt_t *o);
/* counted on the site's map of boards: 0 never asked, 1 the owner allowed it, 2 not. kept by itself, so an
 * older firmware flashed back finds the rest as it left it */
int  store_load_help(void);
void store_save_help(int help);
/* how long this run has lasted, s, saved once a minute: the next start says how long the run
 * before it was, whatever ended it (a reset by the USB line or a power cut too) */
uint32_t store_load_ran(void);
void     store_save_ran(uint32_t s);
