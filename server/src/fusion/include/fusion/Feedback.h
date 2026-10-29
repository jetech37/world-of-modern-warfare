/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_FEEDBACK_H
#define FUSION_FEEDBACK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Bug reports and suggestions sent from the game (the Escape menu's "Report Bug / Suggest"). The
// player types a kind, a title and a description; the server cleans the text, checks the
// account's limits (one report per cooldown, a few per day), adds where the player stands and
// what they target, and keeps it in the characters database's `aw_feedback` table, which the
// owner reads with `aw-admin feedback` (vps/aw-admin.sh). The server is the only judge of the
// limits; the client greys its button from the server's answer.
namespace Fusion::Feedback
{
    enum class Kind : uint8_t
    {
        Bug = 1,
        Suggestion = 2,
        Other = 3,
    };
    bool KnownKind(uint8_t k);
    // The table's ENUM value: "bug", "suggestion", "other" ("" for an unknown kind).
    char const* KindName(Kind k);

    // What a report may hold, in characters (code points) after cleaning.
    constexpr size_t kMaxTitleChars = 80;
    constexpr size_t kMinTitleChars = 3;
    constexpr size_t kMaxBodyChars = 1000;
    constexpr size_t kMaxVersionChars = 64;
    // The wire's limits, in bytes: a title's length is one byte; a body of 1000 four-byte
    // characters fits.
    constexpr size_t kMaxBodyBytes = 4096;

    // ── The wire: a channel of its own (SMSG/CMSG_FUSION_CHANNEL 9), little-endian ──
    // Channels 1-8 are taken (Packets.h build 1 and world 2, achievements 3, companions 4,
    // downed 5, stats 6, classes 7, prestige 8); the world channel's ops are crowded.
    // Client to server:
    //   op 1, submit: [1, kind u8, title_len u8, title, body_len u16, body, version_len u8, version]
    //   op 2, ask:    [2]  (the reply says whether a report may be sent now)
    // Server to client:
    //   op 1, reply:  [1, result u8, wait u32 seconds, id u32, left today u8]
    constexpr uint8_t kChannelFeedback = 9;
    constexpr uint8_t kOpSubmit = 1;
    constexpr uint8_t kOpAsk = 2;
    constexpr uint8_t kOpReply = 1;
    constexpr size_t kReplySize = 11;
    // `left today` when the realm has no daily limit.
    constexpr uint8_t kUnlimited = 255;

    enum class Result : uint8_t
    {
        Sent = 0,           // kept; `id` is its number
        Ready = 1,          // the answer to an ask: a report may be sent now
        Cooldown = 2,       // too soon after the last one: `wait` seconds
        DailyLimit = 3,     // the day's reports are used up: `wait` until one frees
        Disabled = 4,       // the realm takes no reports (Feedback.Enable 0)
        Invalid = 5,        // no title, an unknown kind, or a malformed message
        Failed = 6,         // the server could not keep it
    };

    struct Submission
    {
        Kind kind = Kind::Bug;
        std::string title;
        std::string body;
        std::string version;    // the client's version (the launcher's published version)
    };

    struct Reply
    {
        Result result = Result::Ready;
        uint32_t waitSeconds = 0;
        uint32_t id = 0;
        uint8_t leftToday = kUnlimited;
    };

    // The submit as the client sends it; over-long fields are cut to what the wire carries.
    std::vector<uint8_t> EncodeSubmit(Submission const& s);
    // False on another op, an unknown kind, a field longer than the wire's limits, or a short or
    // overlong buffer. The text comes back as sent: clean it before use.
    bool DecodeSubmit(std::vector<uint8_t> const& body, Submission& out);

    std::vector<uint8_t> EncodeReply(Reply const& r);
    bool DecodeReply(std::vector<uint8_t> const& body, Reply& out);

    // ── Text ──
    // Characters (code points) in UTF-8 text; a stray byte counts as one.
    size_t CountChars(std::string const& s);
    // One line, cleaned: invalid UTF-8 dropped, characters past U+FFFF (which the utf8 table can't
    // hold) as '?', control and invisible formatting characters (bidi overrides, zero-width)
    // dropped, tabs and line breaks as spaces, "||" (WoW's escaped bar) as "|", runs of spaces as
    // one, trimmed, and cut to `maxChars` characters.
    std::string CleanLine(std::string const& in, size_t maxChars);
    // Several lines, cleaned as CleanLine but keeping line breaks ("\r\n" and "\r" become "\n"),
    // each line's trailing spaces dropped, at most one empty line in a row, trimmed.
    std::string CleanText(std::string const& in, size_t maxChars);
    // Text for a chat line: '|' doubled, so the client shows it rather than reading a colour
    // or a link.
    std::string EscapeChat(std::string const& in);

    // ── The limits ──
    constexpr uint32_t kDaySeconds = 86400;
    constexpr uint32_t kDefaultCooldownSeconds = 600;
    constexpr uint32_t kDefaultDailyLimit = 5;

    struct Limits
    {
        uint32_t cooldownSeconds = kDefaultCooldownSeconds;     // 0: none
        uint32_t dailyLimit = kDefaultDailyLimit;               // a rolling 24 hours; 0: none
    };

    struct Verdict
    {
        Result result = Result::Ready;      // Ready, Cooldown or DailyLimit
        uint32_t waitSeconds = 0;
        uint8_t leftToday = kUnlimited;     // reports left in the rolling day (before this one)
    };

    // Whether an account that sent reports at `sentAt` (unix seconds, any order) may send one at
    // `now`. When both limits hold it back, the longer wait is the answer. A time after `now` (the
    // clock went back) counts as just now.
    Verdict Check(std::vector<uint64_t> const& sentAt, uint64_t now, Limits const& limits);

    // Each account's recent reports, for Check. Not locked: the game holds its own lock.
    class Limiter
    {
    public:
        void SetLimits(Limits const& l) { m_limits = l; }
        Limits const& GetLimits() const { return m_limits; }
        Verdict Check(uint32_t account, uint64_t now) const;
        // A report kept at `at`; reports older than a day are forgotten.
        void Record(uint32_t account, uint64_t at, uint64_t now);
        void Clear() { m_sent.clear(); }
        size_t Accounts() const { return m_sent.size(); }

    private:
        Limits m_limits;
        std::unordered_map<uint32_t, std::vector<uint64_t>> m_sent;
    };
}

#endif
