#!/usr/bin/env python3
"""Print the touchscreen axis ranges and every touch-down position for N seconds."""
import fcntl, os, select, struct, sys, time

dev = sys.argv[1]
secs = float(sys.argv[2])
fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
for code, name in ((0x35, "MT_X"), (0x36, "MT_Y"), (0x00, "X"), (0x01, "Y")):
    buf = bytearray(24)
    try:
        fcntl.ioctl(fd, 0x80184540 + code, buf)  # EVIOCGABS(code)
        val, mn, mx = struct.unpack("iii", bytes(buf[:12]))
        print(f"{name}: min={mn} max={mx}")
    except OSError:
        pass
fmt = "llHHi"
size = struct.calcsize(fmt)
x = y = None
down = False
end = time.time() + secs
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if not r:
        continue
    data = os.read(fd, size * 64)
    for i in range(0, len(data), size):
        _, _, typ, code, val = struct.unpack(fmt, data[i:i + size])
        if typ == 3 and code in (0x35, 0x00):
            x = val
        elif typ == 3 and code in (0x36, 0x01):
            y = val
        elif typ == 1 and code == 0x14a:  # BTN_TOUCH
            down = bool(val)
            if not down:
                print(f"touch at x={x} y={y}", flush=True)
