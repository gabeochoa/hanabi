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
import pathlib
import re
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

    # Roll every site up to the innermost frame that is hanabi's OWN code.
    #
    # Without this the table is a list of libc++ and afterhours internals: the
    # top twenty rows of a real run were nineteen `string::__init_copy_ctor`
    # and one `ComponentConfig::ComponentConfig`, which is true and useless.
    # The actionable question is which function in src/ is asking for it, and
    # the same hanabi function reaches the allocator down a dozen different
    # vendor paths, so it only shows up once the vendor frames are collapsed.
    owner: dict[str, list[float]] = {}
    for _, per_frame, per_frame_bytes, pcs in rows:
        names = [resolved.get(pc, hex(pc)) for pc in pcs]
        who = next((n for n in names if is_app(n)), "(no hanabi frame)")
        acc = owner.setdefault(who, [0.0, 0.0])
        acc[0] += float(per_frame)
        acc[1] += float(per_frame_bytes)

    print(f"  {'calls/f':>9} {'bytes/f':>9}  rolled up to the innermost "
          f"hanabi frame")
    for who, (calls, byts) in sorted(owner.items(), key=lambda kv: -kv[1][0]):
        print(f"  {calls:>9.1f} {byts:>9.0f}  {who}")

    print()
    print(f"  {'calls/f':>9} {'bytes/f':>9}  innermost frame  <-  callers")
    for _, per_frame, per_frame_bytes, pcs in rows:
        names = [resolved.get(pc, hex(pc)) for pc in pcs]
        print(f"  {per_frame:>9} {per_frame_bytes:>9}  " + "  <-  ".join(names))
    return 0


VENDOR_PREFIX = ("ah::ui::", "afterhours::", "std::", "string", "sokol",
                 "_sg_", "sg_", "fons", "sgl_", "operator new", "void* std::")


def app_files() -> set[str]:
    """Basenames of hanabi's own sources.

    A frame belongs to the app when its `atos` location names one of these AND
    its symbol is not in a vendored namespace -- both halves are needed,
    because `theme.h` and `components.h` exist in src/ and in
    vendor/afterhours/ alike, and libc++ reports header locations (`vector.h`,
    `future`) that no glob of src/ would ever match.
    """
    root = pathlib.Path(__file__).resolve().parent.parent
    return {p.name for p in (root / "src").rglob("*")
            if p.suffix in (".h", ".cpp", ".mm")}


APP_FILES = app_files()


def is_app(name: str) -> bool:
    if name.startswith("0x") or name.startswith(VENDOR_PREFIX):
        return False
    m = re.search(r"\[([^\]:]+)[:\]]", name)
    return m is not None and m.group(1) in APP_FILES


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
