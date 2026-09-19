#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::snooze_presets {

using Seconds = std::int64_t;

inline constexpr Seconds kMaxHorizonSeconds = 365 * 24 * 60 * 60;
inline constexpr Seconds kCustomDefaultAhead = 24 * 3600;
inline constexpr int kEveningHour = 17;
inline constexpr int kMorningHour = 8;

enum class Choice { NextWorkday8am, OneHour, ThreeHours, OneWeek, Today5pm, Tomorrow8am };

inline constexpr const char* raw_name(Choice c) {
    switch (c) {
        case Choice::NextWorkday8am: return "next_workday_8am";
        case Choice::OneHour: return "one_hour";
        case Choice::ThreeHours: return "three_hours";
        case Choice::OneWeek: return "one_week";
        case Choice::Today5pm: return "today_5pm";
        case Choice::Tomorrow8am: return "tomorrow_8am";
    }
    return "";
}

inline constexpr const char* title(Choice c) {
    switch (c) {
        case Choice::OneHour: return "In 1 Hour";
        case Choice::ThreeHours: return "In 3 Hours";
        case Choice::OneWeek: return "In 1 Week";
        case Choice::Today5pm: return "This Evening";
        case Choice::Tomorrow8am: return "Tomorrow Morning";
        case Choice::NextWorkday8am: return "Next Workday";
    }
    return "";
}

struct Option {
    Choice choice;
    Seconds until;
    bool operator==(const Option&) const = default;
};

struct CustomVerdict {
    enum class Kind { Accepted, NotInTheFuture, PastTheHorizon };
    Kind kind;
    Seconds until;

    [[nodiscard]] std::optional<std::string_view> refusal() const {
        switch (kind) {
            case Kind::Accepted: return std::nullopt;
            case Kind::NotInTheFuture:
                return "That time has already passed. Pick one that is still ahead.";
            case Kind::PastTheHorizon: return "A thread can be snoozed at most a year ahead.";
        }
        return std::nullopt;
    }
    bool operator==(const CustomVerdict&) const = default;
};

inline CustomVerdict custom(Seconds until, Seconds now) {
    if (until <= now) return {CustomVerdict::Kind::NotInTheFuture, until};
    if (until > now + kMaxHorizonSeconds) return {CustomVerdict::Kind::PastTheHorizon, until};
    return {CustomVerdict::Kind::Accepted, until};
}

inline bool is_in_range(Seconds until, Seconds now) {
    return custom(until, now).kind == CustomVerdict::Kind::Accepted;
}

inline Seconds custom_default(Seconds now) { return now + kCustomDefaultAhead; }

inline int days_to_next_workday(int weekday_sunday_is_1) {
    switch (weekday_sunday_is_1) {
        case 6: return 3;
        case 7: return 2;
        default: return 1;
    }
}

struct LocalCalendar {
    std::tm (*to_local)(Seconds) = [](Seconds at) {
        std::tm out{};
        const std::time_t t = static_cast<std::time_t>(at);
        localtime_r(&t, &out);
        return out;
    };
    Seconds (*from_local)(std::tm) = [](std::tm local) {
        local.tm_isdst = -1;
        return static_cast<Seconds>(std::mktime(&local));
    };
};

inline std::optional<Seconds> wall_clock(Seconds now, int days_ahead, int hour,
                                         const LocalCalendar& cal) {
    std::tm local = cal.to_local(now);
    local.tm_mday += days_ahead;
    local.tm_hour = hour;
    local.tm_min = 0;
    local.tm_sec = 0;
    const Seconds at = cal.from_local(local);
    if (at == static_cast<Seconds>(static_cast<std::time_t>(-1))) return std::nullopt;
    return at;
}

inline std::vector<Option> options(Seconds now, const LocalCalendar& cal = {}) {
    std::map<Seconds, Option> by_instant;
    const auto add_wall_clock = [&](Choice choice, std::optional<Seconds> until) {
        if (!until || !is_in_range(*until, now)) return;
        by_instant.try_emplace(*until, Option{choice, *until});
    };
    add_wall_clock(Choice::Today5pm, wall_clock(now, 0, kEveningHour, cal));
    add_wall_clock(Choice::Tomorrow8am, wall_clock(now, 1, kMorningHour, cal));
    const int weekday = cal.to_local(now).tm_wday + 1;
    add_wall_clock(Choice::NextWorkday8am,
                   wall_clock(now, days_to_next_workday(weekday), kMorningHour, cal));

    for (const auto& [choice, ahead] : {std::pair{Choice::OneHour, Seconds{3600}},
                                       std::pair{Choice::ThreeHours, Seconds{3 * 3600}},
                                       std::pair{Choice::OneWeek, Seconds{7 * 24 * 3600}}}) {
        const Seconds until = now + ahead;
        if (!is_in_range(until, now)) continue;
        by_instant.insert_or_assign(until, Option{choice, until});
    }

    std::vector<Option> out;
    out.reserve(by_instant.size());
    for (const auto& [_, option] : by_instant) out.push_back(option);
    return out;
}

inline std::int64_t days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yoe = year - era * 400;
    const std::int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline std::int64_t civil_day(const std::tm& local) {
    return days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

inline std::string clock_text(const std::tm& local) {
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    std::string out = std::to_string(hour12) + ":";
    if (local.tm_min < 10) out += "0";
    out += std::to_string(local.tm_min);
    out += local.tm_hour < 12 ? " AM" : " PM";
    return out;
}

inline constexpr const char* kWeekdayNames[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                                 "Thursday", "Friday", "Saturday"};
inline constexpr const char* kMonthAbbreviations[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

inline std::string due_text(Seconds until, Seconds now, const LocalCalendar& cal = {}) {
    const std::tm until_local = cal.to_local(until);
    const std::tm now_local = cal.to_local(now);
    const std::int64_t days = civil_day(until_local) - civil_day(now_local);
    const std::string clock = clock_text(until_local);
    if (days == 0) return clock;
    if (days == 1) return "tomorrow, " + clock;
    if (days >= 2 && days <= 6)
        return std::string(kWeekdayNames[until_local.tm_wday]) + ", " + clock;
    return std::to_string(until_local.tm_mday) + " " + kMonthAbbreviations[until_local.tm_mon] +
           ", " + clock;
}

inline std::string option_text(const Option& option, Seconds now, const LocalCalendar& cal = {}) {
    return std::string(title(option.choice)) + " \xe2\x80\x94 " + due_text(option.until, now, cal);
}

inline std::string snoozed_until_text(Seconds until, Seconds now, const LocalCalendar& cal = {}) {
    return "Snoozed until " + due_text(until, now, cal);
}

}  // namespace hanabi::snooze_presets
