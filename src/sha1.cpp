#include "minigit/sha1.hpp"
#include "minigit/io.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace minigit {
namespace {
std::uint32_t rol(std::uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}
constexpr char hex[] = "0123456789abcdef";
} // namespace
std::string sha1(std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
        throw Error("Input too large for SHA-1");
    std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
    const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56)
        data.push_back(0);
    for (int i = 7; i >= 0; --i)
        data.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    std::array<std::uint32_t, 5> h{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (std::size_t p = 0; p < data.size(); p += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | data[p + i * 4 + j];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            auto t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::string out;
    for (auto word : h)
        for (int i = 7; i >= 0; --i)
            out += hex[(word >> (i * 4)) & 15];
    return out;
}
bool validOid(std::string_view oid) {
    if (oid.size() != 40)
        return false;
    for (char c : oid)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
std::string oidToRaw(std::string_view oid) {
    if (!validOid(oid))
        throw Error("Invalid object ID");
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string out;
    for (std::size_t i = 0; i < 40; i += 2)
        out += static_cast<char>((digit(oid[i]) << 4) | digit(oid[i + 1]));
    return out;
}
std::string rawToOid(std::string_view raw) {
    if (raw.size() != 20)
        throw Error("Invalid raw object ID");
    std::string out;
    for (unsigned char c : raw) {
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}
} // namespace minigit
