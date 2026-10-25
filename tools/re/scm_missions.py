"""Lists the missions of a GTA SA main.scm: index, offset, size and script name (first SCRIPT_NAME opcode).

usage: python tools/re/scm_missions.py "E:/GTA San Andreas/data/script/main.scm"
"""
import struct
import sys


def segment(data, pos):
    """Every header segment starts with GOTO <next segment> (opcode 0002, type 01 int32) + a byte."""
    op, typ, nxt = struct.unpack_from("<HBi", data, pos)
    assert op == 0x0002 and typ == 0x01, f"no GOTO at {pos:#x}"
    return pos + 8, nxt


def main(path):
    data = open(path, "rb").read()
    pos, nxt = segment(data, 0)          # global variables
    pos, nxt = segment(data, nxt)        # model names
    pos, nxt = segment(data, nxt)        # missions
    main_size, largest, count, exclusive, highest_local = struct.unpack_from("<IIHHI", data, pos)
    pos += 16
    offsets = struct.unpack_from(f"<{count}I", data, pos)
    ends = sorted(offsets) + [len(data)]
    for i, off in enumerate(offsets):
        end = ends[ends.index(off) + 1]
        body = data[off:end]
        name = "?"
        k = body.find(b"\xa4\x03\x09")  # 03A4 SCRIPT_NAME, string8 argument
        if 0 <= k < 4000:
            name = body[k + 3:k + 11].split(b"\0")[0].decode("latin-1")
        print(f"{i:3d}  {off:#08x}  {end - off:7d}  {name}")
    print(f"main size {main_size:#x}, largest mission {largest:#x}, {count} missions")


if __name__ == "__main__":
    main(sys.argv[1])
