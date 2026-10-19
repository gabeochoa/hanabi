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

// Audio the player can play (AVFoundation's formats). Fetched like an image,
// then played from the row (ui/audio_player.h). Other audio -- webm, ogg --
// says it cannot be played here.
inline bool playable_audio(std::string_view media_type) {
    return media_type == "audio/wav" || media_type == "audio/x-wav" || media_type == "audio/wave" ||
           media_type == "audio/mpeg" || media_type == "audio/mp3" || media_type == "audio/mp4" ||
           media_type == "audio/x-m4a" || media_type == "audio/aac" || media_type == "audio/aiff" ||
           media_type == "audio/x-aiff";
}

// What a row can show: a picture, or a clip it can play.
inline bool ends_with(std::string_view s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
inline std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
inline std::string preview_kind(std::string_view media_type, std::string_view file) {
    const std::string mt = lower_ascii(media_type);
    const std::string f = lower_ascii(file);
    if (mt == "text/csv" || mt == "text/tab-separated-values" || mt == "text/tsv") return "csv";
    if (mt == "application/zip" || mt == "application/x-zip-compressed" || mt == "application/x-tar" ||
        mt == "application/gzip" || mt == "application/x-gzip" || mt == "application/x-tgz" ||
        mt == "application/x-gtar")
        return "archive";
    if (mt.empty() || mt == "application/octet-stream" || mt == "text/plain") {
        if (ends_with(f, ".csv") || ends_with(f, ".tsv")) return "csv";
        if (ends_with(f, ".zip") || ends_with(f, ".tar") || ends_with(f, ".tgz") || ends_with(f, ".tar.gz") ||
            ends_with(f, ".gz"))
            return "archive";
    }
    return {};
}
inline bool fetchable(std::string_view media_type, std::string_view file = {}) {
    return drawable(media_type) || playable_audio(media_type) || !preview_kind(media_type, file).empty();
}

inline std::string unsupported_reason(std::string_view media_type) {
    if (media_type.rfind("audio/", 0) == 0) return "this audio format cannot be played here";
    return "unsupported type " + std::string(media_type);
}

inline bool settle_without_fetch(const api::ArtifactRef& ref, api::ArtifactFetch& state,
                                 std::string& reason) {
    reason.clear();
    if (ref.hidden) {
        state = api::ArtifactFetch::Idle;
        return true;
    }
    if (ref.has_metadata() && !fetchable(ref.media_type, ref.file)) {
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

}
