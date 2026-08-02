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

}  // namespace api::disk_cache
