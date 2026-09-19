#pragma once

#include <string>
#include <string_view>

namespace hanabi::artifact_sniff {

inline std::string media_type(std::string_view b) {
    const auto starts = [&](std::string_view sig, std::size_t at = 0) {
        return b.size() >= at + sig.size() && b.substr(at, sig.size()) == sig;
    };
    if (starts("\x89PNG\r\n\x1a\n")) return "image/png";
    if (starts("\xFF\xD8\xFF")) return "image/jpeg";
    if (starts("GIF87a") || starts("GIF89a")) return "image/gif";
    if (starts("RIFF") && starts("WEBP", 8)) return "image/webp";
    if (starts("BM")) return "image/bmp";
    if (starts("RIFF") && starts("WAVE", 8)) return "audio/wav";
    if (starts("ID3") || starts("\xFF\xFB") || starts("\xFF\xF3") || starts("\xFF\xF2"))
        return "audio/mpeg";
    if (starts("ftyp", 4)) return "audio/mp4";
    return "";
}

inline bool usable(std::string_view media_type) {
    return !media_type.empty() && media_type != "application/octet-stream" &&
           media_type != "text/plain" && media_type != "binary/octet-stream";
}

}
