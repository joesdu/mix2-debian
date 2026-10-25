echo "== cmdline"; cat /proc/cmdline; echo
echo "== board-id"; od -An -tx4 /proc/device-tree/qcom,board-id; echo "== msm-id"; od -An -tx4 /proc/device-tree/qcom,msm-id; echo "== model"; cat /proc/device-tree/model; echo
echo "== i2c"; for d in /sys/bus/i2c/devices/*; do n=$(cat $d/name 2>/dev/null); drv=$(readlink $d/driver 2>/dev/null); echo "$d $n ${drv##*/}"; done
echo "== input"; grep -E "^N:|^S:" /proc/bus/input/devices
echo "== fb0"; for f in modes virtual_size bits_per_pixel stride name; do echo "$f: $(cat /sys/class/graphics/fb0/$f 2>/dev/null)"; done
cat /sys/class/graphics/fb0/msm_fb_panel_info 2>/dev/null | head -30
echo "== meminfo"; head -2 /proc/meminfo
echo "== touch dmesg"; dmesg | grep -i -E "synaptics|ft5x46|ft8716|focal|panel|mdss_dsi_panel" | head -25
echo "== storage"; df -h /data /sdcard 2>/dev/null; du -sh /data/media/0 2>/dev/null
echo "== partitions"; ls -l /dev/block/bootdevice/by-name/ | awk '{print $(NF-2), $NF}'
