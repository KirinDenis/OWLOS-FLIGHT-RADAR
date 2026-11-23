# MusicBrainz (in FusionEdge)

The open music database. FusionEdge asks it for a release group when Last.fm (`lastfm.md`) gave no album id, so the Cover Art
Archive (`cover-art-archive.md`) can be asked for a cover. FusionEdge's code (`src/core/coverart.cpp`), read here.

How sure: read from the code. [docs] Not run on a board here.

## URL shape

Plain HTTP:

```
http://musicbrainz.org/ws/2/recording/?fmt=json&limit=5&query=<recording:"title" AND artist:"artist", url-encoded>
```

## Size and parsing

- The answer goes into PSRAM, up to a fixed response limit (`httpGetToPsram`), and is read with cJSON for the release groups'
  ids.
- MusicBrainz's own rate rules and its User-Agent rule were not read here; read them before relying on it.
