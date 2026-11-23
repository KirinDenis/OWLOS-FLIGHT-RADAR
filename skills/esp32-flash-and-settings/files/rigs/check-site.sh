#!/usr/bin/env bash
# fetches what a browser would fetch from a flasher site: the page, version.json, every manifest
# (from fw/versions.json, and whatever manifest="..." the page names) and every part in each
# manifest. says each one that does not answer 200, and a bootloader offset wrong for its chip.
#   check-site.sh <site url>         http://localhost:8793 or https://you.github.io/repo
# needs bash, curl, grep, sed. parts are asked with HEAD, nothing big is downloaded
set -uo pipefail
URL="${1%/}"
bad=0
seen=0

code() { curl -sS -m 30 -o /dev/null -I -w '%{http_code}' "$1"; }

need() {   # <url>: must answer 200
  local c
  c=$(code "$1")
  seen=$((seen + 1))
  if [ "$c" != 200 ]; then
    echo "BAD $c $1"
    bad=$((bad + 1))
  fi
}

boot_at() {   # <chipFamily>: where the ROM looks for the bootloader
  case "$1" in
    ESP32|ESP32-S2) echo 4096 ;;
    ESP32-C5|ESP32-P4) echo 8192 ;;
    *) echo 0 ;;
  esac
}

manifest() {   # <path from the site's root>
  local m="$1" dir body family="" path off
  dir=$(dirname "$m")
  body=$(curl -sS -m 30 "$URL/$m")
  need "$URL/$m"
  # one token a line, in the file's order: a chipFamily, then its parts
  while read -r tok; do
    case "$tok" in
      \"chipFamily*) family=$(echo "$tok" | sed 's/.*: "\(.*\)"/\1/') ;;
      \"path*)
        path=$(echo "$tok" | sed 's/"path": "\([^"]*\)".*/\1/')
        off=$(echo "$tok" | sed 's/.*"offset": \([0-9]*\).*/\1/')
        need "$URL/$dir/$path"
        case "$path" in
          *bootloader*)
            if [ "$off" != "$(boot_at "$family")" ]; then
              echo "BAD offset $off for $family: $m $path"
              bad=$((bad + 1))
            fi ;;
        esac ;;
    esac
  done < <(echo "$body" | grep -o -E '"chipFamily": "[^"]*"|"path": "[^"]*", "offset": [0-9]+')
  echo "ok   $m ($family)"
}

need "$URL/"
page=$(curl -sS -m 30 "$URL/")
for f in version.json CHANGES.md; do
  if [ "$(code "$URL/$f")" = 200 ]; then
    echo "ok   $f"
  fi
done

list=$(echo "$page" | grep -o 'manifest="[^"]*"' | sed 's/manifest="//; s/"$//')
vers=$(curl -sS -m 30 "$URL/fw/versions.json" | grep '"version"')
while read -r line; do
  [ -n "$line" ] || continue
  dir=$(echo "$line" | sed 's/.*"dir": "\([^"]*\)".*/\1/')
  for b in $(echo "$line" | sed 's/.*"boards": \[ \(.*\) \].*/\1/' | tr -d '",'); do
    list="$list"$'\n'"$dir/$b/manifest.json"
  done
done <<< "$vers"

for m in $(echo "$list" | sort -u); do
  manifest "$m"
done
echo "$seen fetched, $bad bad"
[ "$bad" = 0 ]
