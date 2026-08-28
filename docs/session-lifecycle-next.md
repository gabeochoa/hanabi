# Session lifecycle next

## Verified product outcomes

- `/btw <question>` uses Agentcloud's pre-attach `fork_with_prompt` command with the legacy text-only `prompt` field, required title, and `source_session_id`. Hanabi checks `fork_with_prompt_v1` on the source session before sending. It never falls through to a normal parent input.
- Bare **Fork session** uses the existing pre-attach `fork` command with no guessed endpoint or invented verb.
- The mock preserves the source transcript, creates an independent destination, and exposes fork lineage. The UI opens the destination as a kept tab.
- The sidebar sub-agent toggle is persisted. Its data request runs only while open, the adapter caps the response at 2,000 child sessions, and rows open the child transcript.
- Muted rows retain the quiet crossed-ring affordance and click-to-unmute behavior. The native notification decision now has a single tested gate that returns no event for muted transitions.
- **Close all tabs** closes pinned and unpinned tabs, clears both panes, closes split view, and persists the empty tab set through the existing persistence path.
- Star, archive, and mute mutations update every open pane copy of the session summary. Rename already used the same all-pane rule.

## Server limitations

1. Forking is supported only by the Agentcloud adapter and the offline mock. The generic configurable HTTP adapter has no documented fork endpoint and remains unsupported.
2. `/btw` is text-only in Hanabi. The real protocol supports attachment-bearing `input` only when `fork_with_prompt_input_v1` is advertised; Hanabi has no uploaded-file handle path, so it intentionally sends the backward-compatible `prompt` shape.
3. Agentcloud exposes fork lineage on summaries, but Hanabi does not yet render a return-to-source affordance or lineage grouping.
4. Archive, star, and mute remain machine-local overlays because this client has no reachable per-viewer overlay write route. Rename and fork are server-durable.
5. Delete remains unavailable because the server exposes no delete-session verb.
6. The Agentcloud adapter does not maintain a live attached subscription for a newly forked destination; the initial view comes from the normal transcript fetch and later updates follow the existing refresh behavior.

## Cost and bounds

- Closed sub-agent panel: no catalog request and no child scan or child-row build; the persistent header toggle adds six steady-state allocations per frame (`home20` 811 → 817).
- Open sub-agent panel: one bounded list request, at most 2,000 retained child summaries, and 29 of 410 matching child rows built in the 2,000-session stress fixture.
- Fork: one source capability attach/page and one control-lane fork request. Bare fork uses one control-lane request.
- Close all: one pass over open tabs and two pane resets.

## Evidence

- Wire and loopback server: `tests/unit/test_agentcloud.cpp`, `tests/e2e/test_agentcloud_local.cpp`, `tests/harness/agentcloud_local_server.py`
- Mock fork/source isolation: `tests/unit/test_data.cpp`
- Settings relaunch: `tests/unit/test_settings.cpp`
- Native notification suppression: `tests/unit/test_notify_events.cpp`
- Two-pane lifecycle, child-session hot-set admission, mute independence from lazy subscriptions, and close-all: `tests/e2e/test_e2e.cpp`
- Live UI flows and screenshots: `tests/ui/session_btw_fork.e2e`, `tests/ui/session_fork_menu.e2e`, `tests/ui/sidebar_subagents.e2e`, `tests/ui/tab_close_all.e2e`
