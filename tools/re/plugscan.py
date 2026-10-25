"""Find game plugins that may patch or read the same gta_sa.exe bytes as a planned hook.

Before hooking a function entry or a call site, check which .asi/.dll/.exe in the game folder contain that
address (or one a few bytes around it) as a 32-bit little-endian constant: such plugins patch or read those
bytes, and a hook there can break them or be overwritten by them. Real case: GInputSA read an operand out of
the first instruction of CCheat::AddToCheatString (0x438480, operand at 0x438481) and crashed once a MinHook
trampoline had replaced that instruction.

usage:
  python tools/re/plugscan.py 0x438481 0x5D2E07
  python tools/re/plugscan.py 0x438480 --game "E:/GTA San Andreas"

Each address matches the values [addr-4, addr+5]. gta_sa.exe and folders named coop are skipped. .asi files
are listed first. The big installers in BONUS produce random matches; hits in files over 20 MB are marked
"probably data". A hit is only a lead: open the plugin in a disassembler to see what it does there.
"""
import argparse
import mmap
import os
import struct
import sys

BIG = 20 << 20


def plugin_files(game):
    files = []
    for root, dirs, names in os.walk(game):
        dirs[:] = [d for d in dirs if d.lower() != "coop"]
        files += [os.path.join(root, n) for n in names
                  if n.lower().endswith((".asi", ".dll", ".exe")) and n.lower() != "gta_sa.exe"]
    return sorted(files, key=lambda p: (not p.lower().endswith(".asi"), p.lower()))


def scan(path, values):
    """[(file offset, value)] of every 32-bit LE occurrence of the values."""
    if not os.path.getsize(path):
        return []
    hits = []
    with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
        for v in values:
            needle = struct.pack("<I", v)
            k = m.find(needle)
            while k >= 0:
                hits.append((k, v))
                k = m.find(needle, k + 1)
    return sorted(hits)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addresses", nargs="+", type=lambda s: int(s, 16), help="hex addresses, e.g. 0x438481")
    ap.add_argument("--game", default=r"E:\GTA San Andreas", help="game folder (default: %(default)s)")
    a = ap.parse_args()
    if not os.path.isdir(a.game):
        sys.exit(f"error: game folder not found: {a.game}")
    values = {v: (addr, v - addr) for addr in a.addresses for v in range(addr - 4, addr + 6)}
    files = plugin_files(a.game)
    total = 0
    for p in files:
        try:
            hits = scan(p, values)
        except OSError as e:
            print(f"{os.path.relpath(p, a.game)}: cannot read ({e})")
            continue
        big = os.path.getsize(p) > BIG
        for off, v in hits:
            addr, d = values[v]
            print(f"{os.path.relpath(p, a.game)}  {v:#x} (= {addr:#x}{d:+d})  file offset {off:#x}"
                  + ("  probably data (file over 20 MB)" if big else ""))
        total += len(hits)
    print(f"{total} hit(s) in {len(files)} files scanned")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:                                    # output piped into head / Select-Object -First
        os._exit(0)
