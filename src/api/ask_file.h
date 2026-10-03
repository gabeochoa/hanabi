#pragma once

// A file question's answer, staged from a picked path (the reference's
// ElicitationFileControl; Knots kt-zmwc). The bytes ride the resolve INLINE
// under a client-minted id: the server stages an inline answer for the agent
// exactly as it stages any other. That is the only way a file reaches the
// agent from here -- the durable upload door (the web app's bulk-upload
// mutation) is refused to CAT-only clients by the web app's Protected Mode,
// which is why the reference answers inline too -- so a file over the entry's
// inline ceiling says plainly that it cannot be sent from Hanabi yet.

#include <algorithm>
#include <string>

#include "attachments.h"
#include "types.h"

namespace api::ask_file {

inline Result<AskAnswer::File> stage(const PendingAsk& ask, const std::string& path) {
    if (!ask.file_answerable())
        return Result<AskAnswer::File>::failure("This question cannot take a file from Hanabi.");
    auto staged = attachments::stage(path);
    if (!staged.ok) return Result<AskAnswer::File>::failure(staged.error);
    const Attachment& a = staged.value;
    if (!ask.file_media_types.empty() &&
        std::find(ask.file_media_types.begin(), ask.file_media_types.end(), a.media_type) ==
            ask.file_media_types.end())
        return Result<AskAnswer::File>::failure("This question does not take a " + a.media_type +
                                                " file.");
    if (static_cast<std::int64_t>(attachments::encoded_size(a.size_bytes)) > ask.file_max_b64)
        return Result<AskAnswer::File>::failure(
            a.name + " is too large to send from Hanabi yet. Pick a smaller file, or answer "
                     "without it.");
    auto data = attachments::read_base64(a);
    if (!data.ok) return Result<AskAnswer::File>::failure(data.error);
    AskAnswer::File out;
    out.id = "hanabi-file-" + attachments::make_local_id();
    out.name = a.name;
    out.media_type = a.media_type;
    out.data_b64 = std::move(data.value);
    out.size_bytes = a.size_bytes;
    return Result<AskAnswer::File>::success(std::move(out));
}

}  // namespace api::ask_file
