#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "native_audio.h"

namespace {

AVAudioPlayer* g_player = nil;
std::string g_player_path;
std::string g_logged_path;
bool g_logged_playing = false;

struct Analysis {
    double duration = 0.0;
    std::vector<float> peaks;
};

std::mutex g_analyses_mutex;

std::map<std::string, Analysis>& analyses() {
    static std::map<std::string, Analysis> m;
    return m;
}

constexpr int kAnalysisBuckets = 256;
constexpr AVAudioFrameCount kAnalysisChunkFrames = 1 << 16;
constexpr double kAnalysisMaxSeconds = 60.0 * 60.0;

bool logging(const char* verb, const char* path) {
    const char* log = std::getenv("HANABI_AUDIO_LOG");
    if (log == nullptr || *log == 0) return false;
    if (FILE* f = std::fopen(log, "a")) {
        std::fprintf(f, "%s %s\n", verb, path ? path : "");
        std::fclose(f);
    }
    return true;
}

NSURL* url_for(const char* path) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
}

Analysis analysis_for(const char* path) {
    const std::string key = path;
    {
        std::lock_guard<std::mutex> lock(g_analyses_mutex);
        if (auto it = analyses().find(key); it != analyses().end())
            return it->second;
    }
    Analysis a;
    const auto remember = [&] {
        std::lock_guard<std::mutex> lock(g_analyses_mutex);
        analyses()[key] = a;
        return a;
    };
    @autoreleasepool {
        NSError* error = nil;
        AVAudioFile* file = [[AVAudioFile alloc] initForReading:url_for(path)
                                                          error:&error];
        if (file == nil || error != nil) return remember();
        const AVAudioFramePosition frames = file.length;
        const double rate = file.processingFormat.sampleRate;
        if (frames <= 0 || rate <= 0.0) return remember();
        a.duration = static_cast<double>(frames) / rate;
        if (a.duration > kAnalysisMaxSeconds) return remember();
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:file.processingFormat
              frameCapacity:kAnalysisChunkFrames];
        if (buffer == nil) return remember();
        a.peaks.assign(kAnalysisBuckets, 0.0f);
        AVAudioFramePosition consumed = 0;
        while (consumed < frames) {
            if (![file readIntoBuffer:buffer error:&error] || error != nil ||
                buffer.frameLength == 0 || buffer.floatChannelData == nullptr) {
                a.peaks.clear();
                return remember();
            }
            const AVAudioFrameCount n = buffer.frameLength;
            const AVAudioChannelCount channels = buffer.format.channelCount;
            for (AVAudioFrameCount i = 0; i < n; ++i) {
                const int b = static_cast<int>(
                    (consumed + i) * kAnalysisBuckets / frames);
                float& peak = a.peaks[static_cast<std::size_t>(
                    std::min(b, kAnalysisBuckets - 1))];
                for (AVAudioChannelCount c = 0; c < channels; ++c)
                    peak = std::max(peak, std::fabs(buffer.floatChannelData[c][i]));
            }
            consumed += n;
        }
        for (float& p : a.peaks) p = std::min(1.0f, p);
    }
    return remember();
}

bool same_clip(const char* path) {
    return g_player != nil && path != nullptr && g_player_path == path;
}

}  // namespace

void native_audio_play(const char* path) {
    if (path == nullptr || *path == 0) return;
    if (logging("play", path)) {
        g_logged_path = path;
        g_logged_playing = true;
        return;
    }
    if (same_clip(path)) {
        [g_player play];
        return;
    }
    if (g_player != nil) [g_player stop];
    NSError* error = nil;
    g_player = [[AVAudioPlayer alloc] initWithContentsOfURL:url_for(path)
                                                     error:&error];
    if (g_player == nil || error != nil) {
        g_player = nil;
        g_player_path.clear();
        return;
    }
    g_player_path = path;
    [g_player prepareToPlay];
    [g_player play];
}

void native_audio_pause(void) {
    if (logging("pause", g_logged_path.c_str())) {
        g_logged_playing = false;
        return;
    }
    if (g_player != nil) [g_player pause];
}

void native_audio_stop(void) {
    if (logging("stop", g_logged_path.c_str())) {
        g_logged_path.clear();
        g_logged_playing = false;
        return;
    }
    if (g_player != nil) [g_player stop];
    g_player = nil;
    g_player_path.clear();
}

int native_audio_is_playing(const char* path) {
    if (path == nullptr) return 0;
    if (g_logged_playing && g_logged_path == path) return 1;
    return same_clip(path) && g_player.isPlaying ? 1 : 0;
}

double native_audio_position(const char* path) {
    return same_clip(path) ? g_player.currentTime : 0.0;
}

double native_audio_duration(const char* path) {
    if (path == nullptr || *path == 0) return 0.0;
    return analysis_for(path).duration;
}

int native_audio_peaks(const char* path, float* out, int count) {
    if (path == nullptr || *path == 0 || out == nullptr || count <= 0) return 0;
    const Analysis a = analysis_for(path);
    if (a.peaks.empty()) return 0;
    for (int i = 0; i < count; ++i) {
        const int src = static_cast<int>(
            static_cast<long long>(i) * kAnalysisBuckets / count);
        out[i] = a.peaks[static_cast<std::size_t>(std::min(src, kAnalysisBuckets - 1))];
    }
    return count;
}
