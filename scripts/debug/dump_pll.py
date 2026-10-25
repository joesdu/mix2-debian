#!/usr/bin/env python3
"""Dump the DSI0 PLL (0xc994a00, 0x1e0) and PHY common (0xc994400, 0x200) blocks."""
import mmap, os, struct
fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x0c994000)
for name, base, size in (("PLL", 0xa00, 0x1e0), ("CMN", 0x400, 0x200), ("LANE", 0x600, 0x280), ("DSI", 0x000, 0x200)):
    for off in range(0, size, 4):
        print("%s %03x %08x" % (name, off, struct.unpack("<I", m[base + off:base + off + 4])[0]))
