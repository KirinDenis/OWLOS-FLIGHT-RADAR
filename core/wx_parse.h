/* wx_parse - Open-Meteo's answers into weather_t (weather.h): the forecast with its current, hourly and
 * daily parts, then the air quality. both are the API's own flat objects of numbers and arrays of
 * numbers, read by name with no JSON library. the questions they answer are in wx_url_* */
#pragma once
#include <stddef.h>
#include "weather.h"

/* the questions, for a point already rounded (the home to a tenth of a degree, about 10 km) */
void wx_url_forecast(char *buf, size_t cap, float lat, float lon);
void wx_url_air(char *buf, size_t cap, float lat, float lon);
/* the day before, for the graphs: the pressure, the temperature, the weather and the rain by the hour. its
 * own short question (some 2 KB), as the forecast's past_hours would bring the past of every hourly field
 * and not fit the answer's buffer */
void wx_url_past(char *buf, size_t cap, float lat, float lon);

/* 1: the forecast read into w, every field of it but the air's and the radiation's. 0: not an answer
 * this reads, w as it was */
int wx_parse_forecast(const char *json, weather_t *w);
/* 1: the air quality read into w, now and by the hour, matched to the forecast's hours. 0: not read,
 * the air left WX_NONE */
int wx_parse_air(const char *json, weather_t *w);
/* 1: the past day read into w->past_*, matched to the forecast's hours. 0: not read, the past left 0 */
int wx_parse_past(const char *json, weather_t *w);
/* the past day when its answer did not come: was's, the last answer read, moved on by the hours between the
 * two. its own past where that reaches, then its forecast's hours, which are past by now */
void wx_past_from(weather_t *w, const weather_t *was);
