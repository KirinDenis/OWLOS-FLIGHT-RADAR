# Versions

What changed in each version of OWLOS Flight Radar, the newest first. The first `## ` line below is the
version: the firmware is built with it, the site publishes it, and a board compares its own with it.
The site keeps the firmware of each version: a new number here keeps the one before it as it was.
A version marked (beta) is offered on the site beside the stable one, which stays what the site flashes
unless the beta is picked. Only a board that runs a beta hears of a newer beta.

## 0.16.2 (beta)

- The planes move smoothly. The board hears where each plane is every 5 seconds from the free networks
  and every 20 from FR24, and flies it on by itself in between; when the next position came, a plane
  that had gone a little wrong jumped to it. Now it slides there, eased at both ends, in two seconds, or
  longer where two would be faster than the plane itself flies: a light one circling does not swim half
  a kilometre sideways. Its nose turns to the new heading as smoothly, where it snapped round. One that
  stood still on the screen, unheard for longer than a guess goes, still jumps to where it is, and so
  does one more than 3 km from where the board had it.
- Each position is placed at the moment the server made its answer. The free networks write that moment
  in each answer, and the board took the moment it asked instead: an answer is made somewhere in a
  question of 2.5 to 4 seconds, never at the same point of it, so each new position came a hundred
  metres or so ahead or behind, and a plane sped up and slowed down with every answer. FR24's positions
  are placed by their own times.
- A plane in a turn flies on round its curve for 5 seconds, and its shape turns with it: how fast it
  turns comes from its last two positions. Then straight on: a turn is often over by the time two
  positions show it, and one flown on further turned the noses more than the planes turned.
- On FR24 the planes stood still for a moment before each answer and then jumped ahead: a guess went 20
  seconds at most, and FR24's answers come 20 seconds apart and a little more. Now they fly on till the
  next one comes, and through one that does not come, 60 seconds, where they went and came back. The
  RADAR look's LOST greys a plane on FR24 after 32 seconds, not 12: it greyed them all between two
  answers.
- Measured on 12 minutes of real sky round a busy airport, replayed through the board's own code as the
  free networks give it: a plane steps back a pixel 0.6 times a minute where 0.16.1 did 6.6 times, no
  frame moves it more than 17 m off the way it flew where 0.16.1 jumped up to 600 m, and it is drawn 54 m
  from where it is on average, 289 m before. At FR24's pace 0.16.1 jumped up to 2.3 km; now 17 m.
- The map is easier to see on the dark looks. On these screens the land was one or two steps of 32 above
  black and the woods three, where the glass shows next to nothing, least of all from the side: the
  steps of its kinds are now even to the eye, the land and the woods lifted, the water left dark. Water
  and towns have a bright edge inside them, so a coast, a river, a lake and a town stand out by their
  outline: the eye reads where things end more than what fills them. The woods have none, the country is
  woods in pieces and every piece outlined rippled over the whole map. The classic look keeps its own
  colours and outlines only the water, a deeper blue.
- A road, a river or a stream one pixel wide on the map's tiles came out dashed: a point of the map is up
  to 1.4 pixels of a tile and was read at one of them. Now it keeps whichever of its pixels would be lost,
  the road and not the field beside it, and the lines run whole. The names on the classic look no longer
  lose letters the same way.
- The map's own names, the towns and villages, read on the dark looks. The tiles write them dark in a
  light halo, made for a light map, and here they were a dark hole with a glow round it: now the letters
  are near white and the halo dark. They are found by their colours, which OpenStreetMap keeps the same:
  a grey under 100 is a letter, a lighter grey within three pixels of one is its soft edge; read one
  pixel at a time they went to a town, a road or a rail, and a name came out eaten. A letter comes before
  anything else under it, as it does on the tiles.
- Each name is copied word by word, pixel for pixel, as the tiles write it; only where it stands is
  scaled. The tiles write a town's name some 9 pixels high for their own scale, and stretched or shrunk to
  the screen's a stroke came out one pixel here and two there: "Trenčín" read "Trenć'n", its háček an
  accent and its í gone. The thin stem of an í, a lighter grey with no dark pixel of its own, is now part
  of its word, and a name split between two tiles meets itself again. The classic look gets the same.
  The words are gathered in a buffer the board already had for asking OpenStreetMap a town's name, now
  1.1 KB bigger; taken from the free memory beside the tile's own, they cost the 2.8" a third of its maps.
- Light letters on the dark glass looked fatter than the same letters dark on paper, and a small name ran
  into a blot: the soft edge was as bright as the letter. Now it is dimmer, and SET, VIEW, **MAP NAMES**
  says how much: SOFT, half as bright (what a board starts with), THIN, a quarter, or BOLD, as bright. The site's console
  says it as `names`. The classic look is as it was.
- The height legend in the corner of the map and the radar says the height's unit: in metres (SET, UNITS)
  it reads <3KM and <7.6KM, where it read <10K and <25K whatever was set. The colours stay where they
  were, at 10 000 and 25 000 ft.
- The plain greys of a town were taken for an airport's apron and lit up as runways: they are a town now.
- The small roads are quieter the wider the range: from 25 km out the tiles have every lane in the
  country, and whole they were a net over the map.
- The cards under the writing on the map, the planes' names, the degrees, the ring's numbers on the
  radar, the place on the round glass, are solid: the map showed through the letters, more so now it is
  brighter.
- The outlines take some 5 ms a frame on the 3.5" in the MAP and CLASSIC looks, 10 frames a second to 9,
  and nothing to speak of in RADAR; on the round glass a few ms of a frame of 250 or more.
- The site's console writes a board's config as it reads it: each line NAME=VALUE goes to the live board as
  `set` sends it. `echo theme=amber > config` sets one thing, and `cat /boards/a/config > /boards/b/config`
  gives one board all another one has. A line a board does not take, a light that does not dim, is said and
  left; the rest are set. The other files of a board, its info, wifi and log, only show what it says.
- The console's `set` takes as many settings as are given: `set look=radar theme=amber backlight=25 --all`.
  An alias of it is a whole setup in one word.

## 0.16.1 (beta)

- A fourth look, WEATHER: the next 24 hours on the radar's own dial, noon at the top and midnight at
  the bottom, a hand at now as a clock has, and in the middle the temperature now over the home's clock.
  Round the edge the sky as it will be each hour, by the sun's height the board works out, the night
  with its stars; inside it the temperature as a loop in the colours of how warm, frost blue to a hot
  red, the clouds, the rain and the wind. The day is there whole from the first moment: no beam goes
  round it, the weather is for reading, and what moves is the weather itself, the rain, the wind, the
  stars. The panel says it in words: now, how it feels, the humidity, the wind, the pressure and which
  way it goes, the air, the UV, the radiation, the rain and the next days, the air and the UV each with
  a little scale under it, best to worst, the step now lit. Its three dots are the radar's, kept for
  both looks: without the panel the dial stands in the middle with those numbers either side of it, a
  warning's in a box that breathes. The forecast and the air are Open-Meteo's, asked every half an hour
  for the home rounded to about 10 km, no key needed. The radiation has no open source for the whole
  world yet: till it has one, the sun's next rise or set stands in its place.
- SET has a UNITS tab: the distance and the height from VIEW, and the weather's, TEMPERATURE in C or
  F, PRESSURE in hPa or inHg at the sea's level or in mmHg at home's height as a barometer there reads,
  WIND in km/h, m/s, mph or knots, RAIN in millimetres or inches. AUTO, the first, is home's country's
  own: the US's Fahrenheit, inHg, mph and inches, the UK's mph, mmHg where it is kept, m/s in the north
  and the east. The board keeps them; the site's VIEW tab and the console (`temp_unit`, `pressure_unit`,
  `wind_unit`, `rain_unit`) set them too. On the round glass they are under VIEW.
- The site has WEATHER too, on its screen of a board and in the demo without one, drawn by the same
  code as on the board. The page asks Open-Meteo for the forecast itself, as the board does, and only
  while that look is up. The board's VIEW tab there has WEATHER beside the other looks, which of its
  pages is up, the warnings and the units. A page turned on the board turns on the site too, and the
  other way round: the board says `OWLOS WXPAGE` and the page's number. A board before 0.16 keeps its
  three looks there.
- Back to 0.15 from this beta: a board left on WEATHER shows MAP there, and the LED's settings start
  from their defaults. Everything else stays, and flashed to 0.16.1 again the board has all of it back.
- Behind the dial two more pages, a tap on it to each, or BOOT: the pressure and the temperature as
  graphs on one time axis, 14 hours back on the left and the next day on the right, so now stands at
  about the golden section of the width, in the same place on both, a line up to a dot that pulses.
  Each line is a smooth curve through the hours that never swings past the values it joins, a fill
  fading under it, the past thinner and fainter than what is to come, the weather's pictures on it
  every three hours (six on the round glass), and a half sun on the time's line where the sun rises and
  sets. The pressure's hours of a coming change of 6 hPa or more are amber, with the pressure 12 hours
  ago, now and when the change is done under it. The temperature has its numbers on the line and the
  rain under it in bars, the rain to come falling into them, then when it rains and the strongest gust.
  The day before is a third short question to Open-Meteo, some 2 KB: its pressure, temperature, weather
  and rain. A pressure warning's chip opens its page. WEATHER draws at 17 to 20 frames a second on the
  2.8", its graphs at 20 to 25; on the 3.5" at 8 to 10 and 9 to 11.
- Weather warnings on every look, each one on at first and each one the person's to switch off (SET,
  DATA, or the board's VIEW tab on the site): a storm, gusts from 75 km/h or thunder; the pressure 6 hPa
  up or down within 12 hours, the change the body feels; the air poor or worse; radiation over 0.3
  microsievert an hour near home. A chip under the town's name says the worst of them, amber or red,
  and a tap on it opens WEATHER, where the panel says each in full. One that comes says so on the
  screen for a few seconds and lights the LED's new WEATHER row, yellow. The radiation's waits for a
  source of the nearest station's reading.
- The round board draws its discs in whole numbers: its ESP32-C3 has no floating point unit, and the
  WEATHER look's clouds took half its frame.
- The second core draws half of every look and every page now, not of the radar's only: the weather,
  the two maps, the list, SET and the rest, on the boards that have it: the 3.5", the 4.3", the 5" and
  7" boards and the Tab5 (those four are not run on one here). The 2.8" CYD has it now too: there it
  draws the lower half of each strip of the picture, which wants no memory but its own 7 KB, and the
  radar goes from 9 to 12 frames a second, MAP from 16 to 19 and CLASSIC from 13 to 15. The 3.5" draws
  WEATHER with both cores too, once its maps are read. Its maps stay on one core: they want the
  memory, and their frames wait for the cable to the screen rather than for the drawing. There the
  second core takes its memory only when there is room.
- The screen's light can be turned down: SET, VIEW, BACKLIGHT, in four steps, 100%, 50%, 25% and 10%,
  or from the site, on the board's VIEW tab. The board keeps it. Not on the boards whose light only
  goes on: the round one, the RPi LCD and the CrowPanel Advance 5.0.
- SET, VIEW, NIGHT: the light goes down to 50%, 25% or 10% from dusk till dawn, by the sun over the
  board's home: from when it is 3 degrees under the horizon, about a quarter of an hour after sunset,
  till as long before sunrise. The board works the sun out itself, with the time from the network, so
  no time zone is needed. It never makes the screen brighter than BACKLIGHT. Off at first.
- Fixed: on the 3.5" the square map now and then came up empty for 15 seconds. There was memory
  enough, but in pieces too small for the tile reader, which now takes smaller ones, and 2 KB less of
  it: it keeps a tile's last two rows as long as that tile's rows are, not as long as the widest
  picture's could be. And the 3.5" draws its picture in strips of 6 rows instead of 8, which leaves
  the maps 7 KB more: in 20 minutes of looks and ranges changed every 5 seconds, 153 maps of 153
  came whole. MAP and CLASSIC draw a little slower for it there, 10 and 9 frames a second instead of
  12 and 11.
- Fixed: the 2.8" and 3.5" CYD's touch now and then took one noisy read of its chip for a tap. Now and
  then after a restart the 3.5" went on to the next look by itself, as if its look button had been
  touched. Sometimes it also stopped trusting its pen interrupt and read the chip every frame from then on.
  Now a press counts once two reads in a row say it, 15 ms later than before. On every board the glass
  is not read in the first second and a half after power on, and a press held from then on counts only
  once it has lifted.
- Fixed: on the 5", 7" and 10" boards and the Tab5 a slash in the small capitals lost its top pixel
  where a strip of the picture began just under it: there the slash stands a pixel over the capitals.
- Fixed: when the site knew two boards of one kind, two 2.8" CYDs say, a button in one's window could
  go to the other, often one not on the cable, and nothing happened: its settings, a network to forget,
  its screen, the touch's crosses. The console names such boards with the end of their MAC now, like
  2.8-cyd-1a2b, and every window's buttons reach their own board.
- A board already on the site picks its version too: in its window SETTINGS, BOARD, Version, STABLE or
  BETA, then FLASH, its settings kept. Before, only a new board could pick the beta as it was flashed,
  and a board on the beta had no button back to the stable.

## 0.15

- The plain Elecrow CrowPanel 5.0 (DIS07050H), not the Advance: `crow50`. ESP32-S3, 800x480, GT911.
  Its picture pins and its touch (I2C on GPIO 19 and 20, a PCA9557 at 0x18, backlight on GPIO 2) are
  the ones a P2000 firmware already runs on this board. BOOT is the panel's clock, so it is not read.
  Experimental on the site: the radar itself has not run on one yet. By Bert van Mullem (bertvm).
- The time is asked as soon as the WiFi is up, whether or not the clock is drawn. (bertvm)
- ESP-IDF 6: the RGB panel is asked for RGB565 by name, and ADSB.FI's cross-signed GTS root is trusted.
  A 5.5 build skips the certificate line it does not know. (bertvm)
- On every other board 0.15 changes only when the time is asked: there is no need to flash it again.

## 0.14

- Three new boards, by Michael Caldwell-Waller (chipguyhere), from the fork
  chipguyhere/OWLOS-FLIGHT-RADAR-7-7B: the Waveshare ESP32-S3-Touch-LCD-7 and -7B, and the 10.1" Guition
  JC8012P4A1C on an ESP32-P4. On them the sweep turns at the panel's own frame rate: the planes, the sweep
  and the compass ring are laid over the picture while the panel is fed (the README's The moving layer).
  Experimental on the site.
- The JC8012P4A1C's touch needs its controller's firmware, installed once with chipguyhere's installer.
  Erasing the board from the site takes it away too.
- WiFi: a network served by several access points is joined at the strongest one. The board looks on every
  channel first, so joining takes a moment longer. (chipguyhere)
- The USB console finds a command even when a restart left stray bytes in front of it. (chipguyhere)
- Fixed: the map kept holes over the sea and plain land, on every board. OpenStreetMap sends those tiles
  with few colours, 1, 2 or 4 bits a pixel, and the board read only 8. A tile it cannot read is now also
  dropped and asked for again, and the log says so.
- Fixed: after a new range the map went from the screen until all its new tiles came, and with the memory
  short on the 3.5" they could stay away. The map shown now stays until the new one has tiles to show.
- Fixed: on the 3.5" the second core took and gave back 38 KB at every page and broke the memory into
  pieces. It now stays through the pages of the radar look. A rare restart when no memory was left for
  the screen's strips is gone too.
- Fixed: a tile server that does not answer, or a tile cut short, no longer holds the planes back for
  minutes: the other tiles wait for the next round.
- Fixed: after 24.8 days without a toast the last one came back on the screen for good (on the round
  board the HUE LIST SET row too). A plane farther than 327 km no longer draws its trail across the scope.

## 0.13

- The radar can do without its panel: the three dots in the top right corner take it away and the scope
  moves to the middle of the screen. A picked plane brings the panel back while it is picked; the dots
  take it away again, and the plane with it. Tapped once more, they bring the panel back to stay. The
  board remembers which way it was left, and the site's screen of the board does the same as the board.
- The panel's letters go with its width. On the 3.5", 4.3", 5" and 7" boards they are the bigger ones, and
  a row of the nearest planes says each one's height too, and its type where there is room for it. The
  2.8" keeps its letters; over the radar its panel's title says the picked plane's kind.

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
