#!/bin/bash
# Build the production initramfs and boot.img (kernel + DT + initramfs).
# Output: ~/mix2/out/initramfs.cpio.gz, /mnt/g/crazymix2/images/boot.img
set -euo pipefail

WIN=/mnt/g/crazymix2
OUT=~/mix2/out
WORK=~/mix2/initramfs

rm -rf "$WORK"; mkdir -p "$WORK"/{bin,dev,proc,sys,tmp,run,newroot}
cp "$WIN/downloads/busybox/x/usr/bin/busybox" "$WORK/bin/busybox"
cp "$WIN/initramfs/init" "$WORK/init"
sed -i 's/\r$//' "$WORK/init"
chmod 755 "$WORK/init" "$WORK/bin/busybox"
ln -sf busybox "$WORK/bin/sh"
cp "$WIN/tools/mix2-reboot" "$WORK/bin/mix2-reboot"; chmod 755 "$WORK/bin/mix2-reboot"
(cd "$WORK" && find . | cpio -o -H newc --owner=0:0 2>/dev/null | gzip -9) > "$OUT/initramfs.cpio.gz"

CMDLINE="clk_ignore_unused pd_ignore_unused arm-smmu.disable_bypass=0 console=tty0 fbcon=font:TER16x32 quiet loglevel=4 modprobe.blacklist=ipa"
mkbootimg \
	--kernel "$OUT/Image.gz-dtb" \
	--ramdisk "$OUT/initramfs.cpio.gz" \
	--cmdline "$CMDLINE" \
	--base 0x00000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
	--tags_offset 0x00000100 --pagesize 4096 --header_version 0 \
	--output "$OUT/boot.img"
# Same image that stops in the initramfs USB rescue shell (for flash_rootfs.sh)
mkbootimg \
	--kernel "$OUT/Image.gz-dtb" \
	--ramdisk "$OUT/initramfs.cpio.gz" \
	--cmdline "$CMDLINE mix2.rescue" \
	--base 0x00000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
	--tags_offset 0x00000100 --pagesize 4096 --header_version 0 \
	--output "$OUT/boot-rescue.img"
mkdir -p "$WIN/images"
cp "$OUT/boot.img" "$OUT/boot-rescue.img" "$WIN/images/"
ls -la "$WIN/images/boot.img" "$WIN/images/boot-rescue.img"
