# Cover Art Archive (in FusionEdge)

The album covers that MusicBrainz ids point at. FusionEdge's last step for a cover (`lastfm.md`, `musicbrainz.md`).
FusionEdge's code (`src/core/coverart.cpp`), read here.

How sure: read from the code and the README. [docs] Not run on a board here.

## URL shape

Plain HTTP, a 250 px front cover:

```
http://coverartarchive.org/release-group/<mbid>/front-250
http://coverartarchive.org/release/<mbid>/front-250
```

- The archive answers with a redirect to where the image lives. The firmware reads the `Location` and fetches the image from
  there (`httpGetRedirectLocation`).
- The image is kept in PSRAM only, JPEG or PNG. While a cover is missing, downloading or bad, the station's icon stays.

## Terms

Respect the rights of artists and labels in the images; FusionEdge's README says so and credits the archive. [docs]
