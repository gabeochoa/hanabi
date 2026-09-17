#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "../api/artifact_sniff.h"
#include "../api/disk_cache.h"
#include "../native_audio.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

// Fetches a shown artifact's bytes into the cache, once per (id, version,
// file), and stamps every row that refers to it with the result. A fetch does
// not need the version manifest: the id and version address the content, the
// response's Content-Type / Content-Disposition (then the byte signature)
// type it. Transient failures retry on re-observation; permanent ones stand.
struct ArtifactFetchSystem : afterhours::System<UIContext<InputAction>> {
    struct Fetched {
        bool ok = false;
        bool transient = false;
        std::string error;
        std::string path;
        std::string media_type;
        std::string file_name;
    };
    struct Slot {
        std::string localPath;
        std::string mediaType;
        std::string fileName;
        bool failed = false;
        bool transient = false;
        std::string failure;
        std::chrono::steady_clock::time_point failedAt{};
        std::future<Fetched> future;
    };

    static constexpr std::chrono::seconds kTransientRetry{15};

    static std::string key_of(const api::ArtifactRef& ref) {
        return ref.id + "@" + ref.version + "@" + ref.file;
    }

    static std::string extension_for(std::string_view file, std::string_view media_type) {
        const std::string fileExt = std::filesystem::path(std::string(file)).extension().string();
        if (!fileExt.empty() && fileExt.size() <= 6) return fileExt;
        if (media_type == "image/png") return ".png";
        if (media_type == "image/jpeg") return ".jpg";
        if (media_type == "image/gif") return ".gif";
        if (media_type == "image/webp") return ".webp";
        if (media_type == "image/bmp") return ".bmp";
        if (media_type == "audio/wav" || media_type == "audio/x-wav") return ".wav";
        if (media_type == "audio/mpeg") return ".mp3";
        if (media_type == "audio/mp4" || media_type == "audio/x-m4a") return ".m4a";
        return "";
    }

    static bool drawable(std::string_view media_type) {
        api::ArtifactRef probe;
        probe.media_type = std::string(media_type);
        return probe.is_image() || probe.is_audio();
    }

    static Fetched run_fetch(const std::shared_ptr<api::Client>& c, const std::string& sessionId,
                             api::ArtifactRef ref) {
        Fetched out;
        auto result = c->fetch_artifact(sessionId, ref);
        if (!result.ok) {
            out.error = result.error.empty() ? "the fetch failed" : result.error;
            const int st = result.value.http_status;
            out.transient = st == 0 || st == 401 || st == 408 || st == 429 || st >= 500;
            return out;
        }
        auto& content = result.value;
        if (content.bytes.empty()) {
            out.error = "the server returned no bytes";
            return out;
        }
        if (content.bytes.size() > api::disk_cache::kArtifactMaxBytes) {
            out.error = "larger than 32 MB; open in the web app";
            return out;
        }
        std::string type = ref.media_type;
        if (type.empty() && hanabi::artifact_sniff::usable(content.media_type))
            type = content.media_type;
        if (type.empty()) type = hanabi::artifact_sniff::media_type(content.bytes);
        if (!drawable(type)) {
            out.error = type.empty() ? "unrecognised content" : "unsupported type " + type;
            return out;
        }
        std::string file = ref.file;
        if (file.empty()) file = content.file_name;
        const std::string target = api::disk_cache::artifact_path(
            ref.id, ref.version, file, extension_for(file, type));
        if (target.empty()) {
            out.error = "no cache directory";
            return out;
        }
        if (!std::filesystem::exists(target) &&
            !api::disk_cache::store_artifact(target, content.bytes)) {
            out.error = "could not be written to the cache";
            return out;
        }
        content.bytes.clear();
        if (type.rfind("audio/", 0) == 0) native_audio_duration(target.c_str());
        out.ok = true;
        out.path = target;
        out.media_type = type;
        out.file_name = file;
        return out;
    }

    static void poll(Slot& slot) {
        if (!slot.future.valid() ||
            slot.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        Fetched f = slot.future.get();
        if (f.ok) {
            slot.localPath = f.path;
            slot.mediaType = f.media_type;
            slot.fileName = f.file_name;
        } else {
            slot.failed = true;
            slot.transient = f.transient;
            slot.failure = f.error;
            slot.failedAt = std::chrono::steady_clock::now();
        }
    }

    static bool mark(api::ArtifactRef& ref, api::ArtifactFetch state, std::string reason = {}) {
        if (ref.fetch == state && ref.unavailable_reason == reason) return false;
        ref.fetch = state;
        ref.unavailable_reason = std::move(reason);
        return true;
    }

    bool resolve(AppComponent& app, const std::string& sessionId, api::Message& m) {
        api::ArtifactRef& ref = m.artifact;
        if (ref.hidden) return mark(ref, api::ArtifactFetch::Idle);
        if (!ref.local_path.empty()) {
            if (ref.is_image() && m.image_path.empty()) m.image_path = ref.local_path;
            return mark(ref, api::ArtifactFetch::Ready);
        }
        if (ref.has_metadata() && !drawable(ref.media_type))
            return mark(ref, api::ArtifactFetch::Unavailable, "unsupported type " + ref.media_type);
        if (ref.size_bytes > api::disk_cache::kArtifactMaxBytes)
            return mark(ref, api::ArtifactFetch::Unavailable, "larger than 32 MB; open in the web app");
        const std::string key = key_of(ref);
        auto it = slots_.find(key);
        if (it != slots_.end() && it->second.failed && it->second.transient &&
            std::chrono::steady_clock::now() - it->second.failedAt > kTransientRetry) {
            slots_.erase(it);
            it = slots_.end();
        }
        if (it == slots_.end()) {
            Slot slot;
            const std::string known = ref.has_metadata()
                ? api::disk_cache::artifact_path(ref.id, ref.version, ref.file,
                                                 extension_for(ref.file, ref.media_type))
                : std::string();
            if (!known.empty() && std::filesystem::exists(known)) {
                slot.localPath = known;
                slot.mediaType = ref.media_type;
                slot.fileName = ref.file;
            } else if (app.client && app.client->supports_artifacts()) {
                std::shared_ptr<api::Client> c = app.client;
                const api::ArtifactRef copy = ref;
                slot.future = std::async(std::launch::async,
                                         [c, sessionId, copy] { return run_fetch(c, sessionId, copy); });
            } else {
                slot.failed = true;
                slot.failure = "this backend serves no artifacts";
            }
            it = slots_.emplace(key, std::move(slot)).first;
        }
        Slot& slot = it->second;
        poll(slot);
        if (slot.failed) return mark(ref, api::ArtifactFetch::Unavailable, slot.failure);
        if (slot.localPath.empty()) return mark(ref, api::ArtifactFetch::Pending);
        ref.local_path = slot.localPath;
        if (ref.media_type.empty()) ref.media_type = slot.mediaType;
        if (ref.file.empty()) ref.file = slot.fileName;
        if (ref.is_image()) m.image_path = slot.localPath;
        return mark(ref, api::ArtifactFetch::Ready);
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
                if (m.kind == api::EventKind::Artifact && !m.artifact.hidden &&
                    m.artifact.local_path == app->audioClipPath)
                    clipShown = true;
        }
        if (!clipShown) {
            native_audio_stop();
            app->audioClipPath.clear();
        }
        std::vector<std::string> observed;
        for (Pane& pane : app->panes) {
            if (!pane.openSession) continue;
            const std::string sessionId = pane.openSession->summary.id;
            auto& msgs = pane.openSession->messages;
            for (std::size_t i = 0; i < msgs.size(); ++i) {
                api::Message& m = msgs[i];
                if (m.kind != api::EventKind::Artifact) continue;
                observed.push_back(key_of(m.artifact));
                if (resolve(*app, sessionId, m)) pane.note_transcript_update(i);
            }
        }
        // Re-observation retries a transient failure: once its row is out of
        // every pane the slot is dropped, so the row's return fetches again.
        for (auto it = slots_.begin(); it != slots_.end();) {
            const bool gone = std::find(observed.begin(), observed.end(), it->first) == observed.end();
            if (it->second.failed && it->second.transient && gone) it = slots_.erase(it);
            else ++it;
        }
    }

  private:
    std::unordered_map<std::string, Slot> slots_;
};

}  // namespace ecs
