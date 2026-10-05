#include <cstdio>
#include <string>

#include "../../src/ui/md_image.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mi = hanabi::md_image;

int main() {
    // A line that IS an image, alone or as a list item; a title is dropped.
    auto a = mi::parse_line("![Zoom chart](/api/attachments/view?file_id=7)");
    CHECK(a && a->alt == "Zoom chart" && a->source == "/api/attachments/view?file_id=7");
    auto b = mi::parse_line("  - ![a](u.png \"title\")");
    CHECK(b && b->alt == "a" && b->source == "u.png");
    CHECK(mi::parse_line("1. ![x](/tmp/x.png)").has_value());
    CHECK(mi::parse_line("\xe2\x80\xa2 ![x](/tmp/x.png)").has_value());
    // Prose around it is prose; a link is not an image.
    CHECK(!mi::parse_line("see ![a](u) here"));
    CHECK(!mi::parse_line("[a](u)"));
    CHECK(!mi::parse_line("![a](u) trailing"));
    // Resolution.
    const auto none = [](const std::string&) { return false; };
    const auto yes = [](const std::string&) { return true; };
    const std::string web = "https://agentcloud.example.com/";
    auto t = mi::resolve("/api/attachments/view?file_id=7", web, yes);
    CHECK(t.kind == mi::Kind::Web && t.where == "https://agentcloud.example.com/api/attachments/view?file_id=7");
    CHECK(mi::resolve("/tmp/shot.png", web, yes).kind == mi::Kind::Local);
    CHECK(mi::resolve("/tmp/shot.png", web, none).kind == mi::Kind::Web);
    CHECK(mi::resolve("file:///tmp/a.png", web, none).where == "/tmp/a.png");
    CHECK(mi::resolve("https://x.example/p.png", web, none).kind == mi::Kind::Web);
    auto r = mi::resolve("fbobjc/Apps/x/Icon.png", web, none);
    CHECK(r.kind == mi::Kind::Repo && r.where == "https://www.internalfb.com/code/fbsource/fbobjc/Apps/x/Icon.png");
    CHECK(mi::resolve("", web, none).kind == mi::Kind::None);
    CHECK(mi::resolve("data:image/png;base64,AAAA", web, none).kind == mi::Kind::None);
    CHECK(mi::name(mi::Image{"  ", "u"}) == "Image");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
