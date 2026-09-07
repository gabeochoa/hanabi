#pragma once

// ---------------------------------------------------------------------------
// What a create actually did, said once.
//
// A create carries two legs -- mint the session, then land the first message --
// and each leg can end three ways: it worked, it definitely did not, or we
// never heard. Six outcomes, and before this they were read ad hoc at the one
// call site in loader_system.h, where the two that matter most were indistinct:
// a transport that never answered was treated exactly like a server that said
// no.
//
// The two are not the same and the difference is the one that bites. A proven
// refusal can be handed straight back with a Retry, because pressing it cannot
// produce a second conversation. An unheard answer cannot: the session may
// already exist, so an automatic retry -- or a Retry button, which is the same
// thing one press later -- is how a reader ends up with two.
//
// So the rule this file states, and tests pin:
//
//   * NOTHING IS EVER SILENTLY DROPPED. Every failing arm restores the exact
//     text and the staged files somewhere the reader can see them.
//   * A RETRY IS OFFERED ONLY WHEN A REPEAT IS PROVABLY SAFE. An unknown fate
//     gets the draft back and a sentence naming the doubt, and no button.
//
// Pure: no ECS, no UI, no clock. classify_create() is a total function over the
// transport's own answer, so every arm is reachable from a unit test.
// ---------------------------------------------------------------------------

#include <string>

#include "client.h"
#include "types.h"

namespace api {

enum class CreateDisposition {
    // The session exists and it has the first message.
    Created,
    // The session exists; the server refused the first message, and said so.
    CreatedInputRejected,
    // The session exists; nobody knows whether the first message landed.
    CreatedInputUnknown,
    // No session was created, and that is established.
    NotCreatedRejected,
    // Nobody knows whether a session was created.
    NotCreatedUnknown,
};

inline const char* name_of(CreateDisposition d) {
    switch (d) {
        case CreateDisposition::Created: return "created";
        case CreateDisposition::CreatedInputRejected:
            return "created-input-rejected";
        case CreateDisposition::CreatedInputUnknown:
            return "created-input-unknown";
        case CreateDisposition::NotCreatedRejected:
            return "not-created-rejected";
        case CreateDisposition::NotCreatedUnknown:
            return "not-created-unknown";
    }
    return "not-created-unknown";
}

struct CreateVerdict {
    CreateDisposition disposition = CreateDisposition::NotCreatedUnknown;
    // Non-empty only when a session provably exists.
    std::string session_id;
    std::string notice;
    // Put the exact text and the ordered files back in front of the reader.
    bool restore_draft = false;
    // Safe to repeat with one press.
    bool offer_retry = false;
    // Hold it durably, manual-only, against the session that does exist.
    bool park_in_outbox = false;

    [[nodiscard]] bool ok() const {
        return disposition == CreateDisposition::Created;
    }
};

inline bool definite_failure(SendFailureKind kind) {
    return kind != SendFailureKind::Unknown;
}

inline CreateVerdict classify_create(const Result<CreateOutcome>& result) {
    CreateVerdict verdict;
    if (!result.ok) {
        // The transport gives no kind for a create that threw, and a thrown
        // create can still have reached the server. Unknown is the only honest
        // reading, so the draft comes back and the button does not.
        verdict.disposition = CreateDisposition::NotCreatedUnknown;
        verdict.notice =
            result.error +
            " \xe2\x80\x94 we never heard whether the conversation was "
            "created. Your draft is back; check the list before starting it "
            "again.";
        verdict.restore_draft = true;
        return verdict;
    }

    const CreateOutcome& outcome = result.value;
    if (!outcome.created) {
        const bool definite = definite_failure(outcome.create_failure.kind);
        verdict.disposition = definite ? CreateDisposition::NotCreatedRejected
                                       : CreateDisposition::NotCreatedUnknown;
        verdict.restore_draft = true;
        verdict.offer_retry = definite;
        verdict.notice =
            outcome.create_failure.message +
            (definite ? " \xe2\x80\x94 nothing was created. Your draft and "
                        "files are back."
                      : " \xe2\x80\x94 we never heard whether the conversation "
                        "was created. Your draft is back; check the list "
                        "before starting it again.");
        return verdict;
    }

    verdict.session_id = outcome.session_id;
    if (outcome.input_accepted) {
        verdict.disposition = CreateDisposition::Created;
        return verdict;
    }

    if (definite_failure(outcome.input_failure.kind)) {
        verdict.disposition = CreateDisposition::CreatedInputRejected;
        verdict.restore_draft = true;
        verdict.offer_retry = true;
        verdict.notice = outcome.input_failure.message +
                         " \xe2\x80\x94 your message is back in the composer.";
        return verdict;
    }

    verdict.disposition = CreateDisposition::CreatedInputUnknown;
    verdict.park_in_outbox = true;
    verdict.notice =
        outcome.input_failure.message +
        " \xe2\x80\x94 the conversation exists but we never heard whether your "
        "first message landed. It is held and will not send itself.";
    return verdict;
}

}  // namespace api
