#!/usr/bin/env python3
"""
The interior each cutscene plays in, for the review harness's bot_cutscenes (NAME:area,...).

    python tools/cs_areas.py NAME... [--game DIR]

The cutscene's offset (its .cut in anim/cuts.img) is checked against the interior field of the map's instances
around it: text IPLs and the binary stream IPLs inside models/gta3.img and gta_int.img (where the interiors are).
"""
import math, re, struct, sys
from pathlib import Path
args = [a for a in sys.argv[1:]]
G = Path(r"E:/GTA San Andreas")
if "--game" in args:
    i = args.index("--game"); G = Path(args[i + 1]); del args[i:i + 2]
inst = []
for ipl in (G / "data/maps").rglob("*.ipl"):
    section = None
    for line in ipl.read_text(errors="replace").splitlines():
        t = line.split("#")[0].strip().lower()
        if t in ("inst", "cull", "grge", "enex", "pick", "cars", "jump", "tcyc", "auzo", "mult", "occl", "zone", "path"):
            section = t; continue
        if t == "end": section = None; continue
        if section == "inst":
            p = [x.strip() for x in line.split(",")]
            if len(p) >= 6:
                try: inst.append((int(p[2]), float(p[3]), float(p[4]), float(p[5])))
                except ValueError: pass
# the interiors are binary stream IPLs inside the IMG archives: "bnry", count at +4, offset at +0x1C, 40-byte entries
for img in ("models/gta3.img", "models/gta_int.img"):
    raw = (G / img).read_bytes()
    for i in range(struct.unpack_from("<I", raw, 4)[0]):
        off, s1, s2 = struct.unpack_from("<IHH", raw, 8 + i * 32)
        name = raw[8 + i * 32 + 8:8 + i * 32 + 32].split(b"\0")[0].decode("latin1").lower()
        if not name.endswith(".ipl"): continue
        b = raw[off * 2048:(off + (s1 or s2)) * 2048]
        if b[:4] != b"bnry": continue
        n, at = struct.unpack_from("<I", b, 4)[0], struct.unpack_from("<I", b, 0x1C)[0]
        for k in range(n):
            x, y, z = struct.unpack_from("<3f", b, at + k * 40)
            inst.append((struct.unpack_from("<i", b, at + k * 40 + 32)[0], x, y, z))
data = (G / "anim/cuts.img").read_bytes()
offsets = {}
for i in range(struct.unpack_from("<I", data, 4)[0]):
    off, s1, s2 = struct.unpack_from("<IHH", data, 8 + i * 32)
    name = data[8 + i * 32 + 8:8 + i * 32 + 32].split(b"\0")[0].decode("latin1")
    if name.lower().endswith(".cut"):
        m = re.search(rb"offset\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)", data[off * 2048:(off + (s1 or s2)) * 2048])
        if m: offsets[name[:-4].upper()] = tuple(map(float, m.groups()))
out = []
for name in args:
    o = offsets.get(name.upper())
    votes = {}
    # the nearest objects weigh most: interiors sit side by side (the diners of DATE3A and DATE4A are 21 m apart)
    for a, x, y, z in inst:
        d = math.dist((x, y, z), o)
        if a and a != 13 and d < 25: votes[a] = votes.get(a, 0) + 1 / (1 + d * d)
    out.append(f"{name}:{max(votes, key=votes.get) if votes and o[2] > 500 else 0}")
print(",".join(out))
