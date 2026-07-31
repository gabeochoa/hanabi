# hanabi

A small, fast **native desktop client** for browsing conversation sessions.
A single collapsible sidebar (smart views + folders) on the left, VS Code-style
tabs across the top, and the full transcript in the main pane.

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
- **High-signal rows.** A row shows an attention dot + bold title **only** when
  it's done or waiting on you. Self-running threads are dimmed and calm (no dot,
  no bold); parked and archived threads are greyed. At most one tag chip per row
  (Blocked / Review / Done). The status bar shows the blocked-on-you count.
- **Smart views.** Home (a digest ordered waiting-on-you → finished →
  self-running), Blocked, Review (agent-verified / ready to look at), and
  Starred — each swaps the main pane. Plus user folders + Archived.
- **Light / dark theming.** All colors flow from a single swappable token set
  (`src/ui/theme.h`), switchable at runtime and persisted. Dark by default.

## Design

Hanabi talks to a data source through a small, backend-agnostic `Client`
interface (`src/api/client.h`). Two implementations ship:

- **`mock`** *(default)* — deterministic, in-memory sample data (a spread of
  thread states so smart views / folders / high-signal rows all have real
  content to render). The app builds and runs standalone with **no
  configuration and no network access**.
- **`http`** — a generic REST adapter. It has **no service baked in**: the base
  URL, auth token, request paths, and even the JSON field names it reads are
  **all supplied at runtime** via environment variables. Nothing about any
  particular backend is compiled into this repository. The high-signal
  attention model degrades to a calm/unknown state when the backend doesn't
  supply one.

This keeps the app useful out of the box and lets you point it at whatever
backend you like without touching the code.

## Build

Requires a C++23 compiler (clang recommended) and, on macOS, the Metal
frameworks (installed with the Xcode command-line tools).

```bash
git clone --recurse-submodules <this-repo>
cd hanabi
make            # build ./output/hanabi
make run        # build + launch (mock backend)
make bundle     # build a macOS Hanabi.app
make test       # run unit tests
```

## Configuration (optional)

By default the app runs on the `mock` backend. To point it at a real REST
backend, set environment variables — none of these have committed defaults:

| Variable | Meaning | Default |
|---|---|---|
| `HANABI_BACKEND` | `mock` or `http` | `mock` |
| `HANABI_BASE_URL` | base URL, e.g. `https://example.test/api` | *(none)* |
| `HANABI_TOKEN` | bearer token (never logged or stored) | *(none)* |
| `HANABI_SESSIONS_PATH` | path for the session list | `/sessions` |
| `HANABI_MESSAGES_PATH` | transcript path template (`{id}` placeholder) | `/sessions/{id}/messages` |
| `HANABI_FIELD_*` | JSON field-name overrides (`ID`, `TITLE`, `UPDATED_AT`, `STATUS`, `PREVIEW`, `MESSAGES`, `ROLE`, `TEXT`, `CREATED_AT`) | generic names |

If `http` is selected but `HANABI_BASE_URL` is unset, the app cleanly falls
back to the `mock` backend.

```bash
HANABI_BACKEND=http \
HANABI_BASE_URL=https://example.test/api \
HANABI_TOKEN=… \
  ./output/hanabi
```

Configuration is read from the environment only — no endpoints or credentials
live in this repository. Keep any real values in your shell or a local `.env`
that is git-ignored.

## Layout

```
src/
├── api/          # backend-agnostic client interface + mock/http adapters
├── ecs/          # entities, components, and per-frame systems
│   ├── loader_system.h     # async data loading
│   ├── layout_system.h     # panel geometry + sidebar collapse animation
│   ├── sidebar_system.h    # collapsible sidebar (smart views, folders, rows)
│   ├── tab_bar_system.h    # VS Code-style closable tabs + tab flow/restore
│   ├── main_pane_system.h  # smart-view digests + transcript
│   └── status_bar_system.h # bottom bar (backend + blocked count)
├── ui/           # theme (swappable light/dark tokens) + design-system presets
└── util/         # small helpers (formatting, process)
```

## License

MIT. Third-party libraries under `vendor/` retain their own licenses.
