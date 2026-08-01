#include "token_store.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../../vendor/nlohmann/json.hpp"

namespace api {

using json = nlohmann::json;

std::string token_file_path() {
    if (const char* explicit_path = std::getenv("HANABI_TOKEN_FILE");
        explicit_path && *explicit_path) {
        return explicit_path;
    }
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return (fs::path(xdg) / "hanabi" / "token.json").string();
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return (fs::path(home) / ".config" / "hanabi" / "token.json").string();
    }
    return "";
}

StoredToken load_token() {
    StoredToken t;
    const std::string path = token_file_path();
    if (path.empty()) return t;
    std::ifstream in(path);
    if (!in.good()) return t;
    json j;
    try {
        in >> j;
    } catch (...) {
        return t;  // malformed: behave as if no token stored
    }
    if (!j.is_object()) return t;
    if (j.contains("access_token") && j.at("access_token").is_string())
        t.access_token = j.at("access_token").get<std::string>();
    if (j.contains("refresh_token") && j.at("refresh_token").is_string())
        t.refresh_token = j.at("refresh_token").get<std::string>();
    return t;
}

bool save_token(const StoredToken& tok) {
    const std::string path = token_file_path();
    if (path.empty()) return false;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    json j = json::object();
    j["access_token"] = tok.access_token;
    if (!tok.refresh_token.empty()) j["refresh_token"] = tok.refresh_token;

    // Write, then tighten permissions to 0600 (owner read/write only). Create
    // with the umask, then chmod down so the token is never group/world
    // readable. We do NOT log the token value anywhere.
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out.good()) return false;
        out << j.dump(2) << "\n";
        if (!out.good()) return false;
    }
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

}  // namespace api
