# Versions

What changed in each version of OWLOS Flight Radar, the newest first. The first `## ` line below is the
version: the firmware is built with it, the site publishes it, and a board compares its own with it.
The site keeps the firmware of each version: a new number here keeps the one before it as it was.

## 0.12

- At a home in Rusia the board asks for no planes and no map: the screen says NOT AVAILABLE IN RUSIA.
  The country is the one OpenStreetMap gives for the home's point, asked once a home. Crimea and the other
  occupied parts of Ukraine are Ukraine there.
- The map of boards on the site: how many boards were flashed from it, how many are online and how many are
  on a cable to it now, by board, by version and by country. A board or a page is counted only when you
  allow it: the page asks once, and on the board it is SET, DATA, COUNT ME. It tells only the country, the
  board type and the version: no home, no WiFi name, no MAC, and the address it comes from is not kept.
  Rusia is not counted and is plain land on the map.

## 0.11

- Elecrow's CrowPanel 7.0, the plain one (DIS08070H), experimental: ESP32-S3, an 800x480 screen on the RGB
  bus, capacitive touch, the 5" boards' layout. Asked for in our group by an owner who tests it; not run on
  one here yet. Its BOOT button is the screen's clock line: do not press it while the screen runs.

## 0.10

- Fixed: a board could restart on the map, most often right after the range changed. When a map tile came
  only in part, the next one was asked for on a connection that was already broken.
- Fixed: the map could go from the screen after a while and stay away till a restart. A second try for
  the tiles still missing wiped the map first, and with the memory short just then nothing came back. Now
  it adds the missing tiles to the map that is there.

## 0.9

- A beta. The screens on the page are your boards' own, live, and say so: LIVE over each one. Click one
  and the board does the same.
- Each board has a PORT window: what came along its cable, and a line to send it.
- Finding a board's port is steadier. A board that did not answer is not left dark, a port that will not
  open says why, and a board whose cable goes back in is found again by itself.
- The ESP32-4827S043C, experimental: ESP32-S3, a 4.3" 480x272 screen on the RGB bus, capacitive touch,
  with a layout of its own. Asked for in our group; not run on one here yet. Tell us how it goes.

## 0.8

- The board remembers five WiFi networks and joins whichever it finds. A saved one joins with a tap on the
  WiFi page, and the site shows them, each with FORGET.
- A new page: a picture of how the planes reach your board, one CONNECT button, and all your boards at once,
  each with its own live screen and settings. The page you know stays, a link away.
- A board says which build it runs and its MAC when a page asks, so a page can keep several boards apart.
- The M5Stack Tab5, experimental: ESP32-P4, 720x1280 on MIPI-DSI, its WiFi on the C6 beside it. The 5"
  pages 9/4 as big, turned by the P4's pixel engine, the writing smaller for now. Brought in by Paris
  Pitts, who runs it on one with planes; not run on one here yet. Tell us how it goes.

## 0.7

- The radar in your browser, on the site's radar page: the same screen code as on the board. A demo sky at
  first. Connect a board over USB in Chrome or Edge and the page shows its planes, and a tap on either side
  goes to the other: the look, the colours, the range, turning the map, the options, the picked plane.
- The NM-CYD-C5 ran out of the chip's own RAM when its screen started, on the first real board. The map's
  picture, TLS and the WiFi's buffers now lie in its PSRAM.

## 0.6

- The radar look has its small ring back. The pin stays on the two maps, where it is needed.
- A gear under the radar opens its options, also in SET's new RADAR tab and on the site, all off until you
  turn them on: the lowest and highest height shown; DATA BLOCK, climbing and speed in the labels;
  AUTOPILOT, the height set on it; VECTORS, a line ahead of each plane; BEAM STEPS, a plane moves only when
  the beam goes over it; CLOCK, the time in UTC, asked from pool.ntp.org only while it is on; LOST PLANES,
  dim when there is no fresh position; CONFLICT, two planes closing on each other turn red.
- A picked plane's panel is in bigger letters and says more when the plane sends it: what is set on the
  autopilot, airspeed and Mach, the wind where it is.
- Fixed: on the 3.5" the map could stop coming until a restart, when the memory had broken into small
  pieces. The map's reader now needs smaller ones.
- Two 5" boards, experimental, never run on a real board here: Elecrow's CrowPanel Advance 5.0 and the
  Guition JC8048W550, ESP32-S3 with an 800x480 screen on the RGB bus and capacitive touch. The same pages
  as on the 3.5", half as big again, in the screen's own pixels. Tell us how it goes.
- Fixed: looking for WiFi networks on the screen could restart the ESP32-S3 boards.

## 0.5

- Home is a pin now, in a colour nothing else on the screen has, with a dark edge, over the planes. The
  ring it used to be got lost on a map full of white roads and white planes.
- The planes from adsb.fi come from its new address: the old one is on its way out.
- Fixed: the LED on the back lights for what you picked. Since 0.3 it was put out on every frame, as if
  the screen had been touched, so only TEST ever showed.

## 0.4

- Three more boards, each asked for by people who have one, none run here yet: the LCDWIKI ES3C28P
  (ESP32-S3, ILI9341V, capacitive touch), the NM-CYD-C5 (ESP32-C5, ST7789, resistive touch) and
  Elecrow's CrowPanel Advance 2.8" (ESP32-S3, ST7789 IPS, capacitive touch). Tell us how it goes.
- The site talks to a board over the chip's own USB too, not only through a CH340: the ES3C28P has no
  other.
- The WS2812 LED on the ES3C28P and the NM-CYD-C5 lights for the same things as the CYDs' RGB one.
- The site keeps every version from 0.2 on: step 1 can flash an older one, for a board the newest went
  wrong on.

## 0.3

- Planes on the ground can be left out: SET, DATA, ON GROUND, or the site. Next to an airport the parked
  ones no longer fill the list.
- Home is set on the board itself: SET, DATA, HOME, then a town, a postcode or a point typed on the
  screen, and the board looks it up.
- A watch list of up to 8 planes, by callsign, registration or hex address. The board says when one
  comes in range and picks it, and when one takes off or lands anywhere. WATCH on a picked plane's panel
  puts it on the list or takes it off.
- The RGB LED on the back of the 2.8" and 3.5" CYD lights for what you pick: an emergency squawk round
  home, a watched plane in range, taking off or landing, any plane near, no data. How bright, for how
  long, and which pin the red one is on (GPIO 4, or 22 on LCDWIKI's and Elegoo's ESP32-32E boards).
- SET is in tabs: VIEW, DATA, LED.
- ABOUT, in SET's DATA tab: the version, whether the site has a newer one, and a QR code to the site.
  When the site has a newer one, a small note on the main screen says so and opens ABOUT.
- The site shows this history, the newest version, and the version the board runs.
- LCDWIKI's ESP32-32E boards, also sold by Elegoo, take the 2.8" and 3.5" CYD builds.
- Fixed: a setting the site sent the moment the board said it was ready could be lost.

## 0.2

- The first public version. A radar scope over the map, a plain square map, or the map in
  OpenStreetMap's own colours. Planes from the free community ADS-B networks (adsb.lol, adsb.fi) or
  from Flightradar24 with a key. Turn and zoom with a finger, the nearest planes in a panel, every flight
  in a list, five colour themes, a demo sky without WiFi, the WiFi set on the screen or from the site.
- Boards: the 2.8" CYD (all three of its screens), the 3.5" CYD, the 1.28" round Elecrow CrowPanel and
  the 3.5" RPi LCD on OWLOSAirQuality. Flashed and set up from the site.
