#!/bin/bash
# Build the mainline kernel for Xiaomi Mi MIX 2 (chiron) inside WSL.
# Output: ~/mix2/out/{Image.gz,msm8998-xiaomi-chiron.dtb,Image.gz-dtb,modules/}
set -euo pipefail

SRC=~/mix2/linux-6.18.58
OUT=~/mix2/out
WIN=/mnt/g/crazymix2
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

cd "$SRC"
# Later patches may touch files of earlier ones, so a reverse dry-run cannot
# tell what is applied; keep a stamp per patch instead.
mkdir -p .mix2-patches
for p in "$WIN"/kernel/patches/*.patch; do
	[ -e "$p" ] || continue
	stamp=".mix2-patches/${p##*/}.applied"
	if [ -e "$stamp" ]; then
		echo "already applied: ${p##*/}"
	else
		patch -p1 -s -N < "$p"
		touch "$stamp"
		echo "applied: ${p##*/}"
	fi
done
cp "$WIN/kernel/msm8998-xiaomi-chiron.dts" arch/arm64/boot/dts/qcom/
# The phone's own Wi-Fi/Bluetooth addresses stay out of git: kernel/local.dtsi
# (see kernel/local.dtsi.example) is appended when it exists
LOCAL=arch/arm64/boot/dts/qcom/msm8998-xiaomi-chiron-local.dtsi
if [ -f "$WIN/kernel/local.dtsi" ]; then
	sed 's/\r$//' "$WIN/kernel/local.dtsi" > "$LOCAL"
	echo '#include "msm8998-xiaomi-chiron-local.dtsi"' >> arch/arm64/boot/dts/qcom/msm8998-xiaomi-chiron.dts
else
	rm -f "$LOCAL"
	echo "note: no kernel/local.dtsi, Wi-Fi/Bluetooth get no factory address"
fi
# Speaker link (QUAT MI2S -> TAS2559/TAS2560). The sound card waits for the
# amplifier module (kernel/tas2559, built below, loaded by mix2-audio.service)
sed 's/\r$//' "$WIN/kernel/tas2559/chiron-quat-experimental.dtsi" \
	> arch/arm64/boot/dts/qcom/msm8998-xiaomi-chiron-quat.dtsi
echo '#include "msm8998-xiaomi-chiron-quat.dtsi"' >> arch/arm64/boot/dts/qcom/msm8998-xiaomi-chiron.dts
grep -q msm8998-xiaomi-chiron.dtb arch/arm64/boot/dts/qcom/Makefile ||
	sed -i 's/^dtb-$(CONFIG_ARCH_QCOM)\t+= msm8998-xiaomi-sagit.dtb/&\ndtb-$(CONFIG_ARCH_QCOM)\t+= msm8998-xiaomi-chiron.dtb/' \
		arch/arm64/boot/dts/qcom/Makefile
grep -q msm8998-xiaomi-chiron.dtb arch/arm64/boot/dts/qcom/Makefile

if [ ! -f .config ] || [ "$WIN/kernel/chiron.config" -nt .config ]; then
	scripts/kconfig/merge_config.sh -m arch/arm64/configs/defconfig "$WIN/kernel/chiron.config"
	make olddefconfig
	# Report fragment options that did not survive dependency resolution
	while IFS= read -r line; do
		line=${line%$'\r'}	# the fragment may have Windows line ends
		case "$line" in CONFIG_*=*) grep -qxF "$line" .config || echo "NOT APPLIED: $line";; esac
	done < "$WIN/kernel/chiron.config"
fi

make -j"$(nproc)" Image.gz modules qcom/msm8998-xiaomi-chiron.dtb

rm -rf "$OUT"; mkdir -p "$OUT/modules"
cp arch/arm64/boot/Image.gz arch/arm64/boot/dts/qcom/msm8998-xiaomi-chiron.dtb .config "$OUT/"
cat "$OUT/Image.gz" "$OUT/msm8998-xiaomi-chiron.dtb" > "$OUT/Image.gz-dtb"
make -s INSTALL_MOD_PATH="$OUT/modules" INSTALL_MOD_STRIP=1 modules_install
make -s kernelrelease > "$OUT/kernelrelease"
# Out-of-tree modules: speaker amplifiers, warm resets for crash logs
UPD="$OUT/modules/lib/modules/$(cat "$OUT/kernelrelease")/updates"
mkdir -p "$UPD"
for m in tas2559 mix2-pon; do
	T=$(mktemp -d)
	cp "$WIN/kernel/$m"/*.[ch] "$WIN/kernel/$m/Makefile" "$T/"
	sed -i 's/\r$//' "$T"/*
	make -s -C "$SRC" M="$T" modules
	for ko in "$T"/*.ko; do
		"${CROSS_COMPILE}strip" --strip-debug -o "$UPD/${ko##*/}" "$ko"
	done
	rm -rf "$T"
done
echo "BUILD_OK $(cat "$OUT/kernelrelease")"
ls -la "$OUT"
