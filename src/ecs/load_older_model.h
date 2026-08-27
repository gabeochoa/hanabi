#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "components.h"

namespace ecs::model {

struct LoadOlderCompletion {
    std::string sessionId;
    size_t previousMessageCount = 0;
    api::Result<api::Session> result;
};

inline std::optional<LoadOlderCompletion> take_load_older_completion(
    Pane& pane) {
    if (!pane.loadingOlder || !pane.loadOlderFuture.valid() ||
        pane.loadOlderFuture.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
        return std::nullopt;

    LoadOlderCompletion completion{
        .sessionId = std::move(pane.loadOlderPendingId),
        .previousMessageCount = pane.anchorPrevMsgCount,
        .result = pane.loadOlderFuture.get(),
    };
    pane.loadingOlder = false;
    pane.loadOlderPendingId.clear();
    return completion;
}

inline bool load_older_completion_matches(
    const Pane& pane, const LoadOlderCompletion& completion) {
    return completion.result.ok && pane.selectedId == completion.sessionId &&
           completion.result.value.summary.id == completion.sessionId;
}

inline bool apply_load_older_completion(Pane& pane,
                                        LoadOlderCompletion& completion) {
    if (!completion.result.ok) {
        if (pane.selectedId == completion.sessionId)
            pane.transcriptError = completion.result.error;
        return false;
    }
    if (!load_older_completion_matches(pane, completion)) return false;
    const std::size_t previousCount = completion.previousMessageCount;
    const bool prepended =
        completion.result.value.messages.size() > previousCount;
    const std::size_t added = prepended
                                  ? completion.result.value.messages.size() -
                                        previousCount
                                  : 0;
    if (prepended) pane.anchorPending = completion.sessionId;
    pane.openSession = std::move(completion.result.value);
    if (prepended)
        pane.note_transcript_prepend(added);
    else
        pane.note_transcript_reset();
    pane.transcriptState = LoadState::Loaded;
    pane.transcriptError.clear();
    pane.hasMoreOlder = pane.openSession->has_more_older;
    return true;
}

}  // namespace ecs::model
