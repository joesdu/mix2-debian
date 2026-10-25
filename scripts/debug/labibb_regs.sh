#!/bin/sh
# Read 16 registers of the PMI8998 IBB (0xdc40) and LAB (0xde40) blocks only.
R=/sys/kernel/debug/regmap/0-03/registers
for base in 0xdc40 0xde40 0xdc50 0xde50; do
	dd if=$R bs=9 skip=$((base)) count=16 2>/dev/null | tr '\n' ' '
	echo
done
