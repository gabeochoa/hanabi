#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::artifact_models {

inline constexpr std::size_t kTableRowCap = 500;
inline constexpr std::size_t kTableColumnCap = 64;
inline constexpr std::size_t kNumericSampleRows = 100;
inline constexpr std::size_t kEntryCap = 2000;
inline constexpr std::size_t kInflateCap = 64u * 1024u * 1024u;

inline std::string byte_label(std::uint64_t n) {
    char buf[32];
    if (n < 1024) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(n));
    else if (n < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.0f KB", static_cast<double>(n) / 1024.0);
    else if (n < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(n) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(n) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

struct Table {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    bool truncated = false;
    char delimiter = ',';
    friend bool operator==(const Table&, const Table&) = default;
};

inline std::optional<Table> table(std::string_view text) {
    const std::string_view firstLine = text.substr(0, text.find('\n'));
    const std::size_t tabs = static_cast<std::size_t>(std::count(firstLine.begin(), firstLine.end(), '\t'));
    const std::size_t commas = static_cast<std::size_t>(std::count(firstLine.begin(), firstLine.end(), ','));
    const char delimiter = tabs > commas ? '\t' : ',';
    Table out;
    out.delimiter = delimiter;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool truncated = false;
    const auto endField = [&] {
        if (row.size() < kTableColumnCap) row.push_back(field);
        else truncated = true;
        field.clear();
    };
    const auto endRow = [&] {
        endField();
        if (rows.size() <= kTableRowCap) rows.push_back(row);
        else truncated = true;
        row.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }
        if (c == '"' && field.empty()) quoted = true;
        else if (c == delimiter) endField();
        else if (c == '\r') continue;
        else if (c == '\n') endRow();
        else field.push_back(c);
        if (truncated && rows.size() > kTableRowCap) break;
    }
    if (!field.empty() || !row.empty()) endRow();
    if (rows.empty()) return std::nullopt;
    const std::vector<std::string>& header = rows.front();
    if (std::all_of(header.begin(), header.end(), [](const std::string& h) { return h.empty(); })) return std::nullopt;
    out.header = header;
    out.rows.assign(rows.begin() + 1, rows.end());
    out.truncated = truncated;
    return out;
}

inline std::string_view trim_spaces(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

inline bool is_number(std::string_view cellIn) {
    std::string_view text = cellIn;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) text.remove_prefix(1);
    else if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xE2 && static_cast<unsigned char>(text[1]) == 0x88 &&
             static_cast<unsigned char>(text[2]) == 0x92)
        text.remove_prefix(3);  // U+2212 minus
    if (!text.empty() && text.front() == '$') text.remove_prefix(1);
    else if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xC2 &&
             (static_cast<unsigned char>(text[1]) == 0xA3 || static_cast<unsigned char>(text[1]) == 0xA5))
        text.remove_prefix(2);  // £ ¥
    else if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xE2 && static_cast<unsigned char>(text[1]) == 0x82 &&
             static_cast<unsigned char>(text[2]) == 0xAC)
        text.remove_prefix(3);  // €
    if (!text.empty() && text.back() == '%') text.remove_suffix(1);
    std::string digits;
    for (char c : text)
        if (c != ',') digits.push_back(c);
    if (digits.empty() || digits.front() == '.') return false;
    int dots = 0;
    for (char c : digits) {
        if (c == '.') ++dots;
        else if (c < '0' || c > '9') return false;
    }
    return dots <= 1;
}

inline std::set<std::size_t> numeric_columns(const Table& t) {
    std::set<std::size_t> out;
    const std::size_t sample = std::min(t.rows.size(), kNumericSampleRows);
    for (std::size_t column = 0; column < t.header.size(); ++column) {
        bool sawValue = false;
        bool allNumbers = true;
        for (std::size_t r = 0; r < sample; ++r) {
            const auto& row = t.rows[r];
            if (column >= row.size()) continue;
            const std::string_view cell = trim_spaces(row[column]);
            if (cell.empty()) continue;
            sawValue = true;
            if (!is_number(cell)) {
                allNumbers = false;
                break;
            }
        }
        if (sawValue && allNumbers) out.insert(column);
    }
    return out;
}

struct ArchiveEntry {
    std::string path;
    std::optional<std::uint64_t> size;
    bool isDirectory = false;
    friend bool operator==(const ArchiveEntry&, const ArchiveEntry&) = default;
};

struct Listing {
    std::vector<ArchiveEntry> entries;
    std::string container;  // "zip", "tar", "tar in gzip"
    bool truncated = false;
    bool stoppedAtInflateCap = false;
    friend bool operator==(const Listing&, const Listing&) = default;
};

inline constexpr std::string_view kArchiveNameTitle = "Name";
inline constexpr std::string_view kArchiveSizeTitle = "Size";
inline constexpr std::string_view kArchiveFolderLabel = "Folder";

inline std::string archive_size_cell(const ArchiveEntry& e) {
    if (e.isDirectory) return std::string(kArchiveFolderLabel);
    return e.size ? byte_label(*e.size) : std::string("\xe2\x80\x93");
}

namespace detail {
inline std::uint32_t le16(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8);
}
inline std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return le16(b, at) | (le16(b, at + 2) << 16);
}
inline std::optional<std::uint64_t> unsigned_octal(std::string_view s) {
    s = trim_spaces(s);
    if (s.empty()) return std::nullopt;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '7') return std::nullopt;
        v = v * 8 + static_cast<std::uint64_t>(c - '0');
    }
    return v;
}
}  // namespace detail

inline std::optional<Listing> zip_listing(const std::vector<std::uint8_t>& bytes) {
    using detail::le16;
    using detail::le32;
    if (bytes.size() < 22) return std::nullopt;
    std::optional<std::size_t> eocd;
    const std::size_t top = bytes.size() - 22;
    const std::size_t floor = bytes.size() > 22 + 65535 ? bytes.size() - 22 - 65535 : 0;
    for (std::size_t i = top + 1; i-- > floor;) {
        if (bytes[i] == 0x50 && bytes[i + 1] == 0x4B && bytes[i + 2] == 0x05 && bytes[i + 3] == 0x06) {
            eocd = i;
            break;
        }
    }
    if (!eocd) {
        if (bytes[0] == 0x50 && bytes[1] == 0x4B && bytes[2] == 0x05 && bytes[3] == 0x06)
            return Listing{{}, "zip", false, false};
        return std::nullopt;
    }
    const std::size_t count = le16(bytes, *eocd + 10);
    std::size_t cursor = le32(bytes, *eocd + 16);
    Listing out;
    out.container = "zip";
    for (std::size_t n = 0; n < count; ++n) {
        if (cursor + 46 > bytes.size() || bytes[cursor] != 0x50 || bytes[cursor + 1] != 0x4B ||
            bytes[cursor + 2] != 0x01 || bytes[cursor + 3] != 0x02)
            break;
        const std::uint64_t size = le32(bytes, cursor + 24);
        const std::size_t nameLength = le16(bytes, cursor + 28);
        const std::size_t extraLength = le16(bytes, cursor + 30);
        const std::size_t commentLength = le16(bytes, cursor + 32);
        if (cursor + 46 + nameLength > bytes.size()) break;
        std::string name(reinterpret_cast<const char*>(bytes.data() + cursor + 46), nameLength);
        if (out.entries.size() < kEntryCap) {
            const bool dir = !name.empty() && name.back() == '/';
            out.entries.push_back(ArchiveEntry{name, size, dir});
        } else {
            out.truncated = true;
            break;
        }
        cursor += 46 + nameLength + extraLength + commentLength;
    }
    return out;
}

inline std::optional<Listing> tar_listing(const std::vector<std::uint8_t>& bytes, std::string container) {
    Listing out;
    out.container = std::move(container);
    std::size_t cursor = 0;
    const auto field = [&](std::size_t start, std::size_t length) {
        const std::size_t from = cursor + start;
        const std::size_t to = std::min(from + length, bytes.size());
        std::string s;
        for (std::size_t i = from; i < to && bytes[i] != 0; ++i) s.push_back(static_cast<char>(bytes[i]));
        return std::string(trim_spaces(s));
    };
    while (cursor + 512 <= bytes.size()) {
        if (bytes[cursor] == 0) break;
        const std::string name = field(0, 100);
        if (bytes[cursor + 124] & 0x80) break;
        const auto size = detail::unsigned_octal(field(124, 12));
        if (!size) break;
        const std::uint8_t type = bytes[cursor + 156];
        const std::string prefix = field(345, 155);
        const std::string path = prefix.empty() ? name : prefix + "/" + name;
        if (type != 'x' && type != 'g' && type != 'L') {
            if (out.entries.size() < kEntryCap) {
                const bool dir = type == '5';
                out.entries.push_back(ArchiveEntry{path, dir ? std::optional<std::uint64_t>{} : size, dir});
            } else {
                out.truncated = true;
                break;
            }
        }
        const std::uint64_t blocks = *size / 512 + (*size % 512 == 0 ? 1 : 2);
        cursor += static_cast<std::size_t>(blocks * 512);
    }
    if (out.entries.empty() && cursor == 0) return std::nullopt;
    return out;
}

inline bool looks_like_zip(const std::vector<std::uint8_t>& b) {
    return b.size() >= 4 && b[0] == 0x50 && b[1] == 0x4B && (b[2] == 0x03 || b[2] == 0x05 || b[2] == 0x07);
}
inline bool looks_like_gzip(const std::vector<std::uint8_t>& b) { return b.size() >= 2 && b[0] == 0x1F && b[1] == 0x8B; }
inline bool looks_like_tar(const std::vector<std::uint8_t>& b) {
    return b.size() >= 512 && b[257] == 'u' && b[258] == 's' && b[259] == 't' && b[260] == 'a' && b[261] == 'r';
}

struct OutlineRow {
    int depth = 0;
    std::string name;
    ArchiveEntry entry;
    friend bool operator==(const OutlineRow&, const OutlineRow&) = default;
};

inline std::vector<OutlineRow> archive_outline(const std::vector<ArchiveEntry>& entries) {
    struct Node {
        std::string name;
        std::optional<ArchiveEntry> entry;
        bool isFolder = false;
        std::map<std::string, std::size_t> folders;
        std::vector<std::unique_ptr<Node>> children;
        Node* folder(const std::string& n) {
            if (const auto it = folders.find(n); it != folders.end()) return children[it->second].get();
            auto made = std::make_unique<Node>();
            made->name = n;
            made->isFolder = true;
            folders[n] = children.size();
            children.push_back(std::move(made));
            return children.back().get();
        }
    };
    Node root;
    root.isFolder = true;
    for (const ArchiveEntry& e : entries) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : e.path) {
            if (c == '/') {
                if (!cur.empty() && cur != ".") parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty() && cur != ".") parts.push_back(cur);
        if (parts.empty()) continue;
        Node* parent = &root;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) parent = parent->folder(parts[i]);
        if (e.isDirectory) {
            parent->folder(parts.back())->entry = e;
        } else {
            auto leaf = std::make_unique<Node>();
            leaf->name = parts.back();
            leaf->entry = e;
            parent->children.push_back(std::move(leaf));
        }
    }
    std::vector<OutlineRow> rows;
    const auto emit = [&](auto&& self, const Node& node, int depth, const std::string& prefix) -> void {
        for (const auto& child : node.children) {
            const std::string path = prefix + child->name + (child->isFolder ? "/" : "");
            const ArchiveEntry entry = child->entry ? *child->entry : ArchiveEntry{path, std::nullopt, true};
            rows.push_back(OutlineRow{depth, child->name + (child->isFolder ? "/" : ""), entry});
            if (child->isFolder) self(self, *child, depth + 1, path);
        }
    };
    emit(emit, root, 0, "");
    return rows;
}

inline std::vector<std::size_t> visible_outline(const std::vector<OutlineRow>& rows, const std::set<std::size_t>& collapsed) {
    std::vector<std::size_t> shown;
    std::optional<int> hideDeeperThan;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (hideDeeperThan) {
            if (rows[i].depth > *hideDeeperThan) continue;
            hideDeeperThan.reset();
        }
        shown.push_back(i);
        if (rows[i].entry.isDirectory && collapsed.count(i)) hideDeeperThan = rows[i].depth;
    }
    return shown;
}

}  // namespace hanabi::artifact_models
