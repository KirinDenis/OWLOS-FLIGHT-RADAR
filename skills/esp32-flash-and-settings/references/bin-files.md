# Files for esptool and other tools

The site flashes with one button, but some people want esptool, Espressif's Flash Download Tool, or an SD card
launcher. The files are already public: they are what the site serves. The Flight Radar got a GitHub issue "Bin
files": "You forgot the bin files...". FusionEdge's page answers it on every card. The Flight Radar's page still
has no such list (as of 0.16.2).

## Direct links, per board

Give **direct links per board**, written out, not a template with `<board>` in it. A person copies a link, and
Markdown eats `<board>` as a tag.

```
2.8" CYD (ESP32):
  0x1000  https://<you>.github.io/<repo>/fw/ili9341/bootloader.bin
  0x8000  https://<you>.github.io/<repo>/fw/ili9341/partition-table.bin
  0x10000 https://<you>.github.io/<repo>/fw/ili9341/owlos-flight-radar.bin
esptool.py --chip esp32 -p PORT -b 460800 write_flash 0x1000 bootloader.bin 0x8000 partition-table.bin 0x10000 owlos-flight-radar.bin
```

The addresses by board: each `manifest.json` or `flash_args` beside them. Older versions: `fw/<version>/...`.
The Flash Download Tool takes the same three files and addresses.

FusionEdge builds that list on the page itself, from the same `flash_args` the manifests are made from, so it
cannot disagree with them (built, live on its page):

```js
// flash_args says what goes where: the same file the manifests are made from
async function listFiles(card) {
  const base = `${dir}/${card.dataset.board}/`;
  const answer = await fetch(base + 'flash_args');
  const parts = [];
  for (const line of (await answer.text()).split('\n')) {
    const [addr, file] = line.trim().split(/\s+/);
    if (addr && file && !addr.startsWith('--')) {
      parts.push({ addr, file });
    }
  }
  // a link a part with its address, the zipped one-file image, and the esptool line:
  // esptool --chip <chip> -b 460800 write-flash <addr> <file> ...
}
```

It sits in a `<details>` "Files for esptool and other tools" under each card's INSTALL button, follows the
version picked above the cards, and is read only when opened. Under it: "The one-file image goes at 0x0 and
covers the settings too: for a first install only."

## One file: the merged image

A launcher wants one file. A merged image is made in the build folder, because `flash_args` there has the
subfolder paths (it does not work on the flat site folder):

```
cd firmware/build_ili9341
esptool.py --chip esp32 merge_bin -o merged.bin @flash_args
```

or `idf.py -B build_ili9341 merge-bin` (`merged-binary.bin`). Both read from esptool 4.10 and IDF 5.5, not run
here. To add it to the Flight Radar's site: one line in the Build step, one more file in Collect the binaries,
and add it to `FILES` in `archive.sh`.

**A merged image written at 0 goes over NVS** (0x9000 lies between the partition table and the app): it wipes
the settings like an erase. Fine for a first install, wrong for an update. That is why the page uses parts.
Arduino's `merged.bin` is the whole 4 MB, and does the same. On the classic ESP32 the image starts with 0x1000
bytes of nothing, where the ROM looks for the bootloader at 0x1000.

## FusionEdge's merged image, checked in CI

`collect.sh` of the FusionEdge fork makes one per board and checks it before it is published. It runs in that
CI on every push; on 2026-11-23 its live site served version 1.1.1 (build 32) for 7 boards, each with its zip
(measured: the files answer):

```bash
# every image the chip boots from, read back by esptool: a wrong chip or a broken file stops here
for f in $(awk '{ print $2 }' "$OUT/flash_args"); do
  case "$f" in
    bootloader.bin|firmware.bin) esptool --chip "$CHIP" image-info "$OUT/$f" ;;
  esac
done
# one file for tools that take one, written at 0x0. it covers NVS too, so it is for a first install
one="fusionedge-$BOARD-$VER.bin"
(cd "$OUT" && esptool --chip "$CHIP" merge-bin -o "../$one" $(tr '\n' ' ' < flash_args))
boot=$(awk '$2 == "bootloader.bin" { print $1 }' "$OUT/flash_args")
tail -c +$((boot + 1)) "$4/$one" > "$4/from-boot.bin"     # the image as the ROM reads it
esptool --chip "$CHIP" image-info "$4/from-boot.bin"
while read -r addr file; do                                # and every part sits in it at its own address
  size=$(stat -c %s "$OUT/$file")
  cmp -n "$size" -i "$((addr)):0" "$4/$one" "$OUT/$file" || { echo "$file is not at $addr in $one" >&2; exit 1; }
done < "$OUT/flash_args"
(cd "$4" && zip -9 -q "$BOARD/fusionedge-$BOARD.zip" "$one" && rm "$one")
```

- `image-info` on the bootloader and the app: a file built for the wrong chip, or a broken one, stops the run.
- `image-info` on the merged image from the bootloader's offset on: the image as the ROM will read it.
- `cmp` of each part against the merged image at its address: proof that nothing moved.
- Zipped: one download, and a `.bin` named with the board and the version inside.
- esptool 5 (PlatformIO's, from pioarduino) spells the commands `merge-bin`, `image-info`, `write-flash`.
- The archive keeps the zip with each version (`archive.md`, FusionEdge's variant).
