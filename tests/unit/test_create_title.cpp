#include <cstdio>
#include <string>

#include "../../src/api/create_title.h"
#include "../../src/api/generated_title_wait.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace ct = api::create_title;

int main() {
    CHECK(ct::explicit_title("  Payroll audit \n") == "Payroll audit");
    CHECK(ct::explicit_title("   ").empty());
    CHECK(ct::explicit_title("a\nb") == "a b");
    CHECK(ct::explicit_title(std::string(300, 'x')).size() == ct::kMaxTitle);
    // A cap never splits a UTF-8 sequence.
    std::string s(119, 'x');
    s += "\xc3\xa9\xc3\xa9";
    const std::string cut = ct::explicit_title(s);
    CHECK(cut.size() == 119);
    // The create's title: the typed name wins; else the prompt's first line.
    CHECK(ct::for_create("Named", "the prompt\nmore") == "Named");
    CHECK(ct::for_create("  ", "the prompt\nmore") == "the prompt");
    CHECK(ct::for_create("", std::string(200, 'p')).size() == ct::kMaxTitle);
    // Automatic naming (kt-jko9; the reference's ThreadTitle.createTitle):
    // a typed name always goes on the wire; unnamed + auto sends NOTHING,
    // which is what lets the server name the thread; unnamed + off stamps
    // the prompt slice as before; image-only + off is "Image".
    CHECK(ct::for_create("Named", "the prompt", true, false) == "Named");
    CHECK(ct::for_create("", "the prompt\nmore", true, false).empty());
    CHECK(ct::for_create("  ", "the prompt", true, true).empty());
    CHECK(ct::for_create("", "the prompt\nmore", false, false) == "the prompt");
    CHECK(ct::for_create("", "", false, true) == "Image");
    CHECK(ct::for_create("", "", false, false).empty());
    // The display ladder (the reference's ThreadTitle.named): human title
    // first -- even one that reads as the placeholder -- then a stored
    // title that is not the placeholder, then subject, then headline.
    CHECK(ct::named_title("agentcloud session", true, "subj", "head") ==
          "agentcloud session");
    CHECK(ct::named_title("agentcloud session", false, "subj", "head") == "subj");
    CHECK(ct::named_title("Server name", false, "subj", "head") == "Server name");
    CHECK(ct::named_title("", false, "", "head") == "head");
    CHECK(ct::named_title("", false, "", "").empty());
    CHECK(ct::is_placeholder("  agentcloud session "));
    CHECK(!ct::is_placeholder("Server name"));
    // The generated-title wait: only a locally created untitled thread,
    // only inside the budget, and a name settles it at once.
    namespace gw = api::generated_title_wait;
    gw::Tracker wait;
    CHECK(!wait.is_pending("s1", "(untitled)", 1000));
    wait.note("s1", 1000);
    CHECK(wait.is_pending("s1", "(untitled)", 1000));
    CHECK(wait.is_pending("s1", "", 1000 + gw::kBudgetMs));
    CHECK(!wait.is_pending("s1", "(untitled)", 1000 + gw::kBudgetMs + 1));
    CHECK(!wait.is_pending("s1", "Server name", 1000));
    wait.settle("s1");
    CHECK(!wait.is_pending("s1", "(untitled)", 1000));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
