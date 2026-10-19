#include <cstdio>
#include <cstring>

#include "../../src/ui/artifact_models.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using namespace hanabi::artifact_models;

static void test_csv() {
    const auto t = table("sku,price,qty\r\n\"A, Inc\",4.99,3\r\nB,39.99,\r\n\"say \"\"hi\"\"\",\"$1,200.50\",-7\n");
    CHECK(t.has_value());
    CHECK(t->delimiter == ',');
    CHECK((t->header == std::vector<std::string>{"sku", "price", "qty"}));
    CHECK(t->rows.size() == 3);
    CHECK(t->rows[0][0] == "A, Inc");
    CHECK(t->rows[2][0] == "say \"hi\"");
    CHECK(t->rows[2][1] == "$1,200.50");
    CHECK(!t->truncated);
    const auto numeric = numeric_columns(*t);
    CHECK(numeric.count(1) == 1);
    CHECK(numeric.count(2) == 1);
    CHECK(numeric.count(0) == 0);
    const auto tsv = table("a\tb,c\td\n1\t2\t3\n");
    CHECK(tsv && tsv->delimiter == '\t');
    CHECK((tsv->header == std::vector<std::string>{"a", "b,c", "d"}));
    CHECK(!table("").has_value());
    CHECK(!table(",,\n1,2,3\n").has_value());
    std::string big = "h\n";
    for (int i = 0; i < 600; ++i) big += std::to_string(i) + "\n";
    const auto capped = table(big);
    CHECK(capped && capped->truncated);
    CHECK(capped->rows.size() == kTableRowCap);
    CHECK(is_number("4.99"));
    CHECK(is_number("-7"));
    CHECK(is_number("+12"));
    CHECK(is_number("$1,200.50"));
    CHECK(is_number("\xe2\x82\xac" "5"));
    CHECK(is_number("12%"));
    CHECK(is_number("\xe2\x88\x92" "3"));
    CHECK(!is_number(".5"));
    CHECK(!is_number("1.2.3"));
    CHECK(!is_number("abc"));
    CHECK(!is_number(""));
    CHECK(!is_number("$"));
    CHECK(byte_label(512) == "512 B");
    CHECK(byte_label(2048) == "2 KB");
    CHECK(byte_label(1536 * 1024) == "1.5 MB");
}

static void put16(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(v & 0xFF);
    b.push_back((v >> 8) & 0xFF);
}
static void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

static std::vector<std::uint8_t> make_zip(const std::vector<std::pair<std::string, std::uint32_t>>& names) {
    std::vector<std::uint8_t> b;
    std::vector<std::size_t> offsets;
    for (const auto& [name, size] : names) {
        offsets.push_back(b.size());
        b.insert(b.end(), {0x50, 0x4B, 0x03, 0x04});
        for (int i = 0; i < 22; ++i) b.push_back(0);
        put16(b, static_cast<std::uint32_t>(name.size()));
        put16(b, 0);
        b.insert(b.end(), name.begin(), name.end());
    }
    const std::size_t cd = b.size();
    for (std::size_t i = 0; i < names.size(); ++i) {
        b.insert(b.end(), {0x50, 0x4B, 0x01, 0x02});
        for (int k = 0; k < 20; ++k) b.push_back(0);
        put32(b, names[i].second);
        put16(b, static_cast<std::uint32_t>(names[i].first.size()));
        put16(b, 0);
        put16(b, 0);
        for (int k = 0; k < 8; ++k) b.push_back(0);
        put32(b, static_cast<std::uint32_t>(offsets[i]));
        b.insert(b.end(), names[i].first.begin(), names[i].first.end());
    }
    const std::size_t cdSize = b.size() - cd;
    b.insert(b.end(), {0x50, 0x4B, 0x05, 0x06});
    put16(b, 0);
    put16(b, 0);
    put16(b, static_cast<std::uint32_t>(names.size()));
    put16(b, static_cast<std::uint32_t>(names.size()));
    put32(b, static_cast<std::uint32_t>(cdSize));
    put32(b, static_cast<std::uint32_t>(cd));
    put16(b, 0);
    return b;
}

static std::vector<std::uint8_t> make_tar(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<std::uint8_t> b;
    for (const auto& [name, body] : files) {
        std::vector<std::uint8_t> h(512, 0);
        std::memcpy(h.data(), name.data(), name.size());
        char size[13];
        std::snprintf(size, sizeof size, "%011o", static_cast<unsigned>(body.size()));
        std::memcpy(h.data() + 124, size, 11);
        h[156] = name.back() == '/' ? '5' : '0';
        std::memcpy(h.data() + 257, "ustar", 5);
        b.insert(b.end(), h.begin(), h.end());
        b.insert(b.end(), body.begin(), body.end());
        while (b.size() % 512) b.push_back(0);
    }
    b.insert(b.end(), 1024, 0);
    return b;
}

static void test_archives() {
    const auto z = zip_listing(make_zip({{"docs/", 0}, {"docs/readme.md", 120}, {"src/main.cpp", 4096}, {"LICENSE", 1100}}));
    CHECK(z.has_value());
    CHECK(z->container == "zip");
    CHECK(z->entries.size() == 4);
    CHECK(z->entries[0].isDirectory);
    CHECK(z->entries[1].size == 120u);
    CHECK(archive_size_cell(z->entries[0]) == "Folder");
    CHECK(archive_size_cell(z->entries[2]) == "4 KB");
    CHECK(archive_size_cell(ArchiveEntry{"x", std::nullopt, false}) == "\xe2\x80\x93");
    const auto rows = archive_outline(z->entries);
    CHECK(rows.size() == 5);
    CHECK(rows[0].name == "docs/" && rows[0].depth == 0);
    CHECK(rows[1].name == "readme.md" && rows[1].depth == 1);
    CHECK(rows[2].name == "src/" && rows[2].depth == 0 && rows[2].entry.isDirectory && !rows[2].entry.size);
    CHECK(rows[3].name == "main.cpp" && rows[3].depth == 1);
    CHECK(rows[4].name == "LICENSE" && rows[4].depth == 0);
    CHECK((visible_outline(rows, {0}) == std::vector<std::size_t>{0, 2, 3, 4}));
    CHECK((visible_outline(rows, {}) == std::vector<std::size_t>{0, 1, 2, 3, 4}));
    CHECK(!zip_listing({1, 2, 3}).has_value());
    const auto t = tar_listing(make_tar({{"a/", ""}, {"a/b.txt", "hello"}, {"c.bin", std::string(600, 'x')}}), "tar");
    CHECK(t.has_value());
    CHECK(t->entries.size() == 3);
    CHECK(t->entries[0].isDirectory && !t->entries[0].size);
    CHECK(t->entries[1].size == 5u);
    CHECK(t->entries[2].size == 600u);
    CHECK(looks_like_tar(make_tar({{"x", "y"}})));
    CHECK(looks_like_zip(make_zip({{"a", 1}})));
    CHECK(looks_like_gzip({0x1F, 0x8B, 0x08}));
    CHECK(!looks_like_gzip({0x50, 0x4B}));
}

int main() {
    test_csv();
    test_archives();
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
