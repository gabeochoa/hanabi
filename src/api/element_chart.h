#pragma once
// The data a std/Chart element carried, in the shape the fence chart
// plotter draws (ui/chart_spec.h): categories and named series of numbers.
// The reference's data layer (b7d2981d) lets a Chart carry flat `rows` plus
// an `encoding` (Vega-Lite channel -> field name) instead of inline
// `categories`/`series`; both ride the wire tree's props (`tree.p`), so
// they reach Hanabi today -- element_facts_from_value used to read only
// the text lines and drop the plot. Pure: no UI, so
// tests/unit/test_element_kinds.cpp pins the conversion; the transcript
// re-parses the JSON this emits with the fence parser, so the fence's
// caps and refusals (8 series, 200 points a series) apply here too.
//
// Covered: line/bar/area/slope from inline series or rows+encoding
// (`x`, one `y` or a list of `y` fields, `color` splitting one y into a
// series per value), and pie/donut/funnel from `segments` (or rows with
// `label`+`value` encoding) drawn as bars, since the fence plotter has
// no arc marks. Not covered (text lines still draw; documented in the
// bank README): scatter/histogram/box/sankey marks, stacking, bands,
// error bars, annotations, facets, dual axes, log scale -- the fence
// plotter has no such marks yet.
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../../vendor/nlohmann/json.hpp"

namespace api::elements {

struct ElementChartSeries {
    std::string name;
    // (category, value) in category order; a missing value is a gap the
    // text lines show as "-" and the plot simply does not mark.
    std::vector<std::pair<std::string, std::optional<double>>> points;
};
struct ElementChart {
    std::string title;
    std::string kind;  // as emitted: line, bar, pie, ...
    bool bar = false;
    std::string xLabel;
    std::string yLabel;
    std::vector<std::string> categories;
    std::vector<ElementChartSeries> series;
};

namespace chart_detail {
inline std::optional<double> num(const nlohmann::json& v) {
    if (v.is_number()) {
        const double d = v.get<double>();
        if (std::isfinite(d)) return d;
    }
    if (v.is_string()) {  // the contract allows numeric strings
        const std::string s = v.get<std::string>();
        try {
            std::size_t used = 0;
            const double d = std::stod(s, &used);
            if (used == s.size() && std::isfinite(d)) return d;
        } catch (...) {
        }
    }
    return std::nullopt;
}
inline std::string label_of(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return v.dump();
    return {};
}
inline std::string str_prop(const nlohmann::json& p, const char* k) {
    if (p.is_object() && p.contains(k) && p[k].is_string()) return p[k].get<std::string>();
    return {};
}
inline bool all_numeric(const std::vector<std::string>& xs) {
    if (xs.empty()) return false;
    for (const auto& x : xs)
        if (!num(nlohmann::json(x))) return false;
    return true;
}
inline bool all_iso_dates(const std::vector<std::string>& xs) {
    if (xs.empty()) return false;
    for (const auto& x : xs) {
        if (x.size() < 10 || x[4] != '-' || x[7] != '-') return false;
        for (std::size_t i = 0; i < 10; ++i)
            if (i != 4 && i != 7 && (x[i] < '0' || x[i] > '9')) return false;
    }
    return true;
}
}  // namespace chart_detail

// The chart a std/Chart's props carry, or nothing (the caller falls back
// to the projection). `p` is the wire tree's props object.
inline std::optional<ElementChart> element_chart_from_props(const nlohmann::json& p) {
    using namespace chart_detail;
    if (!p.is_object()) return std::nullopt;
    ElementChart c;
    c.title = str_prop(p, "title");
    c.kind = str_prop(p, "kind");
    if (c.kind.empty()) c.kind = str_prop(p, "type");
    c.xLabel = str_prop(p, "xLabel");
    c.yLabel = str_prop(p, "yLabel");
    const bool pieish = c.kind == "pie" || c.kind == "donut" || c.kind == "funnel";
    c.bar = c.kind == "bar" || pieish;
    if (!(c.bar || c.kind == "line" || c.kind == "area" || c.kind == "slope"))
        return std::nullopt;  // scatter/histogram/box/sankey: no fence marks

    // Segments (pie/donut/funnel, or any kind that carries them alone).
    if (pieish || (!p.contains("series") && !p.contains("rows"))) {
        if (p.contains("segments") && p["segments"].is_array()) {
            ElementChartSeries s;
            s.name = c.title.empty() ? "Value" : c.title;
            for (const auto& seg : p["segments"]) {
                const std::string lab = label_of(seg.is_object() && seg.contains("label") ? seg["label"] : nlohmann::json());
                const auto v = seg.is_object() && seg.contains("value") ? num(seg["value"]) : std::nullopt;
                if (lab.empty() || !v) continue;
                c.categories.push_back(lab);
                s.points.push_back({lab, v});
            }
            if (!s.points.empty()) {
                c.series.push_back(std::move(s));
                return c;
            }
        }
        if (!pieish) return std::nullopt;
    }

    // Inline categories + series.
    if (p.contains("series") && p["series"].is_array()) {
        if (p.contains("categories") && p["categories"].is_array())
            for (const auto& cat : p["categories"]) c.categories.push_back(label_of(cat));
        for (const auto& so : p["series"]) {
            if (!so.is_object() || !so.contains("values") || !so["values"].is_array()) continue;
            ElementChartSeries s;
            s.name = str_prop(so, "name");
            if (s.name.empty()) s.name = "Series " + std::to_string(c.series.size() + 1);
            const auto& vals = so["values"];
            for (std::size_t i = 0; i < vals.size(); ++i) {
                const std::string cat = i < c.categories.size() ? c.categories[i] : std::to_string(i + 1);
                if (i >= c.categories.size()) c.categories.push_back(cat);
                s.points.push_back({cat, num(vals[i])});
            }
            if (!s.points.empty()) c.series.push_back(std::move(s));
        }
        if (!c.series.empty()) return c;
        c.categories.clear();
    }

    // Rows + encoding: flat records, one field per channel.
    if (!p.contains("rows") || !p["rows"].is_array() || !p.contains("encoding") ||
        !p["encoding"].is_object())
        return std::nullopt;
    const auto& enc = p["encoding"];
    const auto& rows = p["rows"];
    auto field = [&](const char* k) -> std::string { return str_prop(enc, k); };
    if (pieish) {
        const std::string lf = field("label"), vf = field("value");
        if (lf.empty() || vf.empty()) return std::nullopt;
        ElementChartSeries s;
        s.name = vf;
        for (const auto& r : rows) {
            if (!r.is_object() || !r.contains(lf) || !r.contains(vf)) continue;
            const std::string lab = label_of(r[lf]);
            const auto v = num(r[vf]);
            if (lab.empty() || !v) continue;
            c.categories.push_back(lab);
            s.points.push_back({lab, v});
        }
        if (s.points.empty()) return std::nullopt;
        c.series.push_back(std::move(s));
        return c;
    }
    const std::string xf = field("x"), colorf = field("color");
    if (xf.empty()) return std::nullopt;
    std::vector<std::string> yfields;
    if (enc.contains("y") && enc["y"].is_string()) yfields.push_back(enc["y"].get<std::string>());
    if (enc.contains("y") && enc["y"].is_array())
        for (const auto& y : enc["y"])
            if (y.is_string()) yfields.push_back(y.get<std::string>());
    if (yfields.empty()) return std::nullopt;

    // Categories in row order (sorted when all are numbers or ISO dates,
    // per the contract), series split by `color` or by y field.
    std::vector<std::string> seriesNames;
    auto series_for = [&](const std::string& name) -> ElementChartSeries& {
        for (auto& s : c.series)
            if (s.name == name) return s;
        c.series.push_back(ElementChartSeries{name, {}});
        return c.series.back();
    };
    for (const auto& r : rows) {
        if (!r.is_object() || !r.contains(xf)) continue;
        const std::string cat = label_of(r[xf]);
        if (cat.empty()) continue;
        if (std::find(c.categories.begin(), c.categories.end(), cat) == c.categories.end())
            c.categories.push_back(cat);
        if (!colorf.empty() && yfields.size() == 1) {
            const std::string sn = r.contains(colorf) ? label_of(r[colorf]) : "";
            if (sn.empty() || !r.contains(yfields[0])) continue;
            series_for(sn).points.push_back({cat, num(r[yfields[0]])});
        } else {
            for (const auto& yf : yfields) {
                if (!r.contains(yf)) continue;
                series_for(yf).points.push_back({cat, num(r[yf])});
            }
        }
    }
    if (c.series.empty() || c.categories.empty()) return std::nullopt;
    if (all_numeric(c.categories) || all_iso_dates(c.categories)) {
        auto sorted = c.categories;
        std::sort(sorted.begin(), sorted.end(), [](const std::string& a, const std::string& b) {
            const auto na = num(nlohmann::json(a)), nb = num(nlohmann::json(b));
            if (na && nb) return *na < *nb;
            return a < b;
        });
        if (sorted != c.categories) {
            c.categories = sorted;
            for (auto& s : c.series)
                std::stable_sort(s.points.begin(), s.points.end(),
                                 [&](const std::pair<std::string, std::optional<double>>& a,
                                     const std::pair<std::string, std::optional<double>>& b) {
                                     return std::find(c.categories.begin(), c.categories.end(), a.first) <
                                            std::find(c.categories.begin(), c.categories.end(), b.first);
                                 });
        }
    }
    // Align every series to the category list (gaps stay gaps).
    for (auto& s : c.series) {
        std::vector<std::pair<std::string, std::optional<double>>> aligned;
        for (const auto& cat : c.categories) {
            std::optional<double> v;
            for (const auto& pt : s.points)
                if (pt.first == cat) { v = pt.second; break; }
            aligned.push_back({cat, v});
        }
        s.points = std::move(aligned);
    }
    return c;
}

// The fence-chart JSON for this chart (what the fence parser consumes),
// or "" when it exceeds the fence's caps and the plot must not draw.
inline std::string element_chart_source(const ElementChart& c) {
    if (c.series.empty() || c.series.size() > 8) return {};
    nlohmann::json root;
    root["type"] = c.bar ? "bar" : "line";
    if (!c.title.empty()) root["title"] = c.title;
    if (!c.xLabel.empty()) root["x"] = c.xLabel;
    if (!c.yLabel.empty()) root["y"] = c.yLabel;
    root["series"] = nlohmann::json::array();
    for (const auto& s : c.series) {
        nlohmann::json so;
        so["name"] = s.name;
        so["points"] = nlohmann::json::array();
        std::size_t n = 0;
        for (const auto& pt : s.points) {
            if (!pt.second) continue;
            if (++n > 200) return {};
            if (*pt.second == std::trunc(*pt.second) && std::fabs(*pt.second) < 9e15)
                so["points"].push_back({{"x", pt.first}, {"y", static_cast<long long>(*pt.second)}});
            else
                so["points"].push_back({{"x", pt.first}, {"y", *pt.second}});
        }
        if (so["points"].empty()) return {};
        root["series"].push_back(std::move(so));
    }
    if (root["series"].empty()) return {};
    return root.dump();
}

// The fence JSON for a Chart at this tree node or nested in a container
// (a Card holding a Chart draws the Chart's plot in place), or "".
inline std::string chart_source_from_tree(const std::string& el, const nlohmann::json& tree, int depth = 0) {
    if (!tree.is_object() || depth > 4) return {};
    const auto& p = tree.contains("p") ? tree["p"] : nlohmann::json::object();
    const std::string t = tree.contains("t") && tree["t"].is_string() ? tree["t"].get<std::string>() : el;
    if (t == "std/Chart") {
        if (const auto c = element_chart_from_props(p)) return element_chart_source(*c);
        return {};
    }
    if (tree.contains("c") && tree["c"].is_array())
        for (const auto& ch : tree["c"]) {
            auto s = chart_source_from_tree("", ch, depth + 1);
            if (!s.empty()) return s;
        }
    return {};
}

}  // namespace api::elements
