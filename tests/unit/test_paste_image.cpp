#include <cstdio>
#include <string_view>

#include "../../src/paste_image.h"

namespace pi = hanabi::paste_image;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_paste_image ===\n");
    CHECK(pi::kPreferred.front().uti == "public.png");
    CHECK(pi::extension_for("public.png") == ".png");
    CHECK(pi::extension_for("com.compuserve.gif") == ".gif");
    CHECK(pi::extension_for("org.webmproject.webp") == ".webp");
    CHECK(pi::extension_for("public.jpeg") == ".jpg");
    CHECK(pi::extension_for("public.heic") == ".heic");
    CHECK(pi::extension_for("public.tiff").empty());
    CHECK(pi::extension_for("").empty());
    CHECK(pi::kFallbackExtension == ".png");
    for (const pi::Kind& k : pi::kPreferred) {
        CHECK(!k.extension.empty() && k.extension.front() == '.');
        CHECK(pi::extension_for(k.uti) == k.extension);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
