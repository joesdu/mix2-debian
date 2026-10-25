#!/bin/bash
# Build the Debian 13 (trixie) arm64 root filesystem for the Mi MIX 2.
# Must run as root inside WSL (uses qemu-user binfmt for the arm64 chroot).
# Output: /root/mix2/rootfs (directory tree)
set -euo pipefail

WORK=/root/mix2
ROOT=$WORK/rootfs
mkdir -p "$WORK"
rm -rf "$ROOT"

PKGS=(
	# base system
	systemd-sysv udev dbus dbus-user-session sudo locales tzdata ca-certificates
	apt-utils bash-completion nano vim-tiny less curl wget htop file usbutils
	i2c-tools evtest libinput-tools kmod iproute2 iputils-ping procps psmisc
	xz-utils e2fsprogs parted zram-tools systemd-timesyncd console-setup
	keyboard-configuration man-db
	# networking, Wi-Fi and Bluetooth
	openssh-server network-manager dnsmasq-base wpasupplicant iw wireless-regdb
	rfkill bluez
	# Qualcomm remote processor userspace (modem/Wi-Fi) and firmware
	rmtfs tqftpserv protection-domain-mapper qrtr-tools
	firmware-atheros firmware-qcom-soc
	# boot image tooling for kernel updates on the device itself
	mkbootimg
	# desktop
	xserver-xorg-core xserver-xorg-input-libinput xinit x11-xserver-utils
	xfce4 xfce4-terminal xfce4-power-manager xfce4-screenshooter
	xfce4-taskmanager xfce4-whiskermenu-plugin thunar mousepad ristretto
	lightdm lightdm-gtk-greeter network-manager-applet blueman onboard
	firefox-esr fonts-noto-cjk fonts-noto-color-emoji
	fcitx5 fcitx5-chinese-addons fcitx5-frontend-gtk3 fcitx5-frontend-qt5
	fcitx5-config-qt im-config
	gvfs polkitd pkexec xdg-utils adwaita-icon-theme upower mesa-utils
)

INCLUDE=$(IFS=,; echo "${PKGS[*]}")

mmdebstrap \
	--architectures=arm64 \
	--variant=important \
	--components="main contrib non-free non-free-firmware" \
	--include="$INCLUDE" \
	--aptopt='APT::Install-Recommends "false"' \
	--dpkgopt='path-exclude=/usr/share/doc/*' \
	--dpkgopt='path-include=/usr/share/doc/*/copyright' \
	trixie "$ROOT" \
	"deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware" \
	"deb http://deb.debian.org/debian trixie-updates main contrib non-free non-free-firmware" \
	"deb http://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware"

echo ROOTFS_BASE_OK
du -sh "$ROOT"
