#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

#include "../../src/native_audio.h"

static int failures = 0;
#define CHECK(value)                                                        \
    do {                                                                    \
        if (!(value)) {                                                     \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static const char* kClip = "tests/fixtures/artifacts/clip.wav";

static void test_the_fixture_clip_reads_its_duration() {
    const double d = native_audio_duration(kClip);
    CHECK(std::fabs(d - 1.5) < 0.01);
}

static void test_the_waveform_has_a_shape_not_a_flat_line() {
    std::vector<float> peaks(64, -1.0f);
    CHECK(native_audio_peaks(kClip, peaks.data(), 64) == 64);
    float hi = 0.0f;
    float lo = 1.0f;
    for (float p : peaks) {
        hi = std::max(hi, p);
        lo = std::min(lo, p);
    }
    CHECK(hi > 0.3f);
    CHECK(hi <= 1.0f);
    CHECK(lo >= 0.0f);
    CHECK(hi - lo > 0.2f);
    CHECK(peaks[32] > peaks[1]);
}

static void test_an_unreadable_path_is_zero_not_a_crash() {
    CHECK(native_audio_duration("tests/fixtures/artifacts/missing.wav") == 0.0);
    float one = 0.5f;
    CHECK(native_audio_peaks("tests/fixtures/artifacts/missing.wav", &one, 1) == 0);
    CHECK(native_audio_is_playing(kClip) == 0);
    CHECK(native_audio_position(kClip) == 0.0);
}

static void test_the_log_hook_records_the_play_instead_of_playing() {
    const std::string log = "/tmp/hanabi_test_audio_log_" +
                            std::to_string(static_cast<long>(getpid()));
    std::remove(log.c_str());
    setenv("HANABI_AUDIO_LOG", log.c_str(), 1);
    native_audio_play(kClip);
    CHECK(native_audio_is_playing(kClip) == 1);
    CHECK(native_audio_is_playing("tests/fixtures/artifacts/other.wav") == 0);
    native_audio_pause();
    CHECK(native_audio_is_playing(kClip) == 0);
    native_audio_play(kClip);
    CHECK(native_audio_is_playing(kClip) == 1);
    native_audio_stop();
    unsetenv("HANABI_AUDIO_LOG");
    CHECK(native_audio_is_playing(kClip) == 0);
    std::string lines;
    if (FILE* f = std::fopen(log.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof buf, f)) lines += buf;
        std::fclose(f);
    }
    CHECK(lines.find(std::string("play ") + kClip + "\n") != std::string::npos);
    CHECK(lines.find("pause ") != std::string::npos);
    CHECK(lines.find(std::string("stop ") + kClip + "\n") != std::string::npos);
    std::remove(log.c_str());
}

int main() {
    test_the_fixture_clip_reads_its_duration();
    test_the_waveform_has_a_shape_not_a_flat_line();
    test_an_unreadable_path_is_zero_not_a_crash();
    test_the_log_hook_records_the_play_instead_of_playing();
    if (failures == 0) std::printf("test_native_audio: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
