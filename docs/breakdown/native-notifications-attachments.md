# Hanabi Feature Breakdown: Notifications, Native Integration, Attachments

**Verification result:** Of 11 claimed gaps, **5 already implemented**, **6 real gaps** remain.

## Gaps Verified

### Already Built (don't build these again):
1. **Global hotkey (Cmd+Shift+N)** — fully wired in `native_extras.mm`, focus-gated via NSApp notifications, brings hanabi to front + starts new task (line 41–207).
2. **Native notifications** — `native_notify()` posts macOS NSUserNotification with thread_id deep-link; click opens thread via `native_take_open_thread()` (line 210–290).
3. **Spotlight indexing seam** — `native_spotlight_index()` is wired (line 292–362); NO-OP in bare dev binary (no bundle ID), but ready for the bundled app build.
4. **Spotlight deep-link handler** — `native_openurl_install()` + `native_take_open_thread()` capture `hanabi://thread/<id>` URLs (line 364–459).
5. **Menu-bar extra** — `menubar.mm` shows "N blocked on you" glyph + count, with Show / New Task / Quit actions (line 1–162).

### Real Gaps (6 total):

#### NOTIFICATIONS & BACKGROUND (2 gaps)

**Gap 1: Expanded notification types**
- **What ships:** Notifications beyond "blocked count increased." Users receive alerts for: run finished, approval needed, user input requested, async child completed. Each type has its own title/body format. Mute-gating per-session.
- **UX flow:** When a backend frame signals a significant event (run_finished, approval_blocked, input_requested), the app issues a `native_notify()` call with event-specific text. Clicking opens the thread. Muted sessions skip the notify call entirely. Notification settings UI (Settings → Notifications) allows enabling/disabling per-type.
- **Where it goes:** `main.cpp` event dispatch (currently only watches `blocked_delta > 0`; expand to other frame types). Mute state per session in a new `session_mute_state` field or UserDefaults. Settings UI new toggles in a Notifications pane.
- **Dependencies:** `native_notify()` API already exists; frame types (run_finished, approval_blocked, input_requested, etc.) must be defined in the data model (verify with backend wire format).
- **How verified:** Manual — send message, watch for notification. No e2e script (system notifications not in widget tree).
- **Dev-build status:** Works in dev executable + bundled app.
- **Standalone and reviewable:** Yes; independently ships per-type notification logic.
- **Estimated scope:** ~80 lines (event detection + notification text formatting + mute gate).

**Gap 2: Quiet hours**
- **What ships:** Settings → Notifications pane with "Quiet hours" time-range picker (e.g., 10 PM – 8 AM). Outside quiet hours, notifications fire; during quiet hours, they are suppressed (no banner, no sound, no notification center delivery).
- **UX flow:** User opens Settings, clicks Notifications tab, sees "Quiet Hours" section. Toggles "Enable quiet hours" on. Two time pickers: start + end time. Save persists to UserDefaults. On any `native_notify()` call, `main.cpp` checks if current time is within the quiet window; if yes, skips the call.
- **Where it goes:** Settings UI (new pane tab, ~40 lines). Main loop notify gate (~10 lines).
- **Dependencies:** None; relies on existing `native_notify()`.
- **How verified:** Manual — set quiet hours, send message, confirm no notification appears.
- **Dev-build status:** Works in dev executable + bundled app.
- **Standalone and reviewable:** Yes; independent of Gap 1.
- **Estimated scope:** ~50 lines total.

#### NATIVE macOS INTEGRATION (1 gap)

**Gap 3: System menu integration (File / Edit / View menus)**
- **What ships:** Standard macOS menu bar (File, Edit, View, Window, Help). Keyboard shortcuts shown inline. Menu items include: File → New Conversation (Cmd+N), Edit → Find (Cmd+F), View → Toggle Sidebar (Cmd+B), Window → Minimize (Cmd+M), Help → About / Hanabi Help. Context menus on right-click in sidebar (Rename, Archive, etc.).
- **UX flow:** Right-click a session in sidebar → see "Rename", "Archive", "Pin", "Mute" options. Click File menu → "New Conversation" opens a new session tab. Keyboard shortcuts shown next to menu items.
- **Where it goes:** Platform layer (Mac-only, Obj-C++ in a new or extended `native_*.mm` file). Menu construction on app init, actions route through C function pointers or atomic flags (like menubar.mm does).
- **Dependencies:** None; uses AppKit NSMenu directly.
- **How verified:** Manual — open menu bar, verify items + keyboard shortcuts appear and work.
- **Dev-build status:** Works in dev + bundled (standard AppKit, no bundle ID required).
- **Standalone and reviewable:** Yes; independent feature.
- **Estimated scope:** ~90 lines (menu structure + item handlers).

#### ATTACHMENTS (3 gaps)

**Gap 4: Image paste/drop in composer**
- **What ships:** User pastes (Cmd+V) or drag-drops an image into the composer. Image appears as a chip: small thumbnail + remove button. Multiple images per message supported. On send, images are base64-encoded + sent in the message (wire format: `attachments[]` array with type=image, data=base64, filename). Failed send: attachments remain in composer for retry.
- **UX flow:** User drags a PNG/JPG/WebP from Finder onto the composer area. A chip appears with a thumbnail and an X button. User can add more images or type text. Send button encodes them and submits. If send fails, chips stay (retry without retyping).
- **Where it goes:** Composer UI (`main_pane_system.h` or new `composer_attachments_system.h`). Drop delegate (macOS + afterhours gesture). Encoding on send (convert image path to base64 before wire call).
- **Dependencies:** The wire protocol must support `attachments` array in the send message. **Backend status unknown — verify if server accepts attachment fields in send_message call.** If unsupported, mark BLOCKED.
- **How verified:** Unit test on base64 encoding. Manual drop + send, inspect wire payload.
- **Dev-build status:** Works in dev + bundled (native drop handling available).
- **Standalone and reviewable:** Yes; independent unless blocked by backend.
- **Estimated scope:** ~110 lines (drop delegate + thumbnail rendering + base64 encoding + send gate).

**Gap 5: File picker for file_upload tool**
- **What ships:** When the `file_upload` workspace tool is invoked by the agent, the app shows a native file picker (macOS NSSavePanel or NSOpenPanel). User selects a file. App uploads it via the tool's wire interface. A link/status appears in the transcript.
- **UX flow:** Agent runs the `file_upload` tool. UI shows "Choose a file..." button or auto-opens picker. User selects a file. File is uploaded (with progress bar if large). Transcript shows "File uploaded: foo.csv" with a link.
- **Where it goes:** Tool-specific rendering in the transcript system (`main_pane_system.h` or `transcript_system.h`). Native file picker (macOS-only, `native_file_picker()` in `native_*.mm` behind a C seam). Upload handler in data/wire layer.
- **Dependencies:** The `file_upload` tool definition must be available in the session's tools list. Wire call for upload must be defined. **Backend status unknown — verify tool exists + upload endpoint exists.** If unsupported, mark BLOCKED.
- **How verified:** Manual — agent invokes tool, pick file, confirm upload succeeds + link appears.
- **Dev-build status:** Works in dev + bundled (NSSavePanel available everywhere).
- **Standalone and reviewable:** Yes; independent unless blocked by backend.
- **Estimated scope:** ~100 lines (file picker + upload wire call + transcript rendering).

**Gap 6: Diff rendering for edit tool**
- **What ships:** When an `edit` tool modifies a file in the transcript, the UI shows a diff view: old content (faded or strikethrough), new content (highlighted). File path and "Replace all" indicator at top. Collapsible (click to expand/collapse diff).
- **UX flow:** Agent uses the `edit` tool to change a file. Transcript shows a collapsible "Edited: src/foo.py" row. User clicks to expand and sees a side-by-side or inline diff.
- **Where it goes:** Tool-specific row rendering in `main_pane_system.h`. Diff parsing + rendering logic (compare old vs new, highlight changes).
- **Dependencies:** Tool output must include both old and new content (or the tool result frame must carry both). **Wire format unknown — verify edit tool response includes full old/new content.** If only partial content is sent, mark BLOCKED or note the limitation.
- **How verified:** Unit test on diff parsing. Manual — run edit tool, expand diff, verify highlighting.
- **Dev-build status:** Works in dev + bundled.
- **Standalone and reviewable:** Yes; independent unless blocked by wire format.
- **Estimated scope:** ~120 lines (diff parser + side-by-side layout + collapse state).

---

## Implementation Order (Ship First)

1. **Gap 1: Expanded notification types** (~80 lines, 4 hours)
   - Unblocks user awareness of key events.
   - No backend unknowns if frame types exist.
   - Pairs well with Gap 2 (quiet hours).

2. **Gap 3: System menus** (~90 lines, 4 hours)
   - Table-stakes macOS UI polish.
   - No external dependencies.
   - Independent of other gaps.

3. **Gap 4: Image paste/drop** (~110 lines, 5 hours)
   - Unblocks image sharing (high user request).
   - Verify backend accepts `attachments` first; if yes, proceed.
   - High UX impact for daily use.

---

## Backend Blockers to Resolve First

- **Attachments wire format:** Confirm `send_message` call accepts `attachments[]` array; schema for type/data/filename fields.
- **file_upload tool:** Verify tool exists in session tools; confirm upload endpoint + request/response format.
- **edit tool:** Verify tool response includes old + new content (not just result); diff format (if any) in the wire.

---

## Testing Notes

**Honest testing limitations:**
- **Notifications, quiet hours, menus:** No e2e script. Require manual verification (look at banner, open menu, verify items/shortcuts work). No widget-tree equivalent.
- **Image paste/drop:** Drop gesture not in the widget tree; unit test the encoding, manual test the drop.
- **File picker, diff rendering:** Manual verification (no e2e script for native pickers; diff rendering can be unit-tested + manually spot-checked).

**All verified by:** Manual inspection of the running app, not scripted gates.

---

## Summary Table

| Gap | Type | Status | Scope | Blocker | Verify How |
|-----|------|--------|-------|---------|-----------|
| 1 | Notifications (expanded) | Real | 80 L | Frame types in wire | Manual |
| 2 | Quiet hours | Real | 50 L | None | Manual |
| 3 | System menus | Real | 90 L | None | Manual |
| 4 | Image paste/drop | Real | 110 L | Backend attachment schema | Unit + Manual |
| 5 | File picker (upload) | Real | 100 L | Tool + endpoint in backend | Manual |
| 6 | Diff rendering (edit) | Real | 120 L | Tool wire includes old+new | Unit + Manual |

**5 gaps already implemented** in the codebase (hotkey, notifications core, Spotlight seam, deep-link, menu-bar).
