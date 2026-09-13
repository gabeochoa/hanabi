// Saved views (src/ui/saved_views.h, saved_views_codec.h): the model the
// sidebar's Views `+` and the shelf menu's Save / Delete / Restore Default
// Views act on, held without a window. Each rule is the pinned reference's
// (SavedFilter.swift, SavedFilterEditing.swift, SidebarColumn.saveCurrentAsView
// at bffecaf), restated as behaviour.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/saved_views.h"
#include "../../src/ui/saved_views_codec.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using namespace hanabi::views;

static std::vector<std::string> ids(const std::vector<SavedView>& vs) {
    std::vector<std::string> out;
    for (const auto& v : vs) out.push_back(v.id);
    return out;
}

static SavedView user_view(const std::string& id, const std::string& name) {
    SavedView v;
    v.id = id;
    v.name = name;
    v.glyph = "filter";
    return v;
}

static api::SessionSummary thread(const std::string& id, const std::string& title,
                                  api::ThreadState state = api::ThreadState::Running,
                                  api::ThreadTag tag = api::ThreadTag::None,
                                  const std::string& folder = "",
                                  bool starred = false) {
    api::SessionSummary s;
    s.id = id;
    s.title = title;
    s.state = state;
    s.tag = tag;
    s.folder = folder;
    s.starred = starred;
    return s;
}

// ── seeding ───────────────────────────────────────────────────────────────
static void test_seeding() {
    std::printf("test_seeding\n");
    // Nothing stored: the six built-ins in definition order.
    Store fresh;
    CHECK((ids(fresh.shelves()) == std::vector<std::string>{"home", "settings", "blocked",
                                                            "review", "pinned", "archived"}));
    CHECK(!fresh.has_deleted_built_ins());

    // Stored order wins; a stored built-in comes back as the DEFINITION (a
    // renamed or re-scoped copy on disk cannot drift the app's own shelf);
    // missing built-ins are appended; a record claiming built-in under an
    // unknown id is dropped; a removed built-in stays away.
    SavedView drifted = *built_in("blocked");
    drifted.name = "Stuck";
    drifted.query = "x";
    SavedView impostor = user_view("ghost", "Ghost");
    impostor.builtIn = true;
    Store s({user_view("v1", "Mine"), drifted, impostor}, {"archived"});
    CHECK((ids(s.shelves()) == std::vector<std::string>{"v1", "blocked", "home", "settings",
                                                        "review", "pinned"}));
    CHECK(s.find("blocked")->name == "Blocked");
    CHECK(s.find("blocked")->query.empty());
    CHECK(s.find("ghost") == nullptr);
    CHECK(s.has_deleted_built_ins());
    CHECK(s.removed_built_ins() == std::vector<std::string>{"archived"});

    // Duplicate stored ids collapse to the first.
    Store dup({user_view("v1", "A"), user_view("v1", "B")}, {});
    CHECK(dup.find("v1")->name == "A");
}

// ── save current as view ──────────────────────────────────────────────────
static void test_save_current() {
    std::printf("test_save_current\n");
    Store s;
    const SavedView& blocked = *s.find("blocked");

    // The suggestion: "<workspace leaf> · <view name> · <query>".
    CHECK(derived_name(blocked, "", "") == "Blocked");
    CHECK(derived_name(blocked, "/Users/me/w/hanabi", "  ledger ") ==
          std::string("hanabi") + std::string(kNameSeparator) + "Blocked" +
              std::string(kNameSeparator) + "ledger");
    CHECK(derived_name(blocked, "team-a", "") ==
          std::string("team-a") + std::string(kNameSeparator) + "Blocked");
    SavedView nameless;
    CHECK(derived_name(nameless, "", "   ") == "Filter");
    CHECK(derived_name(nameless, "/trailing/", "") == "/trailing/");  // an empty leaf keeps the whole

    // The snapshot: scope + attention from the current shelf, workspace and
    // trimmed query as given, the name trimmed, a fresh non-built-in id.
    const SavedView made = make_from(blocked, "team-a", " ledger ", "  Ledger stuck ", "v-test");
    CHECK(made.id == "v-test");
    CHECK(made.name == "Ledger stuck");
    CHECK(made.scope == Scope::Catalog);
    CHECK(made.attention == Attention::Blocked);
    CHECK(made.workspace == "team-a");
    CHECK(made.query == "ledger");
    CHECK(!made.builtIn);
    CHECK(made.glyph == kDraftGlyph);

    // Fresh ids are never a built-in word and never repeat.
    const std::string a = fresh_id(), b = fresh_id();
    CHECK(a != b);
    CHECK(built_in(a) == nullptr);
    CHECK(a.rfind("v-", 0) == 0);

    // add: accepted once, refused as a duplicate, refused with an unusable
    // name -- the UI re-prompts on false instead of dropping the save.
    CHECK(s.add(made));
    CHECK(s.find("v-test") != nullptr);
    CHECK(s.shelves().back().id == "v-test");  // appended, so it is the newest shelf
    CHECK(!s.add(made));
    CHECK(!s.add(make_from(blocked, "", "", "   ", "v-blank")));
    CHECK(!usable_name("   "));
    CHECK(usable_name(" x "));
    CHECK(s.find("v-blank") == nullptr);
    SavedView noId = user_view("", "No id");
    CHECK(!s.add(noId));

    // What the UI does next is the reference's: select the new id, clear the
    // query field. resolve() gives the shelf for a selection.
    CHECK(s.resolve(std::string_view("v-test")).id == "v-test");
}

// ── delete / restore / rename / move ──────────────────────────────────────
static void test_delete_restore() {
    std::printf("test_delete_restore\n");
    Store s;
    CHECK(s.add(user_view("v1", "Mine")));

    // Delete is offered on a built-in; it is remembered as removed.
    CHECK(s.remove("review"));
    CHECK(s.find("review") == nullptr);
    CHECK(s.has_deleted_built_ins());
    CHECK(s.removed_built_ins() == std::vector<std::string>{"review"});
    CHECK(!s.remove("review"));  // gone already

    // Seeding from what this store would persist keeps it away.
    Store again(s.all(), s.removed_built_ins());
    CHECK(again.find("review") == nullptr);

    // Restore puts it back at its definition rank -- after blocked, before
    // pinned -- wherever the reader's own shelves sit.
    CHECK(s.move(s.shelves().size() - 1, 0));  // "Mine" to the front
    CHECK(s.shelves().front().id == "v1");
    CHECK(s.restore_built_in("review"));
    CHECK((ids(s.shelves()) == std::vector<std::string>{"v1", "home", "settings", "blocked",
                                                        "review", "pinned", "archived"}));
    CHECK(!s.has_deleted_built_ins());
    CHECK(s.removed_built_ins().empty());
    CHECK(!s.restore_built_in("review"));  // present already
    CHECK(!s.restore_built_in("v1"));      // not a built-in

    // Restore Default Views: only what is missing, counted.
    CHECK(s.remove("home"));
    CHECK(s.remove("archived"));
    CHECK(s.remove("v1"));  // a user view: deleted for good, not "removed"
    CHECK((s.removed_built_ins() == std::vector<std::string>{"archived", "home"}));
    CHECK(s.restore_defaults() == 2);
    CHECK(s.restore_defaults() == 0);
    CHECK((ids(s.shelves()) == std::vector<std::string>{"home", "settings", "blocked", "review",
                                                        "pinned", "archived"}));
    CHECK(s.find("v1") == nullptr);

    // Deleting the selected shelf: the UI falls back to resolve(nullopt) = Home,
    // or the first shelf when Home is gone too.
    CHECK(s.resolve(std::nullopt).id == "home");
    CHECK(s.resolve(std::string_view("nope")).id == "home");
    CHECK(s.remove("home"));
    CHECK(s.resolve(std::nullopt).id == "settings");
    Store empty({}, {"home", "settings", "blocked", "review", "pinned", "archived"});
    CHECK(empty.shelves().empty());
    CHECK(empty.resolve(std::nullopt).id == "home");  // the definition, so the UI has a shelf

    // Rename: user views only, trimmed, a no-op says false.
    Store r;
    CHECK(r.add(user_view("v1", "Mine")));
    CHECK(!r.rename("blocked", "Stuck"));
    CHECK(r.find("blocked")->name == "Blocked");
    CHECK(r.rename("v1", "  Ours "));
    CHECK(r.find("v1")->name == "Ours");
    CHECK(!r.rename("v1", "Ours"));
    CHECK(!r.rename("v1", "   "));
    CHECK(!r.rename("nope", "x"));

    // update: user views only.
    SavedView edited = *r.find("v1");
    edited.query = "q";
    CHECK(r.update(edited));
    CHECK(r.find("v1")->query == "q");
    SavedView bi = *r.find("blocked");
    bi.query = "q";
    CHECK(!r.update(bi));

    // move: bounds and identity.
    CHECK(!r.move(0, 0));
    CHECK(!r.move(0, 99));
    CHECK(r.move(0, 2));
    CHECK(r.shelves()[2].id == "home");
}

// ── keeps: which threads a shelf shows ────────────────────────────────────
static void test_keeps() {
    std::printf("test_keeps\n");
    const auto blocked = thread("b", "Ledger blocked", api::ThreadState::Attention,
                                api::ThreadTag::Blocked, "team-a");
    const auto waiting = thread("w", "Waiting on you", api::ThreadState::Attention,
                                api::ThreadTag::Waiting);
    const auto failed = thread("f", "Import failed", api::ThreadState::Attention,
                               api::ThreadTag::Failed);
    const auto ready = thread("r", "Ready for review", api::ThreadState::Ready);
    const auto running = thread("x", "Quiet running thread", api::ThreadState::Running,
                                api::ThreadTag::None, "team-b", /*starred=*/true);
    auto archived = thread("a", "Old ledger", api::ThreadState::Archived);

    const SavedView& home = *built_in("home");
    const SavedView& blk = *built_in("blocked");
    const SavedView& rev = *built_in("review");
    const SavedView& pin = *built_in("pinned");
    const SavedView& arc = *built_in("archived");
    const SavedView& set = *built_in("settings");

    // Home: every live thread, no archived.
    CHECK(keeps(home, blocked) && keeps(home, ready) && keeps(home, running));
    CHECK(!keeps(home, archived));
    // Attention shelves reuse the sidebar's own predicates.
    CHECK(keeps(blk, blocked) && keeps(blk, waiting) && keeps(blk, failed));
    CHECK(!keeps(blk, ready) && !keeps(blk, running));
    CHECK(keeps(rev, ready) && !keeps(rev, blocked));
    // Pinned / Archived read their own lists.
    CHECK(keeps(pin, running) && !keeps(pin, blocked));
    CHECK(keeps(arc, archived) && !keeps(arc, running));
    // Settings shows no threads.
    CHECK(!keeps(set, running));
    // A machine-local archive override is honoured both ways.
    archived.archive_override = false;
    CHECK(keeps(home, archived) && !keeps(arc, archived));

    // A saved view: workspace narrows, query is case-insensitive on the
    // displayed title, whitespace around the query is ignored.
    SavedView saved = make_from(blk, "team-a", " LEDGER ", "Ledger stuck", "v1");
    CHECK(keeps(saved, blocked));
    CHECK(!keeps(saved, waiting));  // no folder
    SavedView anyWorkspace = make_from(blk, "", "you", "Waiting", "v2");
    CHECK(keeps(anyWorkspace, waiting));
    CHECK(!keeps(anyWorkspace, blocked));  // query misses
    SavedView bare = make_from(home, "team-b", "", "Team B", "v3");
    CHECK(keeps(bare, running) && !keeps(bare, blocked));

    // Counts: what the badges show; archived threads do not count.
    const AttentionCounts c =
        attention_counts({blocked, waiting, failed, ready, running, archived});
    CHECK(c.blocked == 3);
    CHECK(c.review == 1);
    CHECK(c.home == 4);
}

// ── codec: round trip, unknown fields, unreadable records ─────────────────
static void test_codec() {
    std::printf("test_codec\n");
    Store s;
    CHECK(s.add(make_from(*s.find("blocked"), "team-a", "ledger", "Ledger stuck", "v1")));
    CHECK(s.remove("review"));

    nlohmann::json settings = nlohmann::json::object();
    settings["theme"] = "dark";  // an unrelated key survives untouched
    save(settings, s, std::nullopt);
    CHECK(settings["theme"] == "dark");
    CHECK(settings.contains(std::string(kSavedViewsKey)));
    CHECK(settings[std::string(kRemovedBuiltInsKey)] == nlohmann::json::array({"review"}));
    CHECK(!settings.contains(std::string(kUnreadableKey)));

    // Round trip: same shelves, same order, same removed list.
    Loaded back = load(settings);
    CHECK(!back.unreadable.has_value());
    CHECK(ids(back.store.shelves()) == ids(s.shelves()));
    CHECK(*back.store.find("v1") == *s.find("v1"));
    CHECK(back.store.removed_built_ins() == s.removed_built_ins());

    // A record with fields a future build added is read (unknown keys are
    // ignored); an absent optional field takes its default.
    nlohmann::json future = to_json(*s.find("v1"));
    future["colour"] = "teal";
    future.erase("glyph");
    const auto v = view_from_json(future);
    CHECK(v.has_value());
    CHECK(v->id == "v1" && v->glyph.empty() && v->query == "ledger");
    // A stored built-in decodes with built_in=true and is re-adopted on load.
    CHECK(view_from_json(to_json(*built_in("home")))->builtIn);

    // Unreadable: a non-object element, a missing id, an unknown scope word
    // -- each makes the WHOLE record unreadable, and load() preserves the raw
    // text instead of dropping or rewriting it.
    for (nlohmann::json bad :
         {nlohmann::json::array({42}),
          nlohmann::json::array({{{"name", "no id"}}}),
          nlohmann::json::array({{{"id", "v9"}, {"name", "odd"}, {"scope", "galaxy"}}}),
          nlohmann::json("not an array")}) {
        nlohmann::json doc = {{std::string(kSavedViewsKey), bad}};
        Loaded l = load(doc);
        CHECK(l.unreadable.has_value());
        CHECK(*l.unreadable == bad.dump());
        // The store still works: built-ins seeded as if nothing was stored.
        CHECK(l.store.find("home") != nullptr);
        // Saving with the unreadable text set does NOT write over the array.
        nlohmann::json out = doc;
        save(out, l.store, l.unreadable);
        CHECK(out[std::string(kSavedViewsKey)] == bad);
        CHECK(out[std::string(kUnreadableKey)] == bad.dump());
    }

    // The removed list is lenient: a non-string entry is skipped, not fatal.
    CHECK((decode_removed(nlohmann::json::array({"home", 7, "review"})) ==
          std::vector<std::string>{"home", "review"}));
    CHECK(decode_removed(nlohmann::json("x")).empty());

    // Missing keys: a fresh install.
    Loaded none = load(nlohmann::json::object());
    CHECK(!none.unreadable.has_value());
    CHECK(none.store.shelves().size() == built_ins().size());
}

int main() {
    std::printf("test_saved_views\n");
    test_seeding();
    test_save_current();
    test_delete_restore();
    test_keeps();
    test_codec();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
