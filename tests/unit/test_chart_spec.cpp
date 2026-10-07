#include <cmath>
#include <cstdio>
#include <string>

#include "../../src/ui/chart_spec.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace chart = hanabi::chart;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    std::printf("=== test_chart_spec ===\n");

    CHECK(chart::is_chart_lang("chart") && chart::is_chart_lang(" Chart ") && chart::is_chart_lang("CHART"));
    CHECK(!chart::is_chart_lang("json") && !chart::is_chart_lang("charts") && !chart::is_chart_lang(""));

    // A good spec, both label forms and both axis-label forms.
    {
        const auto p = chart::parse(
            R"({"type":"bar","title":"  Payouts  ","x":"Day","y":{"label":"USD"},)"
            R"("series":[{"name":"Mismatched","points":[{"x":"Mon","y":12.4},{"x":2,"y":3.1}]},)"
            R"({"points":[{"x":"Mon","y":-1}]}]})");
        CHECK(p.ok());
        if (p.ok()) {
            const chart::Spec& s = *p.spec;
            CHECK(s.mark == chart::Mark::Bar);
            CHECK(s.title == std::string("Payouts"));
            CHECK(s.xLabel == std::string("Day") && s.yLabel == std::string("USD"));
            CHECK(s.series.size() == 2 && s.series[0].name == "Mismatched" && s.series[1].name == "Series 2");
            CHECK(s.series[0].points[1].label == "2" && near(s.series[0].points[1].value, 3.1));
            const chart::Layout lay(s);
            CHECK(lay.labels.size() == 2 && lay.labels[0] == "Mon" && lay.labels[1] == "2");
            CHECK(near(lay.lower, -1.0) && near(lay.upper, 12.4));
            CHECK(near(lay.x_fraction("Mon", s.mark), 0.25) && near(lay.x_fraction("2", s.mark), 0.75));
            CHECK(near(lay.y_fraction(12.4), 1.0) && near(lay.y_fraction(-1.0), 0.0));
            CHECK(near(lay.tick_value(0.5), 5.7));
            CHECK(chart::summary(s) == "Bar chart, Payouts: Mismatched, Mon 12.4, 2 3.1; Series 2, Mon -1");
        }
    }

    // A bar chart's range always holds zero; a line chart's does not.
    {
        const auto bar = chart::parse(R"({"type":"bar","series":[{"points":[{"x":"a","y":5},{"x":"b","y":9}]}]})");
        const auto line = chart::parse(R"({"type":"line","series":[{"points":[{"x":"a","y":5},{"x":"b","y":9}]}]})");
        CHECK(bar.ok() && line.ok());
        if (bar.ok() && line.ok()) {
            const chart::Layout lb(*bar.spec), ll(*line.spec);
            CHECK(near(lb.lower, 0.0) && near(lb.upper, 9.0));
            CHECK(near(ll.lower, 5.0) && near(ll.upper, 9.0));
            CHECK(near(ll.x_fraction("a", chart::Mark::Line), 0.0) && near(ll.x_fraction("b", chart::Mark::Line), 1.0));
        }
        const auto flat = chart::parse(R"({"type":"line","series":[{"points":[{"x":"a","y":3}]}]})");
        CHECK(flat.ok());
        if (flat.ok()) {
            const chart::Layout lf(*flat.spec);
            CHECK(near(lf.lower, 2.5) && near(lf.upper, 3.5));
            CHECK(near(lf.x_fraction("a", chart::Mark::Line), 0.5));
        }
    }

    // Refusals carry the reference's reasons.
    CHECK(chart::parse("").failure == "Chart source is empty.");
    CHECK(chart::parse("   \n").failure == "Chart source is empty.");
    CHECK(chart::parse("[1,2]").failure == "Chart source must be a JSON object.");
    CHECK(chart::parse("{nope").failure == "Chart source must be a JSON object.");
    CHECK(chart::parse(R"({"type":"pie","series":[]})").failure == "Chart type must be \"bar\" or \"line\".");
    CHECK(chart::parse(R"({"type":"bar","series":[]})").failure == "Chart needs a nonempty series array.");
    CHECK(chart::parse(R"({"type":"bar","series":[1]})").failure == "Series 1 must be an object.");
    CHECK(chart::parse(R"({"type":"bar","series":[{"points":[]}]})").failure ==
          "Series 1 needs a nonempty points array.");
    CHECK(chart::parse(R"({"type":"bar","series":[{"points":[{"x":true,"y":1}]}]})").failure ==
          "Point 1 of series 1 needs an x label or finite number.");
    CHECK(chart::parse(R"({"type":"bar","series":[{"points":[{"x":"a","y":"1"}]}]})").failure ==
          "Point 1 of series 1 needs a finite numeric y.");
    {
        std::string many = R"({"type":"bar","series":[)";
        for (int i = 0; i < 9; ++i) many += std::string(i ? "," : "") + R"({"points":[{"x":"a","y":1}]})";
        many += "]}";
        CHECK(chart::parse(many).failure == "Chart draws at most 8 series. Aggregate the data first.");
        std::string pts = R"({"type":"bar","series":[{"points":[)";
        for (int i = 0; i < 201; ++i) pts += std::string(i ? "," : "") + R"({"x":"a","y":1})";
        pts += "]}]}";
        CHECK(chart::parse(pts).failure == "Series 1 exceeds 200 points. Aggregate the data first.");
        CHECK(chart::parse(std::string(64001, ' ')).failure ==
              "Chart source exceeds 64000 UTF-16 units. Aggregate the data first.");
    }

    // Numbers as the web prints them.
    CHECK(chart::number_label(0) == "0");
    CHECK(chart::number_label(3) == "3");
    CHECK(chart::number_label(-2.5) == "-2.5");
    CHECK(chart::number_label(100000) == "100000");
    CHECK(chart::number_label(0.000001) == "0.000001");
    CHECK(chart::number_label(1e-7) == "1e-7");
    CHECK(chart::number_label(1e21) == "1e+21");
    CHECK(chart::number_label(123456789012) == "123456789012");
    CHECK(chart::tick_label(0) == "0");
    CHECK(chart::tick_label(12.4) == "12.4");
    CHECK(chart::tick_label(1234.5) == "1,235");
    CHECK(chart::tick_label(12345) == "12,350");
    CHECK(chart::tick_label(-0.25) == "-0.25");
    CHECK(chart::tick_label(3.1) == "3.1");
    CHECK(chart::display_label("abcdef", 3) == "abc\xe2\x80\xa6");
    CHECK(chart::display_label("abc", 3) == "abc");

    // Axis labels: ends first, middles only where they fit.
    {
        const auto spots = chart::place_axis_labels(
            {{0, 0.0, 20.0}, {1, 50.0, 20.0}, {2, 60.0, 20.0}, {3, 100.0, 20.0}}, 0.0, 100.0, 4.0);
        CHECK(spots.size() == 3);
        if (spots.size() == 3) {
            CHECK(spots[0].index == 0 && near(spots[0].x, 10.0));
            CHECK(spots[1].index == 1 && near(spots[1].x, 50.0));
            CHECK(spots[2].index == 3 && near(spots[2].x, 90.0));
        }
        const auto one = chart::place_axis_labels({{0, 50.0, 30.0}}, 0.0, 100.0, 4.0);
        CHECK(one.size() == 1 && near(one[0].x, 50.0));
        CHECK(chart::place_axis_labels({{0, 50.0, 300.0}}, 0.0, 100.0, 4.0).empty());
    }
    {
        chart::Spec s;
        s.mark = chart::Mark::Line;
        chart::Series ser;
        for (int i = 0; i < 20; ++i) ser.points.push_back({std::to_string(i), 1.0 * i});
        s.series.push_back(ser);
        const chart::Layout lay(s);
        const auto idx = lay.axis_label_indices();
        CHECK(idx.size() == 8 && idx.front() == 0 && idx.back() == 19);
    }

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
