#!/usr/bin/env bash
# the repository's history for the site's two pictures of it, the code growing (site/growth.js) and the tower of
# time (site/tower.js), from git log: a block a commit, as git log gives them, the page puts them in the order they
# were written. tabs between the fields. of the people their names only, never an address
#   C <unix time> <version> <author> <subject>   version: CHANGES.md's first "## " heading at that commit, a b after
#                                                a beta's number (0.16.1b), - where there was none
#   A|M|D <path>                                 a file added, changed, deleted
#   R <old path> <new path>                      a file moved
#   N <lines added> <lines taken out> <path>     how much of it changed: - and - for a picture or another binary
# needs the whole history: actions/checkout with fetch-depth: 0
#   bash .github/history.sh > public/history.txt
set -euo pipefail
git log --reverse --no-merges -M --format='@%x09%H%x09%at%x09%an%x09%s' --name-status |
while IFS= read -r line; do
  case "$line" in
    '') ;;
    '@'$'\t'*)
      IFS=$'\t' read -r _ sha at name subject <<< "$line"
      head=$(git show "$sha:CHANGES.md" 2>/dev/null | grep -m 1 '^## [0-9]' || true)
      ver=$(printf '%s\n' "$head" | sed -n 's/^## \([0-9][0-9.]*\).*/\1/p')
      case "$head" in *'(beta)'*) ver="${ver}b" ;; esac
      printf 'C\t%s\t%s\t%s\t%s\n' "$at" "${ver:--}" "$name" "$subject"
      git show --numstat -M --format= "$sha" | sed 's/^/N\t/' ;;
    *)
      st=${line%%$'\t'*}                         # A, M, D, or R and how alike: R097
      printf '%s\t%s\n' "${st:0:1}" "${line#*$'\t'}" ;;
  esac
done
