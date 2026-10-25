#!/bin/bash
# Build diag-router (linux-msm/diag) for arm64 in the build chroot and pack it as a .deb.
# The modem's DIAG task starves without something draining the DIAG QRTR
# channel, and the modem watchdog (dog_hb) then kills the WLAN PD every ~19 s.
set -euo pipefail

C=/root/mix2/buildchroot
WIN=/mnt/g/crazymix2
COMMIT=29277ef0bcbe22468a3836b796dc525ba6df87f3
VER=0~git20250324-1+mix2

mkdir -p "$WIN/packages/diag"
curl -sSL -o "$C/tmp/diag.tar.gz" "https://github.com/linux-msm/diag/archive/$COMMIT.tar.gz"
cat > "$C/tmp/build-diag.sh" <<EOS
set -euo pipefail
rm -rf /build/diag && mkdir -p /build/diag && cd /build/diag
tar -xzf /tmp/diag.tar.gz --strip-components=1
make prefix=/usr all
rm -rf /build/diag-pkg && mkdir -p /build/diag-pkg/DEBIAN /build/diag-pkg/usr/lib/systemd/system
make prefix=/usr DESTDIR=/build/diag-pkg install
cat > /build/diag-pkg/usr/lib/systemd/system/diag-router.service <<'EOF'
[Unit]
Description=Routing of diagnostics related messages between host and various subsystems
Before=rmtfs.service

[Service]
ExecStart=/usr/bin/diag-router
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF
cat > /build/diag-pkg/DEBIAN/control <<'EOF'
Package: qcom-diag
Version: $VER
Architecture: arm64
Maintainer: mix2 port <mix2@localhost>
Depends: libc6, libudev1, libqrtr1
Description: Qualcomm DIAG router (linux-msm/diag)
 Drains the modem DIAG channel over QRTR so the MSM8998 modem watchdog
 does not starve and kill the WLAN process.
EOF
dpkg-deb --root-owner-group --build /build/diag-pkg /build/qcom-diag_${VER}_arm64.deb
dpkg-deb -c /build/qcom-diag_${VER}_arm64.deb
EOS
chroot "$C" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LANG=C.UTF-8 bash /tmp/build-diag.sh
cp "$C"/build/qcom-diag_*_arm64.deb "$WIN/packages/diag/"
ls -la "$WIN/packages/diag/"
