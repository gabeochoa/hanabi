# Hanabi — TODO

A small, fast native desktop client for browsing conversation sessions.

## Product constraints
- [x] Version-controlled with a clean history.
- [x] Pushed to a new GitHub repo (gabeochoa/hanabi, private).
- [x] Backend-agnostic: the app never hardcodes a service. A generic, runtime-configured
      HTTP adapter sits behind the same interface as the default in-memory mock.
- [ ] Match the reference desktop look (light/dark themes, accent color, native window chrome,
      dotted-grid canvas, chat/code toggle, mascot).

## Requests / roadmap
- [x] Native (non-Electron) desktop client: C++23 + afterhours (ECS/UI) + Sokol (Metal on macOS).
- [x] Session list + transcript panes; mock backend default so it runs standalone.
- [x] Fix transcript layout bug (Column default FlexWrap::Wrap pushed body text to a side column).
- [ ] Design pass in HTML before C++ implementation.
- [ ] Sidebar smart-views: Home / Blocked / Ready to test / Starred (+ user folders).
- [ ] High-signal sidebar: attention only when a thread is done or waiting on the user.
- [ ] Feature: kick off a task from a system launcher and from a menu-bar icon (macOS).
- [ ] Light + dark theming via a single swappable token set.

## In progress
- HTML design mock (sidebar model + light/dark).

## Done
- Repo scaffold, mock + http adapters, ECS UI (session list / transcript / status bar).
- Layout fix + baseline.
