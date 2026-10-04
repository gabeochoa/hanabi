#pragma once

// A hotkey photographs the window in front into the composer (Knots kt-8uce;
// the reference's ScreenCapture.swift, kt-qb2w). Pure: which window, whether a
// frame is a picture of anything, what the file is called, where the shot
// lands, and what a refusal says. The capture itself is native
// (native_capture_window); this decides around it, so every rule is testable
// without a window server or a Screen Recording grant.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hanabi::screen_capture {

// A window that could be photographed, reduced to what the choice turns on.
struct Window {
    std::uint32_t id = 0;
    int pid = -1;
    int layer = 0;  // 0 = an ordinary document window
    bool onScreen = false;
    int width = 0, height = 0;
    [[nodiscard]] long long area() const { return static_cast<long long>(width) * height; }
};

// An app's invisible helper windows are a pixel or two: never the target.
inline constexpr int kMinimumEdge = 16;

// The window to photograph: the LARGEST on-screen ordinary window of the app
// that was frontmost when the chord fired (read before Hanabi activates --
// read after, and the app photographs itself). The window list's order is not
// documented, so "the first" would be a guess; the biggest is deterministic.
inline std::optional<Window> frontmost(const std::vector<Window>& windows, int pid) {
    std::optional<Window> best;
    for (const auto& w : windows) {
        if (w.pid != pid || !w.onScreen || w.layer != 0) continue;
        if (w.width < kMinimumEdge || w.height < kMinimumEdge) continue;
        if (!best || w.area() > best->area()) best = w;
    }
    return best;
}

// Whether a captured RGBA bitmap is a photograph of nothing: every pixel one
// colour (alpha included). A denied or not-yet-composited capture comes back
// a flat frame rather than an error, and an attachment built from one looks
// like evidence and shows the model nothing. One colour is refused, two is
// accepted (a blank editor pane is a real thing to photograph). A strided
// pass first; a frame it calls flat is scanned in full before it is refused.
inline bool is_flat(const std::uint8_t* rgba, int width, int height, int bytesPerRow, long stride) {
    const long pixels = static_cast<long>(width) * height;
    bool have = false;
    std::uint32_t first = 0;
    for (long i = 0; i < pixels; i += stride) {
        const long off = (i / width) * bytesPerRow + (i % width) * 4;
        const std::uint32_t c = (std::uint32_t(rgba[off]) << 24) | (std::uint32_t(rgba[off + 1]) << 16) |
                                (std::uint32_t(rgba[off + 2]) << 8) | std::uint32_t(rgba[off + 3]);
        if (!have) {
            first = c;
            have = true;
        } else if (c != first) {
            return false;
        }
    }
    return true;
}
inline constexpr long kSampleBudget = 32768;
inline bool is_degenerate(const std::uint8_t* rgba, std::size_t size, int width, int height, int bytesPerRow) {
    if (rgba == nullptr || width <= 0 || height <= 0 || bytesPerRow < width * 4) return true;
    if (size < static_cast<std::size_t>(height - 1) * bytesPerRow + static_cast<std::size_t>(width) * 4) return true;
    const long pixels = static_cast<long>(width) * height;
    const long stride = std::max(1L, pixels / kSampleBudget);
    if (!is_flat(rgba, width, height, bytesPerRow, stride)) return false;
    return is_flat(rgba, width, height, bytesPerRow, 1);
}

// "<app>-window.png" so three staged shots say what they are; the window's
// TITLE is the reader's business and stays out of a name that travels.
inline std::string file_name(const std::string& appName) {
    std::string out;
    for (char c : appName) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (alnum) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "screenshot.png" : out + "-window.png";
}

// Every way the chord can fire and stage nothing; each sentence names a move.
enum class Failure { PermissionDenied, NoWindow, CaptureFailed, DegenerateFrame, EncodingFailed };

// In the app's own chord words (the UI font has no key glyphs).
inline constexpr const char* kFallback = "Shift Cmd 4 still works: take the shot, then paste it into the composer.";

inline std::string reason(Failure f, const std::string& appName, const std::string& detail = {}) {
    switch (f) {
        case Failure::PermissionDenied:
            return appName + " has not been granted Screen Recording, so it cannot photograph the screen.";
        case Failure::NoWindow: return "There was no window in front to photograph.";
        case Failure::CaptureFailed: return "macOS refused the capture (" + detail + ").";
        case Failure::DegenerateFrame:
            return "The capture came back blank, so nothing was attached \xe2\x80\x94 that is what a screenshot "
                   "taken without Screen Recording looks like.";
        case Failure::EncodingFailed: return "The capture could not be encoded as a PNG.";
    }
    return "";
}

// Granting Screen Recording can fix these two; the others it cannot.
inline bool fixable_by_granting(Failure f) {
    return f == Failure::PermissionDenied || f == Failure::DegenerateFrame;
}

// What the reader is told: for a refusal granting can fix, where to grant it
// (the pane is opened for them on the first one); otherwise the reason and
// the paste fallback.
inline std::string message(Failure f, const std::string& appName, const std::string& detail = {}) {
    if (fixable_by_granting(f))
        return "Turn on " + appName + " in System Settings \xe2\x80\xba Privacy & Security \xe2\x80\xba Screen Recording, "
               "then press the shortcut again.";
    return reason(f, appName, detail) + " " + kFallback;
}

// Where a shot lands: the conversation on screen; none on screen opens a new
// conversation and waits there; a conversation whose backend cannot carry
// attachments is refused, never quietly routed to a different thread.
enum class Destination { Session, NewConversation, Unsupported };
inline Destination destination_for(bool threadOnScreen, bool supportsAttachments) {
    if (!supportsAttachments) return Destination::Unsupported;
    return threadOnScreen ? Destination::Session : Destination::NewConversation;
}
inline constexpr const char* kUnsupported =
    "This backend cannot carry attachments, so the screenshot was not staged.";

// What a refusal may do to the reader's afternoon: the FIRST of a launch
// comes to the front and says it (where the remedy is); later ones say it
// without pulling the reader out of the app they are in.
inline bool activates_for_refusal(bool alreadyRefusedThisLaunch) { return !alreadyRefusedThisLaunch; }

// The chord: Ctrl+Shift+4 (the reference's shipped one). Not Cmd+Shift+3/4/5,
// which are macOS's own screenshot chords. Off by default: it is the one chord
// held while Hanabi is NOT in front -- every other global chord is released
// then, by the owner's rule that an unfocused app never swallows a chord.
inline constexpr const char* kChordLabel = "Ctrl Shift 4";

}  // namespace hanabi::screen_capture
