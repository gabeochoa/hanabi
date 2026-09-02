#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/sidebar_buckets.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::SessionSummary;
using Members = std::vector<const SessionSummary*>;

static SessionSummary sess(std::string id, std::string title,
                           std::string folder, bool archived = false) {
    SessionSummary s;
    s.id = std::move(id);
    s.title = std::move(title);
    s.folder = std::move(folder);
    if (archived) s.archive_override = true;
    return s;
}

// The scan render_folder ran once per folder, restated. This is the reference
// the one-pass collection must agree with; it is a copy of the rules on
// purpose, because a shared implementation could not catch the two drifting.
template <class ContentMatch>
static Members reference_scan(const std::vector<SessionSummary>& sessions,
                              const std::string& key, bool archivedArm,
                              bool catchAll, const std::string& q,
                              bool hideAutomated, ContentMatch&& contentMatch) {
    Members out;
    for (const auto& s : sessions) {
        bool match;
        if (archivedArm) {
            match = ecs::model::is_archived(s);
        } else if (catchAll) {
            match = !ecs::model::is_archived(s) &&
                    (s.folder == key || !ecs::model::is_named_folder(s.folder));
        } else {
            match = (s.folder == key && !ecs::model::is_archived(s));
        }
        if (match && hideAutomated && ecs::model::is_automated_title(s.title))
            continue;
        if (match && (ecs::model::title_matches(s.title, q) ||
                      (!q.empty() && contentMatch(s.id, q))))
            out.push_back(&s);
    }
    return out;
}

static std::vector<std::string> reference_folders(
    const std::vector<SessionSummary>& sessions) {
    std::vector<std::string> out;
    for (const auto& s : sessions) {
        if (ecs::model::is_archived(s) ||
            !ecs::model::is_named_folder(s.folder))
            continue;
        bool seen = false;
        for (const auto& f : out)
            if (f == s.folder) seen = true;
        if (!seen) out.push_back(s.folder);
    }
    return out;
}

static bool same(const Members& a, const Members& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static std::vector<SessionSummary> corpus(int variant) {
    std::vector<SessionSummary> v;
    v.push_back(sess("a1", "Alpha review", "/work/subs"));
    v.push_back(sess("a2", "Schedule: nightly sweep", "/work/subs"));
    v.push_back(sess("a3", "beta latency", ""));
    v.push_back(sess("a4", "gamma-tick", ""));
    v.push_back(sess("a5", "Delta archived", "/work/subs", /*archived=*/true));
    v.push_back(sess("a6", "epsilon", "recent"));
    v.push_back(sess("a7", "Zeta monetization", "/work/money"));
    v.push_back(sess("a8", "eta", "/work/money", /*archived=*/true));
    v.push_back(sess("a9", "Theta ALPHA case", ""));
    v.push_back(sess("b0", "", ""));
    // The folder whose only live member is automated. Discovery runs before
    // the hide-automated filter, so this folder must be LISTED with an empty
    // bucket whenever the filter is on -- an implementation that discovered
    // below the filter would drop a cron-only folder out of the sidebar.
    v.push_back(sess("cr", "Schedule: rollup", "/work/cron"));
    if (variant & 1) v.push_back(sess("c1", "Iota", "/work/subs"));
    if (variant & 2) v.push_back(sess("c2", "Schedule: kappa", ""));
    if (variant & 4)
        v.push_back(sess("c3", "Lambda", "/work/third", /*archived=*/true));
    return v;
}

int main() {
    const auto noContent = [](const std::string&, const std::string&) {
        return false;
    };
    // A content probe that matches one id, so the query path's second clause
    // is exercised rather than assumed dead.
    const auto contentHit = [](const std::string& id, const std::string&) {
        return id == "a3";
    };

    const std::vector<std::string> queries{"", "alpha", "z", "nothingmatches",
                                           "schedule"};

    // ---- differential: one pass == the old per-folder scan ---------------
    for (int variant = 0; variant < 8; ++variant) {
        const std::vector<SessionSummary> sessions = corpus(variant);
        for (const std::string& q : queries) {
            for (int hide = 0; hide < 2; ++hide) {
                for (int useContent = 0; useContent < 2; ++useContent) {
                    ecs::model::SidebarBuckets buckets;
                    // A fresh revision every combination: the memo must not
                    // answer for a different query or flag.
                    const std::uint64_t rev =
                        1 + static_cast<std::uint64_t>(variant);
                    if (useContent)
                        buckets.rebuild(rev, sessions, q, hide != 0,
                                        contentHit);
                    else
                        buckets.rebuild(rev, sessions, q, hide != 0, noContent);

                    CHECK(buckets.folders() == reference_folders(sessions));

                    for (const std::string& key : buckets.folders()) {
                        Members want;
                        if (useContent)
                            want = reference_scan(sessions, key, false, false,
                                                  q, hide != 0, contentHit);
                        else
                            want = reference_scan(sessions, key, false, false,
                                                  q, hide != 0, noContent);
                        CHECK(same(buckets.members(key), want));
                    }
                    Members wantRecent;
                    if (useContent)
                        wantRecent = reference_scan(sessions, "recent", false,
                                                    true, q, hide != 0,
                                                    contentHit);
                    else
                        wantRecent = reference_scan(sessions, "recent", false,
                                                    true, q, hide != 0,
                                                    noContent);
                    CHECK(same(buckets.recent(), wantRecent));
                }
            }
        }
    }

    // A session belongs to exactly one place: its named folder, never Recent.
    {
        const std::vector<SessionSummary> sessions = corpus(0);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, sessions, "", false, noContent);
        for (const auto* s : buckets.recent())
            CHECK(!ecs::model::is_named_folder(s->folder));
        size_t filed = buckets.recent().size();
        for (const auto& key : buckets.folders())
            filed += buckets.members(key).size();
        size_t live = 0;
        for (const auto& s : sessions)
            if (!ecs::model::is_archived(s)) ++live;
        CHECK(filed == live);
    }

    // Discovery is NOT filtered by hide-automated either, and that half has
    // its own case because the query half above cannot see it.
    {
        const std::vector<SessionSummary> sessions = corpus(0);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, sessions, "", true, noContent);
        bool listed = false;
        for (const auto& f : buckets.folders())
            if (f == "/work/cron") listed = true;
        CHECK(listed);
        CHECK(buckets.members("/work/cron").empty());
        CHECK(buckets.folders() == reference_folders(sessions));
    }

    // Discovery is NOT filtered: a folder whose every member fails the query
    // is still listed, and renders nothing because its bucket is empty.
    {
        const std::vector<SessionSummary> sessions = corpus(0);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, sessions, "nothingmatches", false, noContent);
        CHECK(buckets.folders() == reference_folders(sessions));
        CHECK(!buckets.folders().empty());
        for (const auto& key : buckets.folders())
            CHECK(buckets.members(key).empty());
        CHECK(buckets.recent().empty());
    }

    // ---- the revision contract -------------------------------------------
    {
        std::vector<SessionSummary> sessions = corpus(0);
        ecs::model::SidebarBuckets buckets;
        CHECK(buckets.rebuild(7, sessions, "", false, noContent));
        CHECK(buckets.rebuilds() == 1);
        const Members firstRecent = buckets.recent();

        // Same revision, same query, same flag: the answer is kept.
        CHECK(!buckets.rebuild(7, sessions, "", false, noContent));
        CHECK(buckets.rebuilds() == 1);
        CHECK(same(buckets.recent(), firstRecent));

        // A new revision rebuilds.
        CHECK(buckets.rebuild(8, sessions, "", false, noContent));
        CHECK(buckets.rebuilds() == 2);

        // The hide-automated flag is part of the key.
        CHECK(buckets.rebuild(8, sessions, "", true, noContent));
        CHECK(buckets.rebuilds() == 3);
        CHECK(!buckets.rebuild(8, sessions, "", true, noContent));
        CHECK(buckets.rebuilds() == 3);

        // A query ALWAYS rebuilds: the disk-cache content probe has no
        // revision, so a kept answer would go stale invisibly.
        CHECK(buckets.rebuild(8, sessions, "alpha", true, noContent));
        CHECK(buckets.rebuilds() == 4);
        CHECK(buckets.rebuild(8, sessions, "alpha", true, noContent));
        CHECK(buckets.rebuilds() == 5);
        CHECK(buckets.rebuild(8, sessions, "alpha", true, noContent));
        CHECK(buckets.rebuilds() == 6);

        // Leaving the query rebuilds once, then holds again.
        CHECK(buckets.rebuild(8, sessions, "", true, noContent));
        CHECK(buckets.rebuilds() == 7);
        CHECK(!buckets.rebuild(8, sessions, "", true, noContent));
        CHECK(buckets.rebuilds() == 7);
    }

    // THE CONTRACT, FROM THE SIDE THAT COSTS. Same revision, different
    // catalog: the memo answers about the OLD one and does not look at the new
    // one. That is deliberate -- checking would cost the walk the memo exists
    // to avoid -- and it is the whole reason `mark_session_catalog_changed()`
    // must be on every path that touches `sessions`. Pinned so nobody
    // "fixes" it into a content comparison, and so the cost of a forgotten
    // bump is written down as a passing test rather than as a paragraph.
    {
        std::vector<SessionSummary> first = corpus(0);
        std::vector<SessionSummary> second = corpus(7);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(5, first, "", false, noContent);
        const std::size_t heldFolders = buckets.folders().size();
        const Members held = buckets.recent();
        CHECK(!buckets.rebuild(5, second, "", false, noContent));
        CHECK(buckets.folders().size() == heldFolders);
        CHECK(same(buckets.recent(), held));
        for (const auto* s : buckets.recent())
            CHECK(s >= first.data() && s < first.data() + first.size());
        // And the moment the revision moves, it is the new catalog's answer.
        CHECK(buckets.rebuild(6, second, "", false, noContent));
        CHECK(buckets.folders() == reference_folders(second));
    }

    // The bucket list is pruned to the folders that exist now, so a catalog
    // that churns folder keys cannot grow it without bound (a real backend
    // files a thread under its workspace path).
    {
        ecs::model::SidebarBuckets buckets;
        for (int k = 0; k < 50; ++k) {
            std::vector<SessionSummary> churn;
            churn.push_back(sess("w" + std::to_string(k), "Work",
                                 "/ws/" + std::to_string(k)));
            churn.push_back(sess("u" + std::to_string(k), "Unfiled", ""));
            buckets.rebuild(static_cast<std::uint64_t>(100 + k), churn, "",
                            false, noContent);
            CHECK(buckets.folders().size() == 1);
            CHECK(buckets.members("/ws/" + std::to_string(k)).size() == 1);
            if (k > 0)
                CHECK(buckets.members("/ws/" + std::to_string(k - 1)).empty());
        }
    }

    // A rebuild after the catalog moved must answer about the NEW catalog,
    // pointers included -- this is the dangling-pointer contract that makes
    // caching SessionSummary* safe.
    {
        std::vector<SessionSummary> sessions = corpus(0);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, sessions, "", false, noContent);
        std::vector<SessionSummary> replacement = corpus(7);
        buckets.rebuild(2, replacement, "", false, noContent);
        CHECK(buckets.folders() == reference_folders(replacement));
        CHECK(same(buckets.recent(),
                   reference_scan(replacement, "recent", false, true, "", false,
                                  noContent)));
        for (const auto* s : buckets.recent())
            CHECK(s >= replacement.data() &&
                  s < replacement.data() + replacement.size());
    }

    // A folder that disappears stops being listed and reports no members.
    {
        std::vector<SessionSummary> sessions = corpus(4);
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, sessions, "", false, noContent);
        const bool hadMoney = [&] {
            for (const auto& f : buckets.folders())
                if (f == "/work/money") return true;
            return false;
        }();
        CHECK(hadMoney);
        std::vector<SessionSummary> fewer;
        for (const auto& s : sessions)
            if (s.folder != "/work/money") fewer.push_back(s);
        buckets.rebuild(2, fewer, "", false, noContent);
        for (const auto& f : buckets.folders()) CHECK(f != "/work/money");
        CHECK(buckets.members("/work/money").empty());
        CHECK(buckets.members("/never/existed").empty());
    }

    // An empty catalog is a valid answer, not a crash.
    {
        const std::vector<SessionSummary> none;
        ecs::model::SidebarBuckets buckets;
        buckets.rebuild(1, none, "", false, noContent);
        CHECK(buckets.folders().empty());
        CHECK(buckets.recent().empty());
    }

    if (failures == 0) std::printf("test_sidebar_buckets: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
