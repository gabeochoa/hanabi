#include <cstdio>
#include <map>
#include <string>

#include "../../src/ui/inbox_triage.h"

namespace tr = hanabi::triage;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_inbox_triage ===\n");
    CHECK(tr::kDefinitions.size() == 9);
    CHECK(tr::find("archive") != nullptr && tr::find("archive")->command == tr::Command::Archive);
    CHECK(tr::find("select") == nullptr);
    CHECK((tr::parse_binding("j") == tr::Binding{"j", ""}));
    CHECK((tr::parse_binding("g i") == tr::Binding{"g", "i"}));
    CHECK((tr::parse_binding("  g   e ") == tr::Binding{"g", "e"}));
    CHECK(tr::parse_binding("").empty());
    CHECK(tr::serialize_binding(tr::Binding{"g", "s"}) == "g s");
    CHECK(tr::serialize_binding(tr::Binding{"return", ""}) == "return");
    CHECK(tr::serialize_binding(tr::Binding{}).empty());
    CHECK((tr::shipped(tr::Command::GoPinned) == tr::Binding{"g", "s"}));
    CHECK((tr::shipped(tr::Command::Open) == tr::Binding{"return", ""}));

    const auto shipped = [](tr::Command c) { return tr::shipped(c); };
    auto r = tr::resolve("j", "", shipped);
    CHECK(r.matched() && r.command == tr::Command::Next && !r.pending_prefix);
    r = tr::resolve("g", "", shipped);
    CHECK(!r.matched() && r.pending_prefix);
    r = tr::resolve("i", "g", shipped);
    CHECK(r.matched() && r.command == tr::Command::GoHome);
    r = tr::resolve("e", "g", shipped);
    CHECK(r.matched() && r.command == tr::Command::GoArchived);
    r = tr::resolve("e", "", shipped);
    CHECK(r.matched() && r.command == tr::Command::Archive);
    r = tr::resolve("z", "g", shipped);
    CHECK(!r.matched() && !r.pending_prefix);
    r = tr::resolve("q", "", shipped);
    CHECK(!r.matched() && !r.pending_prefix);

    std::map<tr::Command, tr::Binding> remapped;
    remapped[tr::Command::Archive] = tr::Binding{"d", ""};
    remapped[tr::Command::Next] = tr::Binding{};
    const auto custom = [&](tr::Command c) {
        auto it = remapped.find(c);
        return it == remapped.end() ? tr::shipped(c) : it->second;
    };
    r = tr::resolve("d", "", custom);
    CHECK(r.matched() && r.command == tr::Command::Archive);
    r = tr::resolve("e", "", custom);
    CHECK(!r.matched());
    r = tr::resolve("j", "", custom);
    CHECK(!r.matched());

    CHECK(tr::display(tr::Binding{"g", "i"}) == "G then I");
    CHECK(tr::display(tr::Binding{"return", ""}) == "Return");
    CHECK(tr::display(tr::Binding{"/", ""}) == "/");
    CHECK(tr::display(tr::Binding{}) == "Unassigned");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
