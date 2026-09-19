#pragma once

namespace hanabi::appearance {

enum class Scheme { Dark, Light };

using Sink = void (*)(Scheme);

inline Sink& sink() {
    static Sink s = nullptr;
    return s;
}

inline Scheme& last_published() {
    static Scheme s = Scheme::Dark;
    return s;
}

inline bool& published_once() {
    static bool once = false;
    return once;
}

inline void publish(Scheme scheme) {
    last_published() = scheme;
    published_once() = true;
    if (sink() != nullptr) sink()(scheme);
}

inline void install_sink(Sink s) {
    sink() = s;
    if (s != nullptr && published_once()) s(last_published());
}

inline void reset_for_test() {
    sink() = nullptr;
    last_published() = Scheme::Dark;
    published_once() = false;
}

}
