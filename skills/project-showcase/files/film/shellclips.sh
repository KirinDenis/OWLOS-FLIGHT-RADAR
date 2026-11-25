#!/usr/bin/env bash
# the console for the release film: two beats from the 0.16.2 film's one session (../film4/site3), each three
# commands short. the boards on top x1.3, the console under them x2, as in that film
set -euo pipefail
F=$(cd "$(dirname "$0")" && pwd)
FF="${FFMPEG:-ffmpeg} -hide_banner -loglevel error -y"
cd "$F"
mkdir -p clips/k
clip() {
  local out=$1; shift
  local list="clips/k/$out.txt"
  : > "$list"
  local last=""
  while [ $# -gt 0 ]; do
    printf "file '../../../film4/site3/%s.png'\nduration %s\n" "$1" "$2" >> "$list"
    last=$1
    shift 2
  done
  printf "file '../../../film4/site3/%s.png'\nduration 12\nfile '../../../film4/site3/%s.png'\n" "$last" "$last" >> "$list"
  $FF -f concat -safe 0 -i "$list" -f lavfi -i "color=c=0x06142C:s=1100x759:r=30" -filter_complex \
    "[0:v]fps=30,split=2[p][q];[p]crop=712:258:49:34,scale=924:336:flags=lanczos[boards];
     [q]crop=349:206:414:340,scale=704:416:flags=lanczos[shell];
     [1:v][boards]overlay=88:0:shortest=1[b];[b][shell]overlay=198:341,format=yuv444p" \
    -c:v libx264 -preset slow -crf 10 -pix_fmt yuv444p -r 30 "clips/$out.mp4"
  echo "$out.mp4"
}
clip k14 c02-open 0.6 c05-boards-typed 0.6 c06-boards 1.2 c08-cat-typed 0.7 c09-cat 1.3 c10-echo-typed 0.6 c11-echo 1.0 c12-copy-typed 0.7 c13-copy 1
clip k15 c14-set-typed 0.7 c15-set 1.9 c18-home-typed 0.8 c19-home 1.9 c22-man-typed 0.7 c23-man 1
