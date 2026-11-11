#ifndef CITIES_H
#define CITIES_H

#include <Arduino.h>

struct BuiltinCity {
    const char *name;
    const char *display;
    float lat;
    float lon;
    const char *country;
};

static const BuiltinCity BUILTIN_CITIES[] = {
    // Myanmar Major Cities, Towns & Airfields
    { "yangon",        "Yangon",        16.8409f,  96.1735f, "Myanmar" },
    { "rangoon",       "Yangon",        16.8409f,  96.1735f, "Myanmar" },
    { "mandalay",      "Mandalay",      21.9588f,  96.0891f, "Myanmar" },
    { "naypyidaw",     "Naypyidaw",     19.7633f,  96.0785f, "Myanmar" },
    { "naypyitaw",     "Naypyidaw",     19.7633f,  96.0785f, "Myanmar" },
    { "bago",          "Bago",          17.3221f,  96.4660f, "Myanmar" },
    { "taunggyi",      "Taunggyi",      20.7831f,  97.0378f, "Myanmar" },
    { "mawlamyine",    "Mawlamyine",    16.4905f,  97.6283f, "Myanmar" },
    { "moulmein",      "Mawlamyine",    16.4905f,  97.6283f, "Myanmar" },
    { "pathein",       "Pathein",       16.7792f,  94.7326f, "Myanmar" },
    { "bassein",       "Pathein",       16.7792f,  94.7326f, "Myanmar" },
    { "monywa",        "Monywa",        22.1086f,  95.1358f, "Myanmar" },
    { "meiktila",      "Meiktila",      20.8787f,  95.8601f, "Myanmar" },
    { "pyin oo lwin",   "Pyin Oo Lwin",  22.0366f,  96.4619f, "Myanmar" },
    { "maymyo",        "Pyin Oo Lwin",  22.0366f,  96.4619f, "Myanmar" },
    { "sittwe",        "Sittwe",        20.1462f,  92.8983f, "Myanmar" },
    { "myitkyina",     "Myitkyina",     25.3832f,  97.3964f, "Myanmar" },
    { "lashio",        "Lashio",        22.9333f,  97.7500f, "Myanmar" },
    { "magway",        "Magway",        20.1544f,  94.9455f, "Myanmar" },
    { "dawei",         "Dawei",         14.0828f,  98.1942f, "Myanmar" },
    { "pyay",          "Pyay",          18.8239f,  95.2223f, "Myanmar" },
    { "prome",         "Pyay",          18.8239f,  95.2223f, "Myanmar" },
    { "kawthaung",      "Kawthaung",      9.9806f,  98.5528f, "Myanmar" },
    { "kalay",         "Kalay",         23.1894f,  94.0583f, "Myanmar" },
    { "tachileik",     "Tachileik",     20.4475f,  99.8808f, "Myanmar" },
    { "kengtung",      "Kengtung",      21.2917f,  99.6000f, "Myanmar" },
    { "hpa-an",        "Hpa-An",        16.8906f,  97.6333f, "Myanmar" },
    { "shwebo",        "Shwebo",        22.5694f,  95.6983f, "Myanmar" },
    { "pakokku",       "Pakokku",       21.3333f,  95.0833f, "Myanmar" },
    { "thanlyin",      "Thanlyin",      16.7578f,  96.2517f, "Myanmar" },
    { "bagan",         "Bagan",         21.1717f,  94.8585f, "Myanmar" },
    { "nyaung-u",      "Bagan",         21.1969f,  94.9042f, "Myanmar" },
    { "inle",          "Inle Lake",     20.5847f,  96.9247f, "Myanmar" },

    // Southeast & East Asia Capitals & Hubs
    { "bangkok",       "Bangkok",       13.7563f, 100.5018f, "Thailand" },
    { "chiang mai",    "Chiang Mai",    18.7883f,  98.9853f, "Thailand" },
    { "phuket",         "Phuket",         7.8804f,  98.3923f, "Thailand" },
    { "singapore",      "Singapore",      1.3521f, 103.8198f, "Singapore" },
    { "kuala lumpur",   "Kuala Lumpur",   3.1390f, 101.6869f, "Malaysia" },
    { "penang",         "Penang",         5.4164f, 100.3327f, "Malaysia" },
    { "jakarta",       "Jakarta",       -6.2088f, 106.8456f, "Indonesia" },
    { "bali",          "Bali",          -8.4095f, 115.1889f, "Indonesia" },
    { "hanoi",         "Hanoi",         21.0285f, 105.8542f, "Vietnam" },
    { "ho chi minh",   "Ho Chi Minh",   10.8231f, 106.6297f, "Vietnam" },
    { "saigon",        "Ho Chi Minh",   10.8231f, 106.6297f, "Vietnam" },
    { "phnom penh",    "Phnom Penh",    11.5564f, 104.9282f, "Cambodia" },
    { "vientiane",     "Vientiane",     17.9757f, 102.6331f, "Laos" },
    { "manila",        "Manila",        14.5995f, 120.9842f, "Philippines" },
    { "tokyo",         "Tokyo",         35.6762f, 139.6503f, "Japan" },
    { "osaka",         "Osaka",         34.6937f, 135.5023f, "Japan" },
    { "seoul",         "Seoul",         37.5665f, 126.9780f, "South Korea" },
    { "beijing",       "Beijing",       39.9042f, 116.4074f, "China" },
    { "shanghai",      "Shanghai",      31.2304f, 121.4737f, "China" },
    { "guangzhou",     "Guangzhou",     23.1291f, 113.2644f, "China" },
    { "kunming",       "Kunming",       25.0453f, 102.7097f, "China" },
    { "hong kong",     "Hong Kong",     22.3193f, 114.1694f, "Hong Kong" },
    { "taipei",        "Taipei",        25.0330f, 121.5654f, "Taiwan" },
    { "new delhi",     "New Delhi",     28.6139f,  77.2090f, "India" },
    { "delhi",         "Delhi",         28.6139f,  77.2090f, "India" },
    { "kolkata",       "Kolkata",       22.5726f,  88.3639f, "India" },
    { "mumbai",        "Mumbai",        19.0760f,  72.8777f, "India" },
    { "chennai",       "Chennai",       13.0827f,  80.2707f, "India" },
    { "dhaka",         "Dhaka",         23.8103f,  90.4125f, "Bangladesh" },
    { "colombo",        "Colombo",        6.9271f,  79.8612f, "Sri Lanka" },

    // Middle East & World Hubs
    { "dubai",         "Dubai",         25.2048f,  55.2708f, "UAE" },
    { "doha",          "Doha",          25.2854f,  51.5310f, "Qatar" },
    { "london",        "London",        51.5074f,  -0.1278f, "United Kingdom" },
    { "paris",         "Paris",         48.8566f,   2.3522f, "France" },
    { "frankfurt",     "Frankfurt",     50.1109f,   8.6821f, "Germany" },
    { "new york",      "New York",      40.7128f, -74.0060f, "USA" },
    { "los angeles",   "Los Angeles",   34.0522f, -118.2437f, "USA" },
    { "sydney",       "Sydney",        -33.8688f, 151.2093f, "Australia" },
    { "melbourne",    "Melbourne",     -37.8136f, 144.9631f, "Australia" }
};

inline const BuiltinCity* findBuiltinCity(const char *name) {
    if (!name || !name[0]) return NULL;
    String q = String(name);
    q.trim();
    q.toLowerCase();

    for (size_t i = 0; i < sizeof(BUILTIN_CITIES) / sizeof(BUILTIN_CITIES[0]); i++) {
        if (q.equalsIgnoreCase(BUILTIN_CITIES[i].name)) {
            return &BUILTIN_CITIES[i];
        }
    }
    // Partial substring match if longer than 3 chars
    if (q.length() >= 3) {
        for (size_t i = 0; i < sizeof(BUILTIN_CITIES) / sizeof(BUILTIN_CITIES[0]); i++) {
            if (String(BUILTIN_CITIES[i].name).indexOf(q) >= 0 || q.indexOf(BUILTIN_CITIES[i].name) >= 0) {
                return &BUILTIN_CITIES[i];
            }
        }
    }
    return NULL;
}

#endif // CITIES_H
