#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <stb/stb_image.h>

namespace hanabi::inflate {

struct Result {
    std::vector<std::uint8_t> bytes;
    bool stoppedAtCap = false;
};

inline Result gunzip(const std::vector<std::uint8_t>& gz, std::size_t cap) {
    Result out;
    if (gz.size() < 18 || gz[0] != 0x1F || gz[1] != 0x8B || gz[2] != 8) return out;
    const std::uint8_t flags = gz[3];
    std::size_t at = 10;
    if (flags & 0x04) {
        if (at + 2 > gz.size()) return out;
        const std::size_t xlen = gz[at] | (gz[at + 1] << 8);
        at += 2 + xlen;
    }
    if (flags & 0x08) {
        while (at < gz.size() && gz[at] != 0) ++at;
        ++at;
    }
    if (flags & 0x10) {
        while (at < gz.size() && gz[at] != 0) ++at;
        ++at;
    }
    if (flags & 0x02) at += 2;
    if (at + 8 > gz.size()) return out;
    const std::size_t n = gz.size() - 8;
    const std::uint32_t isize = static_cast<std::uint32_t>(gz[n]) | (static_cast<std::uint32_t>(gz[n + 1]) << 8) |
                                (static_cast<std::uint32_t>(gz[n + 2]) << 16) | (static_cast<std::uint32_t>(gz[n + 3]) << 24);
    if (isize > cap) {
        out.stoppedAtCap = true;
        return out;
    }
    int outlen = 0;
    char* raw = stbi_zlib_decode_malloc_guesssize_headerflag(reinterpret_cast<const char*>(gz.data() + at),
                                                             static_cast<int>(n - at),
                                                             static_cast<int>(isize > 0 ? isize : 1 << 16), &outlen, 0);
    if (raw == nullptr || outlen <= 0) {
        std::free(raw);
        return out;
    }
    if (static_cast<std::size_t>(outlen) > cap) {
        outlen = static_cast<int>(cap);
        out.stoppedAtCap = true;
    }
    out.bytes.assign(raw, raw + outlen);
    std::free(raw);
    return out;
}

}  // namespace hanabi::inflate
