#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/changed_files.h"

namespace cf = hanabi::changed_files;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

struct E { bool writesWhole; };
struct F { std::string path; std::vector<E> edits; };

int main() {
    std::printf("=== test_changed_files ===\n");
    CHECK(cf::mark_of(F{"a", {{true}}}) == cf::Mark::Written);
    CHECK(cf::mark_of(F{"a", {{true}, {true}}}) == cf::Mark::Written);
    CHECK(cf::mark_of(F{"a", {{false}}}) == cf::Mark::Modified);
    CHECK(cf::mark_of(F{"a", {{true}, {false}}}) == cf::Mark::Modified);
    CHECK(cf::mark_of(F{"a", {}}) == cf::Mark::Modified);
    CHECK(cf::letter(cf::Mark::Written) == 'W' && cf::letter(cf::Mark::Modified) == 'M');
    CHECK(cf::mark_tip(cf::Mark::Written).find("overwrite") != std::string_view::npos);
    CHECK(cf::mark_tip(cf::Mark::Modified) == "Edited in place this session");
    CHECK(cf::group_of("src/worker/retry.rs") == cf::Group::Code);
    CHECK(cf::group_of("src/worker/retry_test.rs") == cf::Group::Tests);
    CHECK(cf::group_of("tests/unit/test_x.cpp") == cf::Group::Tests);
    CHECK(cf::group_of("docs/api-parity.md") == cf::Group::Docs);
    CHECK(cf::group_of("README") == cf::Group::Docs);
    CHECK(cf::group_of("notes.txt") == cf::Group::Docs);
    CHECK(cf::group_of("src/Testimony.h") == cf::Group::Code);
    CHECK(cf::common_prefix({"src/a/x.rs", "src/a/y.rs"}) == "src/a/");
    CHECK(cf::common_prefix({"src/a/x.rs", "src/b/y.rs"}) == "src/");
    CHECK(cf::common_prefix({"a.rs", "b.rs"}).empty());
    CHECK(cf::common_prefix({"only.rs"}).empty());
    std::vector<F> files{{"src/w/test_x.rs", {{false}}}, {"src/w/dir/z.rs", {{true}}},
                         {"src/w/a.rs", {{false}}}, {"src/w/README.md", {{true}}}};
    const auto rows = cf::rows(files);
    CHECK(rows.size() == 4);
    CHECK(rows[0].group == cf::Group::Docs && rows[0].shown == "README.md" && rows[0].mark == cf::Mark::Written);
    CHECK(rows[1].group == cf::Group::Code && rows[1].shown == "a.rs");
    CHECK(rows[2].group == cf::Group::Code && rows[2].shown == "dir/z.rs");
    CHECK(rows[3].group == cf::Group::Tests && rows[3].shown == "test_x.rs");
    CHECK(rows[2].index == 1);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
