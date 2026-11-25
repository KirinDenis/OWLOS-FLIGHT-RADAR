#!/usr/bin/env bash
# the 0.16 release film: a segment a beat (its slide, its clip in the room SlideBake left, its voice), then one encode
# of them all at -16 LUFS. the first segment opens on its picture, never on black: the networks take the first frame
# for the post's picture. the weather from film5.c in Trencin, the map, the planes and the console from the 0.16.2
# film's shots (relabelled 0.15 and 0.16), the history from the site's own window, 0.17's weather from its branch
set -euo pipefail
F=$(cd "$(dirname "$0")" && pwd)
FF="${FFMPEG:-ffmpeg} -hide_banner -loglevel error -y"
PROBE=${FFPROBE:-ffprobe}
OUT="$F/owlos-flight-radar-0.16.mp4"
cd "$F"
. ./places.sh
mkdir -p seg
: > list.txt
calc() { awk "BEGIN{printf \"%.3f\", $1}"; }
xywh() { set -- $1; X=$1; Y=$2; WW=$3; HH=$4; }

LEAD=0.3
LAST=21
for n in $(seq 1 $LAST); do
  id=$(printf '%02d' "$n")
  v=$($PROBE -v error -show_entries format=duration -of csv=p=0 "voice/$id.wav")
  tail=0.45
  [ "$id" = $LAST ] && tail=4.5
  D=$(calc "$v+$LEAD+$tail")
  O=$(calc "$D-0.3")
  L=$(calc "$LEAD*1000" | cut -d. -f1)
  IN="fade=t=in:st=0:d=0.3,"
  [ "$id" = 01 ] && IN=""                              # the first frame is the picture itself
  END="${IN}fade=t=out:st=$O:d=0.3"
  [ "$id" = $LAST ] && END="${IN}fade=t=out:st=$(calc "$D-1.2"):d=1.2"
  case "$id" in                                        # the console, 14 and 15, is one session: a straight cut
    14) END="${IN}null" ;;
    15) END="fade=t=out:st=$O:d=0.3" ;;
  esac
  AUD="aresample=48000,adelay=${L}|${L},apad,atrim=0:$D"
  ENC="-c:v libx264 -preset veryfast -crf 12 -pix_fmt yuv420p -r 30 -c:a pcm_s16le -ac 2 -t $D"
  # one slide, one clip in the room P: from S s into the clip, its first frame held HOLD s, a layer TOP over it all,
  # the clip CROPped first
  one() {
    local slide=$1 clip=$2 P=$3 FLAGS=$4 S=${5:-0} HOLD=${6:-0} TOP=${7:-} CROP=${8:-}
    xywh "$P"
    local c="[2:v]fps=30,${CROP}scale=${WW}:${HH}:flags=${FLAGS},tpad=start_mode=clone:start_duration=${HOLD}:stop_mode=clone:stop_duration=30[c]"
    if [ -n "$TOP" ]; then
      $FF -loop 1 -framerate 30 -t "$D" -i "slides/$slide.png" -i "voice/$id.wav" -ss "$S" -i "clips/$clip" -loop 1 -framerate 30 -t "$D" -i "slides/$TOP.png" \
        -filter_complex "$c;[0:v][c]overlay=$X:$Y:shortest=0[b];[b][3:v]overlay=0:0,format=yuv420p,$END[v];[1:a]$AUD[a]" \
        -map "[v]" -map "[a]" $ENC "seg/$id.mkv"
    else
      $FF -loop 1 -framerate 30 -t "$D" -i "slides/$slide.png" -i "voice/$id.wav" -ss "$S" -i "clips/$clip" \
        -filter_complex "$c;[0:v][c]overlay=$X:$Y,format=yuv420p,$END[v];[1:a]$AUD[a]" \
        -map "[v]" -map "[a]" $ENC "seg/$id.mkv"
    fi
  }
  still() {
    $FF -loop 1 -framerate 30 -t "$D" -i "slides/$1.png" -i "voice/$id.wav" \
      -filter_complex "[0:v]format=yuv420p,$END[v];[1:a]$AUD[a]" -map "[v]" -map "[a]" $ENC "seg/$id.mkv"
  }
  # two stills, the second from T s on
  two() {
    local T=$1 a=$2 b=$3
    $FF -loop 1 -framerate 30 -t "$T" -i "slides/$a.png" -loop 1 -framerate 30 -t "$(calc "$D-$T")" -i "slides/$b.png" -i "voice/$id.wav" \
      -filter_complex "[0:v][1:v]concat=n=2:v=1:a=0,format=yuv420p,$END[v];[2:a]$AUD[a]" -map "[v]" -map "[a]" $ENC "seg/$id.mkv"
  }
  case "$id" in
    01) one 01 d10-open.mp4 "$P01" lanczos ;;
    02) one 02 d10-bare.mp4 "$P02" lanczos ;;
    03) one 03 d10-bare.mp4 "$P03" lanczos 4 0 03-top "crop=480:480:161:0," ;;
    04) one 04 d10-open.mp4 "$P04" lanczos 6 0 04-top ;;
    05) one 05 b05-pages.mp4 "$P05" lanczos ;;
    06) one 06 d1-alert.mp4 "$P06" neighbor 2.5 ;;     # the tap on "a tap opens it"
    07) two "$(calc "$LEAD+6.9")" 07a 07b ;;          # AUTO on its words
    08)                                                # the light in steps on its words, then the night dims it slowly
      T1=$(calc "$LEAD+2.1"); T2=$(calc "$LEAD+2.65"); T3=$(calc "$LEAD+3.4"); T4=$(calc "$LEAD+4.5")
      xywh "$P08"
      $FF -loop 1 -framerate 30 -t "$T1" -i slides/08a.png -loop 1 -framerate 30 -t "$(calc "$T2-$T1")" -i slides/08b.png \
          -loop 1 -framerate 30 -t "$(calc "$T3-$T2")" -i slides/08c.png -loop 1 -framerate 30 -t "$(calc "$T4-$T3")" -i slides/08d.png \
          -loop 1 -framerate 30 -t "$(calc "$D-$T4")" -i slides/08e.png -i "voice/$id.wav" -i clips/d1-radar.mp4 -filter_complex \
        "[0:v][1:v][2:v][3:v][4:v]concat=n=5:v=1:a=0[bg];
         [6:v]fps=30,scale=${WW}:${HH}:flags=neighbor,tpad=stop_mode=clone:stop_duration=30,split=5[r0][r1][r2][r3][r4];
         [r0]trim=0:$T1,setpts=PTS-STARTPTS[d0];
         [r1]trim=$T1:$T2,setpts=PTS-STARTPTS,colorchannelmixer=rr=0.72:gg=0.72:bb=0.72[d1];
         [r2]trim=$T2:$T3,setpts=PTS-STARTPTS,colorchannelmixer=rr=0.5:gg=0.5:bb=0.5[d2];
         [r3]trim=$T3:$T4,setpts=PTS-STARTPTS,colorchannelmixer=rr=0.3:gg=0.3:bb=0.3[d3];
         [r4]trim=$T4:$D,setpts=PTS-STARTPTS,split=2[n0][n1];[n1]colorchannelmixer=rr=0.45:gg=0.45:bb=0.45[nd];
         [n0][nd]blend=all_expr='A*(1-clip((T-0.8)/2.4\,0\,1))+B*clip((T-0.8)/2.4\,0\,1)'[d4];
         [d0][d1][d2][d3][d4]concat=n=5:v=1:a=0[clip];
         [bg][clip]overlay=$X:$Y,format=yuv420p,$END[v];[5:a]$AUD[a]" \
        -map "[v]" -map "[a]" $ENC "seg/$id.mkv" ;;
    09) one 09 c02-map-wide.mp4 "$P09" lanczos 0 1.5 ;; # the curtain on "Its steps are even"
    10)                                                # the roads' curtain, then the names on their words
      T1=$(calc "$LEAD+3.2")
      xywh "$P10"
      $FF -loop 1 -framerate 30 -t "$T1" -i slides/10a.png -loop 1 -framerate 30 -t "$(calc "$D-$T1")" -i slides/10b.png -i "voice/$id.wav" \
          -i clips/c04-roads.mp4 -filter_complex \
        "[3:v]fps=30,scale=${WW}:${HH}:flags=lanczos,trim=0:$T1,setpts=PTS-STARTPTS[c];[0:v][c]overlay=$X:$Y[a];
         [a][1:v]concat=n=2:v=1:a=0,format=yuv420p,$END[v];[2:a]$AUD[au]" \
        -map "[v]" -map "[au]" $ENC "seg/$id.mkv" ;;
    11) one 11 b11-planes.mp4 "$P11" lanczos ;;
    12) one 12 d1-radar.mp4 "$P12" neighbor ;;
    13) two "$(calc "$LEAD+5.7")" 13a 13b ;;          # the page turned, on "A page turned"
    14) one 14 k14.mp4 "$P14" lanczos ;;
    15) one 15 k15.mp4 "$P15" lanczos ;;
    16) still 16 ;;
    17) one 17 h17.mp4 "$P17" lanczos ;;
    18) one 18 h18.mp4 "$P18" lanczos ;;
    19) still 19 ;;
    20)                                                # 0.17's weather, the 2.8" and the 3.5" side by side
      xywh "$P20a"; XA=$X; YA=$Y; WA=$WW; HA=$HH
      xywh "$P20b"
      $FF -loop 1 -framerate 30 -t "$D" -i slides/20.png -i "voice/$id.wav" -i clips/h1-hud.mp4 -i clips/h2-hud.mp4 -filter_complex \
        "[2:v]fps=30,scale=${WA}:${HA}:flags=neighbor,tpad=stop_mode=clone:stop_duration=30[a];
         [3:v]fps=30,scale=${WW}:${HH}:flags=neighbor,tpad=stop_mode=clone:stop_duration=30[b];
         [0:v][a]overlay=$XA:$YA[s];[s][b]overlay=$X:$Y,format=yuv420p,$END[v];[1:a]$AUD[au]" \
        -map "[v]" -map "[au]" $ENC "seg/$id.mkv" ;;
    21) still 21 ;;
  esac
  echo "file 'seg/$id.mkv'" >> list.txt
  printf '%s %s\n' "$id" "$D"
done
$FF -f concat -safe 0 -i list.txt -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -r 30 \
  -af "loudnorm=I=-16:TP=-1.5:LRA=11,aresample=48000" -c:a aac -b:a 160k -movflags +faststart "$OUT"
$FF -i "$OUT" -frames:v 1 first-frame.png
${FFMPEG:-ffmpeg} -hide_banner -i "$OUT" -frames:v 1 -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG" -f null - 2>&1 | grep -o "YAVG=[0-9.]*"
$PROBE -v error -show_entries format=duration,size -of csv=p=0 "$OUT"
