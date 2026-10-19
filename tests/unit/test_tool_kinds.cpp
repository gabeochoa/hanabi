// The tool kinds the fleet runs each read as themselves; an unknown tool is
// still the generic block with the same headline it had before.
#include <cstdio>
#include <string>

#include "../../src/api/tool_kinds.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::tool_kinds::classify;
using api::tool_kinds::headline;
using api::tool_kinds::Kind;
using api::tool_kinds::word;

static void test_every_spelling_of_a_kind_is_that_kind() {
    CHECK(classify("read") == Kind::Read);
    CHECK(classify("Read") == Kind::Read);
    CHECK(classify("mcp__fs__read") == Kind::Read);
    CHECK(classify("write") == Kind::Write);
    CHECK(classify("Write") == Kind::Write);
    CHECK(classify("edit") == Kind::Edit);
    CHECK(classify("MultiEdit") == Kind::Edit);
    CHECK(classify("bash") == Kind::Shell);
    CHECK(classify("Bash") == Kind::Shell);
    CHECK(classify("python") == Kind::Shell);
    CHECK(classify("grep") == Kind::Grep);
    CHECK(classify("Grep") == Kind::Grep);
    CHECK(classify("glob") == Kind::Glob);
    CHECK(classify("subagent__spawn") == Kind::SubAgent);
    CHECK(classify("Task") == Kind::SubAgent);
    CHECK(classify("WebFetch") == Kind::Web);
    CHECK(classify("WebSearch") == Kind::Web);
    CHECK(classify("web__fetch") == Kind::Web);
    CHECK(classify("meta__run") == Kind::Generic);
    CHECK(classify("step") == Kind::Step);
    CHECK(classify("") == Kind::Generic);
    CHECK(std::string(word(Kind::Read)) == "read");
    CHECK(std::string(word(Kind::Shell)) == "shell");
    CHECK(std::string(word(Kind::Generic)) == "tool");
}

static void test_each_kind_reads_its_own_argument() {
    CHECK(headline(Kind::Read, R"({"file_path":"/a/b.h"})") == "/a/b.h");
    CHECK(headline(Kind::Read, R"({"file_path":"/a/b.h","offset":10,"limit":20})") ==
          "/a/b.h  lines 11\xe2\x80\x93" "30");
    CHECK(headline(Kind::Edit, R"({"file_path":"/a/b.h","old_string":"x","new_string":"y"})") ==
          "/a/b.h");
    CHECK(headline(Kind::Write, R"({"path":"/tmp/out.md","content":"..."})") ==
          "/tmp/out.md");
    CHECK(headline(Kind::Shell, R"({"command":"ls -l","timeout_ms":5})") == "ls -l");
    CHECK(headline(Kind::Grep, R"({"pattern":"TODO","path":"src/"})") ==
          "TODO  in src/");
    CHECK(headline(Kind::Grep, R"({"pattern":"TODO"})") == "TODO");
    CHECK(headline(Kind::Glob, R"({"pattern":"*.rs","path":"/repo"})") ==
          "*.rs  in /repo");
    CHECK(headline(Kind::Web, R"({"url":"https://x.test/a"})") ==
          "https://x.test/a");
    CHECK(headline(Kind::SubAgent, R"({"prompt":"do the thing","title":"worker 3"})") ==
          "worker 3");
}

static void test_an_unknown_tool_keeps_the_old_headline() {
    // What readable_tool_input did before the registry: the first of
    // command/text/query/path/pattern, else the raw input.
    CHECK(headline(Kind::Generic, R"({"command":"meta oncall.rotation list"})") ==
          "meta oncall.rotation list");
    CHECK(headline(Kind::Generic, R"({"text":"Reading the notes"})") ==
          "Reading the notes");
    CHECK(headline(Kind::Generic, R"({"limit":5})") == R"({"limit":5})");
    CHECK(headline(Kind::Generic, "not json") == "not json");
    CHECK(headline(Kind::Read, "") == "");
    // A known kind with none of its keys falls back the same way rather than
    // going blank.
    CHECK(headline(Kind::Read, R"({"query":"q"})") == "q");
}

static void test_a_row_names_the_tool_in_words() {
    std::printf("test_a_row_names_the_tool_in_words\n");
    CHECK(api::tool_kinds::display_name("subagent__spawn") == "agent");
    CHECK(api::tool_kinds::display_name("web__fetch") == "web");
    CHECK(api::tool_kinds::display_name("WebFetch") == "webfetch");
    CHECK(api::tool_kinds::display_name("Bash") == "bash");
    CHECK(api::tool_kinds::display_name("python") == "python");
    CHECK(api::tool_kinds::display_name("weather__forecast_daily") ==
          "weather forecast daily");
    CHECK(api::tool_kinds::display_name("meta__run") == "meta run");
    CHECK(api::tool_kinds::display_name("__read").find("__") == std::string::npos);
    CHECK(api::tool_kinds::display_name("") == "");
    CHECK(api::tool_kinds::debug_suffix(api::tool_kinds::Kind::Read) == "_read");
    CHECK(api::tool_kinds::debug_suffix(api::tool_kinds::Kind::Generic) == "");
}

static void test_step_is_narration() {
    using api::tool_kinds::Kind;
    using api::tool_kinds::classify;
    using api::tool_kinds::is_narration;
    using api::tool_kinds::step_text;
    CHECK(classify("step") == Kind::Step);
    CHECK(classify("functions.step") == Kind::Step);
    CHECK(classify("mcp__x__step") == Kind::Step);
    CHECK(classify("stepper") == Kind::Generic);
    CHECK(step_text(R"({"text":"  Reading the catalog  "})") == "Reading the catalog");
    CHECK(step_text(R"({"text":"Mixed Case, no full stop added"})") == "Mixed Case, no full stop added");
    CHECK(step_text(R"({"text":""})").empty());
    CHECK(step_text(R"({"text":"   \n"})").empty());
    CHECK(step_text(R"({"note":"x"})").empty());
    CHECK(step_text("not json").empty());
    CHECK(step_text(R"({"text":")" + std::string(513, 'a') + R"("})").empty());
    CHECK(step_text(R"({"text":")" + std::string(512, 'a') + R"("})").size() == 512);
    CHECK(is_narration("step", R"({"text":"Working"})"));
    CHECK(!is_narration("step", R"({"text":""})"));
    CHECK(!is_narration("bash", R"({"text":"Working"})"));
    CHECK(api::tool_kinds::headline(Kind::Step, R"({"text":" The line "})") == "The line");
    CHECK(api::tool_kinds::debug_suffix(Kind::Step) == "_step");
}

int main() {
    test_step_is_narration();
    std::printf("=== test_tool_kinds ===\n");
    test_every_spelling_of_a_kind_is_that_kind();
    test_a_row_names_the_tool_in_words();
    test_each_kind_reads_its_own_argument();
    test_an_unknown_tool_keeps_the_old_headline();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
