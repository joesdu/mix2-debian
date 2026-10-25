#!/bin/sh
ovp() { grep wled_ovp /proc/interrupts | tr -s ' ' | cut -d' ' -f3; }
rate() { a=$(ovp); sleep 3; b=$(ovp); echo "ovp rate: $(( (b - a) / 3 ))/s"; }
BL=$(ls -d /sys/class/backlight/* | head -1)
echo "before:"; rate
echo 2047 > $BL/brightness; sleep 0.5; echo 2048 > $BL/brightness
echo "after brightness rewrite:"; rate
