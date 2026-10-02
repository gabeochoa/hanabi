#pragma once

// Files `/knot` requests (api/knots_runner.h) off the frame, one at a time,
// and says what became of each: the knot's id and a Copy for its link, or the
// refusal with the command handed back to the composer it was typed in.

#include <sys/sysctl.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <string>

#include "../api/knots_runner.h"
#include "../version.h"
#include "components.h"

namespace ecs {

inline std::string macos_version() {
    char buf[64] = {0};
    size_t len = sizeof buf;
    if (::sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0) == 0 && buf[0] != 0)
        return std::string("macOS ") + buf;
    return {};
}

struct KnotsSystem : afterhours::System<AppComponent> {
    void for_each_with(afterhours::Entity&, AppComponent& app, float) override {
        using namespace std::chrono_literals;
        if (app.knotPending && app.knotFuture.valid() &&
            app.knotFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app.knotFuture.get();
            app.knotPending = false;
            if (r.ok) {
                app.lastKnotUrl = r.value.url;
                app.slashNotice = "Filed " + r.value.id + ".";
                app.slashNoticePane = app.knotPane;
                app.raise_copy_toast("Filed " + r.value.id, r.value.url);
            } else {
                app.knotError = r.error;
                app.knotRestoreDraft = "/knot " + app.knotText;
                app.knotRestoreSessionId = app.knotSession;
                app.slashNotice = r.error;
                app.slashNoticePane = app.knotPane;
            }
        }
        if (app.knotPending || app.requestKnotText.empty()) return;
        app.knotText = std::move(app.requestKnotText);
        app.requestKnotText.clear();
        app.knotSession = app.requestKnotSession;
        app.knotPane = app.requestKnotPane;
        api::knots::Context ctx;
        ctx.version = hanabi::kVersion;
        ctx.os = macos_version();
        ctx.surface = "composer";
        if (const char* u = std::getenv("USER")) ctx.reporter = u;
        ctx.sessionId = app.knotSession;
        app.knotPending = true;
        const std::string text = app.knotText;
        app.knotFuture = std::async(std::launch::async, [text, ctx] {
            return api::knots::file(text, ctx, api::knots::Board::App);
        });
    }
};

}  // namespace ecs
