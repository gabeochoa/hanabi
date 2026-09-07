#pragma once

// ---------------------------------------------------------------------------
// ONE notice row, with a fixed precedence and a dismiss.
//
// WHAT WAS THERE. A complaint about an attachment appeared in TWO places at
// once -- a note row under the chips and a toast -- and could only be cleared
// by fixing the cause. A failed send had nowhere to go at all: the outbox
// retried silently, and `attempts()` was exposed on it for a reader that did
// not exist. Meanwhile the strip's caption slot carried a different, unrelated
// ladder. Two channels, neither dismissible, and one state with no channel.
//
// WHAT THIS IS. The reference client's rule, ported: one row, a fixed slot order, and
// dismissing clears exactly the slot that was showing, so the next one takes
// the row rather than everything vanishing at once. The order is by how much
// the reader can do about it:
//
//   send       -- a message or a create that did not land. The most
//                 consequential thing the composer can say.
//   outbox     -- something held and retrying. Less urgent than a hard
//                 failure, and it must not hide one.
//   command    -- an answer to a slash command the reader just typed.
//   attachment -- a refused or capped file.
//
// The reference's `node` and `approval` slots have no counterpart here (hanabi has no
// node roster and its approvals live on the ask card), so they are absent
// rather than declared empty -- a slot nothing can ever fill is a slot that
// misleads the next reader about what the row can say.
//
// This is NOT the live-status ladder (sending, uploading, queued, the send-key
// hint). Those are conditions, not notices: they clear themselves when the
// condition ends and there is nothing to dismiss. They stay in the strip and
// the notice outranks them, because a notice is something that already went
// wrong.
//
// Pure: no ECS, no UI. Every arm is reachable from a unit test.
// ---------------------------------------------------------------------------

#include <string>

namespace ecs::model {

enum class NoticeSlot {
    None,
    Send,
    Outbox,
    Command,
    Attachment,
};

inline const char* name_of(NoticeSlot slot) {
    switch (slot) {
        case NoticeSlot::None: return "none";
        case NoticeSlot::Send: return "send";
        case NoticeSlot::Outbox: return "outbox";
        case NoticeSlot::Command: return "command";
        case NoticeSlot::Attachment: return "attachment";
    }
    return "none";
}

struct ComposerNotices {
    std::string send;
    std::string outbox;
    std::string command;
    std::string attachment;

    [[nodiscard]] NoticeSlot visible_slot() const {
        if (!send.empty()) return NoticeSlot::Send;
        if (!outbox.empty()) return NoticeSlot::Outbox;
        if (!command.empty()) return NoticeSlot::Command;
        if (!attachment.empty()) return NoticeSlot::Attachment;
        return NoticeSlot::None;
    }

    [[nodiscard]] const std::string& visible() const {
        static const std::string kEmpty;
        switch (visible_slot()) {
            case NoticeSlot::Send: return send;
            case NoticeSlot::Outbox: return outbox;
            case NoticeSlot::Command: return command;
            case NoticeSlot::Attachment: return attachment;
            case NoticeSlot::None: break;
        }
        return kEmpty;
    }

    [[nodiscard]] bool any() const {
        return visible_slot() != NoticeSlot::None;
    }

    // Clears EXACTLY the slot on screen. Dismissing a send failure must not
    // also throw away the attachment complaint underneath it -- the reader
    // dismissed one sentence, not the state of the composer.
    NoticeSlot dismiss() {
        const NoticeSlot slot = visible_slot();
        switch (slot) {
            case NoticeSlot::Send: send.clear(); break;
            case NoticeSlot::Outbox: outbox.clear(); break;
            case NoticeSlot::Command: command.clear(); break;
            case NoticeSlot::Attachment: attachment.clear(); break;
            case NoticeSlot::None: break;
        }
        return slot;
    }

    void clear() {
        send.clear();
        outbox.clear();
        command.clear();
        attachment.clear();
    }
};

// What the outbox says while it holds a message. Kept beside the slot order so
// the sentence and the rule that raises it cannot drift apart, and phrased so
// it never claims delivery: the outbox retries, it does not confirm.
inline std::string outbox_notice(std::size_t held, int attempts) {
    if (held == 0) return {};
    std::string out = held == 1
                          ? std::string("1 message has not reached the server")
                          : std::to_string(held) +
                                " messages have not reached the server";
    if (attempts > 0)
        out += attempts == 1 ? " \xc2\xb7 1 try so far"
                             : " \xc2\xb7 " + std::to_string(attempts) +
                                   " tries so far";
    return out + " \xc2\xb7 still retrying";
}

}  // namespace ecs::model
