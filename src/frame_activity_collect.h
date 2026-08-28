#pragma once

#include <chrono>
#include <cmath>
#include <future>

#include "api/disk_cache.h"
#include "ecs/components.h"
#include "ecs/keyboard_focus.h"
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

inline bool pane_has_request(const ecs::Pane& pane) {
    return !pane.requestOpenId.empty() || pane.requestLoadOlder ||
           pane.findScrollPending || !pane.scrollBottomPending.empty();
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

    s.state_request =
        s.state_request || app.requestListRefresh ||
        !app.requestOpenTab.empty() || !app.requestSplitOpen.empty() ||
        app.requestSplitClose || app.requestSplitToggle ||
        !app.requestToggleStar.empty() || !app.requestToggleArchive.empty() ||
        !app.requestResetRowOrder.empty() || app.requestNewTask ||
        !app.requestKickoffPrompt.empty() || !app.requestSendPrompt.empty() ||
        !app.requestRetryPrompt.empty() || !app.composerSubmit.empty() ||
        !app.requestStreamPrompt.empty() || app.requestAuthCancel ||
        !app.requestRenameId.empty() || app.renameSubmit ||
        app.requestSettings || !app.pendingSendQueue.empty() ||
        app.refocusComposer;
    s.split_change = app.splitDragging || app.requestSplitClose ||
                     app.requestSplitToggle || !app.requestSplitOpen.empty();
    s.dragging = s.dragging || app.splitDragging || app.rowDrag.live;
    s.streaming = app.streamActive;
    s.thinking = app.streamCollecting ||
                 app.streamPhase == ecs::AppComponent::StreamPhase::Thinking;

    s.pending_future =
        s.pending_future || app.listPending || app.kickoffPending ||
        app.steerPending || app.sendPending || app.streamCollecting ||
        app.authBeginPending || app.renamePending || app.settingsPending;
    s.async_ready = s.async_ready || frame_future_ready(app.listFuture) ||
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

    for (auto& [id, live] : app.liveSubs) {
        (void)id;
        const bool dirty = live.dirty->load();
        const FrameSignals lifecycleDirty =
            lifecycle_frame_signals({.sse_dirty = dirty});
        s.sse_event = s.sse_event || lifecycleDirty.sse_event;
        s.timer = s.timer || lifecycleDirty.timer;
        s.pending_future = s.pending_future || live.pending;
        s.async_ready = s.async_ready || frame_future_ready(live.future);
    }

    s.timer = s.timer || app.showAuth || !app.outboxRetry.empty() ||
              Settings::get().is_settings_dirty() ||
              Settings::get().get_theme_rotate_secs() > 0;
    s.caret = ecs::any_text_field_focused();
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
