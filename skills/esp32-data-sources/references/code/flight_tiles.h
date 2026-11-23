/* tiles - OpenStreetMap's map tiles, downloaded by the board and kept in a flash partition of
 * their own ("tiles", partitions.csv): the PNG files as they came, one after another round the
 * partition, the oldest written over first. OSM's tile policy asks for exactly this: a
 * program that names itself, only the tiles it shows, each kept at least a week (these stay
 * till the room is needed) */
#pragma once
#include "map.h"

int  tiles_init(void);                       /* finds the partition and what it keeps: 0 = none */
int  tiles_have(const tile_id_t *t);
/* downloads those of these not kept yet, over one connection. how many are kept now */
int  tiles_fetch(const tile_id_t *t, int n);

/* one kept tile read for png_decode: tiles_open, then tiles_read as its png_read_fn */
typedef struct { uint32_t off, len, at; } tile_src_t;
int  tiles_open(const tile_id_t *t, tile_src_t *s);
int  tiles_read(void *src, uint8_t *buf, int cap);
