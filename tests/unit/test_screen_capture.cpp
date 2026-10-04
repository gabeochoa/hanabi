#include <cstdio>
#include <vector>

#include "../../src/ui/screen_capture.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sc = hanabi::screen_capture;

int main() {
    // The window: the largest on-screen ordinary window of the app that was in front.
    const std::vector<sc::Window> ws{
        {1, 7, 0, true, 800, 600},   // the document
        {2, 7, 0, true, 1, 1},       // an invisible helper
        {3, 7, 25, true, 2000, 30},  // a menu/status layer, wider than everything
        {4, 7, 0, false, 1600, 1200},  // off screen
        {5, 9, 0, true, 3000, 2000},   // another app's
        {6, 7, 0, true, 900, 650},     // its bigger document window
    };
    const auto w = sc::frontmost(ws, 7);
    CHECK(w && w->id == 6);
    CHECK(!sc::frontmost(ws, 11));
    CHECK(!sc::frontmost({{8, 7, 0, true, 15, 400}}, 7));  // under 16 px a side

    // A flat frame is refused; two colours are a picture.
    std::vector<std::uint8_t> flat(4 * 4 * 4, 0);
    CHECK(sc::is_degenerate(flat.data(), flat.size(), 4, 4, 16));
    auto two = flat;
    two[4 * 5] = 255;  // one pixel differs
    CHECK(!sc::is_degenerate(two.data(), two.size(), 4, 4, 16));
    CHECK(sc::is_degenerate(nullptr, 0, 4, 4, 16));
    CHECK(sc::is_degenerate(flat.data(), 10, 4, 4, 16));  // short buffer
    // A big frame whose one different pixel the stride skips is still a picture.
    std::vector<std::uint8_t> big(4 * 400 * 400, 9);
    big[4 * (400 * 200 + 201) + 1] = 10;
    CHECK(!sc::is_degenerate(big.data(), big.size(), 400, 400, 1600));

    // The file name says what was photographed, never the window's title.
    CHECK(sc::file_name("Google Chrome") == "google-chrome-window.png");
    CHECK(sc::file_name("  Visual Studio Code!") == "visual-studio-code-window.png");
    CHECK(sc::file_name("") == "screenshot.png" && sc::file_name("\xe2\x9c\x93") == "screenshot.png");

    // Where it lands.
    CHECK(sc::destination_for(true, true) == sc::Destination::Session);
    CHECK(sc::destination_for(false, true) == sc::Destination::NewConversation);
    CHECK(sc::destination_for(true, false) == sc::Destination::Unsupported);

    // What a refusal says: a remedy for the two granting fixes, else the reason + the paste fallback.
    CHECK(sc::fixable_by_granting(sc::Failure::PermissionDenied) && sc::fixable_by_granting(sc::Failure::DegenerateFrame));
    CHECK(!sc::fixable_by_granting(sc::Failure::NoWindow));
    CHECK(sc::message(sc::Failure::PermissionDenied, "Hanabi").find("Screen Recording") != std::string::npos);
    CHECK(sc::message(sc::Failure::NoWindow, "Hanabi") ==
          std::string("There was no window in front to photograph. ") + sc::kFallback);
    CHECK(sc::message(sc::Failure::CaptureFailed, "Hanabi", "the stream stopped").find("(the stream stopped)") !=
          std::string::npos);
    // Only the first refusal of a launch pulls the reader to the front.
    CHECK(sc::activates_for_refusal(false) && !sc::activates_for_refusal(true));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
