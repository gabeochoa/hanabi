#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/session_changes.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace ch = api::changes;

static ch::Call edit(const std::string& path, const std::string& o, const std::string& n,
                     bool applied = true, const std::string& node = "") {
    nlohmann::json in{{"file_path", path}, {"old_string", o}, {"new_string", n}};
    return ch::Call{"Edit", in.dump(), applied, node};
}
static ch::Call write(const std::string& path, const std::string& content,
                      const std::string& node = "") {
    nlohmann::json in{{"path", path}, {"content", content}};
    return ch::Call{"write", in.dump(), true, node};
}

static void test_diff_keeps_context_and_counts() {
    const auto lines = ch::diff_lines("a\nb\nc\n", "a\nB\nc\nd\n");
    CHECK(lines.size() == 5);
    CHECK(lines[0].side == ch::Side::Context && lines[0].text == "a");
    int add = 0, del = 0;
    for (const auto& l : lines) {
        add += l.side == ch::Side::Added;
        del += l.side == ch::Side::Removed;
    }
    CHECK(add == 2 && del == 1);
    CHECK(ch::diff_lines("", "x\ny").size() == 2);
    CHECK(ch::diff_lines("same", "same").size() == 1);
}

static void test_only_applied_edits_and_writes_count() {
    std::vector<ch::Call> calls{
        edit("src/a.cpp", "x", "y"),
        edit("src/b.cpp", "x", "y", /*applied=*/false),
        ch::Call{"bash", R"({"command":"ls"})", true, ""},
        ch::Call{"Read", R"({"file_path":"src/c.cpp"})", true, ""},
        write("docs/n.md", "hello\nworld\n"),
    };
    const auto files = ch::fold(calls);
    CHECK(files.size() == 2);
    CHECK(files[0].path == "docs/n.md");  // most recent first
    CHECK(files[1].path == "src/a.cpp");
    CHECK(files[0].name() == "n.md" && files[0].directory() == "docs");
    const auto s = ch::summary(files);
    CHECK(s.files == 2 && s.additions == 3 && s.deletions == 1);
    CHECK(ch::chip_label(s) == "2 files" && ch::chip_label(s, false) == "2");
    CHECK(ch::header_label(s) == "2 files changed");
}

static void test_one_file_per_node_and_path_newest_first() {
    std::vector<ch::Call> calls{
        edit("a.txt", "1", "2", true, "aspen"), edit("b.txt", "1", "2"),
        edit("a.txt", "2", "3", true, "aspen"), edit("a.txt", "1", "2", true, "boulder")};
    const auto files = ch::fold(calls);
    CHECK(files.size() == 3);
    CHECK(files[0].key == "boulder:a.txt");
    CHECK(files[1].key == "aspen:a.txt" && files[1].edits.size() == 2);
    CHECK(files[2].key == "b.txt");
}

static void test_whole_text_only_when_every_edit_places_exactly() {
    std::vector<ch::Call> calls{write("f.py", "def a():\n    return 1\n"),
                                edit("f.py", "return 1", "return 2")};
    auto files = ch::fold(calls);
    std::string whole;
    CHECK(ch::whole_text(files[0], &whole));
    CHECK(whole == "def a():\n    return 2\n");
    // An edit whose old text is not found: unknown, not guessed.
    calls.push_back(edit("f.py", "nowhere", "x"));
    files = ch::fold(calls);
    CHECK(!ch::whole_text(files[0], &whole));
    // No write at all: unknown.
    files = ch::fold({edit("g.py", "a", "b")});
    CHECK(!ch::whole_text(files[0], &whole));
    // replace_all applies to every hit.
    nlohmann::json all{{"file_path", "h.txt"}, {"old_string", "x"}, {"new_string", "y"},
                       {"replace_all", true}};
    files = ch::fold({write("h.txt", "x x x"), ch::Call{"edit", all.dump(), true, ""}});
    CHECK(ch::whole_text(files[0], &whole) && whole == "y y y");
}

int main() {
    test_diff_keeps_context_and_counts();
    test_only_applied_edits_and_writes_count();
    test_one_file_per_node_and_path_newest_first();
    test_whole_text_only_when_every_edit_places_exactly();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
