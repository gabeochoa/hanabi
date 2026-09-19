#pragma once

#include <cctype>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "snooze_presets.h"

namespace hanabi::snooze_parse {

using Seconds = std::int64_t;
using snooze_presets::LocalCalendar;

inline constexpr std::size_t kMaxArgumentCharacters = 40;
inline constexpr int kDefaultNamedHour = 8;
inline constexpr std::string_view kClearWord = "off";
inline constexpr std::string_view kTomorrowWord = "tomorrow";
inline constexpr std::string_view kNextWorkdayWord = "next workday";
inline constexpr std::string_view kWeekdayWords[] = {
    "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};

enum class Choice { Named, Relative, Time };

enum class Failure { NonexistentTime, OutOfRange, Unrecognized };

inline constexpr std::string_view hint(Failure f) {
    switch (f) {
        case Failure::NonexistentTime:
            return "That clock time does not exist on that day. Daylight saving skips it.";
        case Failure::OutOfRange: return "Pick a time within the next year.";
        case Failure::Unrecognized:
            return "Try `9am`, `in 2h`, `wednesday 9am`, `tomorrow at 9am`, `next workday`, or `off`.";
    }
    return "";
}

struct Command {
    enum class Kind { Clear, Picker, Set, Invalid };
    Kind kind = Kind::Invalid;
    Choice choice = Choice::Named;
    Seconds until = 0;
    Failure failure = Failure::Unrecognized;

    static Command clear() { return {Kind::Clear, Choice::Named, 0, Failure::Unrecognized}; }
    static Command picker() { return {Kind::Picker, Choice::Named, 0, Failure::Unrecognized}; }
    static Command set(Choice c, Seconds until) { return {Kind::Set, c, until, Failure::Unrecognized}; }
    static Command invalid(Failure f) { return {Kind::Invalid, Choice::Named, 0, f}; }
    bool operator==(const Command&) const = default;
};

struct ClockTime {
    int hour = 0;
    int minute = 0;
    bool operator==(const ClockTime&) const = default;
};

inline constexpr std::uint32_t kUnicodeWhiteSpace[] = {
    0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x0020, 0x0085, 0x00a0, 0x1680, 0x2000,
    0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200a,
    0x2028, 0x2029, 0x202f, 0x205f, 0x3000};

inline constexpr std::uint32_t kKelvinSign = 0x212a;

inline bool is_unicode_white_space(std::uint32_t cp) {
    for (const std::uint32_t w : kUnicodeWhiteSpace)
        if (w == cp) return true;
    return false;
}

inline std::size_t decode_utf8(std::string_view s, std::size_t at, std::uint32_t* cp) {
    const unsigned char b0 = static_cast<unsigned char>(s[at]);
    std::size_t len = 1;
    std::uint32_t value = b0;
    if (b0 >= 0xf0) { len = 4; value = b0 & 0x07U; }
    else if (b0 >= 0xe0) { len = 3; value = b0 & 0x0fU; }
    else if (b0 >= 0xc0) { len = 2; value = b0 & 0x1fU; }
    if (at + len > s.size()) len = s.size() - at;
    for (std::size_t i = 1; i < len; ++i)
        value = (value << 6) | (static_cast<unsigned char>(s[at + i]) & 0x3fU);
    *cp = value;
    return len;
}

inline std::string normalized(std::string_view argument) {
    std::string out;
    bool pending_space = false;
    std::size_t at = 0;
    while (at < argument.size()) {
        std::uint32_t cp = 0;
        const std::size_t len = decode_utf8(argument, at, &cp);
        if (is_unicode_white_space(cp)) {
            pending_space = !out.empty();
            at += len;
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        if (cp < 0x80) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(cp))));
        else if (cp == kKelvinSign) out.push_back('k');
        else out.append(argument.substr(at, len));
        at += len;
    }
    return out;
}

inline std::size_t character_count(std::string_view utf8) {
    std::size_t n = 0;
    for (const unsigned char c : utf8)
        if ((c & 0xc0U) != 0x80U) ++n;
    return n;
}

inline std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline std::string join_words(const std::vector<std::string>& words, std::size_t from,
                              std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to && i < words.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += words[i];
    }
    return out;
}

inline bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (const unsigned char c : s)
        if (!std::isdigit(c)) return false;
    return true;
}

inline std::optional<Seconds> relative_seconds(std::string_view text) {
    std::string_view body = text;
    if (body.substr(0, 3) == "in ") body.remove_prefix(3);
    std::string digits;
    std::string unit;
    for (const char c : body) {
        if (std::isdigit(static_cast<unsigned char>(c)) && unit.empty()) digits.push_back(c);
        else if (c == ' ' && !digits.empty()) continue;
        else unit.push_back(c);
    }
    if (digits.empty() || digits.size() > 4) return std::nullopt;
    const long count = std::stol(digits);
    if (count < 1) return std::nullopt;
    if (unit == "m" || unit == "minute" || unit == "minutes") return count * 60;
    if (unit == "h" || unit == "hour" || unit == "hours") return count * 3600;
    if (unit == "d" || unit == "day" || unit == "days") return count * 86400;
    return std::nullopt;
}

inline std::optional<ClockTime> hour_minute(std::string_view token, bool allow_bare_hour) {
    const std::size_t colon = token.find(':');
    if (colon == std::string_view::npos) {
        if (!allow_bare_hour || token.size() > 2 || !all_digits(token)) return std::nullopt;
        return ClockTime{std::stoi(std::string(token)), 0};
    }
    const std::string_view h = token.substr(0, colon);
    const std::string_view m = token.substr(colon + 1);
    if (m.find(':') != std::string_view::npos) return std::nullopt;
    if (h.empty() || h.size() > 2 || m.size() != 2 || !all_digits(h) || !all_digits(m))
        return std::nullopt;
    return ClockTime{std::stoi(std::string(h)), std::stoi(std::string(m))};
}

inline std::optional<ClockTime> clock_time(std::string_view token) {
    std::string compact;
    for (const char c : token)
        if (c != ' ' && c != '.') compact.push_back(c);
    const bool am = compact.size() >= 2 && compact.compare(compact.size() - 2, 2, "am") == 0;
    const bool pm = compact.size() >= 2 && compact.compare(compact.size() - 2, 2, "pm") == 0;
    if (am || pm) {
        const auto parsed = hour_minute(std::string_view(compact).substr(0, compact.size() - 2), true);
        if (!parsed || parsed->hour < 1 || parsed->hour > 12 || parsed->minute > 59) return std::nullopt;
        const int midnight_based = parsed->hour == 12 ? 0 : parsed->hour;
        return ClockTime{pm ? midnight_based + 12 : midnight_based, parsed->minute};
    }
    const auto parsed = hour_minute(compact, false);
    if (!parsed || parsed->hour > 23 || parsed->minute > 59) return std::nullopt;
    return parsed;
}

inline bool is_day_word(std::string_view word) {
    if (word == kTomorrowWord || word == kNextWorkdayWord) return true;
    for (const std::string_view w : kWeekdayWords)
        if (w == word) return true;
    return false;
}

struct DayAndTime {
    std::string day;
    ClockTime time;
};

inline std::optional<DayAndTime> day_and_time(std::string_view text) {
    std::string stripped(text);
    for (char& c : stripped)
        if (c == ',') c = ' ';
    const std::vector<std::string> words = split_words(stripped);
    if (words.empty()) return std::nullopt;
    const std::string whole = join_words(words, 0, words.size());
    if (is_day_word(whole)) return DayAndTime{whole, ClockTime{kDefaultNamedHour, 0}};

    std::vector<std::string> kept;
    for (const std::string& w : words)
        if (w != "at" && w != "on") kept.push_back(w);
    if (kept.size() < 2) return std::nullopt;

    const std::size_t max_time_words = std::min<std::size_t>(2, kept.size() - 1);
    for (std::size_t time_words = 1; time_words <= max_time_words; ++time_words) {
        const std::string head = join_words(kept, 0, kept.size() - time_words);
        const std::string tail = join_words(kept, kept.size() - time_words, kept.size());
        if (is_day_word(head))
            if (const auto t = clock_time(tail)) return DayAndTime{head, *t};
        const std::string reversed_head = join_words(kept, time_words, kept.size());
        const std::string reversed_tail = join_words(kept, 0, time_words);
        if (is_day_word(reversed_head))
            if (const auto t = clock_time(reversed_tail)) return DayAndTime{reversed_head, *t};
    }
    return std::nullopt;
}

inline std::optional<std::vector<int>> named_day_offsets(std::string_view day_word, Seconds now,
                                                         const LocalCalendar& cal) {
    if (day_word == kTomorrowWord) return std::vector<int>{1};
    const int weekday = cal.to_local(now).tm_wday + 1;
    if (day_word == kNextWorkdayWord)
        return std::vector<int>{snooze_presets::days_to_next_workday(weekday)};
    int target = -1;
    for (int i = 0; i < 7; ++i)
        if (kWeekdayWords[i] == day_word) target = i;
    if (target < 0) return std::nullopt;
    const int today = weekday - 1;
    if (target == today) return std::vector<int>{0, 7};
    return std::vector<int>{(target - today + 7) % 7};
}

inline std::optional<Seconds> strict_wall_clock(Seconds now, int days_ahead, int hour, int minute,
                                                const LocalCalendar& cal) {
    std::tm local = cal.to_local(now);
    local.tm_mday += days_ahead;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    const Seconds at = cal.from_local(local);
    if (at == static_cast<Seconds>(static_cast<std::time_t>(-1))) return std::nullopt;
    const std::tm back = cal.to_local(at);
    if (back.tm_hour != hour || back.tm_min != minute) return std::nullopt;
    return at;
}

inline std::optional<Seconds> next_wall_clock(Seconds now, int hour, int minute,
                                              const LocalCalendar& cal) {
    for (int days = 0; days <= 7; ++days) {
        const auto candidate = strict_wall_clock(now, days, hour, minute, cal);
        if (candidate && *candidate > now) return candidate;
    }
    return std::nullopt;
}

inline Command bounded(Seconds until, Choice choice, Seconds now) {
    if (snooze_presets::custom(until, now).kind != snooze_presets::CustomVerdict::Kind::Accepted)
        return Command::invalid(Failure::OutOfRange);
    return Command::set(choice, until);
}

inline Command parse(std::string_view argument, Seconds now, const LocalCalendar& cal = {}) {
    const std::string text = normalized(argument);
    if (text.empty()) return Command::picker();
    if (character_count(text) > kMaxArgumentCharacters) return Command::invalid(Failure::Unrecognized);
    if (text == kClearWord) return Command::clear();

    if (const auto offset = relative_seconds(text)) return bounded(now + *offset, Choice::Relative, now);

    if (const auto bare = clock_time(text)) {
        const auto until = next_wall_clock(now, bare->hour, bare->minute, cal);
        if (!until) return Command::invalid(Failure::NonexistentTime);
        return bounded(*until, Choice::Time, now);
    }

    const auto dt = day_and_time(text);
    if (!dt) return Command::invalid(Failure::Unrecognized);
    const auto offsets = named_day_offsets(dt->day, now, cal);
    if (!offsets) return Command::invalid(Failure::Unrecognized);
    for (const int days : *offsets) {
        const auto until = strict_wall_clock(now, days, dt->time.hour, dt->time.minute, cal);
        if (until && *until > now) return bounded(*until, Choice::Named, now);
    }
    return Command::invalid(Failure::NonexistentTime);
}

struct PromptOutcome {
    enum class Kind { Set, Quiet, Refuse };
    Kind kind = Kind::Quiet;
    Seconds until = 0;
    std::string_view hint;
    bool operator==(const PromptOutcome&) const = default;
};

inline PromptOutcome outcome_of_submitted_phrase(std::string_view phrase, Seconds submitted_at,
                                                 const LocalCalendar& cal = {}) {
    const Command c = parse(phrase, submitted_at, cal);
    switch (c.kind) {
        case Command::Kind::Set: return {PromptOutcome::Kind::Set, c.until, {}};
        case Command::Kind::Clear:
        case Command::Kind::Picker: return {PromptOutcome::Kind::Quiet, 0, {}};
        case Command::Kind::Invalid: return {PromptOutcome::Kind::Refuse, 0, hint(c.failure)};
    }
    return {};
}

}
