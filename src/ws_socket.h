// ws_socket.h
// A WebSocket client, as a C seam over NSURLSessionWebSocketTask.
//
// hanabi's HTTP goes through cpp-httplib, which does not speak WebSocket, and
// the agentcloud orchestrator speaks nothing else. Rather than take a new
// dependency, this wraps the one the OS already ships — the same class the
// reference client uses against this server, so the upgrade handshake, the
// 64MB read bound and the server's 5s pings are all somebody else's problem.
//
// Follows the house pattern (menubar.h, native_extras.h): every Foundation
// type stays behind extern "C" and no Obj-C type crosses into the C++ core.
//
// TRANSPORT NOTE. The connection is plaintext ws:// to a LOCAL forward proxy,
// which terminates and re-originates TLS carrying the device identity. The app
// itself performs no TLS. Point it somewhere without such a proxy and it will
// happily talk cleartext to the network, so the proxy is not optional — see
// agentcloud_auth.h, which mints the credential through the same one.
//
// THREADING. on_text fires on URLSession's delegate queue, never the UI
// thread. on_close fires there too, EXCEPT when ws_close is what ends the
// connection: then it runs on whatever thread called ws_close. Marshal to app
// state the way the SSE sink does; do not touch the widget tree from either.
//
// CALLBACK LIFETIME. Neither callback runs after ws_close returns: ws_close
// waits for every in-flight receive and send to finish before it returns. That
// is what makes the usual `user` -- the address of a stack object owned by the
// calling frame -- safe, because the frame may unwind the instant ws_close
// returns. Do not call ws_close FROM either callback; it would wait on itself.
//
// LIVENESS. A receive is always pending internally. The reference client
// measured that without one the socket wedges on an idle connection, and that
// task state alone is not proof of liveness.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_conn ws_conn;

// One inbound text message. `text` is valid only for the call.
typedef void (*ws_on_text_fn)(void* user, const char* text, size_t len);

// Fires exactly once, for any terminal outcome: a failed upgrade, a server
// close, or ws_close(). `reason` is never null but may be empty.
typedef void (*ws_on_close_fn)(void* user, const char* reason);

typedef struct {
    const char* url;         // ws://host/path?query
    const char* proxy_host;  // forward proxy; required (see TRANSPORT NOTE)
    int proxy_port;
    // Request headers, e.g. the minted credential. Parallel arrays, count
    // entries each; copied before ws_open returns.
    const char* const* header_keys;
    const char* const* header_values;
    size_t header_count;
    ws_on_text_fn on_text;
    ws_on_close_fn on_close;
    void* user;
} ws_config;

// Begins connecting and returns immediately; the socket is not open yet. A
// failed upgrade arrives as on_close. Returns null only if the URL will not
// parse. Free with ws_close.
ws_conn* ws_open(const ws_config* cfg);

// Queues one text frame. False if the connection is already closed. Safe from
// any thread.
bool ws_send_text(ws_conn* c, const char* text, size_t len);

// Closes and frees. on_close fires before this returns if it has not already,
// and no callback runs after it returns (see CALLBACK LIFETIME). Blocks until
// the in-flight callbacks are done. Safe to call twice; safe on null.
void ws_close(ws_conn* c);

#ifdef __cplusplus
}  // extern "C"
#endif
