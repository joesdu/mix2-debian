#!/bin/bash
# usage: bisect_boot.sh <image>  -> RAM-boots it from the running good system and
# reports PASS (test kernel reachable, logs saved) or FAIL (the test kernel
# crashed and the phone came back on the kernel in the boot partition).
# The test image needs "mix2.test" on its command line (make_dbg_boot.sh adds
# it) and should carry "panic=5" so a hang reboots instead of sitting there.
set -uo pipefail
cd /g/crazymix2
IMG=$1; NAME=$(basename "$IMG" .img)
[ -s "$IMG" ] || { echo "$NAME: image missing"; exit 4; }
scripts/mix2ssh "echo mix2 | sudo -S -p '' systemctl reboot --reboot-argument=bootloader" >/dev/null 2>&1
timeout 120 bash -c 'until fastboot devices | grep -q fastboot; do sleep 1; done' || { echo "$NAME: no fastboot"; exit 2; }
fastboot boot "$IMG" >/dev/null 2>&1
sleep 20
timeout 480 bash -c 'until scripts/mix2ssh true 2>/dev/null; do sleep 4; done' || { echo "$NAME: phone unreachable (needs manual reset)"; exit 3; }
V=$(scripts/mix2ssh "uname -v")
if ! scripts/mix2ssh "grep -qwF mix2.test /proc/cmdline"; then
	echo "$NAME: FAIL (fell back to the good kernel)"
	exit 1
fi
scripts/mix2ssh "echo mix2 | sudo -S -p '' sh -c 'dmesg; echo ===deferred; cat /sys/kernel/debug/devices_deferred'" > "kernel/$NAME.log" 2>&1
echo "$NAME: PASS ($V) log kernel/$NAME.log"
grep -A40 "===deferred" "kernel/$NAME.log" | head -20
