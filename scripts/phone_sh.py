"""Run one shell command on the bring-up initramfs over USB networking.

usage: python phone_sh.py "command" [host] [port]
"""
import socket
import sys

MARK = "__MIX2_RC__="


def run(cmd, host="172.16.42.1", port=5555, timeout=120):
    with socket.create_connection((host, port), timeout=10) as s:
        s.settimeout(timeout)
        # The remote shell exits after the command, which closes the connection
        s.sendall(f"( {cmd} ) 2>&1\necho {MARK}$?\nexit\n".encode())
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    out, _, status = data.decode(errors="replace").rpartition(MARK)
    return out, status.strip()


if __name__ == "__main__":
    host = sys.argv[2] if len(sys.argv) > 2 else "172.16.42.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 5555
    out, status = run(sys.argv[1], host, port)
    sys.stdout.write(out)
    sys.exit(int(status) if status.isdigit() else 1)
