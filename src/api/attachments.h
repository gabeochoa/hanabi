#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

namespace api::attachments {

inline constexpr std::size_t kMaxCount = 5;
inline constexpr std::uint64_t kMaxFileBytes = 7ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxRequestBytes = 10ULL * 1024ULL * 1024ULL;

inline std::string lower_extension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

inline std::string media_type_for_path(const std::string& path) {
    const std::string ext = lower_extension(path);
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".txt") return "text/plain";
    if (ext == ".md" || ext == ".markdown") return "text/markdown";
    if (ext == ".pdf") return "application/pdf";
    return {};
}

inline Result<Attachment> stage(const std::string& input_path) {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::absolute(input_path, ec);
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec)
        return Result<Attachment>::failure("That file is no longer available.");
    const std::string media_type = media_type_for_path(path.string());
    if (media_type.empty())
        return Result<Attachment>::failure(
            "Choose a PNG, JPEG, GIF, WebP, PDF, Markdown, or plain-text file.");
    const std::uint64_t size = std::filesystem::file_size(path, ec);
    if (ec || size == 0)
        return Result<Attachment>::failure("That file is empty or unreadable.");
    if (size > kMaxFileBytes)
        return Result<Attachment>::failure("That file is over the 7 MB limit.");
    Attachment out;
    out.path = path.string();
    out.name = path.filename().string();
    out.media_type = media_type;
    out.size_bytes = size;
    return Result<Attachment>::success(std::move(out));
}

inline std::string make_local_id() {
    static std::atomic<std::uint64_t> serial{0};
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return "hanabi-" + std::to_string(ticks) + "-" +
           std::to_string(serial.fetch_add(1));
}

inline OutgoingMessage outgoing(std::string text,
                                std::vector<Attachment> attachments = {},
                                OutgoingTarget target = {}) {
    OutgoingMessage out;
    out.local_id = make_local_id();
    out.text = std::move(text);
    out.attachments = std::move(attachments);
    out.target = std::move(target);
    return out;
}

inline void mark_delivery_started(OutgoingMessage& message) {
    if (message.attachments.empty()) return;
    message.auto_retry = false;
    message.attachment_delivery_started = true;
}

inline Result<std::string> read_base64(const Attachment& attachment) {
    std::ifstream in(attachment.path, std::ios::binary);
    if (!in.good())
        return Result<std::string>::failure(attachment.name + " is no longer available.");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty())
        return Result<std::string>::failure(attachment.name + " is empty or unreadable.");
    if (bytes.size() > kMaxFileBytes)
        return Result<std::string>::failure(attachment.name + " is over the 7 MB limit.");
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const std::uint32_t word = (a << 16) | (b << 8) | c;
        out.push_back(table[(word >> 18) & 63]);
        out.push_back(table[(word >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? table[(word >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? table[word & 63] : '=');
    }
    return Result<std::string>::success(std::move(out));
}

inline std::uint64_t encoded_size(std::uint64_t bytes) {
    return ((bytes + 2) / 3) * 4;
}

inline Result<bool> validate_message(const OutgoingMessage& message) {
    if (message.attachments.size() > kMaxCount)
        return Result<bool>::failure("A message can carry at most five files.");
    std::uint64_t encoded = message.text.size() + 256;
    for (const Attachment& attachment : message.attachments) {
        if (attachment.media_type.empty() || attachment.name.empty())
            return Result<bool>::failure("An attachment is missing its file metadata.");
        if (attachment.size_bytes == 0)
            return Result<bool>::failure(attachment.name + " is empty or unreadable.");
        if (attachment.size_bytes > kMaxFileBytes)
            return Result<bool>::failure(attachment.name + " is over the 7 MB limit.");
        encoded += encoded_size(attachment.size_bytes) + attachment.name.size() +
                   attachment.media_type.size() + 96;
    }
    if (encoded > kMaxRequestBytes)
        return Result<bool>::failure(
            "Those files are too large to send together. Remove one and try again.");
    return Result<bool>::success(true);
}

}  // namespace api::attachments
