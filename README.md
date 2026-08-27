# hanabi

A small, fast **native desktop client** for AI assistant conversations.
A single collapsible sidebar (smart views + folders) on the left, VS Code-style
tabs across the top, and the full transcript — with an inline composer — in the
main pane. Browse your threads, kick off new tasks, and reply inline with a live
token-by-token stream.

Built with **C++23**, the [afterhours](https://github.com/gabeochoa/afterhours)
ECS + immediate-mode UI framework, and **Sokol** (Metal on macOS) for rendering.

<!-- screenshot goes here -->

## Features

- **Collapsible single sidebar.** One sidebar with two states: a folded thin
  rail (icon-only smart views + collapse/expand toggle) and an unfolded full
  state (brand + search + New task + Settings + collapse; smart-view list with
  counts; folders; recent; a low-signal collapsed Archived section). The width
  animates with a smoothstep ease. Toggle via the header button **or Cmd+B**.
- **Tabbed chat panels.** Clicking a thread opens it in a tab (or focuses the
  existing one); an active-tab accent underline, per-tab close (×), and **Cmd+W**
  to close the active tab. Switching tabs swaps the transcript.
- **Tab persistence.** The open tab set and the active tab are saved and
  restored across launches.
- **Interactive composer.** A persistent composer at the bottom of the
  transcript. Type a reply and **Send** to continue an open thread, or use
  **New task** (`+` / Cmd+N) to kick one off. Replies stream back **live,
  token-by-token** — the assistant bubble appears immediately and fills in as
  tokens arrive, with a *thinking… / streaming…* affordance. Works fully offline
  against the mock; a config-driven SSE adapter drives it against a real backend.
- **In-app device-code login.** Point hanabi at a real backend without pasting a
  token: an in-app device-code flow shows a short code + a verification URL,
  polls until you approve in your browser, then persists the token (mode 0600,
  git-ignored). Entirely opt-in and config-driven — the zero-config mock needs
  no auth.
- **Menu-bar extra.** A native `NSStatusItem` shows the blocked-on-you count
  (`✦ N`) with a dropdown: Show hanabi, New task…, Quit — ambient presence
  without opening the window.
- **High-signal rows.** A row shows a status glyph + bold title **only** when
  it's blocked, needs you, review, or done — with a dedicated *shape* per state
  (red triangle blocked, green diamond review, dot done). Self-running threads
  are dimmed and calm; parked/archived are greyed; scheduled/cron rows are muted
  with a distinct repeat glyph. The status bar shows the blocked-on-you count.
- **Starred threads.** Star any thread from its sidebar row; stars persist across
  launches and surface in the Starred smart view.
- **Smart views.** Home (a digest grouped waiting-on-you → finished →
  self-running, each header color-coded by state), Blocked, Review, and Starred —
  each swaps the main pane. Plus user folders + Archived.
- **Native icons.** A single Lucide (ISC) spritesheet — one replaceable
  `resources/icons/icons.png` atlas, tinted per theme at draw time.
- **Light / dark theming.** All colors flow from a single swappable token set
  (`src/ui/theme.h`), switchable at runtime and persisted. Dark by default.

## Design

Hanabi talks to a data source through a small, backend-agnostic `Client`
interface (`src/api/client.h`). Two implementations ship:

- **`mock`** *(default)* — deterministic, in-memory sample data (a spread of
  thread states so smart views / folders / high-signal rows all have real
  content to render). The app builds and runs standalone with **no
  configuration and no network access** — including interactive replies and
  live streaming, simulated locally.
- **`http`** — a generic REST adapter. It has **no service baked in**: the base
  URL, auth token, request/chat/stream/auth paths, and even the JSON field names
  it reads are **all supplied at runtime** via a config file or environment
  variables. Nothing about any particular backend is compiled into this
  repository. The high-signal attention model degrades to a calm/unknown state
  when the backend doesn't supply one.

Every capability sits behind that one seam: browse (`list_sessions` /
`get_session`), kick off (`create_session`), reply (`send_message`), stream
(`send_message_streaming`), and authenticate (device-code flow) all have a
zero-config mock implementation and a config-driven http one. This keeps the app
useful out of the box and lets you point it at whatever backend you like without
touching the code.

## Build

Requires a C++23 compiler (clang recommended) and, on macOS, the Metal
frameworks (installed with the Xcode command-line tools).

```bash
git clone --recurse-submodules <this-repo>
cd hanabi
make            # build ./output/hanabi.exe for development
make run        # build + launch without changing the app-bundle workflow
make app        # build a self-contained TLS-enabled output/Hanabi.app
make verify-app # validate identity, signature, resources, and runtime libraries
make install-app   # install/update ~/Applications/Hanabi.app + register it
make uninstall-app # unregister and remove that exact bundle
make test       # run unit, headless UI, screenshot, and performance gates
```

## macOS app bundle

`make app` produces `output/Hanabi.app` with the default bundle identifier
`io.github.gabeochoa.hanabi`. The four defaults live only in
`resources/macos/branding.json`; `make app APP_NAME=Ember
BUNDLE_ID=io.github.gabeochoa.ember EXECUTABLE_NAME=ember URL_SCHEME=ember`
builds an isolated alternate identity. It includes resources plus the OpenSSL
dylibs used by the TLS build, uses macOS Keychain roots for certificate
verification, and is ad-hoc signed so its local bundle seal and identifier can
be verified. It is not Developer ID signed or notarized.

Building does not install or register anything. `make install-app` copies the
verified app to `~/Applications/<app_name>.app` and registers it with
LaunchServices; `make uninstall-app` unregisters and removes only an app at that
path with the resolved display name and bundle identifier.
`APP_INSTALL_DIR=/path/Name.app` overrides the destination. `make register-app`
/ `make unregister-app` manage the build output directly for local verification.

The bundled app requests notification permission on its first windowed launch.
The developer executable and every headless screenshot/test path remain
non-bundled no-ops for notifications and Spotlight. See
[`docs/macos-bundle.md`](docs/macos-bundle.md) for diagnostics and verification.

## Before you push

There is no CI for this repo — the remote is a plain GitHub repo with no
workflows, so nothing runs the tests when a commit lands. The gate is local:

```bash
make gate          # tests + screenshot baselines; a few minutes
make install-hooks # optional: run the gate automatically on git push
```

`make gate` runs both halves even if the first one fails, so one run tells you
everything that is wrong. When a screen has drifted from its committed
baseline, the evidence is written to `test-failures/`: the baseline, the fresh
capture, a diff image with the changed pixels in red, and `summary.json`. See
`docs/screenshots/baselines/README.md` for what to do with it.

## Configuration (optional)

By default the app runs on the zero-config `mock` backend (offline sample data,
no network). To point it at a real REST backend you have two options — a **config
file** (easiest) or **environment variables**. If both are present, env vars
override the file.

### Option A — config file (recommended)

Copy the template and fill in your values:

```bash
mkdir -p ~/.config/hanabi
cp docs/config.example.json ~/.config/hanabi/config.json
$EDITOR ~/.config/hanabi/config.json    # set api_base_url + token
```

That's it — no environment variables needed. The app reads
`~/.config/hanabi/config.json` (or `$XDG_CONFIG_HOME/hanabi/config.json`, or a
path given by `$HANABI_CONFIG`) at startup. A real `config.json` is git-ignored
so a token can never be committed. Delete the file (or set `"backend": "mock"`)
to go back to offline sample data. See `docs/config.example.json` for every key.

### Option B — environment variables

Every config key also has an env var, which overrides the file:

| Variable | Meaning | Default |
|---|---|---|
| `HANABI_CONFIG` | path to a config JSON (overrides the default location) | *(auto)* |
| `HANABI_BACKEND` | `mock` or `http` | `mock` |
| `HANABI_API_BASE_URL` | base URL, e.g. `https://example.test/api` | *(none)* |
| `HANABI_TOKEN` | bearer token (never logged or stored) | *(none)* |
| `HANABI_SESSIONS_PATH` | path for the session list | `/sessions` |
| `HANABI_MESSAGES_PATH` | transcript path template (`{id}` placeholder) | `/sessions/{id}/messages` |
| `HANABI_CHAT_PATH` | POST path for kickoff/reply (`{id}` for reply) | *(opt-in)* |
| `HANABI_STREAM_PATH` | SSE path for a live streamed reply (`{id}`) | *(opt-in)* |
| `HANABI_AUTH_*` | device-code endpoints/fields (`DEVICE_PATH`, `TOKEN_PATH`, `CLIENT_ID`, `SCOPE`, …) | *(opt-in)* |
| `HANABI_FIELD_*` | JSON field-name overrides (`ID`, `TITLE`, `UPDATED_AT`, `STATUS`, `PREVIEW`, `MESSAGES`, `ROLE`, `TEXT`, `CREATED_AT`, `BLOCKS`, `BLOCK_TYPE`, `BLOCK_CONTENT`, `BLOCK_TEXT_TYPE`, `PROMPT`, `EVENT_*`) | generic names |

If `http` is selected but no base URL is set, the app cleanly falls back to the
`mock` backend.

### HTTPS backends need a TLS build

The default build has no networking dependencies and speaks plain `http://`
only. For a real `https://` backend, either use the one-shot **`make run`**
(auto-enables TLS when OpenSSL is installed) or build TLS explicitly:

```bash
make run               # builds (TLS auto-on if OpenSSL present) and launches
# or:
make HANABI_TLS=1      # explicit TLS build (needs: brew install openssl@3)
```

`make run` records the last build mode and only recompiles when it changes, so
repeat runs are fast. If OpenSSL isn't installed it still builds + launches, but
an https config then shows a clean error instead of connecting (or crashing).

Nothing about any endpoint or credential lives in this repository — real values
stay in your local (git-ignored) `config.json` or your shell.

## Layout

```
src/
├── api/          # backend-agnostic client interface + mock/http adapters
│   ├── client.h            # the Client seam (list/get/create/send/stream/auth)
│   ├── mock_client.h       # zero-config offline sample data (+ mock stream)
│   ├── http_client.*       # generic REST adapter (config-driven; SSE parser)
│   ├── auth.*              # device-code login state machine (pure, testable)
│   ├── token_store.*       # persist the acquired token (mode 0600, ignored)
│   └── config.*            # file + env config (nothing baked in)
├── ecs/          # entities, components, and per-frame systems
│   ├── loader_system.h     # async data loading + streamed-reply token drain
│   ├── layout_system.h     # panel geometry + sidebar collapse animation
│   ├── sidebar_system.h    # collapsible sidebar (smart views, folders, rows)
│   ├── tab_bar_system.h    # VS Code-style closable tabs + tab flow/restore
│   ├── main_pane_system.h  # smart-view digests + transcript + composer
│   ├── composer_system.h   # New-task sheet
│   ├── settings_system.h   # settings overlay (theme toggle)
│   ├── auth_system.h       # device-code login overlay
│   └── status_bar_system.h # bottom bar (backend + blocked count)
├── ui/           # theme (swappable light/dark tokens) + presets + icon atlas
├── util/         # small helpers (formatting, process)
├── menubar.*     # native NSStatusItem menu-bar extra (Obj-C++)
└── settings.*    # persisted window/tabs/theme/starred state
```

## License

MIT. Third-party libraries under `vendor/` retain their own licenses.
