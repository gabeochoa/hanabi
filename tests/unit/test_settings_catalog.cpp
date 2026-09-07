
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/ui/settings_catalog.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace cat = hanabi::settings_catalog;
using cat::Focus;
using cat::Pane;
using cat::Stops;
using cat::Zone;

static void test_every_row_lands_in_a_pane_that_exists() {
    for (const cat::Row& r : cat::kRows) {
        bool found = false;
        for (const cat::PaneInfo& m : cat::kPanes)
            if (m.pane == r.pane) found = true;
        if (!found) std::printf("  orphan row: %s\n", r.id);
        CHECK(found);
    }
}

static void test_row_ids_are_unique() {
    std::set<std::string_view> seen;
    for (const cat::Row& r : cat::kRows) {
        const bool fresh = seen.insert(r.id).second;
        if (!fresh) std::printf("  duplicate row id: %s\n", r.id);
        CHECK(fresh);
    }
    CHECK(seen.size() == cat::kRows.size());
}

static void test_pane_slugs_are_unique_and_resolvable() {
    std::set<std::string_view> seen;
    for (const cat::PaneInfo& m : cat::kPanes) {
        CHECK(seen.insert(m.slug).second);
        CHECK(cat::is_known_slug(m.slug));
        CHECK(cat::pane_from_slug(m.slug) == m.pane);
    }
    CHECK(!cat::is_known_slug("speech"));
    CHECK(cat::pane_from_slug("speech") == Pane::General);
    CHECK(cat::pane_from_slug("") == Pane::General);
}

static void test_pane_list_is_grouped() {
    std::set<int> closed;
    int previous = -1;
    for (const cat::PaneInfo& m : cat::kPanes) {
        const int g = static_cast<int>(m.group);
        if (g != previous) {
            const bool reopened = closed.count(g) > 0;
            if (reopened)
                std::printf("  group %s appears twice\n",
                            cat::group_heading(m.group));
            CHECK(!reopened);
            if (previous >= 0) closed.insert(previous);
            previous = g;
        }
    }
    CHECK(previous >= 0);
}

static void test_every_pane_has_a_row_unless_it_says_otherwise() {
    for (const cat::PaneInfo& m : cat::kPanes) {
        const size_t n = cat::rows_in(m.pane).size();
        if (cat::pane_is_rowless(m.pane)) {
            CHECK(n == 0);
        } else {
            if (n == 0) std::printf("  empty pane: %s\n", m.label);
            CHECK(n > 0);
        }
    }
}

static void test_rows_in_returns_catalogue_order() {
    const auto general = cat::rows_in(Pane::General);
    CHECK(general.size() == 4);
    CHECK(std::string_view(general[0]->id) == "send_key");
    CHECK(std::string_view(general[1]->id) == "new_line");
    CHECK(std::string_view(general[2]->id) == "restore_tabs");
    CHECK(std::string_view(general[3]->id) == "timestamps");
}

static void test_every_origin_says_something() {
    for (const cat::Row& r : cat::kRows) {
        CHECK(std::string_view(cat::origin_mark(r.origin)).size() > 0);
        CHECK(std::string_view(cat::origin_help(r.origin)).size() > 0);
    }
    CHECK(std::string_view(cat::origin_mark(cat::Origin::Device)) ==
          "This Mac");
    CHECK(std::string_view(cat::origin_mark(cat::Origin::Account)) ==
          "Your account");
    CHECK(std::string_view(cat::origin_mark(cat::Origin::Readout)) ==
          "Read-only");
}

static void test_account_rows_are_the_synced_ones() {
    const char* synced[] = {"yap", "notify_sound", "memory_backend",
                            "auto_archive"};
    for (const char* id : synced) {
        const cat::Row* r = cat::find_row(id);
        CHECK(r != nullptr);
        if (r) CHECK(r->origin == cat::Origin::Account);
    }
    for (const char* id : {"palette", "font", "accent", "timestamps"}) {
        const cat::Row* r = cat::find_row(id);
        CHECK(r != nullptr);
        if (r) CHECK(r->origin == cat::Origin::Device);
    }
    for (const char* id : {"endpoint", "identity", "new_line"}) {
        const cat::Row* r = cat::find_row(id);
        CHECK(r != nullptr);
        if (r) CHECK(r->origin == cat::Origin::Readout);
    }
}

static void test_empty_query_is_not_a_query() {
    CHECK(cat::search("").empty());
    CHECK(cat::search("   ").empty());
    CHECK(cat::search("\t \n").empty());
}

static void test_a_query_that_matches_nothing_returns_nothing() {
    const auto hits = cat::search("xyzzyplugh");
    CHECK(hits.empty());
}

static void test_title_beats_keyword() {
    const auto hits = cat::search("theme");
    CHECK(!hits.empty());
    if (!hits.empty()) {
        CHECK(std::string_view(hits[0].row->id) == "theme_rotate");
        CHECK(hits[0].field == cat::MatchField::Title);
    }
    bool sawPalette = false;
    for (const auto& h : hits)
        if (std::string_view(h.row->id) == "palette") sawPalette = true;
    CHECK(sawPalette);
    for (size_t i = 1; i < hits.size(); ++i)
        CHECK(hits[i].score <= hits[i - 1].score);
}

static void test_a_prefix_outranks_a_substring() {
    const auto hits = cat::search("them");
    CHECK(!hits.empty());
    if (!hits.empty()) {
        CHECK(std::string_view(hits[0].row->id) == "theme_rotate");
    }
}

static void test_a_keyword_finds_a_row_whose_title_lacks_the_word() {
    const auto hits = cat::search("dark");
    CHECK(!hits.empty());
    if (!hits.empty()) {
        CHECK(std::string_view(hits[0].row->id) == "palette");
        CHECK(hits[0].field == cat::MatchField::Keyword);
    }
    const auto dnd = cat::search("dnd");
    CHECK(!dnd.empty());
    if (!dnd.empty())
        CHECK(std::string_view(dnd[0].row->id) == "quiet_hours");
}

static void test_search_is_case_and_space_insensitive() {
    const auto a = cat::search("THEME");
    const auto b = cat::search("  theme  ");
    const auto c = cat::search("Theme");
    CHECK(a.size() == b.size());
    CHECK(b.size() == c.size());
    if (!a.empty() && !b.empty() && !c.empty()) {
        CHECK(std::string_view(a[0].row->id) == "theme_rotate");
        CHECK(std::string_view(b[0].row->id) == "theme_rotate");
        CHECK(std::string_view(c[0].row->id) == "theme_rotate");
    }
}

static void test_a_pane_name_finds_its_rows() {
    const auto hits = cat::search("storage");
    CHECK(!hits.empty());
    bool allStorage = !hits.empty();
    for (const auto& h : hits)
        if (h.row->pane != Pane::Storage) allStorage = false;
    CHECK(allStorage);
}

static void test_ties_keep_catalogue_order() {
    const auto hits = cat::search("colour");
    CHECK(hits.size() >= 2);
    for (size_t i = 1; i < hits.size(); ++i) {
        CHECK(hits[i].score <= hits[i - 1].score);
        if (hits[i].score == hits[i - 1].score) {
            size_t prev = 0, cur = 0;
            for (size_t k = 0; k < cat::kRows.size(); ++k) {
                if (&cat::kRows[k] == hits[i - 1].row) prev = k;
                if (&cat::kRows[k] == hits[i].row) cur = k;
            }
            CHECK(prev < cur);
        }
    }
}

static void test_tab_walks_search_then_nav_then_content() {
    Stops s;
    s.content = 3;
    Focus f{Zone::Search, 0};
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Nav, 0}));
    for (int i = 1; i < static_cast<int>(cat::kPaneCount); ++i) {
        f = cat::focus_next(f, s);
        CHECK(f == (Focus{Zone::Nav, i}));
    }
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Content, 0}));
    f = cat::focus_next(f, s);
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Content, 2}));
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Search, 0}));
}

static void test_shift_tab_is_the_exact_inverse() {
    Stops s;
    s.content = 3;
    Focus f{Zone::Search, 0};
    for (int i = 0; i < 40; ++i) {
        const Focus forward = cat::focus_next(f, s);
        const Focus back = cat::focus_prev(forward, s);
        CHECK(back == f);
        f = forward;
    }
}

static void test_an_empty_content_zone_is_skipped() {
    Stops s;
    s.content = 0;
    Focus f{Zone::Nav, static_cast<int>(cat::kPaneCount) - 1};
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Search, 0}));
    f = cat::focus_prev(f, s);
    CHECK(f == (Focus{Zone::Nav, static_cast<int>(cat::kPaneCount) - 1}));
}

static void test_search_results_replace_the_nav_zone() {
    Stops s;
    s.searching = true;
    s.results = 2;
    s.content = 5;  // ignored while searching
    CHECK(s.nav_stops() == 2);
    CHECK(s.content_stops() == 0);
    Focus f{Zone::Search, 0};
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Nav, 0}));
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Nav, 1}));
    f = cat::focus_next(f, s);
    CHECK(f == (Focus{Zone::Search, 0}));
}

static void test_a_zero_result_search_keeps_focus_in_the_field() {
    Stops s;
    s.searching = true;
    s.results = 0;
    s.content = 5;
    Focus f{Zone::Search, 0};
    CHECK(cat::focus_next(f, s) == (Focus{Zone::Search, 0}));
    CHECK(cat::focus_prev(f, s) == (Focus{Zone::Search, 0}));
    CHECK(cat::clamp_focus(Focus{Zone::Content, 3}, s) ==
          (Focus{Zone::Search, 0}));
    CHECK(cat::clamp_focus(Focus{Zone::Nav, 1}, s) == (Focus{Zone::Search, 0}));
}

static void test_arrows_clamp_where_tab_wraps() {
    Stops s;
    s.content = 3;
    Focus top{Zone::Nav, 0};
    CHECK(cat::focus_step_in_zone(top, s, -1) == top);
    Focus bottom{Zone::Nav, static_cast<int>(cat::kPaneCount) - 1};
    CHECK(cat::focus_step_in_zone(bottom, s, +1) == bottom);
    CHECK(cat::focus_step_in_zone(top, s, +1) == (Focus{Zone::Nav, 1}));
    Focus field{Zone::Search, 0};
    CHECK(cat::focus_step_in_zone(field, s, +1) == field);
}

static void test_focus_survives_a_pane_switch_to_a_smaller_pane() {
    Stops big;
    big.content = 6;
    Focus deep{Zone::Content, 5};
    CHECK(cat::clamp_focus(deep, big) == deep);
    Stops small;
    small.content = 2;
    CHECK(cat::clamp_focus(deep, small) == (Focus{Zone::Content, 1}));
}

static void test_reveal_centres_a_row_and_clamps_at_the_top() {
    CHECK(cat::reveal_offset(195.0f, 195.0f, 52.0f, 312.0f, 0.0f) == 0.0f);
    CHECK(cat::reveal_offset(615.0f, 195.0f, 52.0f, 312.0f, 0.0f) == 290.0f);
    CHECK(cat::reveal_offset(300.0f, 195.0f, 52.0f, 312.0f, 100.0f) == 75.0f);
    CHECK(cat::reveal_offset(195.0f, 195.0f, 400.0f, 312.0f, 0.0f) == 44.0f);
}

int main() {
    std::printf("== settings catalogue ==\n");
    test_every_row_lands_in_a_pane_that_exists();
    test_row_ids_are_unique();
    test_pane_slugs_are_unique_and_resolvable();
    test_pane_list_is_grouped();
    test_every_pane_has_a_row_unless_it_says_otherwise();
    test_rows_in_returns_catalogue_order();
    test_every_origin_says_something();
    test_account_rows_are_the_synced_ones();

    test_empty_query_is_not_a_query();
    test_a_query_that_matches_nothing_returns_nothing();
    test_title_beats_keyword();
    test_a_prefix_outranks_a_substring();
    test_a_keyword_finds_a_row_whose_title_lacks_the_word();
    test_search_is_case_and_space_insensitive();
    test_a_pane_name_finds_its_rows();
    test_ties_keep_catalogue_order();

    test_tab_walks_search_then_nav_then_content();
    test_shift_tab_is_the_exact_inverse();
    test_an_empty_content_zone_is_skipped();
    test_search_results_replace_the_nav_zone();
    test_a_zero_result_search_keeps_focus_in_the_field();
    test_arrows_clamp_where_tab_wraps();
    test_focus_survives_a_pane_switch_to_a_smaller_pane();
    test_reveal_centres_a_row_and_clamps_at_the_top();

    if (g_failures != 0) {
        std::printf("FAILED (%d of %d checks)\n", g_failures, g_checks);
        return 1;
    }
    if (g_checks == 0) {
        std::printf("FAILED (no checks ran)\n");
        return 1;
    }
    std::printf("OK (%d checks)\n", g_checks);
    return 0;
}
