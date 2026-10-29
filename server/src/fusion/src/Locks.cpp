/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Locks.h"

#include "fusion/Sha256.h"

#include <algorithm>

namespace Fusion { namespace Locks
{
    static_assert(uint8_t(kDefault) == Build::kDefaultLock, "a new piece's lock (Build.h) is the default mode");

    bool ValidMode(uint8_t m)
    {
        return m < kModeCount;
    }

    char const* ModeName(Mode m)
    {
        switch (m)
        {
            case Mode::Unlocked: return "Unlocked";
            case Mode::Owner: return "Owner only";
            case Mode::Party: return "Owner and party";
            case Mode::Guild: return "Owner and guild";
            case Mode::Password: return "Code";
        }
        return "Unlocked";
    }

    bool Lockable(Build::PieceType t)
    {
        return Build::BaseOf(t) == Build::PieceType::WoodDoor || Build::IsPortal(t);
    }

    bool ValidCode(std::string const& code)
    {
        return code.size() >= kMinCode && code.size() <= kMaxCode
            && std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    std::string HashCode(std::string const& code, std::string const& saltHex)
    {
        std::vector<uint8_t> salt;
        if (!Sha256::FromHex(saltHex, salt) || salt.empty())
            return std::string();
        std::vector<uint8_t> in(salt);
        in.insert(in.end(), code.begin(), code.end());
        Sha256::Digest d = Sha256::Hash(in);
        std::vector<uint8_t> round(d.size() + salt.size());
        for (uint32_t i = 0; i < kStretch; ++i)
        {
            std::copy(d.begin(), d.end(), round.begin());
            std::copy(salt.begin(), salt.end(), round.begin() + std::ptrdiff_t(d.size()));
            d = Sha256::Hash(round);
        }
        return Sha256::Hex(d);
    }

    Secret MakeSecret(std::string const& code, std::vector<uint8_t> const& saltBytes)
    {
        Secret s;
        if (!ValidCode(code) || saltBytes.size() < 8)
            return s;
        s.salt = Sha256::Hex(saltBytes.data(), saltBytes.size());
        s.hash = HashCode(code, s.salt);
        if (s.hash.empty())
            s = Secret();
        return s;
    }

    bool CheckCode(Secret const& s, std::string const& code)
    {
        if (!s.Set() || !ValidCode(code))
            return false;
        std::string const h = HashCode(code, s.salt);
        if (h.size() != s.hash.size())
            return false;
        uint8_t diff = 0;
        for (size_t i = 0; i < h.size(); ++i)
            diff |= uint8_t(h[i] ^ s.hash[i]);
        return diff == 0;
    }

    Verdict MayOpen(Mode mode, Visitor const& v)
    {
        if (v.gm || v.owner || v.household)
            return Verdict::Open;
        switch (mode)
        {
            case Mode::Unlocked:
                return Verdict::Open;
            case Mode::Owner:
                return Verdict::Locked;
            case Mode::Party:
                return v.party ? Verdict::Open : Verdict::Locked;
            case Mode::Guild:
                return v.guild ? Verdict::Open : Verdict::Locked;
            case Mode::Password:
                return v.knowsCode ? Verdict::Open : Verdict::NeedsCode;
        }
        return Verdict::Locked;
    }

    bool MaySet(bool owner, bool plotOwner, bool gm)
    {
        return owner || plotOwner || gm;
    }

    int64_t Limiter::WaitFor(uint64_t key, int64_t now) const
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || it->second.until <= now)
            return 0;
        return it->second.until - now;
    }

    int64_t Limiter::Miss(uint64_t key, int64_t now)
    {
        Entry& e = m_entries[key];
        // A lock left alone a long while starts over.
        if (e.last && now - e.last >= kForgetSeconds)
            e = Entry();
        e.last = now;
        ++e.misses;
        if (e.misses <= kFreeTries)
            return 0;
        int64_t wait = kFirstLockout;
        for (uint32_t k = kFreeTries + 1; k < e.misses && wait < kMaxLockout; ++k)
            wait *= 2;
        wait = std::min(wait, kMaxLockout);
        e.until = now + wait;
        return wait;
    }

    void Limiter::Clear(uint64_t key)
    {
        m_entries.erase(key);
    }

    void Limiter::Forget(int64_t now)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (it->second.until <= now && now - it->second.last >= kForgetSeconds)
                it = m_entries.erase(it);
            else
                ++it;
        }
    }

    namespace
    {
        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        uint32_t Get32(uint8_t const* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

        // A code's digits as they come: at most kMaxCode, digits only (an empty one is fine here).
        bool DigitsOk(std::string const& s)
        {
            return s.size() <= kMaxCode && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        }
    }

    bool DecodeSetLock(std::vector<uint8_t> const& body, SetLock& out)
    {
        if (body.size() < 7 || body[0] != kOpSetLock || !ValidMode(body[5]))
            return false;
        size_t const n = body[6];
        if (body.size() != 7 + n)
            return false;
        out.piece = Get32(&body[1]);
        out.mode = Mode(body[5]);
        out.code.assign(body.begin() + 7, body.end());
        return DigitsOk(out.code);
    }

    std::vector<uint8_t> EncodeSetLock(SetLock const& s)
    {
        std::vector<uint8_t> o{kOpSetLock};
        Put32(o, s.piece);
        o.push_back(uint8_t(s.mode));
        size_t const n = std::min<size_t>(s.code.size(), kMaxCode);
        o.push_back(uint8_t(n));
        o.insert(o.end(), s.code.begin(), s.code.begin() + std::ptrdiff_t(n));
        return o;
    }

    bool DecodeTryCode(std::vector<uint8_t> const& body, uint32_t& piece, std::string& code)
    {
        if (body.size() < 6 || body[0] != kOpTryCode)
            return false;
        size_t const n = body[5];
        if (body.size() != 6 + n)
            return false;
        piece = Get32(&body[1]);
        code.assign(body.begin() + 6, body.end());
        return DigitsOk(code);
    }

    std::vector<uint8_t> EncodeTryCode(uint32_t piece, std::string const& code)
    {
        std::vector<uint8_t> o{kOpTryCode};
        Put32(o, piece);
        size_t const n = std::min<size_t>(code.size(), kMaxCode);
        o.push_back(uint8_t(n));
        o.insert(o.end(), code.begin(), code.begin() + std::ptrdiff_t(n));
        return o;
    }

    std::vector<uint8_t> EncodeAskCode(uint32_t piece, bool portal)
    {
        std::vector<uint8_t> o{kOpAskCode};
        Put32(o, piece);
        o.push_back(portal ? 1 : 0);
        return o;
    }
}}
