#!/bin/sh
# Collect what the initramfs needs for root on LVM and for the rescue shell:
# lvm, dmsetup, thin_check, e2fsprogs and GNU tar (keeps xattrs/capabilities),
# with their libraries, plus an lvm.conf for an initramfs without udev.
# Run as root on the phone (Debian arm64, lvm2 thin-provisioning-tools
# e2fsprogs tar installed). Writes /tmp/initramfs-extras-arm64.tar.gz; put it
# into downloads/ on the PC, scripts/make_boot.sh adds it to the initramfs.
set -eu
S=$(mktemp -d)
L=usr/lib/aarch64-linux-gnu
mkdir -p "$S/usr/sbin" "$S/usr/bin" "$S/$L" "$S/etc/lvm"
ln -s usr/lib "$S/lib"

add() {
	cp -L "$1" "$S$1"
	ldd "$1" | awk '/=>/ { print $3 } /^[ \t]*\// { print $1 }' | while read -r lib; do
		[ -f "$lib" ] || continue
		case "$lib" in
			*/ld-linux-*) cp -L "$lib" "$S/usr/lib/${lib##*/}" ;;
			*) cp -L "$lib" "$S/$L/${lib##*/}" ;;
		esac
	done
}

for b in /usr/sbin/lvm /usr/sbin/dmsetup /usr/sbin/pdata_tools /usr/sbin/mke2fs \
	/usr/sbin/e2fsck /usr/sbin/resize2fs /usr/sbin/tune2fs /usr/bin/tar; do
	add "$b"
done
ln -s pdata_tools "$S/usr/sbin/thin_check"
ln -s mke2fs "$S/usr/sbin/mkfs.ext4"
cp /etc/mke2fs.conf "$S/etc/"

cat > "$S/etc/lvm/lvm.conf" <<'EOF'
# initramfs: no udev, lvm makes the device nodes itself
devices {
	obtain_device_list_from_udev = 0
	external_device_info_source = "none"
}
activation {
	udev_sync = 0
	udev_rules = 0
	monitoring = 0
}
global {
	thin_check_executable = "/usr/sbin/thin_check"
}
backup {
	backup = 0
	archive = 0
}
EOF

tar -C "$S" -czf /tmp/initramfs-extras-arm64.tar.gz .
rm -rf "$S"
ls -la /tmp/initramfs-extras-arm64.tar.gz
