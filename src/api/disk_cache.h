#pragma once

// On-disk cache for the session list and transcripts (stale-while-revalidate).
//
// WHY: on a slow / flaky network every launch and every uncached thread-open
// would otherwise block on the network showing "Loading…". This cache persists
// the last-known session list and the transcripts the user has opened to
// ~/.config/hanabi/cache/ as JSON, so a relaunch can paint the list + a recent
// transcript INSTANTLY from disk, then refresh from the network in the
// background and swap in fresh data when it arrives. The network is never on
// the UI's critical path for a repeat view.
//
// Scope: read/write JSON only — no graphics, no threading, no network. The
// loader owns the policy (when to read/write, background revalidate); this is
// just durable storage behind a tiny API. Safe to unit-test headlessly.
//
// Privacy: the cache lives under the same per-user config dir as settings and
// is NOT git-tracked. It holds the same session content the app already shows;
// it never stores tokens or credentials.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace api::disk_cache {

// Scope the cache to a particular backend so two different backends (e.g. two
// real servers with different base URLs) never read each other's stale data —
// the exact class of bug where switching servers shows the wrong server's
// sessions. Pass a stable key (the http adapter passes its base_url); the cache
// files then live under …/hanabi/cache/<sanitized-key-hash>/. Call ONCE at
// startup before any load/save. An empty key (the default) keeps the flat
// …/hanabi/cache/ layout. Idempotent.
void set_namespace(const std::string& key);

// Directory that holds the cache files (…/hanabi/cache[/<ns>]). Created on
// demand. Empty if no HOME/XDG config dir is resolvable (then all ops no-op).
std::string cache_dir();

// --- Session list -------------------------------------------------------
// Persist / load the session list (the sidebar + Home digest source). Best
// effort: a write failure is silently ignored (the cache is an optimization,
// never a correctness dependency); a missing/corrupt file loads as nullopt.
void save_sessions(const std::vector<SessionSummary>& sessions);
std::optional<std::vector<SessionSummary>> load_sessions();

// --- Transcripts (one file per session id) ------------------------------
void save_transcript(const Session& session);
std::optional<Session> load_transcript(const std::string& id);

// --- Introspection / maintenance (for the settings screen) --------------
// total_bytes(): sum of the byte sizes of every cache file under cache_dir()
// (sessions.json + every tx_*.json in the ACTIVE namespace). Cheap stat walk,
// no parsing. 0 when the dir doesn't exist. The settings UI shows this so the
// user can see how much disk the on-disk transcript cache is using (feature #2).
std::uint64_t total_bytes();

// wipe_all(): delete every cache file in the ACTIVE namespace (sessions.json +
// tx_*.json), so the settings "clear cache" button can reclaim disk + force a
// cold refetch. Best-effort; returns the number of files removed. Does NOT
// touch other namespaces or non-cache files.
std::size_t wipe_all();

// --- Crash-safe draft / queue persistence (local-first) -----------------
// WHY: while the user is drafting a prompt (or has queued unsent prompts) a
// crash/restart would otherwise lose that in-progress work. These persist the
// composer draft text and any queued-but-unsent prompts to a single small JSON
// file under the ACTIVE namespaced cache dir (drafts.json), so a relaunch can
// restore exactly what was being typed. Everything is LOCAL-first: it lives on
// disk beside the transcript cache and NEVER touches the network.
//
// Keying: pass a session id, or the stable key "new" for the not-yet-created
// "New task" composer draft (which has no session id yet). Each key's draft +
// queue are stored independently so per-session drafts don't collide.
//
// Cost: drafts are small; these are tiny synchronous JSON read/writes (the
// whole drafts.json is rewritten atomically on each change — a few hundred
// bytes typical). The composer calls save_draft on each text change; that is
// acceptable at this size (no debounce needed) and mirrors the atomic temp+
// rename write the rest of this cache uses, so a crash mid-write never corrupts
// the file. Best-effort: a write failure is silently ignored; a missing/corrupt
// file loads as an empty string / empty vector.

// The stable key for the "New task" composer draft (no session id yet).
inline const char* new_draft_key() { return "new"; }

// Persist / restore the composer draft TEXT for `key`. Saving an empty string
// is equivalent to clear_draft(key) (an empty draft is nothing to preserve).
void save_draft(const std::string& key, const std::string& text);
std::string load_draft(const std::string& key);

// Persist / restore the queued-but-unsent prompts for `key` (FIFO order).
// Saving an empty vector clears the persisted queue for that key.
void save_queue(const std::string& key, const std::vector<std::string>& prompts);
std::vector<std::string> load_queue(const std::string& key);

// Drop the persisted draft AND queue for `key` — call once a draft is
// sent/cleared so its saved copy doesn't linger. No-op if nothing was stored.
void clear_draft(const std::string& key);

// --- Local-first OUTBOX (crash-safe sent-message log) -------------------
// A message is written to the outbox BEFORE it goes to the network, so a crash
// mid-send never loses the user's words; it's removed once the server confirms
// (Synced). A Failed send stays in the outbox to retry. Backed by the same
// per-key store as the queue (survives restart). outbox_add appends `prompt`
// under session `id`; outbox_remove drops the first matching `prompt`;
// outbox_list returns the still-unconfirmed prompts for `id` (FIFO).
void outbox_add(const std::string& id, const std::string& prompt);
void outbox_remove(const std::string& id, const std::string& prompt);
std::vector<std::string> outbox_list(const std::string& id);

// --- Cache cap / eviction (feature #C) ----------------------------------
// touch_transcript(id): bump the on-disk transcript file's modified-time to
// "now" WITHOUT rewriting it. This is how a thread's LAST-OPENED time is
// tracked for LRU eviction: the loader should call this whenever the user
// opens a thread (a save already sets a fresh mtime, so an unopened-but-saved
// thread and a just-opened one are distinguished only if open touches it).
// No-op if the file doesn't exist. Cheap (a single utime()-style call).
//
// NOTE FOR THE PARENT AGENT: wiring this into "on thread open" lives in
// loader_system.h, which THIS agent does not own. Until it's wired, eviction
// still works correctly but orders purely by file mtime (= last SAVE time),
// which is a reasonable recency proxy; opening a thread just won't refresh its
// recency. See REPORT.
void touch_transcript(const std::string& id);

// trim_to_cap(cap_bytes, keep_tail): if the ACTIVE namespace's total_bytes()
// exceeds cap_bytes, evict oldest transcript data until back under the cap.
// cap_bytes == 0 means "unlimited" → a no-op. Eviction policy (oldest-first):
//   1. ARCHIVED threads are evicted before non-archived ones (they're unlikely
//      to be reopened soon), each group ordered least-recently-opened first
//      (by file mtime — see touch_transcript).
//   2. Eviction TRIMS a transcript to its newest `keep_tail` messages (tail of
//      the cached transcript) and rewrites it smaller, rather than deleting it
//      outright — a relaunch can still paint the last few messages instantly.
//      A transcript already at/under keep_tail messages is deleted entirely
//      (nothing left to trim; its whole file is the eviction unit).
//   3. sessions.json is never evicted (it's tiny and the list is the app's
//      spine); only tx_*.json transcripts are trimmed/removed.
// Runs after a transcript save (the point the cache grows). Best-effort; any
// per-file error is skipped. Returns the number of BYTES reclaimed (approx,
// pre/post total_bytes delta). Safe to call with cap_bytes==0 (no-op).
std::uint64_t trim_to_cap(std::uint64_t cap_bytes, std::size_t keep_tail = 10);

}  // namespace api::disk_cache
