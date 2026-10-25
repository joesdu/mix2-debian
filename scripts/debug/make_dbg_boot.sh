#!/bin/bash
# Debug boot image (images/dev/boot-dbg.img): verbose console, panic=5 and the
# mix2.test marker for bisect_boot.sh, extra cmdline appended (e.g. modprobe.blacklist=msm)
set -euo pipefail
OUT=~/mix2/out
EXTRA="${1:-}"
mkbootimg --kernel "$OUT/Image.gz-dtb" --ramdisk "$OUT/initramfs.cpio.gz" \
	--cmdline "clk_ignore_unused pd_ignore_unused arm-smmu.disable_bypass=0 console=tty0 fbcon=font:TER16x32 loglevel=7 modprobe.blacklist=ipa mix2.test panic=5 $EXTRA" \
	--base 0x00000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
	--tags_offset 0x00000100 --pagesize 4096 --header_version 0 \
	--output /mnt/g/crazymix2/images/dev/boot-dbg.img
echo "cmdline extra: $EXTRA"
