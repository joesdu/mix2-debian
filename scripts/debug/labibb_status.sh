#!/bin/sh
R=/sys/kernel/debug/regmap/0-03/registers
for a in 0xdc08 0xdc46 0xde08 0xde46 0xde49; do dd if=$R bs=9 skip=$((a)) count=1 2>/dev/null | tr '\n' ' '; done; echo
