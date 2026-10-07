#pragma once

// ---------------------------------------------------------------------------
// A ```chart fence body, parsed and laid out the way the reference parses it
// (ChartSpec.swift / ChartSpecLayout, which follow the web app's
// chartSpec.ts): a value of the wrong type is refused with a reason rather
// than coerced, and the reason is what the transcript shows above the fence's
// source when it does not draw (D123317236, kt-exwb).
//
// Spec shape:
//   {"type": "bar" | "line",
//    "title": "...",                       (optional)
//    "x": "label" | {"label": "..."},      (optional axis label)
//    "y": "label" | {"label": "..."},      (optional axis label)
//    "series": [{"name": "...",            (optional; "Series N")
//                "points": [{"x": "Mon" | 3, "y": 12.5}, ...]}, ...]}
//
// Pure: no UI, so tests/unit/test_chart_spec.cpp pins it. The transcript's
// chart block (main_pane_system.h render_chart_block) draws from it.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace hanabi::chart {

enum class Mark { Bar, Line };

struct Point {
    std::string label;
    double value = 0.0;
};

struct Series {
    std::string name;
    std::vector<Point> points;
};

struct Spec {
    Mark mark = Mark::Bar;
    std::vector<Series> series;
    std::optional<std::string> title;
    std::optional<std::string> xLabel;
    std::optional<std::string> yLabel;
};

// Either a spec or the reason it is not one.
struct Parse {
    std::optional<Spec> spec;
    std::string failure;
    [[nodiscard]] bool ok() const { return spec.has_value(); }
};

inline constexpr std::size_t kMaxSourceUtf16 = 64000;
inline constexpr std::size_t kMaxSeries = 8;
inline constexpr std::size_t kMaxPoints = 200;

inline bool is_chart_lang(std::string_view lang) {
    std::size_t b = 0, e = lang.size();
    while (b < e && (lang[b] == ' ' || lang[b] == '\t')) ++b;
    while (e > b && (lang[e - 1] == ' ' || lang[e - 1] == '\t')) --e;
    const std::string_view t = lang.substr(b, e - b);
    if (t.size() != 5) return false;
    for (std::size_t i = 0; i < 5; ++i)
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(t[i]))) != "chart"[i])
            return false;
    return true;
}

namespace detail {

// UTF-16 length of a UTF-8 string (astral code points count two).
inline std::size_t utf16_len(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n += len == 4 ? 2 : 1;
        i += len;
    }
    return n;
}

// Trimmed of the whitespace the reference trims (ASCII and the common Unicode
// spaces, including NBSP and the BOM); nullopt when nothing is left or the
// value is not a string.
inline std::optional<std::string> optional_text(const nlohmann::json* v) {
    if (v == nullptr || !v->is_string()) return std::nullopt;
    const std::string s = v->get<std::string>();
    static const char* const kSpaces[] = {
        "\t", "\n", "\v", "\f", "\r", " ", "\xc2\xa0", "\xe1\x9a\x80", "\xe2\x80\x80",
        "\xe2\x80\x81", "\xe2\x80\x82", "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85",
        "\xe2\x80\x86", "\xe2\x80\x87", "\xe2\x80\x88", "\xe2\x80\x89", "\xe2\x80\x8a",
        "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f", "\xe3\x80\x80",
        "\xef\xbb\xbf"};
    std::size_t b = 0, e = s.size();
    bool moved = true;
    while (moved && b < e) {
        moved = false;
        for (const char* sp : kSpaces) {
            const std::string_view w(sp);
            if (s.compare(b, w.size(), w) == 0 && b + w.size() <= e) {
                b += w.size();
                moved = true;
                break;
            }
        }
    }
    moved = true;
    while (moved && e > b) {
        moved = false;
        for (const char* sp : kSpaces) {
            const std::string_view w(sp);
            if (e - b >= w.size() && s.compare(e - w.size(), w.size(), w) == 0) {
                e -= w.size();
                moved = true;
                break;
            }
        }
    }
    if (e <= b) return std::nullopt;
    return s.substr(b, e - b);
}

inline const nlohmann::json* field(const nlohmann::json& o, const char* k) {
    const auto it = o.find(k);
    return it == o.end() ? nullptr : &*it;
}

inline std::optional<double> number(const nlohmann::json* v) {
    if (v == nullptr || !v->is_number()) return std::nullopt;  // booleans are not numbers here
    const double d = v->get<double>();
    if (!std::isfinite(d)) return std::nullopt;
    return d;
}

inline std::optional<std::string> axis_label(const nlohmann::json* v) {
    if (auto t = optional_text(v)) return t;
    if (v != nullptr && v->is_object()) return optional_text(field(*v, "label"));
    return std::nullopt;
}

}  // namespace detail

// The reference's numberLabel: the shortest round-trip digits, written out
// in full for exponents -6..20 and in e-notation outside them (JavaScript's
// Number#toString, which the web app prints).
inline std::string number_label(double value) {
    if (value == 0.0) return "0";
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::scientific);
    std::string sci(buf, res.ptr);  // "-d.ddde+XX"
    const std::size_t epos = sci.find('e');
    std::string mantissa = sci.substr(0, epos);
    const int exponent = std::stoi(sci.substr(epos + 1));
    if (exponent < -6 || exponent > 20)
        return mantissa + "e" + (exponent >= 0 ? "+" : "") + std::to_string(exponent);
    std::string sign;
    if (!mantissa.empty() && mantissa[0] == '-') {
        sign = "-";
        mantissa.erase(0, 1);
    }
    std::string digits;
    for (char c : mantissa)
        if (c != '.') digits += c;
    const int position = 1 + exponent;  // digits before the point
    if (position <= 0) return sign + "0." + std::string(static_cast<std::size_t>(-position), '0') + digits;
    if (position >= static_cast<int>(digits.size()))
        return sign + digits + std::string(static_cast<std::size_t>(position) - digits.size(), '0');
    return sign + digits.substr(0, static_cast<std::size_t>(position)) + "." +
           digits.substr(static_cast<std::size_t>(position));
}

// A label cut to `limit` UTF-16 units with an ellipsis (displayLabel).
inline std::string display_label(const std::string& s, std::size_t limit = 1024) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n += len == 4 ? 2 : 1;
        if (n > limit) return s.substr(0, i) + "\xe2\x80\xa6";
        i += len;
    }
    return s;
}

// An axis tick: at most four significant digits, thousands grouped, no
// trailing zeros (the reference formats with .significantDigits(1...4)).
inline std::string tick_label(double v) {
    if (v == 0.0 || !std::isfinite(v)) return "0";
    const double mag = std::floor(std::log10(std::fabs(v)));
    const double unit = std::pow(10.0, mag - 3.0);
    double r = std::round(v / unit) * unit;
    const int decimals = std::max(0, static_cast<int>(3.0 - mag));
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, r);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    std::string sign;
    if (!s.empty() && s[0] == '-') {
        sign = "-";
        s.erase(0, 1);
    }
    const std::size_t dot = s.find('.');
    std::string whole = s.substr(0, dot);
    const std::string frac = dot == std::string::npos ? std::string() : s.substr(dot);
    std::string grouped;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped += ',';
        grouped += whole[i];
    }
    return sign + grouped + frac;
}

inline Parse parse(const std::string& source) {
    const auto fail = [](std::string m) {
        Parse p;
        p.failure = std::move(m);
        return p;
    };
    if (detail::utf16_len(source) > kMaxSourceUtf16)
        return fail("Chart source exceeds 64000 UTF-16 units. Aggregate the data first.");
    const nlohmann::json asText = source;
    if (!detail::optional_text(&asText)) return fail("Chart source is empty.");
    const nlohmann::json root = nlohmann::json::parse(source, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return fail("Chart source must be a JSON object.");
    Spec spec;
    const auto type = detail::optional_text(detail::field(root, "type"));
    if (type == "bar")
        spec.mark = Mark::Bar;
    else if (type == "line")
        spec.mark = Mark::Line;
    else
        return fail("Chart type must be \"bar\" or \"line\".");
    const nlohmann::json* series = detail::field(root, "series");
    if (series == nullptr || !series->is_array() || series->empty())
        return fail("Chart needs a nonempty series array.");
    if (series->size() > kMaxSeries) return fail("Chart draws at most 8 series. Aggregate the data first.");
    for (std::size_t i = 0; i < series->size(); ++i) {
        const nlohmann::json& so = (*series)[i];
        const std::string n = std::to_string(i + 1);
        if (!so.is_object()) return fail("Series " + n + " must be an object.");
        Series s;
        s.name = detail::optional_text(detail::field(so, "name")).value_or("Series " + n);
        const nlohmann::json* pts = detail::field(so, "points");
        if (pts == nullptr || !pts->is_array() || pts->empty())
            return fail("Series " + n + " needs a nonempty points array.");
        if (pts->size() > kMaxPoints) return fail("Series " + n + " exceeds 200 points. Aggregate the data first.");
        for (std::size_t j = 0; j < pts->size(); ++j) {
            const nlohmann::json& po = (*pts)[j];
            const std::string pn = "Point " + std::to_string(j + 1) + " of series " + n;
            if (!po.is_object()) return fail(pn + " must be an object.");
            const nlohmann::json* xv = detail::field(po, "x");
            std::optional<std::string> label;
            if (xv != nullptr && xv->is_string())
                label = xv->get<std::string>();
            else if (const auto xn = detail::number(xv))
                label = number_label(*xn);
            if (!label) return fail(pn + " needs an x label or finite number.");
            const auto y = detail::number(detail::field(po, "y"));
            if (!y) return fail(pn + " needs a finite numeric y.");
            s.points.push_back(Point{*label, *y});
        }
        spec.series.push_back(std::move(s));
    }
    spec.title = detail::optional_text(detail::field(root, "title"));
    spec.xLabel = detail::axis_label(detail::field(root, "x"));
    spec.yLabel = detail::axis_label(detail::field(root, "y"));
    Parse p;
    p.spec = std::move(spec);
    return p;
}

// Where everything goes on the plot (ChartSpecLayout).
struct Layout {
    std::vector<std::string> labels;
    std::unordered_map<std::string, std::size_t> labelIndex;
    double lower = 0.0;
    double upper = 0.0;
    double divisor = 1.0;

    explicit Layout(const Spec& spec) {
        double low = 0.0, high = 0.0;
        bool any = false;
        for (const Series& s : spec.series)
            for (const Point& p : s.points) {
                if (labelIndex.find(p.label) == labelIndex.end()) {
                    labelIndex.emplace(p.label, labels.size());
                    labels.push_back(p.label);
                }
                if (!any) {
                    low = high = p.value;
                    any = true;
                }
                low = std::min(low, p.value);
                high = std::max(high, p.value);
            }
        if (spec.mark == Mark::Bar) {
            low = std::min(0.0, low);
            high = std::max(0.0, high);
        }
        if (low == high) {
            const double el = low - 0.5, eh = high + 0.5;
            if (el < low || eh > high) {
                low = std::isfinite(el) ? el : low;
                high = std::isfinite(eh) ? eh : high;
            } else {
                low = std::nextafter(low, -std::numeric_limits<double>::infinity());
                high = std::nextafter(high, std::numeric_limits<double>::infinity());
            }
        }
        lower = low;
        upper = high;
        divisor = std::max({std::fabs(low), std::fabs(high), std::numeric_limits<double>::min()});
    }

    [[nodiscard]] double y_fraction(double v) const {
        const double bottom = lower / divisor, top = upper / divisor, span = top - bottom;
        if (!(span > 0.0)) return 0.5;
        return std::min(1.0, std::max(0.0, (v / divisor - bottom) / span));
    }
    [[nodiscard]] double tick_value(double f) const {
        f = std::min(1.0, std::max(0.0, f));
        return lower * (1.0 - f) + upper * f;
    }
    [[nodiscard]] double x_fraction(const std::string& label, Mark mark) const {
        const auto it = labelIndex.find(label);
        if (it == labelIndex.end()) return 0.5;
        const double i = static_cast<double>(it->second);
        const double n = static_cast<double>(labels.size());
        if (mark == Mark::Bar) return (i + 0.5) / n;
        return labels.size() == 1 ? 0.5 : i / (n - 1.0);
    }
    // At most eight axis labels, spread evenly.
    [[nodiscard]] std::vector<std::size_t> axis_label_indices() const {
        std::vector<std::size_t> out;
        if (labels.size() <= 8) {
            for (std::size_t i = 0; i < labels.size(); ++i) out.push_back(i);
            return out;
        }
        for (int k = 0; k < 8; ++k)
            out.push_back(static_cast<std::size_t>(
                std::round(static_cast<double>(k) * static_cast<double>(labels.size() - 1) / 7.0)));
        return out;
    }
};

// placeAxisLabels: each candidate centred on its mark, slid inward to stay
// inside [lo, hi]; the two ends first (the first sliding left to clear the
// last), the middles only where they clear both neighbours by `gap`.
struct AxisCandidate {
    std::size_t index = 0;
    double x = 0.0;
    double width = 0.0;
};
struct AxisSpot {
    std::size_t index = 0;
    double x = 0.0;
};
inline std::vector<AxisSpot> place_axis_labels(const std::vector<AxisCandidate>& cands, double lo, double hi,
                                               double gap) {
    struct Spot {
        std::size_t index;
        double x, l, h;
    };
    const auto clamped = [&](const AxisCandidate& c) -> std::optional<Spot> {
        const double half = c.width / 2.0;
        if (c.width > hi - lo) return std::nullopt;
        const double x = std::min(std::max(c.x, lo + half), hi - half);
        return Spot{c.index, x, x - half, x + half};
    };
    std::vector<AxisSpot> out;
    if (cands.empty()) return out;
    std::optional<Spot> first = cands.size() > 1 ? clamped(cands.front()) : std::nullopt;
    const std::optional<Spot> last = clamped(cands.back());
    if (!last) {
        if (first) out.push_back({first->index, first->x});
        return out;
    }
    std::vector<Spot> placed;
    if (first) {
        const double half = (first->h - first->l) / 2.0;
        const double room = last->l - gap - half;
        if (first->x > room) first = Spot{first->index, room, room - half, room + half};
        if (first->l >= lo) placed.push_back(*first);
    }
    double prevHi = placed.empty() ? lo - gap : placed.back().h;
    for (std::size_t i = 1; i + 1 < cands.size(); ++i) {
        const auto s = clamped(cands[i]);
        if (!s) continue;
        if (s->l >= prevHi + gap && s->h + gap <= last->l) {
            placed.push_back(*s);
            prevHi = s->h;
        }
    }
    placed.push_back(*last);
    for (const Spot& s : placed) out.push_back({s.index, s.x});
    return out;
}

// What a screen reader hears for the plot: the kind, the title, the series,
// and every value, so the numbers are reachable as text.
inline std::string summary(const Spec& spec) {
    std::string s = spec.mark == Mark::Bar ? "Bar chart" : "Line chart";
    if (spec.title) s += ", " + display_label(*spec.title, 96);
    s += ": ";
    for (std::size_t i = 0; i < spec.series.size(); ++i) {
        if (i) s += "; ";
        s += display_label(spec.series[i].name, 48);
        for (const Point& p : spec.series[i].points)
            s += ", " + display_label(p.label) + " " + number_label(p.value);
    }
    return s;
}

}  // namespace hanabi::chart
