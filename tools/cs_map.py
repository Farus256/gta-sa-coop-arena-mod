#!/usr/bin/env python3
"""
Cutscene review maps: the plan of the place around CJ (from the game's collision) next to the shot itself.

    python tools/cs_map.py <csreview dir> [game dir]

The review harness (bot_cutscenes) leaves per shot NAME_NN.bmp (the frame), NAME_NN.txt (camera, CJ, actors,
every spot tried for the partner and why it was dropped) and, once per place, NAME_NN_world.txt (collision
triangles, boxes and spheres of the buildings and objects around CJ, in world coordinates). Out comes
NAME_NN_map.jpg: the frame on the left, the plan on the right.

Plan: floor light grey; seat-high surfaces tan, table-high orange, shelves brown (a darker shade is higher);
walls cut at 1 m black, furniture cut at 0.45 m brown. Camera yellow with its field of view, CJ white,
actors blue (filled: in the frame). Spots tried: circle stand, square lean, triangle sit/steps; green taken
into account (brighter is better), grey out of frame, red solid, orange crowd, magenta covers CJ, purple covers
an actor, cyan not seen, dark grey distance, pink in the way of the scene's people, blue fails later in the shot.
The chosen one is the big star with its heading. The rest of the shot: the camera's path in small yellow dots (its
view at the end of the shot in thin lines), CJ's and the actors' tracks in small white and blue dots. The frame just
before the cut (NAME_NN_end.bmp) goes under the first one.
"""
import math
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

GAME = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(r"E:/GTA San Andreas")
SRC = Path(sys.argv[1])
SCALE = 40.0  # px per metre
MAP = 720
POSES = ["stand", "lean", "sit", "steps", "hidden"]
REJECT_COLOURS = {0: (90, 90, 90), 1: (255, 150, 0), 2: (170, 170, 170), 3: (255, 0, 255), 4: (150, 60, 220),
                  5: (230, 30, 30), 6: (0, 220, 230), 7: (120, 60, 30), 8: (255, 140, 180), 9: (70, 110, 255)}
REJECT_NAMES = ["distance", "crowd", "frame", "covers CJ", "covers actor", "solid", "not seen", "floor", "in the way",
                "later in the shot"]


def model_names():
    names = {}
    ides = []
    for dat in ("data/default.dat", "data/gta.dat"):
        p = GAME / dat
        if p.exists():
            for line in p.read_text(errors="replace").splitlines():
                m = re.match(r"\s*IDE\s+(\S+)", line, re.I)
                if m:
                    ides.append(GAME / m.group(1).replace("\\", "/"))
    for ide in ides:
        if not ide.exists():
            continue
        section = None
        for line in ide.read_text(errors="replace").splitlines():
            line = line.split("#")[0].strip()
            if not line:
                continue
            if line.lower() in ("objs", "tobj", "anim", "peds", "cars", "weap", "hier", "txdp", "2dfx", "path"):
                section = line.lower()
                continue
            if line.lower() == "end":
                section = None
                continue
            if section in ("objs", "tobj", "anim", "hier", "peds", "weap"):
                parts = [x.strip() for x in line.split(",")]
                if len(parts) > 1 and parts[0].isdigit():
                    names[int(parts[0])] = parts[1]
    return names


def read_world(path):
    ents, tris, spheres = [], [], []
    render, props, objs = [], [], []
    missing = 0
    for line in path.read_text().splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == "E":
            ents.append((int(p[1]), int(p[2]), float(p[3]), float(p[4]), float(p[5])))
        elif p[0] == "T":
            v = list(map(float, p[1:10]))
            tris.append(((v[0], v[1], v[2]), (v[3], v[4], v[5]), (v[6], v[7], v[8])))
        elif p[0] == "B":
            c = list(map(float, p[2:26]))
            pts = [(c[i * 3], c[i * 3 + 1], c[i * 3 + 2]) for i in range(8)]
            for a, b, cc, d in ((0, 2, 6, 4), (1, 3, 7, 5), (0, 1, 5, 4), (2, 3, 7, 6), (0, 1, 3, 2), (4, 5, 7, 6)):
                tris.append((pts[a], pts[b], pts[cc]))
                tris.append((pts[a], pts[cc], pts[d]))
        elif p[0] == "S":
            spheres.append(tuple(map(float, p[1:5])))
        elif p[0] in ("R", "Q"):
            v = list(map(float, p[1:10]))
            (render if p[0] == "R" else props).append(((v[0], v[1], v[2]), (v[3], v[4], v[5]), (v[6], v[7], v[8])))
        elif p[0] == "P":
            objs.append((int(p[1]), int(p[2]), float(p[3]), float(p[4]), float(p[5])))
        elif p[0] == "#":
            missing += 1
    if missing:
        print(f"{path.name}: {missing} atomics without geometry in memory")
    return ents, tris, spheres, render, props, objs


def read_shot(path):
    shot = {"actors": [], "tries": [], "gt": [], "gs": [], "ahead": []}
    for line in path.read_text().splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == "shot":
            shot["name"], shot["n"] = p[1], int(p[2])
        elif p[0] == "cam":
            shot["cam"] = tuple(map(float, p[1:8]))
        elif p[0] == "cj":
            shot["cj"] = tuple(map(float, p[1:4]))
            shot["floor"] = float(p[4])
        elif p[0] == "actor":
            shot["actors"].append((float(p[1]), float(p[2]), float(p[3]), int(p[4])))
        elif p[0] == "spot":
            shot["spot"] = (float(p[1]), float(p[2]), float(p[3]), float(p[4]), int(p[5]), int(p[6]))
        elif p[0] == "try":
            shot["tries"].append((float(p[1]), float(p[2]), float(p[3]), float(p[4]), int(p[5]), float(p[6]), int(p[7])))
        elif p[0] == "grid":
            shot["grid"] = (float(p[1]), float(p[2]), float(p[3]), int(p[4]), float(p[5]))
        elif p[0] in ("gt", "gs"):
            shot[p[0]].append(bytes.fromhex(p[1]))
        elif p[0] == "ahead":
            v = list(map(float, p[1:]))
            shot["ahead"].append((v[0], tuple(v[1:4]), tuple(v[4:7]), tuple(v[7:9]),
                                  [tuple(v[9 + i:11 + i]) for i in range(0, len(v) - 9, 2)]))
    return shot


def normal(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    l = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return nx / l, ny / l, nz / l


def cut(t, z):
    """Segment where the triangle crosses the plane at height z."""
    pts = []
    for i in range(3):
        a, b = t[i], t[(i + 1) % 3]
        if (a[2] - z) * (b[2] - z) < 0:
            k = (z - a[2]) / (b[2] - a[2])
            pts.append((a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k))
    return pts if len(pts) == 2 else None


def render(shot, world, names, frame, end):
    cx, cy, cz = shot["cj"]
    floor = shot["floor"]
    img = Image.new("RGB", (MAP, MAP), (60, 60, 60))
    d = ImageDraw.Draw(img)
    font = ImageFont.load_default()

    def P(x, y):
        return ((x - cx) * SCALE + MAP / 2, MAP / 2 - (y - cy) * SCALE)

    ents, tris, spheres, render, props, objs = world
    flat = []
    for t in tris:
        nz = normal(t)[2]
        if abs(nz) > 0.7:
            z = sum(v[2] for v in t) / 3 - floor
            if -0.25 <= z <= 0.25:
                col = (215, 215, 215)
            elif z < -0.25:
                col = (120, 120, 120)
            elif z <= 0.7:
                col = (205, 160, 100)
            elif z <= 1.2:
                col = (215, 120, 40)
            elif z <= 2.2:
                col = (130, 80, 40)
            else:
                continue
            flat.append((z, col, t))
    for z, col, t in sorted(flat, key=lambda f: f[0]):
        d.polygon([P(v[0], v[1]) for v in t], fill=col)
    # what is drawn (render geometry): walls cut at 1 m in blue, even where there is no collision
    for t in render:
        if abs(normal(t)[2]) > 0.7:
            continue
        s = cut(t, floor + 1.0)
        if s:
            d.line([P(*s[0]), P(*s[1])], fill=(70, 110, 200), width=2)
    for t in tris:
        if abs(normal(t)[2]) > 0.7:
            continue
        s = cut(t, floor + 0.45)
        if s:
            d.line([P(*s[0]), P(*s[1])], fill=(140, 70, 20), width=2)
        s = cut(t, floor + 1.0)
        if s:
            d.line([P(*s[0]), P(*s[1])], fill=(0, 0, 0), width=3)
    # the cutscene's props (furniture without collision): tops by height, outlines in purple
    for t in sorted(props, key=lambda t: max(v[2] for v in t)):
        nz = normal(t)[2]
        z = sum(v[2] for v in t) / 3 - floor
        if abs(nz) > 0.7 and -0.1 < z < 2.2:
            shade = (230, 190, 120) if z <= 0.7 else (220, 130, 60) if z <= 1.2 else (150, 90, 50)
            d.polygon([P(v[0], v[1]) for v in t], fill=shade)
    for t in props:
        if abs(normal(t)[2]) > 0.7:
            continue
        for h, w in ((0.45, 2), (1.0, 2)):
            s = cut(t, floor + h)
            if s:
                d.line([P(*s[0]), P(*s[1])], fill=(190, 60, 220), width=w)
    # the height map the partner's spots were checked against: obstacles red (darker is higher), seats green
    if "grid" in shot and shot["gt"]:
        gx0, gy0, gfloor, cells, cell = shot["grid"]
        for row, (tops, seats) in enumerate(zip(shot["gt"], shot["gs"])):
            for col in range(cells):
                t, s = tops[col], seats[col]
                if not t and not s:
                    continue
                x, y = gx0 + col * cell, gy0 + row * cell
                a, b = P(x, y + cell), P(x + cell, y)
                if a[0] > MAP or b[0] < 0 or a[1] > MAP or b[1] < 0:
                    continue
                if s and abs(t * 0.02 - s * 0.01) < 0.03:
                    d.rectangle([a, b], fill=(40, 200, 90))
                else:
                    k = min(1.0, t * 0.02 / 2.0)
                    d.rectangle([a, b], fill=(int(250 - 120 * k), int(150 - 130 * k), int(150 - 130 * k)))
    for x, y, z, r in spheres:
        if z - r < floor + 1.0 < z + r:
            rr = math.sqrt(max(r * r - (floor + 1.0 - z) ** 2, 0)) * SCALE
            px, py = P(x, y)
            d.ellipse([px - rr, py - rr, px + rr, py + rr], outline=(0, 0, 0), width=2)
    for mid, person, x, y, z in objs:
        if not person:
            px, py = P(x, y)
            d.text((px + 2, py + 4), f"prop {names.get(mid, mid)}", fill=(230, 150, 255), font=font)
    # names of the objects and of the buildings close to CJ
    for mid, kind, x, y, z in ents:
        if kind == 4 or math.hypot(x - cx, y - cy) < 4.0:
            px, py = P(x, y)
            if 0 <= px < MAP and 0 <= py < MAP:
                d.text((px + 2, py - 5), names.get(mid, str(mid)), fill=(255, 255, 160) if kind == 4 else (200, 230, 255),
                       font=font)

    # camera and its field of view
    if "cam" in shot:
        x, y, z, fx, fy, fz, fov = shot["cam"]
        h = math.atan2(fy, fx)
        half = math.radians(fov) * 0.5  # the game's field of view is horizontal
        a = P(x, y)
        for s in (-1, 1):
            d.line([a, P(x + math.cos(h + s * half) * 12, y + math.sin(h + s * half) * 12)], fill=(255, 220, 0), width=1)
        d.line([a, P(x + math.cos(h) * 2, y + math.sin(h) * 2)], fill=(255, 220, 0), width=3)
        d.ellipse([a[0] - 6, a[1] - 6, a[0] + 6, a[1] + 6], fill=(255, 220, 0))

    # the rest of the shot: the camera's path, its view at the end, the people's tracks
    for dt, cpos, cfwd, cj, actors in shot["ahead"]:
        px, py = P(cpos[0], cpos[1])
        d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(255, 220, 0))
        px, py = P(*cj)
        d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(255, 255, 255))
        for a in actors:
            px, py = P(*a)
            d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(80, 140, 255))
    if shot["ahead"] and "cam" in shot:
        dt, cpos, cfwd, cj, actors = shot["ahead"][-1]
        h, half = math.atan2(cfwd[1], cfwd[0]), math.radians(shot["cam"][6]) * 0.5
        a = P(cpos[0], cpos[1])
        for s in (-1, 1):
            d.line([a, P(cpos[0] + math.cos(h + s * half) * 12, cpos[1] + math.sin(h + s * half) * 12)], fill=(150, 130, 0))

    for t in shot["tries"]:
        x, y, z, heading, pose, score, rej = t
        px, py = P(x, y)
        col = REJECT_COLOURS.get(rej, (0, 255, 0))
        if rej < 0:
            g = max(60, min(255, int(120 + score * 40)))
            col = (0, g, 0)
        r = 3
        if pose == 1:
            d.rectangle([px - r, py - r, px + r, py + r], fill=col)
        elif pose in (2, 3):
            d.polygon([(px, py - r - 1), (px - r - 1, py + r), (px + r + 1, py + r)], fill=col)
        else:
            d.ellipse([px - r, py - r, px + r, py + r], fill=col)

    for x, y, z, on in shot["actors"]:
        px, py = P(x, y)
        d.ellipse([px - 7, py - 7, px + 7, py + 7], outline=(80, 140, 255), width=2, fill=(80, 140, 255) if on else None)
    px, py = P(cx, cy)
    d.ellipse([px - 8, py - 8, px + 8, py + 8], fill=(255, 255, 255))
    d.text((px + 10, py - 6), "CJ", fill=(255, 255, 255), font=font)

    if "spot" in shot:
        x, y, z, heading, pose, act = shot["spot"]
        px, py = P(x, y)
        col = (255, 60, 60) if pose == 4 else (60, 255, 60)
        pts = []
        for i in range(10):
            rr = 11 if i % 2 == 0 else 5
            a = math.pi / 2 + i * math.pi / 5
            pts.append((px + math.cos(a) * rr, py - math.sin(a) * rr))
        d.polygon(pts, fill=col, outline=(0, 0, 0))
        if pose != 4:  # heading 0 faces north (+y); the game turns it counter-clockwise
            fx, fy = -math.sin(heading), math.cos(heading)
            d.line([(px, py), P(x + fx * 1.2, y + fy * 1.2)], fill=col, width=3)

    # scale bar: 2 m
    d.line([(10, MAP - 12), (10 + 2 * SCALE, MAP - 12)], fill=(255, 255, 255), width=3)
    d.text((14, MAP - 26), "2 m", fill=(255, 255, 255), font=font)

    out = Image.new("RGB", (640 + MAP, MAP), (25, 25, 25))
    if frame is not None:
        out.paste(frame.resize((640, 360)), (0, 0))
    if end is not None:
        out.paste(end.resize((640, 360)), (0, 360))
    out.paste(img, (640, 0))
    d2 = ImageDraw.Draw(out)
    spot = shot.get("spot")
    lines = [f"{shot['name']} shot {shot['n']}", f"floor {floor:.2f}, CJ {cx:.1f} {cy:.1f} {cz:.1f}"]
    if spot:
        lines.append(f"chosen: {POSES[spot[4]]} act {spot[5]} at {spot[0]:.1f} {spot[1]:.1f} {spot[2]:.1f}")
    counts = {}
    for t in shot["tries"]:
        counts[t[6]] = counts.get(t[6], 0) + 1
    lines.append("tried: " + ", ".join(f"{REJECT_NAMES[k] if k >= 0 else 'fits'} {v}" for k, v in sorted(counts.items())))
    for i, s in enumerate(lines):
        d2.rectangle([646, 4 + i * 16, 646 + 7 * len(s), 18 + i * 16], fill=(0, 0, 0))
        d2.text((648, 6 + i * 16), s, fill=(230, 230, 230), font=font)
    return out


def main():
    names = model_names()
    worlds = sorted(SRC.glob("*_world.txt"))
    done = 0
    for st in sorted(SRC.glob("*.txt")):
        if st.name.endswith("_world.txt"):
            continue
        m = re.match(r"(.+)_(\d+)$", st.stem)
        if not m:
            continue
        name, n = m.group(1), int(m.group(2))
        candidates = []
        for w in worlds:
            wm = re.match(r"(.+)_(\d+)_world$", w.stem)
            if wm and wm.group(1) == name and int(wm.group(2)) <= n:
                candidates.append((int(wm.group(2)), w))
        if not candidates:
            continue
        world = read_world(max(candidates)[1])
        shot = read_shot(st)
        bmp = st.with_suffix(".bmp")
        frame = Image.open(bmp).convert("RGB") if bmp.exists() else None
        endbmp = st.with_name(st.stem + "_end.bmp")
        end = Image.open(endbmp).convert("RGB") if endbmp.exists() else None
        render(shot, world, names, frame, end).save(SRC / f"{st.stem}_map.jpg", quality=85)
        done += 1
    print(done, "maps")


if __name__ == "__main__":
    main()
