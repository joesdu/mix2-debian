#!/bin/sh
# Restart the touch controller (rmi4 unbind/bind pulses its reset line)
pkill -f '[t]ouch_probe' 2>/dev/null
echo 0-0020 > /sys/bus/i2c/drivers/rmi4_i2c/unbind
sleep 1
echo 0-0020 > /sys/bus/i2c/drivers/rmi4_i2c/bind
sleep 2
dmesg | grep -i rmi | tail -3
E=/dev/input/$(basename $(dirname $(dirname $(grep -l Synaptics /sys/class/input/event*/device/name))))
echo "$E"
nohup python3 -u /tmp/touch_probe.py "$E" 240 > /tmp/touch.log 2>&1 &
