"""Build an ath10k board-2.bin holding one board data file under several names.

usage: python make_board2.py <bdwlan.bin> <out board-2.bin> <board name>...
"""
import struct
import sys

MAGIC = b"QCA-ATH10K-BOARD\0"
IE_BOARD, IE_BOARD_NAME, IE_BOARD_DATA = 0, 0, 1


def ie(ie_id, payload):
    pad = (-len(payload)) % 4
    return struct.pack("<II", ie_id, len(payload)) + payload + b"\0" * pad


def main():
    bdf_path, out_path, *names = sys.argv[1:]
    data = open(bdf_path, "rb").read()
    blob = MAGIC + b"\0" * ((-len(MAGIC)) % 4)
    for name in names:
        blob += ie(IE_BOARD, ie(IE_BOARD_NAME, name.encode()) + ie(IE_BOARD_DATA, data))
    open(out_path, "wb").write(blob)
    print(f"{out_path}: {len(names)} entries, {len(data)} byte board data")


if __name__ == "__main__":
    main()
