/*
 * Azeroth Warfare: tests for bug reports and suggestions (Feedback.h). GPL-2.0-or-later, see
 * Common.h.
 */

#include "Test.h"
#include "fusion/Feedback.h"

using namespace Fusion::Feedback;

// ── The wire ──

TEST(feedback_submit_bytes_are_the_clients)
{
    // The same bytes as the client's test (client/src/feedback.rs).
    Submission s;
    s.kind = Kind::Suggestion;
    s.title = "Hi";
    s.body = "a\nb";
    s.version = "v1";
    std::vector<uint8_t> const want = {1, 2, 2, 'H', 'i', 3, 0, 'a', '\n', 'b', 2, 'v', '1'};
    CHECK(EncodeSubmit(s) == want);
    Submission back;
    CHECK(DecodeSubmit(want, back));
    CHECK(back.kind == Kind::Suggestion && back.title == "Hi" && back.body == "a\nb" && back.version == "v1");
}

TEST(feedback_submit_round_trips_long_text)
{
    Submission s;
    s.kind = Kind::Other;
    s.title = std::string(255, 't');
    s.body = std::string(kMaxBodyBytes, 'b');
    s.version = "2026-10-18 12:00";
    Submission back;
    CHECK(DecodeSubmit(EncodeSubmit(s), back));
    CHECK(back.title.size() == 255 && back.body.size() == kMaxBodyBytes && back.version == s.version);
    // Longer than the wire carries: cut, never inside a character.
    s.title = std::string(254, 'x') + "\xC3\xA9";     // 256 bytes, the last character 2 bytes
    s.body = std::string(kMaxBodyBytes + 50, 'y');
    CHECK(DecodeSubmit(EncodeSubmit(s), back));
    CHECK(back.title == std::string(254, 'x'));
    CHECK(back.body.size() == kMaxBodyBytes);
}

TEST(feedback_submit_refuses_what_is_malformed)
{
    Submission s;
    s.title = "Title";
    s.body = "Body";
    std::vector<uint8_t> good = EncodeSubmit(s);
    Submission out;
    CHECK(DecodeSubmit(good, out));
    // Another op, an unknown kind, short, trailing bytes, an empty buffer.
    std::vector<uint8_t> b = good;
    b[0] = kOpAsk;
    CHECK(!DecodeSubmit(b, out));
    for (uint8_t kind : {uint8_t(0), uint8_t(4), uint8_t(255)})
    {
        b = good;
        b[1] = kind;
        CHECK(!DecodeSubmit(b, out));
    }
    b = good;
    b.pop_back();
    CHECK(!DecodeSubmit(b, out));
    b = good;
    b.push_back(0);
    CHECK(!DecodeSubmit(b, out));
    CHECK(!DecodeSubmit({}, out));
    CHECK(!DecodeSubmit({kOpSubmit}, out));
    // A body length past the wire's limit, even with the bytes there.
    std::vector<uint8_t> big = {kOpSubmit, 1, 1, 'T'};
    uint16_t const n = uint16_t(kMaxBodyBytes + 1);
    big.push_back(uint8_t(n));
    big.push_back(uint8_t(n >> 8));
    big.insert(big.end(), n, 'z');
    big.push_back(0);
    CHECK(!DecodeSubmit(big, out));
}

TEST(feedback_reply_bytes_are_the_clients)
{
    Reply r;
    r.result = Result::Cooldown;
    r.waitSeconds = 300;
    r.id = 7;
    r.leftToday = 3;
    std::vector<uint8_t> const want = {1, 2, 44, 1, 0, 0, 7, 0, 0, 0, 3};
    CHECK(EncodeReply(r) == want);
    Reply back;
    CHECK(DecodeReply(want, back));
    CHECK(back.result == Result::Cooldown && back.waitSeconds == 300 && back.id == 7 && back.leftToday == 3);
    std::vector<uint8_t> bad = want;
    bad[1] = 7;
    CHECK(!DecodeReply(bad, back));
    bad = want;
    bad.pop_back();
    CHECK(!DecodeReply(bad, back));
}

TEST(feedback_kinds_name_the_tables_enum)
{
    CHECK(std::string(KindName(Kind::Bug)) == "bug");
    CHECK(std::string(KindName(Kind::Suggestion)) == "suggestion");
    CHECK(std::string(KindName(Kind::Other)) == "other");
    CHECK(!KnownKind(0) && KnownKind(1) && KnownKind(3) && !KnownKind(4));
}

// ── Text ──

TEST(feedback_a_line_loses_controls_and_extra_spaces)
{
    CHECK(CleanLine("  Hello   world  ", 80) == "Hello world");
    CHECK(CleanLine("tab\there\nand\r\nbreaks", 80) == "tab here and breaks");
    CHECK(CleanLine("bell\x07 esc\x1B[31m nul", 80) == "bell esc[31m nul");
    CHECK(CleanLine(std::string("a\0b", 3), 80) == "ab");
    CHECK(CleanLine("\x7F" "del", 80) == "del");
    CHECK(CleanLine("", 80).empty());
    CHECK(CleanLine("   \n\t ", 80).empty());
}

TEST(feedback_text_is_cut_by_characters_not_bytes)
{
    CHECK(CleanLine("abcdef", 3) == "abc");
    // é is two bytes, one character.
    std::string const e = "\xC3\xA9";
    CHECK(CleanLine(e + e + e + e, 3) == e + e + e);
    CHECK(CountChars(e + "a" + e) == 3);
    // A cut that ends in spaces is trimmed again.
    CHECK(CleanLine("ab   cd", 3) == "ab");
    CHECK(CleanText(std::string(2000, 'x'), kMaxBodyChars).size() == kMaxBodyChars);
}

TEST(feedback_text_keeps_lines_but_not_runs_of_blank_ones)
{
    CHECK(CleanText("line one  \r\nline two\rline three", 1000) == "line one\nline two\nline three");
    CHECK(CleanText("\n\n  para one\n\n\n\n\npara two\n\n", 1000) == "para one\n\npara two");
    // Indentation inside the text stays (a pasted stack, a list).
    CHECK(CleanText("steps:\n  1. jump\n  2. fall", 1000) == "steps:\n  1. jump\n  2. fall");
    CHECK(CleanText("a\tb", 1000) == "a b");
}

TEST(feedback_text_drops_bad_utf8_and_what_the_table_cannot_hold)
{
    // Stray continuation, a truncated sequence, an overlong '/', a surrogate, past U+10FFFF.
    CHECK(CleanLine("a\x80" "b", 80) == "ab");
    CHECK(CleanLine("a\xE2\x82", 80) == "a");
    CHECK(CleanLine("a\xC0\xAF" "b", 80) == "ab");
    CHECK(CleanLine("a\xED\xA0\x80" "b", 80) == "ab");
    CHECK(CleanLine("a\xF4\x90\x80\x80" "b", 80) == "ab");
    // An emoji (U+1F600) is past the BMP: a '?'.
    CHECK(CleanLine("hi \xF0\x9F\x98\x80", 80) == "hi ?");
    // Kept: accents, CJK, the euro sign.
    CHECK(CleanLine("caf\xC3\xA9 \xE6\x97\xA5 \xE2\x82\xAC", 80) == "caf\xC3\xA9 \xE6\x97\xA5 \xE2\x82\xAC");
}

TEST(feedback_text_drops_invisible_formatting)
{
    // RLO (U+202E), a zero-width space (U+200B), a BOM, a C1 control (U+0085).
    CHECK(CleanLine("a\xE2\x80\xAE" "b\xE2\x80\x8B" "c\xEF\xBB\xBF" "d\xC2\x85" "e", 80) == "abcde");
    // The line separator U+2028 is a break.
    CHECK(CleanText("a\xE2\x80\xA8" "b", 80) == "a\nb");
    CHECK(CleanLine("a\xE2\x80\xA8" "b", 80) == "a b");
}

TEST(feedback_text_reads_wows_escaped_bar)
{
    CHECK(CleanLine("a || b", 80) == "a | b");
    CHECK(CleanLine("|cffff0000red|r", 80) == "|cffff0000red|r");
    CHECK(EscapeChat("|cffff0000red|r") == "||cffff0000red||r");
    CHECK(EscapeChat("plain") == "plain");
}

TEST(feedback_quotes_and_sql_are_just_text)
{
    // Kept as typed: the game stores it with a prepared statement, aw-admin reads it as data.
    CHECK(CleanLine("Robert'); DROP TABLE aw_feedback;--", 80) == "Robert'); DROP TABLE aw_feedback;--");
    CHECK(CleanLine("\"quoted\" \\ back", 80) == "\"quoted\" \\ back");
}

// ── The limits ──

TEST(feedback_limits_the_first_report_is_free)
{
    Limits l;
    Verdict v = Check({}, 1000000, l);
    CHECK(v.result == Result::Ready && v.waitSeconds == 0 && v.leftToday == 5);
}

TEST(feedback_limits_one_per_cooldown)
{
    Limits l;   // 600 s, 5 a day
    uint64_t const t0 = 1000000;
    Verdict v = Check({t0}, t0 + 1, l);
    CHECK(v.result == Result::Cooldown && v.waitSeconds == 599 && v.leftToday == 4);
    v = Check({t0}, t0 + 599, l);
    CHECK(v.result == Result::Cooldown && v.waitSeconds == 1);
    v = Check({t0}, t0 + 600, l);
    CHECK(v.result == Result::Ready && v.waitSeconds == 0 && v.leftToday == 4);
    // Only the newest counts for the cooldown, whatever the order.
    v = Check({t0 + 300, t0}, t0 + 600, l);
    CHECK(v.result == Result::Cooldown && v.waitSeconds == 300);
}

TEST(feedback_limits_five_a_rolling_day)
{
    Limits l;
    uint64_t const t0 = 1000000;
    std::vector<uint64_t> sent = {t0, t0 + 1000, t0 + 2000, t0 + 3000, t0 + 4000};
    Verdict v = Check(sent, t0 + 5000, l);
    CHECK(v.result == Result::DailyLimit && v.leftToday == 0);
    // The oldest of the five frees its place a day after it was sent.
    CHECK(v.waitSeconds == kDaySeconds - 5000);
    v = Check(sent, t0 + kDaySeconds, l);
    CHECK(v.result == Result::Ready && v.leftToday == 1);
    // Four sent: one left, but the cooldown still holds.
    sent.pop_back();
    v = Check(sent, t0 + 3001, l);
    CHECK(v.result == Result::Cooldown && v.leftToday == 1 && v.waitSeconds == 599);
}

TEST(feedback_limits_the_longer_wait_wins)
{
    // Daily limit 2, cooldown an hour: both block; the daily wait (longer) is the answer.
    Limits l{3600, 2};
    uint64_t const t0 = 1000000;
    Verdict v = Check({t0, t0 + 3600}, t0 + 3601, l);
    CHECK(v.result == Result::DailyLimit && v.waitSeconds == kDaySeconds - 3601);
    // Cooldown 2 days (silly, but configurable): the cooldown is longer.
    Limits slow{2 * kDaySeconds, 2};
    v = Check({t0, t0 + 10}, t0 + 20, slow);
    CHECK(v.result == Result::Cooldown && v.waitSeconds == 2 * kDaySeconds - 10);
}

TEST(feedback_limits_zero_turns_a_limit_off)
{
    uint64_t const t0 = 1000000;
    std::vector<uint64_t> many(50, t0);
    Verdict v = Check(many, t0 + 1, Limits{0, 0});
    CHECK(v.result == Result::Ready && v.leftToday == kUnlimited);
    v = Check(many, t0 + 1, Limits{0, 5});
    CHECK(v.result == Result::DailyLimit && v.leftToday == 0);
    v = Check({t0}, t0 + 1, Limits{600, 0});
    CHECK(v.result == Result::Cooldown && v.leftToday == kUnlimited);
}

TEST(feedback_limits_a_clock_gone_back_counts_as_now)
{
    Limits l;
    uint64_t const now = 1000000;
    Verdict v = Check({now + 5000}, now, l);
    CHECK(v.result == Result::Cooldown && v.waitSeconds == 600);
}

TEST(feedback_limiter_keeps_accounts_apart_and_forgets_old_reports)
{
    Limiter lim;
    uint64_t const t0 = 1000000;
    lim.Record(1, t0, t0);
    CHECK(lim.Check(1, t0 + 10).result == Result::Cooldown);
    CHECK(lim.Check(2, t0 + 10).result == Result::Ready);
    for (int i = 1; i < 5; ++i)
        lim.Record(1, t0 + i * 1000, t0 + i * 1000);
    CHECK(lim.Check(1, t0 + 5000).result == Result::DailyLimit);
    // A day on, the first is forgotten: one free.
    Verdict v = lim.Check(1, t0 + kDaySeconds);
    CHECK(v.result == Result::Ready && v.leftToday == 1);
    // Recording prunes: a report loaded from more than a day ago keeps nothing.
    lim.Record(3, t0, t0 + 2 * kDaySeconds);
    CHECK(lim.Accounts() == 1);
    lim.SetLimits(Limits{0, 0});
    CHECK(lim.Check(1, t0 + 5000).result == Result::Ready);
}
