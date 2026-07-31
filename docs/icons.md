# hanabi — icon strategy (SIMPLE: one replaceable spritesheet)

Decision (user, 2026-07-31): keep it simple. No installs, no per-icon files, no
SF-Symbols licensing risk. Ship ONE icon spritesheet file that we can replace
later. Look "made in Swift" via an SF-like open icon set.

## Chosen set: LUCIDE (ISC license)
- ISC: free for commercial + personal use, NO attribution required, freely
  redistributable. Cleanest terms of any option; single source (unlike
  open-symbols, which mixes several sets each with their own license).
- Lucide is a fork of Feather — thin, rounded, consistent strokes that read as
  SF-adjacent, so the app looks native without using Apple assets.
- Keep resources/icons/LICENSE (the ISC text) next to the spritesheet.

## Format: a SINGLE spritesheet file (replaceable)
- **C++ app:** one PNG atlas `resources/icons/icons.png` (e.g. a grid of NxN
  cells, all icons white/monochrome on transparent so we can tint per-theme at
  draw time) + a tiny atlas map `resources/icons/icons.atlas` (or a generated
  header) of `name -> {row,col}` (or x,y,w,h). The app loads the ONE texture at
  startup and blits the sub-rect for each icon; tint via vertex color. To change
  icons later, regenerate the single PNG (+ map). No dependency, no install.
- **HTML mock:** one inline SVG sprite (`<svg style="display:none"><symbol
  id="ic-gear">…</symbol>…</svg>` then `<use href="#ic-gear">`). Single block,
  swappable, self-contained (no external refs — keeps the mock file standalone).
- Same icon NAMES on both sides (hanabi-neutral: gear, plus, search, sidebar,
  folder, folder-grid, pin, archive, close, chevron, home, blocked, review,
  star) so mock and app stay in lockstep and the set is swappable.

## Icons we need (chrome; ~15)
gear(settings), plus(new task), search, sidebar(collapse toggle), chevron(fold),
folder, folder-grid(all-folders), pin, archive, close(x), home, blocked, review,
star, working-ring. (Status GLYPHS — blocked triangle / review diamond / done
dot — stay DRAWN vector shapes in the app; they're not from the sheet.)

## Explicitly NOT doing (now)
- NOT bundling Apple SF Symbols (Apple-licensed, non-redistributable).
- NOT rendering SF Symbols from the OS via an Obj-C++ NSImage shim — possible
  later for extra native polish, but it's more complexity than the one-file
  spritesheet and NOT needed to look native. Left as an optional future note.
- NOT adding any package/dependency/CDN. One file in the repo, that's it.

## How to (re)generate the sheet (later, Phase H)
- Pull the ~15 Lucide SVGs by name, normalize to a common box, rasterize into a
  single PNG grid (monochrome/white on transparent), emit the name->cell map.
  A small offline script (scripts/gen_icons.*) does this; committing the OUTPUT
  (icons.png + map + LICENSE) is what matters — the script is a convenience.

## Phase
- Phase H (docs/phased-plan.md): swap ad-hoc chrome icons to the Lucide
  spritesheet; keep drawn status glyphs. License-audit gate: repo has NO Apple
  assets; the set is ISC/MIT with LICENSE present; icons load from the single sheet.
