/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Sha256.h"

namespace Fusion { namespace Sha256
{
    namespace
    {
        uint32_t const kK[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };

        uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        void Block(uint32_t h[8], uint8_t const* p)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | uint32_t(p[4 * i + 3]);
            for (int i = 16; i < 64; ++i)
            {
                uint32_t const s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t const s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
            for (int i = 0; i < 64; ++i)
            {
                uint32_t const s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
                uint32_t const ch = (e & f) ^ (~e & g);
                uint32_t const t1 = k + s1 + ch + kK[i] + w[i];
                uint32_t const s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
                uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t const t2 = s0 + maj;
                k = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
        }
    }

    Digest Hash(uint8_t const* data, size_t size)
    {
        uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        size_t i = 0;
        for (; i + 64 <= size; i += 64)
            Block(h, data + i);
        // The tail, the 1 bit, zeros and the length in bits (big-endian), in one or two blocks.
        uint8_t tail[128] = {};
        size_t const rest = size - i;
        for (size_t k = 0; k < rest; ++k)
            tail[k] = data[i + k];
        tail[rest] = 0x80;
        size_t const blocks = rest + 1 + 8 <= 64 ? 1 : 2;
        uint64_t const bits = uint64_t(size) * 8;
        for (int k = 0; k < 8; ++k)
            tail[blocks * 64 - 1 - k] = uint8_t(bits >> (8 * k));
        for (size_t b = 0; b < blocks; ++b)
            Block(h, tail + b * 64);
        Digest out;
        for (int k = 0; k < 8; ++k)
        {
            out[4 * k] = uint8_t(h[k] >> 24);
            out[4 * k + 1] = uint8_t(h[k] >> 16);
            out[4 * k + 2] = uint8_t(h[k] >> 8);
            out[4 * k + 3] = uint8_t(h[k]);
        }
        return out;
    }

    Digest Hash(std::string const& s)
    {
        return Hash(reinterpret_cast<uint8_t const*>(s.data()), s.size());
    }

    Digest Hash(std::vector<uint8_t> const& v)
    {
        return Hash(v.data(), v.size());
    }

    std::string Hex(uint8_t const* data, size_t size)
    {
        static char const digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(size * 2);
        for (size_t i = 0; i < size; ++i)
        {
            s.push_back(digits[data[i] >> 4]);
            s.push_back(digits[data[i] & 15]);
        }
        return s;
    }

    std::string Hex(Digest const& d)
    {
        return Hex(d.data(), d.size());
    }

    bool FromHex(std::string const& hex, std::vector<uint8_t>& out)
    {
        out.clear();
        if (hex.size() % 2)
            return false;
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            int const hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0)
            {
                out.clear();
                return false;
            }
            out.push_back(uint8_t(hi << 4 | lo));
        }
        return true;
    }
}}
