#!/bin/sh
# usage: dpms_cycle.sh <long_init 0|1>
echo "$1" > /sys/module/panel_jdi_nt35596s/parameters/long_init
T=$(cut -d' ' -f1 /proc/uptime)
runuser -u mix2 -- env DISPLAY=:0 XAUTHORITY=/home/mix2/.Xauthority xset dpms force off
sleep 3
runuser -u mix2 -- env DISPLAY=:0 XAUTHORITY=/home/mix2/.Xauthority xset dpms force on
sleep 4
echo "long_init=$(cat /sys/module/panel_jdi_nt35596s/parameters/long_init)"
dmesg | awk -v t=$T -F'[][]' '$2+0>t' | grep -v -E 'Modules linked|^ ' | head -8
grep -i rmi4 /proc/interrupts | head -1
pkill -f '[t]ouch_probe'
E=/dev/input/$(basename $(dirname $(dirname $(grep -l Synaptics /sys/class/input/event*/device/name))))
nohup python3 -u /tmp/touch_probe.py "$E" 240 > /tmp/touch.log 2>&1 &
