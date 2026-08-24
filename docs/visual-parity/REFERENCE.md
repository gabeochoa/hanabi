# The frozen reference set

`ref/*.png` are the ONLY reference. Do not re-shoot Puffin: five agents share
one desktop and one app, and an agent that relaunches it (or one that flips its
backend with `--args -mockBackend NO`, which already happened) moves the target
under everyone else. These files do not move.

- `ref/01_home.png` — 1180x949, mock backend, welcome dismissed, one thread open

Compare against them:

```bash
/usr/bin/python3 ~/w/vis/compare.py ~/w/vis/ref/01_home.png <your_shot.png> --regions --diff /tmp/d.png
```

If you genuinely need a state that is not here — a settings sheet, a light
theme, an expanded tool pile — ASK ME rather than driving the live app. I will
capture it, freeze it, and add it to this list.

## Compare LIKE FOR LIKE — this is worth 45 percentage points

`ref/01_home.png` has a **thread open**. If you shoot hanabi on the Home
digest and compare it to that, the `main` region reads ~58% and tells you
nothing: you are measuring "a list of cards versus a transcript", not a design
difference.

Shoot hanabi with the same tab open:

```json
{"window_width":1180,"window_height":949,"open_tabs":["t9","t2"],"active_tab":"t2","pinned_tabs":["t9","t2"],"theme":"dark"}
```

Measured on the same binary, same minute: Home digest **47.9% structural**, one
thread open **12.5%**. Same code, same palette — the whole difference was what
was on screen.

The same trap, one region down: `ref/01_home.png` has **two** tabs open and
**both are pinned**. Shooting a single unpinned tab scores the tab bar at
25.3% — a second tab's worth of empty strip, plus a filled active tab sitting
where the reference has an outlined inactive one, plus two missing pin glyphs.
Restoring the second pinned tab in the blob above takes the region to 5.5% with
no code change at all. Anything you measure in the tab bar before that is
measuring the fixture.
