#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/composer_templates.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace tp = hanabi::templates;

static const std::vector<std::string_view> kBuiltins{"new", "model", "effort",
                                                     "btw", "compact"};

static void test_names_follow_the_verb_alphabet() {
    std::vector<tp::Template> none;
    CHECK(tp::refusal("standup", "Yesterday:", none, kBuiltins).empty());
    CHECK(tp::refusal("pr-review_2", "x", none, kBuiltins).empty());
    CHECK(!tp::refusal("", "x", none, kBuiltins).empty());
    CHECK(!tp::refusal("Stand up", "x", none, kBuiltins).empty());
    CHECK(!tp::refusal(std::string(tp::kNameMax + 1, 'a'), "x", none, kBuiltins).empty());
    CHECK(tp::refusal(std::string(tp::kNameMax, 'a'), "x", none, kBuiltins).empty());
}

static void test_builtins_and_duplicates_are_refused_and_say_why() {
    std::vector<tp::Template> have{{"standup", "a"}};
    const std::string builtin = tp::refusal("new", "x", have, kBuiltins);
    CHECK(builtin == "/new is a built-in command.");
    CHECK(tp::refusal("standup", "b", have, kBuiltins) ==
          "A template called /standup already exists.");
    CHECK(tp::refusal("blank", " \n\t", have, kBuiltins) == "A template needs some text.");
    std::vector<tp::Template> full(tp::kMaxTemplates, tp::Template{"x", "y"});
    for (std::size_t i = 0; i < full.size(); ++i) full[i].name = "t" + std::to_string(i);
    CHECK(!tp::refusal("one-more", "z", full, kBuiltins).empty());
}

static void test_normalized_name_drops_slash_case_and_space() {
    CHECK(tp::normalized_name(" /StandUp ") == "standup");
    CHECK(tp::normalized_name("//a") == "a");
    CHECK(tp::normalized_name("") == "");
}

static void test_a_draft_offers_templates_by_prefix_until_its_argument() {
    std::vector<tp::Template> all{{"standup", "Yesterday"}, {"stats", "s"}, {"pr", "p"}};
    CHECK(tp::offered("/st", all).size() == 2);
    CHECK(tp::offered("/ST", all).size() == 2);
    CHECK(tp::offered("/", all).size() == 3);
    CHECK(tp::offered("/pr", all).size() == 1);
    CHECK(tp::offered("/st ", all).empty());   // reached an argument
    CHECK(tp::offered("st", all).empty());     // not a command at all
    CHECK(tp::offered("", all).empty());
    CHECK(tp::find("stats", all) != nullptr && tp::find("stat", all) == nullptr);
    CHECK(tp::first_line("one\ntwo") == "one");
    CHECK(tp::first_line("only") == "only");
}

int main() {
    test_names_follow_the_verb_alphabet();
    test_builtins_and_duplicates_are_refused_and_say_why();
    test_normalized_name_drops_slash_case_and_space();
    test_a_draft_offers_templates_by_prefix_until_its_argument();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
