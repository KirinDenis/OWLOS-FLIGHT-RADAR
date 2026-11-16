#!/usr/bin/env bash
# writes an ESP Web Tools manifest.json next to every screen's binaries
#   make-manifests.sh <fw folder> "<version text>"
# <fw folder>/<screen>/ holds bootloader.bin, partition-table.bin, owlos-flight-radar.bin and
# the build's flash_args, which says at what address each one goes
set -euo pipefail
FW="$1"
VERSION="$2"

for dir in "$FW"/*/; do
  disp=$(basename "$dir")
  case "$disp" in
    ili9341) name='2.8" CYD (320x240, ILI9341 or ST7789)';   family='ESP32' ;;
    st7796)  name='3.5" CYD (ST7796 480x320)';                family='ESP32' ;;
    ili9486) name='3.5" RPi LCD on OWLOSAirQuality (ILI9486)'; family='ESP32' ;;
    gc9a01)  name='1.28" round (GC9A01 240x240)';              family='ESP32-C3' ;;
    es3c28p) name='2.8" LCDWIKI ES3C28P (ESP32-S3, ILI9341V)'; family='ESP32-S3' ;;
    nmcydc5) name='2.8" NM-CYD-C5 (ESP32-C5, ST7789)';          family='ESP32-C5' ;;
    crowadv28) name='2.8" Elecrow CrowPanel Advance (ESP32-S3, ST7789)'; family='ESP32-S3' ;;
    crowadv50) name='5" Elecrow CrowPanel Advance (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    jc8048w550) name='5" Guition JC8048W550 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    tab5) name='5" M5Stack Tab5 (ESP32-P4, 1280x720 on MIPI-DSI, experimental)'; family='ESP32-P4' ;;
    esp4827s043) name='4.3" ESP32-4827S043C (ESP32-S3, 480x272, experimental)'; family='ESP32-S3' ;;
    crow70) name='7" Elecrow CrowPanel 7.0 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    wave7) name='7" Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    wave7b) name='7" Waveshare ESP32-S3-Touch-LCD-7B (ESP32-S3, 1024x600, experimental)'; family='ESP32-S3' ;;
    jc8012) name='10.1" Guition JC8012P4A1C (ESP32-P4, 1280x800 on MIPI-DSI, experimental)'; family='ESP32-P4' ;;
    *) echo "skipping unknown folder $disp"; continue ;;
  esac
  parts=""
  while read -r addr path; do
    case "$addr" in ""|--*) continue ;; esac          # the first line is the flash settings
    file=$(basename "$path")
    [ -f "$dir/$file" ] || { echo "$disp: $file is missing" >&2; exit 1; }
    parts="$parts${parts:+, }{ \"path\": \"$file\", \"offset\": $((addr)) }"
  done < "$dir/flash_args"
  cat > "$dir/manifest.json" <<JSON
{
  "name": "OWLOS Flight Radar - ${name//\"/\\\"}",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [ { "chipFamily": "$family", "parts": [ $parts ] } ]
}
JSON
  echo "$disp: $(tr -d '\n' < "$dir/manifest.json")"
done
