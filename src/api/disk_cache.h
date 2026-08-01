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

#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace api::disk_cache {

// Directory that holds the cache files (…/hanabi/cache). Created on demand.
// Empty if no HOME/XDG config dir is resolvable (then all ops are no-ops).
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

}  // namespace api::disk_cache
