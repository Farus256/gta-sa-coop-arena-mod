# tools/re

Reverse-engineering helpers for `gta_sa.exe` 1.0 US (HOODLUM). Python 3 standard library, plus `capstone`
for `fn.py` and `xref.py` (it sits in the user site-packages, so run these two without `python -I`). The
examples run from the project root. The tools only read the game folder.

Inputs: the game in `E:\GTA San Andreas`, the symbol map `re/gta_sa_symbols.txt` (`ADDR name` lines, kept
locally and not in the repository),
`toolchain/llvm-mingw/bin/llvm-objdump.exe` (for `xref.py`) and a gta-reversed checkout (for `fn.py`: by
default `build/re/gta-reversed/source`, a copy of github.com/gta-reversed/gta-reversed's `source` folder; set
`GTA_REVERSED` to another `source` folder to use a fresher one).

On top of these, REA (npm `rea-agents`, an MCP server with Ghidra decompilation) is set up for Claude Code from
portable copies in `build/re`: Node.js 24 (`node`, REA does not run on Node 25), JDK 21 (`jdk`) and Ghidra 12.1.4
(`ghidra`). Its registration in `~/.claude.json` (`mcpServers.rea`) starts `build/re/node/node.exe` with
`GHIDRA_INSTALL_DIR` and `JAVA_HOME` set; `build/re/node/rea.cmd doctor` checks it.

## fn.py: disassemble one function

```
python tools/re/fn.py CPools::LoadPedPool
python tools/re/fn.py 0x5D2E07 --no-src     # an address inside a function shows the whole function, >> marks it
```

Prints the function from its symbol to its end with bytes, Intel syntax and symbol names for call targets,
globals and immediates, then the matching gta-reversed source (or says there is none, or that it is only a
`plugin::Call` stub). The function ends at the next symbol or at a `ret`/`jmp` followed by alignment padding,
capped by `--max` (default 0x2000 bytes). A name can be `Class::Method` (matched as the mangled
`N6CPools11LoadPedPoolE`), `Class::Class` for a constructor, a raw symbol or a substring. An ambiguous name
lists the candidates.

## xref.py: who references an address

```
python tools/re/xref.py 0x5D0880 0x60D5B0
python tools/re/xref.py 0xB7CD98 --range 0x190   # every field of CWorld::Players[0]
```

Lists instructions that use the address as a call/jmp target, immediate or memory operand, grouped by the
containing function. For an exact address it also lists pointers stored as data, so a virtual method shows its
vtable slot. The first run caches a full disassembly in `build/re/gta_sa.asm` (130 MB, about 8 s). The cache
is rebuilt when the exe is newer. The cache is a linear sweep: after data embedded in code it can lose sync for a
few instructions (about 1% of function starts), so a reference there can be missed.

## plugscan.py: plugins that touch the bytes you want to hook

```
python tools/re/plugscan.py 0x438481 0x5D2E07
```

Run it before hooking a function entry or a call site. It searches every .asi/.dll/.exe in the game folder for
32-bit constants in [addr-4, addr+5]. GInputSA read an operand from the first instruction of
`CCheat::AddToCheatString` (0x438481) and crashed when a MinHook trampoline replaced that instruction.
Hits in files over 20 MB (the installers in BONUS) are marked "probably data".

## pdb_symbols.py: build the symbol map

```
python -I tools/re/pdb_symbols.py gta_sa.pdb "E:/GTA San Andreas/gta_sa.exe" re/gta_sa_symbols.txt
```

Dumps the public and global symbols of a PDB as a sorted `ADDR name` list. That list is the map used above.

## scm_missions.py: list the missions in main.scm

```
python tools/re/scm_missions.py "E:/GTA San Andreas/data/script/main.scm"
```

Prints the index, offset, size and script name of each mission.

## scm_disasm.py: disassemble main.scm

```
python tools/re/scm_disasm.py "E:/GTA San Andreas/data/script/main.scm" --start 0x2f662 --count 40
python tools/re/scm_disasm.py "E:/GTA San Andreas/data/script/main.scm" --find 0417
```

Disassembles SCM opcodes from an offset with names from `re/sa_commands.json`, or lists the raw offsets of an opcode.

## HOODLUM stubs

The crack moved about 700 original instructions out of their functions: in place of the instruction there is a
`jmp` into a stub at 0x401000+ (sometimes after a `nop`), and the stub jumps back right after that `jmp`.
`fn.py` prints the moved instructions under the `jmp`, marked with `|`. `xref.py` credits a reference inside a
stub to the function it was moved from. Keep this in mind when you patch bytes: the original instruction is no
longer there.

## destub.py: an exe for decompilers

```
python tools/re/destub.py      # -> build/re/gta_sa_analysis.exe
```

Ghidra takes a stub's `jmp` for the end of the function and cuts the decompiled body there (REA reports it as a
terminal call; `CPlayerPed::ClearWeaponTarget` came out as one call). This writes a copy of the exe with the 525
moved runs put back in place (nop-padded, a relative `call` re-aimed), for analysis only. The copy also marks the
encrypted leftovers of the retail protection (a second `.text`, `.init` and a second `.data`, 9 MB flagged
executable) as plain data: Ghidra decompiled garbage there and its analysis ran past REA's 330 s startup limit.
Open that copy in REA: `open_binary` with `E:\GTA COOP\build\re\gta_sa_analysis.exe` and `provider_id: "ghidra"`,
then `procedure_pseudo_code` with an address (`0x609c80`). The first request waits for Ghidra's default analysis,
which is redone on every open. Its functions are named `FUN_<addr>`, and the Windows Ghidra route of REA does not
accept renames: `fn.py` gives the names.

Self-checks of `fn.py` and `xref.py` (name matching, function ends, brace matching, xref against the real exe) are
kept with the tests, outside the repository.
