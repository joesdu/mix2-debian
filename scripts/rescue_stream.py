"""Stream data to or from the MIX 2 rescue shell (raw sh on port 5555).

  rescue_stream.py get "<command writing to stdout>" out.file
  rescue_stream.py put "<command reading stdin>" in.file

The command runs with exec, so the connection closes when it exits.
"""
import socket
import sys
import time

HOST, PORT = "172.16.42.1", 5555
mode, cmd, path = sys.argv[1:4]

s = socket.create_connection((HOST, PORT), timeout=30)
s.settimeout(600)
s.sendall(("exec " + cmd + "\n").encode())
t0, n = time.time(), 0
if mode == "get":
    with open(path, "wb") as f:
        while True:
            b = s.recv(1 << 20)
            if not b:
                break
            f.write(b)
            n += len(b)
else:
    time.sleep(1.5)  # let the shell read only the command line
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            s.sendall(b)
            n += len(b)
    s.shutdown(socket.SHUT_WR)
    rest = b""
    while True:
        b = s.recv(65536)
        if not b:
            break
        rest += b
    if rest:
        sys.stdout.write(rest.decode(errors="replace"))
dt = time.time() - t0
print("%s %d bytes in %.0f s (%.1f MB/s)" % (mode, n, dt, n / max(dt, 1) / 1e6))
