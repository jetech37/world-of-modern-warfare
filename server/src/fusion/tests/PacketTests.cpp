/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/AreaLoot.h"
#include "fusion/Packets.h"

#include <fstream>
#include <map>
#include <sstream>

using namespace Fusion;
using namespace Fusion::Packets;

namespace
{
    // vectors/packets.txt: "name hex" per line, '#' comments. The benilla fork's tests read the
    // same file, so both ends agree on every byte.
    std::map<std::string, std::string> const& Vectors()
    {
        static std::map<std::string, std::string> vectors = [] {
            std::map<std::string, std::string> v;
            std::ifstream in(std::string(FUSION_VECTORS_DIR) + "/packets.txt");
            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;
                std::istringstream s(line);
                std::string name, hex;
                s >> name >> hex;
                v[name] = hex;
            }
            return v;
        }();
        return vectors;
    }

    std::string Vector(char const* name)
    {
        auto it = Vectors().find(name);
        return it == Vectors().end() ? std::string("<missing>") : it->second;
    }
}

static ShotBatch SampleShots()
{
    ShotBatch b;
    Shot s;
    s.timeMs = 123456;
    s.weaponId = 7;
    s.flags = SHOT_ADS | SHOT_CLAIMS_HEAD;
    s.stance = Stance::Crouch;
    s.targetGuid = 0xF1A2B3;
    s.origin = {10.5f, -20.25f, 30.f};
    s.direction = {0.f, 1.f, 0.f};
    s.seed = 0xDEADBEEF;
    s.spreadDeg = 2.5f;
    b.shots.push_back(s);
    return b;
}

static State SampleState()
{
    State st;
    st.stance = Stance::Prone;
    st.gait = Gait::Walk;
    st.flags = STATE_AIMING | STATE_FFA_ON;
    st.heldWeaponId = 42;
    st.loadedAmmo = 5;
    return st;
}

static Events SampleEvents()
{
    Events ev;
    ev.events.push_back({EventKind::Hit, EVENT_HEADSHOT, 0x1234, 45, 7});
    ev.events.push_back({EventKind::Kill, EVENT_FATAL, 0x1234, 0, 7});
    return ev;
}

TEST(opcodes_follow_the_1_12_table)
{
    CHECK(CMSG_FUSION_SHOTS == 828);
    CHECK(CMSG_FUSION_LOADOUT == 831 && SMSG_FUSION_LOADOUT == 832);
    CHECK(CMSG_FUSION_CHANNEL == 833 && SMSG_FUSION_CHANNEL == 834);
    CHECK(kNumMsgTypes == 835);
    CHECK(kVersion == 1);           // the pinned client rejects any other (plan ruling P2-1)
}

TEST(packets_match_the_shared_vectors)
{
    CHECK(ToHex(Encode(SampleShots())) == Vector("shots_one"));
    CHECK(ToHex(Encode(ShotBatch{})) == Vector("shots_empty"));
    CHECK(ToHex(Encode(SampleState())) == Vector("state"));
    CHECK(ToHex(Encode(SampleEvents())) == Vector("events_hit_kill"));
}

TEST(packets_round_trip)
{
    ShotBatch b;
    CHECK(Decode(FromHex(Vector("shots_one")), b));
    CHECK(b.shots.size() == 1);
    if (b.shots.size() != 1)
        return;
    CHECK(b.shots[0].timeMs == 123456 && b.shots[0].weaponId == 7 && b.shots[0].stance == Stance::Crouch);
    CHECK(b.shots[0].targetGuid == 0xF1A2B3);
    CHECK_NEAR(b.shots[0].origin.y, -20.25, 1e-6);
    CHECK(b.shots[0].seed == 0xDEADBEEFu);
    CHECK_NEAR(b.shots[0].spreadDeg, 2.5, 1e-6);
    std::vector<uint8_t> old = FromHex(Vector("shots_one"));
    old[0] = 1;                         // the layout before the seed and spread
    CHECK(!Decode(old, b));

    State st;
    CHECK(Decode(FromHex(Vector("state")), st));
    CHECK(st.stance == Stance::Prone && st.gait == Gait::Walk && st.heldWeaponId == 42 && st.loadedAmmo == 5);

    Events ev;
    CHECK(Decode(FromHex(Vector("events_hit_kill")), ev));
    CHECK(ev.events.size() == 2 && ev.events.back().kind == EventKind::Kill && ev.events.front().amount == 45);
}

TEST(packets_reject_malformed_bodies)
{
    std::vector<uint8_t> body = Encode(SampleShots());
    ShotBatch b;
    std::vector<uint8_t> shortBody(body.begin(), body.end() - 1);
    CHECK(!Decode(shortBody, b));
    std::vector<uint8_t> longBody = body;
    longBody.push_back(0);
    CHECK(!Decode(longBody, b));
    std::vector<uint8_t> wrongVersion = body;
    wrongVersion[0] = 1;            // the old shot layout
    CHECK(!Decode(wrongVersion, b));
    std::vector<uint8_t> badStance = body;
    badStance[2 + 7] = 9;
    CHECK(!Decode(badStance, b));
    std::vector<uint8_t> tooMany = {kVersion, uint8_t(kMaxShotsPerBatch + 1)};
    CHECK(!Decode(tooMany, b));
    // A NaN direction.
    std::vector<uint8_t> nan = body;
    nan[2 + 28] = 0x00; nan[2 + 29] = 0x00; nan[2 + 30] = 0xc0; nan[2 + 31] = 0x7f;
    CHECK(!Decode(nan, b));

    State st;
    std::vector<uint8_t> badGait = Encode(SampleState());
    badGait[2] = 9;
    CHECK(!Decode(badGait, st));
}

TEST(state_flag_values)
{
    CHECK(STATE_MANTLE == 0x20);
    CHECK(STATE_LUNGE == 0x40);
    uint8_t const existing = STATE_AIMING | STATE_FFA_ON | STATE_FFA_OFF | STATE_RELOADING | STATE_REARM;
    CHECK((existing & (STATE_MANTLE | STATE_LUNGE)) == 0);
}

TEST(akimbo_and_shell_flag_values)
{
    // The left gun's rounds and magazine, a single shell: new bits beside the old ones.
    CHECK(SHOT_LEFT == 0x10 && STATE_RELOAD_ONE == 0x80 && EVENT_LEFT == 0x10);
    CHECK(((SHOT_ADS | SHOT_CLAIMS_HEAD | SHOT_AIRBORNE | SHOT_DETONATE) & SHOT_LEFT) == 0);
    CHECK(((EVENT_HEADSHOT | EVENT_FATAL | EVENT_ASSIST | EVENT_PROGRESS) & EVENT_LEFT) == 0);
    // The flag byte rides through as it is.
    State st;
    st.flags = STATE_RELOAD_ONE;
    State back;
    CHECK(Decode(Encode(st), back) && back.flags == STATE_RELOAD_ONE);
}

namespace
{
    ClassLoadout SampleClass()
    {
        ClassLoadout l;
        l.name = "Admin";
        l.primary = 2;
        l.primaryAtt1 = "acog";
        l.primaryCamo = 1;
        l.secondary = 30;
        l.streak1 = 1;
        l.streak2 = 5;
        l.streak3 = 7;
        return l;
    }
}

TEST(loadout_messages_match_the_shared_vectors)
{
    LoadoutRequest req;
    req.equipSlot = 0;
    req.classes.push_back({0, SampleClass()});
    CHECK(ToHex(Encode(req)) == Vector("loadout_request_one"));
    LoadoutReply rep;
    rep.status = LoadoutStatus::Applied;
    rep.error = LoadoutError::None;
    rep.errorSlot = kNoClass;
    rep.equippedSlot = 0;
    rep.classes.push_back({0, SampleClass()});
    CHECK(ToHex(Encode(rep)) == Vector("loadout_reply_applied"));
}

TEST(loadout_messages_round_trip)
{
    LoadoutRequest req;
    CHECK(Decode(FromHex(Vector("loadout_request_one")), req));
    CHECK(req.equipSlot == 0 && req.classes.size() == 1);
    CHECK(req.classes[0].first == 0 && req.classes[0].second == SampleClass());
    LoadoutReply rep;
    CHECK(Decode(FromHex(Vector("loadout_reply_applied")), rep));
    CHECK(rep.status == LoadoutStatus::Applied && rep.equippedSlot == 0 && rep.errorSlot == kNoClass);
    CHECK(rep.classes.size() == 1 && rep.classes[0].second == SampleClass());
}

TEST(a_kits_third_attachment_and_pro_bits_travel)
{
    // Loadout version 4: the primary's third attachment and the perk slots' Pro bits.
    ClassLoadout l = SampleClass();
    l.primaryAtt3 = "fmj";
    l.perkPro = 5;
    LoadoutRequest req;
    req.equipSlot = 0;
    req.classes.push_back({0, l});
    CHECK(ToHex(Encode(req)) == Vector("loadout_request_rails"));
    LoadoutRequest back;
    CHECK(Decode(FromHex(Vector("loadout_request_rails")), back));
    CHECK(back.classes.size() == 1 && back.classes[0].second == l);
    CHECK(back.classes[0].second.primaryAtt3 == "fmj" && back.classes[0].second.perkPro == 5);
}

TEST(a_kits_fourth_attachment_travels)
{
    // Loadout version 5: the primary's fourth attachment (Tinker's Rails three times), last.
    ClassLoadout l = SampleClass();
    l.primaryAtt3 = "fmj";
    l.perkPro = 5;
    l.primaryAtt4 = "xmags";
    LoadoutRequest req;
    req.equipSlot = 0;
    req.classes.push_back({0, l});
    CHECK(ToHex(Encode(req)) == Vector("loadout_request_four"));
    LoadoutRequest back;
    CHECK(Decode(FromHex(Vector("loadout_request_four")), back));
    CHECK(back.classes.size() == 1 && back.classes[0].second == l);
    CHECK(back.classes[0].second.primaryAtt4 == "xmags");
    // A version 4 body (no fourth attachment) is refused.
    std::vector<uint8_t> old = FromHex(Vector("loadout_request_rails"));
    old[0] = 4;
    old.pop_back();
    CHECK(!Decode(old, back));
}

TEST(a_malformed_loadout_is_refused)
{
    std::vector<uint8_t> body = FromHex(Vector("loadout_request_one"));
    LoadoutRequest req;
    std::vector<uint8_t> shortBody(body.begin(), body.end() - 1);
    CHECK(!Decode(shortBody, req));
    std::vector<uint8_t> extra = body;
    extra.push_back(0);
    CHECK(!Decode(extra, req));
    std::vector<uint8_t> badSlot = body;
    badSlot[3] = 5;                 // slots are 0-4
    CHECK(!Decode(badSlot, req));
    std::vector<uint8_t> tooMany = body;
    tooMany[2] = 6;                 // at most five classes
    CHECK(!Decode(tooMany, req));
    std::vector<uint8_t> longName = body;
    longName[4] = 25;               // a name over 24 bytes
    CHECK(!Decode(longName, req));
}

TEST(the_too_fast_error_round_trips)
{
    // The client (benilla-protocol) decodes 11 as TooFast: the two enums agree.
    CHECK(uint8_t(LoadoutError::TooFast) == 11);
    std::vector<uint8_t> body = FromHex(Vector("loadout_reply_applied"));
    body[1] = uint8_t(LoadoutStatus::Rejected);
    body[2] = uint8_t(LoadoutError::TooFast);
    LoadoutReply rep;
    CHECK(Decode(body, rep));
    CHECK(rep.error == LoadoutError::TooFast);
    // The kit errors that follow it agree too.
    CHECK(uint8_t(LoadoutError::NotAtKitStation) == 12 && uint8_t(LoadoutError::UnknownTalent) == 13
        && uint8_t(LoadoutError::UnknownStreak) == 14);
    body[2] = 13;
    CHECK(Decode(body, rep) && rep.error == LoadoutError::UnknownTalent);
    body[2] = 14;
    CHECK(Decode(body, rep) && rep.error == LoadoutError::UnknownStreak);
    // Version 4's: a perk in too many slots, a Pro not earned.
    CHECK(uint8_t(LoadoutError::PerkDoesNotStack) == 15 && uint8_t(LoadoutError::ProLocked) == 16);
    body[2] = 15;
    CHECK(Decode(body, rep) && rep.error == LoadoutError::PerkDoesNotStack);
    body[2] = 16;
    CHECK(Decode(body, rep) && rep.error == LoadoutError::ProLocked);
    body[2] = 17;                   // past the last error
    CHECK(!Decode(body, rep));
}

TEST(the_channel_round_trips)
{
    // The same bytes the client's benilla_protocol::fusion::encode_channel writes.
    ChannelMessage m;
    m.channel = kChannelBuild;
    m.body = {1, 2, 3};
    std::vector<uint8_t> const bytes = Encode(m);
    CHECK(bytes == std::vector<uint8_t>({kVersion, kChannelBuild, 3, 0, 1, 2, 3}));
    ChannelMessage back;
    CHECK(Decode(bytes, back) && back.channel == m.channel && back.body == m.body);
    CHECK(!Decode(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1), back));
}

static Fusion::AreaLoot::SourceMap SampleSourceMap()
{
    Fusion::AreaLoot::SourceMap m;
    m.lootGuid = 0xF130000C3500002Aull;
    m.sources = {{0xF130000C3500002Aull, 3125}, {0xF130000C3600002Bull, 3126}, {0xF130000C3700002Cull, 3127}};
    m.rows = {{0, 0}, {1, 0}, {2, 1}};
    return m;
}

TEST(the_area_loot_source_map_matches_the_vector_and_round_trips)
{
    using namespace Fusion::AreaLoot;
    std::vector<uint8_t> const bytes = Encode(SampleSourceMap());
    CHECK(ToHex(bytes) == Vector("area_loot_sources"));

    SourceMap back;
    CHECK(Decode(bytes, back));
    CHECK(back.lootGuid == 0xF130000C3500002Aull);
    CHECK(back.sources.size() == 3 && back.sources[1].guid == 0xF130000C3600002Bull && back.sources[2].entry == 3127);
    CHECK(back.rows == SampleSourceMap().rows);

    // Short, long, another op, a row naming no source: refused.
    CHECK(!Decode(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1), back));
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    CHECK(!Decode(longer, back));
    std::vector<uint8_t> other = bytes;
    other[0] = 35;
    CHECK(!Decode(other, back));
    std::vector<uint8_t> stray = bytes;
    stray.back() = 3;
    CHECK(!Decode(stray, back));

    // A row whose source index is out of range is not written.
    SourceMap odd = SampleSourceMap();
    odd.rows.push_back({5, 9});
    CHECK(Encode(odd) == bytes);
}

TEST(a_struck_event_round_trips_and_is_debounced_per_unit)
{
    Events ev;
    ev.events.push_back({EventKind::Struck, 0, 0xF130000000001234ull, 0, 0});
    Events back;
    CHECK(Decode(Encode(ev), back));
    CHECK(back.events.size() == 1 && back.events[0].kind == EventKind::Struck);
    CHECK(back.events[0].guid == 0xF130000000001234ull);
    // A kind past Struck is still refused.
    std::vector<uint8_t> bad = Encode(ev);
    bad[2] = uint8_t(EventKind::Struck) + 1;   // version, count, then the first kind
    CHECK(!Decode(bad, back));
    // Another unit at once; the same one only after half a second.
    CHECK(StruckDue(0, 0, 7, 1000));
    CHECK(StruckDue(7, 1000, 8, 1001));
    CHECK(!StruckDue(7, 1000, 7, 1200));
    CHECK(StruckDue(7, 1000, 7, 1500));
}
