#!/usr/bin/env python3
"""Packs files into a GTA SA IMG archive (version 2): python tools/make_img.py out.img file1 file2 ...

Used for coop\\cerberus.img: the Cerberus model (Manhunt) the mod loads as a special character."""
import os
import struct
import sys

SECTOR = 2048


def main():
    out, files = sys.argv[1], sys.argv[2:]
    header = 8 + 32 * len(files)
    pos = (header + SECTOR - 1) // SECTOR
    entries, blobs = [], []
    for f in files:
        data = open(f, "rb").read()
        sectors = (len(data) + SECTOR - 1) // SECTOR
        name = os.path.basename(f).encode("ascii")
        assert len(name) < 24, name
        entries.append(struct.pack("<IHH24s", pos, sectors, 0, name))
        blobs.append((pos, data))
        pos += sectors
    with open(out, "wb") as o:
        o.write(b"VER2" + struct.pack("<I", len(files)) + b"".join(entries))
        for at, data in blobs:
            o.seek(at * SECTOR)
            o.write(data)
        o.truncate(pos * SECTOR)


if __name__ == "__main__":
    main()
