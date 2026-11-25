#!/usr/bin/env bash
# the map's before and after: the preview's shots of 0.16.1's core (the map as in 0.15) and 0.16.2's (mapvis/prev), a curtain
# going across from the old to the new, a line at its edge, the version over each side
set -euo pipefail
F=$(cd "$(dirname "$0")" && pwd)
P="$F/../mapvis/prev"
FF="${FFMPEG:-ffmpeg} -hide_banner -loglevel error -y"
FONT="fontfile='C\:/Windows/Fonts/consolab.ttf'"
# wipe OUT SHOT CROP(w:h:x:y) W H [T0 TW TOTAL]: old for T0 s, the curtain over TW s, then the new
wipe() {
  local out=$1 shot=$2 crop=$3 W=$4 H=$5 T0=${6:-1.2} TW=${7:-2.2} TOT=${8:-12}
  local e="clip((T-$T0)/$TW\,0\,1)"
  $FF -loop 1 -framerate 30 -t "$TOT" -i "$P/old/$shot" -loop 1 -framerate 30 -t "$TOT" -i "$P/new/$shot" -filter_complex \
    "[0]crop=$crop,scale=$W:$H:flags=neighbor,format=gbrp[a];[1]crop=$crop,scale=$W:$H:flags=neighbor,format=gbrp[b];
     [b][a]blend=all_expr='if(lt(X\,W*$e)\,A\,B)',format=yuv444p,
     drawbox=x='iw*clip((t-$T0)/$TW,0,1)-2':y=0:w=4:h=ih:color=0x5CE1FF@0.9:t=fill:enable='between(t,$T0,$T0+$TW)',
     drawtext=$FONT:text='0.15':fontcolor=0xFFC45C:fontsize=$((H/18)):box=1:boxcolor=0x030818@0.85:boxborderw=10:x=w-tw-24:y=20:enable='lt(t,$T0+$TW*0.5)',
     drawtext=$FONT:text='0.16':fontcolor=0x5CE1FF:fontsize=$((H/18)):box=1:boxcolor=0x030818@0.85:boxborderw=10:x=24:y=20:enable='gte(t,$T0+$TW*0.5)'" \
    -c:v libx264 -preset slow -crf 10 -pix_fmt yuv444p -r 30 "$F/clips/$out"
  echo "$out"
}
# the opening and the map: the 3.5" (960x640, two pixels a pixel), MAP at 10 km and at the wide range
wipe c02-map-wide.mp4 shots2/20-map.png 960:640:0:0 1260 840 1.4 2.4 12
# up close: the town and its river, then the country's roads
wipe c04-roads.mp4 shots2/20-map.png 480:320:60:300 1260 840 1.0 2.0 11
# the cards: the radar's lower right, its ring's numbers and the map's name card
# CLASSIC (2.8") and the round glass, side by side
