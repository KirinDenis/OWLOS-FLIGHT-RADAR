#pragma once
#include <stdint.h>
#include <stddef.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  HIGH-PRECISION REGIONAL VECTOR MAP (MYANMAR, THAILAND & NEIGHBORING REGION)
//  Stored 100% in Flash .rodata — 0 Bytes DRAM consumed!
// ═══════════════════════════════════════════════════════════════════════════════

typedef struct {
    int16_t lat; // Latitude  * 100 (e.g. 1684 = 16.84 N)
    int16_t lon; // Longitude * 100 (e.g. 9617 = 96.17 E)
} vmap_pt_t;

enum {
    VMAP_COAST = 0,
    VMAP_BORDER = 1,
    VMAP_RIVER = 2
};

typedef struct {
    uint8_t type;
    uint8_t count;
    const vmap_pt_t *points;
} vmap_seg_t;

// ── 1. Myanmar Coastline (Naf River -> Delta -> Mottama -> Kawthaung) ──────────
static const vmap_pt_t VMAP_MYANMAR_COAST[] = {
    { 2070,  9235 }, { 2055,  9255 }, { 2015,  9290 }, { 1990,  9320 },
    { 1942,  9355 }, { 1900,  9375 }, { 1845,  9430 }, { 1750,  9455 },
    { 1685,  9430 }, { 1595,  9420 }, { 1575,  9475 }, { 1575,  9525 },
    { 1585,  9580 }, { 1620,  9630 }, { 1650,  9660 }, { 1690,  9685 },
    { 1715,  9695 }, { 1670,  9740 }, { 1648,  9762 }, { 1580,  9775 },
    { 1525,  9785 }, { 1460,  9805 }, { 1408,  9820 }, { 1350,  9820 },
    { 1244,  9860 }, { 1150,  9860 }, { 1050,  9855 }, {  998,  9855 }
};

// ── 2. Thailand Gulf Coast (Bangkok Bay, Pattaya, Hua Hin, Songkhla -> Tak Bai) ─
static const vmap_pt_t VMAP_THAI_GULF_COAST[] = {
    { 1165, 10290 }, { 1210, 10230 }, { 1260, 10190 }, { 1268, 10128 },
    { 1292, 10088 }, { 1336, 10098 }, { 1350, 10060 }, { 1350, 10028 },
    { 1325, 10000 }, { 1310,  9995 }, { 1257,  9996 }, { 1180,  9980 },
    { 1110,  9950 }, { 1050,  9918 }, {  980,  9915 }, {  915,  9933 },
    {  890,  9990 }, {  843,  9996 }, {  780, 10030 }, {  720, 10060 },
    {  675, 10125 }, {  645, 10180 }, {  625, 10210 }
};

// ── 3. Thailand Andaman Coast (Ranong -> Phuket -> Satun) ─────────────────────
static const vmap_pt_t VMAP_THAI_ANDAMAN_COAST[] = {
    {  998,  9855 }, {  950,  9840 }, {  890,  9825 }, {  840,  9825 },
    {  800,  9835 }, {  788,  9839 }, {  806,  9891 }, {  755,  9960 },
    {  700,  9970 }, {  668, 10007 }
};

// ── 4. Myanmar - Thailand Border (Kawthaung -> Three Pagodas -> Mae Sai) ──────
static const vmap_pt_t VMAP_MYANMAR_THAI_BORDER[] = {
    {  998,  9855 }, { 1050,  9880 }, { 1100,  9890 }, { 1150,  9920 },
    { 1190,  9915 }, { 1250,  9940 }, { 1355,  9930 }, { 1440,  9900 },
    { 1530,  9839 }, { 1590,  9870 }, { 1669,  9851 }, { 1730,  9800 },
    { 1775,  9770 }, { 1835,  9740 }, { 1890,  9755 }, { 1960,  9810 },
    { 2000,  9900 }, { 2045,  9988 }
};

// ── 5. Thailand - Malaysia Border ─────────────────────────────────────────────
static const vmap_pt_t VMAP_THAI_MALAY_BORDER[] = {
    {  668, 10007 }, {  650, 10020 }, {  620, 10080 }, {  580, 10180 },
    {  600, 10200 }, {  625, 10210 }
};

// ── 6. Thailand - Cambodia Border ─────────────────────────────────────────────
static const vmap_pt_t VMAP_THAI_CAMBODIA_BORDER[] = {
    { 1165, 10290 }, { 1220, 10270 }, { 1250, 10250 }, { 1300, 10230 },
    { 1366, 10250 }, { 1415, 10290 }, { 1440, 10350 }, { 1445, 10450 },
    { 1435, 10530 }
};

// ── 7. Thailand - Laos Border (Mekong River & Northern Arc) ───────────────────
static const vmap_pt_t VMAP_THAI_LAOS_BORDER[] = {
    { 1435, 10530 }, { 1520, 10545 }, { 1580, 10530 }, { 1655, 10472 },
    { 1740, 10478 }, { 1788, 10275 }, { 1820, 10200 }, { 1790, 10160 },
    { 1750, 10080 }, { 1820, 10080 }, { 1880, 10120 }, { 1940, 10120 },
    { 1970, 10050 }, { 2025, 10040 }, { 2036, 10008 }
};

// ── 8. Myanmar - Laos Border (Mekong River) ───────────────────────────────────
static const vmap_pt_t VMAP_MYANMAR_LAOS_BORDER[] = {
    { 2036, 10008 }, { 2080, 10050 }, { 2150, 10115 }
};

// ── 9. Myanmar - China Border (Shan & Kachin North) ───────────────────────────
static const vmap_pt_t VMAP_MYANMAR_CHINA_BORDER[] = {
    { 2150, 10115 }, { 2220, 10000 }, { 2320,  9900 }, { 2398,  9785 },
    { 2470,  9760 }, { 2550,  9820 }, { 2650,  9870 }, { 2750,  9840 },
    { 2825,  9740 }
};

// ── 10. Myanmar - India & Bangladesh Borders ──────────────────────────────────
static const vmap_pt_t VMAP_MYANMAR_INDIA_BORDER[] = {
    { 2825,  9740 }, { 2700,  9650 }, { 2600,  9500 }, { 2450,  9430 },
    { 2380,  9330 }, { 2250,  9330 }, { 2130,  9250 }, { 2070,  9235 }
};

// ── 11. Major Rivers: Ayeyarwady & Chao Phraya ────────────────────────────────
static const vmap_pt_t VMAP_AYEYARWADY_RIVER[] = {
    { 2550,  9740 }, { 2430,  9690 }, { 2350,  9595 }, { 2198,  9608 },
    { 2140,  9530 }, { 2015,  9495 }, { 1880,  9525 }, { 1765,  9530 },
    { 1690,  9560 }, { 1580,  9525 }
};

static const vmap_pt_t VMAP_CHAO_PHRAYA_RIVER[] = {
    { 1570, 10012 }, { 1520, 10030 }, { 1435, 10055 }, { 1375, 10050 },
    { 1350, 10060 }
};

// ── Vector Map Segments Registry ───────────────────────────────────────────────
static const vmap_seg_t VMAP_SEGMENTS[] = {
    { VMAP_COAST,  sizeof(VMAP_MYANMAR_COAST)      / sizeof(vmap_pt_t), VMAP_MYANMAR_COAST },
    { VMAP_COAST,  sizeof(VMAP_THAI_GULF_COAST)    / sizeof(vmap_pt_t), VMAP_THAI_GULF_COAST },
    { VMAP_COAST,  sizeof(VMAP_THAI_ANDAMAN_COAST) / sizeof(vmap_pt_t), VMAP_THAI_ANDAMAN_COAST },
    { VMAP_BORDER, sizeof(VMAP_MYANMAR_THAI_BORDER)/ sizeof(vmap_pt_t), VMAP_MYANMAR_THAI_BORDER },
    { VMAP_BORDER, sizeof(VMAP_THAI_MALAY_BORDER)  / sizeof(vmap_pt_t), VMAP_THAI_MALAY_BORDER },
    { VMAP_BORDER, sizeof(VMAP_THAI_CAMBODIA_BORDER)/sizeof(vmap_pt_t), VMAP_THAI_CAMBODIA_BORDER },
    { VMAP_BORDER, sizeof(VMAP_THAI_LAOS_BORDER)   / sizeof(vmap_pt_t), VMAP_THAI_LAOS_BORDER },
    { VMAP_BORDER, sizeof(VMAP_MYANMAR_LAOS_BORDER)/ sizeof(vmap_pt_t), VMAP_MYANMAR_LAOS_BORDER },
    { VMAP_BORDER, sizeof(VMAP_MYANMAR_CHINA_BORDER)/sizeof(vmap_pt_t), VMAP_MYANMAR_CHINA_BORDER },
    { VMAP_BORDER, sizeof(VMAP_MYANMAR_INDIA_BORDER)/sizeof(vmap_pt_t), VMAP_MYANMAR_INDIA_BORDER },
    { VMAP_RIVER,  sizeof(VMAP_AYEYARWADY_RIVER)   / sizeof(vmap_pt_t), VMAP_AYEYARWADY_RIVER },
    { VMAP_RIVER,  sizeof(VMAP_CHAO_PHRAYA_RIVER)  / sizeof(vmap_pt_t), VMAP_CHAO_PHRAYA_RIVER },
};

#define VMAP_SEGMENTS_COUNT (sizeof(VMAP_SEGMENTS) / sizeof(vmap_seg_t))
