#!/usr/bin/env python3
"""Find labels whose horizontal padding does nothing.

WHY THIS EXISTS. `with_padding()` on an element that has a label and no child
divs is a silent no-op: padding is applied when laying out an element's
CHILDREN, and a label is not a child — it is drawn into the element's rect by
`position_text_ex`, which reads the rect and the alignment and never the
padding. `afterhours_gaps.md` #85, #91 and #109.

It has cost this project three times, and each time the code, its author and
its comment all believed the padding was applied:

  * a smart-view row's label sat 6px left of the reference for the whole
    parity effort, under a comment doing the arithmetic out loud (#85);
  * the composer's pill labels drew at their element's edge (#91);
  * **every session-row title was one pixel left**, which turned out to be
    100% of the session list's remaining parity headroom, after SIX rounds of
    investigation concluded the difference was the text rasterizer (#109).

The third one is why this script exists. A silent no-op on a value someone
computed is worse than a compile error and worse than a crash: it produces a
plausible wrong answer and a comment asserting the right one.

WHAT IT DOES. Scans the ECS/UI sources for `ComponentConfig` chains and reports
any that set a horizontal `Padding` on a `with_label` element with no nested
`div(`/`button(` — i.e. padding with nothing to push.

Two deliberate exemptions, because a checker that cries wolf gets waived
wholesale:

  * **`.left`/`.right` of 0** is the documented workaround for gap #76 (an
    element with NO padding silently gets a fraction of the SCREEN), so zeroing
    is a deliberate no-op.
  * **`TextAlignment::Center`**, where horizontal padding is irrelevant by
    construction: the text centres in the rect whatever the padding says. The
    defect is padding meant to inset text from the edge it is aligned to, which
    is Left and Right.

WHAT TO DO ABOUT A HIT. `with_margin()` — margin moves the ELEMENT, and the
element's rect is what the text is drawn from. #85's escape list rules that
out, wrongly, which is what kept #109 alive; #109 is the correction.

WHY THERE IS A BASELINE RATHER THAN A CLEAN SWEEP. There are ten of these
today, and they are NOT ten bugs to go and fix: wherever one is visible,
somebody has already tuned the geometry around it, so "fixing" the padding
moves something that currently looks right. What they are is thirty places
where the next edit will silently do nothing. So the existing set is frozen in
`label_padding_baseline.txt` and this fails only on a NEW one — the point is to
stop the thirty-first, and to hand the list to anyone who touches one of the
thirty.

  usage: check_label_padding.py            fail on anything not in the baseline
         check_label_padding.py --update   rewrite the baseline (say why)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = sorted((ROOT / "src").rglob("*.h")) + sorted((ROOT / "src").rglob("*.cpp"))

# A config chain: from `ComponentConfig{}` to the `with_debug_name(...)` that
# ends it. Non-greedy, so nested chains stay separate.
CHAIN = re.compile(
    r"ComponentConfig\{\}(?P<body>.*?)\.with_debug_name\(\s*\"(?P<name>[^\"]*)\"",
    re.S,
)
# The PADDING blocks in a chain, and then the horizontal fields inside one.
#
# Two regexes and not one, because one was wrong. It used to match
# `.left|.right = pixels(N)` anywhere in the chain body -- which is also the
# shape of a MARGIN, the very thing #109 prescribes as the FIX. So a widget
# that had already been fixed was counted as a defect: `thinking_dot` sat in
# the baseline for its `Margin{.right = pixels(8)}`, and the only reason it
# ever left was that the 8 became an expression and stopped looking like a
# number. A checker whose job is to stop false confidence cannot be a source
# of it.
#
# The value is `[^,}]+` and not a numeral for the same reason: a real defect
# written `pixels(kRowTitlePad)` -- which is exactly how #109's was written --
# must count. Only a literal zero is exempt (gap #76's deliberate no-op), and
# that is now tested for rather than inferred from a failed parse.
PAD_BLOCK = re.compile(r"\.with_padding\(\s*Padding\{(?P<pad>.*?)\}\s*\)", re.S)
H_PAD = re.compile(r"\.(?:left|right)\s*=\s*pixels\(\s*(?P<v>[^,}]+?)\s*\)")


def _is_zero(value: str) -> bool:
    """A literal zero, in any of the spellings this repo uses."""
    return value.strip().rstrip("f") in ("0", "0.0", "0.")

BASELINE = ROOT / "scripts" / "label_padding_baseline.txt"


def hits():
    out = []
    for path in SOURCES:
        text = path.read_text(errors="ignore")
        for m in CHAIN.finditer(text):
            body, name = m.group("body"), m.group("name")
            if ".with_label(" not in body:
                continue
            # A chain that opens a container for children is not this defect.
            if "div(ctx" in body or "button(ctx" in body:
                continue
            if "TextAlignment::Center" in body:
                continue
            pads = [v for blk in PAD_BLOCK.finditer(body)
                    for v in H_PAD.findall(blk.group("pad"))]
            live = [v for v in pads if not _is_zero(v)]
            if not live:
                continue
            line = text[: m.start()].count("\n") + 1
            out.append((path.relative_to(ROOT), line, name or "<unnamed>",
                        ", ".join(live)))
    return out


def main():
    # Keyed by NAME, not by line: the baseline must survive the file moving
    # around it, or it becomes a merge conflict nobody reads.
    found = {name: (path, line, pad) for path, line, name, pad in hits()}

    if "--update" in sys.argv:
        BASELINE.write_text("\n".join(sorted(found)) + "\n")
        print(f"baseline rewritten: {len(found)} entries")
        return 0

    known = set()
    if BASELINE.exists():
        known = {ln.strip() for ln in BASELINE.read_text().splitlines()
                 if ln.strip() and not ln.startswith("#")}

    new = sorted(set(found) - known)
    gone = sorted(known - set(found))

    for name in gone:
        print(f"  FIXED   {name}  (in the baseline, no longer a hit)")
    for name in new:
        path, line, pad = found[name]
        print(f"  NEW     {path}:{line}  {name}  ({pad})")

    if new:
        print(f"\n{len(new)} NEW label(s) set horizontal padding that does "
              f"nothing (afterhours_gaps.md #85/#91/#109).")
        print("Padding on a label is not applied -- a label is not a child.")
        print("Use with_margin(): it moves the ELEMENT, and the element's rect "
              "is what the text is drawn from.")
        print("If it is deliberate (zeroing to defeat gap #76, or a centred "
              "label), the checker already exempts those -- otherwise run "
              "--update and say why in the commit.")
        return 1

    if gone:
        print(f"\n{len(gone)} baseline entr(y/ies) fixed -- run --update.")
        return 1

    print(f"no new label-padding no-ops ({len(known)} known, all still there)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
