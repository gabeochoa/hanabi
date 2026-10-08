#include <cstdio>
#include <string_view>

#include "../../src/paste_image.h"

namespace pi = hanabi::paste_image;
using pi::Decision;

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
    constexpr std::uint64_t budget = 7ULL * 1024ULL * 1024ULL;
    CHECK(pi::kPreferred.front().uti == "public.png");
    CHECK(pi::kind_for("public.png")->extension == ".png" && !pi::kind_for("public.png")->animated);
    CHECK(pi::kind_for("public.jpeg")->extension == ".jpg" && !pi::kind_for("public.jpeg")->animated);
    CHECK(pi::kind_for("org.webmproject.webp")->extension == ".webp" && pi::kind_for("org.webmproject.webp")->animated);
    CHECK(pi::kind_for("com.compuserve.gif")->extension == ".gif" && pi::kind_for("com.compuserve.gif")->animated);
    CHECK(pi::kind_for("public.tiff") == nullptr);
    CHECK(pi::kind_for("public.heic") == nullptr);
    CHECK(pi::kind_for("") == nullptr);
    CHECK(pi::kFallbackExtension == ".png");

    CHECK(pi::decide("public.png", 1024, budget) == Decision::KeepOriginal);
    CHECK(pi::decide("com.compuserve.gif", budget, budget) == Decision::KeepOriginal);
    CHECK(pi::decide("org.webmproject.webp", budget - 1, budget) == Decision::KeepOriginal);
    CHECK(pi::decide("com.compuserve.gif", budget + 1, budget) == Decision::Refuse);
    CHECK(pi::decide("org.webmproject.webp", budget + 1, budget) == Decision::Refuse);
    CHECK(pi::decide("public.png", budget + 1, budget) == Decision::Reencode);
    CHECK(pi::decide("public.jpeg", budget + 1, budget) == Decision::Reencode);
    CHECK(pi::decide("public.tiff", 10, budget) == Decision::Reencode);
    CHECK(pi::decide("public.heic", 10, budget) == Decision::Reencode);
    CHECK(pi::decide("", 0, budget) == Decision::Reencode);
    for (const pi::Kind& k : pi::kPreferred) CHECK(!k.extension.empty() && k.extension.front() == '.');
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
