/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_SHA256_H
#define FUSION_SHA256_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// SHA-256 (FIPS 180-4), small and dependency-free so the rules library keeps building anywhere
// the server does. Used to keep door codes (Locks.h) as salted hashes, never as the code.
namespace Fusion { namespace Sha256
{
    using Digest = std::array<uint8_t, 32>;

    Digest Hash(uint8_t const* data, size_t size);
    Digest Hash(std::string const& s);
    Digest Hash(std::vector<uint8_t> const& v);
    // Lower-case hex of any bytes, and back (false on an odd length or a non-hex character).
    std::string Hex(uint8_t const* data, size_t size);
    std::string Hex(Digest const& d);
    bool FromHex(std::string const& hex, std::vector<uint8_t>& out);
}}

#endif
