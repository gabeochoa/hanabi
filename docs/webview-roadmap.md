# hanabi — feature roadmap (from the web client)

Cataloged from the reference web client's UI source. Descriptions are
capability-level only — no endpoints/keys/schemas, no company references.
This is the running roadmap; MVP items are the near-term dogfood.

## MVP — browse sessions + view transcripts + kick off tasks
- Session/thread list — title, status dot (running/complete/error), model, timestamps; sub-sessions nested under parent.
- Open & read a transcript — full message history.
- Message roles + author avatars — distinct user vs assistant rows.
- Markdown rendering — rich markdown, auto-linking, internal links.
- Code block rendering — syntax highlight + copy button + language detection.
- Tool-call rendering — collapsible tool invocations, per-tool renderers, result blocks.
- Streaming responses — token-by-token with in-progress state.
- Activity indicator — subtle "working" light while the agent runs.
- Task kickoff / compose — start a new task or reply; send + new-chat.
- Model selector — model + reasoning effort at kickoff.
- Node/target selector — lock a task to a specific machine/node.
- New chat / suggested starters — welcome screen + clickable prompts.
- Inline image/media rendering — inline + generated-image outputs.

## NEAR — soon after MVP
- Session search + cross-session search — ranked results, snippet preview, match highlighting.
- Pin / unpin sessions.
- Archive / restore sessions.
- Rename session + auto-title.
- Workspaces / folder organization — group sessions; new chat scoped to one.
- Steering a running agent — queue/send follow-ups mid-run; cancel queued.
- Diff rendering — rendered code diffs from tool output.
- Charts / rich inline artifacts — inline component/artifact preview.
- Attachments — files/images/pasted text on a message.
- Artifact dock / sidebar — pinned artifacts, PR/link previews, dock<->sidebar toggle.
- Sub-agent visibility — status panel + jump into sub-agent sessions.
- Context-usage indicator.
- Connected-nodes view — active nodes + online/offline, connect prompt.
- Theme + notification-sound + verbosity preferences.
- Keyboard shortcuts — search, toggle sidebar, rename, new chat, scroll, jump-to-N.
- Share a session — read-only link, toggle sharing.
- File browser / workspace files — browse/preview/download/share produced files.

## LATER — advanced / nice-to-have
- Kanban / board view (command center) — cards across lanes; approve/reject/retry/priority/move.
- Status / uptime dashboard + digests.
- Schedules / crons management — at/every/cron jobs + run history.
- Lifecycle hooks.
- Slash commands — in-composer menu + custom command management.
- Sub-agent profiles.
- Skills marketplace — browse/install/enable.
- Webhooks.
- Memory graph — interactive entity+memory explorer.
- Message reactions.
- "Send in fork" — branch a session into a new forked session.
- Session replay / trajectory viewer.
- Autoresearch runs dashboard.
- Asset library.
- Guided onboarding / install wizard.
- Bug reporting / feedback capture.
- Integration/connector settings panels.

## Desktop-native-advantaged (native app does these better than web)
- Menu-bar / status-item presence — glanceable running count + attention badge.  [planned — Phase 4]
- Global hotkey — system-wide quick-kickoff from any app.  [planned — Phase 4]
- Native OS notifications — run-complete / needs-review / steering-requested, click-to-open, DND-aware.
- Local caching / offline reading — instant browse of recent sessions offline.
- Background sync + Dock badge — unread/attention counts on the Dock icon.
- Multi-window — several transcripts as separate native windows.
- Native file handoff — drag to Finder, Reveal in Finder, Quick Look.
- Deep links / URL scheme — hanabi://session/... handoff from other apps + notifications.

## Top 5 to add after the current MVP
1. Session search + cross-session search (snippet highlighting).
2. Pin + archive + rename/auto-title (organization primitives).
3. Steering a running agent (queue/cancel follow-ups).
4. Native OS notifications on run-complete / needs-attention (biggest native-only win; pairs with menu-bar/Dock badge + background sync).
5. Workspaces (folder organization).
