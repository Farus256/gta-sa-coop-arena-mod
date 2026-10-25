#!/usr/bin/env python3
"""
Checks the Feature-Sliced Design rules of src/ (python tools/fsd_check.py, exit code 1 on a violation).

Each binary (src/client, src/server, src/loader) is an FSD application with the layers
app > pages > widgets > features > entities > shared. A file may include:
  * a lower layer;
  * its own slice (pages, widgets, features, entities are divided into slices: the first folder under the layer);
  * any segment of its own layer when the layer has no slices (app, shared);
  * another entity: the C++ counterpart of FSD's @x cross-imports between entities;
  * src/common, the code of all three binaries. src/common includes only itself.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"
LAYERS = ["shared", "entities", "features", "widgets", "pages", "app"]  # bottom to top
SLICED = {"entities", "features", "widgets", "pages"}
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)


def where(rel):
    """(layer, slice) of a path inside an application, e.g. features/story/missions/Missions.h."""
    parts = rel.split("/")
    layer = parts[0]
    return layer, parts[1] if layer in SLICED and len(parts) > 2 else None


def check():
    errors = []
    for f in sorted(SRC.rglob("*")):
        if f.suffix not in (".cpp", ".h"):
            continue
        rel = f.relative_to(SRC).as_posix()
        app, inner = rel.split("/", 1)
        for inc in INCLUDE.findall(f.read_text(encoding="utf-8")):
            if inc.startswith("common/"):
                continue
            if app == "common":
                errors.append(f"{rel}: src/common includes {inc} (only common/ is allowed)")
                continue
            if not (SRC / app / inc).exists():
                errors.append(f"{rel}: {inc} is not a file of src/{app}")
                continue
            (layer, slice_), (to_layer, to_slice) = where(inner), where(inc)
            if layer not in LAYERS or to_layer not in LAYERS:
                errors.append(f"{rel}: {inc} is outside the layers {LAYERS}")
                continue
            low, high = LAYERS.index(to_layer), LAYERS.index(layer)
            if low < high:
                continue
            if low > high:
                errors.append(f"{rel}: {layer} includes the upper layer {to_layer} ({inc})")
            elif layer in SLICED and slice_ != to_slice and layer != "entities":
                errors.append(f"{rel}: slice {layer}/{slice_} includes slice {to_layer}/{to_slice} ({inc})")
    return errors


if __name__ == "__main__":
    problems = check()
    for p in problems:
        print(p)
    print(f"FSD check: {len(problems)} violation(s)" if problems else "FSD check: ok")
    sys.exit(1 if problems else 0)
