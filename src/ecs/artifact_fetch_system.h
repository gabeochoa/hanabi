#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "../api/disk_cache.h"
#include "../native_audio.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct ArtifactFetchSystem : afterhours::System<UIContext<InputAction>> {
    struct Slot {
        std::string localPath;
        bool failed = false;
        std::future<api::Result<api::ArtifactContent>> future;
        std::string target;
        bool audio = false;
    };

    static std::string key_of(const api::ArtifactRef& ref) {
        return ref.id + "@" + ref.version + "@" + ref.file;
    }

    static std::string extension_for(const api::ArtifactRef& ref) {
        const std::string fileExt =
            std::filesystem::path(ref.file).extension().string();
        if (!fileExt.empty() && fileExt.size() <= 6) return fileExt;
        if (ref.media_type == "image/png") return ".png";
        if (ref.media_type == "image/jpeg") return ".jpg";
        if (ref.media_type == "image/gif") return ".gif";
        if (ref.media_type == "image/webp") return ".webp";
        if (ref.media_type == "audio/wav" || ref.media_type == "audio/x-wav")
            return ".wav";
        if (ref.media_type == "audio/mpeg") return ".mp3";
        if (ref.media_type == "audio/mp4" || ref.media_type == "audio/x-m4a")
            return ".m4a";
        return "";
    }

    static std::string cache_target(const api::ArtifactRef& ref) {
        return api::disk_cache::artifact_path(ref.id, ref.version,
                                              extension_for(ref));
    }

    static void poll(Slot& slot) {
        if (!slot.future.valid() ||
            slot.future.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready)
            return;
        if (slot.future.get().ok) slot.localPath = slot.target;
        else slot.failed = true;
    }

    bool resolve(AppComponent& app, const std::string& sessionId,
                 api::Message& m) {
        api::ArtifactRef& ref = m.artifact;
        if (!ref.local_path.empty()) return false;
        if (!(ref.is_image() || ref.is_audio())) return false;
        const std::string key = key_of(ref);
        auto it = slots_.find(key);
        if (it == slots_.end()) {
            Slot slot;
            slot.target = cache_target(ref);
            slot.audio = ref.is_audio();
            if (slot.target.empty()) {
                slot.failed = true;
            } else if (std::filesystem::exists(slot.target)) {
                slot.localPath = slot.target;
            } else if (app.client && app.client->supports_artifacts()) {
                std::shared_ptr<api::Client> c = app.client;
                const api::ArtifactRef copy = ref;
                const std::string target = slot.target;
                const bool audio = slot.audio;
                slot.future = std::async(std::launch::async, [c, sessionId, copy,
                                                              target, audio] {
                    auto result = c->fetch_artifact(sessionId, copy);
                    if (result.ok &&
                        api::disk_cache::store_artifact(target, result.value.bytes)) {
                        result.value.bytes.clear();
                        if (audio) native_audio_duration(target.c_str());
                        return result;
                    }
                    if (result.ok)
                        result = api::Result<api::ArtifactContent>::failure(
                            "The artifact could not be written to the cache.");
                    return result;
                });
            } else {
                slot.failed = true;
            }
            it = slots_.emplace(key, std::move(slot)).first;
        }
        Slot& slot = it->second;
        poll(slot);
        if (slot.localPath.empty()) return false;
        ref.local_path = slot.localPath;
        if (ref.is_image()) m.image_path = slot.localPath;
        return true;
    }

    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;
        app->artifactFetchPending = false;
        for (auto& [key, slot] : slots_) {
            poll(slot);
            if (slot.future.valid()) app->artifactFetchPending = true;
        }
        bool clipShown = app->audioClipPath.empty();
        for (std::size_t i = 0; i < app->active_pane_count(); ++i) {
            const Pane& pane = app->panes[i];
            if (!pane.openSession) continue;
            for (const api::Message& m : pane.openSession->messages)
                if (m.kind == api::EventKind::Artifact &&
                    m.artifact.local_path == app->audioClipPath)
                    clipShown = true;
        }
        if (!clipShown) {
            native_audio_stop();
            app->audioClipPath.clear();
        }
        for (Pane& pane : app->panes) {
            if (!pane.openSession) continue;
            const std::string sessionId = pane.openSession->summary.id;
            auto& msgs = pane.openSession->messages;
            for (std::size_t i = 0; i < msgs.size(); ++i) {
                api::Message& m = msgs[i];
                if (m.kind != api::EventKind::Artifact) continue;
                if (m.artifact.is_image() && m.image_path.empty() &&
                    !m.artifact.local_path.empty()) {
                    m.image_path = m.artifact.local_path;
                    pane.note_transcript_update(i);
                }
                if (resolve(*app, sessionId, m)) pane.note_transcript_update(i);
            }
        }
    }

  private:
    std::unordered_map<std::string, Slot> slots_;
};

}  // namespace ecs
