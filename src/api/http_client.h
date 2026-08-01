#pragma once

// Generic REST adapter. This class is deliberately dumb: it knows how to do an
// authenticated GET and pull a configurable set of JSON field names out of the
// response. It has NO knowledge of any specific service — every URL, path,
// header, and field name comes from `Config` (populated from the environment
// at runtime). Nothing about a real endpoint is compiled into this repo.
//
// Networking uses the header-only cpp-httplib bundled under vendor/. If the
// backend is not fully configured, construction still succeeds but calls
// return a clean failure Result (the app then shows an error and the user can
// fall back to the mock backend).

#include <string>

#include "auth.h"
#include "client.h"

namespace api {

class HttpClient : public Client {
  public:
    explicit HttpClient(Config cfg) : cfg_(std::move(cfg)) {}

    std::string backend_label() const override { return "http"; }

    Result<std::vector<SessionSummary>> list_sessions() override;
    Result<Session> get_session(const std::string& id) override;

  private:
    // Perform an authenticated GET against base_url + path. Returns the raw
    // body on success. Implemented in http_client.cpp.
    Result<std::string> get(const std::string& path);

    Config cfg_;
};

// Build the real device-code transport hook for a Config. Reads the endpoint
// origin from cfg (base_url); NEVER hardcodes any endpoint. TLS-guarded like
// HttpClient::get. Tests inject a fake transport instead of this one.
AuthTransport make_http_auth_transport(const Config& cfg);

}  // namespace api
