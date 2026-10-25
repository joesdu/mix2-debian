#!/bin/bash
# Pack the configured rootfs into an ext4 image (label mix2-root), compressed
# for flash_rootfs.sh. Run as root inside WSL.
#
# Do NOT turn this into a sparse image for `fastboot flash userdata`: the
# chiron bootloader mishandles sparse FILL chunks (a block of one repeated
# 32-bit value is never written), which silently corrupts files and ext4
# metadata. flash_rootfs.sh writes the raw image from the rescue initramfs.
set -euo pipefail

ROOT=/root/mix2/rootfs
OUT=/root/mix2
WIN=/mnt/g/crazymix2
SIZE=${SIZE:-5G}

for m in proc sys dev; do mountpoint -q "$ROOT/$m" && umount -l "$ROOT/$m"; done
find "$ROOT/lib/firmware/qcom/msm8998" "$ROOT/lib/firmware/updates" \
	"$ROOT/lib/firmware/qcom/a540_gpmu.fw2" -type f -exec chmod 644 {} +

# Boot to the desktop through LightDM
ln -sf /usr/lib/systemd/system/graphical.target "$ROOT/etc/systemd/system/default.target"
ls -la "$ROOT/etc/systemd/system/display-manager.service"

rm -f "$OUT/rootfs.img"
truncate -s "$SIZE" "$OUT/rootfs.img"
mkfs.ext4 -q -F -L mix2-root -m 1 -d "$ROOT" "$OUT/rootfs.img"
e2fsck -fn "$OUT/rootfs.img"
mkdir -p "$WIN/images"
command -v pigz >/dev/null || apt-get install -y -qq pigz >/dev/null
pigz -1 -c "$OUT/rootfs.img" > "$WIN/images/debian13-mix2-rootfs.img.gz"
(cd "$OUT" && sha256sum rootfs.img) > "$WIN/images/debian13-mix2-rootfs.img.sha256"
stat -c %s "$OUT/rootfs.img" > "$WIN/images/debian13-mix2-rootfs.img.size"
ls -la "$WIN"/images/debian13-mix2-rootfs.img.*
echo IMAGE_OK
