#!/bin/bash
# Dump every named partition except userdata to backup/partitions via adb exec-out.
cd /g/crazymix2
export MSYS_NO_PATHCONV=1
while read -r name dev; do
  [ "$name" = userdata ] && continue
  out="backup/partitions/$name.img"
  adb exec-out "su -c 'dd if=$dev bs=1M 2>/dev/null'" > "$out"
  echo "$name $dev $(stat -c %s "$out")"
done < backup/byname.txt
echo BACKUP_PARTS_DONE
