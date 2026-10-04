#include <cstdio>
#include <string>

#include "../../src/api/pins_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace pn = api::pins;
using V = std::vector<std::string>;
using S = std::set<std::string>;

int main() {
    // Overlays: known includes the isPinned:false row; a login page is not "nothing pinned".
    const auto snap = pn::parse_overlays(
        R"({"overlays":[{"sessionId":"a","isPinned":true},{"sessionId":"b","isPinned":false,"folderId":"f"},{"sessionId":"c"}]})");
    CHECK(snap && snap->pinned == S{"a"} && snap->known == (S{"a", "b", "c"}));
    CHECK(!pn::parse_overlays("<html>").has_value() && pn::parse_overlays(R"({"overlays":[]})")->known.empty());

    // Order: preferences first; a doc without the key is "never arranged".
    CHECK((*pn::parse_order(R"({"preferences":{"pinnedSessionOrderIds":["x","y","x",""]}})") == V{"x", "y"}));
    CHECK(pn::parse_order(R"({"preferences":{},"synced":{"pinnedSessionOrderIds":["z"]}})")->empty());
    CHECK((*pn::parse_order(R"({"synced":{"pinnedSessionOrderIds":["z"]}})") == V{"z"}));
    CHECK(!pn::parse_order(R"({"exists":false})").has_value());
    CHECK(pn::order_body({"a", "a", "b"}) == R"({"pinnedSessionOrderIds":["a","b"]})");
    V many;
    for (int i = 0; i < 300; ++i) many.push_back("id" + std::to_string(i));
    CHECK(pn::normalize(many).size() == 256);

    // Arranged: unarranged first, stale stored ids ignored, empty list keeps incoming order.
    CHECK((pn::arranged({"a", "b", "c", "d"}, {"c", "gone", "a"}) == V{"b", "d", "c", "a"}));
    CHECK((pn::arranged({"a", "b"}, {}) == V{"a", "b"}));
    CHECK(pn::arranged(many, {}).size() == 300);  // the cap never unpins
    CHECK((pn::after_pin({}, "n").empty()));
    CHECK((pn::after_pin({"a", "n", "b"}, "n") == V{"n", "a", "b"}));
    CHECK((pn::after_unpin({"a", "n", "b"}, "n") == V{"a", "b"}));

    // Reconcile: known+absent dropped, unknown+absent kept local-only, arrivals appended sorted.
    {
        const auto r = pn::reconcile({"keep", "gone", "old"}, {"keep", "gone", "old"}, {"keep", "z", "new"},
                                     {"gone"}, {}, {});
        CHECK((r.order == V{"keep", "old", "new", "z"}));
        CHECK((r.dropped == V{"gone"}) && (r.local_only == V{"old"}) && (r.arrived == V{"new", "z"}));
        CHECK(r.server_known.count("z") && !r.server_known.count("gone"));
    }
    // A row naming an id with isPinned:false (seen) answers for it even when serverKnown is empty.
    {
        const auto r = pn::reconcile({"a"}, {"a"}, {}, {}, {"a"}, {});
        CHECK(r.order.empty() && (r.dropped == V{"a"}));
    }
    // Pinned while the read was in flight (not asked): survives.
    {
        const auto r = pn::reconcile({"a", "fresh"}, {"a"}, {"a"}, {"fresh"}, {"fresh"}, {});
        CHECK((r.order == V{"a", "fresh"}));
    }
    // Unlanded intent outranks the answer, both ways.
    {
        const auto r = pn::reconcile({"p"}, {"p"}, {}, {"p"}, {"p"}, {{"p", true}, {"u", false}});
        CHECK((r.order == V{"p"}));
        const auto r2 = pn::reconcile({}, {}, {"u"}, {}, {}, {{"u", false}});
        CHECK(r2.order.empty() && r2.arrived.empty());
    }
    // Reachability and the sentence (the reference's InboxState.describe).
    namespace pw = api::pins;
    CHECK(pw::reach_of(false, "the web app was unreachable: Connection", 0, false) == pw::Reach::Unreachable);
    CHECK(pw::reach_of(false, "no web credential", 0, false) == pw::Reach::NoCredential);
    CHECK(pw::reach_of(true, "", 307, false) == pw::Reach::LoginRedirect);
    CHECK(pw::reach_of(true, "", 500, false) == pw::Reach::Refused);
    CHECK(pw::reach_of(true, "", 200, false) == pw::Reach::Malformed);
    CHECK(pw::reach_of(true, "", 200, true) == pw::Reach::Synced);
    CHECK(!pw::sync_note(pw::Reach::Synced, 0, 0));
    CHECK(pw::sync_note(pw::Reach::Synced, 0, 2) == "Synced \xc2\xb7 2 pinned on this Mac only");
    CHECK(pw::describe(pw::Reach::Refused, 403, 0) == "On this Mac only \xe2\x80\x94 the web app refused (HTTP 403)");
    CHECK(pw::describe(pw::Reach::Unknown, 0, 0) == "Checking your pins\xe2\x80\xa6");
    // The one-shot carry: this Mac's pins the web lacks, in this Mac's order, once each.
    CHECK((pw::migration_ids({"t6", "t3", "t7", "t3"}, {"t7"}) == std::vector<std::string>{"t6", "t3"}));
    CHECK(pw::migration_ids({"t7"}, {"t7"}).empty());
    // A session the web has a row for (unpinned there) is the web's word: not carried.
    CHECK((pw::migration_ids({"t3", "t6"}, {}, {"t3"}) == std::vector<std::string>{"t6"}));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
