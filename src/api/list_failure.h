#pragma once

// What the app says when the thread list cannot be read (Knots kt-qu8m, web
// parity: "signed out, in words -- the Inbox says you are signed out and
// offers Sign in, instead of printing 'CAT mint rejected: HTTP 401'"). Pure.
//
// Two kinds, because they have different remedies. A CREDENTIAL failure --
// the mint refused this Mac's identity, the orchestrator refused the
// credential, or nothing is configured -- is "not signed in", said in words
// with the one move that fixes it and no status code. Anything else (a
// timeout, an unreachable host) keeps the transport's own words, which name
// what went wrong.

#include <algorithm>
#include <cctype>
#include <string>

namespace api::list_failure {

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Whether an error is the credential's (not signed in) rather than transport.
inline bool is_signed_out(const std::string& error) {
    const std::string e = lower(error);
    for (const char* k : {"401", "403", "credential", "not configured", "unauthorized", "unauthenticated",
                          "oidc", "sign in", "signed out", "forbidden", "mint returned http 4"})
        if (e.find(k) != std::string::npos) return true;
    return false;
}

struct View {
    bool signedOut = false;
    std::string headline;  // one line, the state
    std::string detail;    // the move that fixes it, or the transport's words
};

inline View view_for(const std::string& error, const std::string& appName) {
    View v;
    v.signedOut = is_signed_out(error);
    if (v.signedOut) {
        v.headline = "This Mac isn\xe2\x80\x99t signed in to Agentcloud";
        v.detail = appName + " signs in with this Mac\xe2\x80\x99s own identity. Connect to the company network or "
                             "VPN, then try again.";
    } else {
        v.headline = "Couldn\xe2\x80\x99t load your conversations";
        v.detail = error.empty() ? std::string("The thread list did not answer.") : error;
    }
    return v;
}

// The notice behind a list that stays on screen: in words for a credential
// failure (no status code), the transport's own words otherwise.
inline std::string stale_notice(const std::string& error) {
    if (is_signed_out(error))
        return "Signed out of Agentcloud on this Mac \xe2\x80\x94 showing the saved list.";
    return "Couldn't refresh the thread list (" + error + "). Showing the saved list.";
}

}  // namespace api::list_failure
