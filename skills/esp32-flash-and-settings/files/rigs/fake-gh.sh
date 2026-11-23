#!/usr/bin/env bash
# a stand-in for gh in the archive test, for a PC without it: put it on PATH as `gh`. the runs are
# folders: $FAKE/runs/<run id>/fw-<board>/<board>/ with the files the firmware job uploaded.
# it proves the script's logic and nothing about GitHub's rules
set -euo pipefail
cmd="$1 $2"
shift 2
case "$cmd" in
  "run download")
    run="$1"
    shift
    dir=""
    while [ $# -gt 0 ]; do
      case "$1" in
        -D) dir="$2"; shift 2 ;;
        *) shift ;;
      esac
    done
    mkdir -p "$dir"
    cp -r "$FAKE/runs/$run"/fw-* "$dir/" ;;
  *) echo "fake gh: $cmd not faked" >&2; exit 1 ;;
esac
