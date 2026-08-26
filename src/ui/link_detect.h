#pragma once

// ---------------------------------------------------------------------------
// Work-tracker ids in message text, made clickable.
//
// "See D948120 for the diff" is a reference to something that lives elsewhere,
// and the transcript printed it as prose. This finds those ids, draws them as
// links, and opens the one that was clicked.
//
// WHERE A LINK IS ON SCREEN. The app does not lay the text out; afterhours
// does, inside draw_text_in_rect, and there is still no "where did byte N of
// this label land" query (afterhours_gaps.md #51). So the position of an id
// inside a wrapped line is re-derived the way find_highlight.h established and
// text_select.h extended: call afterhours' OWN wrapping primitive with the
// same inputs the renderer used, then apply the renderer's positioning
// arithmetic. This file does not repeat that arithmetic a third time — it
// calls text_select::detail::layout_of, so all three features move together
// when the renderer's constants change.
//
// WHAT AN ID LINKS TO. Nothing, unless this deployment has been told where its
// tracker lives (api::Config::tracker_base_url / HANABI_TRACKER_BASE_URL). No
// host is compiled in, and with no host configured an id stays PLAIN TEXT: it
// is not underlined, not coloured, and not clickable. A link that opens
// nothing is worse than the prose it replaced — it promises somewhere to go
// and then wastes the click.
// ---------------------------------------------------------------------------

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "text_select.h"
#include "theme.h"

namespace hanabi::links {

// One id found in a piece of text: where it starts, how long it is, and the
// id itself.
struct Link {
    size_t off = 0;
    size_t len = 0;
    std::string id;
};

// The three prefixes this client knows: a diff/task (D), a task (T), an
// incident (S). Anything else is prose.
inline bool is_prefix(char c) { return c == 'D' || c == 'T' || c == 'S'; }

// Digits an id needs before it is an id rather than a word. Three is the
// shortest real tracker number; two would make "T12" out of half the acronyms
// in an engineering conversation.
inline constexpr size_t kMinDigits = 3;

// A character that binds to what follows it, so what follows is part of a
// bigger token and not an id of its own: the D in "ABCD1234" and the T in
// "https://host/T123" are both somebody else's letters.
inline bool binds(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '/' ||
           c == '_' || c == '.' || c == '-';
}

inline std::vector<Link> find(const std::string& text) {
    std::vector<Link> out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (!is_prefix(text[i])) continue;
        if (i > 0 && binds(text[i - 1])) continue;
        size_t j = i + 1;
        while (j < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[j])) != 0)
            ++j;
        const size_t digits = j - i - 1;
        if (digits < kMinDigits) continue;
        // A trailing letter means the run is a word that happens to start
        // this way ("T12345th"), not an id.
        if (j < text.size() &&
            std::isalnum(static_cast<unsigned char>(text[j])) != 0)
            continue;
        out.push_back(Link{i, j - i, text.substr(i, j - i)});
        i = j - 1;
    }
    return out;
}

// The URL an id opens. Empty base -> empty URL -> no link at all; every caller
// treats an empty URL as "this is prose".
inline std::string url_for(const std::string& base, const std::string& id) {
    if (base.empty() || id.empty()) return "";
    if (base.back() == '/') return base + id;
    return base + "/" + id;
}

// The on-screen rectangles a byte range occupies inside `rect`, one per
// wrapped line it crosses. Empty when the font manager is not up yet or the
// text does not lay out.
inline std::vector<RectangleType> rects_for(RectangleType rect,
                                            const std::string& text,
                                            size_t off, size_t len,
                                            float fontPx) {
    std::vector<RectangleType> out;
    const auto lay = hanabi::text_select::detail::layout_of(rect, text, fontPx);
    if (!lay.ok) return out;
    auto* fm = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (fm == nullptr) return out;
    const afterhours::Font font = fm->get_active_font();
    const auto measure = [&](const std::string& s) {
        return hanabi::atlas::check(
            s, fontPx,
            afterhours::measure_text(font, s.c_str(), fontPx, 1.0f).x);
    };
    const size_t from = off;
    const size_t to = off + len;
    size_t pos = 0;
    for (size_t i = 0; i < lay.lines.size(); ++i) {
        const std::string& ln = lay.lines[i];
        const size_t at = text.find(ln, pos);
        if (at == std::string::npos) break;
        const size_t lineBegin = at;
        const size_t lineEnd = at + ln.size();
        pos = lineEnd;
        if (to <= lineBegin || from >= lineEnd) continue;
        const size_t a = from > lineBegin ? from - lineBegin : 0;
        const size_t b = to < lineEnd ? to - lineBegin : ln.size();
        if (b <= a) continue;
        const float xa = lay.x0 + measure(ln.substr(0, a));
        const float xb = lay.x0 + measure(ln.substr(0, b));
        out.push_back(RectangleType{
            xa, lay.y0 + lay.lineH * static_cast<float>(i), xb - xa,
            lay.lineH});
    }
    return out;
}

// WHERE A LINK ACTUALLY LANDED, for the scripted-UI harness only.
//
// A .e2e script cannot ask for a byte range's position (gap #51), so
// tests/ui/tracker_links.e2e pinned the pixel it had measured — and a pinned
// pixel is a coordinate that rots. It rotted twice: once by 321px when the
// pane lost its title header, once by 26px when six feature branches moved
// every transcript body line down, and each time the test read as "the link
// feature is broken". draw_underlines already derives the rect for every link
// it paints, so the e2e build keeps the last one per id and the `click_link`
// command aims at its centre. Nothing outside the e2e binary compiles this,
// and a script that names an id that is not on screen fails saying which ids
// were.
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
inline std::map<std::string, RectangleType>& painted_rects() {
    static std::map<std::string, RectangleType> m;
    return m;
}

inline void record_painted(const std::string& id, const RectangleType& r) {
    painted_rects()[id] = r;
}
#endif

// Underline every link in `text` as it is laid out inside `rect`. Called from
// on_draw_fg so the rule sits over the element's own fill, under nothing.
inline void draw_underlines(RectangleType rect, const std::string& text,
                            const std::vector<Link>& links, float fontPx) {
    if (links.empty()) return;
    const theme::Color c = theme::link();
    for (const Link& l : links)
        for (const RectangleType& r : rects_for(rect, text, l.off, l.len,
                                                fontPx)) {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
            record_painted(l.id, r);
#endif
            const float y = r.y + r.height - 2.0f;
            afterhours::draw_line_ex(afterhours::vec2{r.x, y},
                                     afterhours::vec2{r.x + r.width, y}, 1.0f,
                                     c);
        }
}

// Every rect a click was tested against, printed when HANABI_LINK_AUDIT is
// set. There is no way to ask where a byte range landed (afterhours_gaps.md
// #51), so three tests in tests/ui reach into the transcript by raw
// coordinate and a miss reads as "the feature is broken" rather than as "the
// layout moved". This is the reading that tells them apart, and it is what a
// re-measure should be done against rather than against a screenshot ruler.
inline void audit(const char* what, const std::string& id, float px, float py,
                  const RectangleType& r) {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_LINK_AUDIT");
        return v != nullptr && *v != '\0' && std::string(v) != "0";
    }();
    if (!on) return;
    std::printf("[link] %-4s id=%-12s point=(%.1f,%.1f)  rect=(%.1f,%.1f "
                "%.1fx%.1f)\n",
                what, id.c_str(), px, py, r.x, r.y, r.width, r.height);
    std::fflush(stdout);
}

// Which link, if any, is under (px, py). Empty id when the point is on prose.
inline std::string hit(RectangleType rect, const std::string& text,
                       const std::vector<Link>& links, float fontPx, float px,
                       float py) {
    for (const Link& l : links)
        for (const RectangleType& r : rects_for(rect, text, l.off, l.len,
                                                fontPx)) {
            const bool in = px >= r.x && px <= r.x + r.width && py >= r.y &&
                            py <= r.y + r.height;
            audit(in ? "HIT" : "miss", l.id, px, py, r);
            if (in) return l.id;
        }
    return "";
}

// Opening a URL is a native call, and the two paths that render without a user
// in front of them (the screenshot capture and the scripted-UI harness) must
// not hand a browser to nobody. They set headless(), which makes open() record
// the URL and stop; the app still says what it did, so a test can assert on
// the behaviour without launching anything.
inline bool& headless() {
    static bool v = false;
    return v;
}
inline std::string& last_opened() {
    static std::string s;
    return s;
}

// Declared here rather than by including native_extras.h so this header stays
// linkable from a test TU that has no AppKit in it; the definition lives in
// native_extras.mm, which both real binaries compile.
extern "C" void native_open_url(const char* url);

// Open what an id points at. Records it either way, so what the app decided
// is inspectable whether or not a browser was there to take it.
inline void open(const std::string& url) {
    if (url.empty()) return;
    last_opened() = url;
    if (headless()) return;
#if defined(__APPLE__)
    native_open_url(url.c_str());
#endif
}

}  // namespace hanabi::links
