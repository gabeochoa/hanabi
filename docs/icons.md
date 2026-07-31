# hanabi — icon strategy (native macOS look, licensing-safe)

Goal: look like a native SwiftUI/AppKit app. Two viable paths; we do BOTH —
real SF Symbols when running on macOS, a bundled MIT fallback set otherwise.

## The SF Symbols licensing reality (important)
- SF Symbols are Apple-licensed and may be used ONLY on Apple platforms (macOS/
  iOS/etc.). You may NOT redistribute the symbol font/SVGs or ship them on
  non-Apple platforms. You may NOT extract-and-embed the glyphs as your own
  assets in the repo.
- BUT: a native macOS app is ALLOWED to RENDER SF Symbols at runtime via the OS
  API — the glyphs come from the operating system, not from our repo. That's the
  compliant, zero-redistribution path, and it's exactly what makes an app look
  "made in Swift".
- => We must NOT commit any SF Symbol SVG/font into the repo. Render them from
  the OS at runtime instead (below), and keep a separately-licensed fallback set
  in the repo for the (non-macOS / API-unavailable) case.

## Path A (macOS runtime) — render real SF Symbols from the OS  [PREFERRED on mac]
- AppKit exposes: `[NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil]`
  (Obj-C) — available macOS 11+. This returns an OS-provided image; nothing is
  bundled or redistributed. We call it from a tiny Objective-C++ (.mm) shim
  (same pattern as src/sokol_impl.mm — does NOT touch vendor/afterhours).
- Rendering into afterhours' Metal/Sokol UI: rasterize the NSImage to RGBA
  bytes (draw into a bitmap context / CGImage) and upload as a texture the UI
  draws — OR draw the symbol into the same offscreen path used elsewhere. This
  is app code in our .mm, not a vendor change.
- Symbol names are just strings ("gearshape", "plus", "sidebar.left",
  "magnifyingglass", "triangle.fill", "circle.fill", "diamond.fill",
  "chevron.right", "folder", "archivebox", "pin.fill", "xmark"). We keep a
  small hanabi→SF-name map so the rest of the UI stays backend-neutral.
- Gate on availability: if `imageWithSystemSymbolName` returns nil (older OS)
  fall back to Path B.

## Path B (fallback / non-mac) — bundled MIT-licensed icon set  [always in repo]
Ship a small set of open, commercial-OK SVGs so the app is self-sufficient and
the repo has no Apple assets. Best matches for the SF look (all free for
commercial use, redistributable):
- Lucide (ISC license) — https://lucide.dev — clean, SF-ish stroke icons. TOP PICK.
- Feather (MIT) — https://feathericons.com — minimal stroke, very SF-like.
- Remix Icon (Apache-2.0) — outlined + filled variants (good for our filled glyphs).
- Tabler Icons (MIT), Iconoir (MIT) — also fine.
- (buzap/open-symbols converts OSS sets INTO the SF Symbols format for Apple
  apps — same idea as OrchardKit; but for a fallback we just need the raw SVGs,
  so Lucide/Feather directly is simpler. Do NOT ship anything labeled as actual
  Apple SF Symbols.)
Chosen fallback: LUCIDE (ISC) — closest to the SF stroke weight/rounding. Vendor
a handful of the ~15 SVGs we actually use into resources/icons/ with the ISC
LICENSE file. Attribution kept in resources/icons/LICENSE.

## Decision
- macOS build: Path A (OS-rendered SF Symbols) for the authentic system look.
- Fallback + any non-mac: Path B (bundled Lucide SVGs, ISC).
- NEVER commit Apple SF Symbol assets to the repo. The macOS path renders them
  from the OS at runtime only.
- Icon lookup goes behind a small `icon(name)` indirection so callers use
  hanabi-neutral names; the mac shim maps to SF names, the fallback maps to a
  bundled SVG.

## Phase placement
- Currently the app draws glyphs as vector shapes (triangle/diamond/circle) via
  afterhours draw primitives — fine for status glyphs. The SF-Symbol work is a
  polish phase (chrome icons: gear, plus, search, sidebar toggle, folder, pin,
  archive, close). See docs/phased-plan.md Phase H (icons).
