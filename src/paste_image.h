#pragma once

#include <array>
#include <string_view>

namespace hanabi::paste_image {

struct Kind {
    std::string_view uti;
    std::string_view extension;
};

inline constexpr std::array<Kind, 5> kPreferred{{
    {"public.png", ".png"},
    {"com.compuserve.gif", ".gif"},
    {"org.webmproject.webp", ".webp"},
    {"public.jpeg", ".jpg"},
    {"public.heic", ".heic"},
}};

inline constexpr std::string_view kFallbackExtension = ".png";

inline std::string_view extension_for(std::string_view uti) {
    for (const Kind& k : kPreferred)
        if (k.uti == uti) return k.extension;
    return {};
}

}
