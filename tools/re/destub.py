"""Write an analysis-only copy of gta_sa.exe with the HOODLUM crack's moved instructions put back in place.

The HOODLUM exe has ~700 instructions moved out of line: the original is replaced by `jmp stub` (after nop padding),
and the stub at 0x401000+ holds it and jumps straight back. Decompilers (Ghidra through REA) take that jump for the end
of the function and cut its body there. This copy has each original instruction back where the jump is, padded with
nops, a relative call in a stub re-aimed from its new address, and the protection's encrypted leftovers marked as data
(see hide_leftovers). It is for analysis only and never runs.

usage: python tools/re/destub.py [--out build/re/gta_sa_analysis.exe]
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fn import EXE, MD, ROOT, hoodlum_stub, load_pe, section_of  # noqa: E402


def destub(img, secs):
    """{site va: original bytes} for every moved instruction run, the site being where the stub's code goes back"""
    out = {}
    text = next(s for s in secs if s[0] == ".text")
    raw = img[text[3]:text[3] + text[4]]
    pos = raw.find(b"\xE9")
    while pos != -1 and pos + 5 <= len(raw):
        jmp = text[1] + pos
        target = jmp + 5 + struct.unpack_from("<i", raw, pos + 1)[0]
        stub = hoodlum_stub(img, secs, target, jmp + 5) if section_of(secs, target) else None
        size = sum(i.size for i in stub) if stub else 0
        start = jmp + 5 - size
        # the original run ends where the stub returns and starts after nothing but this jump and its nop padding
        if stub and size >= 5 and raw[pos - (jmp - start):pos] == b"\x90" * (jmp - start):
            code = b""
            for i in stub:
                b = bytes(i.bytes)
                if i.mnemonic == "call" and b[0] == 0xE8:  # rel32 call: same target from its address here
                    dest = i.address + 5 + struct.unpack_from("<i", b, 1)[0]
                    b = b"\xE8" + struct.pack("<i", dest - (start + len(code) + 5))
                code += b
            out[start] = code
        pos = raw.find(b"\xE9", pos + 1)
    return out


def hide_leftovers(data, secs):
    """Names of the sections made plain data: the encrypted leftovers of the retail protection (a second .text, .init
    and a second .data, 9 MB) are marked executable, and Ghidra's analysis decompiled garbage there for minutes.
    Game code never jumps into them; .HOODLUM stays code (the crack moved the first functions of .text there)."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    hidden = []
    for k, s in enumerate(secs):
        if s[1] < 0xCB1000 or s[0] == ".HOODLUM" or not s[5] & 0x20000000:
            continue
        o = pe + 24 + opt + 40 * k + 36
        struct.pack_into("<I", data, o, s[5] & ~0x20000020 | 0x40)  # no MEM_EXECUTE / CNT_CODE, initialized data
        hidden.append(f"{s[0]}@{s[1]:#x}")
    return hidden


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=str(ROOT / "build" / "re" / "gta_sa_analysis.exe"))
    args = ap.parse_args()
    img, secs, _ = load_pe(EXE)
    sites = destub(img, secs)
    data = bytearray(img)
    for va, code in sites.items():
        s = section_of(secs, va)
        off = s[3] + va - s[1]
        data[off:off + len(code)] = code
    hidden = hide_leftovers(data, secs)
    assert len(hidden) == 3, f"expected 3 leftover sections, got {hidden}"
    # self-check: CPlayerPed::ClearWeaponTarget reads the ped type in place again (mov eax, [edi + 0x598])
    s = section_of(secs, 0x609C83)
    assert data[s[3] + 0x609C83 - s[1]:][:6] == bytes.fromhex("8b8798050000"), "ClearWeaponTarget not restored"
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_bytes(data)
    print(f"{len(sites)} moved instruction runs put back, {', '.join(hidden)} made data -> {args.out}")


if __name__ == "__main__":
    main()
