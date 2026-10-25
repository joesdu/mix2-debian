#!/usr/bin/env python3
"""Dump the DSI0 controller and DPU INTF1 timing the bootloader programmed."""
import mmap, os, struct, time

fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)

def block(base, size=0x1000):
    return mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)

def rd(m, off):
    return struct.unpack("<I", m[off:off + 4])[0]

dsi = block(0x0c994000)
print("DSI hw version  %08x" % rd(dsi, 0x0))
for name, off in (("CTRL", 0x04), ("VID_CFG0", 0x10), ("VID_CFG1", 0x20),
                  ("ACTIVE_H", 0x24), ("ACTIVE_V", 0x28), ("TOTAL", 0x2c),
                  ("ACTIVE_HSYNC", 0x30), ("VSYNC_HPOS", 0x34), ("VSYNC_VPOS", 0x38),
                  ("LANE_CTRL", 0xac), ("CLKOUT_TIMING", 0xc4), ("EOT_PACKET", 0xcc),
                  ("CLK_CTRL", 0x11c)):
    print("DSI %-14s %08x" % (name, rd(dsi, off)))

intf = block(0x0c96b000)
o = 0x800
for name, off in (("TIMING_EN", 0x0), ("HSYNC_CTL", 0x8), ("VSYNC_PERIOD", 0xc),
                  ("VSYNC_PW", 0x14), ("DISP_V_START", 0x1c), ("DISP_V_END", 0x24),
                  ("ACTIVE_V_START", 0x2c), ("DISP_HCTL", 0x3c), ("ACTIVE_HCTL", 0x40),
                  ("POLARITY", 0x50), ("PANEL_FORMAT", 0x90)):
    print("INTF1 %-14s %08x" % (name, rd(intf, o + off)))
f0 = rd(intf, o + 0xac)
time.sleep(2)
f1 = rd(intf, o + 0xac)
print("INTF1 frames/s %.2f" % ((f1 - f0) / 2))

phy = block(0x0c994000)
P, L = 0x400, 0xa00
for name, off in [("CLK_CFG0", 0x10), ("CLK_CFG1", 0x14), ("GLBL_CTRL", 0x18), ("CTRL_0", 0x24)] + \
                 [("TIMING_%d" % i, 0xac + 4 * i) for i in range(12)]:
    print("PHY %-14s %08x" % (name, rd(phy, P + off)))
dec = rd(phy, L + 0xcc) & 0xff
frac = (rd(phy, L + 0xd0) & 0xff) | ((rd(phy, L + 0xd4) & 0xff) << 8) | ((rd(phy, L + 0xd8) & 0x3) << 16)
outdiv = rd(phy, L + 0x140) & 0x3
cfg0 = rd(phy, P + 0x10)
vco2 = 19.2e6 * 2 * (dec + frac / 2**18)
vco1 = 19.2e6 * (dec + frac / 2**18)
bitdiv = (cfg0 & 0xf) or 1
pixdiv = (cfg0 >> 4) or 1
mux = rd(phy, P + 0x14) & 3
for name, vco in (("mainline x2", vco2), ("porthole x1", vco1)):
    bit = vco / (1 << outdiv) / bitdiv
    src = [bit, bit / 2, vco / (1 << outdiv), vco / (1 << outdiv) / 4][mux]
    print("%-12s vco %7.1f MHz  bitclk %6.1f MHz  pclk %6.1f MHz" % (name, vco / 1e6, bit / 1e6, src / pixdiv / 1e6))
