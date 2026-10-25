"""Extract the appended DTB from an Android boot.img v0 (Image.gz-dtb)."""
import struct
import sys
import zlib

img = open(sys.argv[1], "rb").read()
ksize, = struct.unpack_from("<I", img, 8)
page, = struct.unpack_from("<I", img, 36)
kernel = img[page:page + ksize]
d = zlib.decompressobj(16 + zlib.MAX_WBITS)
d.decompress(kernel)
tail = d.unused_data
assert tail[:4] == b"\xd0\x0d\xfe\xed", tail[:8]
size, = struct.unpack_from(">I", tail, 4)
open(sys.argv[2], "wb").write(tail[:size])
print("dtb", size, "bytes")
