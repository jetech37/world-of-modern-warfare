/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PRESTIGE_H
#define FUSION_PRESTIGE_H

#include "fusion/Achievements.h"
#include "fusion/Catalog.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Prestige (design doc section 14): the player chooses what to prestige. Three tracks, each with
// its own ranks (1 to 10) and rewards:
//
// - Level: at 60, back to level 1 and no experience. Kept: items (gear the new level cannot wear
//   goes to the bags), gold, reputation, professions, mounts, quests done, the kits. The game
//   gives a prestiged character more experience from kills on the way back up (KillXpMultiplier),
//   since the quests it has done stay done (doing them again would pay their gold and items twice).
// - Guns: with every gun and every attachment unlocked, the account's gun unlocks go back to the
//   starter kit: every earned achievement but the Feats is forgotten and the counters start again
//   (else they would earn everything back at once). Kept: Feats and their titles, and whatever
//   prestige itself unlocked (its camos stay on every gun, for when the gun is earned again).
//   Unlocks were always the account's, so this track is the account's too.
// - Profession: a profession at 300 goes back to 1. Kept: its recipes and its trained rank (the
//   most it can reach). Keeping recipes is not generous: crafting already needs the recipe's skill,
//   so they come back into use one by one as the skill climbs again.
//
// Ranks pay prestige tokens (the Prestige tab's token shop's currency), a title, an emblem in the
// achievements window's Prestige tab and, at some ranks, a cosmetic (a camo on every gun, a tracer
// colour). Milestones pay far more, for having all three maxed together (level 60, every gun and
// attachment unlocked, a profession at 300) with every track prestiged at least so many times.
namespace Fusion::Prestige
{
    enum class Track : uint8_t
    {
        Level = 1,
        Guns = 2,
        Profession = 3,
    };

    constexpr uint8_t kMaxRank = 10;
    constexpr uint32_t kMaxLevel = 60;
    constexpr uint16_t kMaxSkill = 300;
    constexpr uint16_t kSkillAfterPrestige = 1;

    // The professions that prestige: vanilla's primary and secondary skill lines.
    bool IsProfession(uint16_t skillLine);
    char const* ProfessionName(uint16_t skillLine);
    std::vector<uint16_t> const& Professions();

    // A tracer colour (a cosmetic bit, 1 << colour). 0 is MW2's own.
    enum class Tracer : uint8_t
    {
        Default = 0,
        Red = 1,
        Green = 2,
        Blue = 3,
        Violet = 4,
        Gold = 5,
        White = 6,
        Count,
    };
    char const* TracerName(Tracer t);
    // Linear sRGB-ish colour for the client's tracer material (0..255 each).
    struct Rgb { uint8_t r, g, b; };
    Rgb TracerColour(Tracer t);

    // Everything prestige keeps for a character (and its account's gun rank).
    struct State
    {
        uint8_t level = 0;                                  // the character's level rank
        uint8_t guns = 0;                                   // the account's gun rank
        std::map<uint16_t, uint8_t> professions;            // skill line -> rank
        uint32_t tokens = 0;                                // unspent prestige tokens
        uint32_t milestones = 0;                            // bit per milestone id claimed
        uint32_t accountMilestones = 0;                     // the account's claims (kAccountMilestones)
        uint32_t cosmetics = 0;                             // bit per Tracer owned
        uint16_t title = 0;                                 // the title worn, 0 none
        Tracer tracer = Tracer::Default;                    // the tracer colour worn

        uint8_t ProfessionRank(uint16_t line) const;
        uint8_t BestProfessionRank() const;
        bool HasTracer(Tracer t) const { return t == Tracer::Default || (cosmetics >> uint8_t(t)) & 1u; }
    };

    // Where the character stands now, for eligibility and the milestones.
    struct Standing
    {
        uint32_t level = 1;
        bool gunsMaxed = false;                             // GunsMaxed
        std::map<uint16_t, uint16_t> skills;                // known professions -> skill (no gear bonus)
    };

    // Every Weapons and Attachments achievement in the book earned: every gun and attachment
    // unlocked. (Camos, perks and the rest are not asked for; they come with play.)
    bool GunsMaxed(AchievementBook const& book, std::set<uint32_t> const& earned);
    // What the account keeps of its earned achievements after a gun prestige: the Feats.
    std::set<uint32_t> KeptAfterGunPrestige(AchievementBook const& book, std::set<uint32_t> const& earned);

    enum class Refusal : uint8_t
    {
        None = 0,
        NotMaxed = 1,           // not 60 / not everything unlocked / the profession not at 300
        MaxRank = 2,            // already rank 10
        NotAProfession = 3,
        Busy = 4,               // in combat, dead, in a battleground or a match (the game says)
        NothingAsked = 5,       // a confirm with no question pending, or a stale one
    };
    char const* RefusalText(Refusal r);

    // Whether the track may prestige now (`key` the skill line for a profession).
    Refusal CanPrestige(State const& s, Standing const& now, Track track, uint16_t key);

    // The rank a track stands at (`key` for a profession).
    uint8_t RankOf(State const& s, Track track, uint16_t key);

    // What reaching `rank` on a track pays.
    struct Reward
    {
        uint32_t tokens = 0;
        uint16_t title = 0;                                 // a title now held
        Tracer tracer = Tracer::Default;                    // a tracer colour now owned
        uint8_t camo = 0;                                   // a catalog camo now on every gun
    };
    Reward RankReward(Track track, uint16_t key, uint8_t rank);

    // Apply a prestige to the state: the rank up and its reward (tokens and cosmetics). The game
    // does the resetting itself (level, unlocks, skill). Returns the reward.
    Reward Apply(State& s, Track track, uint16_t key);

    // ── Titles: vanilla has none, so they are the fusion's (worn in chat and shown in the window) ──
    // Ids: level ranks 1-10, gun ranks 11-20, profession ranks 100 + index * 10 + rank - 1, the
    // milestones 200 + id.
    std::string TitleText(uint16_t id);
    // A title is worn before the name ("Sergeant Thrall") or after it ("Thrall the Gunsmith").
    bool TitleIsPrefix(uint16_t id);
    std::string Titled(std::string const& name, uint16_t id);
    // Every title the state holds, lowest id first.
    std::vector<uint16_t> TitlesHeld(State const& s);
    bool HoldsTitle(State const& s, uint16_t id);

    // Camos prestige puts on every gun that takes camos (the gun track's ranks).
    std::vector<uint8_t> PrestigeCamos(State const& s);
    // Grant them on top of the account's unlocks (every camo-taking gun in the catalog).
    void GrantPrestigeUnlocks(Catalog const& catalog, State const& s, UnlockSet& unlocks);

    // ── Milestones: everything maxed together ──
    struct Milestone
    {
        uint8_t id = 0;                                     // its bit in State::milestones
        uint8_t minRank = 0;                                // every track prestiged at least this often
        char const* name = "";
        uint16_t title = 0;
        uint32_t goldCopper = 0;
        uint32_t tokens = 0;
        uint32_t companionItem = 0;                         // an item_template entry (a W5 companion), 0 none
        Tracer tracer = Tracer::Default;
        uint8_t camo = 0;                                   // a camo on every gun
    };
    std::vector<Milestone> const& Milestones();
    Milestone const* FindMilestone(uint8_t id);
    // The milestones claimed once an account, not once a character: the Triple Threat (0) pays
    // 100g and needs no prestige, so every alt brought to the top claimed it again (security
    // review 2026-10, #22). The rest need prestige ranks, which are the character's own work.
    constexpr uint32_t kAccountMilestones = 1u << 0;
    // Level 60, every gun and attachment unlocked and a profession at 300, all at once.
    bool AllMaxed(Standing const& now);
    // The milestones reached and not yet claimed, lowest first.
    std::vector<uint8_t> MilestonesDue(State const& s, Standing const& now);
    // Mark a milestone claimed and pay its tokens and cosmetics into the state (the game pays the
    // gold and the companion item). False if unknown or already claimed.
    bool ClaimMilestone(State& s, uint8_t id);

    // The experience a prestiged character gets from a kill below level 60: +50% a level rank,
    // up to 3x, so the second climb skips the quests done the first time without dragging.
    float KillXpMultiplier(uint8_t levelRank, uint32_t level);

    // ── The token shop (the Prestige tab of the achievements window) ──
    enum class GoodKind : uint8_t
    {
        Item = 1,               // `item` x `count` into the bags
        TracerColour = 2,       // owns `tracer`
        Camo = 3,               // `camo` on every gun (kept as a cosmetic bit, 16 + camo)
    };
    struct Good
    {
        uint8_t id = 0;
        GoodKind kind = GoodKind::Item;
        char const* name = "";
        uint32_t price = 0;                                 // tokens
        uint32_t item = 0;
        uint8_t count = 1;
        Tracer tracer = Tracer::Default;
        uint8_t camo = 0;
    };
    std::vector<Good> const& Shop();
    Good const* FindGood(uint8_t id);
    // Whether the state already owns a cosmetic good (it is not sold twice).
    bool Owns(State const& s, Good const& g);
    enum class BuyResult : uint8_t { Ok, Unknown, NotEnoughTokens, AlreadyOwned };
    // Take the tokens and, for a cosmetic, mark it owned (the game hands over an item itself).
    BuyResult Buy(State& s, uint8_t goodId);
    // Camo bits bought in the shop live above the tracer bits.
    constexpr uint8_t kCamoCosmeticShift = 16;

    // ── The two-step confirm: the client asks, the server names what will be lost and a nonce,
    // the client confirms with the nonce within kConfirmMs. ──
    constexpr uint32_t kConfirmMs = 30000;
    struct Pending
    {
        Track track = Track::Level;
        uint16_t key = 0;
        uint32_t nonce = 0;
        uint32_t untilMs = 0;
        bool active = false;
    };
    // Start a question (replacing any other); `nonce` from the game's random numbers (never 0).
    void Ask(Pending& p, Track track, uint16_t key, uint32_t nonce, uint32_t nowMs);
    // The answer: true (and the question cleared) when the nonce matches and it has not expired.
    bool Confirm(Pending& p, uint32_t nonce, uint32_t nowMs, Track& track, uint16_t& key);
    // What the confirm dialog says a track's prestige takes away and keeps.
    std::string WhatItCosts(Track track, uint16_t key);

    // ── The wire: SMSG/CMSG_FUSION_CHANNEL on kChannelPrestige, little-endian ──
    constexpr uint8_t kChannelPrestige = 8;
    constexpr uint8_t kVersion = 1;
    enum class Op : uint8_t
    {
        // Client -> server.
        Request = 1,            // send me the state (the Prestige tab opened)
        Ask = 2,                // track u8 · key u16: I want to prestige this
        Confirm = 3,            // nonce u32: yes, really
        SetTitle = 4,           // title u16 (0 none)
        SetTracer = 5,          // tracer u8
        Cancel = 6,             // drop the question
        Buy = 7,                // good u8: buy it from the token shop
        // Server -> client.
        StateMsg = 1,           // the state (EncodeState)
        Prompt = 2,             // nonce u32 · track u8 · key u16 · text str16
        Result = 3,             // ok u8 · text str16
        Equipment = 4,          // slot u8 (0 lethal, 1 tactical) · count u8: a field gear item used
        Stim = 5,               // kind u8 · ms u32: an alchemist's stim took (FieldGear.h)
        Look = 6,               // tracer r g b: the tracer colour worn
        ShopList = 7,           // n u8 · n x (good u8 · price u32 · owned u8 · name str8): the token shop
    };

    // What the Prestige tab shows; built by the game from the state and the standing.
    struct View
    {
        State state;
        Standing standing;
        bool busy = false;                                  // in combat, dead, in a match: no prestige now
    };
    std::vector<uint8_t> EncodeState(View const& v);
    std::vector<uint8_t> EncodePrompt(uint32_t nonce, Track track, uint16_t key, std::string const& text);
    std::vector<uint8_t> EncodeResult(bool ok, std::string const& text);
    std::vector<uint8_t> EncodeEquipment(uint8_t slot, uint8_t count);
    std::vector<uint8_t> EncodeStim(uint8_t kind, uint32_t ms);
    std::vector<uint8_t> EncodeLook(Tracer t);
    // The token shop as this state sees it (what it owns is not sold twice).
    std::vector<uint8_t> EncodeShop(State const& s);
    // What a purchase answers, for the tab's result line.
    std::string BuyText(BuyResult r, Good const& g, uint32_t tokensLeft);

    // A client message, decoded.
    struct ClientMessage
    {
        Op op = Op::Request;
        Track track = Track::Level;
        uint16_t key = 0;
        uint32_t nonce = 0;
        uint16_t title = 0;
        Tracer tracer = Tracer::Default;
        uint8_t good = 0;
    };
    bool DecodeClient(std::vector<uint8_t> const& body, ClientMessage& out);
    std::vector<uint8_t> EncodeClient(ClientMessage const& m);
}

#endif
