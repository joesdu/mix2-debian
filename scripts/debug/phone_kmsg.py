"""Stream the kernel log of the bring-up initramfs over USB until the link dies.

usage: python phone_kmsg.py out.log [extra shell command run after the dump]
"""
import socket
import sys
import time

out = open(sys.argv[1], "w", encoding="utf-8", errors="replace")
extra = sys.argv[2] if len(sys.argv) > 2 else ""
deadline = time.time() + 300
while time.time() < deadline:
    try:
        s = socket.create_connection(("172.16.42.1", 5555), timeout=3)
        break
    except OSError:
        time.sleep(1)
else:
    sys.exit("rescue shell never came up")
print("connected", flush=True)
s.settimeout(240)
s.sendall(f"dmesg; {extra}\ncat /proc/kmsg\n".encode())
try:
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        text = chunk.decode(errors="replace")
        out.write(text)
        out.flush()
except OSError as e:
    out.write(f"\n### link lost: {e}\n")
print("stream ended", flush=True)
