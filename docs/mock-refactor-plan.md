# Mock shared-asset refactor plan (execute when mock/ tree is free)

## Why
mock/index.html (~1602 lines) and mock/bluesky.html duplicate the same big
<style> + <script>. The phaseA–F.html files each re-embed subsets. Duplication
means every design tweak must be repeated N times and they drift.

## Hard constraint: MUST still open via file://
Browsers block ES modules (`type="module"`) and many fetch paths on file://.
So shared code loads via CLASSIC tags with RELATIVE paths:
  <link rel="stylesheet" href="assets/hanabi.css">
  <script src="assets/data.js"></script>
  <script src="assets/ui.js"></script>
No import/export, no fetch, no CDN. Plain global functions/objects (as today).
Verify by actually opening file:// still works (the JS runs, no module errors).

## Target layout
mock/
  assets/
    hanabi.css        # the full shared stylesheet (all tokens + component CSS)
    data.js           # THREADS, FOLDER_MEMBERS, sample data (invented)
    ui.js             # render/tab/transcript/theme/spotlight/menubar/keyboard logic
    (optional) v2.css / v2.js  # blue-sky-only empty-state + motion styles/logic
  index.html          # full app: links assets/hanabi.css + data.js + ui.js + the body markup
  bluesky.html        # index body + assets/v2.css + v2.js on top
  mock-phases/
    phaseA..F.html     # each links the SHARED css, includes only the body markup
                       # subset it needs, and calls only the ui.js it needs.

## Sequencing (avoid breaking all mocks at once)
1. Extract CSS -> assets/hanabi.css; make index.html link it; VERIFY index renders
   identically (render + eyeball). Commit.
2. Extract JS data -> assets/data.js; then ui.js; index.html uses <script src>.
   VERIFY render + node --check each asset. Commit.
3. Point bluesky.html at the shared assets + its own v2 overlay. VERIFY. Commit.
4. Rework phaseA–F to link shared CSS + minimal body. These are SUBSETS — keep
   each phase showing only its scope (don't just include everything). VERIFY each. Commit.
5. Each step: <div> balance, node --check, forbidden-term grep, file:// open sanity.

## Guardrails
- One extraction step at a time, commit between, so a break is isolated.
- Do NOT change the visual output — pure refactor. Screens must look identical.
- Keep hanabi-neutral names; no company terms; no external refs.
