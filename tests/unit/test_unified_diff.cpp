#include <cstdio>
#include <string>

#include "../../src/ui/unified_diff.h"

namespace ud = hanabi::unified_diff;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_unified_diff ===\n");
    const std::string one =
        "--- a/src/retry.rs\n+++ b/src/retry.rs\n@@ -1,3 +1,4 @@\n fn a() {}\n-let x = 1;\n+let x = 2;\n+let y = 3;\n";
    CHECK(ud::looks_like(one));
    const auto p = ud::parse(one);
    CHECK(p.ok && p.files.size() == 1);
    CHECK(p.files[0].old_path == "src/retry.rs" && p.files[0].new_path == "src/retry.rs");
    CHECK(p.title() == "src/retry.rs");
    CHECK(p.additions == 2 && p.deletions == 1);
    CHECK(p.lines.size() == 7);
    CHECK(p.lines[0].kind == ud::Kind::Header && p.lines[2].kind == ud::Kind::Hunk);
    CHECK(p.lines[3].kind == ud::Kind::Context && p.lines[4].kind == ud::Kind::Removed && p.lines[5].kind == ud::Kind::Added);
    CHECK(ud::counts(p.additions, p.deletions) == "+2 \xe2\x88\x92" "1");

    const std::string two =
        "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n-a\n+b\n--- /dev/null\n+++ b/new.txt\n@@ -0,0 +1 @@\n+hello\n";
    const auto q = ud::parse(two);
    CHECK(q.ok && q.files.size() == 2);
    CHECK(q.title() == "2 files");
    CHECK(q.files[1].title() == "new.txt");
    CHECK(q.lines[0].kind == ud::Kind::Meta);
    CHECK(q.additions == 2 && q.deletions == 1);

    const std::string plain = "fn a() {}\nlet x = 1;\n";
    CHECK(!ud::looks_like(plain));
    CHECK(!ud::parse(plain).ok);
    const std::string hunkOnly = "@@ -1 +1 @@\n-a\n+b\n";
    CHECK(!ud::looks_like(hunkOnly));
    const std::string noHunk = "--- a\n+++ b\n-a\n+b\n";
    CHECK(!ud::looks_like(noHunk));
    const std::string pathOnly = "--- a/x\n+++ b/x\n@@ -1 +1 @@\n";
    CHECK(ud::parse(pathOnly).title() == "x");
    CHECK(ud::is_lang("diff") && ud::is_lang("patch") && !ud::is_lang("rust"));
    CHECK(ud::strip_path_prefix("b/src/a.rs\t2026-01-01") == "src/a.rs");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
