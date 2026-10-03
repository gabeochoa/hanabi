#pragma once

// Playing an audio artifact in the transcript (the reference's media artifact
// renderer; Knots kt-nmbp, the audio half). One clip plays at a time -- a
// second Play stops the first, as the reference's single player does.
//
// The real app plays through AVAudioPlayer (native_extras.mm). Scripted and
// headless builds never make a sound: they run a silent stand-in whose clock
// is wall time and whose duration is read from the file's own header (WAV),
// so a script can press Play and watch the time move without a speaker.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "../native_extras.h"

namespace hanabi::audio {

struct State {
    std::string path;  // the clip loaded, "" = none
    bool playing = false;
    double position = 0.0;  // seconds
    double duration = 0.0;  // seconds, 0 = unknown
};

// "m:ss" for a time in seconds.
inline std::string clock(double seconds) {
    if (seconds < 0) seconds = 0;
    const int s = static_cast<int>(seconds + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// A PCM WAV's length from its header (byte rate and the data chunk), 0 when
// the file is not one. Enough for the stand-in; the real player reads any
// format AVFoundation can.
inline double wav_seconds(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return 0.0;
    unsigned char h[12];
    double out = 0.0;
    std::uint32_t byteRate = 0;
    if (std::fread(h, 1, 12, f) == 12 && std::string(reinterpret_cast<char*>(h), 4) == "RIFF" &&
        std::string(reinterpret_cast<char*>(h) + 8, 4) == "WAVE") {
        unsigned char ch[8];
        while (std::fread(ch, 1, 8, f) == 8) {
            const std::uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) |
                                       (static_cast<std::uint32_t>(ch[7]) << 24);
            const std::string id(reinterpret_cast<char*>(ch), 4);
            if (id == "fmt ") {
                unsigned char fmt[16];
                if (size < 16 || std::fread(fmt, 1, 16, f) != 16) break;
                byteRate = fmt[8] | (fmt[9] << 8) | (fmt[10] << 16) |
                           (static_cast<std::uint32_t>(fmt[11]) << 24);
                std::fseek(f, static_cast<long>(size - 16 + (size & 1)), SEEK_CUR);
            } else if (id == "data") {
                if (byteRate > 0) out = static_cast<double>(size) / byteRate;
                break;
            } else {
                std::fseek(f, static_cast<long>(size + (size & 1)), SEEK_CUR);
            }
        }
    }
    std::fclose(f);
    return out;
}

// The silent stand-in. Its clock is the APP's frame time (advance(dt) once a
// frame), not the wall: scripted runs step time faster than the wall, and a
// `wait 2` there must mean two seconds of playback.
struct Silent {
    State s;
    void advance(double dt) {
        if (!s.playing || dt <= 0) return;
        s.position += dt;
        if (s.duration > 0 && s.position >= s.duration) {
            s.position = s.duration;
            s.playing = false;
        }
    }
    State read() const { return s; }
};

inline bool silent_mode() {
#if defined(AFTER_HOURS_ENABLE_E2E_TESTING)
    return true;
#else
    const char* v = std::getenv("HANABI_SILENT_AUDIO");
    return v != nullptr && *v != '\0';
#endif
}

inline Silent& silent() {
    static Silent s;
    return s;
}

// Once a frame: moves the silent stand-in's clock (the real player keeps its
// own).
inline void advance(double dt) {
    if (silent_mode()) silent().advance(dt);
}

inline State state() {
    if (silent_mode()) return silent().read();
    NativeAudioState n{};
    native_audio_state(&n);
    State out;
    out.path = n.path;
    out.playing = n.playing;
    out.position = n.position;
    out.duration = n.duration;
    return out;
}

// Play `path` from where it was paused (from the start when it is a
// different clip, or it ran to the end). False when it cannot be played.
inline bool play(const std::string& path) {
    if (silent_mode()) {
        Silent& z = silent();
        if (z.s.path != path) {
            z.s = State{};
            z.s.path = path;
            z.s.duration = wav_seconds(path);
            if (z.s.duration <= 0) {
                z.s = State{};
                return false;
            }
        }
        if (z.s.position >= z.s.duration) z.s.position = 0.0;  // ran to the end: again
        z.s.playing = true;
        return true;
    }
    return native_audio_play(path.c_str());
}

// A clip's length before it has played (to show "0:00 / 0:12" up front):
// the header in silent mode, AVFoundation's probe otherwise. Cached per path.
inline double known_duration(const std::string& path) {
    static std::map<std::string, double> cache;
    if (const auto it = cache.find(path); it != cache.end()) return it->second;
    const double d = silent_mode() ? wav_seconds(path) : native_audio_probe(path.c_str());
    if (cache.size() > 256) cache.clear();
    cache.emplace(path, d);
    return d;
}

inline void pause() {
    if (silent_mode()) {
        silent().s.playing = false;
        return;
    }
    native_audio_pause();
}

inline void stop() {
    if (silent_mode()) {
        silent().s = State{};
        return;
    }
    native_audio_stop();
}

}  // namespace hanabi::audio
