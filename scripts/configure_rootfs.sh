#!/bin/bash
# Configure the Debian rootfs for the Mi MIX 2 and install kernel + firmware.
# Run as root inside WSL after build_rootfs.sh and build_kernel.sh.
set -euo pipefail

ROOT=/root/mix2/rootfs
WIN=/mnt/g/crazymix2
KOUT=/home/joe/mix2/out
KVER=$(cat "$KOUT/kernelrelease")
FW=$ROOT/lib/firmware/qcom/msm8998/xiaomi/chiron
USER_NAME=mix2
USER_PASS=mix2

in_root() { chroot "$ROOT" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

# --- packages that are only Recommends of the desktop and needed here
mount --bind /proc "$ROOT/proc"; mount --bind /sys "$ROOT/sys"; mount --bind /dev "$ROOT/dev"
trap 'umount "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" 2>/dev/null || true' EXIT
rm -f "$ROOT/etc/resolv.conf"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"
in_root apt-get update
in_root apt-get install -y --no-install-recommends at-spi2-core \
	xfce4-notifyd gnome-themes-extra elementary-xfce-icon-theme \
	dosfstools xinput xserver-xorg-video-fbdev libglib2.0-bin dconf-cli libnotify-bin
in_root apt-get clean

# --- identity
echo mix2 > "$ROOT/etc/hostname"
cat > "$ROOT/etc/hosts" <<EOF
127.0.0.1	localhost
127.0.1.1	mix2
::1		localhost ip6-localhost ip6-loopback
EOF
cat > "$ROOT/etc/fstab" <<EOF
# The initramfs mounts the userdata partition (label mix2-root) as /
LABEL=mix2-root	/	ext4	defaults,noatime,errors=remount-ro	0	1
tmpfs		/tmp	tmpfs	defaults,nosuid,nodev,size=1G	0	0
EOF

# --- locale and time zone (Chinese desktop, English available)
sed -i -e 's/^# *\(zh_CN.UTF-8 UTF-8\)/\1/' -e 's/^# *\(en_US.UTF-8 UTF-8\)/\1/' "$ROOT/etc/locale.gen"
in_root locale-gen
echo 'LANG=zh_CN.UTF-8' > "$ROOT/etc/default/locale"
ln -sf /usr/share/zoneinfo/Asia/Shanghai "$ROOT/etc/localtime"
echo Asia/Shanghai > "$ROOT/etc/timezone"
# Large console font for the 1080x2160 panel
sed -i -e 's/^FONTFACE=.*/FONTFACE="Terminus"/' -e 's/^FONTSIZE=.*/FONTSIZE="16x32"/' "$ROOT/etc/default/console-setup"

# --- user
for g in sudo video audio input netdev plugdev bluetooth render; do
	in_root getent group "$g" >/dev/null || in_root groupadd -r "$g"
done
if ! in_root id "$USER_NAME" >/dev/null 2>&1; then
	in_root useradd -m -s /bin/bash -G sudo,video,audio,input,netdev,plugdev,bluetooth,render "$USER_NAME"
fi
echo "$USER_NAME:$USER_PASS" | in_root chpasswd
in_root passwd -l root

# --- desktop: autologin into XFCE, touch friendly scaling
mkdir -p "$ROOT/etc/lightdm/lightdm.conf.d"
cat > "$ROOT/etc/lightdm/lightdm.conf.d/50-mix2-autologin.conf" <<EOF
[Seat:*]
autologin-user=$USER_NAME
autologin-user-timeout=0
autologin-session=xfce
user-session=xfce
EOF
XFCONF=$ROOT/home/$USER_NAME/.config/xfce4/xfconf/xfce-perchannel-xml
mkdir -p "$XFCONF"
cat > "$XFCONF/xsettings.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xsettings" version="1.0">
  <property name="Net" type="empty">
    <property name="ThemeName" type="string" value="Adwaita"/>
    <property name="IconThemeName" type="string" value="elementary-xfce"/>
  </property>
  <property name="Xft" type="empty">
    <property name="DPI" type="int" value="180"/>
    <property name="Antialias" type="int" value="1"/>
    <property name="HintStyle" type="string" value="hintslight"/>
  </property>
  <property name="Gtk" type="empty">
    <property name="CursorThemeSize" type="int" value="48"/>
    <property name="FontName" type="string" value="Noto Sans CJK SC 10"/>
    <property name="MonospaceFontName" type="string" value="Noto Sans Mono CJK SC 10"/>
  </property>
</channel>
EOF
cat > "$XFCONF/xfwm4.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0">
  <property name="general" type="empty">
    <property name="theme" type="string" value="Default-xhdpi"/>
    <property name="title_font" type="string" value="Noto Sans CJK SC Bold 9"/>
    <property name="use_compositing" type="bool" value="false"/>
  </property>
</channel>
EOF
cat > "$XFCONF/xfce4-power-manager.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-power-manager" version="1.0">
  <property name="xfce4-power-manager" type="empty">
    <property name="power-button-action" type="uint" value="0"/>
    <property name="dpms-enabled" type="bool" value="false"/>
    <property name="blank-on-ac" type="int" value="0"/>
    <property name="blank-on-battery" type="int" value="0"/>
    <property name="lock-screen-suspend-hibernate" type="bool" value="false"/>
  </property>
</channel>
EOF
# On-screen keyboard and input method start with the session
mkdir -p "$ROOT/home/$USER_NAME/.config/autostart"
cp "$ROOT/usr/share/applications/onboard.desktop" "$ROOT/home/$USER_NAME/.config/autostart/" 2>/dev/null || true
mkdir -p "$ROOT/home/$USER_NAME/.config/dconf"
rm -f "$ROOT/etc/X11/Xsession.d/90mix2-onboard"
# Finger-sized panels (56 px top bar, 88 px dock, single-line clock) tuned on the phone;
# Onboard's docking defaults come from the gschema override in overlay/
cp -r "$WIN/skel/." "$ROOT/home/$USER_NAME/"
find "$ROOT/home/$USER_NAME" -type f -name "*.xml" -exec sed -i 's/\r$//' {} +
# SSH key of the build machine for mix2ssh / deploy_kernel.sh (keys/ is not in git:
# ssh-keygen -t ed25519 -N "" -f keys/mix2_ed25519)
if [ -f "$WIN/keys/mix2_ed25519.pub" ]; then
	install -d -m 700 "$ROOT/home/$USER_NAME/.ssh"
	install -m 600 "$WIN/keys/mix2_ed25519.pub" "$ROOT/home/$USER_NAME/.ssh/authorized_keys"
fi
in_root chown -R "$USER_NAME:$USER_NAME" "/home/$USER_NAME"

# Screen rotation (mix2-rotate, Super+O / Super+arrows, panel button) comes from overlay/ and skel/

# --- power handling: suspend is not validated on this kernel yet.
# A short power key press blanks/wakes the screen (mix2-power-key.service,
# X ignores the key); a long press powers off.
mkdir -p "$ROOT/etc/systemd/logind.conf.d"
cat > "$ROOT/etc/systemd/logind.conf.d/50-mix2.conf" <<EOF
[Login]
HandlePowerKey=ignore
HandlePowerKeyLongPress=poweroff
HandleLidSwitch=ignore
HandleLidSwitchExternalPower=ignore
HandleLidSwitchDocked=ignore
EOF
for t in sleep suspend hibernate hybrid-sleep suspend-then-hibernate; do
	ln -sf /dev/null "$ROOT/etc/systemd/system/$t.target"
done

# --- zram swap
cat > "$ROOT/etc/default/zramswap" <<EOF
ALGO=zstd
PERCENT=50
PRIORITY=100
EOF

# --- USB networking to the PC: phone 172.16.42.1, PC gets 172.16.42.2 via DHCP.
# NetworkManager leaves USB gadget interfaces unmanaged (85-nm-unmanaged.rules),
# so usb0 is configured by mix2-usb-gadget and served by a private dnsmasq.
cp -r "$WIN/overlay/." "$ROOT/"
(cd "$WIN/overlay" && find . -type f) | while read -r f; do
	sed -i 's/\r$//' "$ROOT/$f"
	chown root:root "$ROOT/$f"
	case "$f" in
		*/bin/*|*/sbin/*) chmod 755 "$ROOT/$f" ;;
		./etc/sudoers.d/*) chmod 440 "$ROOT/$f" ;;
		*) chmod 644 "$ROOT/$f" ;;
	esac
done
rm -f "$ROOT/etc/NetworkManager/system-connections/usb0.nmconnection"
in_root glib-compile-schemas /usr/share/glib-2.0/schemas

# --- grow the root filesystem to the whole userdata partition on first boot
cat > "$ROOT/etc/systemd/system/mix2-growfs.service" <<'EOF'
[Unit]
Description=Grow the root filesystem to fill the userdata partition
ConditionPathExists=!/var/lib/mix2/growfs-done

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'resize2fs "$(findmnt -n -o SOURCE /)" && mkdir -p /var/lib/mix2 && touch /var/lib/mix2/growfs-done'

[Install]
WantedBy=multi-user.target
EOF

# --- Qualcomm remote processor daemons
# rmtfs serves the modem's EFS partitions (modemst1/modemst2/fsg) like Android's rmt_storage
in_root systemctl enable mix2-usb-gadget.service mix2-usb-dhcp.service mix2-growfs.service \
	mix2-power-key.service ssh.service NetworkManager.service lightdm.service zramswap.service
for s in rmtfs tqftpserv pd-mapper; do in_root systemctl enable "$s.service" || true; done
# The QRTR name service lives in the kernel since 5.7; the userspace one only conflicts
in_root systemctl disable qrtr-ns.service || true

# Rebuilt rmtfs that also serves /boot/modem_fsg_oem_{1,2} (no such partitions;
# rejecting them makes the modem take a fatal error) and diag-router, which
# drains the modem DIAG channel (otherwise the modem watchdog kills the WLAN
# process every ~19 s). Both found by the Pixel 2 port in porthole-dev/pmaports.
cp "$WIN"/packages/rmtfs/rmtfs_*+mix2*_arm64.deb "$WIN"/packages/diag/qcom-diag_*_arm64.deb "$ROOT/tmp/"
in_root sh -c 'dpkg -i /tmp/rmtfs_*+mix2*_arm64.deb /tmp/qcom-diag_*_arm64.deb && rm -f /tmp/*.deb'
in_root apt-mark hold rmtfs
in_root systemctl enable diag-router.service
cat > "$ROOT/etc/tmpfiles.d/rmtfs-oem.conf" <<EOF
d /var/lib/rmtfs 0700 root root -
f /var/lib/rmtfs/modem_fsg_oem_1 0600 root root -
f /var/lib/rmtfs/modem_fsg_oem_2 0600 root root -
EOF
# tqftpserv's /readwrite root lives on tmpfs; the modem asks for this file every boot
cat > "$ROOT/etc/tmpfiles.d/tqftpserv-ota.conf" <<EOF
d /tmp/tqftpserv/ota_firewall 0755 root root -
f /tmp/tqftpserv/ota_firewall/ruleset 0644 root root -
EOF
# Keep the permanent MAC for scans; the WLAN.HL.1.0 firmware is fragile enough
mkdir -p "$ROOT/etc/NetworkManager/conf.d"
cat > "$ROOT/etc/NetworkManager/conf.d/50-mix2-wifi.conf" <<EOF
[device]
wifi.scan-rand-mac-address=no
EOF

# --- kernel, modules and boot image tooling
rm -rf "$ROOT/lib/modules"
mkdir -p "$ROOT/lib/modules"
cp -a "$KOUT/modules/lib/modules/$KVER" "$ROOT/lib/modules/"
rm -f "$ROOT/lib/modules/$KVER/build" "$ROOT/lib/modules/$KVER/source"
in_root depmod -a "$KVER"
mkdir -p "$ROOT/boot/mix2"
cp "$KOUT/Image.gz" "$KOUT/msm8998-xiaomi-chiron.dtb" "$KOUT/.config" "$ROOT/boot/mix2/"
cp "$KOUT/initramfs.cpio.gz" "$ROOT/boot/mix2/initramfs.cpio.gz"
echo "$KVER" > "$ROOT/boot/mix2/kernelrelease"
cp "$WIN/scripts/mix2-flash-boot" "$ROOT/usr/local/sbin/mix2-flash-boot"
sed -i 's/\r$//' "$ROOT/usr/local/sbin/mix2-flash-boot"
chmod 755 "$ROOT/usr/local/sbin/mix2-flash-boot"

# --- device firmware taken from this phone's own Android partitions
TMP=$(mktemp -d)
tar -xf "$WIN/backup/firmware/android-firmware.tar" -C "$TMP"
mkdir -p "$FW"
IMG=$TMP/vendor/firmware_mnt/image
cp "$IMG"/mba.mbn "$IMG"/modem.* "$IMG"/adsp.* "$IMG"/slpi_v2.* "$IMG"/wlanmdsp.mbn "$FW/"
cp "$IMG"/*.jsn "$FW/" 2>/dev/null || true
# Carrier/hardware MCFG configs the modem fetches over TFTP
cp -r "$IMG/modem_pr" "$FW/"
cp "$TMP"/vendor/firmware/a540_zap.* "$FW/"
cp "$TMP"/vendor/firmware/a540_gpmu.fw2 "$ROOT/lib/firmware/qcom/a540_gpmu.fw2"
# Wi-Fi board data for ath10k (WCN3990): linux-firmware's board-2.bin would
# match this chip with the Thundercomm DB845C data, so override it with this
# phone's bdwlan.bin (also as the xiaomi_chiron calibration variant)
U=$ROOT/lib/firmware/updates
mkdir -p "$U/ath10k/WCN3990/hw1.0" "$U/qca"
python3 "$WIN/tools/make_board2.py" "$IMG/bdwlan.bin" "$U/ath10k/WCN3990/hw1.0/board-2.bin" \
	"bus=snoc,qmi-board-id=ff,qmi-chip-id=30214,variant=xiaomi_chiron" \
	"bus=snoc,qmi-board-id=ff,qmi-chip-id=30214"
# Bluetooth: the stock rampatch and NVM belong together (linux-firmware's
# rampatch with this NVM fails LE setup)
cp "$TMP"/vendor/bt_firmware/image/crbtfw21.tlv "$TMP"/vendor/bt_firmware/image/crnv21.bin "$U/qca/"
rm -rf "$TMP"

# --- misc
truncate -s 0 "$ROOT/etc/machine-id"
rm -f "$ROOT/var/lib/dbus/machine-id"
rm -f "$ROOT/etc/resolv.conf"
ln -sf ../run/NetworkManager/resolv.conf "$ROOT/etc/resolv.conf"

umount "$ROOT/proc" "$ROOT/sys" "$ROOT/dev"
trap - EXIT
echo CONFIGURE_OK
du -sh "$ROOT"
