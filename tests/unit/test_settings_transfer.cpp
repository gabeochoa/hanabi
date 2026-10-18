#include <cstdio>
#include <string>

#include "../../src/ui/settings_transfer.h"

namespace st = hanabi::settings_transfer;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_settings_transfer ===\n");
    CHECK(st::export_path("/Users/x/Desktop") == "/Users/x/Desktop/hanabi-settings.json");
    CHECK(st::export_path("/Users/x/Desktop/") == "/Users/x/Desktop/hanabi-settings.json");
    CHECK(st::export_path("") == "hanabi-settings.json");
    CHECK(st::inspect("") == st::Verdict::Empty);
    CHECK(st::inspect("  \n\t ") == st::Verdict::Empty);
    CHECK(st::inspect("[1,2]") == st::Verdict::NotJsonObject);
    CHECK(st::inspect("hello") == st::Verdict::NotJsonObject);
    CHECK(st::inspect("{\"foo\": 1}") == st::Verdict::NotSettings);
    CHECK(st::inspect("{\"theme\": \"dark\"}") == st::Verdict::Ok);
    CHECK(st::inspect("\n  {\"window_width\": 1100}") == st::Verdict::Ok);
    CHECK(st::inspect("{\"sound_override\": {}}") == st::Verdict::Ok);
    CHECK(st::reason(st::Verdict::Ok).empty());
    CHECK(st::reason(st::Verdict::Empty) == "the file is empty");
    CHECK(st::reason(st::Verdict::NotJsonObject) == "not a JSON object");
    CHECK(st::reason(st::Verdict::NotSettings) == "no Hanabi settings in it");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
