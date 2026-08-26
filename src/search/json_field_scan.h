#pragma once

// ---------------------------------------------------------------------------
// Reading VALUES out of a JSON document without parsing it.
//
// The sidebar's deep filter asks one question per cached transcript — "is this
// word in this conversation" — for every thread, on the first frame of a
// query. It used to answer it by lowercasing the whole cache file and calling
// find(), which is fast and wrong: the file is
// {"version":1,"summary":{…},"messages":[…],"sub_agents":[…]}, so `state`,
// `tag`, `preview`, `folder`, `subtitle`, `messages` and every session id are
// in the corpus as STRUCTURE. Typing any of them matched every thread that had
// ever been cached, and because the title match runs first it only ever fired
// once the title had missed — which is exactly when it looks like a real deep
// hit (docs/SEARCH.md S3).
//
// Parsing the document properly would fix it and cost a full nlohmann parse
// per file per query. This is the middle: one pass over the bytes that knows
// only enough JSON to tell a key from a value, so it can look inside the
// values of ONE named field and nowhere else. Same order of cost as the
// lowercase-and-find it replaces, minus the field names.
//
// It is deliberately not a JSON parser. It does not validate, it does not
// build a tree, and it does not care about nesting: it answers "does any
// string value stored under this key contain this needle". For the transcript
// cache that is exactly the conversation, because `text` is the only field
// message bodies are written to (api::disk_cache::to_json(const Message&)).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <string_view>

namespace hanabi::search {

namespace detail {

inline bool json_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Decode the JSON string literal at doc[i] (which must be '"') into `out`,
// ASCII-lowercased as it goes. Returns the index just past the closing quote,
// or npos if the literal never closes.
inline std::size_t decode_lowered(std::string_view doc, std::size_t i,
                                  std::string& out) {
    out.clear();
    ++i;
    while (i < doc.size()) {
        const char c = doc[i];
        if (c == '"') return i + 1;
        if (c != '\\') {
            out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
            ++i;
            continue;
        }
        if (i + 1 >= doc.size()) break;
        const char e = doc[i + 1];
        i += 2;
        switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                // nlohmann only emits \u for control characters — everything
                // printable, including non-ASCII, is written through as UTF-8.
                // A control character cannot be part of an ASCII-folded query,
                // but dropping it would weld the words on either side into one
                // token that is in no message, so it becomes a space.
                out.push_back(' ');
                const std::size_t skip = doc.size() - i < 4 ? doc.size() - i : 4;
                i += skip;
                break;
            }
            default: out.push_back(e); break;  // \" \\ \/ and anything odd
        }
    }
    return std::string_view::npos;
}

}  // namespace detail

// Does any string value stored under `key` contain `lowerNeedle`? `key` and
// `lowerNeedle` must both already be ASCII-lowercased; the document's own
// bytes are folded as they are read.
//
// A string token is a KEY when the next non-space character after it is ':'.
// Everything else is a value, and only the value immediately following a
// matching key is searched — so a message whose BODY contains the word "text"
// contributes its body and never its field names.
inline bool json_field_contains(std::string_view doc, std::string_view key,
                                std::string_view lowerNeedle) {
    if (lowerNeedle.empty() || key.empty()) return false;
    std::string tok, val;
    std::size_t i = 0;
    while (i < doc.size()) {
        if (doc[i] != '"') {
            ++i;
            continue;
        }
        const std::size_t end = detail::decode_lowered(doc, i, tok);
        if (end == std::string_view::npos) return false;
        i = end;
        std::size_t j = end;
        while (j < doc.size() && detail::json_space(doc[j])) ++j;
        if (j >= doc.size() || doc[j] != ':') continue;  // a value, not a key
        if (tok != key) continue;
        std::size_t k = j + 1;
        while (k < doc.size() && detail::json_space(doc[k])) ++k;
        if (k >= doc.size() || doc[k] != '"') continue;  // not a string value
        const std::size_t vend = detail::decode_lowered(doc, k, val);
        if (vend == std::string_view::npos) return false;
        if (val.find(lowerNeedle) != std::string::npos) return true;
        i = vend;
    }
    return false;
}

}  // namespace hanabi::search
