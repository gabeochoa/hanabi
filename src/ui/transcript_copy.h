#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../api/types.h"

namespace hanabi::transcript_copy {

inline std::string clipboard_text(std::string_view content) {
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r'))
        content.remove_suffix(1);
    return std::string(content);
}

inline bool is_worth_showing(std::string_view content) {
    return !clipboard_text(content).empty();
}

inline bool is_turn_start(const api::Message& m) {
    return m.role == api::Role::User && m.kind == api::EventKind::Text;
}

inline bool offers_copy_message(const api::Message& m) {
    if (m.role == api::Role::Tool || m.role == api::Role::System) return false;
    switch (m.kind) {
        case api::EventKind::Text:
        case api::EventKind::Delivery: return is_worth_showing(m.text);
        default: return false;
    }
}

inline int index_of(const std::vector<api::Message>& messages, std::string_view id) {
    if (id.empty()) return -1;
    for (std::size_t i = 0; i < messages.size(); ++i)
        if (messages[i].id == id) return static_cast<int>(i);
    return -1;
}

struct TurnSpan {
    int first = -1;
    int last = -1;
    bool empty() const { return first < 0; }
};
inline TurnSpan turn_around(const std::vector<api::Message>& messages, std::string_view id) {
    const int at = index_of(messages, id);
    if (at < 0) return {};
    int start = at;
    while (start > 0 && !is_turn_start(messages[static_cast<std::size_t>(start)])) --start;
    int end = at;
    while (end + 1 < static_cast<int>(messages.size()) &&
           !is_turn_start(messages[static_cast<std::size_t>(end + 1)]))
        ++end;
    return {start, end};
}

inline std::string lowercased(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool is_thinking_text(const api::Message& m) {
    return m.role == api::Role::Assistant && m.subtitle == "thinking";
}

inline std::string tool_line(const api::Message& m) {
    std::string line = "tool: " + (m.subtitle.empty() ? std::string("unknown") : m.subtitle);
    if (!m.tool_node.empty()) line += " on " + m.tool_node;
    if (!m.tool_status.empty()) line += " \xe2\x80\x94 " + m.tool_status;
    return line;
}

inline std::string body_of(const api::Message& m) {
    std::string out;
    if (m.role == api::Role::Tool) {
        out = "*" + tool_line(m) + "*\n\n";
    } else {
        switch (m.kind) {
            case api::EventKind::Thinking: return "";
            case api::EventKind::Compaction: out = "*(context compacted)*\n\n"; break;
            case api::EventKind::Skill:
                out = "*(skill: " + (m.text.empty() ? m.subtitle : m.text) + ")*\n\n";
                break;
            case api::EventKind::Node:
            case api::EventKind::Status:
            case api::EventKind::Plan:
            case api::EventKind::Goal:
            case api::EventKind::SubAgent: out = "*(" + lowercased(m.text) + ")*\n\n"; break;
            case api::EventKind::Notice: out = "> **notice** \xe2\x80\x94 " + m.text + "\n\n"; break;
            case api::EventKind::Unsupported:
                out = "> **" + (m.subtitle.empty() ? std::string("unknown event") : m.subtitle) +
                      "** \xe2\x80\x94 " + m.text + "\n\n";
                break;
            case api::EventKind::Delivery:
                out = "### **Delivered**" + (m.subtitle.empty() ? "" : " (" + m.subtitle + ")") +
                      "\n\n" + m.text + "\n\n";
                break;
            case api::EventKind::ToolCall: out = "*" + tool_line(m) + "*\n\n"; break;
            case api::EventKind::Text:
            default:
                if (m.role == api::Role::User) {
                    out = "### **You**\n\n" + m.text + "\n\n";
                } else if (m.role == api::Role::System) {
                    out = "*(" + lowercased(m.text) + ")*\n\n";
                } else if (is_thinking_text(m)) {
                    return "";
                } else {
                    out = "### **Agentcloud**\n\n" + m.text + "\n\n";
                }
                break;
        }
    }
    if (!m.run_outcome.empty()) out += "---\n\n*(turn " + m.run_outcome + ")*\n\n";
    return out;
}

inline std::string render_turn(const std::vector<api::Message>& messages, TurnSpan span) {
    std::string out;
    if (span.empty()) return out;
    for (int i = span.first; i <= span.last; ++i) out += body_of(messages[static_cast<std::size_t>(i)]);
    while (out.size() >= 2 && out.compare(out.size() - 2, 2, "\n\n") == 0) out.pop_back();
    return out;
}

inline std::string copy_message_payload(const std::vector<api::Message>& messages,
                                        std::string_view id) {
    const int at = index_of(messages, id);
    if (at < 0) return {};
    const api::Message& m = messages[static_cast<std::size_t>(at)];
    if (!offers_copy_message(m)) return {};
    return clipboard_text(m.text);
}
inline std::string copy_turn_payload(const std::vector<api::Message>& messages,
                                     std::string_view id) {
    return render_turn(messages, turn_around(messages, id));
}

}
