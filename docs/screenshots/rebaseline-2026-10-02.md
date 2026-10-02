# Rebaseline review, 2026-10-02 (lead lane)

What this is: every committed screenshot baseline re-captured at main 42509a1
(`zig build update-baselines`, Aspen, the same zig 0.16 toolchain and bundled
fonts). The previous set was committed on 2026-09-16 (70b7208), and at the
batch-2 tip 9b664d0 no screen matched it (0/167). 161 PNGs re-captured. The
other 6 (18ag/18ah/18ap submenus, 18al/18l/18m effort pickers) are DELETED:
no declared scene produces them any more -- the row menu went flat in 2801134
(09-14, the reference's shape) and the effort picker became part of the one
model panel in 77877a1 (09-13) -- so they could only ever read MISSING.

How it was reviewed: a pixel diff of every pair, then old/new side by side
for one screen from each family (home, transcript, chat welcome, loading,
settings x3, error, attach refused, artifacts, big transcript, split
settings, auth, archived, ask cards x3, folded sidebar, palette, shortcuts,
narrow, skeleton). The sheets are not committed; the old PNGs are in git
history at 70b7208.

## Intended changes (each traced to a commit or a recorded decision)

- **Sidebar.** A RECENTS section header with "N need you · M", filled
  orange count badges on Home/Blocked/Review, the selected row tinted with
  the accent, and the list starts one row lower (the last row drops off the
  bottom of the fixed-height captures). Running-row spinners are drawn at
  a different phase; the capture clock pins the phase, the arc is the same.
- **Palette.** The reference's palette (b4a65fa, "the reference's palette":
  nightSky #0D141D window, headerBg #171F2A rail, naviBlue #3B9EDB accent,
  hairline #262F38). Light theme: cards drawn as filled panels, not
  outlined white boxes.
- **Settings is a tab** in the main pane with its own pane list in the
  sidebar, not a modal sheet; Appearance in the reference's grouped shape
  (Reading width / Context / Composer / Typeface ...).
- **Composer strip.** The model chip reads the mock's model ("Opus 5")
  on an attached thread and "Server default" (no effort suffix) on the New
  Thread surface; previously "Server default (High)" everywhere.
- **Artifacts.** The audio row reads "unavailable · audio is not played in
  this build" (715fe97, the in-app player is held out of the build); the
  image row sits lower because the run's last message now shows above it.
- **Shortcuts.** New rows: Close Kept Tab, the six relative tab moves and
  Archive Current Conversation (this lane), so the list scrolls further.
- **Settings > Chat.** "Start from the newest match" (this lane).

## Look like regressions, for owner review (not fixed here)

1. **Outlines on raised surfaces are gone (contrast ~1.05:1).** The
   hairline token `border` = #262F38 is "mutedText 20 % on nightSky", i.e.
   derived over the WINDOW. Drawn on `panel_bg_2` = #242C37 (the ask card,
   skeleton cards, home cards) it is two levels away from its ground:
   measured at x=700 in 55_ask_card_dark the field edge is (38,47,56) on
   (36,44,55). Where it shows:
   - the ask card's free-text fields ("Or write your own answer", "Anything
     I should know first?"): no visible box, only a caret target;
   - the Decline / Deny buttons (55, 59-66): no outline, so they read as
     plain text next to the filled Submit/Approve;
   - the loading skeleton cards (23): the placeholder bars are barely
     visible inside them;
   - Home cards (01, 08-13, 48): border gone, cards are fill-only.
   **Fixed after review (coordinator call, 2026-10-02), for owner review:**
   `theme::border_raised()` = mutedText blended over panel_bg_2 just far
   enough for 3:1 (3.03:1 dark, 3.02:1 light; test_theme_contrast holds it
   to 3.0-3.6) now draws those outlines and the skeleton bars; `border`
   itself is unchanged everywhere else. 34 screens re-captured. The same
   pass found the light-theme Decline/Deny label at 4.26:1 rendered (mutedText
   on the card) under ask_contrast_gate's 4.5 bar; enabled unfilled ask
   actions now take the primary ink in both themes, as dark already did; and a
   DISABLED action's outline (the window hairline halved toward the card)
   was the card's own colour, so the unanswerable-backend card's
   Approve/Deny drew no buttons at all -- it now halves the raised hairline.
   ask_contrast_gate is green again (it had been red since the re-capture).
2. **Model chip disagreement.** The same mock account shows "Opus 5" on an
   open thread and "Server default" on New Thread (13, 17, 23, 74). Left as
   is, because Hanabi cannot know what "Server default" will resolve to: an
   open thread's chip reads the model its attach reported serving, but a
   create with no model pin sends no `options.llm` and no harness, and the
   server then picks the harness (not on the wire) and applies the user's
   server-side preference and that harness's default. The model menu marks a
   default per HARNESS, never which harness a create gets, so "Server
   default (Opus 5)" would be a guess on any account with more than one
   harness.

Nothing else read as broken: no clipped text, no overlapping widgets, no
blank panes beyond the ones the scene intends.
