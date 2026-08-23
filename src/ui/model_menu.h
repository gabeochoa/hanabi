#pragma once

// ---------------------------------------------------------------------------
// The models this client offers, and the one place that decides what a model
// id is called on screen.
//
// WHERE THE LIST COMES FROM. agentcloud serves its curated native menu two
// ways: `NATIVE_MODEL_MENU` in
// fbcode/agentcloud/shared/inference/src/router.rs, and the pre-attach
// `models` control command on the chat socket
// (orchestrator/webserver/src/chat_control.rs -> model_menu::models_reply),
// which answers per-harness rows with the deployment's default marked and the
// caller's entitlements applied. hanabi does not speak that command yet, so
// this is the curated list copied verbatim from the server's own source —
// ids exactly as the gateway serves them (`muse-spark-1.2-internal` keeps its
// suffix; the tidier spelling 404s), display names exactly as the menu spells
// them. When hanabi learns the `models` verb, this becomes the fallback for a
// server that answers nothing.
//
// WHAT PICKING ONE DOES. It writes the SAME preference the settings sheet's
// "Default model" row writes — `Settings::set_default_model`, pushed to the
// backend as `defaultModelId` by the loader's debounced settings sync. There
// is no per-session model verb in this client: `PatchSessionOptions`
// (agentcloud spec 115) is real on the wire but hanabi's api::Client has no
// call for it, and inventing one here would be inventing a wire call. So the
// picker sets the default for the work you start next, which is a true thing
// it can do, rather than pretending to retune the running session.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::models {

struct Entry {
    // The id persisted and sent as defaultModelId.
    std::string_view id;
    // What the menu calls it.
    std::string_view name;
};

// "default" first: it is hanabi's existing stored value and means "leave the
// choice to the server", which is a real answer and the safest one.
inline const std::vector<Entry>& all() {
    static const std::vector<Entry> kModels = {
        {"default", "Server default"},
        {"claude-opus-5", "Opus 5"},
        {"claude-fable-5", "Fable 5"},
        {"claude-opus-4-8", "Opus 4.8"},
        {"claude-sonnet-5", "Sonnet 5"},
        {"avocado-code-flex", "Avocado Code Flex"},
        {"muse-spark-1.2-internal", "Muse Spark 1.2"},
        {"gpt-5.6-sol", "GPT-5.6 Sol"},
        {"gpt-5.5", "GPT-5.5"},
    };
    return kModels;
}

// Where an id sits in the menu; all().size() when it is not on it.
inline size_t index_of(std::string_view id) {
    const auto& list = all();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) return i;
    return list.size();
}

// A stored id that is not on the curated menu is shown as itself rather than
// silently redrawn as "Server default" — family routing accepts ids this list
// does not carry, and a settings file written by a later build may hold one.
inline std::string display_name(std::string_view id) {
    const size_t i = index_of(id);
    if (i < all().size()) return std::string(all()[i].name);
    return std::string(id);
}

}  // namespace hanabi::models
