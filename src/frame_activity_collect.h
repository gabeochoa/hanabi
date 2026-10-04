#pragma once

#include <chrono>
#include <cmath>
#include <future>

#include "api/disk_cache.h"
#include "ui/status_mark.h"
#include "ecs/components.h"
#include "ecs/keyboard_focus.h"
#include "ecs/tooltip_system.h"
#include "settings.h"
#include "frame_activity.h"

namespace hanabi {

template <class T>
bool frame_future_ready(std::future<T>& future) {
    return future.valid() &&
           future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

template <class T>
bool any_frame_future_ready(std::vector<std::future<T>>& futures) {
    for (auto& future : futures)
        if (frame_future_ready(future)) return true;
    return false;
}

// A pending open-at-bottom only wants a frame while the thread it names is
// the one on this pane: it is consumed by the pane's own draw, so a pending
// for a tab behind another (the restore sets it on the last tab opened), for
// a surface tab (Settings has no transcript to pin), or for a thread not yet
// loaded is kept for when it comes on screen -- and does not hold the frame
// loop awake meanwhile. It did: five restored tabs, or Settings, or a split,
// redrew at the display rate forever (cpu_audit: 1,200 full frames in 10 s).
inline bool scroll_bottom_due(const ecs::Pane& pane) {
    return !pane.scrollBottomPending.empty() && pane.openSession &&
           pane.openSession->summary.id == pane.scrollBottomPending;
}

inline bool pane_has_request(const ecs::Pane& pane) {
    return !pane.requestOpenId.empty() || pane.requestLoadOlder ||
           pane.findScrollPending || scroll_bottom_due(pane);
}

inline bool pane_has_pending_future(ecs::Pane& pane) {
    return pane.transcriptPending || pane.diskReadPending || pane.loadingOlder ||
           !pane.supersededTranscriptFutures.empty() ||
           !pane.supersededDiskReadFutures.empty() ||
           !pane.supersededLoadOlderFutures.empty();
}

inline bool pane_has_ready_future(ecs::Pane& pane) {
    return frame_future_ready(pane.transcriptFuture) ||
           frame_future_ready(pane.diskReadFuture) ||
           frame_future_ready(pane.loadOlderFuture) ||
           any_frame_future_ready(pane.supersededTranscriptFutures) ||
           any_frame_future_ready(pane.supersededDiskReadFutures) ||
           any_frame_future_ready(pane.supersededLoadOlderFutures);
}

inline FrameSignals collect_app_frame_signals(ecs::AppComponent& app) {
    static FrameActivityTransitions transitions;
    FrameSignals s = transitions.observe(
        app.sessionSearchOpen, api::disk_cache::epoch(),
        Settings::get().shortcut_revision(), Settings::get().font_revision());
    for (std::size_t i = 0; i < app.active_pane_count(); ++i) {
        auto& pane = app.panes[i];
        s.state_request = s.state_request || pane_has_request(pane);
        s.pending_future = s.pending_future || pane_has_pending_future(pane);
        s.async_ready = s.async_ready || pane_has_ready_future(pane);
    }

    s.pending_future = s.pending_future || app.captureFuture.valid();
    s.async_ready = s.async_ready || frame_future_ready(app.captureFuture);
    s.state_request =
        s.state_request || app.requestCapture.has_value() || app.requestListRefresh ||
        !app.requestOpenTab.empty() || !app.requestSplitOpen.empty() ||
        app.requestSplitClose || app.requestSplitToggle ||
        !app.requestToggleStar.empty() || !app.requestToggleArchive.empty() ||
        !app.requestResetRowOrder.empty() || app.requestNewTask ||
        app.requestKickoff.has_value() || app.requestSend.has_value() ||
        app.requestRetryMessage.has_value() || app.composerSubmit.has_value() ||
        app.requestStream.has_value() || app.requestAuthCancel ||
        !app.requestRenameId.empty() || app.renameSubmit ||
        app.requestSettings || !app.pendingSendQueue.empty() ||
        app.refocusComposer;
    s.split_change = app.splitDragging || app.requestSplitClose ||
                     app.requestSplitToggle || !app.requestSplitOpen.empty();
    s.dragging = s.dragging || app.splitDragging || app.rowDrag.live;
    s.streaming = app.streamActive;
    s.pending_future = s.pending_future || app.artifactFetchPending;
    s.thinking = app.streamCollecting ||
                 app.streamPhase == ecs::AppComponent::StreamPhase::Thinking;

    s.pending_future =
        s.pending_future || app.listPending || app.kickoffPending ||
        app.steerPending || app.sendPending || app.streamCollecting ||
        app.authBeginPending || app.renamePending || app.settingsPending ||
        app.createdAtPending;
    s.async_ready = s.async_ready || frame_future_ready(app.listFuture) ||
                    frame_future_ready(app.createdAtFuture) ||
                    frame_future_ready(app.kickoffFuture) ||
                    frame_future_ready(app.steerFuture) ||
                    frame_future_ready(app.sendFuture) ||
                    frame_future_ready(app.streamCollectFuture) ||
                    frame_future_ready(app.authBeginFuture) ||
                    frame_future_ready(app.renameFuture) ||
                    frame_future_ready(app.settingsFuture);

    const FrameSignals lifecycle = lifecycle_frame_signals({
        .fork_request = !app.requestForkSourceId.empty(),
        .fork_pending = app.forkPending,
        .fork_ready = frame_future_ready(app.forkFuture),
        .subagent_request = app.requestSubagentRefresh,
        .subagent_pending = app.subagentListPending,
        .subagent_ready = frame_future_ready(app.subagentListFuture),
        .mute_toggle = !app.requestToggleMute.empty(),
        .toast_active = !app.toastMessage.empty(),
    });
    s.state_request = s.state_request || lifecycle.state_request;
    s.pending_future = s.pending_future || lifecycle.pending_future;
    s.async_ready = s.async_ready || lifecycle.async_ready;
    s.timer = s.timer || lifecycle.timer;

    // A live thread wakes a frame when its refetch is DUE (ecs LiveSub), not
    // for as long as it is dirty: a dirty flag waiting out a debounce used to
    // be an immediate wake on every display callback.
    if (!app.liveSubs.empty()) {
        const auto now = std::chrono::steady_clock::now();
        for (auto& [id, live] : app.liveSubs) {
            (void)id;
            s.sse_event = s.sse_event || live.refetch_due(now);
            s.pending_future = s.pending_future || live.pending;
            s.async_ready = s.async_ready || frame_future_ready(live.future);
        }
    }

    s.timer = s.timer || app.showAuth || !app.outboxRetry.empty() ||
              Settings::get().is_settings_dirty() ||
              Settings::get().get_theme_rotate_secs() > 0;
    // A focused field wants a frame when its caret toggles (ui/caret_clock.h),
    // not ten times a second; until a frame has placed it, at once.
    if (ecs::any_text_field_focused()) {
        const double next = hanabi::caret::next_toggle_at();
        s.caret = next == 0.0 || hanabi::caret::due(hanabi::caret::wall_seconds());
    }
    // An MM3 face is due to change (ui/status_mark.h): draw then, and only
    // then. Only a moving face that actually drew counts -- a row scrolled
    // away, a still face, or a paused one costs no frames.
    s.animation = s.animation || hanabi::status_mark::mm3_motion().change_due();
    s.tooltip_dwell = ecs::pending_reveal();
    return s;
}

inline void collect_ui_frame_signals(FrameSignals& s) {
    for (auto& entity : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!entity) continue;
        if (entity->has<afterhours::ui::HasScrollView>()) {
            const auto& scroll = entity->get<afterhours::ui::HasScrollView>();
            s.scrolling = s.scrolling ||
                          std::fabs(scroll.scroll_target.x -
                                    scroll.scroll_offset.x) > 0.5f ||
                          std::fabs(scroll.scroll_target.y -
                                    scroll.scroll_offset.y) > 0.5f;
            s.dragging = s.dragging || scroll.dragging_scrollbar;
        }
        if (entity->has<ecs::LayoutComponent>()) {
            const auto& layout = entity->get<ecs::LayoutComponent>();
            s.animation = s.animation || layout.sidebarAnimT < 1.0f;
        }
        if (entity->has<ecs::TabStripComponent>()) {
            const auto& tabs = entity->get<ecs::TabStripComponent>();
            s.dragging = s.dragging || tabs.dragging;
        }
    }
}

}
