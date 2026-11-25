#!/usr/bin/env bash
# log-summary.sh: what does a long raw log say, in numbers only? Restarts and crashes, the frame rate and
# the heap, the maps whole or not and why, the weather's answers, the planes' answers by server and HTTP
# code, the WiFi, the second core, the rig's own commands.
#
# Run, in Git Bash or any bash, on a log written by cycle-looks.ps1, log-board.ps1 or soak-board.ps1:
#   bash log-summary.sh soak/soak-COM5-cycle.log
#   bash log-summary.sh long-COM5.log --tail 40     also the last 40 lines that name nothing of the person
# Needs: bash, grep, sed, awk, sort, uniq (Git Bash has them all). Touches no board, writes nothing.
# Prints: counts only, never a line of the log. The raw log holds the request URLs (the home), tile
#   addresses and the network's name. --tail prints whole lines, but only those of known harmless
#   kinds (frame and heap figures, map and fetch summaries, restarts) and only when they have no web
#   address, no network, no home, no place, no IP address, no quoted text and no tile address.
# How to read it: a soak passes with 0 restarts or crashes after the first line (the start the rig made),
#   every map whole when the board is left alone, and the lowest heap not falling run after run.
set -u
L="${1:-}"
TAIL=0
if [ "${2:-}" = "--tail" ]; then
  TAIL="${3:-40}"
fi
if [ -z "$L" ] || [ ! -f "$L" ]; then
  echo "usage: log-summary.sh <log> [--tail N]" >&2
  exit 1
fi

last=$(grep -a -E '^ *[0-9][0-9.,]* s? ' "$L" | tail -n 1 | awk '{print $1}')
echo "== $(basename "$L"): $(wc -l < "$L") lines, ${last:-?} s"

crash=$(grep -a -c -E 'rst:0x|Guru Meditation|abort\(\) was called|stack overflow|assert failed|Backtrace:|task_wdt' "$L")
echo "restarts or crashes: $crash (a log that began with a restart counts that one too)"
grep -a -o -E 'rst:0x[0-9a-f]+ \([A-Z_]+\)|Guru Meditation[^)]*\)|abort\(\) was called|stack overflow[^ ]*|assert failed|task_wdt' "$L" |
  sort | uniq -c | sed 's/^/  /' | head -n 8

grep -a -o -E '[0-9]+ fps, [0-9]+ ms a frame, view [0-9]+, [0-9]+ planes, heap [0-9]+' "$L" |
  sed -E 's/^([0-9]+) fps, ([0-9]+) ms a frame, view [0-9]+, [0-9]+ planes, heap ([0-9]+)$/\1 \2 \3/' |
  awk 'NR==1{mn=$1;mx=$1;hm=$3;hs=$3} {if($1<mn)mn=$1; if($1>mx)mx=$1; if($3<hm)hm=$3; he=$3; n++; s+=$1}
       END{if(n) printf "frames: %d reports, fps %d to %d (mean %.1f); heap first %d, lowest %d, last %d\n", n, mn, mx, s/n, hs, hm, he; else print "frames: no 10 s report"}'

grep -a -o -E 'map: zoom [0-9]+, [0-9]+ of [0-9]+ tiles' "$L" |
  awk '{got=$4; of=$6; n++; if (got==of) w++; else {p++; part=part" "got"/"of}}
       END{printf "maps: %d made, %d whole, %d not whole%s\n", n, w, p, (p ? " (" part " )" : "")}'
least=$(grep -a -o -E '[0-9]+ after reading them' "$L" | awk '{print $1}' | sort -n | head -n 1)
echo "map heap: least ${least:-?} after reading the tiles"
echo "map memory: $(grep -a -c 'no memory to read a tile in' "$L") tile reads with no memory, $(grep -a -c 'map: no memory for' "$L") maps with no memory, $(grep -a -c 'kept tiles not readable' "$L") times kept tiles unreadable"

grep -a -o -E 'weather: [0-9]+ bytes, .* air (read|not read), past (read|not read)' "$L" |
  sed -E 's/.* air (read|not read), past (read|not read)/air \1, past \2/' | sort | uniq -c | sed 's/^/weather answers: /'
echo "weather misses: $(grep -a -c -E 'weather: (HTTP|no memory)' "$L")"

grep -a -o -E '[A-Z][A-Z0-9.]*: HTTP -?[0-9]+, -?[0-9]+ bytes in' "$L" | sed -E 's/, -?[0-9]+ bytes in$//' |
  sort | uniq -c | sed 's/^/planes: /'

echo "WiFi lost: $(grep -a -c 'WiFi lost' "$L"), WiFi up: $(grep -a -c 'WiFi up' "$L")"
grep -a -o -E 'WiFi lost \(reason [0-9]+\)' "$L" | sort | uniq -c | sed 's/^/  /' | head -n 6
echo "second core: started $(grep -a -c -E 'the second core draws (every other strip|the lower half of every strip)' "$L"), stopped $(grep -a -c 'the second core stops drawing' "$L")"
echo "RIG commands: $(grep -a -c ' RIG look' "$L")"

if [ "$TAIL" -gt 0 ] 2>/dev/null; then
  echo "-- the last $TAIL lines that name nothing of the person"
  grep -a -E 'fps, [0-9]+ ms a frame|map: zoom|planes in it|no memory|rst:0x|Guru|panic|abort\(|App version' "$L" |
    grep -a -v -i -E 'https?://|/v2/|network|ssid|bssid|wifi:|home|place|ip:|"|[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|[0-9]+/[0-9]+/[0-9]+' |
    sed -E 's/^( *[0-9][0-9.,]* s?) [IWE] \([0-9]+\) [a-z_-]+: /\1 /' | tail -n "$TAIL"
fi
