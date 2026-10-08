#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace hanabi::paste_image {

struct Kind {
    std::string_view uti;
    std::string_view extension;
    bool animated;
};

inline constexpr std::array<Kind, 4> kPreferred{{
    {"public.png", ".png", false},
    {"public.jpeg", ".jpg", false},
    {"org.webmproject.webp", ".webp", true},
    {"com.compuserve.gif", ".gif", true},
}};

inline constexpr std::string_view kFallbackExtension = ".png";

enum class Decision { KeepOriginal, Reencode, Refuse };

inline const Kind* kind_for(std::string_view uti) {
    for (const Kind& k : kPreferred)
        if (k.uti == uti) return &k;
    return nullptr;
}

inline constexpr std::string_view kRefusalSentence =
    "That animated image is too large to send (limit 7 MB); it was not flattened.";

inline Decision decide(std::string_view uti, std::uint64_t bytes, std::uint64_t budget) {
    const Kind* k = kind_for(uti);
    if (k == nullptr) return Decision::Reencode;
    if (bytes <= budget) return Decision::KeepOriginal;
    return k->animated ? Decision::Refuse : Decision::Reencode;
}

}
