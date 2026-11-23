# Internet radio streams

Audio streams from radio stations' own servers (Icecast, Shoutcast and the like), played by FusionEdge, the internet radio
firmware the project's fork brings to boards without PSRAM. The stream code is FusionEdge's (from yoRadio and ESP32-audioI2S);
what is here is what was met porting it to the classic ESP32 boards with 4 MB of flash: the 2.8" (an LCDWIKI E32R28T and the
CYD build) and LCDWIKI's 3.5" E32R35T.

How sure: MP3 over plain HTTP played on both boards, heard on the speaker. [measured]

## URL shape

Whatever the station list says: `http://host[:port]/path` or `https://...`, MP3, AAC, FLAC, Ogg. The station list is the
firmware's own playlist, set in its web UI.

## What plays without PSRAM

- MP3 over plain HTTP: yes. The largest free block about 50 KB on the 3.5", about 55 KB on the 2.8", while playing. [measured]
- The memory cuts that made it fit: an audio ring of 10 KB, output 4096, header 2 KB, the MP3 decoder's output 2304 samples, no
  FLAC, Opus or Vorbis, no VU or FFT buffers, `ps_malloc` turned into `malloc`, the clock's speech off. [built, run]
- AAC: no. faad2's `NeAACDecOpen` fails without PSRAM; the helix AAC decoder would be the way. [measured]
- HTTPS streams, even MP3: no. TLS leaves about 5 KB. A station on HTTPS with AAC made a boot loop: `make_unique<AACDecoder>`
  threw, abort, and the firmware's smart start played the same station again after the restart. Fixed: no AAC decoder without
  PSRAM, MP3 and WAV made with `new(nothrow)`. [measured]
- An idea, not tried: allocate the MP3 decoder's buffers before the TLS connect.
- FLAC stations stall. [measured]

## Sound on the ESP32's own DAC

Not a data matter, but met on the same streams: the I2S-DAC's DMA sends each 32-bit word's second 16-bit sample first (swap the
pairs, or it is a hiss of digital noise); the APLL clock crackled all the while a stream played, the default clock (44100.1 Hz
measured) is clean. On the E32R28T the speaker's amplifier is enabled by GPIO 4 low. [measured] The pins are in the boards
skill: `../../../esp32-boards/references/boards/e32r28t.md` and `../../../esp32-boards/references/boards/e32r35t.md`.

## On the S3 boards with PSRAM

FusionEdge's own notes: HTTPS streams need contiguous internal RAM on the ESP32-S3, so its cover-art lookups use plain HTTP and
keep their buffers in PSRAM rather than open a second TLS session (`lastfm.md`). [docs]
