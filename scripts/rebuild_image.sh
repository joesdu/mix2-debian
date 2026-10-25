#!/bin/bash
# Full image rebuild after build_rootfs.sh + build_kernel.sh: boot images, rootfs config, ext4 image.
# Run as root in WSL.
set -euo pipefail
S=/mnt/g/crazymix2/scripts
for f in configure_rootfs.sh make_image.sh make_boot.sh mix2-flash-boot; do sed -i 's/\r$//' "$S/$f"; done
su joe -c "bash $S/make_boot.sh"
bash "$S/configure_rootfs.sh"
bash "$S/make_image.sh"
