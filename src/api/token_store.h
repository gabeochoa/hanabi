#pragma once

// Phase AUTH — persist/load a bearer token acquired via the device-code flow.
//
// The token is written to ~/.config/hanabi/token.json with mode 0600 (owner
// read/write only), so a previously-authed user is silently logged in at
// startup without re-running the flow. The token is NEVER logged. The path is
// gitignored. Path resolution mirrors config.cpp:
//   1. $HANABI_TOKEN_FILE (explicit)
//   2. $XDG_CONFIG_HOME/hanabi/token.json
//   3. $HOME/.config/hanabi/token.json

#include <string>

namespace api {

struct StoredToken {
    std::string access_token;
    std::string refresh_token;  // optional
    bool valid() const { return !access_token.empty(); }
};

// Resolve the token file path (empty if HOME/XDG unavailable).
std::string token_file_path();

// Load a previously-persisted token. Returns an invalid StoredToken when the
// file is absent/unreadable/malformed (never an error — the app just prompts).
StoredToken load_token();

// Persist a token to disk with mode 0600. Creates the parent directory if
// needed. Returns true on success. Never logs the token value.
bool save_token(const StoredToken& tok);

}  // namespace api
