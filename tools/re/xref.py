"""Find every instruction in gta_sa.exe that references an address (call/jmp target, immediate, memory operand),
grouped by the containing function (nearest preceding symbol).

usage:
  python tools/re/xref.py 0x5D0880 0x60D5B0          # several addresses (symbol names work too, as in fn.py)
  python tools/re/xref.py 0xB7CD98 --range 0x190     # anything inside [addr, addr+0x190): fields of a struct global

Searches the full-text disassembly cached in build/re/gta_sa.asm (llvm-objdump linear sweep, regenerated when
missing or older than the exe). Linear sweep also decodes data inside code sections, so an odd hit can be junk,
and after such data it can lose sync for a few instructions (about 1% of function starts are affected), so a
reference there can be missed: check suspicious gaps with fn.py.
Instructions the HOODLUM crack moved into stubs at 0x401000+ are credited to the place they were moved from.
For an exact address, pointers stored as data (vtables, function tables in .rdata/.data) are listed as well.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fn  # noqa: E402  symbols, PE parsing, HOODLUM stub detection

ASM = fn.ROOT / "build" / "re" / "gta_sa.asm"
OBJDUMP = fn.ROOT / "toolchain" / "llvm-mingw" / "bin" / "llvm-objdump.exe"
JMP = re.compile(rb" *([0-9a-f]+):\s+jmp\t0x([0-9a-f]+)")
BRANCH = re.compile(rb"\t(j[a-z]+|ret)\b")


def load_asm():
    if not ASM.exists() or ASM.stat().st_mtime < fn.EXE.stat().st_mtime:
        if not OBJDUMP.exists():
            fn.fail(f"llvm-objdump not found: {OBJDUMP}")
        print(f"disassembling {fn.EXE} into {ASM} (a few seconds)...", file=sys.stderr)
        ASM.parent.mkdir(parents=True, exist_ok=True)
        tmp = ASM.with_suffix(".tmp")
        with open(tmp, "wb") as f:
            r = subprocess.run([str(OBJDUMP), "-d", "--no-show-raw-insn", "--x86-asm-syntax=intel", str(fn.EXE)],
                               stdout=f)
        if r.returncode:
            fn.fail(f"llvm-objdump failed (exit code {r.returncode}); the cache was not updated")
        os.replace(tmp, ASM)
    return ASM.read_bytes()


def value_regex(lo, hi):
    """Hex numbers in lo..hi the way llvm-objdump prints them (0x, lowercase, no leading zeros); filter after."""
    a, b = f"{lo:x}", f"{hi:x}"
    if len(a) != len(b):
        return re.compile(rb"0x[0-9a-f]+(?![0-9a-f])")
    p = os.path.commonprefix([a, b])
    return re.compile(f"0x{p}[0-9a-f]{{{len(a) - len(p)}}}(?![0-9a-f])".encode())


def stub_site(img, secs, asm, ls, addr):
    """If the instruction at addr (line at offset ls) sits in a HOODLUM stub: the address of the `jmp stub`
    in the original function, else None."""
    for _ in range(14):
        le = asm.find(b"\n", ls)
        m = JMP.match(asm, ls, le)
        if m:
            back = int(m.group(2), 16)
            j = next(fn.MD.disasm(fn.read_va(img, secs, back - 5, 5), back - 5), None)
            if j and j.mnemonic == "jmp" and j.operands[0].type == fn.X86_OP_IMM:
                target = j.operands[0].imm
                if target <= addr <= int(m.group(1), 16) and fn.hoodlum_stub(img, secs, target, back) is not None:
                    return back - 5
            return None
        if BRANCH.search(asm, ls, le):
            return None
        ls = le + 1
    return None


def refs(asm, img, secs, lo, hi):
    """{instruction address: (instruction text, stub site or None)} for lines with a number in lo..hi."""
    out = {}
    for m in value_regex(lo, hi).finditer(asm):
        s = m.start()
        # skip "<.text+0x..>" labels and negative numbers ("-0x1", "[eax - 0x10]")
        if asm[s - 1:s] in (b"+", b"-") or asm[s - 2:s] == b"- " or not lo <= int(m.group()[2:], 16) <= hi:
            continue
        ls = asm.rfind(b"\n", 0, s) + 1
        head, _, ins = asm[ls:asm.find(b"\n", s)].decode("latin-1").partition(":")
        try:
            addr = int(head, 16)
        except ValueError:
            continue
        ins = re.sub(r"\s+", " ", re.sub(r"\s*<[^>]*>", "", ins)).strip()
        out[addr] = (ins, stub_site(img, secs, asm, ls, addr))
    return out


def data_refs(img, secs, addr):
    """32-bit pointers to addr stored in non-code sections (vtables, function pointer tables)."""
    needle, out = struct.pack("<I", addr), []
    for name, va, _size, raw, rsize, ch in secs:
        if ch & 0x20:                                     # IMAGE_SCN_CNT_CODE: covered by the disassembly
            continue
        k = img.find(needle, raw, raw + rsize)
        while k >= 0:
            out.append((name, va + k - raw))
            k = img.find(needle, k + 1, raw + rsize)
    return out


def symoff(va):
    s = fn.lookup(va)
    return fn.label(s[1]) + (f" +{va - s[0]:#x}" if va != s[0] else "") if s else "(no symbol)"


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("targets", nargs="+", help="addresses (0x5D0880) or symbol names")
    ap.add_argument("--range", type=lambda s: int(s, 0), default=1, metavar="N",
                    help="also match addresses inside [addr, addr+N)")
    a = ap.parse_args()
    if a.range < 1:
        fn.fail("--range must be at least 1")
    targets = [fn.parse_target(t) for t in a.targets]
    img, secs, _ = fn.load_pe()
    asm = load_asm()
    for t in targets:
        hits = refs(asm, img, secs, t, t + a.range - 1)
        span = f"{t:#x}..{t + a.range - 1:#x}" if a.range > 1 else f"{t:#x}"
        groups = defaultdict(list)
        for addr, (ins, site) in sorted(hits.items()):
            groups[fn.lookup(site if site is not None else addr)].append((addr, ins, site))
        print(f"== {span}  {symoff(t)}: {len(hits)} instruction(s) in {len(groups)} function(s)")
        for g, rows in sorted(groups.items(), key=lambda kv: kv[0] or (0, "")):
            print(f"  {g[0]:#x}  {fn.label(g[1])}" if g else "  (no symbol)")
            for addr, ins, site in rows:
                extra = f"   ; HOODLUM stub for {site:#x}" if site is not None else ""
                if a.range > 1:
                    vals = [int(v, 16) for v in re.findall(r"(?<![+\-\w])0x([0-9a-f]+)", ins)]
                    off = [v - t for v in vals if t <= v < t + a.range]
                    extra = (f"   [+{off[0]:#x}]" if off else "") + extra
                print(f"    {addr:8x}  {ins}{extra}")
        if a.range == 1:
            for sec, va in data_refs(img, secs, t):
                print(f"  data pointer in {sec} at {va:#x}  ({symoff(va)})")
        print()


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:                                    # output piped into head / Select-Object -First
        os._exit(0)
