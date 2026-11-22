#!/bin/bash
# Verify that kernel/patches reproduce the working tree: apply them to a
# pristine linux-7.3.5 and compare every file they touch.
set -euo pipefail
WIN=/mnt/g/crazymix2
WORK=~/mix2/linux-7.3.5
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
tar -xf ~/mix2/linux-7.3.5.tar.xz -C "$T"
cd "$T/linux-7.3.5"
for p in "$WIN"/kernel/patches/*.patch; do patch -p1 -s -N < "$p"; done
files=$(grep -h -E "^\+\+\+ b/" "$WIN"/kernel/patches/*.patch | sed 's|^+++ b/||; s|\t.*||' | sort -u)
bad=0
for f in $files; do
	if ! cmp -s "$f" "$WORK/$f"; then echo "DIFFERS: $f"; bad=1; fi
done
[ $bad = 0 ] && echo "ALL PATCHED FILES MATCH ($(echo "$files" | wc -l) files)"
