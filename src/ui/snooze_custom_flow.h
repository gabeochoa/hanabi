#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "../native_snooze_prompt.h"
#include "snooze_presets.h"

namespace hanabi::snooze_custom {

inline constexpr const char* kLeafId = "snooze_custom";
inline constexpr const char* kLeafLabel = "Custom\xe2\x80\xa6";

struct Pending {
    std::uint64_t prompt_generation = 0;
    std::uint64_t client_generation = 0;
    std::string session_id;
};

struct Gates {
    std::uint64_t client_generation = 0;
    bool session_known = false;
    bool supports_inbox_state = false;
    bool synced = false;
    bool write_pending = false;
    std::int64_t now_unix_sec = 0;
};

enum class Verdict { Ignored, Nothing, Refused, Set };

struct Settled {
    Verdict verdict = Verdict::Ignored;
    std::string session_id;
    std::int64_t until_unix_sec = 0;
    std::string reason;
    bool operator==(const Settled&) const = default;
};

inline Settled settle(const Pending& pending, const native_snooze_prompt::Result& r, const Gates& g) {
    using Kind = native_snooze_prompt::Result::Kind;
    Settled out;
    if (r.generation != pending.prompt_generation) return out;
    if (g.client_generation != pending.client_generation) {
        out.verdict = Verdict::Nothing;
        return out;
    }
    out.session_id = pending.session_id;
    if (r.kind == Kind::Cancelled || r.kind == Kind::Quiet || r.kind == Kind::Refused) {
        out.verdict = Verdict::Nothing;
        return out;
    }
    if (!g.session_known) {
        out.verdict = Verdict::Refused;
        out.reason = "That thread is no longer listed";
        return out;
    }
    if (!g.supports_inbox_state || !g.synced) {
        out.verdict = Verdict::Refused;
        out.reason = "Snooze is unsynced";
        return out;
    }
    if (g.write_pending) {
        out.verdict = Verdict::Refused;
        out.reason = "Still saving the last snooze for this thread";
        return out;
    }
    if (!snooze_presets::is_in_range(r.until_unix_sec, g.now_unix_sec)) {
        out.verdict = Verdict::Refused;
        out.reason = "Snooze refused: outside the allowed window";
        return out;
    }
    out.verdict = Verdict::Set;
    out.until_unix_sec = r.until_unix_sec;
    return out;
}

inline std::string scope_for(std::string_view session_id) {
    return "session:" + std::string(session_id);
}

}
