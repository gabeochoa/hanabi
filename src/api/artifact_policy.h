#pragma once

#include <string>
#include <string_view>

#include "disk_cache.h"
#include "types.h"

namespace hanabi::artifact_policy {

inline bool drawable(std::string_view media_type) {
    api::ArtifactRef probe;
    probe.media_type = std::string(media_type);
    return probe.is_image();
}

inline std::string unsupported_reason(std::string_view media_type) {
    if (media_type.rfind("audio/", 0) == 0) return "audio is not played in this build";
    return "unsupported type " + std::string(media_type);
}

// The row's state from its ref alone: hidden -> a known undrawable type is
// unavailable whatever bytes are cached -> cached bytes are ready -> too
// large -> needs a fetch (false).
inline bool settle_without_fetch(const api::ArtifactRef& ref, api::ArtifactFetch& state,
                                 std::string& reason) {
    reason.clear();
    if (ref.hidden) {
        state = api::ArtifactFetch::Idle;
        return true;
    }
    if (ref.has_metadata() && !drawable(ref.media_type)) {
        state = api::ArtifactFetch::Unavailable;
        reason = unsupported_reason(ref.media_type);
        return true;
    }
    if (!ref.local_path.empty()) {
        state = api::ArtifactFetch::Ready;
        return true;
    }
    if (ref.size_bytes > api::disk_cache::kArtifactMaxBytes) {
        state = api::ArtifactFetch::Unavailable;
        reason = "larger than 32 MB; open in the web app";
        return true;
    }
    return false;
}

}  // namespace hanabi::artifact_policy
