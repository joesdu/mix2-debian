#!/bin/bash
# Rebuild Debian's rmtfs with the MSM8998 oem fsg patch, inside the arm64 build chroot.
# Output: /mnt/g/crazymix2/packages/rmtfs/rmtfs_*+mix2*_arm64.deb
set -euo pipefail

C=/root/mix2/buildchroot
WIN=/mnt/g/crazymix2
PATCH=serve-oem-fsg-from-files.patch

cp "$WIN/packages/rmtfs/$PATCH" "$C/tmp/$PATCH"
sed -i 's/\r$//' "$C/tmp/$PATCH"
cat > "$C/tmp/build.sh" <<'EOS'
set -euo pipefail
export DEBEMAIL="mix2@localhost" DEBFULLNAME="mix2 port" DEB_BUILD_OPTIONS=nocheck
mkdir -p /build && cd /build && rm -rf rmtfs-*
apt-get update -qq
apt-get source -qq rmtfs
DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq rmtfs >/dev/null
cd rmtfs-*/
mkdir -p debian/patches
cp /tmp/serve-oem-fsg-from-files.patch debian/patches/
echo serve-oem-fsg-from-files.patch >> debian/patches/series
dch --local +mix2 "Serve /boot/modem_fsg_oem_{1,2} from files (MSM8998 modem fatal error)."
dpkg-buildpackage -b -us -uc
ls -la /build/*.deb
EOS
chroot "$C" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LANG=C.UTF-8 bash /tmp/build.sh
cp "$C"/build/rmtfs_*_arm64.deb "$WIN/packages/rmtfs/"
ls -la "$WIN/packages/rmtfs/"
