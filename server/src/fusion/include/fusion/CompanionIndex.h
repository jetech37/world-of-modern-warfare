/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_INDEX_H
#define FUSION_COMPANION_INDEX_H

#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// The Companions window (the client's crate::companion_ui): the Bestiary, the companions a
// player carries, and what may be done from it. It opens from the HUD (anyone, anywhere, the
// limited view) or at any stable master (the full view: breeding and changing an ability).
//
//  - The index lists every species by family. A player sees an entry once they have seen a
//    creature of it (targeted one: a stage's creature, wild or someone's companion) or carried
//    one (captured); the rest are a family's unknown rows. The server sends a full entry only for
//    what the reader may see, so a client cannot read what it was not shown.
//  - A game master sees the whole index and, from the window, puts any species in their bags at
//    any stage and level, and changes any carried companion's level, stage, abilities and traits
//    (the Companion Keeper's tools, CompanionKeeper.h, now that the keeper is gone). Players never
//    buy companions: they drop, and they are bred.
//
// The seen set is kept per character (characters.character_fusion_companion_seen, made by the
// server) and sent with the index.
namespace Fusion::Companions::Index
{
    // How far a character has come with a species.
    enum class Seen : uint8_t
    {
        Unknown = 0,
        Seen = 1,       // targeted one
        Captured = 2,   // carried one
    };
    using SeenMap = std::map<uint16_t, Seen>;

    // Record a sighting or a capture; true when it moved the species up (a new row, or seen to
    // captured). Never moves one down.
    bool Record(SeenMap& seen, uint16_t species, Seen how);
    Seen Of(SeenMap const& seen, uint16_t species);
    // Whether the reader sees the species' entry in full: a game master all of them, anyone else
    // those seen or captured.
    bool Visible(SeenMap const& seen, uint16_t species, bool gm);

    // Creature entry -> the species whose stages show it (a few share a model's creature).
    using CreatureMap = std::unordered_map<uint32_t, std::vector<uint16_t>>;
    CreatureMap MapCreatures(std::vector<CompanionSpecies> const& all);

    // ── What may be done, and where ─────────────────────────────────────────────────────────
    // The window's opener (the client's panels::Access).
    enum class Access : uint8_t
    {
        Hud = 0,
        Npc = 1,    // at a stable master
    };

    enum class Action : uint8_t
    {
        Summon = 0,     // summon or put away a carried companion
        Evolve = 1,
        Pick = 2,       // an owed ability choice (the first is free, anywhere)
        Repick = 3,     // change a stage's ability (CompanionKeeper.h's PickPrice)
        Breed = 4,
        Duel = 5,       // challenge the target's companion
        GmGive = 6,     // any species into the bags
        GmEdit = 7,     // level, stage, abilities, traits of a carried one
        StableCare = 8, // revive the fainted, heal the wounded, buy a Reviving Draught
        Draught = 9,    // give a fainted companion a Reviving Draught
    };
    // Summon, Evolve, Pick, Duel, Draught: anyone, anywhere. Repick, Breed and StableCare: at a
    // stable master (an admin anywhere). GmGive and GmEdit: admins only.
    bool Allowed(Action a, Access at, bool gm);

    // ── Riding ──────────────────────────────────────────────────────────────────────────────
    // The display a ridden companion shows (UNIT_FIELD_MOUNTDISPLAYID), never the mount spell's
    // own: the lines without a vanilla mount ride on the wolf's spells (580 / 23250), whose aura
    // would show a wolf. 0 when its stage is not ridden. The companion's own display as it walked
    // beside its owner (`petDisplay`: the server passes LookDisplay's, so a hybrid rides as it
    // walks) when known, else the stage's own display (the look table: a mount line's last form is
    // a mount model), else the stage's ride display, else the stage creature's (`creatureDisplay`).
    uint32_t MountDisplay(CompanionSpecies const& sp, CompanionState const& s, uint32_t petDisplay,
        uint32_t creatureDisplay);

    // ── The wire: channel 4 (kChannelCompanion), little-endian, first byte the op ───────────
    // Server to client.
    constexpr uint8_t kOpIndex = 30;    // index entries (a page of them)
    constexpr uint8_t kOpCarried = 31;  // the companions in the bags
    constexpr uint8_t kOpNotice = 32;   // a line for the UI error frame, instead of the chat
    constexpr uint8_t kOpOpen = 33;     // open the window (a stable master was spoken to)
    // Client to server.
    constexpr uint8_t kOpIndexRequest = 20;     // [20]: the index and the carried list
    constexpr uint8_t kOpCarriedRequest = 21;   // [21]: the carried list
    constexpr uint8_t kOpGmGive = 22;           // [22, species u16, stage u8, level u8]
    constexpr uint8_t kOpGmEdit = 23;           // [23, item u64, what u8, a u8, b u8]
    constexpr uint8_t kOpBreed = 24;            // [24, item a u64, item b u64]
    constexpr uint8_t kOpRepick = 25;           // [25, item u64, stage u8, option u8]
    constexpr uint8_t kOpStableRevive = 26;     // [26]: revive and heal every companion in the bags
    constexpr uint8_t kOpUseDraught = 27;       // [27, item u64]: a Reviving Draught for it (0: the
                                                //   companion that fainted last)
    constexpr uint8_t kOpBuyDraught = 28;       // [28]: buy a Reviving Draught at the stable master

    // What a GM edit changes.
    enum class Edit : uint8_t
    {
        Level = 1,      // a: the level
        Stage = 2,      // a: the stage (0-based)
        Pick = 3,       // a: the stage, b: the option (1-2)
        Trait = 4,      // a: the trait id, toggled
        Twin = 5,       // a: the Twin-souled stage (1-3)
        ClearTraits = 6,
    };

    struct Request
    {
        uint8_t op = 0;
        uint64_t item = 0;
        uint64_t other = 0;     // breed: the second parent
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 0;
        Edit what = Edit::Level;
        uint8_t a = 0;
        uint8_t b = 0;
    };
    std::vector<uint8_t> EncodeRequest(Request const& r);
    // False for an op that is not the window's or a body of the wrong length.
    bool DecodeRequest(std::vector<uint8_t> const& body, Request& out);

    // One stage of an entry: its form, how it is reached, its two ability options.
    struct StageEntry
    {
        uint32_t creature = 0;
        std::string name;               // the form's name (its creature's)
        std::string reach;              // how it is reached ("Level 20, 1 Primal Beast Essence, at night")
        bool rideable = false;
        std::array<std::string, 2> abilities;
        std::array<uint8_t, 2> kinds{};     // AbilityKind
        std::array<uint8_t, 2> cooldowns{}; // seconds
    };

    // op 30 entry: an unknown one is its id and family only.
    struct Entry
    {
        uint16_t id = 0;
        Family family = Family::Critter;
        Seen seen = Seen::Unknown;      // Unknown: the rest is empty
        Role role = Role::Melee;
        uint32_t item = 0;
        std::string name;
        std::string trait;              // the family trait, always on
        std::string mounted;            // the mounted move, a rideable last stage
        uint32_t eligible = 0;          // the traits it may have (TraitBit mask)
        std::vector<StageEntry> stages;
    };

    // The stage's "how it is reached" line: "Starts here" for the first; else the rule's level,
    // catalyst (by `itemName`), places (by `placeNames`), hour and kills.
    std::string ReachText(CompanionSpecies const& sp, uint8_t stage, std::function<std::string(uint32_t)> const& itemName,
        std::function<std::string(uint32_t)> const& placeName);

    // A species' entry as the reader may see it (Visible): the family alone for one unseen.
    Entry MakeEntry(CompanionSpecies const& sp, SeenMap const& seen, bool gm,
        std::function<std::string(uint32_t)> const& creatureName, std::function<std::string(uint32_t)> const& itemName,
        std::function<std::string(uint32_t)> const& placeName);

    enum IndexFlags : uint8_t
    {
        INDEX_GM = 0x01,        // the reader is an admin
        INDEX_FIRST = 0x02,     // the first page: the client starts afresh
        INDEX_LAST = 0x04,
    };
    // op 30: [30, flags u8, count u8, entries...], each [id u16, family u8, seen u8] and, when
    // seen: role u8, item u32, name, trait, mounted (u8 length + bytes), eligible u32, stage
    // count u8, each stage: creature u32, name, reach, rideable u8, two x (name, kind u8,
    // cooldown u8).
    std::vector<uint8_t> EncodeIndex(uint8_t flags, std::vector<Entry> const& entries);
    bool DecodeIndex(std::vector<uint8_t> const& body, uint8_t& flags, std::vector<Entry>& out);
    // The pages for a whole index, at most `perPage` entries each, flagged first and last.
    std::vector<std::vector<uint8_t>> IndexPages(std::vector<Entry> const& entries, bool gm, size_t perPage = 12);

    enum CarriedFlags : uint8_t
    {
        CARRIED_OUT = 0x01,         // summoned
        CARRIED_RIDING = 0x02,
        CARRIED_CAN_EVOLVE = 0x04,
        CARRIED_FINAL = 0x08,
        CARRIED_RIDEABLE = 0x10,
        CARRIED_CAPPED = 0x20,      // held at its owner's level with a full bar (Bar's BAR_CAPPED)
        CARRIED_FAINTED = 0x40,     // fainted (CompanionRest.h): it cannot be summoned until revived
    };
    struct CarriedEntry
    {
        uint64_t item = 0;
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 1;
        uint32_t xp = 0;
        uint32_t xpToNext = 0;
        uint32_t traits = 0;
        uint8_t rarity = 0;
        uint8_t twinStage = 0;
        std::array<uint8_t, kMaxStages> picks{};
        int8_t pending = -1;            // the stage whose choice is owed
        uint8_t flags = 0;
        uint8_t block = 0;              // EvolveBlock
        uint8_t nextEvolveLevel = 0;
        uint8_t breedCount = 0;
        uint32_t breedRestSeconds = 0;  // until it may breed again
        uint8_t generation = 0;
        uint32_t faintSeconds = 0;      // fainted: until it comes round on its own
        uint8_t healthPct = 100;        // its health now (out) or when called (in its item)
        uint32_t seed = 0;              // its item's seed (ItemSeed), for the item tooltip's line
    };
    enum CarriedHeader : uint8_t
    {
        CARRIED_GM = 0x01,
        CARRIED_AT_STABLE = 0x02,       // the reader stands at a stable master
    };
    // What the window shows of reviving, after the entries.
    struct CarriedCare
    {
        uint8_t draughts = 0;           // Reviving Draughts in the bags (at most 255)
        uint32_t reviveCost = 0;        // a stable master's fee for every fainted one, copper
        uint8_t needsCare = 0;          // companions fainted or hurt (a stable master's work)
    };
    // op 31: [31, header u8, count u8, entries: item u64, species u16, stage u8, level u8,
    //         xp u32, xp to next u32, traits u32, rarity u8, twin u8, picks 3 x u8, pending i8,
    //         flags u8, block u8, next evolve level u8, breed count u8, rest u32, generation u8,
    //         faint seconds u32, health % u8, seed u32; then draughts u8, revive cost u32,
    //         needing care u8].
    std::vector<uint8_t> EncodeCarried(uint8_t header, std::vector<CarriedEntry> const& list,
        CarriedCare const& care = {});
    bool DecodeCarried(std::vector<uint8_t> const& body, uint8_t& header, std::vector<CarriedEntry>& out);
    bool DecodeCarried(std::vector<uint8_t> const& body, uint8_t& header, std::vector<CarriedEntry>& out,
        CarriedCare& care);

    enum class NoticeKind : uint8_t
    {
        Error = 0,      // red, as the game's own refusals
        Info = 1,       // yellow
        Good = 2,       // a level, an evolution, a young one
    };
    // op 32: [32, kind u8, text: u16 length + bytes].
    std::vector<uint8_t> EncodeNotice(NoticeKind kind, std::string const& text);
    bool DecodeNotice(std::vector<uint8_t> const& body, NoticeKind& kind, std::string& text);
    // op 33: [33, access u8].
    std::vector<uint8_t> EncodeOpen(Access at);

    // ── What each species looks like: the Bestiary's models and icons ──────────────────────
    // Sent with the index for what the reader may see (Visible), and again for an entry newly
    // seen. Its own op so a client that does not know it skips it.
    constexpr uint8_t kOpLooks = 34;    // server to client
    struct StageLook
    {
        uint32_t display = 0;               // the stage's display (its own, else its creature's)
        std::array<uint32_t, 2> spells{};   // its two ability options' vanilla spells (their icons)
    };
    struct Looks
    {
        uint16_t id = 0;
        std::vector<StageLook> stages;
        uint32_t basicSpell = 0;    // its basic attack's look (0: a plain melee swing)
        uint32_t mountedSpell = 0;  // a rideable last stage's mounted move
        uint32_t rideDisplay = 0;   // the display it is ridden with (MountDisplay), 0: not ridden
        std::string found;          // where it is found ("Drops from ...")
    };
    // "Drops from Young Stranglethorn Tiger, Stranglethorn Tiger and Elder Stranglethorn Tiger",
    // "Drops from any creature of the wolf family", "Drops from any dragonkin".
    std::string FoundText(CompanionSpecies const& sp, std::function<std::string(uint32_t)> const& creatureName);
    // `creatureDisplay`: a creature entry's display (0 unknown).
    Looks MakeLooks(CompanionSpecies const& sp, std::function<uint32_t(uint32_t)> const& creatureDisplay,
        std::function<std::string(uint32_t)> const& creatureName);
    // op 34: [34, count u8, each: id u16, stage count u8, each stage: display u32, two spells
    // u32; basic spell u32, mounted spell u32, ride display u32, found (u8 length + bytes)].
    std::vector<uint8_t> EncodeLooks(std::vector<Looks> const& looks);
    bool DecodeLooks(std::vector<uint8_t> const& body, std::vector<Looks>& out);
    // The pages for many, at most `perPage` each (well under the channel's 8 KiB).
    std::vector<std::vector<uint8_t>> LooksPages(std::vector<Looks> const& looks, size_t perPage = 24);
}

#endif
