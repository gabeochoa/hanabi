#include "http_client.h"

#include <cstdlib>

#include "../../vendor/nlohmann/json.hpp"

// cpp-httplib is header-only. We keep the implementation macro local to this
// translation unit so the rest of the app never pulls in networking headers.
// TLS support is opt-in at build time (define HANABI_ENABLE_TLS and link
// OpenSSL) so the default build has zero extra dependencies.
#ifdef HANABI_ENABLE_TLS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace api {

using json = nlohmann::json;

namespace {

// Split "https://host:port/prefix" into a scheme+host[:port] origin (for the
// httplib::Client) and a leading path prefix that is prepended to requests.
struct SplitUrl {
    std::string origin;  // scheme://host[:port]
    std::string prefix;  // path prefix, may be empty
};

SplitUrl split_url(const std::string& url) {
    SplitUrl s;
    auto scheme_end = url.find("://");
    size_t host_start = (scheme_end == std::string::npos) ? 0 : scheme_end + 3;
    auto path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        s.origin = url;
    } else {
        s.origin = url.substr(0, path_start);
        s.prefix = url.substr(path_start);
        if (s.prefix == "/") s.prefix.clear();
    }
    return s;
}

std::string replace_id(std::string tmpl, const std::string& id) {
    auto pos = tmpl.find("{id}");
    if (pos != std::string::npos) tmpl.replace(pos, 4, id);
    return tmpl;
}

Role parse_role(const std::string& r) {
    if (r == "user") return Role::User;
    if (r == "assistant") return Role::Assistant;
    if (r == "system") return Role::System;
    if (r == "tool") return Role::Tool;
    return Role::Assistant;
}

// Read a string field that might be a string or a number (epoch).
int64_t as_epoch(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return 0;
    const auto& v = obj.at(key);
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_float()) return static_cast<int64_t>(v.get<double>());
    return 0;
}

std::string as_string(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return "";
    const auto& v = obj.at(key);
    if (v.is_string()) return v.get<std::string>();
    return "";
}

}  // namespace

Result<std::string> HttpClient::get(const std::string& path) {
    if (!cfg_.http_ready())
        return Result<std::string>::failure(
            "http backend not configured (set HANABI_API_BASE_URL)");

    SplitUrl s = split_url(cfg_.base_url);

    // Guard the https-without-TLS case explicitly: without a TLS build,
    // httplib's client throws std::invalid_argument on an https origin. Catch
    // it (and any other transport error) and return a clean failure Result so
    // the app degrades to an error / mock fallback instead of aborting.
#ifndef HANABI_ENABLE_TLS
    if (s.origin.rfind("https://", 0) == 0)
        return Result<std::string>::failure(
            "https backend requires a TLS build (rebuild with HANABI_TLS=1)");
#endif

    try {
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "application/json");

        auto res = cli.Get((s.prefix + path).c_str(), headers);
        if (!res)
            return Result<std::string>::failure("request failed (no response)");
        if (res->status < 200 || res->status >= 300)
            return Result<std::string>::failure(
                "http status " + std::to_string(res->status));
        return Result<std::string>::success(res->body);
    } catch (const std::exception& ex) {
        return Result<std::string>::failure(std::string("request failed: ") +
                                            ex.what());
    }
}

Result<std::vector<SessionSummary>> HttpClient::list_sessions() {
    auto raw = get(cfg_.sessions_path);
    if (!raw.ok)
        return Result<std::vector<SessionSummary>>::failure(raw.error);

    std::vector<SessionSummary> out;
    try {
        json j = json::parse(raw.value);
        // Accept either a bare array or an object wrapping an array under a
        // common key ("sessions" / "data" / "items").
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object()) {
            for (const char* k : {"sessions", "data", "items"}) {
                if (j.contains(k) && j.at(k).is_array()) { arr = &j.at(k); break; }
            }
        }
        if (!arr)
            return Result<std::vector<SessionSummary>>::failure(
                "unexpected response shape for session list");

        for (const auto& e : *arr) {
            SessionSummary s;
            s.id = as_string(e, cfg_.field_id);
            s.title = as_string(e, cfg_.field_title);
            s.updated_at = as_epoch(e, cfg_.field_updated_at);
            s.status = as_string(e, cfg_.field_status);
            s.preview = as_string(e, cfg_.field_preview);
            out.push_back(std::move(s));
        }
    } catch (const std::exception& ex) {
        return Result<std::vector<SessionSummary>>::failure(
            std::string("json parse error: ") + ex.what());
    }
    return Result<std::vector<SessionSummary>>::success(std::move(out));
}

Result<Session> HttpClient::get_session(const std::string& id) {
    auto raw = get(replace_id(cfg_.messages_path, id));
    if (!raw.ok) return Result<Session>::failure(raw.error);

    Session session;
    session.summary.id = id;
    try {
        json j = json::parse(raw.value);
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object() && j.contains(cfg_.field_messages) &&
                   j.at(cfg_.field_messages).is_array()) {
            arr = &j.at(cfg_.field_messages);
        }
        if (!arr)
            return Result<Session>::failure(
                "unexpected response shape for transcript");

        for (const auto& e : *arr) {
            Message m;
            m.id = as_string(e, cfg_.field_id);
            m.role = parse_role(as_string(e, cfg_.field_role));
            m.created_at = as_epoch(e, cfg_.field_created_at);
            // Prefer a flat text field; if the message instead carries a blocks
            // array, concatenate the content of its text-type blocks. Also note
            // any non-text block (e.g. a tool call) as a subtitle hint.
            m.text = as_string(e, cfg_.field_text);
            if (m.text.empty() && e.contains(cfg_.field_blocks) &&
                e.at(cfg_.field_blocks).is_array()) {
                std::string joined;
                for (const auto& b : e.at(cfg_.field_blocks)) {
                    if (!b.is_object()) continue;
                    const std::string btype = as_string(b, cfg_.field_block_type);
                    if (btype == cfg_.field_block_text_type) {
                        const std::string c =
                            as_string(b, cfg_.field_block_content);
                        if (!c.empty()) {
                            if (!joined.empty()) joined += "\n\n";
                            joined += c;
                        }
                    } else if (!btype.empty() && m.subtitle.empty()) {
                        m.subtitle = btype;
                    }
                }
                m.text = std::move(joined);
            }
            session.messages.push_back(std::move(m));
        }
    } catch (const std::exception& ex) {
        return Result<Session>::failure(
            std::string("json parse error: ") + ex.what());
    }
    return Result<Session>::success(std::move(session));
}

}  // namespace api
