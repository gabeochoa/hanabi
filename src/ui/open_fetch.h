#pragma once

#include <cstdint>
#include <string>

namespace hanabi::model {

enum class OpenFetch { Full, WaitForDisk };

inline OpenFetch open_fetch_plan(bool diskCacheOn, bool diskHasTranscript) {
    return diskCacheOn && diskHasTranscript ? OpenFetch::WaitForDisk : OpenFetch::Full;
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
