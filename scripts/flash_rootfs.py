"""Flash the Debian rootfs and boot image to a Mi MIX 2 that is in fastboot mode.

usage: python flash_rootfs.py [--yes]

The rootfs is written from the rescue initramfs over USB networking instead of
`fastboot flash userdata`, because the chiron bootloader corrupts sparse images
(FILL chunks are not written). Everything on the userdata partition is erased.
"""
import gzip
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from phone_sh import run  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(HERE, "..", "images")
IMG_GZ = os.path.join(IMAGES, "debian13-mix2-rootfs.img.gz")
SHA_FILE = os.path.join(IMAGES, "debian13-mix2-rootfs.img.sha256")
SIZE_FILE = os.path.join(IMAGES, "debian13-mix2-rootfs.img.size")
BOOT_IMG = os.path.join(IMAGES, "boot.img")
RESCUE_IMG = os.path.join(IMAGES, "boot-rescue.img")
CHUNK = 4 * 1024 * 1024


def fastboot(*args):
    print("+ fastboot", " ".join(args))
    subprocess.run(["fastboot", *args], check=True)


def wait_for(cond, what, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return
        time.sleep(2)
    sys.exit(f"timed out waiting for {what}")


def in_fastboot():
    out = subprocess.run(["fastboot", "devices"], capture_output=True, text=True).stdout
    return "fastboot" in out


def shell_up():
    try:
        return run("true", timeout=10)[1] == "0"
    except OSError:
        return False


def sh(cmd, timeout=120):
    out, status = run(cmd, timeout=timeout)
    if status != "0":
        sys.exit(f"phone command failed ({status}): {cmd}\n{out}")
    return out.strip()


def main():
    expected = open(SHA_FILE).read().split()[0]
    size = int(open(SIZE_FILE).read())
    if size % CHUNK:
        sys.exit("image size must be a multiple of 4 MiB")
    if "--yes" not in sys.argv:
        answer = input("This ERASES the phone's userdata partition. Type YES to continue: ")
        if answer != "YES":
            sys.exit("aborted")

    if not in_fastboot():
        sys.exit("phone is not in fastboot mode (hold Power + Volume down)")
    fastboot("boot", RESCUE_IMG)
    wait_for(shell_up, "rescue shell on 172.16.42.1", 120)

    dev = sh("for u in /sys/class/block/*/uevent; do grep -qx PARTNAME=userdata $u && "
             "sed -n 's/^DEVNAME=//p' $u; done")
    dev = "/dev/" + dev.splitlines()[0]
    part_size = int(sh(f"cat /sys/class/block/{dev[5:]}/size")) * 512
    if part_size < size:
        sys.exit(f"{dev} is smaller than the image")
    print(f"userdata is {dev} ({part_size / 2**30:.1f} GiB)")

    sh("(nc -l -p 6000 | dd of=%s bs=4M > /tmp/flash.log 2>&1; echo DD_RC=$? >> /tmp/flash.log; sync) "
       ">/dev/null 2>&1 &" % dev)
    time.sleep(2)
    sent, t0 = 0, time.time()
    with socket.create_connection(("172.16.42.1", 6000), timeout=60) as s, gzip.open(IMG_GZ, "rb") as f:
        while True:
            buf = f.read(CHUNK)
            if not buf:
                break
            s.sendall(buf)
            sent += len(buf)
            print(f"\r  written {sent / 2**20:8.0f} / {size / 2**20:.0f} MiB "
                  f"({sent / 2**20 / (time.time() - t0):5.1f} MiB/s)", end="", flush=True)
        s.shutdown(socket.SHUT_WR)
    print()
    wait_for(lambda: "DD_RC=" in run("cat /tmp/flash.log")[0], "dd to finish", 600)
    log = sh("cat /tmp/flash.log")
    if "DD_RC=0" not in log:
        sys.exit("dd failed:\n" + log)

    print("verifying ...")
    sh("(dd if=%s bs=4M count=%d 2>/dev/null | sha256sum > /tmp/sha.part && mv /tmp/sha.part /tmp/sha.txt) "
       ">/dev/null 2>&1 &" % (dev, size // CHUNK))
    wait_for(lambda: len(run("cat /tmp/sha.txt 2>/dev/null")[0].split()) > 0, "checksum", 900)
    got = sh("cat /tmp/sha.txt").split()[0]
    if got != expected:
        sys.exit(f"checksum mismatch: phone {got} != image {expected}")
    print("rootfs verified:", got)

    run("(sleep 1; mix2-reboot bootloader) >/dev/null 2>&1 &")
    wait_for(in_fastboot, "fastboot", 90)
    fastboot("flash", "boot", BOOT_IMG)
    fastboot("reboot")
    print("done: the phone is booting Debian")


if __name__ == "__main__":
    main()
