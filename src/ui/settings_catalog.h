#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::settings_catalog {

enum class Pane {
    General,
    Appearance,
    Chat,
    Notifications,
    Storage,
    Connection,
    Model,
    Shortcuts,
    Commands,
    About,
};

inline constexpr size_t kPaneCount = 10;

enum class Group { App, Backend, Input, System };

struct PaneInfo {
    Pane pane;
    Group group;
    const char* label;    // what the navigation row reads
    const char* slug;     // stable id: persisted, and the deep-link target
    const char* summary;  // the line under the pane's own title
};

inline constexpr std::array<PaneInfo, kPaneCount> kPanes{{
    {Pane::General, Group::App, "General", "general",
     "Sending, timestamps, and what comes back on restart"},
    {Pane::Appearance, Group::App, "Appearance", "appearance",
     "Theme, typeface, and the two colours you can name"},
    {Pane::Chat, Group::App, "Chat", "chat",
     "What a transcript shows you, and how much of it"},
    {Pane::Notifications, Group::App, "Notifications", "notifications",
     "When this Mac may interrupt you, and when it may not"},
    {Pane::Storage, Group::Backend, "Storage", "storage",
     "What is kept on this disk, and your own copies of it"},
    {Pane::Connection, Group::Backend, "Connection", "connection",
     "Which backend this window talks to, and who it thinks you are"},
    {Pane::Model, Group::Backend, "Model", "model",
     "Which model a new conversation starts on, and how hard it thinks"},
    {Pane::Shortcuts, Group::Input, "Shortcuts", "shortcuts",
     "Chords you can rebind, and the ones the system owns"},
    {Pane::Commands, Group::Input, "Commands", "commands",
     "What a slash in the composer can do here, and what it cannot yet"},
    {Pane::About, Group::System, "About", "about",
     "Version, and what this build carries"},
}};

inline constexpr const char* group_heading(Group g) {
    switch (g) {
        case Group::App: return "APP";
        case Group::Backend: return "BACKEND";
        case Group::Input: return "INPUT";
        case Group::System: return "SYSTEM";
    }
    return "APP";
}

inline const PaneInfo& pane_info(Pane p) {
    for (const PaneInfo& m : kPanes)
        if (m.pane == p) return m;
    return kPanes[0];
}

inline int pane_index(Pane p) {
    for (size_t i = 0; i < kPanes.size(); ++i)
        if (kPanes[i].pane == p) return static_cast<int>(i);
    return 0;
}

inline Pane pane_at(int index) {
    if (index < 0) index = 0;
    if (index >= static_cast<int>(kPaneCount))
        index = static_cast<int>(kPaneCount) - 1;
    return kPanes[static_cast<size_t>(index)].pane;
}

inline Pane pane_from_slug(std::string_view slug) {
    for (const PaneInfo& m : kPanes)
        if (slug == m.slug) return m.pane;
    return Pane::General;
}

inline bool is_known_slug(std::string_view slug) {
    for (const PaneInfo& m : kPanes)
        if (slug == m.slug) return true;
    return false;
}

enum class Origin { Device, Account, Readout };

inline constexpr const char* origin_mark(Origin o) {
    switch (o) {
        case Origin::Device: return "This Mac";
        case Origin::Account: return "Your account";
        case Origin::Readout: return "Read-only";
    }
    return "This Mac";
}

inline constexpr const char* origin_help(Origin o) {
    switch (o) {
        case Origin::Device:
            return "kept here only";
        case Origin::Account:
            return "follows your account";
        case Origin::Readout:
            return "shown, not settable";
    }
    return "";
}

struct Row {
    const char* id;
    Pane pane;
    const char* title;
    const char* keywords;
    Origin origin;
};

inline constexpr std::array<Row, 39> kRows{{
    {"send_key", Pane::General, "Send message with",
     "return enter submit send keyboard chord newline", Origin::Device},
    {"new_line", Pane::General, "New line with",
     "return enter newline break paragraph shift", Origin::Readout},
    {"restore_tabs", Pane::General, "Restore open tabs on restart",
     "restore reopen relaunch startup launch windows tabs session",
     Origin::Device},
    {"timestamps", Pane::General, "Show timestamps",
     "time clock when sent stamp date", Origin::Device},

    {"theme_rotate", Pane::Appearance, "Rotate theme",
     "rotate cycle interval timer automatic switch", Origin::Device},
    {"font", Pane::Appearance, "App font",
     "typeface family text letters serif mono", Origin::Device},
    {"font_weight", Pane::Appearance, "Font weight",
     "bold regular light emphasis heavier thinner", Origin::Device},
    {"palette", Pane::Appearance, "Palette",
     "palette theme dark light system named preview appearance colour color "
     "mode night",
     Origin::Device},
    {"user_font", Pane::Appearance, "Your messages",
     "typeface font family user side messages mine", Origin::Device},
    {"assistant_font", Pane::Appearance, "Replies",
     "typeface font family assistant replies side answers", Origin::Device},
    {"accent", Pane::Appearance, "Accent",
     "accent colour color highlight tint blue swatch", Origin::Device},
    {"highlight", Pane::Appearance, "Find highlight",
     "find search highlight match colour color yellow swatch",
     Origin::Device},

    {"reasoning", Pane::Chat, "Reasoning blocks",
     "reasoning thinking thought chain hide show", Origin::Device},
    {"fold_long", Pane::Chat, "Long messages",
     "fold collapse truncate show more long wall", Origin::Device},
    {"date_dividers", Pane::Chat, "Date dividers",
     "date divider day separator calendar", Origin::Device},
    {"subagents", Pane::Chat, "Finished sub-agents",
     "subagent sub-agent child helper chip finished hide", Origin::Device},
    {"yap", Pane::Chat, "Reply length",
     "yap verbose verbosity brief terse chatty length", Origin::Account},
    {"jump_latest", Pane::Chat, "Jump to the latest message when you return",
     "jump latest bottom scroll follow return newest tail", Origin::Device},
    {"minimap", Pane::Chat, "Chat minimap",
     "minimap overview map scrollbar marks miniature", Origin::Device},
    {"minimap_marks", Pane::Chat, "Marks on the minimap",
     "minimap marks kinds filter tools replies asks events quiet",
     Origin::Device},

    {"notify_show", Pane::Notifications, "Show notifications",
     "notification notify alert banner interrupt post", Origin::Device},
    {"notify_sound", Pane::Notifications, "Play a sound",
     "sound audio ping chime noise alert", Origin::Account},
    {"quiet_hours", Pane::Notifications, "Quiet hours",
     "quiet silence night dnd disturb window hours", Origin::Device},
    {"run_chime", Pane::Notifications, "Chime when a run finishes",
     "chime sound cue finished complete done bell", Origin::Device},
    {"notify_subagents", Pane::Notifications, "Notify me about sub-agents",
     "subagent sub-agent child helper notify alert", Origin::Device},

    {"disk_usage", Pane::Storage, "On disk",
     "disk cache storage space bytes clear wipe usage", Origin::Device},
    {"disk_cap", Pane::Storage, "Keep at most on disk",
     "cap limit maximum quota size disk cache bytes", Origin::Device},
    {"export", Pane::Storage, "Export conversations",
     "export markdown copy save backup folder own", Origin::Device},

    {"usage_data", Pane::Connection, "Send settings to your account",
     "usage telemetry privacy sync push account share data", Origin::Device},
    {"endpoint", Pane::Connection, "Endpoint",
     "endpoint host url backend server address dial", Origin::Readout},
    {"identity", Pane::Connection, "Signed in as",
     "identity account user who login sign auth", Origin::Readout},
    {"memory_backend", Pane::Connection, "Memory backend",
     "memory backend recall store traditional", Origin::Account},
    {"auto_archive", Pane::Connection, "Auto-archive",
     "archive age days old automatic tidy", Origin::Account},

    {"default_model", Pane::Model, "Default model",
     "model default server opus fable sonnet gpt muse avocado pick new "
     "conversation start",
     Origin::Account},
    {"default_effort", Pane::Model, "Thinking effort",
     "effort reasoning thinking low medium high xhigh max ladder slow fast",
     Origin::Device},

    {"global_chords", Pane::Shortcuts, "Global shortcuts",
     "global anywhere desktop chord hotkey summon launcher", Origin::Device},
    {"command_chords", Pane::Shortcuts, "Command shortcuts",
     "shortcut chord keyboard key rebind record reset binding",
     Origin::Device},
    {"slash_commands", Pane::Commands, "Slash commands",
     "slash command verb new model effort btw compact fork composer menu "
     "type",
     Origin::Readout},
    {"version", Pane::About, "Version",
     "version build about release number", Origin::Readout},
}};

inline constexpr bool pane_is_rowless(Pane) { return false; }

inline const Row* find_row(std::string_view id) {
    for (const Row& r : kRows)
        if (id == r.id) return &r;
    return nullptr;
}

inline std::vector<const Row*> rows_in(Pane pane) {
    std::vector<const Row*> out;
    for (const Row& r : kRows)
        if (r.pane == pane) out.push_back(&r);
    return out;
}

inline constexpr int kScoreTitleExact = 1000;
inline constexpr int kScoreTitlePrefix = 800;
inline constexpr int kScoreTitleWord = 600;
inline constexpr int kScoreTitleSubstring = 400;
inline constexpr int kScoreKeywordWord = 300;
inline constexpr int kScoreKeywordSubstring = 200;
inline constexpr int kScorePaneLabel = 120;
inline constexpr int kScoreOriginLabel = 60;

enum class MatchField { Title, Keyword, PaneLabel, OriginLabel };

struct Hit {
    const Row* row;
    int score;
    MatchField field;
};

inline std::string fold(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in)
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string normalize_query(std::string_view raw) {
    size_t b = 0, e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    return fold(raw.substr(b, e - b));
}

inline bool word_prefix(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return false;
    size_t at = hay.find(needle);
    while (at != std::string_view::npos) {
        const bool atWordStart =
            at == 0 || hay[at - 1] == ' ' || hay[at - 1] == '-' ||
            hay[at - 1] == '/';
        if (atWordStart) return true;
        at = hay.find(needle, at + 1);
    }
    return false;
}

inline std::vector<Hit> search(std::string_view rawQuery) {
    const std::string q = normalize_query(rawQuery);
    std::vector<Hit> hits;
    if (q.empty()) return hits;

    for (const Row& r : kRows) {
        const std::string title = fold(r.title);
        const std::string keys = fold(r.keywords);
        const std::string pane = fold(pane_info(r.pane).label);
        const std::string origin = fold(origin_mark(r.origin));

        int score = 0;
        MatchField field = MatchField::Title;
        if (title == q) {
            score = kScoreTitleExact;
        } else if (title.rfind(q, 0) == 0) {
            score = kScoreTitlePrefix;
        } else if (word_prefix(title, q)) {
            score = kScoreTitleWord;
        } else if (title.find(q) != std::string::npos) {
            score = kScoreTitleSubstring;
        } else if (word_prefix(keys, q)) {
            score = kScoreKeywordWord;
            field = MatchField::Keyword;
        } else if (keys.find(q) != std::string::npos) {
            score = kScoreKeywordSubstring;
            field = MatchField::Keyword;
        } else if (pane.find(q) != std::string::npos) {
            score = kScorePaneLabel;
            field = MatchField::PaneLabel;
        } else if (origin.find(q) != std::string::npos) {
            score = kScoreOriginLabel;
            field = MatchField::OriginLabel;
        }
        if (score > 0) hits.push_back(Hit{&r, score, field});
    }

    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) {
                         return a.score > b.score;
                     });
    return hits;
}

inline float reveal_offset(float rowTop, float viewTop, float rowHeight,
                           float viewHeight, float currentOffset) {
    const float relative = rowTop - viewTop + currentOffset;
    const float centred = relative - (viewHeight - rowHeight) * 0.5f;
    return centred < 0.0f ? 0.0f : centred;
}

enum class Zone { Search, Nav, Content };

struct Focus {
    Zone zone = Zone::Search;
    int index = 0;

    friend bool operator==(const Focus& a, const Focus& b) {
        return a.zone == b.zone && a.index == b.index;
    }
};

struct Stops {
    int nav = static_cast<int>(kPaneCount);
    int content = 0;
    bool searching = false;
    int results = 0;

    int nav_stops() const { return searching ? results : nav; }
    int content_stops() const { return searching ? 0 : content; }
};

inline Focus clamp_focus(Focus f, const Stops& s) {
    if (f.zone == Zone::Nav) {
        const int n = s.nav_stops();
        if (n <= 0) return Focus{Zone::Search, 0};
        f.index = std::clamp(f.index, 0, n - 1);
        return f;
    }
    if (f.zone == Zone::Content) {
        const int n = s.content_stops();
        if (n <= 0) return Focus{Zone::Search, 0};
        f.index = std::clamp(f.index, 0, n - 1);
        return f;
    }
    return Focus{Zone::Search, 0};
}

inline Focus focus_next(Focus f, const Stops& s) {
    f = clamp_focus(f, s);
    switch (f.zone) {
        case Zone::Search:
            if (s.nav_stops() > 0) return Focus{Zone::Nav, 0};
            if (s.content_stops() > 0) return Focus{Zone::Content, 0};
            return Focus{Zone::Search, 0};
        case Zone::Nav:
            if (f.index + 1 < s.nav_stops())
                return Focus{Zone::Nav, f.index + 1};
            if (s.content_stops() > 0) return Focus{Zone::Content, 0};
            return Focus{Zone::Search, 0};
        case Zone::Content:
            if (f.index + 1 < s.content_stops())
                return Focus{Zone::Content, f.index + 1};
            return Focus{Zone::Search, 0};
    }
    return Focus{Zone::Search, 0};
}

inline Focus focus_prev(Focus f, const Stops& s) {
    f = clamp_focus(f, s);
    switch (f.zone) {
        case Zone::Search:
            if (s.content_stops() > 0)
                return Focus{Zone::Content, s.content_stops() - 1};
            if (s.nav_stops() > 0)
                return Focus{Zone::Nav, s.nav_stops() - 1};
            return Focus{Zone::Search, 0};
        case Zone::Nav:
            if (f.index > 0) return Focus{Zone::Nav, f.index - 1};
            return Focus{Zone::Search, 0};
        case Zone::Content:
            if (f.index > 0) return Focus{Zone::Content, f.index - 1};
            if (s.nav_stops() > 0)
                return Focus{Zone::Nav, s.nav_stops() - 1};
            return Focus{Zone::Search, 0};
    }
    return Focus{Zone::Search, 0};
}

inline Focus focus_step_in_zone(Focus f, const Stops& s, int delta) {
    f = clamp_focus(f, s);
    if (f.zone == Zone::Search) return f;
    const int n = f.zone == Zone::Nav ? s.nav_stops() : s.content_stops();
    if (n <= 0) return Focus{Zone::Search, 0};
    f.index = std::clamp(f.index + delta, 0, n - 1);
    return f;
}

}  // namespace hanabi::settings_catalog
