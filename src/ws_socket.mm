// ws_socket.mm — NSURLSessionWebSocketTask behind the C seam in ws_socket.h.

#import <Foundation/Foundation.h>

#include <atomic>
#include <cstring>
#include <string>

#include "ws_socket.h"

// The server's own /ws/chat read bound. Anything larger is a protocol error on
// their side, so matching it here turns a silent truncation into a close.
static const NSInteger kMaxMessageBytes = 64 * 1024 * 1024;

struct ws_conn {
    NSURLSession* session = nil;
    NSURLSessionWebSocketTask* task = nil;
    dispatch_queue_t queue = nil;
    ws_on_text_fn on_text = nullptr;
    ws_on_close_fn on_close = nullptr;
    void* user = nullptr;
    // on_close fires exactly once. Both the receive loop and ws_close can
    // reach it, from different threads.
    std::atomic<bool> closed{false};
};

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
static void ws_pump(ws_conn* c) {
    if (c == nullptr || c->closed.load()) return;
    NSURLSessionWebSocketTask* task = c->task;
    if (task == nil) return;

    [task receiveMessageWithCompletionHandler:^(
              NSURLSessionWebSocketMessage* message, NSError* error) {
        if (error != nil) {
            ws_finish(c, std::string("receive failed: ") +
                             [[error localizedDescription] UTF8String]);
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
    }];
}

ws_conn* ws_open(const ws_config* cfg) {
    if (cfg == nullptr || cfg->url == nullptr) return nullptr;

    NSString* urlStr = [NSString stringWithUTF8String:cfg->url];
    NSURL* url = (urlStr != nil) ? [NSURL URLWithString:urlStr] : nil;
    if (url == nil) return nullptr;

    ws_conn* c = new ws_conn();
    c->on_text = cfg->on_text;
    c->on_close = cfg->on_close;
    c->user = cfg->user;
    c->queue = dispatch_queue_create("hanabi.ws", DISPATCH_QUEUE_SERIAL);

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
    [c->task resume];
    ws_pump(c);
    return c;
}

bool ws_send_text(ws_conn* c, const char* text, size_t len) {
    if (c == nullptr || text == nullptr || c->closed.load()) return false;
    NSString* s = [[NSString alloc] initWithBytes:text
                                           length:len
                                         encoding:NSUTF8StringEncoding];
    if (s == nil) return false;
    NSURLSessionWebSocketMessage* msg =
        [[NSURLSessionWebSocketMessage alloc] initWithString:s];
    [c->task sendMessage:msg
        completionHandler:^(NSError* error) {
          if (error != nil)
              ws_finish(c, std::string("send failed: ") +
                               [[error localizedDescription] UTF8String]);
        }];
    return true;
}

void ws_close(ws_conn* c) {
    if (c == nullptr) return;
    ws_finish(c, "closed by client");
    if (c->task != nil) {
        [c->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure
                              reason:nil];
        c->task = nil;
    }
    if (c->session != nil) {
        [c->session invalidateAndCancel];
        c->session = nil;
    }
    c->queue = nil;
    delete c;
}
