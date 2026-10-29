/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * Every wire decoder fed what a broken or hostile client could send: random bytes of every
 * length, behind every first byte (the op), and every cut of a long message. A decoder must
 * return (true or false) without reading past the message, throwing or hanging, and must not
 * make more entries than the message has bytes (a count in the message is never trusted).
 */

#include "Test.h"
#include "fusion/Achievements.h"
#include "fusion/ActionSystems.h"
#include "fusion/AreaLoot.h"
#include "fusion/Bounties.h"
#include "fusion/Build.h"
#include "fusion/Classes.h"
#include "fusion/CompanionIndex.h"
#include "fusion/Companions.h"
#include "fusion/Construction.h"
#include "fusion/Downed.h"
#include "fusion/DruidForms.h"
#include "fusion/Feedback.h"
#include "fusion/Homestead.h"
#include "fusion/Kits.h"
#include "fusion/Locks.h"
#include "fusion/Match.h"
#include "fusion/Packets.h"
#include "fusion/Pickups.h"
#include "fusion/Portals.h"
#include "fusion/Prestige.h"
#include "fusion/Rounds.h"
#include "fusion/SkyStreaks.h"
#include "fusion/Spellcasting.h"
#include "fusion/Stats.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace Fusion;

namespace
{
    // A small fixed PRNG, so a failure repeats.
    struct Rng
    {
        uint64_t s = 0x9E3779B97F4A7C15ull;
        uint8_t Next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return uint8_t(s >> 24);
        }
    };

    // The messages a decoder is fed. `bytes` biases some of them toward 0xFF and 0x00, the
    // values that make counts and lengths largest and smallest.
    std::vector<std::vector<uint8_t>> Inputs()
    {
        std::vector<std::vector<uint8_t>> out;
        Rng rng;
        out.push_back({});
        for (int op = 0; op < 256; ++op)
        {
            for (size_t len : {size_t(1), size_t(2), size_t(3), size_t(4), size_t(5), size_t(6), size_t(8),
                     size_t(9), size_t(12), size_t(13), size_t(16), size_t(17), size_t(21), size_t(25),
                     size_t(33), size_t(40), size_t(64), size_t(100), size_t(300)})
            {
                for (int fill = 0; fill < 3; ++fill)
                {
                    std::vector<uint8_t> b(len);
                    for (uint8_t& x : b)
                        x = fill == 0 ? rng.Next() : fill == 1 ? 0xFF : uint8_t(rng.Next() & 0x03);
                    b[0] = uint8_t(op);
                    out.push_back(std::move(b));
                }
            }
        }
        // Every cut of a long random message, behind a version byte of 1 (most wires' first).
        std::vector<uint8_t> big(1100);
        for (uint8_t& x : big)
            x = rng.Next();
        big[0] = 1;
        for (size_t n = 0; n <= big.size(); n += 7)
            out.emplace_back(big.begin(), big.begin() + n);
        return out;
    }

    std::vector<std::vector<uint8_t>> const& All()
    {
        static std::vector<std::vector<uint8_t>> const inputs = Inputs();
        return inputs;
    }

    // Runs a decoder over every input; it gives back how many entries it made (0 if none), which
    // must never be more than the message's bytes.
    void Fuzz(char const* name, std::function<size_t(std::vector<uint8_t> const&)> const& decode)
    {
        size_t over = 0;
        for (std::vector<uint8_t> const& body : All())
        {
            size_t made = 0;
            try
            {
                made = decode(body);
            }
            catch (...)
            {
                FusionTest::Fail(__FILE__, __LINE__, std::string(name) + " threw");
                return;
            }
            if (made > body.size())
                ++over;
        }
        if (over)
            FusionTest::Fail(__FILE__, __LINE__, std::string(name) + " made more entries than the message has bytes");
    }
}

TEST(every_client_decoder_survives_hostile_bytes)
{
    // What the server decodes from clients.
    Fuzz("Packets::Decode(ShotBatch)", [](auto const& b) { Packets::ShotBatch o; Packets::Decode(b, o); return o.shots.size(); });
    Fuzz("Packets::Decode(State)", [](auto const& b) { Packets::State o; Packets::Decode(b, o); return size_t(0); });
    Fuzz("Packets::Decode(LoadoutRequest)", [](auto const& b) { Packets::LoadoutRequest o; Packets::Decode(b, o); return size_t(0); });
    Fuzz("Packets::Decode(ChannelMessage)", [](auto const& b) { Packets::ChannelMessage o; Packets::Decode(b, o); return o.body.size(); });
    Fuzz("ActionSystems::DecodeSystem", [](auto const& b) { (void)ActionSystems::DecodeSystem(b); return size_t(0); });
    Fuzz("ActionSystems::DecodeTool", [](auto const& b) { (void)ActionSystems::DecodeTool(b); return size_t(0); });
    Fuzz("ActionSystems::DecodeStrike", [](auto const& b) { (void)ActionSystems::DecodeStrike(b); return size_t(0); });
    Fuzz("Build::DecodePlace", [](auto const& b) { Build::PlaceRequest o; Build::DecodePlace(b, o); return size_t(0); });
    Fuzz("Build::DecodeRemove", [](auto const& b) { uint32_t id; Build::DecodeRemove(b, id); return size_t(0); });
    Fuzz("Build::DecodeToggle", [](auto const& b) { uint32_t id; Build::DecodeToggle(b, id); return size_t(0); });
    Fuzz("Construction::DecodeReinforce", [](auto const& b) { uint32_t id; Construction::DecodeReinforce(b, id); return size_t(0); });
    Fuzz("Homestead::DecodeAct", [](auto const& b) { Homestead::Action a; uint32_t arg; Homestead::DecodeAct(b, a, arg); return size_t(0); });
    Fuzz("Locks::DecodeSetLock", [](auto const& b) { Locks::SetLock o; Locks::DecodeSetLock(b, o); return size_t(0); });
    Fuzz("Locks::DecodeTryCode", [](auto const& b) { uint32_t p; std::string c; Locks::DecodeTryCode(b, p, c); return c.size(); });
    Fuzz("Portals::DecodeAsk", [](auto const& b) { uint32_t p; Portals::DecodeAsk(b, p); return size_t(0); });
    Fuzz("Portals::DecodeTravel", [](auto const& b) { uint32_t p; uint8_t d; Portals::DecodeTravel(b, p, d); return size_t(0); });
    Fuzz("DecodeUseLedger", [](auto const& b) { uint64_t g; DecodeUseLedger(b, g); return size_t(0); });
    Fuzz("Feedback::DecodeSubmit", [](auto const& b) { Feedback::Submission o; Feedback::DecodeSubmit(b, o); return o.title.size() + o.body.size() + o.version.size(); });
    Fuzz("AchievementWire::DecodeRecord", [](auto const& b) { std::vector<std::pair<Stat, uint32_t>> o; AchievementWire::DecodeRecord(b, o); return o.size(); });
    Fuzz("Sky::DecodeRocketReport", [](auto const& b) { (void)Sky::DecodeRocketReport(b); return size_t(0); });
    Fuzz("Bounty::DecodeRequest", [](auto const& b) { uint8_t op; uint32_t q; Bounty::DecodeRequest(b, op, q); return size_t(0); });
    Fuzz("Companions::DecodeRequest", [](auto const& b) { Companions::Request o; Companions::DecodeRequest(b, o); return size_t(0); });
    Fuzz("Companions::Index::DecodeRequest", [](auto const& b) { Companions::Index::Request o; Companions::Index::DecodeRequest(b, o); return size_t(0); });
    Fuzz("Downed::DecodeUse", [](auto const& b) { Downed::UseRequest o; Downed::DecodeUse(b, o); return size_t(0); });
    Fuzz("Prestige::DecodeClient", [](auto const& b) { Prestige::ClientMessage o; Prestige::DecodeClient(b, o); return size_t(0); });
    Fuzz("Pickups::DecodeTake", [](auto const& b) { uint32_t id; Pickups::DecodeTake(b, id); return size_t(0); });
    Fuzz("Rounds::DecodeLoaded", [](auto const& b) { Rounds::Loaded o; Rounds::DecodeLoaded(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeHandCast", [](auto const& b) { Spellcasting::HandCast o; Spellcasting::DecodeHandCast(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeHandAim", [](auto const& b) { Spellcasting::HandAim o; Spellcasting::DecodeHandAim(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeSprayAim", [](auto const& b) { Spellcasting::SprayAim o; Spellcasting::DecodeSprayAim(b, o); return size_t(0); });

    // What the client decodes from the server (the same code, kept honest the same way).
    Fuzz("Packets::Decode(Events)", [](auto const& b) { Packets::Events o; Packets::Decode(b, o); return o.events.size(); });
    Fuzz("Packets::Decode(LoadoutReply)", [](auto const& b) { Packets::LoadoutReply o; Packets::Decode(b, o); return size_t(0); });
    Fuzz("Bounty::DecodeBoard", [](auto const& b) { Bounty::Board o; Bounty::DecodeBoard(b, o); return o.rows.size(); });
    Fuzz("Bounty::DecodeResult", [](auto const& b) { bool ok; std::string t; Bounty::DecodeResult(b, ok, t); return t.size(); });
    Fuzz("AreaLoot::Decode", [](auto const& b) { AreaLoot::SourceMap o; AreaLoot::Decode(b, o); return o.sources.size() + o.rows.size(); });
    Fuzz("AchievementWire::DecodePage", [](auto const& b) { AchievementWire::Page o; AchievementWire::DecodePage(b, o); return size_t(0); });
    Fuzz("AchievementWire::DecodeEarned", [](auto const& b) { std::vector<uint32_t> o; AchievementWire::DecodeEarned(b, o); return o.size(); });
    Fuzz("AchievementWire::DecodeProgress", [](auto const& b) { std::vector<AchievementWire::ProgressEntry> o; AchievementWire::DecodeProgress(b, o); return o.size(); });
    Fuzz("Classes::DecodeState", [](auto const& b) { Classes::State o; Classes::DecodeState(b, o); return size_t(0); });
    Fuzz("Classes::DecodeMeters", [](auto const& b) { std::vector<Classes::Meter> o; Classes::DecodeMeters(b, o); return o.size(); });
    Fuzz("Classes::DecodeSpellTweaks", [](auto const& b) { std::vector<Classes::SpellTweak> o; Classes::DecodeSpellTweaks(b, o); return o.size(); });
    Fuzz("Classes::DecodePage", [](auto const& b) { Classes::Page o; Classes::DecodePage(b, o); return size_t(0); });
    Fuzz("Sky::DecodeNews", [](auto const& b) { auto const m = Sky::DecodeNews(b); return m ? m->name.size() + m->points.size() : size_t(0); });
    Fuzz("Companions::Index::DecodeIndex", [](auto const& b) { uint8_t f; std::vector<Companions::Index::Entry> o; Companions::Index::DecodeIndex(b, f, o); return o.size(); });
    Fuzz("Companions::Index::DecodeCarried", [](auto const& b) { uint8_t h; std::vector<Companions::Index::CarriedEntry> o; Companions::Index::DecodeCarried(b, h, o); return o.size(); });
    Fuzz("Companions::Index::DecodeNotice", [](auto const& b) { Companions::Index::NoticeKind k; std::string t; Companions::Index::DecodeNotice(b, k, t); return t.size(); });
    Fuzz("Companions::Index::DecodeLooks", [](auto const& b) { std::vector<Companions::Index::Looks> o; Companions::Index::DecodeLooks(b, o); return o.size(); });
    Fuzz("Companions::DecodeBar", [](auto const& b) { Companions::Bar o; Companions::DecodeBar(b, o); return size_t(0); });
    Fuzz("Companions::DecodeDuel", [](auto const& b) { Companions::DuelUpdate o; Companions::DecodeDuel(b, o); return size_t(0); });
    Fuzz("Companions::DecodeMoves", [](auto const& b) { Companions::Moves o; Companions::DecodeMoves(b, o); return size_t(0); });
    Fuzz("Construction::DecodeRecipes", [](auto const& b) { std::vector<Construction::Recipe> o; Construction::DecodeRecipes(b, o); return o.size(); });
    Fuzz("Downed::DecodeDowned", [](auto const& b) { Downed::DownedMsg o; Downed::DecodeDowned(b, o); return size_t(0); });
    Fuzz("Downed::DecodeUp", [](auto const& b) { Downed::UpMsg o; Downed::DecodeUp(b, o); return size_t(0); });
    Fuzz("Downed::DecodeDied", [](auto const& b) { Downed::DeathCause o; Downed::DecodeDied(b, o); return size_t(0); });
    Fuzz("Downed::DecodeKit", [](auto const& b) { Downed::KitMsg o; Downed::DecodeKit(b, o); return size_t(0); });
    Fuzz("Downed::DecodeNear", [](auto const& b) { uint64_t p; bool d; Downed::DecodeNear(b, p, d); return size_t(0); });
    Fuzz("DruidForms::DecodeForms", [](auto const& b) { std::vector<DruidForms::WireForm> o; DruidForms::DecodeForms(b, o); return o.size(); });
    Fuzz("Feedback::DecodeReply", [](auto const& b) { Feedback::Reply o; Feedback::DecodeReply(b, o); return size_t(0); });
    Fuzz("Homestead::DecodeWindow", [](auto const& b) { Homestead::Window o; Homestead::DecodeWindow(b, o); return size_t(0); });
    Fuzz("DecodeOpenKitEditor", [](auto const& b) { KitOpener o; uint16_t s; DecodeOpenKitEditor(b, o, s); return size_t(0); });
    Fuzz("DecodeKitStatus", [](auto const& b) { KitStatus o; DecodeKitStatus(b, o); return size_t(0); });
    Fuzz("Match::DecodeHud", [](auto const& b) { Match::Hud o; Match::DecodeHud(b, o); return size_t(0); });
    Fuzz("Match::DecodeScoreboard", [](auto const& b) { Match::Mode m; std::vector<Match::Row> o; Match::DecodeScoreboard(b, m, o); return o.size(); });
    Fuzz("Pickups::DecodeGuns", [](auto const& b) { std::vector<Pickups::Dropped> o; Pickups::DecodeGuns(b, o); return o.size(); });
    Fuzz("Pickups::DecodeHeld", [](auto const& b) { Pickups::Hands o; Pickups::DecodeHeld(b, o); return size_t(0); });
    Fuzz("Portals::DecodeMenu", [](auto const& b) { Portals::Menu o; Portals::DecodeMenu(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeBoltEvent", [](auto const& b) { Spellcasting::BoltEvent o; Spellcasting::DecodeBoltEvent(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeHandSpells", [](auto const& b) { std::vector<Spellcasting::HandSpell> o; Spellcasting::DecodeHandSpells(b, o); return o.size(); });
    Fuzz("Spellcasting::DecodeCastResult", [](auto const& b) { uint8_t h; Spellcasting::CastError e; uint32_t s; Spellcasting::DecodeCastResult(b, h, e, s); return size_t(0); });
    Fuzz("Spellcasting::DecodeZone", [](auto const& b) { Spellcasting::Zone o; Spellcasting::DecodeZone(b, o); return size_t(0); });
    Fuzz("Spellcasting::DecodeSprayEvent", [](auto const& b) { Spellcasting::SprayEvent o; Spellcasting::DecodeSprayEvent(b, o); return size_t(0); });
    Fuzz("Stats::DecodeHandling", [](auto const& b) { Stats::Derived o; Stats::DecodeHandling(b, o); return size_t(0); });
}
