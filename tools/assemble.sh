#!/usr/bin/env bash
# the site as pages.yml puts it together, for a look on this PC: into ./_public, from local builds
#   bash tools/assemble.sh
# a board with no local build is left out of fw/. nothing here is published
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=_public
VER=$(tr -d '\r' < CHANGES.md | sed -n 's/^## \([0-9][0-9.]*\).*/\1/p' | head -n 1)
rm -rf "$OUT"
mkdir -p "$OUT/fw" "$OUT/radar" "$OUT/shots"
for d in ili9341 st7796 ili9486 gc9a01 es3c28p nmcydc5 crowadv28 crowadv50 jc8048w550 esp4827s043 crow70; do
  B=firmware/build_$d
  [ "$d" = ili9341 ] && B=firmware/build
  [ -f "$B/owlos-flight-radar.bin" ] || continue
  mkdir -p "$OUT/fw/$d"
  cp "$B/bootloader/bootloader.bin" "$B/partition_table/partition-table.bin" "$B/owlos-flight-radar.bin" "$B/flash_args" "$OUT/fw/$d/"
done
cp web/index.html web/twin*.js web/twin*.wasm "$OUT/radar/"
cp -r site/. "$OUT/"
cp CHANGES.md "$OUT/"
cp tools/stats-sample.json "$OUT/"             # made-up counts for a look at the stats here: index.html?stats=sample
cp -r shots/. "$OUT/shots/"
STATS=$(tr -d ' \r\n' < server/public-url.txt 2>/dev/null || true)   # as pages.yml does it
printf '{"version":"%s","stats":"%s"}\n' "$VER" "$STATS" > "$OUT/version.json"
tr -d '\r' < .github/make-manifests.sh > "$OUT/.m.sh"
bash "$OUT/.m.sh" "$OUT/fw" "$VER, local" > /dev/null
rm "$OUT/.m.sh"
sed -i "s|@VERSION@|$VER, local|g" "$OUT/index.html" "$OUT/previous/index.html"
ls "$OUT/fw"
if [ "${1:-}" = "--test" ]; then
  cp tools/fakeserial.js "$OUT/"
  sed 's|<script src="radar/twin.js"></script>|<script src="fakeserial.js"></script>\n<script src="radar/twin.js"></script>|' "$OUT/index.html" > "$OUT/test.html"
fi
