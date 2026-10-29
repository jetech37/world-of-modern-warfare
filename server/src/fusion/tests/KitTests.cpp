/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Achievements.h"
#include "fusion/Ammo.h"
#include "fusion/Kits.h"
#include "fusion/Loadout.h"
#include "fusion/Packets.h"

using namespace Fusion;

TEST(every_trainer_and_innkeeper_is_a_kit_station)
{
    CHECK(IsKitStation(kNpcFlagTrainer));                               // class, profession, weapon master
    CHECK(IsKitStation(kNpcFlagInnkeeper | kNpcFlagGossip | 0x04));     // an innkeeper (and vendor)
    CHECK(IsKitStation(0x13));                                          // gossip, quests, trainer
    CHECK(!IsKitStation(0));
    CHECK(!IsKitStation(kNpcFlagGossip | 0x04 | 0x08 | 0x100));         // a vendor, flight master, banker
}

TEST(kits_change_only_in_reach_of_a_living_friendly_station)
{
    CHECK(KitStationAllows(kNpcFlagTrainer, true, false, 0.f));
    CHECK(KitStationAllows(kNpcFlagInnkeeper, true, false, kKitStationReach));
    CHECK(!KitStationAllows(kNpcFlagInnkeeper, true, false, kKitStationReach + 0.1f)); // out of reach
    CHECK(!KitStationAllows(kNpcFlagTrainer, false, false, 2.f));                       // dead
    CHECK(!KitStationAllows(kNpcFlagTrainer, true, true, 2.f));                         // the other side's
    CHECK(!KitStationAllows(kNpcFlagGossip, true, false, 2.f));                         // not a station
}

TEST(kits_change_only_at_a_station_the_field_editor_or_by_a_gm)
{
    CHECK(KitAccessOf(false, false, false) == KitAccess::View);
    CHECK(KitAccessOf(false, true, false) == KitAccess::Station);
    CHECK(KitAccessOf(false, false, true) == KitAccess::Field);
    // A station at hand spares the field editor (its cooldown is not spent).
    CHECK(KitAccessOf(false, true, true) == KitAccess::Station);
    CHECK(KitAccessOf(true, false, false) == KitAccess::Gm);
    CHECK(!CanChangeKits(KitAccess::View));
    CHECK(CanChangeKits(KitAccess::Station) && CanChangeKits(KitAccess::Field) && CanChangeKits(KitAccess::Gm));
}

TEST(saving_or_switching_kits_is_a_change_reequipping_the_kit_in_hand_is_not)
{
    CHECK(KitRequestChanges(true, kNoClass, 0));        // a save
    CHECK(KitRequestChanges(false, 1, 0));              // a switch
    CHECK(KitRequestChanges(false, 0, kNoClass));       // the first kit put in hand
    CHECK(!KitRequestChanges(false, 0, 0));             // the kit already in hand
    CHECK(!KitRequestChanges(false, kNoClass, 0));      // nothing at all
}

TEST(the_hud_views_and_a_station_or_the_field_editor_edits)
{
    CHECK(!OpenerMayEdit(KitOpener::Hud));
    CHECK(OpenerMayEdit(KitOpener::Station) && OpenerMayEdit(KitOpener::Field));
    CHECK(OpenKitEditor(KitOpener::Station) == std::vector<uint8_t>({kOpOpenClasses, 1}));
    CHECK(OpenKitEditor(KitOpener::Hud) == std::vector<uint8_t>({kOpOpenClasses, 0}));
    CHECK(OpenKitEditor(KitOpener::Field, 120) == std::vector<uint8_t>({kOpOpenClasses, 2, 120, 0}));
    KitOpener o = KitOpener::Hud;
    uint16_t secs = 0;
    CHECK(DecodeOpenKitEditor(OpenKitEditor(KitOpener::Field, 300), o, secs) && o == KitOpener::Field && secs == 300);
    CHECK(DecodeOpenKitEditor(OpenKitEditor(KitOpener::Station), o, secs) && o == KitOpener::Station && secs == 0);
    CHECK(!DecodeOpenKitEditor({kOpOpenClasses, 3}, o, secs));
    CHECK(!DecodeOpenKitEditor({kOpOpenClasses, 2}, o, secs));     // the field editor's seconds missing
    // Where the player stands decides each request, not who opened the window: the HUD's view
    // grants nothing by itself, and a station's window walked away from is refused.
    CHECK(KitAccessOf(false, false, false) == KitAccess::View);
}

TEST(the_field_editor_opens_a_window_and_its_cooldown_starts_with_the_first_change)
{
    FieldEditor f;
    uint64_t const t = 1'000'000;
    CHECK(!FieldEditorOpen(f, t));
    CHECK(UseFieldEditor(f, t) == FieldEditorUse::Opened);
    CHECK(FieldEditorOpen(f, t + kFieldEditorWindowSeconds - 1));
    CHECK(!FieldEditorOpen(f, t + kFieldEditorWindowSeconds));
    CHECK(FieldEditorWindowLeft(f, t + 20) == kFieldEditorWindowSeconds - 20);
    // Only looking costs nothing; asking again while open keeps the same window.
    CHECK(UseFieldEditor(f, t + 10) == FieldEditorUse::Opened);
    CHECK(FieldEditorSecondsLeft(f, t + 10) == 0);
    CHECK(FieldEditorWindowLeft(f, t + 10) == kFieldEditorWindowSeconds - 10);
    // The first change starts the hour; more changes in the same window don't restart it.
    CHECK(SpendFieldEditor(f, t + 20));
    CHECK(!SpendFieldEditor(f, t + 30));
    CHECK(FieldEditorSecondsLeft(f, t + 20) == kFieldEditorCooldownSeconds);
    CHECK(FieldEditorOpen(f, t + 40));
    CHECK(UseFieldEditor(f, t + 200) == FieldEditorUse::OnCooldown);
    CHECK(!FieldEditorOpen(f, t + 200));
    CHECK(FieldEditorSecondsLeft(f, t + 20 + kFieldEditorCooldownSeconds - 1) == 1);
    CHECK(UseFieldEditor(f, t + 20 + kFieldEditorCooldownSeconds) == FieldEditorUse::Opened);
    // A window left unused lapses without a cooldown.
    FieldEditor g;
    UseFieldEditor(g, t);
    CHECK(UseFieldEditor(g, t + kFieldEditorWindowSeconds + 5) == FieldEditorUse::Opened);
    CHECK(FieldEditorSecondsLeft(g, t + kFieldEditorWindowSeconds + 5) == 0);
}

TEST(the_field_editor_cooldown_survives_a_relog)
{
    // Saved as the time it is ready again; a fresh login only knows that.
    FieldEditor before;
    uint64_t const t = 5'000'000;
    UseFieldEditor(before, t);
    SpendFieldEditor(before, t);
    FieldEditor after;
    after.readyAt = before.readyAt;
    CHECK(UseFieldEditor(after, t + 600) == FieldEditorUse::OnCooldown);
    CHECK(FieldEditorSecondsLeft(after, t + 600) == kFieldEditorCooldownSeconds - 600);
}

TEST(the_kit_status_says_where_kits_change_and_the_field_editors_clock)
{
    FieldEditor f;
    uint64_t const t = 2'000'000;
    KitStatus s = KitStatusOf(KitAccess::View, f, t);
    CHECK(s.flags == 0 && s.fieldReadyIn == 0 && s.fieldOpenFor == 0);
    CHECK(KitStatusOf(KitAccess::Station, f, t).flags == kKitStatusCanEdit);
    CHECK(KitStatusOf(KitAccess::Gm, f, t).flags == (kKitStatusCanEdit | kKitStatusGm));
    UseFieldEditor(f, t);
    s = KitStatusOf(KitAccess::Field, f, t + 30);
    CHECK(s.flags == (kKitStatusCanEdit | kKitStatusFieldOpen) && s.fieldOpenFor == kFieldEditorWindowSeconds - 30);
    SpendFieldEditor(f, t + 30);
    s = KitStatusOf(KitAccess::Field, f, t + 40);
    CHECK(s.fieldReadyIn == kFieldEditorCooldownSeconds - 10);
    KitStatus back;
    CHECK(DecodeKitStatus(EncodeKitStatus(s), back) && back == s);
    CHECK(EncodeKitStatus(s).size() == 8 && EncodeKitStatus(s)[0] == kOpKitStatus);
    CHECK(!DecodeKitStatus({kOpKitStatus, 1}, back));
}

TEST(the_ledger_request_carries_the_item_guid)
{
    std::vector<uint8_t> const body = {kOpUseLedger, 0x02, 0x01, 0, 0, 0, 0, 0, 0x40};
    uint64_t guid = 0;
    CHECK(DecodeUseLedger(body, guid) && guid == 0x4000000000000102ull);
    CHECK(!DecodeUseLedger({kOpUseLedger, 1, 2}, guid));
    CHECK(!DecodeUseLedger({uint8_t(kOpUseLedger + 1), 0, 0, 0, 0, 0, 0, 0, 0}, guid));
    CHECK(kOpFieldEdit != kOpUseLedger && kOpAskKitStatus != kOpFieldEdit);
}

TEST(a_kit_plays_the_killstreaks_it_chose_or_the_whole_ladder)
{
    std::vector<StreakReward> const ladder = DefaultStreakLadder();
    UnlockSet const all = UnlockSet::Everything();
    // No kit, or a kit that chose none: every unlocked reward.
    CHECK(KitStreaks(nullptr, ladder, all).size() == ladder.size());
    ClassLoadout k;
    CHECK(KitStreaks(&k, ladder, all).size() == ladder.size());
    // Three chosen: those, in the ladder's order.
    k.streak1 = uint16_t(StreakId::HarrierStrike);
    k.streak2 = uint16_t(StreakId::Uav);
    k.streak3 = uint16_t(StreakId::TacticalNuke);
    std::vector<StreakReward> const mine = KitStreaks(&k, ladder, all);
    CHECK(mine.size() == 3);
    CHECK(mine[0].id == StreakId::Uav && mine[1].id == StreakId::HarrierStrike && mine[2].id == StreakId::TacticalNuke);
    // Only what the account has unlocked plays.
    UnlockSet some;
    some.Grant(Unlock::Killstreak(StreakId::Uav));
    std::vector<StreakReward> const held = KitStreaks(&k, ladder, some);
    CHECK(held.size() == 1 && held[0].id == StreakId::Uav);
}

TEST(a_kit_chooses_ladder_killstreaks_once_each)
{
    Catalog c;
    ClassLoadout k;
    k.streak1 = uint16_t(StreakId::Uav);
    k.streak2 = uint16_t(StreakId::SentryGun);
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::None);
    k.streak3 = uint16_t(StreakId::Uav);                     // twice
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::UnknownStreak);
    k.streak3 = uint16_t(StreakId::Ac130);                   // not on the ladder (not built)
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::UnknownStreak);
    k.streak3 = 0;
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::None);
}

TEST(a_kit_keeps_room_for_six_talent_picks)
{
    ClassLoadout k;
    CHECK(k.talentPicks.size() == kKitTalentTiers && kKitTalentTiers == 6);
    for (uint16_t p : k.talentPicks)
        CHECK(p == 0);
    ClassLoadout other = k;
    other.talentPicks[5] = 7;
    CHECK(!(other == k));
}

TEST(talent_picks_round_trip_on_the_wire)
{
    Packets::LoadoutReply rep;
    rep.status = Packets::LoadoutStatus::Applied;
    rep.equippedSlot = 2;
    ClassLoadout k;
    k.name = "Sniper";
    k.talentPicks = {1, 0, 3, 0, 5, 65535};
    rep.classes.push_back({2, k});
    Packets::LoadoutReply back;
    CHECK(Packets::Decode(Packets::Encode(rep), back));
    CHECK(back.classes.size() == 1 && back.classes[0].second == k);
}

TEST(talent_picks_are_left_to_the_class_rules)
{
    // A kit's picks need the character's class, level and bought options (Classes.h
    // ValidatePicks, which the game runs beside this); the loadout check leaves them be.
    Catalog c;
    ClassLoadout k;
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::None);
    k.talentPicks[0] = 4;
    CHECK(ValidateLoadout(c, k, 60, false) == LoadoutError::None);
}

TEST(a_kit_station_rearms_for_the_quartermasters_fee)
{
    uint32_t const fee = QuartermasterFee(60);
    CHECK(PlanStationRearm(fee, fee) == StationRearm::Rounds);
    CHECK(PlanStationRearm(fee - 1, fee) == StationRearm::NoMoney);
    CHECK(PlanStationRearm(0, 0) == StationRearm::Rounds);
    std::string const line = RearmGossipText(4800);
    CHECK(line.find("Rearm") == 0 && line.find("48s") != std::string::npos);
    CHECK(RearmResultText(StationRearm::Rounds, 4800).find("topped up") != std::string::npos);
    CHECK(RearmResultText(StationRearm::NoMoney, 4800).find("more than you have") != std::string::npos);
}
