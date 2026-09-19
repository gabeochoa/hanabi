#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace hanabi::native_snooze_prompt {

inline constexpr const char* kTitle = "Snooze Until";
inline constexpr const char* kBody = "The thread comes back when this time arrives.";
inline constexpr const char* kSubmitButton = "Snooze";
inline constexpr const char* kCancelButton = "Cancel";
inline constexpr const char* kPlaceholder = "9am on Monday";
inline constexpr const char* kRefusalTitle = "That thread was not snoozed";
inline constexpr const char* kRefusalButton = "OK";
inline constexpr float kFieldWidth = 260.0f;
inline constexpr float kFieldHeight = 24.0f;

struct Request {
    std::uint64_t generation = 0;
    std::string scope;
    std::string debug_name;
};

struct Result {
    enum class Kind { Cancelled, Quiet, Refused, Set };
    std::uint64_t generation = 0;
    Kind kind = Kind::Cancelled;
    std::int64_t until_unix_sec = 0;
    std::int64_t submitted_at_unix_sec = 0;
    bool operator==(const Result&) const = default;
};

class Lifecycle {
public:
    std::uint64_t request(Request req) {
        std::lock_guard<std::mutex> lock(mu_);
        if (queued_.has_value() || dispatched_ != 0 || showing_ != 0) return 0;
        req.generation = next_++;
        result_.reset();
        queued_ = std::move(req);
        return queued_->generation;
    }

    bool busy() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queued_.has_value() || dispatched_ != 0 || showing_ != 0;
    }

    std::uint64_t current_generation() const {
        std::lock_guard<std::mutex> lock(mu_);
        if (queued_.has_value()) return queued_->generation;
        if (dispatched_ != 0) return dispatched_;
        return showing_;
    }

    std::optional<Request> take_for_dispatch() {
        std::lock_guard<std::mutex> lock(mu_);
        if (!queued_.has_value() || dispatched_ != 0 || showing_ != 0) return std::nullopt;
        std::optional<Request> take = std::move(queued_);
        queued_.reset();
        dispatched_ = take->generation;
        return take;
    }

    bool begin_show(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mu_);
        if (dispatched_ != generation) return false;
        dispatched_ = 0;
        showing_ = generation;
        return true;
    }

    void deliver(Result r) {
        std::lock_guard<std::mutex> lock(mu_);
        if (r.generation != showing_) return;
        result_ = r;
        showing_ = 0;
    }

    bool take_result(std::uint64_t generation, Result* out) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!result_.has_value() || result_->generation != generation) return false;
        if (out != nullptr) *out = *result_;
        result_.reset();
        return true;
    }

    void cancel(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mu_);
        if (generation == 0) return;
        if (queued_.has_value() && queued_->generation == generation) queued_.reset();
        if (dispatched_ == generation) dispatched_ = 0;
    }

    void inject_result_for_test(Result r) {
        std::lock_guard<std::mutex> lock(mu_);
        bool owned = false;
        if (queued_.has_value() && queued_->generation == r.generation) {
            queued_.reset();
            owned = true;
        }
        if (dispatched_ == r.generation) {
            dispatched_ = 0;
            owned = true;
        }
        if (showing_ == r.generation) {
            showing_ = 0;
            owned = true;
        }
        if (owned) result_ = r;
    }

    bool is_showing(std::uint64_t generation) const {
        std::lock_guard<std::mutex> lock(mu_);
        return showing_ == generation;
    }

private:
    mutable std::mutex mu_;
    std::uint64_t next_ = 1;
    std::optional<Request> queued_;
    std::uint64_t dispatched_ = 0;
    std::uint64_t showing_ = 0;
    std::optional<Result> result_;
};

template <typename Permitted, typename RunModal>
Result show_or_refuse(Lifecycle& life, const Request& req, Permitted&& permitted,
                      RunModal&& run_modal) {
    Result r;
    r.generation = req.generation;
    if (!life.begin_show(req.generation)) return r;
    if (!permitted()) {
        life.deliver(r);
        return r;
    }
    r = run_modal(req);
    r.generation = req.generation;
    life.deliver(r);
    return r;
}

bool available();
std::uint64_t request(Request req);
bool busy();
std::uint64_t current_generation();
void pump_after_frame();
bool take_result(std::uint64_t generation, Result* out);
void cancel(std::uint64_t generation);

void inject_result_for_test(Result r);

}
