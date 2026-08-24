#!/usr/bin/env python3
"""Generate the hanabi chrome-icon spritesheet from Lucide (ISC) SVGs.

WHAT: fetch ~13 Lucide line-icon SVGs, rasterize each into an equal cell
(white/monochrome on transparent so the app tints per-theme at draw time),
pack them into ONE PNG atlas laid out as a grid, and emit:
  - resources/icons/icons.png        (the atlas)
  - resources/icons/icons.atlas      (human-readable name -> x,y,w,h map)
  - src/ui/icons_atlas.h             (generated C++ name->rect table)
  - resources/icons/LICENSE          (Lucide ISC text; fetched)

WHY THIS APPROACH: aspen has no rsvg-convert/inkscape and cairosvg can't load
libcairo, and ImageMagick's internal SVG renderer produces blank output. macOS
`qlmanage` (WebKit) rasterizes SVGs reliably, but flattens transparency, so we
render each icon with a BLACK stroke on white, then convert luminance->alpha
(dark strokes -> opaque, white bg -> transparent) and force the color to white
via ImageMagick. Committing the OUTPUT (icons.png + map + LICENSE) is what
matters; this script is a convenience to regenerate it.

Requires: curl, qlmanage (macOS), magick (ImageMagick 7).
Run:  python3 scripts/gen_icons.py
"""
import os, subprocess, sys, tempfile, shutil, json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ICONS_DIR = os.path.join(REPO, "resources", "icons")
ATLAS_PNG = os.path.join(ICONS_DIR, "icons.png")
ATLAS_MAP = os.path.join(ICONS_DIR, "icons.atlas")
LICENSE_OUT = os.path.join(ICONS_DIR, "LICENSE")
HEADER_OUT = os.path.join(REPO, "src", "ui", "icons_atlas.h")

RAW = "https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/{}.svg"
LICENSE_URL = "https://raw.githubusercontent.com/lucide-icons/lucide/main/LICENSE"

# hanabi-neutral name -> Lucide icon file (in grid order).
# Chrome icons only; the status glyphs (triangle/diamond/dot) stay drawn shapes.
ICONS = [
    ("brand",        "sparkle"),          # ✦ brand mark in the title bar
    ("gear",         "settings"),         # ⚙ settings
    ("plus",         "plus"),             # + new task
    ("search",       "search"),           # 🔍 search field
    ("sidebar_close","panel-left-close"), # « collapse
    ("sidebar_open", "panel-left-open"),  # » expand
    ("chevron_down", "chevron-down"),     # ▾ folder chevron
    ("home",         "house"),            # ⌂ Home smart view
    ("blocked",      "ban"),              # ⛔ Blocked smart view
    ("review",       "check"),            # ✓ Review smart view
    ("star",         "star"),             # ★ Starred smart view
    ("folder_grid",  "folder-tree"),      # all-folders header
    ("fold_all",     "chevrons-down-up"), # fold-all header
    ("clock",        "clock"),            # ⏱ attention / waiting (gap #19)
    ("automated",    "repeat"),           # ⟳ automated / scheduled / cron (gap #20)
    ("close",        "x"),                # ✕ close buttons (tab/composer/settings/search-clear)
    ("archive",      "archive"),          # ▤ Archived smart view (was the last TODO(icon-atlas))
    ("layers",       "layers"),           # ≡ tool-call pile count badge (last raw unicode chrome)
    # The smart-view glyphs the reference actually uses. Puffin names them as
    # SF Symbols in SmartViewSidebar.systemImage -- hand.raised,
    # checkmark.circle, pin, sidebar.leading -- and these are the Lucide icons
    # of the same drawing. Before these existed the Blocked view wore a
    # hand-drawn warning triangle and a comment explaining that the atlas had
    # nothing better, Review wore a bare check, and Pinned wore a star.
    ("hand",         "hand"),             # Blocked   (SF: hand.raised)
    ("check_circle", "circle-check"),     # Review    (SF: checkmark.circle)
    ("pin",          "pin"),              # Pinned    (SF: pin)
    ("panel_left",   "panel-left"),       # the VIEWS strip's panel toggle
    ("sliders",      "sliders-horizontal"), # the search pill's filter affordance
]

CELL = 32          # px per cell. Icons draw at ~14-16px logical (up to ~32px on
                   # a 2x-retina window), so a 32px cell keeps the minification
                   # ratio at the real draw size <= ~2x, which bilinear handles
                   # cleanly. afterhours' load_texture sampler has no mipmaps
                   # (mipmap_filter=NEAREST, single mip level), so authoring the
                   # atlas NEAR the draw size is the only app-side anti-alias
                   # lever — see afterhours_gaps.md #14.
COLS = 4           # grid columns
PAD  = 3           # inner padding within each cell (SVG rendered smaller then centered)
RENDER = 240       # intrinsic px we blow the SVG up to before qlmanage rasterizes

def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)

def fetch(url, dest):
    run(["curl", "-fsSL", "-m", "20", "-o", dest, url])

def rasterize_cell(svg_path, out_png, tmp):
    """Render a Lucide SVG to a CELLxCELL white-on-transparent PNG cell."""
    # 1) force a solid black stroke so WebKit renders visible geometry on white.
    with open(svg_path) as f:
        svg = f.read()
    svg_b = svg.replace('stroke="currentColor"', 'stroke="#000000"')
    # keep any fills currentColor -> black too (most Lucide are stroke-only)
    svg_b = svg_b.replace('fill="currentColor"', 'fill="#000000"')
    # Bump the stroke weight so the thin (2px @ 24vb) line survives raster +
    # downscale and reads clearly at the ~16px on-screen size. ~2.6px keeps the
    # Lucide look while staying legible in a small cell.
    svg_b = svg_b.replace('stroke-width="2"', 'stroke-width="2.6"')
    # qlmanage rasterizes an SVG at its intrinsic width/height (24px) in the
    # top-left of the thumbnail rather than scaling to fill, so blow the
    # intrinsic size up to RENDER px for a crisp high-res raster we then trim +
    # downscale (upscaling a 24px render would be blurry).
    svg_b = svg_b.replace('width="24"', f'width="{RENDER}"')
    svg_b = svg_b.replace('height="24"', f'height="{RENDER}"')
    black_svg = os.path.join(tmp, "black.svg")
    with open(black_svg, "w") as f:
        f.write(svg_b)
    # 2) qlmanage renders the (now RENDER-px) SVG into the thumbnail.
    inner = CELL - 2 * PAD
    ql_dir = os.path.join(tmp, "ql")
    if os.path.isdir(ql_dir):
        shutil.rmtree(ql_dir)
    os.makedirs(ql_dir)
    run(["qlmanage", "-t", "-s", str(RENDER), "-o", ql_dir, black_svg])
    rendered = None
    for fn in os.listdir(ql_dir):
        if fn.endswith(".png"):
            rendered = os.path.join(ql_dir, fn)
            break
    if not rendered:
        raise RuntimeError(f"qlmanage produced no PNG for {svg_path}")
    # 3) black-on-white -> white-on-transparent. qlmanage renders the SVG at
    #    its native 24px in the top-left of the thumbnail (it does NOT scale to
    #    fill), so we build a white-on-transparent version (alpha := 1 - lum),
    #    TRIM to the icon's actual bounds, scale that up to `inner`, and center
    #    it in a CELLxCELL transparent canvas.
    run([
        "magick", rendered,
        "-colorspace", "Gray",
        # inverted luminance -> alpha mask (dark strokes opaque, white bg clear)
        "-negate", "-write", "mpr:alpha", "+delete",
        # a pure-white RGB layer the same size as the rendered thumbnail...
        rendered, "-alpha", "off", "-fill", "white", "-colorize", "100",
        # ...masked by that alpha.
        "mpr:alpha", "-alpha", "off", "-compose", "CopyOpacity", "-composite",
        "-trim", "+repage",
        "-resize", f"{inner}x{inner}",
        "-background", "none", "-gravity", "center", "-extent", f"{CELL}x{CELL}",
        "-type", "TrueColorAlpha", f"PNG32:{out_png}",
    ])

def alpha_mean(png):
    r = run(["magick", png, "-channel", "A", "-separate",
             "-format", "%[fx:mean]", "info:"])
    return float(r.stdout.strip())

def main():
    os.makedirs(ICONS_DIR, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="hanabi_icons_")
    print(f"tmp: {tmp}")

    # License
    fetch(LICENSE_URL, LICENSE_OUT)
    print(f"wrote {LICENSE_OUT}")

    rows = (len(ICONS) + COLS - 1) // COLS
    sheet_w = COLS * CELL
    sheet_h = rows * CELL

    cell_pngs = []
    entries = []  # (name, x, y, w, h)
    for idx, (name, lucide) in enumerate(ICONS):
        svg = os.path.join(tmp, f"{lucide}.svg")
        fetch(RAW.format(lucide), svg)
        cell = os.path.join(tmp, f"cell_{name}.png")
        rasterize_cell(svg, cell, tmp)
        am = alpha_mean(cell)
        if am <= 0.0:
            raise RuntimeError(f"BLANK cell for {name} ({lucide}) — alpha mean 0")
        col = idx % COLS
        row = idx // COLS
        x, y = col * CELL, row * CELL
        entries.append((name, x, y, CELL, CELL))
        cell_pngs.append((cell, x, y))
        print(f"  {name:13s} <- {lucide:18s} cell=({x},{y}) alpha_mean={am:.4f}")

    # Compose the atlas: transparent canvas + each cell at its offset.
    compose = ["magick", "-size", f"{sheet_w}x{sheet_h}", "xc:none"]
    for cell, x, y in cell_pngs:
        compose += ["(", cell, ")", "-geometry", f"+{x}+{y}", "-composite"]
    # Force every RGB channel to white while PRESERVING alpha. The app tints
    # the atlas at draw time via a color multiply (draw_texture_pro), so the
    # stroke RGB MUST be white (tint*white == tint); black strokes would make
    # tint*0 == black and ignore the theme color entirely — invisible in dark
    # mode. Some ImageMagick builds' CopyOpacity path above leaves the RGB
    # black, so we assert white here as the final, backend-independent step.
    compose += ["-channel", "RGB", "-evaluate", "set", "100%", "+channel"]
    compose += ["-type", "TrueColorAlpha", f"PNG32:{ATLAS_PNG}"]
    run(compose)
    print(f"wrote {ATLAS_PNG} ({sheet_w}x{sheet_h}, {rows}x{COLS} grid, {CELL}px cells)")

    # Atlas map (human-readable).
    with open(ATLAS_MAP, "w") as f:
        f.write("# hanabi icon atlas — Lucide (ISC). Generated by scripts/gen_icons.py\n")
        f.write(f"# sheet {sheet_w}x{sheet_h}, {CELL}px cells, {COLS} cols\n")
        f.write("# name x y w h\n")
        for (name, x, y, w, h) in entries:
            f.write(f"{name} {x} {y} {w} {h}\n")
    print(f"wrote {ATLAS_MAP}")

    # Generated C++ header.
    with open(HEADER_OUT, "w") as f:
        f.write("// AUTO-GENERATED by scripts/gen_icons.py — do not edit by hand.\n")
        f.write("// Lucide (ISC) chrome-icon atlas: name -> source rect in icons.png.\n")
        f.write("#pragma once\n#include <array>\n#include <string_view>\n\n")
        f.write("namespace hanabi::icons {\n\n")
        f.write("struct AtlasEntry { std::string_view name; float x, y, w, h; };\n\n")
        f.write(f"inline constexpr int kSheetWidth  = {sheet_w};\n")
        f.write(f"inline constexpr int kSheetHeight = {sheet_h};\n")
        f.write(f"inline constexpr int kCell        = {CELL};\n\n")
        f.write(f"inline constexpr std::array<AtlasEntry, {len(entries)}> kAtlas = {{{{\n")
        for (name, x, y, w, h) in entries:
            f.write(f'    {{"{name}", {x}.f, {y}.f, {w}.f, {h}.f}},\n')
        f.write("}};\n\n")
        f.write("} // namespace hanabi::icons\n")
    print(f"wrote {HEADER_OUT}")

    shutil.rmtree(tmp, ignore_errors=True)
    print("done.")

if __name__ == "__main__":
    main()
