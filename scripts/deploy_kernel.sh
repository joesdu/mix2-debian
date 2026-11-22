#!/bin/bash
# Push the kernel build in images/dev (Image.gz, DTB, initramfs, modules) to the
# phone, write the boot partition there and reboot.
# usage: deploy_kernel.sh [--no-reboot]   (MIX2_HOST / MIX2_PASS may be set)
set -euo pipefail
cd /g/crazymix2
PASS=${MIX2_PASS:-mix2}
HOST=${MIX2_HOST:-172.16.42.1}
KEY=${MIX2_KEY:-keys/mix2_ed25519}
SCP=(scp -q -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
KVER=$(tar -tzf images/dev/modules.tar.gz | awk -F/ 'NF >= 2 && $1 != "" && !found {value=$1; found=1} END {if (!found) exit 1; print value}')
case "$KVER" in
	*[!A-Za-z0-9._+-]*|"") echo "cannot determine kernel release from modules.tar.gz" >&2; exit 1;;
esac

"${SCP[@]}" images/dev/modules.tar.gz images/dev/Image.gz images/dev/msm8998-xiaomi-chiron.dtb \
	images/dev/initramfs.cpio.gz images/dev/kernelrelease "mix2@$HOST:/tmp/"
scripts/mix2ssh "echo '$PASS' | sudo -S -p '' sh -c '
	set -e
	rm -rf /lib/modules/$KVER
	tar -m -xzf /tmp/modules.tar.gz -C /lib/modules/ 2>/dev/null
	depmod -a $KVER
	install -m 644 /tmp/Image.gz /tmp/msm8998-xiaomi-chiron.dtb /tmp/initramfs.cpio.gz /tmp/kernelrelease /boot/mix2/
	mix2-flash-boot
'"
if [ "${1:-}" != --no-reboot ]; then
	scripts/mix2ssh "echo '$PASS' | sudo -S -p '' systemctl reboot" || true
	sleep 10
	timeout 240 bash -c "until scripts/mix2ssh true 2>/dev/null; do sleep 5; done"
	echo "phone is back: $(scripts/mix2ssh uname -v)"
fi
