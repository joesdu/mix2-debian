#!/bin/sh
clks() { grep -E "dsi0vco|dsi0_pll_out_div|dsi0_pll_bit|dsi0_pclk_mux|out_dsiclk|out_byteclk" /sys/kernel/debug/clk/clk_summary | sed -E "s/ +/ /g" | cut -d" " -f2,6; }
echo "== before"; clks; python3 /tmp/read_disp_regs.py | grep -E "CLK_CFG|frames|vco"
echo 0x02 > /sys/module/drm/parameters/debug
T=$(cut -d' ' -f1 /proc/uptime)
runuser -u mix2 -- env DISPLAY=:0 XAUTHORITY=/home/mix2/.Xauthority xset dpms force off
sleep 3
echo "== while off"; clks
runuser -u mix2 -- env DISPLAY=:0 XAUTHORITY=/home/mix2/.Xauthority xset dpms force on
sleep 4
echo 0 > /sys/module/drm/parameters/debug
echo "== after"; clks; python3 /tmp/read_disp_regs.py | grep -E "CLK_CFG|frames|vco"
dmesg | awk -v t=$T -F'[][]' '$2+0>t' | grep -i -E "pclk=|save_state|restore|PLL0 rate" | head -12
