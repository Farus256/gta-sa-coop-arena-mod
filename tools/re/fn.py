"""Disassemble one gta_sa.exe (1.0 US) function with symbol annotations and show its gta-reversed source.

usage:
  python tools/re/fn.py 0x5D2D70                    # by address; an address inside a function shows the whole
                                                    # function and marks that instruction with >>
  python tools/re/fn.py CPools::LoadPedPool         # Class::Method, matched as the mangled N6CPools11LoadPedPoolE
  python tools/re/fn.py _ZN10CPlayerPedC2Eib        # raw symbol name; any other text is a substring search
  python tools/re/fn.py CPlayerPed::CPlayerPed --no-src --max 0x400

The function ends at the next symbol, or earlier at a ret/jmp followed only by alignment padding (nop, int3,
lea esp,[esp]...) up to the next 16-byte boundary when no branch seen so far targets a later address.
Switch jump tables (jmp [reg*4+table]) count as branch targets and end the code at the table.
Source lookup: set GTA_REVERSED to another gta-reversed/source directory if the default one moves.
"""
import argparse
import bisect
import os
import re
import struct
import sys
from pathlib import Path

try:
    from capstone import CS_ARCH_X86, CS_GRP_JUMP, CS_GRP_RET, CS_MODE_32, Cs
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM
except ImportError:
    sys.exit("error: capstone is not importable (it is in the user site-packages, which python -I hides)")

ROOT = Path(__file__).resolve().parents[2]
EXE = Path(r"E:\GTA San Andreas\gta_sa.exe")
SYMS = ROOT / "re" / "gta_sa_symbols.txt"
SRC = Path(os.environ.get("GTA_REVERSED", ROOT / "build" / "re" / "gta-reversed" / "source"))


def fail(msg):
    sys.exit(f"error: {msg}")


# ---------------------------------------------------------------- symbols

def load_symbols():
    if not SYMS.exists():
        fail(f"symbol map not found: {SYMS}")
    pairs = []
    for line in open(SYMS, encoding="utf-8", errors="replace"):
        a, _, n = line.strip().partition(" ")
        if n:
            pairs.append((int(a, 16), n))
    pairs.sort()
    return [a for a, _ in pairs], [n for _, n in pairs]


SYM_ADDRS, SYM_NAMES = load_symbols()
DIGITS = re.compile(r"\d+")
MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True


def lookup(va):
    """(start, name) of the symbol at or before va, or None."""
    k = bisect.bisect_right(SYM_ADDRS, va) - 1
    return (SYM_ADDRS[k], SYM_NAMES[k]) if k >= 0 else None


def next_symbol(va):
    k = bisect.bisect_right(SYM_ADDRS, va)
    return (SYM_ADDRS[k], SYM_NAMES[k]) if k < len(SYM_ADDRS) else None


def pretty(name):
    """Readable nested Itanium name, no full demangling: _ZN6CPools11LoadPedPoolEv -> CPools::LoadPedPool.
    Anything it does not understand (operators, templates) is returned unchanged."""
    kind = {"_ZTV": "vtable ", "_ZTI": "typeinfo ", "_ZTS": "typeinfo name "}.get(name[:4])
    if kind:
        return kind + pretty("_Z" + name[4:])
    m = re.match(r"_Z(N[KVr]*)?", name)
    if not m:
        return name
    nested, i, parts = bool(m.group(1)), m.end(), []
    while i < len(name):
        d = DIGITS.match(name, i)
        if d:
            n = int(d.group())
            parts.append(name[d.end():d.end() + n])
            i = d.end() + n
        elif name[i:i + 2] in ("C1", "C2", "C3") and parts:
            parts.append(parts[-1])
            i += 2
        elif name[i:i + 2] in ("D0", "D1", "D2") and parts:
            parts.append("~" + parts[-1])
            i += 2
        else:
            break
        if not nested:
            break
    if not parts or nested and name[i:i + 1] != "E":
        return name
    return "::".join(parts)


def label(name):
    p = pretty(name)
    return p if p == name else f"{p}  [{name}]"


def find_symbols(q):
    """Symbols matching a query, best tier first: exact / Class::Method as mangled N...E, then a mangled
    name component, then substring. Returns [(addr, name)] of the first non-empty tier."""
    parts, enc = q.split("::"), ""
    for k, p in enumerate(parts):
        if k and p == parts[k - 1]:
            enc += "C[123]"                      # constructor
        elif k and p == "~" + parts[k - 1]:
            enc += "D[012]"                      # destructor
        else:
            enc += f"{len(p)}{re.escape(p)}"
    nested = len(parts) > 1
    anchored = re.compile(rf"_ZN[KVr]*{enc}E" if nested else rf"_Z{enc}")
    component = re.compile(rf"(?<![0-9]){enc}" + ("E" if nested else ""))
    alt = q.replace("::", "__")                  # some plain names in the map look like CPad__AddToPCCheatString
    tiers = (lambda n: n == q or n == alt or anchored.match(n),
             lambda n: component.search(n),
             lambda n: q in n or alt in n)
    for test in tiers:
        hits = [(a, n) for a, n in zip(SYM_ADDRS, SYM_NAMES) if test(n)]
        if hits:
            return hits
    return []


def parse_target(q):
    """Address from '0x5D2D70', '5D2D70' or a symbol query; exits with a candidate list when ambiguous."""
    if re.fullmatch(r"0x[0-9a-fA-F]+", q) or re.fullmatch(r"[0-9a-fA-F]{6,8}", q) and re.search(r"\d", q):
        return int(q, 16)
    hits = find_symbols(q)
    if not hits:
        fail(f"no symbol matches {q!r}")
    if len({a for a, _ in hits}) > 1:
        print(f"{q!r} is ambiguous, {len(hits)} matches:")
        for a, n in hits[:40]:
            print(f"  {a:#08x}  {label(n)}")
        if len(hits) > 40:
            print(f"  ... and {len(hits) - 40} more")
        sys.exit(1)
    return hits[0][0]


# ---------------------------------------------------------------- PE image

def load_pe(path=EXE):
    """(file bytes, [(name, va, size, raw_offset, raw_size, characteristics)], image_end); va includes the image base."""
    if not path.exists():
        fail(f"executable not found: {path}")
    img = path.read_bytes()
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    nsec, opt = struct.unpack_from("<H", img, pe + 6)[0], struct.unpack_from("<H", img, pe + 20)[0]
    base = struct.unpack_from("<I", img, pe + 52)[0]
    secs = []
    for k in range(nsec):
        o = pe + 24 + opt + 40 * k
        vsize, va, rsize, raw = struct.unpack_from("<4I", img, o + 8)
        secs.append((img[o:o + 8].rstrip(b"\0").decode("latin-1"), base + va, max(vsize, rsize), raw, rsize,
                     struct.unpack_from("<I", img, o + 36)[0]))
    return img, secs, max(s[1] + s[2] for s in secs)


def section_of(secs, va):
    return next((s for s in secs if s[1] <= va < s[1] + s[2]), None)


def read_va(img, secs, va, n):
    """Up to n file-backed bytes at va (shorter at the end of a section's raw data)."""
    s = section_of(secs, va)
    if not s or va - s[1] >= s[4]:
        return b""
    off = s[3] + va - s[1]
    return img[off:off + min(n, s[4] - (va - s[1]))]


# ---------------------------------------------------------------- disassembly

FILLER = re.compile(r"(\w+), (?:(?:\w+ ptr )?\[\1\]|\1)")   # lea esp, [esp] / lea ecx, [ecx] / mov edi, edi


def is_filler(i):
    return i.mnemonic in ("nop", "int3") or i.mnemonic in ("lea", "mov", "xchg") and FILLER.fullmatch(i.op_str)


def padding_only(img, secs, va):
    """True when the bytes from va up to the next 16-byte boundary are alignment fillers (or there are none)."""
    end, pos = (va + 15) & ~15, va
    for i in MD.disasm(read_va(img, secs, va, end - va), va):
        if not is_filler(i):
            return False
        pos = i.address + i.size
    return pos == end


def hoodlum_stub(img, secs, target, back):
    """Instructions the HOODLUM crack moved out of line (code at target that jumps straight back to `back`), else None.
    The exe has ~700 of these: an original instruction replaced by `jmp stub` (+nop), the stub at 0x401000+."""
    out = []
    for i in MD.disasm(read_va(img, secs, target, 96), target):
        if i.mnemonic == "jmp":
            return out if i.operands and i.operands[0].type == X86_OP_IMM and i.operands[0].imm == back else None
        if i.group(CS_GRP_JUMP) or i.group(CS_GRP_RET) or len(out) == 12:
            return None
        out.append(i)
    return None


def decode(img, secs, start, limit, limit_why="limit reached"):
    """Instructions of the function at start, the reason it ended, {jmp address: switch case targets}
    and {jmp address: HOODLUM stub instructions}."""
    insns, hi, tables, stubs, why = [], start, {}, {}, None
    for i in MD.disasm(read_va(img, secs, start, limit - start), start):
        if i.address >= limit:
            break
        insns.append(i)
        nxt = i.address + i.size
        if i.group(CS_GRP_JUMP) and i.operands:
            op = i.operands[0]
            if op.type == X86_OP_IMM and start < op.imm < limit:
                hi = max(hi, op.imm)
            elif op.type == X86_OP_IMM and i.mnemonic == "jmp":
                stub = hoodlum_stub(img, secs, op.imm, nxt)
                if stub is not None:
                    stubs[i.address] = stub
                    continue
            elif op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index and op.mem.scale == 4:
                table, cases = op.mem.disp & 0xFFFFFFFF, []     # switch: jmp dword ptr [reg*4 + table]
                bound = table if table > start else limit       # MSVC puts the table right after the code
                for k in range(512):
                    e = struct.unpack("<I", read_va(img, secs, table + 4 * k, 4).ljust(4, b"\0"))[0]
                    if not start <= e < bound:
                        break
                    cases.append(e)
                tables[i.address] = cases
                hi = max([hi] + cases)
                if i.address < table < limit:
                    limit, why = table, f"jump table at {table:#x}"
        if (i.group(CS_GRP_RET) or i.mnemonic == "jmp") and hi < nxt and padding_only(img, secs, nxt):
            return insns, "ret/jmp + alignment padding", tables, stubs
    if not insns:
        return insns, "no decodable code", tables, stubs
    end = insns[-1].address + insns[-1].size
    return insns, why or (limit_why if end + 15 >= limit else f"undecodable bytes at {end:#x}"), tables, stubs


def note(v, lo, hi, fstart, fend):
    """Symbol name (+offset) for an image address outside the current function."""
    if not lo <= v < hi or fstart <= v < fend:
        return None
    s = lookup(v)
    if not s or v - s[0] > 0x10000:
        return None
    return pretty(s[1]) + (f"+{v - s[0]:#x}" if v != s[0] else "")


def show_function(img, secs, image_end, va, maxlen):
    sec = section_of(secs, va)
    if not sec:
        fail(f"{va:#x} is outside gta_sa.exe")
    sym = lookup(va)
    if not sym or not sec[5] & 0x20000020:                    # IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE
        off = f" +{va - sym[0]:#x}" if sym and va != sym[0] else ""
        print(f"{va:#x} is in {sec[0]} (not code)" + (f": {label(sym[1])}{off}" if sym else ""))
        return None
    start, name = sym
    if start < sec[1]:
        start = va                                            # nearest symbol belongs to another section
    nxt = next_symbol(start)
    limit, limit_why = min((nxt[0] if nxt else sec[1] + sec[2], "next symbol"), (start + maxlen, "--max cap"),
                           (sec[1] + sec[4], "end of section"))
    print(f"{start:#x}  {label(name) if start == sym[0] else '(no symbol)'}")
    if va != start:
        print(f"query {va:#x} is at +{va - start:#x} (marked >>)")
    insns, why, tables, stubs = decode(img, secs, start, limit, limit_why)
    end = insns[-1].address + insns[-1].size if insns else start
    nxt_txt = f"; next symbol {nxt[0]:#x} {pretty(nxt[1])}" if nxt else ""
    print(f"{start:#x}..{end:#x} ({end - start:#x} bytes, {len(insns)} instructions), ended: {why}{nxt_txt}\n")
    lo = min(s[1] for s in secs) & ~0xFFFF

    def notes_of(i):
        notes = []
        for op in i.operands:
            v = op.imm & 0xFFFFFFFF if op.type == X86_OP_IMM else op.mem.disp & 0xFFFFFFFF if op.type == X86_OP_MEM else None
            n = v is not None and note(v, lo, image_end, start, end)
            if n and n not in notes:
                notes.append(n)
        if i.address in tables:
            notes.append(f"switch, {len(tables[i.address])} cases")
        if i.address in stubs:                                # the stub's own symbol (sub_40xxxx+N) is meaningless
            notes = [f"HOODLUM stub, returns to {i.address + i.size:#x}:"]
        return notes

    def line(i, mark):
        notes = notes_of(i)
        text = f"{mark}{i.address:8x}  {i.bytes.hex():<16} {i.mnemonic} {i.op_str}".rstrip()
        print(f"{text:<72} ; {', '.join(notes)}" if notes else text)

    for i in insns:
        line(i, ">>" if i.address <= va < i.address + i.size and va != start else "  ")
        for s in stubs.get(i.address, ()):
            line(s, " |")
    return start


# ---------------------------------------------------------------- gta-reversed source

CLEAN = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)
INSTALL = re.compile(r"RH_Scoped(\w*)Install\(\s*([^,]+?)\s*,")
OWNER = re.compile(r"RH_Scoped(?:Virtual|Named)?Class\(\s*([\w:]+)|([\w:]+)::InjectHooks\s*\(")
CONTROL = re.compile(r"\s*(if|for|while|switch|else|do|return|case)\b")
MORE_INIT = re.compile(r"\s*[,{]")
COL0 = re.compile(r"^(?=[^\s{}#])", re.M)                   # top-level lines (comments are blanked)
NL = "\n"


def blank(text):
    """text with comments and string literals replaced by spaces (offsets and line breaks kept)."""
    return CLEAN.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)


def body_end(code, pos):
    """End offset (after the matching '}') of a function definition whose header starts at pos, else None.
    code is the file text with comments and string literals blanked out."""
    b, s = code.find("{", pos), code.find(";", pos)
    if b < 0 or 0 <= s < b or "(" not in code[pos:b] or CONTROL.match(code, pos):
        return None
    while True:
        depth = 0
        for k in range(b, len(code)):
            depth += (code[k] == "{") - (code[k] == "}")
            if not depth:
                break
        else:
            return None
        if not MORE_INIT.match(code, k + 1):              # ctor init list braces: m_a{1}, m_b{2} {
            return k + 1
        b = code.find("{", k + 1)


def find_source(addr):
    """(location, snippet, remark) of the gta-reversed definition of the function at addr, or (None, None, remark)."""
    if not SRC.is_dir():
        return None, None, f"gta-reversed tree not found at {SRC} (set GTA_REVERSED)"
    pat = re.compile(rf"0x0*{addr:x}(?![0-9a-f])", re.I)
    files = []
    for p in sorted(SRC.rglob("*"), key=lambda p: (p.suffix != ".cpp", str(p).lower())):
        if p.suffix in (".cpp", ".h", ".hpp"):
            text = p.read_text(encoding="utf-8", errors="replace")
            hits = [m.start() for m in pat.finditer(text)]
            if hits:
                files.append((p, text, blank(text), hits))

    def where(p, text, pos):
        return f"{p.relative_to(SRC)}:{text.count(NL, 0, pos) + 1}"

    installs, mentions = [], []
    for p, text, code, hits in files:                        # address comment before (or after) a definition
        for pos in hits:
            ls, le = text.rfind(NL, 0, pos) + 1, text.find(NL, pos)
            le = len(text) if le < 0 else le
            m = INSTALL.search(text, ls, le)
            mentions.append(f"{where(p, text, ls)}: {text[ls:le].strip()}")
            if m:
                installs.append((p, text, code, ls, m))
                continue
            end = body_end(code, le if not code[ls:le].strip() else ls)
            if not end and "plugin::Call" in code[ls:le]:     # address inside a one-statement plugin::Call wrapper
                start = ([0] + [d.start() for d in COL0.finditer(code, 0, ls)])[-1]
                end = body_end(code, start)
                if end and end > pos and code.count(";", start, end) == 1:
                    ls = start
                else:
                    end = None
            if end:
                return where(p, text, ls), text[ls:end], None
    for p, text, code, ls, m in installs:  # only RH_ScopedInstall: Class::Name( in that file, else Name( in the class header
        name = m.group(2)
        flag = " (hook has .Reversed = false)" if ".Reversed = false" in text[ls:text.find(NL, ls)] else ""
        owners = list(dict.fromkeys(o.group(1) or o.group(2) for o in OWNER.finditer(code, 0, ls)))[::-1]
        owners = [None] if "Global" in m.group(1) or not owners else owners  # nested RH_ScopedClass blocks: try each
        h = p.with_suffix(".h")
        htext = h.read_text(encoding="utf-8", errors="replace") if h.exists() else ""
        hcode, places = blank(htext), []
        for cls in owners:
            places.append((p, text, code, 0, r"^(?=\S)", re.escape(f"{cls}::{name}" if cls else name)))
            c = cls and re.search(rf"\b(?:class|struct)\b[^;{{]*?\b{re.escape(cls.split('::')[-1])}\b[^;{{]*{{", hcode)
            if c:                                             # inline method inside the class body
                places.append((h, htext, hcode, c.start(), r"^", re.escape(name)))
        for fp, ftext, fcode, start, indent, nm in places:
            for d in re.compile(rf"{indent}[^\n;{{}}]*?(?<![\w:.>~]){nm}\s*\(", re.M).finditer(fcode, start):
                end = body_end(fcode, d.start())
                if end:
                    return where(fp, ftext, d.start()), ftext[d.start():end], f"found via RH_ScopedInstall({name}){flag}"
    if mentions:
        return None, None, "no definition found; the address is mentioned at:\n  " + "\n  ".join(mentions)
    return None, None, f"no RH_ScopedInstall or address comment for {addr:#x}"


def show_source(addr):
    loc, snippet, remark = find_source(addr)
    if not snippet:
        print(f"\n--- gta-reversed: no source for {addr:#x}: {remark}")
        return
    calls = re.search(r"plugin::Call\w*\s*<", snippet)
    if calls and CLEAN.sub("", snippet).count(";") <= 1:
        kind = "only a plugin::Call stub into the original code, not reversed"
    else:
        kind = "reversed" + (", still calls original code via plugin::Call" if calls else "")
    print(f"\n--- gta-reversed {loc} ({kind}){'; ' + remark if remark else ''}")
    print(snippet.rstrip())


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", help="address (0x5D2D70) or symbol (CPools::LoadPedPool, mangled name, substring)")
    ap.add_argument("--max", type=lambda s: int(s, 0), default=0x2000, help="max function size in bytes")
    ap.add_argument("--no-src", action="store_true", help="skip the gta-reversed source lookup")
    a = ap.parse_args()
    va = parse_target(a.target)
    img, secs, image_end = load_pe()
    start = show_function(img, secs, image_end, va, a.max)
    if start is not None and not a.no_src:
        show_source(start)


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:                                    # output piped into head / Select-Object -First
        os._exit(0)
