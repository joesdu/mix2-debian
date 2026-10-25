#!/bin/sh
# Read 0x40-0x7f of the PMI8998 IBB (0xdc00) and LAB (0xde00) peripherals only.
R=/sys/kernel/debug/regmap/0-03/registers
for base in 0xdc00 0xde00; do
	for off in 0x40 0x50 0x60 0x70; do
		dd if=$R bs=9 skip=$((base + off)) count=16 2>/dev/null | tr '\n' ' '
		echo
	done
done
