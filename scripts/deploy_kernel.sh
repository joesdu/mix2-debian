#!/bin/bash
# Push the kernel build in images/dev (Image.gz, DTB, initramfs, modules) to the
# phone, write the boot partition there and reboot.
# usage: deploy_kernel.sh [--no-reboot]   (MIX2_HOST / MIX2_PASS may be set)
set -euo pipefail
cd /g/crazymix2
PASS=${MIX2_PASS:-mix2}
HOST=${MIX2_HOST:-172.16.42.1}
SCP=(scp -q -i keys/mix2_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)

"${SCP[@]}" images/dev/modules.tar.gz images/dev/Image.gz images/dev/msm8998-xiaomi-chiron.dtb \
	images/dev/initramfs.cpio.gz "mix2@$HOST:/tmp/"
scripts/mix2ssh "echo '$PASS' | sudo -S -p '' sh -c '
	set -e
	rm -rf /lib/modules/6.18.58-chiron
	tar -m -xzf /tmp/modules.tar.gz -C /lib/modules/ 2>/dev/null
	depmod -a 6.18.58-chiron
	install -m 644 /tmp/Image.gz /tmp/msm8998-xiaomi-chiron.dtb /tmp/initramfs.cpio.gz /boot/mix2/
	mix2-flash-boot
'"
if [ "${1:-}" != --no-reboot ]; then
	scripts/mix2ssh "echo '$PASS' | sudo -S -p '' systemctl reboot" || true
	sleep 10
	timeout 240 bash -c "until scripts/mix2ssh true 2>/dev/null; do sleep 5; done"
	echo "phone is back: $(scripts/mix2ssh uname -v)"
fi
