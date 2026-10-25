#!/bin/bash
# Minimal Debian 13 arm64 build chroot (qemu) for rebuilding packages for the phone.
set -euo pipefail
C=/root/mix2/buildchroot
rm -rf "$C"
mmdebstrap --architectures=arm64 --variant=buildd \
	--include=devscripts,quilt,fakeroot,libqrtr-dev,libudev-dev,pkgconf \
	trixie "$C" \
	"deb http://deb.debian.org/debian trixie main" \
	"deb-src http://deb.debian.org/debian trixie main"
echo CHROOT_OK
