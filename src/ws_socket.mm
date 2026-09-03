// ws_socket.mm — NSURLSessionWebSocketTask behind the C seam in ws_socket.h.

#import <Foundation/Foundation.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ws_socket.h"

// The server's own /ws/chat read bound. Anything larger is a protocol error on
// their side, so matching it here turns a silent truncation into a close.
static const NSInteger kMaxMessageBytes = 64 * 1024 * 1024;

static const int64_t kQuiesceTimeoutNs = 5LL * NSEC_PER_SEC;

struct ws_conn {
    NSURLSession* session = nil;
    NSURLSessionWebSocketTask* task = nil;
    dispatch_group_t inflight = nil;
    ws_on_text_fn on_text = nullptr;
    ws_on_close_fn on_close = nullptr;
    void* user = nullptr;
    // on_close fires exactly once. Both the receive loop and ws_close can
    // reach it, from different threads.
    std::atomic<bool> closed{false};
};

static std::mutex& ws_live_lock() {
    static std::mutex m;
    return m;
}

static std::unordered_map<ws_conn*, std::shared_ptr<ws_conn>>& ws_live() {
    static std::unordered_map<ws_conn*, std::shared_ptr<ws_conn>> m;
    return m;
}

static std::shared_ptr<ws_conn> ws_hold(ws_conn* c) {
    std::lock_guard<std::mutex> guard(ws_live_lock());
    const auto it = ws_live().find(c);
    return it == ws_live().end() ? nullptr : it->second;
}

// Fires on_close the first time anyone gets here and nowhere else.
static void ws_finish(ws_conn* c, const std::string& reason) {
    if (c == nullptr) return;
    bool expected = false;
    if (!c->closed.compare_exchange_strong(expected, true)) return;
    if (c->on_close) c->on_close(c->user, reason.c_str());
}

// Re-arms itself after every message. There must always be a receive pending:
// URLSession only answers the server's 5s pings while one is, and without it
// an idle socket wedges with the task still reporting .running.
static void ws_pump(std::shared_ptr<ws_conn> c) {
    if (!c || c->closed.load()) return;
    NSURLSessionWebSocketTask* task = c->task;
    if (task == nil) return;

    dispatch_group_enter(c->inflight);
    [task receiveMessageWithCompletionHandler:^(
              NSURLSessionWebSocketMessage* message, NSError* error) {
        if (error != nil) {
            ws_finish(c.get(), std::string("receive failed: ") +
                                   [[error localizedDescription] UTF8String]);
            dispatch_group_leave(c->inflight);
            return;
        }
        if (message != nil &&
            message.type == NSURLSessionWebSocketMessageTypeString) {
            NSString* s = message.string;
            if (s != nil && c->on_text != nullptr && !c->closed.load()) {
                const char* utf8 = [s UTF8String];
                if (utf8 != nullptr)
                    c->on_text(c->user, utf8, std::strlen(utf8));
            }
        }
        // Binary frames are ignored: this protocol is JSON text only, and
        // silently dropping one is better than guessing at an encoding.
        ws_pump(c);
        dispatch_group_leave(c->inflight);
    }];
}

ws_conn* ws_open(const ws_config* cfg) {
    if (cfg == nullptr || cfg->url == nullptr) return nullptr;

    NSString* urlStr = [NSString stringWithUTF8String:cfg->url];
    NSURL* url = (urlStr != nil) ? [NSURL URLWithString:urlStr] : nil;
    if (url == nil) return nullptr;

    std::shared_ptr<ws_conn> owned = std::make_shared<ws_conn>();
    ws_conn* c = owned.get();
    c->on_text = cfg->on_text;
    c->on_close = cfg->on_close;
    c->user = cfg->user;
    c->inflight = dispatch_group_create();

    NSURLSessionConfiguration* sc =
        [NSURLSessionConfiguration ephemeralSessionConfiguration];
    // Plaintext to the local proxy, which originates TLS with the device
    // identity. kCFStreamProperty* keys rather than the HTTPS pair on purpose:
    // CONNECT-tunnelling would make TLS end-to-end and lock the proxy out of
    // the very injection that authenticates us.
    if (cfg->proxy_host != nullptr && cfg->proxy_port > 0) {
        NSString* host = [NSString stringWithUTF8String:cfg->proxy_host];
        if (host != nil) {
            sc.connectionProxyDictionary = @{
                (NSString*)kCFNetworkProxiesHTTPEnable : @YES,
                (NSString*)kCFNetworkProxiesHTTPProxy : host,
                (NSString*)kCFNetworkProxiesHTTPPort : @(cfg->proxy_port),
            };
        }
    }

    c->session = [NSURLSession sessionWithConfiguration:sc];

    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
    for (size_t i = 0; i < cfg->header_count; ++i) {
        const char* k = cfg->header_keys ? cfg->header_keys[i] : nullptr;
        const char* v = cfg->header_values ? cfg->header_values[i] : nullptr;
        if (k == nullptr || v == nullptr) continue;
        NSString* ks = [NSString stringWithUTF8String:k];
        NSString* vs = [NSString stringWithUTF8String:v];
        if (ks != nil && vs != nil) [req setValue:vs forHTTPHeaderField:ks];
    }

    c->task = [c->session webSocketTaskWithRequest:req];
    c->task.maximumMessageSize = kMaxMessageBytes;
    {
        std::lock_guard<std::mutex> guard(ws_live_lock());
        ws_live().emplace(c, owned);
    }
    [c->task resume];
    ws_pump(owned);
    return c;
}

bool ws_send_text(ws_conn* c, const char* text, size_t len) {
    if (c == nullptr || text == nullptr) return false;
    std::shared_ptr<ws_conn> held = ws_hold(c);
    if (!held || held->closed.load()) return false;
    NSString* s = [[NSString alloc] initWithBytes:text
                                           length:len
                                         encoding:NSUTF8StringEncoding];
    if (s == nil) return false;
    NSURLSessionWebSocketMessage* msg =
        [[NSURLSessionWebSocketMessage alloc] initWithString:s];
    dispatch_group_enter(held->inflight);
    [held->task sendMessage:msg
          completionHandler:^(NSError* error) {
            if (error != nil)
                ws_finish(held.get(),
                          std::string("send failed: ") +
                              [[error localizedDescription] UTF8String]);
            dispatch_group_leave(held->inflight);
          }];
    return true;
}

void ws_close(ws_conn* c) {
    if (c == nullptr) return;
    std::shared_ptr<ws_conn> held = ws_hold(c);
    if (!held) return;
    ws_finish(held.get(), "closed by client");
    if (held->task != nil)
        [held->task
            cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure
                         reason:nil];
    if (held->session != nil) [held->session invalidateAndCancel];
    const dispatch_time_t limit =
        dispatch_time(DISPATCH_TIME_NOW, kQuiesceTimeoutNs);
    if (dispatch_group_wait(held->inflight, limit) != 0)
        NSLog(@"ws_close: callbacks still running after %.0fs",
              (double)kQuiesceTimeoutNs / 1e9);
    {
        std::lock_guard<std::mutex> guard(ws_live_lock());
        ws_live().erase(c);
    }
}
