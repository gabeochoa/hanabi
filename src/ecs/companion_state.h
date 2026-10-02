#pragma once

// The Companion's state (companion_system.h): which entity it shows, what
// has been read for it, and the reads in flight. One entity at a time; a
// read that lands for an entity the reader has left is dropped.

#include <future>
#include <optional>
#include <string>
#include <vector>

#include "../api/client.h"
#include "../api/companion_wire.h"

namespace ecs {

struct CompanionState {
    bool open = false;
    char kind = 'D';        // 'D' a diff, 'T' a task
    std::string number;     // digits only
    std::uint64_t generation = 0;

    // A diff.
    std::optional<api::companion::Diff> diff;
    int selectedFile = -1;
    std::optional<std::vector<api::companion::Hunk>> hunks;
    std::string fileError;

    // A task.
    std::optional<api::companion::Task> task;
    int page = 0;  // 0 Overview, 1 Comments
    std::optional<std::vector<api::companion::Comment>> comments;
    std::string commentsError;
    bool commentsAsked = false;

    std::string error;  // the document's own failure, one sentence

    // Requests (one-shot).
    bool requestLoad = false;
    int requestFile = -1;
    bool requestComments = false;

    // In flight.
    std::future<api::Result<nlohmann::json>> docFuture;
    std::future<api::Result<nlohmann::json>> fileFuture;
    std::future<api::Result<nlohmann::json>> commentsFuture;
    std::uint64_t docGen = 0, fileGen = 0, commentsGen = 0;

    void open_entity(char k, const std::string& n) {
        open = true;
        kind = k;
        number = n;
        ++generation;
        diff.reset();
        selectedFile = -1;
        hunks.reset();
        fileError.clear();
        task.reset();
        page = 0;
        comments.reset();
        commentsError.clear();
        commentsAsked = false;
        error.clear();
        requestLoad = true;
    }
};

}  // namespace ecs
