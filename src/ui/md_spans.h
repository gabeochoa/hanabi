#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <afterhours/src/plugins/ui/ui_core_components.h>

namespace hanabi::md {

struct Palette {
    afterhours::Color base;
    afterhours::Color code;
    afterhours::Color strong;
};

struct Spans {
    std::string visible;
    std::vector<afterhours::ui::TextSpan> spans;
};

namespace detail {

inline bool same_color(const afterhours::Color& a, const afterhours::Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline void push_run(Spans& out, std::string_view run,
                     const afterhours::Color& color) {
    if (run.empty()) return;
    out.visible.append(run);
    if (!out.spans.empty() && same_color(out.spans.back().color, color))
        out.spans.back().text.append(run);
    else
        out.spans.push_back(
            afterhours::ui::TextSpan{std::string(run), color});
}

}  // namespace detail

inline Spans inline_spans(std::string_view line, const Palette& palette) {
    Spans out;
    out.visible.reserve(line.size());
    const std::size_t n = line.size();
    std::size_t i = 0;
    while (i < n) {
        const std::size_t mark = line.find_first_of("`*_", i);
        if (mark == std::string_view::npos) {
            detail::push_run(out, line.substr(i), palette.base);
            break;
        }
        detail::push_run(out, line.substr(i, mark - i), palette.base);
        i = mark;

        if (line[i] == '`') {
            const std::size_t close = line.find('`', i + 1);
            if (close != std::string_view::npos && close > i + 1) {
                detail::push_run(out, line.substr(i + 1, close - i - 1),
                                 palette.code);
                i = close + 1;
                continue;
            }
        }

        bool paired = false;
        for (const std::string_view d : {std::string_view("**"),
                                         std::string_view("__")}) {
            if (line.compare(i, 2, d) != 0) continue;
            const std::size_t close = line.find(d, i + 2);
            if (close != std::string_view::npos && close > i + 2) {
                detail::push_run(out, line.substr(i + 2, close - i - 2),
                                 palette.strong);
                i = close + 2;
                paired = true;
            }
            break;
        }
        if (paired) continue;

        for (const char d : {'*', '_'}) {
            if (line[i] != d || i + 1 >= n || line[i + 1] == ' ') continue;
            const std::size_t close = line.find(d, i + 1);
            if (close != std::string_view::npos && close > i + 1) {
                detail::push_run(out, line.substr(i + 1, close - i - 1),
                                 palette.strong);
                i = close + 1;
                paired = true;
            }
            break;
        }
        if (paired) continue;

        detail::push_run(out, line.substr(i, 1), palette.base);
        ++i;
    }
    return out;
}

}  // namespace hanabi::md
