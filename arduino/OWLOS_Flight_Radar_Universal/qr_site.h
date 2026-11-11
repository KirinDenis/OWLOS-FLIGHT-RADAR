/* the site's address and its QR code, for ABOUT (ui_about.c) and for the board to ask the
 * site for its version. written by tools/QrBake, don't edit: QrBake https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/ core */
#pragma once
#include <stdint.h>

#define SITE_URL    "https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/"
#define QR_SITE_N   33                 /* modules a side: version 4, error correction M */
#define QR_SITE_ROW 5                  /* bytes a row, the leftmost module in the top bit */
extern const uint8_t QR_SITE[QR_SITE_N * QR_SITE_ROW];   /* 1: dark */
