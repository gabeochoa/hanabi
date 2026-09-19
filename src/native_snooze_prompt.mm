#include "native_snooze_prompt.h"

#import <AppKit/AppKit.h>
#include <ctime>
#include <string>

#include "ui/snooze_parse.h"

namespace hanabi::native_snooze_prompt {
namespace {

Lifecycle g_life;

NSWindow* the_window() {
    NSWindow* w = [NSApp mainWindow];
    if (w == nil) w = [NSApp keyWindow];
    if (w == nil)
        for (NSWindow* c in [NSApp windows])
            if ([c isVisible]) return c;
    return w;
}

NSString* ns(const char* utf8) { return [NSString stringWithUTF8String:utf8]; }

void show_refusal(std::string_view hint) {
    NSAlert* refused = [[NSAlert alloc] init];
    refused.messageText = ns(kRefusalTitle);
    refused.informativeText = [[NSString alloc] initWithBytes:hint.data()
                                                       length:hint.size()
                                                     encoding:NSUTF8StringEncoding];
    refused.alertStyle = NSAlertStyleWarning;
    [refused addButtonWithTitle:ns(kRefusalButton)];
    [NSApp activateIgnoringOtherApps:YES];
    [refused runModal];
}

Result run_modal(const Request& req) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = ns(kTitle);
    alert.informativeText = ns(kBody);
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:ns(kSubmitButton)];
    [alert addButtonWithTitle:ns(kCancelButton)];

    NSTextField* field =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, kFieldWidth, kFieldHeight)];
    field.placeholderString = ns(kPlaceholder);
    field.stringValue = @"";
    alert.accessoryView = field;
    alert.window.initialFirstResponder = field;

    [NSApp activateIgnoringOtherApps:YES];
    const NSModalResponse response = [alert runModal];

    Result r;
    r.generation = req.generation;
    r.submitted_at_unix_sec = static_cast<std::int64_t>(std::time(nullptr));
    if (response != NSAlertFirstButtonReturn) return r;
    const char* typed = [field.stringValue UTF8String];
    const hanabi::snooze_parse::PromptOutcome outcome =
        hanabi::snooze_parse::outcome_of_submitted_phrase(typed != nullptr ? typed : "",
                                                          r.submitted_at_unix_sec);
    switch (outcome.kind) {
        case hanabi::snooze_parse::PromptOutcome::Kind::Set:
            r.kind = Result::Kind::Set;
            r.until_unix_sec = outcome.until;
            break;
        case hanabi::snooze_parse::PromptOutcome::Kind::Quiet:
            r.kind = Result::Kind::Quiet;
            break;
        case hanabi::snooze_parse::PromptOutcome::Kind::Refuse:
            r.kind = Result::Kind::Refused;
            show_refusal(outcome.hint);
            break;
    }
    return r;
}

}

bool available() {
    @autoreleasepool {
        NSWindow* win = the_window();
        return win != nil && [win isVisible];
    }
}

std::uint64_t request(Request req) { return g_life.request(std::move(req)); }

bool busy() { return g_life.busy(); }

std::uint64_t current_generation() { return g_life.current_generation(); }

void pump_after_frame() {
    std::optional<Request> take = g_life.take_for_dispatch();
    if (!take.has_value()) return;
    const Request copy = std::move(*take);
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            show_or_refuse(g_life, copy, available, run_modal);
        }
    });
}

bool take_result(std::uint64_t generation, Result* out) {
    return g_life.take_result(generation, out);
}

void cancel(std::uint64_t generation) { g_life.cancel(generation); }

void inject_result_for_test(Result r) { g_life.inject_result_for_test(r); }

void set_dispatch_hold_for_test(bool hold) { g_life.set_dispatch_hold_for_test(hold); }

bool dispatch_held_for_test() { return g_life.dispatch_held_for_test(); }

}
