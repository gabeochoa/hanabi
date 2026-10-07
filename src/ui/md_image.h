#pragma once

// A markdown image in a message: `![alt](source)` on a line of its own (or as
// a list item's whole content) is a PICTURE, not prose (the reference's
// MarkdownImageView, kt-g07i: "an image that cannot load must SAY so").
// Before, Hanabi drew the raw `![alt](...)` text. Pure.
//
// Where the source points decides what is drawn:
//   - a file on this Mac (file://, or an absolute path that exists): the
//     picture itself, fit to the column;
//   - anything else is a chip naming the picture with a button to where it
//     can be seen -- an app-relative /api/... address resolves against the
//     web app (whose intern auth this app has no cookie for, the same wall the
//     reference documents), http(s) opens as written, and a bare repo path
//     opens in CodeHub (no server this app can ask serves repo bytes).

#include <optional>
#include <string>
#include <string_view>

namespace hanabi::md_image {

struct Image {
    std::string alt;
    std::string source;
};

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// The image a line IS, or nothing: the whole (trimmed) line, after an optional
// list marker, must be one `![alt](source)`; a title after the source
// (`![a](u "t")`) is dropped.
inline std::optional<Image> parse_line(std::string_view line) {
    std::string_view s = trim(line);
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') s = trim(s.substr(2));
    else if (s.substr(0, 4) == "\xe2\x80\xa2 ") s = trim(s.substr(4));  // a bullet already drawn as "•"
    else {
        std::size_t d = 0;
        while (d < s.size() && s[d] >= '0' && s[d] <= '9') ++d;
        if (d > 0 && d + 1 < s.size() && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') s = trim(s.substr(d + 2));
    }
    if (s.size() < 5 || s[0] != '!' || s[1] != '[' || s.back() != ')') return std::nullopt;
    const std::size_t close = s.find("](", 2);
    if (close == std::string_view::npos) return std::nullopt;
    std::string_view alt = s.substr(2, close - 2);
    if (alt.find(']') != std::string_view::npos) return std::nullopt;
    std::string_view src = s.substr(close + 2, s.size() - close - 3);
    src = trim(src);
    if (const std::size_t sp = src.find(' '); sp != std::string_view::npos) src = src.substr(0, sp);
    if (src.size() >= 2 && src.front() == '<' && src.back() == '>') src = src.substr(1, src.size() - 2);
    if (src.find(')') != std::string_view::npos) return std::nullopt;
    return Image{std::string(alt), std::string(src)};
}

enum class Kind { Local, Web, Repo, None };

struct Target {
    Kind kind = Kind::None;
    std::string where;  // a local path, or the URL the chip opens
};

inline bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// `exists` answers whether an absolute path is a file on this Mac (injected so
// this stays pure); `webBase` is the web app's origin.
template <typename Exists>
Target resolve(const std::string& source, const std::string& webBase, Exists&& exists) {
    if (source.empty()) return {};
    if (starts(source, "file://")) return {Kind::Local, source.substr(7)};
    if (starts(source, "http://") || starts(source, "https://")) return {Kind::Web, source};
    if (source[0] == '/') {
        if (!starts(source, "/api/") && exists(source)) return {Kind::Local, source};
        if (webBase.empty()) return {};
        std::string base = webBase;
        while (!base.empty() && base.back() == '/') base.pop_back();
        return {Kind::Web, base + source};
    }
    if (source.find("://") != std::string::npos || starts(source, "data:")) return {};
    return {Kind::Repo, "https://www.internalfb.com/code/fbsource/" + source};
}

// The name a chip shows: the alt text, or "Image" when there is none.
inline std::string name(const Image& im) {
    const std::string_view a = trim(im.alt);
    return a.empty() ? std::string("Image") : std::string(a);
}

// The host of an http(s) URL, lowercased; "" for anything else.
inline std::string host_of(std::string_view url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) return {};
    std::string_view rest = url.substr(scheme + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    std::string out(rest.substr(0, rest.find(':')));
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Where a web chip's button takes the reader, in words. A Pixelcloud post
// (pxl.cl redirects to a gated page, which is why it cannot be drawn: the
// reference's D123327745) says Pixelcloud; the web app's own addresses say
// the web app; any other address opens in the browser. `webBase` is the web
// app's origin; empty keeps the old wording.
inline std::string web_place(std::string_view where, std::string_view webBase) {
    const std::string host = host_of(where);
    if (host == "pxl.cl" || host.find("pixelcloud") != std::string::npos) return "Pixelcloud";
    if (webBase.empty() || host.empty() || host == host_of(webBase)) return "the web app";
    return "the browser";
}

inline std::string chip_label(const Image& im, Kind kind, std::string_view where = {},
                              std::string_view webBase = {}) {
    if (kind == Kind::None) return name(im) + " \xc2\xb7 this picture has no address Hanabi can open";
    const std::string place = kind == Kind::Repo ? std::string("CodeHub") : web_place(where, webBase);
    return name(im) + " \xc2\xb7 can't be shown here \xc2\xb7 open it in " + place;
}

}  // namespace hanabi::md_image
