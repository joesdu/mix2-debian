"""Find LZMA-compressed sections in the ABL image and dump interesting strings."""
import lzma, re, sys
data = open(sys.argv[1], "rb").read()
found = []
# LZMA (alone) streams in EDK2 FVs start with properties byte 0x5d and dict size
for m in re.finditer(rb"\x5d\x00\x00[\x00-\xff][\x00-\x02]", data, re.S):
    off = m.start()
    try:
        out = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(data[off:off + 4_000_000])
    except Exception:
        continue
    if len(out) > 100000:
        found.append(out)
blob = b"".join(found) if found else data
print("decompressed bytes:", sum(len(x) for x in found), file=sys.stderr)
pat = re.compile(rb"[ -~]{6,}")
upat = re.compile(rb"(?:[ -~]\x00){6,}")
strs = [s.decode() for s in pat.findall(blob)] + [s.decode("utf-16le") for s in upat.findall(blob)]
key = re.compile(sys.argv[2], re.I)
for s in strs:
    if key.search(s):
        print(s)
