#include <cstdio>
#include <string>

#include "../../src/api/artifact_policy.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::ArtifactFetch;
namespace ap = hanabi::artifact_policy;

static api::ArtifactRef ref(const char* mime, const char* local = "") {
    api::ArtifactRef r;
    r.id = "art-1";
    r.version = "v1";
    r.file = "f";
    r.media_type = mime;
    r.local_path = local;
    return r;
}

int main() {
    std::printf("=== test_artifact_policy ===\n");
    ArtifactFetch st{};
    std::string why;

    CHECK(ap::settle_without_fetch(ref("audio/wav", "/cache/artifacts/a.wav"), st, why));
    CHECK(st == ArtifactFetch::Unavailable && why == "audio is not played in this build");
    CHECK(ap::settle_without_fetch(ref("audio/wav"), st, why));
    CHECK(st == ArtifactFetch::Unavailable && why == "audio is not played in this build");

    CHECK(ap::settle_without_fetch(ref("image/png", "/cache/artifacts/a.png"), st, why));
    CHECK(st == ArtifactFetch::Ready && why.empty());

    CHECK(ap::settle_without_fetch(ref("application/pdf", "/cache/artifacts/a.pdf"), st, why));
    CHECK(st == ArtifactFetch::Unavailable && why == "unsupported type application/pdf");

    auto h = ref("image/png", "/cache/artifacts/a.png");
    h.hidden = true;
    CHECK(ap::settle_without_fetch(h, st, why));
    CHECK(st == ArtifactFetch::Idle && why.empty());

    CHECK(!ap::settle_without_fetch(ref(""), st, why));
    CHECK(ap::settle_without_fetch(ref("", "/cache/artifacts/x.png"), st, why));
    CHECK(st == ArtifactFetch::Ready);

    auto big = ref("image/png");
    big.size_bytes = api::disk_cache::kArtifactMaxBytes + 1;
    CHECK(ap::settle_without_fetch(big, st, why));
    CHECK(st == ArtifactFetch::Unavailable && why == "larger than 32 MB; open in the web app");

    CHECK(!ap::settle_without_fetch(ref("image/png"), st, why));

    CHECK(ap::drawable("image/png") && ap::drawable("image/webp") && !ap::drawable("audio/mpeg") &&
          !ap::drawable("") && !ap::drawable("text/plain"));

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
