# Last.fm (in FusionEdge)

FusionEdge can show the playing track's album cover in place of the station's icon. Last.fm matches the stream's "Artist -
Title" and, when it can, gives the album's MusicBrainz id; MusicBrainz (`musicbrainz.md`) and the Cover Art Archive
(`cover-art-archive.md`) do the rest. This is FusionEdge's code (`src/core/coverart.cpp`) and README, read here. Optional:
`USE_LASTFM_COVER` and `LASTFM_API_KEY` in `myoptions.h`.

How sure: read from the code and the README. [docs] Not run on a board here: the no-PSRAM builds leave it out.

## URL shape

Plain HTTP:

```
http://ws.audioscrobbler.com/2.0/?method=track.getInfo&autocorrect=1&format=json&api_key=<key>&artist=..&track=..
http://ws.audioscrobbler.com/2.0/?method=track.search&format=json&limit=10&api_key=<key>&track=<cleaned title>
```

- `track.search` is the fallback for Bluetooth sources whose artist field is a video channel's name: the title is cleaned first.
- Only the API key is sent. Never put a Last.fm shared secret in firmware.

## How it is done in little memory

- A low-priority background task, started only when the metadata has both an artist and a title.
- Plain HTTP on purpose: another TLS session can take the contiguous internal RAM that HTTPS audio streams need on the ESP32-S3.
- The task's stack, the JSON and the images in PSRAM. Images are not written to the file system.
- Answers are cached in PSRAM by the server's `Cache-Control` and `Expires`. `no-store` and `no-cache` answers are not reused.
- Stream changes come first: a cover download pauses for them.
- Matching ignores ASCII and Unicode punctuation, folds Cyrillic case, drops a trailing numeric id after a semicolon, and accepts
  several artists separated by semicolons when the title and one artist agree.

## Terms

Read Last.fm's API terms before shipping, and keep the visible credit: FusionEdge's web UI links to Last.fm. [docs]
