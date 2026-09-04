#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.h"

namespace api::elicitation {

using nlohmann::json;

inline std::string trimmed(const std::string& in) {
    std::size_t b = 0;
    std::size_t e = in.size();
    while (b < e && (std::isspace(static_cast<unsigned char>(in[b])) != 0)) ++b;
    while (e > b && (std::isspace(static_cast<unsigned char>(in[e - 1])) != 0))
        --e;
    return in.substr(b, e - b);
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool natural_less(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            std::size_t ia = i;
            std::size_t jb = j;
            while (ia < a.size() &&
                   std::isdigit(static_cast<unsigned char>(a[ia])) != 0)
                ++ia;
            while (jb < b.size() &&
                   std::isdigit(static_cast<unsigned char>(b[jb])) != 0)
                ++jb;
            std::string na = a.substr(i, ia - i);
            std::string nb = b.substr(j, jb - j);
            na.erase(0, std::min(na.find_first_not_of('0'), na.size() - 1));
            nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size() - 1));
            if (na.size() != nb.size()) return na.size() < nb.size();
            if (na != nb) return na < nb;
            i = ia;
            j = jb;
            continue;
        }
        if (a[i] != b[j]) return a[i] < b[j];
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

namespace detail {

inline std::string str_field(const json& j, const char* key,
                             const std::string& dflt = "") {
    if (!j.is_object() || !j.contains(key)) return dflt;
    const json& v = j.at(key);
    return v.is_string() ? v.get<std::string>() : dflt;
}

inline int64_t int_field(const json& j, const char* key, int64_t dflt = 0) {
    if (!j.is_object() || !j.contains(key)) return dflt;
    const json& v = j.at(key);
    return v.is_number_integer() ? v.get<int64_t>() : dflt;
}

inline const json* array_field(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_array())
        return nullptr;
    return &j.at(key);
}

inline const json* object_field(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_object())
        return nullptr;
    return &j.at(key);
}

inline bool option_list_from(const json& entries,
                             std::vector<AskOption>* out) {
    for (const json& e : entries) {
        if (!e.is_object() || !e.contains("const")) continue;
        const json& c = e.at("const");
        if (!c.is_string()) continue;
        AskOption option;
        option.value = c.get<std::string>();
        const std::string title = str_field(e, "title");
        option.label = title.empty() ? option.value : title;
        static const std::string kFold = " \xE2\x80\x94 ";
        const std::size_t at = option.label.find(kFold);
        if (at != std::string::npos &&
            option.label.substr(0, at) == option.value) {
            option.detail = option.label.substr(at + kFold.size());
            option.label = option.value;
        }
        out->push_back(std::move(option));
    }
    return !out->empty();
}

inline bool enum_options_from(const json& body, const json& items,
                              std::vector<AskOption>* out) {
    const json* values = array_field(body, "enum");
    if (values == nullptr && items.is_object())
        values = array_field(items, "enum");
    if (values == nullptr) return false;
    const json* names = array_field(body, "enumNames");
    if (names == nullptr && items.is_object())
        names = array_field(items, "enumNames");
    std::size_t index = 0;
    for (const json& v : *values) {
        if (!v.is_string()) {
            ++index;
            continue;
        }
        AskOption option;
        option.value = v.get<std::string>();
        option.label = option.value;
        if (names != nullptr && index < names->size() &&
            names->at(index).is_string())
            option.label = names->at(index).get<std::string>();
        out->push_back(std::move(option));
        ++index;
    }
    return !out->empty();
}

inline AskValueType value_type_of(const json& body) {
    const std::string type = str_field(body, "type");
    if (type == "number") return AskValueType::Number;
    if (type == "integer") return AskValueType::Integer;
    if (type == "boolean") return AskValueType::Boolean;
    return AskValueType::String;
}

inline void classify_property(const json& body, AskQuestion* q) {
    const json kNull = json::object();
    const json& items = body.is_object() && body.contains("items") &&
                                body.at("items").is_object()
                            ? body.at("items")
                            : kNull;
    const bool is_array = str_field(body, "type") == "array";

    const json* oneOf = array_field(body, "oneOf");
    const json* itemsAnyOf = array_field(items, "anyOf");
    const json* anyOf = array_field(body, "anyOf");
    const json* entries = oneOf != nullptr      ? oneOf
                          : itemsAnyOf != nullptr ? itemsAnyOf
                                                  : anyOf;
    if (entries != nullptr && option_list_from(*entries, &q->options)) {
        q->control = (is_array || itemsAnyOf != nullptr) ? AskControl::Multi
                                                         : AskControl::Single;
        return;
    }
    q->options.clear();
    if (enum_options_from(body, items, &q->options)) {
        q->control = is_array ? AskControl::Multi : AskControl::Single;
        return;
    }
    q->options.clear();
    q->control = AskControl::Text;
    q->value_type = value_type_of(body);
}

}  // namespace detail

inline json typed_value(AskValueType type, const std::string& text) {
    if (type == AskValueType::Boolean) {
        if (text == "true") return true;
        if (text == "false") return false;
        return text;
    }
    if (type == AskValueType::Integer) {
        try {
            std::size_t used = 0;
            const long long v = std::stoll(text, &used);
            if (used == text.size()) return v;
        } catch (const std::exception&) {
        }
        return text;
    }
    if (type == AskValueType::Number) {
        try {
            std::size_t used = 0;
            const double v = std::stod(text, &used);
            if (used == text.size()) return v;
        } catch (const std::exception&) {
        }
        return text;
    }
    return text;
}

inline std::vector<AskQuestion> parse_schema(
    const std::string& schema_json,
    const std::vector<std::string>& file_keys) {
    std::vector<AskQuestion> out;
    if (trimmed(schema_json).empty()) return out;
    const json schema = json::parse(schema_json, nullptr, false);
    if (schema.is_discarded()) return out;
    const json* properties = detail::object_field(schema, "properties");
    if (properties == nullptr) return out;

    std::vector<std::string> keys;
    for (auto it = properties->begin(); it != properties->end(); ++it)
        keys.push_back(it.key());
    std::sort(keys.begin(), keys.end(), natural_less);

    std::map<std::string, AskQuestion> built;
    for (const std::string& key : keys) {
        AskQuestion q;
        q.key = key;
        const json& body = properties->at(key);
        q.prompt = detail::str_field(body, "title", key);
        detail::classify_property(body, &q);
        if (q.control == AskControl::Text &&
            std::find(file_keys.begin(), file_keys.end(), key) !=
                file_keys.end())
            q.control = AskControl::File;
        built.emplace(key, std::move(q));
    }

    std::vector<std::string> claimed;
    for (const std::string& key : keys) {
        static const std::string kSuffix = "_other";
        if (key.size() <= kSuffix.size() ||
            key.compare(key.size() - kSuffix.size(), kSuffix.size(),
                        kSuffix) != 0)
            continue;
        const std::string owner = key.substr(0, key.size() - kSuffix.size());
        auto o = built.find(owner);
        auto c = built.find(key);
        if (o == built.end() || c == built.end()) continue;
        if (o->second.control != AskControl::Single &&
            o->second.control != AskControl::Multi)
            continue;
        if (c->second.control != AskControl::Text) continue;
        o->second.free_text_key = key;
        o->second.free_text_label =
            c->second.prompt == key ? "Other" : c->second.prompt;
        claimed.push_back(key);
    }

    for (const std::string& key : keys) {
        if (std::find(claimed.begin(), claimed.end(), key) != claimed.end())
            continue;
        out.push_back(built.at(key));
    }
    return out;
}

inline PendingAsk ask_from_entry(const json& entry,
                                 const std::string& owner_session,
                                 const std::string& child_session) {
    PendingAsk ask;
    ask.owner_session = owner_session;
    ask.child_session = child_session;
    ask.seq = static_cast<std::uint64_t>(detail::int_field(entry,
                                                           "elicitation", 0));
    ask.tool = detail::str_field(entry, "tool");
    ask.message = detail::str_field(entry, "message");
    ask.timeout_ms = detail::int_field(entry, "timeout_ms", 0);
    ask.input = detail::str_field(entry, "input");
    ask.kind = detail::str_field(entry, "kind") == "approval"
                   ? AskKind::Approval
                   : AskKind::Form;

    std::vector<std::string> file_keys;
    if (const json* keys = detail::array_field(entry, "file_keys"))
        for (const json& k : *keys)
            if (k.is_string()) file_keys.push_back(k.get<std::string>());

    const std::string schema = detail::str_field(entry, "requested_schema");
    ask.questions = parse_schema(schema, file_keys);
    if (!child_session.empty())
        for (AskQuestion& q : ask.questions)
            if (q.control == AskControl::Text && !ends_with(q.key, "_other"))
                q.control = AskControl::File;
    ask.schema_unreadable =
        ask.kind == AskKind::Form && !trimmed(schema).empty() &&
        ask.questions.empty();
    return ask;
}

inline bool ask_entry_from_frame(const std::string& frame_json,
                                 json* entry) {
    const json root = json::parse(frame_json, nullptr, false);
    if (root.is_discarded()) return false;
    const json* wrapped = detail::object_field(root, "event");
    const json& event = wrapped != nullptr ? *wrapped : root;
    if (detail::str_field(event, "type") != "elicitation_requested")
        return false;
    const int64_t seq = detail::int_field(root, "seq", 0);
    if (seq <= 0) return false;
    json out = json::object();
    out["elicitation"] = seq;
    out["tool"] = detail::str_field(event, "tool");
    out["message"] = detail::str_field(event, "message");
    out["requested_schema"] = detail::str_field(event, "requested_schema");
    out["timeout_ms"] = detail::int_field(event, "timeout_ms", 0);
    const std::string kind = detail::str_field(event, "kind");
    if (!kind.empty()) out["kind"] = kind;
    if (event.is_object() && event.contains("input") &&
        event.at("input").is_string())
        out["input"] = event.at("input");
    if (const json* keys = detail::array_field(event, "file_keys"))
        out["file_keys"] = *keys;
    if (const json* limits = detail::object_field(event, "file_limits"))
        out["file_limits"] = *limits;
    *entry = std::move(out);
    return true;
}

inline std::vector<PendingAsk> asks_from_state(
    const json& state, const std::string& owner_session) {
    std::vector<PendingAsk> out;
    if (const json* own = detail::array_field(state, "pending_elicitations"))
        for (const json& e : *own) {
            if (!e.is_object() || !e.contains("elicitation")) continue;
            out.push_back(ask_from_entry(e, owner_session, ""));
        }
    std::sort(out.begin(), out.end(),
              [](const PendingAsk& a, const PendingAsk& b) {
                  return a.seq < b.seq;
              });

    std::vector<PendingAsk> children;
    if (const json* kids =
            detail::array_field(state, "child_pending_elicitations"))
        for (const json& e : *kids) {
            if (!e.is_object()) continue;
            const std::string session = detail::str_field(e, "session");
            const json* nested = detail::object_field(e, "elicitation");
            if (session.empty() || nested == nullptr) continue;
            children.push_back(
                ask_from_entry(*nested, owner_session, session));
        }
    std::sort(children.begin(), children.end(),
              [](const PendingAsk& a, const PendingAsk& b) {
                  if (a.child_session != b.child_session)
                      return a.child_session < b.child_session;
                  return a.seq < b.seq;
              });
    out.insert(out.end(), children.begin(), children.end());
    return out;
}

inline json content_object(const PendingAsk& ask, const AskAnswer& answer) {
    json content = json::object();
    if (ask.kind == AskKind::Approval) return content;
    for (const AskQuestion& q : ask.questions) {
        if (q.control == AskControl::File) continue;
        if (q.control == AskControl::Text) {
            const auto it = answer.text.find(q.key);
            if (it == answer.text.end()) continue;
            const std::string value = trimmed(it->second);
            if (value.empty()) continue;
            content[q.key] = typed_value(q.value_type, value);
            continue;
        }
        const auto picked = answer.picks.find(q.key);
        if (picked != answer.picks.end() && !picked->second.empty()) {
            std::vector<std::string> values = picked->second;
            if (q.control == AskControl::Multi) {
                std::sort(values.begin(), values.end());
                content[q.key] = values;
            } else {
                content[q.key] = values.front();
            }
        }
        if (!q.free_text_key.empty()) {
            const auto typed = answer.text.find(q.free_text_key);
            if (typed == answer.text.end()) continue;
            const std::string value = trimmed(typed->second);
            if (!value.empty()) content[q.free_text_key] = value;
        }
    }
    return content;
}

inline bool answer_has_content(const PendingAsk& ask,
                               const AskAnswer& answer) {
    if (ask.kind == AskKind::Approval) return true;
    for (const AskQuestion& q : ask.questions) {
        if (q.control == AskControl::File) continue;
        if (q.control == AskControl::Text) {
            const auto it = answer.text.find(q.key);
            if (it != answer.text.end() && !trimmed(it->second).empty())
                return true;
            continue;
        }
        const auto picked = answer.picks.find(q.key);
        if (picked != answer.picks.end() && !picked->second.empty())
            return true;
        if (q.free_text_key.empty()) continue;
        const auto typed = answer.text.find(q.free_text_key);
        if (typed != answer.text.end() && !trimmed(typed->second).empty())
            return true;
    }
    return false;
}

inline constexpr std::size_t kContentCapBytes = 262144;

inline constexpr const char* kAskGoneReason =
    "this question is no longer pending";

inline constexpr std::size_t kApprovalInputCap = 4000;

inline std::string capped_input(const std::string& in) {
    if (in.size() <= kApprovalInputCap) return in;
    return in.substr(0, kApprovalInputCap) + "… (truncated)";
}

inline std::size_t escaped_content_len(const json& content) {
    return json(content.dump()).dump().size();
}

inline bool answer_within_cap(const PendingAsk& ask,
                              const AskAnswer& answer) {
    if (ask.kind == AskKind::Approval) return true;
    return escaped_content_len(content_object(ask, answer)) <= kContentCapBytes;
}

inline const char* action_word(AskAction action) {
    switch (action) {
        case AskAction::Accept: return "accept";
        case AskAction::Decline: return "decline";
        case AskAction::Cancel: return "cancel";
    }
    return "cancel";
}

inline std::string resolve_command_json(const PendingAsk& ask,
                                        AskAction action,
                                        const AskAnswer& answer) {
    json payload = {{"cmd", "resolve_elicitation"},
                    {"elicitation", ask.seq},
                    {"action", action_word(action)}};
    if (action == AskAction::Accept && ask.kind != AskKind::Approval) {
        const json content = content_object(ask, answer);
        if (!content.empty()) payload["content"] = content;
    }
    if (!ask.child_session.empty()) payload["session"] = ask.child_session;
    return payload.dump();
}

inline bool fold_ask_resolved(const std::string& frame_json,
                              std::uint64_t* seq, std::string* action,
                              std::string* by) {
    const json root = json::parse(frame_json, nullptr, false);
    if (root.is_discarded()) return false;
    const json* frame = detail::object_field(root, "event");
    const json& event = frame != nullptr ? *frame : root;
    if (detail::str_field(event, "type") != "elicitation_resolved")
        return false;
    if (!event.contains("elicitation")) return false;
    if (seq != nullptr)
        *seq = static_cast<std::uint64_t>(
            detail::int_field(event, "elicitation", 0));
    if (action != nullptr) {
        const std::string word = detail::str_field(event, "action");
        *action = word.empty() ? "cancel" : word;
    }
    if (by != nullptr) {
        const json* who = detail::object_field(event, "by");
        *by = who != nullptr ? detail::str_field(*who, "by") : "";
    }
    return true;
}

inline bool fold_child_update(const std::string& frame_json,
                              std::string* session, std::uint64_t* seq,
                              std::uint64_t* cause, bool* causeKnown,
                              bool* pending, json* entry) {
    const json root = json::parse(frame_json, nullptr, false);
    if (root.is_discarded()) return false;
    const json* wrapped = detail::object_field(root, "event");
    const json& event = wrapped != nullptr ? *wrapped : root;
    const std::string type = detail::str_field(event, "type");
    if (type != "child_elicitation_update" &&
        type != "child_elicitation_notice")
        return false;
    const std::string who = detail::str_field(event, "session");
    if (who.empty()) return false;
    if (session != nullptr) *session = who;
    if (seq != nullptr)
        *seq = static_cast<std::uint64_t>(
            detail::int_field(event, "elicitation", 0));
    if (cause != nullptr)
        *cause = event.is_object() && event.contains("cause") &&
                         event.at("cause").is_number_integer()
                     ? static_cast<std::uint64_t>(
                           detail::int_field(event, "cause", 0))
                     : 0;
    const bool hasCause = event.is_object() && event.contains("cause") &&
                          event.at("cause").is_number_integer();
    if (causeKnown != nullptr) *causeKnown = hasCause;
    const json* held = detail::object_field(event, "pending");
    if (pending != nullptr) *pending = held != nullptr;
    if (entry != nullptr && held != nullptr) {
        json out = *held;
        out["elicitation"] = detail::int_field(event, "elicitation", 0);
        *entry = std::move(out);
    }
    return true;
}

inline bool fold_child_ask_retracted(const std::string& frame_json,
                                     const std::string& child_session,
                                     std::uint64_t seq) {
    const json root = json::parse(frame_json, nullptr, false);
    if (root.is_discarded()) return false;
    const json* wrapped = detail::object_field(root, "event");
    const json& event = wrapped != nullptr ? *wrapped : root;
    const std::string type = detail::str_field(event, "type");
    if (type != "child_elicitation_update" &&
        type != "child_elicitation_notice")
        return false;
    if (detail::str_field(event, "session") != child_session) return false;
    if (static_cast<std::uint64_t>(detail::int_field(event, "elicitation",
                                                     0)) != seq)
        return false;
    return detail::object_field(event, "pending") == nullptr;
}

inline std::string answered_summary(const std::string& content_text) {
    const json content = json::parse(content_text, nullptr, false);
    if (content.is_discarded() || !content.is_object() || content.empty())
        return "";
    std::vector<std::string> parts;
    for (auto it = content.begin(); it != content.end(); ++it) {
        if (it->is_string()) {
            parts.push_back(it.key() + ": " + it->get<std::string>());
            continue;
        }
        if (it->is_array()) {
            std::string joined;
            for (const json& v : *it) {
                if (!v.is_string()) continue;
                if (!joined.empty()) joined += ", ";
                joined += v.get<std::string>();
            }
            if (!joined.empty()) parts.push_back(it.key() + ": " + joined);
        }
    }
    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty()) out += "  ·  ";
        out += p;
    }
    return out;
}

}  // namespace api::elicitation
