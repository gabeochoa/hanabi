#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace hanabi::model {

enum class OpenFetch { Full, WaitForDisk };

inline OpenFetch open_fetch_plan(bool diskCacheOn, bool diskHasTranscript, bool instantThreads = true) {
    return instantThreads && diskCacheOn && diskHasTranscript ? OpenFetch::WaitForDisk : OpenFetch::Full;
}

inline bool instant_threads_on() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_INSTANT_THREADS");
        return v == nullptr || *v == '\0' || *v != '0';
    }();
    return on;
}

struct DeltaPlan {
    bool delta = false;
    std::uint64_t since = 0;
};

inline DeltaPlan after_disk_read(bool diskAccepted, std::uint64_t cachedNewestSeq) {
    if (!diskAccepted || cachedNewestSeq == 0) return {};
    return {true, cachedNewestSeq};
}

}
