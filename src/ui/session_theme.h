#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::session_theme {

struct Choice {
    std::string mode;    // "", "light", "dark"
    std::string accent;  // "", or an accent swatch key
    [[nodiscard]] bool inherits() const { return mode.empty() && accent.empty(); }
    bool operator==(const Choice&) const = default;
};

inline bool is_mode(std::string_view s) { return s == "light" || s == "dark"; }

inline std::string serialize(const Choice& c) {
    if (c.inherits()) return "";
    if (c.accent.empty()) return c.mode;
    if (c.mode.empty()) return c.accent;
    return c.mode + "+" + c.accent;
}

inline Choice parse(std::string_view s) {
    Choice c;
    if (s.empty() || s == "inherit") return c;
    const std::size_t plus = s.find('+');
    const std::string_view a = s.substr(0, plus);
    const std::string_view b = plus == std::string_view::npos ? std::string_view{} : s.substr(plus + 1);
    for (std::string_view part : {a, b}) {
        if (part.empty()) continue;
        if (is_mode(part)) c.mode = std::string(part);
        else c.accent = std::string(part);
    }
    return c;
}

inline Choice with_mode(Choice c, std::string_view mode) {
    c.mode = std::string(mode);
    return c;
}

inline Choice with_accent(Choice c, std::string_view accent) {
    c.accent = std::string(accent);
    return c;
}

struct Resolved {
    bool light = false;
    std::string accent;
    bool own = false;
};

inline Resolved resolve(const Choice& session, bool appLight, std::string_view appAccent) {
    Resolved r;
    r.light = session.mode.empty() ? appLight : session.mode == "light";
    r.accent = session.accent.empty() ? std::string(appAccent) : session.accent;
    r.own = !session.inherits();
    return r;
}

inline constexpr const char* kActionPrefix = "theme:";

inline std::string action_id(std::string_view key) { return std::string(kActionPrefix) + std::string(key); }

inline std::optional<std::string> picked_key(std::string_view action) {
    const std::string_view p = kActionPrefix;
    if (action.rfind(p, 0) != 0) return std::nullopt;
    return std::string(action.substr(p.size()));
}

inline Choice apply_pick(const Choice& current, std::string_view key) {
    if (key == "inherit") return Choice{};
    if (is_mode(key)) return with_mode(current, key);
    if (key == "accent-inherit") return with_accent(current, "");
    return with_accent(current, key);
}

inline std::string label(const Choice& c, std::string_view accentName) {
    if (c.inherits()) return "Theme: Default";
    std::string out = "Theme: ";
    if (!c.mode.empty()) out += c.mode == "light" ? "Light" : "Dark";
    if (!c.accent.empty()) {
        if (!c.mode.empty()) out += " \xc2\xb7 ";
        out += accentName;
    }
    return out;
}

}
