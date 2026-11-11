#pragma once
#include "map.h"

int  tiles_init(void);
int  tiles_have(const tile_id_t *t);
int  tiles_fetch(const tile_id_t *t, int n);

typedef struct { uint32_t off, len, at; } tile_src_t;
int  tiles_open(const tile_id_t *t, tile_src_t *s);
int  tiles_read(void *src, uint8_t *buf, int cap);
