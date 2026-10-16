#pragma once

#include <string>
#include <string_view>

namespace hanabi::share {

struct Payload {
    std::string text;
    std::string notice;
};

inline Payload session_link(std::string_view title, std::string_view web_url,
                            std::string_view deep_link) {
    Payload out;
    const std::string_view link = web_url.empty() ? deep_link : web_url;
    if (!title.empty()) {
        out.text = std::string(title) + "\n" + std::string(link);
    } else {
        out.text = std::string(link);
    }
    out.notice = web_url.empty() ? "Copied the conversation's link (no web address is set)"
                                 : "Copied the conversation's link";
    return out;
}

inline Payload turn(std::string_view turn_text, std::string_view web_url,
                    std::string_view deep_link) {
    Payload out;
    const std::string_view link = web_url.empty() ? deep_link : web_url;
    out.text = std::string(turn_text);
    if (!out.text.empty() && out.text.back() != '\n') out.text += "\n";
    out.text += "\n\xe2\x80\x94 " + std::string(link);
    out.notice = web_url.empty() ? "Copied the turn with the conversation's link (no web address is set)"
                                 : "Copied the turn with its link";
    return out;
}

}
