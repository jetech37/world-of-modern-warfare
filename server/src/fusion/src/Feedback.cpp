/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Feedback.h"

#include <algorithm>
#include <functional>

namespace Fusion::Feedback
{
    bool KnownKind(uint8_t k)
    {
        return k >= uint8_t(Kind::Bug) && k <= uint8_t(Kind::Other);
    }

    char const* KindName(Kind k)
    {
        switch (k)
        {
            case Kind::Bug: return "bug";
            case Kind::Suggestion: return "suggestion";
            case Kind::Other: return "other";
        }
        return "";
    }

    // ── The wire ──

    namespace
    {
        void Put(std::vector<uint8_t>& out, uint64_t v, size_t bytes)
        {
            for (size_t i = 0; i < bytes; ++i)
                out.push_back(uint8_t(v >> (8 * i)));
        }

        bool Get(std::vector<uint8_t> const& in, size_t& at, size_t bytes, uint64_t& v)
        {
            if (at > in.size() || in.size() - at < bytes)
                return false;
            v = 0;
            for (size_t i = 0; i < bytes; ++i)
                v |= uint64_t(in[at + i]) << (8 * i);
            at += bytes;
            return true;
        }

        bool GetText(std::vector<uint8_t> const& in, size_t& at, size_t lenBytes, size_t max, std::string& out)
        {
            uint64_t n = 0;
            if (!Get(in, at, lenBytes, n) || n > max || in.size() - at < n)
                return false;
            out.assign(reinterpret_cast<char const*>(in.data() + at), size_t(n));
            at += size_t(n);
            return true;
        }

        // `s` cut to at most `max` bytes, not inside a UTF-8 character.
        std::string CutBytes(std::string const& s, size_t max)
        {
            if (s.size() <= max)
                return s;
            size_t end = max;
            while (end > 0 && (uint8_t(s[end]) & 0xC0) == 0x80)
                --end;
            return s.substr(0, end);
        }
    }

    std::vector<uint8_t> EncodeSubmit(Submission const& s)
    {
        std::string const title = CutBytes(s.title, 255);
        std::string const body = CutBytes(s.body, kMaxBodyBytes);
        std::string const version = CutBytes(s.version, 255);
        std::vector<uint8_t> out;
        out.reserve(6 + title.size() + body.size() + version.size());
        out.push_back(kOpSubmit);
        out.push_back(uint8_t(s.kind));
        Put(out, title.size(), 1);
        out.insert(out.end(), title.begin(), title.end());
        Put(out, body.size(), 2);
        out.insert(out.end(), body.begin(), body.end());
        Put(out, version.size(), 1);
        out.insert(out.end(), version.begin(), version.end());
        return out;
    }

    bool DecodeSubmit(std::vector<uint8_t> const& body, Submission& out)
    {
        size_t at = 0;
        uint64_t v = 0;
        if (!Get(body, at, 1, v) || v != kOpSubmit)
            return false;
        if (!Get(body, at, 1, v) || !KnownKind(uint8_t(v)))
            return false;
        Submission s;
        s.kind = Kind(v);
        if (!GetText(body, at, 1, 255, s.title) || !GetText(body, at, 2, kMaxBodyBytes, s.body)
            || !GetText(body, at, 1, 255, s.version) || at != body.size())
            return false;
        out = std::move(s);
        return true;
    }

    std::vector<uint8_t> EncodeReply(Reply const& r)
    {
        std::vector<uint8_t> out;
        out.reserve(kReplySize);
        out.push_back(kOpReply);
        out.push_back(uint8_t(r.result));
        Put(out, r.waitSeconds, 4);
        Put(out, r.id, 4);
        out.push_back(r.leftToday);
        return out;
    }

    bool DecodeReply(std::vector<uint8_t> const& body, Reply& out)
    {
        if (body.size() != kReplySize || body[0] != kOpReply || body[1] > uint8_t(Result::Failed))
            return false;
        size_t at = 2;
        uint64_t wait = 0, id = 0;
        Get(body, at, 4, wait);
        Get(body, at, 4, id);
        out.result = Result(body[1]);
        out.waitSeconds = uint32_t(wait);
        out.id = uint32_t(id);
        out.leftToday = body[10];
        return true;
    }

    // ── Text ──

    namespace
    {
        constexpr uint32_t kDropped = 0xFFFFFFFFu;

        // Decodes UTF-8 to code points, skipping what is not well-formed (overlong forms,
        // surrogates, stray continuation bytes, past U+10FFFF).
        std::vector<uint32_t> Decode(std::string const& s)
        {
            std::vector<uint32_t> cps;
            cps.reserve(s.size());
            size_t i = 0;
            size_t const n = s.size();
            auto cont = [&](size_t at) { return at < n && (uint8_t(s[at]) & 0xC0) == 0x80; };
            while (i < n)
            {
                uint8_t const b = uint8_t(s[i]);
                if (b < 0x80)
                {
                    cps.push_back(b);
                    ++i;
                    continue;
                }
                if (b >= 0xC2 && b <= 0xDF && cont(i + 1))
                {
                    cps.push_back((uint32_t(b & 0x1F) << 6) | (uint8_t(s[i + 1]) & 0x3F));
                    i += 2;
                    continue;
                }
                if (b >= 0xE0 && b <= 0xEF && cont(i + 1) && cont(i + 2))
                {
                    uint8_t const b1 = uint8_t(s[i + 1]);
                    bool const ok = (b != 0xE0 || b1 >= 0xA0) && (b != 0xED || b1 < 0xA0);
                    if (ok)
                    {
                        cps.push_back((uint32_t(b & 0x0F) << 12) | (uint32_t(b1 & 0x3F) << 6)
                            | (uint8_t(s[i + 2]) & 0x3F));
                        i += 3;
                        continue;
                    }
                }
                if (b >= 0xF0 && b <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3))
                {
                    uint8_t const b1 = uint8_t(s[i + 1]);
                    bool const ok = (b != 0xF0 || b1 >= 0x90) && (b != 0xF4 || b1 < 0x90);
                    if (ok)
                    {
                        cps.push_back((uint32_t(b & 0x07) << 18) | (uint32_t(b1 & 0x3F) << 12)
                            | (uint32_t(uint8_t(s[i + 2]) & 0x3F) << 6) | (uint8_t(s[i + 3]) & 0x3F));
                        i += 4;
                        continue;
                    }
                }
                ++i;    // not well-formed: the byte is dropped
            }
            return cps;
        }

        void Append(std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
                out.push_back(char(cp));
            else if (cp < 0x800)
            {
                out.push_back(char(0xC0 | (cp >> 6)));
                out.push_back(char(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(char(0xE0 | (cp >> 12)));
                out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(char(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(char(0xF0 | (cp >> 18)));
                out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(char(0x80 | (cp & 0x3F)));
            }
        }

        bool Invisible(uint32_t cp)
        {
            return cp == 0x00AD                         // soft hyphen
                || (cp >= 0x200B && cp <= 0x200F)       // zero-width, LRM/RLM
                || (cp >= 0x202A && cp <= 0x202E)       // bidi embeddings and overrides
                || (cp >= 0x2060 && cp <= 0x2064)       // word joiner, invisible operators
                || (cp >= 0x2066 && cp <= 0x2069)       // bidi isolates
                || cp == 0xFEFF;                        // byte order mark
        }

        // One code point: kept, mapped ('\n' for a line break, ' ' for a tab, '?' past the BMP)
        // or kDropped. `lines`: line breaks stay; otherwise they are spaces.
        uint32_t Map(uint32_t cp, bool lines)
        {
            if (cp == '\n' || cp == 0x2028 || cp == 0x2029)
                return lines ? '\n' : ' ';
            if (cp == '\t')
                return ' ';
            if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F))
                return kDropped;
            if (Invisible(cp))
                return kDropped;
            if (cp > 0xFFFF)
                return '?';
            return cp;
        }

        std::string Clean(std::string const& in, size_t maxChars, bool lines)
        {
            std::vector<uint32_t> const raw = Decode(in);
            std::vector<uint32_t> cps;
            cps.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i)
            {
                uint32_t cp = raw[i];
                if (cp == '\r')
                {
                    // "\r\n" is one break, a lone "\r" is one too.
                    if (i + 1 < raw.size() && raw[i + 1] == '\n')
                        continue;
                    cp = '\n';
                }
                if (cp == '|' && i + 1 < raw.size() && raw[i + 1] == '|')
                    ++i;    // WoW's escaped bar: one bar
                cp = Map(cp, lines);
                if (cp == kDropped)
                    continue;
                if (cp == '\n')
                {
                    // The line's trailing spaces go; at most one empty line in a row.
                    while (!cps.empty() && cps.back() == ' ')
                        cps.pop_back();
                    size_t breaks = 0;
                    for (size_t k = cps.size(); k > 0 && cps[k - 1] == '\n'; --k)
                        ++breaks;
                    if (breaks >= 2)
                        continue;
                }
                else if (cp == ' ' && !lines && !cps.empty() && cps.back() == ' ')
                    continue;   // one line: runs of spaces as one
                cps.push_back(cp);
            }
            auto blank = [](uint32_t c) { return c == ' ' || c == '\n'; };
            size_t begin = 0;
            while (begin < cps.size() && blank(cps[begin]))
                ++begin;
            size_t end = std::min(cps.size(), begin + maxChars);
            while (end > begin && blank(cps[end - 1]))
                --end;
            std::string out;
            out.reserve(end - begin);
            for (size_t i = begin; i < end; ++i)
                Append(out, cps[i]);
            return out;
        }
    }

    size_t CountChars(std::string const& s)
    {
        size_t n = 0;
        for (char c : s)
            if ((uint8_t(c) & 0xC0) != 0x80)
                ++n;
        return n;
    }

    std::string CleanLine(std::string const& in, size_t maxChars)
    {
        return Clean(in, maxChars, false);
    }

    std::string CleanText(std::string const& in, size_t maxChars)
    {
        return Clean(in, maxChars, true);
    }

    std::string EscapeChat(std::string const& in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
        {
            out.push_back(c);
            if (c == '|')
                out.push_back('|');
        }
        return out;
    }

    // ── The limits ──

    Verdict Check(std::vector<uint64_t> const& sentAt, uint64_t now, Limits const& limits)
    {
        // The day's reports, newest first; a time ahead of now counts as now.
        std::vector<uint64_t> recent;
        recent.reserve(sentAt.size());
        for (uint64_t t : sentAt)
        {
            uint64_t const at = std::min(t, now);
            if (at + kDaySeconds > now)
                recent.push_back(at);
        }
        std::sort(recent.begin(), recent.end(), std::greater<uint64_t>());

        Verdict v;
        if (limits.dailyLimit)
        {
            size_t const used = std::min<size_t>(recent.size(), limits.dailyLimit);
            v.leftToday = uint8_t(std::min<size_t>(limits.dailyLimit - used, kUnlimited - 1));
        }
        uint64_t cooldownWait = 0, dailyWait = 0;
        if (limits.cooldownSeconds && !recent.empty() && recent.front() + limits.cooldownSeconds > now)
            cooldownWait = recent.front() + limits.cooldownSeconds - now;
        if (limits.dailyLimit && recent.size() >= limits.dailyLimit)
            dailyWait = recent[limits.dailyLimit - 1] + kDaySeconds - now;
        if (dailyWait && dailyWait >= cooldownWait)
        {
            v.result = Result::DailyLimit;
            v.waitSeconds = uint32_t(dailyWait);
        }
        else if (cooldownWait)
        {
            v.result = Result::Cooldown;
            v.waitSeconds = uint32_t(cooldownWait);
        }
        return v;
    }

    Verdict Limiter::Check(uint32_t account, uint64_t now) const
    {
        auto const it = m_sent.find(account);
        if (it == m_sent.end())
            return Fusion::Feedback::Check({}, now, m_limits);
        return Fusion::Feedback::Check(it->second, now, m_limits);
    }

    void Limiter::Record(uint32_t account, uint64_t at, uint64_t now)
    {
        std::vector<uint64_t>& sent = m_sent[account];
        sent.push_back(at);
        sent.erase(std::remove_if(sent.begin(), sent.end(),
            [now](uint64_t t) { return std::min(t, now) + kDaySeconds <= now; }), sent.end());
        if (sent.empty())
            m_sent.erase(account);
    }
}
