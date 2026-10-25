"""
Minimal MSF 7.00 / PDB reader that dumps public symbols (S_PUB32) and
global data/procedure symbols with their virtual addresses.

Usage:
    python -I pdb_symbols.py <gta_sa.pdb> <gta_sa.exe> <out.txt>

The output is a sorted "address  name" list which is used as a reverse
engineering reference (function and variable addresses of gta_sa.exe 1.0 US).
"""
import struct
import sys

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"


class Msf:
    def __init__(self, data: bytes):
        if not data.startswith(MSF_MAGIC):
            raise ValueError("not an MSF 7.00 file")
        (self.block_size, _fpm, self.num_blocks, dir_bytes, _unk,
         block_map_addr) = struct.unpack_from("<6I", data, len(MSF_MAGIC))
        self.data = data
        bs = self.block_size
        n_dir_blocks = (dir_bytes + bs - 1) // bs
        dir_block_idx = struct.unpack_from(f"<{n_dir_blocks}I", data, block_map_addr * bs)
        directory = b"".join(data[i * bs:(i + 1) * bs] for i in dir_block_idx)[:dir_bytes]
        num_streams = struct.unpack_from("<I", directory, 0)[0]
        sizes = struct.unpack_from(f"<{num_streams}I", directory, 4)
        pos = 4 + 4 * num_streams
        self.streams = []
        for size in sizes:
            if size == 0xFFFFFFFF:
                size = 0
            nb = (size + bs - 1) // bs
            blocks = struct.unpack_from(f"<{nb}I", directory, pos)
            pos += 4 * nb
            self.streams.append((size, blocks))

    def stream(self, idx: int) -> bytes:
        size, blocks = self.streams[idx]
        bs = self.block_size
        return b"".join(self.data[b * bs:(b + 1) * bs] for b in blocks)[:size]


def pe_sections(exe: bytes):
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    nsec = struct.unpack_from("<H", exe, pe + 6)[0]
    opt_size = struct.unpack_from("<H", exe, pe + 20)[0]
    image_base = struct.unpack_from("<I", exe, pe + 24 + 28)[0]
    sec_off = pe + 24 + opt_size
    secs = []
    for i in range(nsec):
        name = exe[sec_off + i * 40: sec_off + i * 40 + 8].rstrip(b"\0").decode()
        vsize, va = struct.unpack_from("<II", exe, sec_off + i * 40 + 8)
        secs.append((name, va, vsize))
    return image_base, secs


def main():
    pdb_path, exe_path, out_path = sys.argv[1:4]
    msf = Msf(open(pdb_path, "rb").read())
    image_base, secs = pe_sections(open(exe_path, "rb").read())

    dbi = msf.stream(3)
    # DBI header (new format)
    (sig, ver, age, gsi_stream, build, psgsi_stream, pdb_dll, sym_rec_stream,
     rbld, modi_size, secc_size, secmap_size, filinf_size, tsmap_size,
     mfc_idx, dbghdr_size, ecinfo_size, flags, machine, pad) = struct.unpack_from(
        "<iIIHHHHHHiiiiiIiiHHI", dbi, 0)
    print(f"DBI ver={ver} machine={machine:#x} symrec={sym_rec_stream} pub={psgsi_stream}")

    # section headers from the optional debug header if present, otherwise PE
    dbg_off = 64 + modi_size + secc_size + secmap_size + filinf_size + tsmap_size + ecinfo_size
    dbg = struct.unpack_from(f"<{dbghdr_size // 2}h", dbi, dbg_off) if dbghdr_size else ()
    seg_va = {}
    if len(dbg) > 5 and dbg[5] not in (-1, 0xFFFF):
        sh = msf.stream(dbg[5])
        for i in range(len(sh) // 40):
            va = struct.unpack_from("<I", sh, i * 40 + 12)[0]
            seg_va[i + 1] = va
    else:
        for i, (_n, va, _s) in enumerate(secs):
            seg_va[i + 1] = va

    rec = msf.stream(sym_rec_stream)
    pos = 0
    syms = {}
    while pos + 4 <= len(rec):
        reclen, kind = struct.unpack_from("<HH", rec, pos)
        body = pos + 4
        if kind == 0x110E:  # S_PUB32
            flags, off, seg = struct.unpack_from("<IIH", rec, body)
            name = rec[body + 10: rec.index(b"\0", body + 10)].decode("latin-1")
            if seg in seg_va:
                syms[image_base + seg_va[seg] + off] = name
        elif kind in (0x110C, 0x110D):  # S_LDATA32 / S_GDATA32
            _typ, off, seg = struct.unpack_from("<IIH", rec, body)
            name = rec[body + 10: rec.index(b"\0", body + 10)].decode("latin-1")
            if seg in seg_va:
                syms.setdefault(image_base + seg_va[seg] + off, name)
        pos += 2 + reclen

    with open(out_path, "w", encoding="utf-8") as f:
        for addr in sorted(syms):
            f.write(f"{addr:08X} {syms[addr]}\n")
    print(f"{len(syms)} symbols written to {out_path}")


if __name__ == "__main__":
    main()
