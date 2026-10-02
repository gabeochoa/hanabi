#pragma once

// Opening Help > Report a Bug: the window is captured FIRST, so the picture
// is what the reader was looking at, not the report sheet over it.

#include <unistd.h>

#include <string>

#include "components.h"

extern "C" void metal_take_screenshot(const char* filename);

namespace ecs {

inline void open_bug_report(AppComponent& app, const std::string& surface) {
    if (app.showBugReport) return;
    app.showBugReport = true;
    app.bugReportSurface = surface;
    app.bugReportError.clear();
    app.bugReportCapture.clear();
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    // A scripted run has no window worth capturing and never uploads one for
    // real (the fake runner answers): a stand-in path keeps the flow whole.
    app.bugReportCapture = "/tmp/hanabi-report-capture-e2e.png";
#else
    const std::string path =
        "/tmp/hanabi-report-capture-" + std::to_string(::getpid()) + ".png";
    ::unlink(path.c_str());
    metal_take_screenshot(path.c_str());
    if (::access(path.c_str(), R_OK) == 0) app.bugReportCapture = path;
#endif
}

}  // namespace ecs
