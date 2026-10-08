#pragma once

#include <cstdint>

namespace hanabi::api::resume_paging {

inline constexpr int kProbeFrames = 25;
inline constexpr int kSafetyPages = 200;

inline int page_limit(int pageIndex, int window, bool resume) {
    if (window <= 0) return 500;
    const int full = window > 500 ? 500 : window;
    if (!resume || pageIndex > 0) return full;
    return kProbeFrames < full ? kProbeFrames : full;
}

inline int page_bound(bool wantAll, bool resume, int wholeTranscriptPages) {
    if (resume) return kSafetyPages;
    return wantAll ? wholeTranscriptPages : 1;
}

enum class Next { Continue, Reached, NoCursor };

inline Next after_page(bool resume, std::uint64_t oldestSeq, std::uint64_t sinceSeq) {
    if (oldestSeq == 0) return Next::NoCursor;
    if (resume && oldestSeq <= sinceSeq) return Next::Reached;
    return Next::Continue;
}

struct GapOutcome {
    bool keepPages;
    bool hasMoreOlder;
    std::uint64_t retryBefore;
};

inline GapOutcome on_page_error(bool resume, int pagesHeld, std::uint64_t before) {
    if (!resume || pagesHeld == 0) return GapOutcome{false, false, 0};
    return GapOutcome{true, true, before};
}

inline bool gap_closed(bool resume, std::uint64_t oldestSeq, std::uint64_t sinceSeq) {
    return !resume || (oldestSeq != 0 && oldestSeq <= sinceSeq);
}

}
