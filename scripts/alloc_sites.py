#!/usr/bin/env python3
"""Symbolize scripts/alloc_sites.sh output.

The app prints, for each of the busiest allocation call sites, three raw
return addresses and — separately — every loaded image and the address it
landed at. Picking the right image per address is what makes a frame inside
libsystem_malloc as readable as a frame inside hanabi; `atos -o hanabi.exe`
renders it as a bare hex number.

Reads the run's log on stdin, writes the table on stdout.
"""
import bisect
import subprocess
import sys


def main() -> int:
    images: list[tuple[int, str]] = []
    rows: list[tuple[str, str, str, list[int]]] = []
    for line in sys.stdin:
        if line.startswith("[sites-image] "):
            base, path = line.split(None, 2)[1:]
            images.append((int(base, 16), path.strip()))
        elif line.startswith("[sites] ") and line.rstrip().endswith(("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f")) and line.split()[1].isdigit() and line.split()[-1].startswith("0x"):
            f = line.split()
            rows.append((f[1], f[2], f[3], [int(x, 16) for x in f[4:] if x != "0x0"]))

    if not rows:
        print("alloc_sites: no [sites] rows — was HANABI_PROF_SITES=1 set?",
              file=sys.stderr)
        return 1

    images.sort()
    bases = [b for b, _ in images]

    wanted: dict[str, list[int]] = {}
    for _, _, _, pcs in rows:
        for pc in pcs:
            i = bisect.bisect_right(bases, pc) - 1
            if i >= 0:
                wanted.setdefault(images[i][1], []).append(pc)

    resolved: dict[int, str] = {}
    for path, pcs in wanted.items():
        uniq = sorted(set(pcs))
        base = bases[bisect.bisect_right(bases, uniq[0]) - 1]
        try:
            out = subprocess.run(
                ["atos", "-o", path, "-l", hex(base)] + [hex(p) for p in uniq],
                capture_output=True, text=True, timeout=120).stdout.splitlines()
        except Exception:
            out = []
        short = path.rsplit("/", 1)[-1]
        for pc, sym in zip(uniq, out):
            resolved[pc] = tidy(sym, short)

    print(f"  {'calls/f':>9} {'bytes/f':>9}  innermost frame  <-  callers")
    for _, per_frame, per_frame_bytes, pcs in rows:
        names = [resolved.get(pc, hex(pc)) for pc in pcs]
        print(f"  {per_frame:>9} {per_frame_bytes:>9}  " + "  <-  ".join(names))
    return 0


def tidy(sym: str, image: str) -> str:
    """Shorten a symbol to the part a reader is looking for."""
    sym = sym.replace("std::__1::basic_string<char, std::__1::char_traits<char>,"
                      " std::__1::allocator<char>>", "string")
    sym = sym.replace("std::__1::", "std::")
    sym = sym.replace("afterhours::ui::", "ah::ui::")
    sym = sym.replace(" (in hanabi.exe)", "")
    if "(" in sym and sym.endswith(")"):
        head, _, tail = sym.rpartition(" (")
        loc = tail[:-1]
        name = head.split("(")[0].strip()
        if len(name) > 76:
            name = name[:73] + "..."
        return f"{name} [{loc}]"
    if image != "hanabi.exe":
        return f"{sym.split('(')[0].strip()} [{image}]"
    return sym


if __name__ == "__main__":
    sys.exit(main())
