# hanabi

A small, fast **native desktop client** for browsing conversation sessions.
It loads your threads locally and renders them in a clean, minimal interface —
a session list on the left, the full transcript on the right.

Built with **C++23**, the [afterhours](https://github.com/gabeochoa/afterhours)
ECS + immediate-mode UI framework, and **Sokol** (Metal on macOS) for rendering.

<!-- screenshot goes here -->

## Design

Hanabi talks to a data source through a small, backend-agnostic `Client`
interface (`src/api/client.h`). Two implementations ship:

- **`mock`** *(default)* — deterministic, in-memory sample data. The app builds
  and runs standalone with **no configuration and no network access**.
- **`http`** — a generic REST adapter. It has **no service baked in**: the base
  URL, auth token, request paths, and even the JSON field names it reads are
  **all supplied at runtime** via environment variables. Nothing about any
  particular backend is compiled into this repository.

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
│   ├── loader_system.h        # async data loading
│   ├── layout_system.h        # panel geometry
│   ├── session_list_system.h  # left sidebar
│   ├── transcript_system.h    # right transcript pane
│   └── status_bar_system.h    # bottom bar
├── ui/           # theme + design-system presets
└── util/         # small helpers (formatting, process)
```

## License

MIT. Third-party libraries under `vendor/` retain their own licenses.
