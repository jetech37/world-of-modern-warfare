/*
 * World of Modern Warfare: the class framework (design/GAME_DESIGN.md section 2,
 * design/CLASS_CATALOG.md). GPL-2.0-or-later, see Common.h.
 *
 * Each of the nine classes has 3 core abilities (every build, level 1) and 18 options: 6 tiers at
 * levels 10..60, 3 per tier, one per archetype. An option is bought once at the class trainer and
 * then picked into a kit (one per tier); the character's class spells are exactly the core, the
 * class's Utility spells and its kit's picks. These rules hold the catalog and decide what may be
 * bought, picked and known; the game (game/Fusion/FusionClasses.cpp) learns and unlearns the
 * spells, sells the options and runs the custom abilities.
 */

#ifndef FUSION_CLASSES_H
#define FUSION_CLASSES_H

#include "fusion/Common.h"

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace Fusion::Classes
{
    // Vanilla class ids (ChrClasses.dbc).
    constexpr uint8_t kWarrior = 1;
    constexpr uint8_t kPaladin = 2;
    constexpr uint8_t kHunter = 3;
    constexpr uint8_t kRogue = 4;
    constexpr uint8_t kPriest = 5;
    constexpr uint8_t kShaman = 7;
    constexpr uint8_t kMage = 8;
    constexpr uint8_t kWarlock = 9;
    constexpr uint8_t kDruid = 11;

    constexpr uint8_t kTiers = 6;
    constexpr uint8_t kArchetypes = 3;
    constexpr uint8_t kCoreTier = 0;            // Option::tier of a core ability
    constexpr uint8_t kCoreArchetype = 0xFF;    // Option::archetype of a core ability

    // A kit's talent picks: one option id per tier (index 0 is tier 1, level 10), 0 for none.
    using Picks = std::array<uint16_t, kTiers>;

    // How an option is built (CLASS_CATALOG.md section 0).
    enum class Route : uint8_t
    {
        A,      // a vanilla spell, as is or tweaked
        B,      // a vanilla spell's name, icon and look; custom server logic
        C,      // fully custom; a donor spell for the look
    };

    enum class Kind : uint8_t
    {
        Ability,
        Passive,    // no button: the server applies it while it is picked
        Form,       // a druid form: its own spell plus the form's abilities
    };

    // One core ability or tier option.
    struct Option
    {
        uint16_t id = 0;
        uint8_t cls = 0;
        uint8_t tier = 0;               // 0 core, 1..6
        uint8_t archetype = 0;          // 0..2, kCoreArchetype for a core ability
        Route route = Route::A;
        Kind kind = Kind::Ability;
        bool capstone = false;
        char const* name = "";
        // The option's spell first (the highest rank of a ranked chain: the server teaches the
        // rank the character's level allows), then any spells that come with it (a form's
        // abilities). 0 ends the list.
        std::array<uint32_t, 4> spells{};
    };

    // The option id for a class, tier (1..6, or 0 for a core ability) and archetype (0..2; for
    // a core ability, its index 0..2): class * 100 + tier * 10 + archetype + 1. Warrior's tier 1
    // Berserker option is 111, a druid's tier 6 Starcaller option 1163, a mage's first core 801.
    constexpr uint16_t OptionId(uint8_t cls, uint8_t tier, uint8_t archetype)
    {
        return uint16_t(cls * 100 + tier * 10 + archetype + 1);
    }

    bool IsPlayableClass(uint8_t cls);
    // Every core ability and option of every class.
    std::vector<Option> const& Catalog();
    Option const* FindOption(uint16_t id);
    // A class's 18 options in tier order, then archetype.
    std::vector<Option const*> OptionsOf(uint8_t cls);
    // A tier's three options (tier 1..6), by archetype.
    std::array<Option const*, kArchetypes> TierOptions(uint8_t cls, uint8_t tier);
    // The class's three core abilities.
    std::array<Option const*, 3> CoreOf(uint8_t cls);
    // The class's Utility spells (out-of-combat spells outside the kit), as their highest rank.
    std::vector<uint32_t> const& UtilityOf(uint8_t cls);
    char const* ArchetypeName(uint8_t cls, uint8_t archetype);
    char const* ClassName(uint8_t cls);
    // The option of the class whose spells include `spell` (any spell id the catalog names, not
    // lower ranks: the game maps a rank to its chain's top first). Core abilities included.
    Option const* OptionBySpell(uint8_t cls, uint32_t spell);

    // Whether an option's spells go into the spellbook: abilities and forms always; passives of
    // route A (a vanilla passive aura) too; the custom passives of routes B and C never (their
    // donor is only an icon, and the server applies them from the kit).
    bool LearnsSpells(Option const& o);

    // The class's basic damaging spell (Smite, Wrath, Shadow Bolt, Fireball, Lightning Bolt,
    // Exorcism), as its highest rank: every kit of a caster has it from level 1, at the rank the
    // level allows like the stock spell, on the bar right after the core. It is no option (a tier
    // option may name the same spell: the option then adds its archetype's part). 0 for the
    // warrior, hunter and rogue.
    uint32_t BasicSpellOf(uint8_t cls);
    // Spells an earlier build taught as one of the class's options and no longer does (a donor
    // moved to another spell): the game takes them out of the spellbook.
    std::vector<uint32_t> const& RetiredSpells(uint8_t cls);
    // What a bar button holding a retired spell (its chain's top rank) gets instead: the option
    // that took its tier's place (the Bard's Hand Drum's button gets the Duelist's Sinister
    // Strike), 0 when nothing does (the button is cleared).
    uint32_t RetiredReplacement(uint8_t cls, uint32_t spell);
    // The retired spells' bar buttons moved on (2026-10-21: the Bard became the Duelist, whose
    // options keep the Bard's ids 413..463, so a kit's picks and the options bought carry over
    // as they are; only the spells changed). Each button (slot, spell) whose spell's top rank
    // (`topOf`) is retired gets its replacement when the character knows it (`known`), else it
    // is cleared (0). The moves, in the buttons' order; none once the bar is moved on, so it is
    // safe to run at every login.
    std::vector<std::pair<uint8_t, uint32_t>> MigrateRetiredButtons(uint8_t cls,
        std::vector<std::pair<uint8_t, uint32_t>> const& buttons, std::function<uint32_t(uint32_t)> const& topOf,
        std::function<bool(uint32_t)> const& known);

    // A tier's level (10..60; tier 0, the core, is 1) and price in copper (1/5/12/22/35/60 gold).
    uint8_t TierLevel(uint8_t tier);
    uint32_t TierPrice(uint8_t tier);
    // What a pure build costs (one option per tier), and every option of a class.
    uint32_t PureBuildPrice();

    // ── Buying at the trainer ──
    enum class BuyError : uint8_t
    {
        None,
        UnknownOption,
        WrongClass,
        Core,           // core abilities are free and automatic
        AlreadyLearned,
        TooLow,         // below the tier's level
        NoMoney,
        Unavailable,    // the audit marks it unusable for now
    };
    BuyError CanBuy(uint8_t cls, uint8_t level, uint64_t money, std::set<uint16_t> const& learned, uint16_t id);

    // ── Picking into a kit ──
    enum class PickError : uint8_t
    {
        None,
        UnknownOption,
        WrongClass,
        WrongTier,      // the option is not of the tier it was picked in
        NotLearned,
        TierLocked,     // the character is below the tier's level
        Unavailable,    // the audit marks it unusable for now
    };
    // One pick (tier 1..6; 0 is always fine). `admin`: a game master skips the learned, level and
    // availability gates (testing).
    PickError ValidatePick(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, uint8_t tier, uint16_t id,
        bool admin = false);
    // Every pick; the first refused one's tier (1..6) in `badTier`.
    PickError ValidatePicks(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, Picks const& picks,
        uint8_t* badTier = nullptr, bool admin = false);
    // The picks with every refused one cleared (a kit saved before a level or option was lost).
    Picks Sanitized(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, Picks const& picks, bool admin = false);
    // After buying `o`: put it in the kit's tier if that tier is empty (true if it did).
    bool AutoSlot(Picks& picks, Option const& o);

    // The kit's options in order: the three core abilities, then the picks (tier order).
    std::vector<Option const*> KitOptions(uint8_t cls, Picks const& picks);
    bool InKit(uint8_t cls, Picks const& picks, uint16_t id);

    // ── Ranks ──
    // One rank of a spell chain: its spell and the level it needs (spell_template.spellLevel).
    struct Rank
    {
        uint32_t spell = 0;
        uint8_t level = 0;
    };
    // The highest rank (by `level`, ranks ascending) the character's level allows; the first rank
    // when it allows none (an option bought at its tier always has one); 0 for an empty chain.
    uint32_t RankForLevel(std::vector<Rank> const& chain, uint8_t level);

    // ── One entry per ability (design/GAME_DESIGN.md section 2, "No ranks") ──
    // A class spell exists once, like a gun: the character knows one entry per chain, the rank its
    // level allows, and that entry grows into the next rank as the level reaches it (with the
    // spell's own per-level growth in between). The lower ranks stay learned under it but are no
    // entry of their own: vmangos's spellbook supersede (SpellMgr::GetSpellBookSuccessorSpellId)
    // keeps them out of the book and moves the bars onto the entry, and a stale reference (a bar,
    // a macro, a hand) is cast as the entry.
    //
    // Each rank's successor in the book (the last rank has none).
    std::vector<std::pair<uint32_t, uint32_t>> Successors(std::vector<Rank> const& chain);
    // Whether `spell` is one of the chain's ranks.
    bool InChain(std::vector<Rank> const& chain, uint32_t spell);
    // The entry a reference to `spell` (any rank of `chain`) is cast as: the highest rank the
    // character knows (`known`), 0 when `spell` is not of the chain or none is known.
    uint32_t SingleEntry(std::vector<Rank> const& chain, uint32_t spell, std::function<bool(uint32_t)> const& known);
    // A bar's spell buttons (slot, spell) moved onto their single entries at the level: each
    // button whose spell is a rank of one of `chains` other than the chain's rank for the level
    // (when the character knows that rank) is moved. The moves, in the buttons' order.
    std::vector<std::pair<uint8_t, uint32_t>> CollapseBar(std::vector<std::pair<uint8_t, uint32_t>> const& buttons,
        std::vector<std::vector<Rank>> const& chains, uint8_t level, std::function<bool(uint32_t)> const& known);

    // ── The action bar ──
    // The kit on the main bar's first page (action slots 0..11), which the hands mode shows: the
    // three core abilities in 0..2, the picks with a button in tier order from 3 (passives have
    // none), then the Utility spells in what is left (at most to slot 11). Spell ids are the
    // catalog's (the game swaps in the rank known); 0 is an empty slot.
    std::array<uint32_t, 12> BarLayout(uint8_t cls, Picks const& picks);

    // ── The old spellbook ──
    // What the game knows of a spell the character has, for KeepSpell.
    struct SpellFacts
    {
        bool classSkillLine = false;    // in a skill line of the character's class (SkillLine category 7)
        bool talent = false;            // a talent rank
        bool profession = false;        // a profession or secondary skill's spell
        bool racial = false;            // in a racial skill line
        bool proficiency = false;       // weapon, armor, defense, language, riding
        bool kit = false;               // one of the kit's, core's or Utility's chains
        bool revive = false;            // a resurrection kept for the downed state (W2)
    };
    // Whether a character keeps a spell: everything but the old class spellbook (class skill
    // lines and talents) outside its kit, core and Utility.
    bool KeepSpell(SpellFacts const& f);

    // ── Archetype resources (GAME_DESIGN.md section 2, "Class resources") ──
    // Only the three central to their archetype's fantasy are left; the client shows each as a
    // stacking buff (its icon, the count, a tooltip), not a bar. WoW's own resources (mana, rage,
    // energy, combo points) are untouched. The values are the wire's (op 2) and stay as an
    // earlier build numbered them; the twelve other resources it had are gone (Fury, Solar
    // Power, Axes, Crescendo, Shadow Orbs, Devotion, Foresight, Earth Charges, Spellweave,
    // Bladecharge, Fel Fury, Lunar and Solar): what they paid for is a cooldown, a chance or a
    // flat effect now (FusionClassAbilities.cpp).
    enum class Resource : uint8_t
    {
        None = 0,
        HolyPower = 2,      // Crusader, 3 (5 with Zealotry): strikes build it, finishers spend it
        Static = 9,         // Elementalist, 5 (8 with Overcharge): full, Lightning Bolt overloads
        LifeForce = 14,     // Necromancer, 100 (150 with Death's Harvest): kills fill it, raising spends it
        Momentum = 16,      // Duelist, 5 (8 with Relentless): blows build it, it ebbs when they stop
    };
    // Momentum (the Duelist, CLASS_CATALOG.md section 4): +1 a melee blow (Sinister Strike's
    // second blow 1 more), each point +5% to the Duelist's melee damage; 2 s without a blow and
    // one ebbs away each second after.
    constexpr uint16_t kMomentumMax = 5;
    constexpr uint16_t kMomentumRelentless = 8;
    constexpr float kMomentumDamagePerPoint = 0.05f;
    constexpr uint32_t kMomentumHoldMs = 2000;
    float MomentumMultiplier(uint16_t points);
    // What is left `idleMs` after the last blow: all of it within kMomentumHoldMs, then one a second less.
    uint16_t MomentumAfter(uint16_t points, uint32_t idleMs);
    char const* ResourceName(Resource r);
    // Every resource there is, in wire order.
    std::vector<Resource> AllResources();
    // What an option needs of its archetype's resource to be cast (its audit row's cost):
    // {None, 0} when it needs nothing.
    std::pair<Resource, uint16_t> CastCost(uint16_t option);
    // The archetype's resource; empty when it has none.
    std::vector<Resource> ArchetypeResources(uint8_t cls, uint8_t archetype);
    // Every resource the kit uses: those of each archetype it picks from (in that order, once).
    std::vector<Resource> KitResources(uint8_t cls, Picks const& picks);
    // A resource's cap with the kit (passives raise some).
    uint16_t ResourceMax(Resource r, uint8_t cls, Picks const& picks);

    struct Meter
    {
        Resource resource = Resource::None;
        uint16_t value = 0;
        uint16_t max = 0;
        // Add up to the cap; the amount actually added.
        uint16_t Add(uint16_t n);
        // Take `n` if there is that much (true), else nothing.
        bool Spend(uint16_t n);
        // Take everything; what there was.
        uint16_t SpendAll();
        bool Full() const { return max && value >= max; }
    };

    // Solar Power's day (Lightwarden): heals raise it; at the cap the sun is at Zenith for
    // kZenithSeconds (heals +50%), then it sets to 0.
    constexpr float kZenithSeconds = 8.f;
    constexpr float kZenithHealBonus = 0.5f;

    // ── The audit (design/CLASS_CATALOG.md section 15) ──
    enum class Status : uint8_t
    {
        Working,        // does what the catalog says (a note may tell a small difference)
        Partial,        // safe to use and does its core; the note says what is missing
        Unavailable,    // not built well enough to use: shown, never sold or picked
    };
    // How whatever an option starts ends: the way out of the state it puts its user in.
    enum class Ends : uint8_t
    {
        Instant,        // nothing stays on the caster (a strike, a heal, a jump, a dispel)
        Timed,          // an aura, a zone, a minion or an effect that runs out (durationMs)
        Toggle,         // on until pressed again or the aura cancelled (stealth, a song)
        Channel,        // a channel: moving or another cast ends it
        Passive,        // no button: applied while picked
        Form,           // a druid form: leaving it (its button again, or another form) ends it
    };
    struct Spec
    {
        uint16_t id;
        Status status;
        Ends ends;
        uint32_t durationMs;    // Timed: how long it lasts
        uint32_t cooldownMs;    // the button's cooldown (0: none, e.g. a charge-limited ability)
        Resource resource;      // the archetype resource it spends (None: nothing)
        uint16_t cost;
        uint8_t rage;           // the rage it costs
        char const* note;       // what differs from the catalog, or why it is unavailable
    };
    // Every option's audit row (core abilities included), in catalog order.
    std::vector<Spec> const& Specs();
    Spec const* SpecOf(uint16_t id);
    // Whether an option may be bought and picked (an unavailable one is never sold).
    bool Available(uint16_t id);
    char const* StatusName(Status s);

    // ── How the class spells are fitted to the kit system (the game's startup refit) ──
    enum RefitFlags : uint32_t
    {
        REFIT_IN_COMBAT = 0x0001,       // usable in combat (Charge, traps, Blink Pads)
        REFIT_NO_COMBO = 0x0002,        // a finisher without combo points (the server keeps some up)
        REFIT_KEEP_REAGENT = 0x0004,    // keeps its reagents (soul shards)
        REFIT_SELF = 0x0008,            // cast on oneself: the custom part places the effect
        REFIT_KEEP_COST = 0x0010,       // keeps its power cost (rage)
        REFIT_REPLACE = 0x0020,         // its vanilla effects are replaced by the custom part
        REFIT_REPLACE_FIRST = 0x0040,   // only its first effect is (Arugal's Curse's charm)
        REFIT_NO_WEAPON = 0x0080,       // needs no weapon or shield equipped
        REFIT_NOT_CHANNEL = 0x0100,     // a donor's channel is no channel (it would hold the caster)
        REFIT_ANY_TARGET = 0x0200,      // any creature type (Exorcism beyond undead and demons)
        REFIT_NO_AURA_STATE = 0x0400,   // no caster or target aura state (Judgement without a seal)
        REFIT_AT_FEET = 0x0800,         // a ground-targeted spell lands at the caster's feet (no cursor)
        REFIT_NO_DRAIN = 0x1000,        // no "use all power" (Lay on Hands)
        REFIT_REPLACE_SECOND = 0x2000,  // only its second effect is replaced
        REFIT_REPLACE_THIRD = 0x4000,   // only its third effect is (Death Wish's armor cut)
        REFIT_FREE = 0x8000,            // no power cost at all, rage included (the bear's abilities)
        REFIT_KEEP_STANCE = 0x10000,    // keeps the form it needs (Prowl in Cat Form)
        REFIT_NOT_NEXT_SWING = 0x20000, // a strike at once, not "on the next swing" (Maul)
        REFIT_ANY_SIDE = 0x40000,       // no "must be behind" of its own: the custom part decides (the Assassin's openers)
    };
    // The effects (bit i: effect i) of a refit spell the custom part replaces.
    uint8_t ReplaceMask(uint32_t flags);
    struct Refit
    {
        uint32_t spell;         // every rank of its chain
        int32_t cooldownMs;     // its own recovery (the category's is cleared); -1 keeps the spell's
        int32_t castIndex;      // SpellCastTimes.dbc: 1 instant, 4 1 s, 16 1.5 s, 5 2 s; -1 keeps
        int32_t rangeIndex;     // SpellRange.dbc; -1 keeps (REFIT_SELF makes it 1, self)
        uint32_t flags;
    };
    // Every class spell the kit system refits: the options', the basic spells', Rekindle's.
    std::vector<Refit> const& Refits();
    Refit const* RefitOf(uint32_t spell);

    // ── Bullet-blocking shapes (CLASS_CATALOG.md section 13, shared system 1) ──
    enum class BlockShape : uint8_t
    {
        Arc,    // held by a unit: bullets from inside its frontal cone (Bulwark)
        Wall,   // a standing screen: bullets crossing it from the front (Barrier, Warding Rune)
        Dome,   // a sphere: bullets fired from outside into it (Bastion of Light, Sanctum, Unbreakable)
    };
    struct Blocker
    {
        BlockShape shape = BlockShape::Dome;
        Vec3 center;                // Wall: the middle of its base; Dome: its centre; Arc: the holder
        float facing = 0.f;         // radians, the way the front looks (Wall, Arc)
        float radius = 0.f;         // Dome: radius; Wall: half its width
        float height = 0.f;         // Wall: height
        float halfAngle = 0.f;      // Arc: half the cone, radians
        uint64_t owner = 0;         // who raised it
        uint32_t untilMs = 0;
        float health = 0.f;         // Wall: what it can take before it breaks (0: unbreakable)
        bool followsOwner = false;  // its centre is its owner's place (a dome raised around one)
        bool broken = false;        // worn through: it stops nothing more
    };
    // Whether a round fired from `from` toward `to` is stopped by the shape (a hostile shooter;
    // the game skips the owner's allies). `at`: how far along the segment it is stopped (0..1).
    bool Blocks(Blocker const& b, Vec3 from, Vec3 to, float* at = nullptr);
    // A round of `damage` stopped by a breakable shape wears it down; true when that breaks it
    // (it is then gone: `broken`, its time up). An unbreakable one (health 0) never breaks.
    bool Wear(Blocker& b, float damage);
    // Arc: whether a shooter at `from` is inside the holder's frontal cone.
    bool InArc(Vec3 holder, float facing, float halfAngle, Vec3 from);

    // ── Vision blockers (smoke) ──
    struct Smoke
    {
        Vec3 center;
        float radius = 8.f;
        uint32_t untilMs = 0;
    };
    // Whether the segment a-b passes through the smoke (either end inside counts).
    bool SightBlocked(Smoke const& s, Vec3 a, Vec3 b);

    // ── The wire: SMSG/CMSG_FUSION_CHANNEL bodies on channel kChannelClasses, little-endian,
    // first byte the op ──
    constexpr uint8_t kChannelClasses = 7;
    // Server to client.
    constexpr uint8_t kOpState = 1;         // the class page: see State
    constexpr uint8_t kOpMeters = 2;        // [2, n u8, (resource u8, value u16, max u16) * n]
    constexpr uint8_t kOpSpotted = 3;       // [3, n u8, (guid u64, ms u16) * n]: outline these for ms
    constexpr uint8_t kOpPickResult = 4;    // [4, error u8, tier u8]
    constexpr uint8_t kOpSmoke = 5;         // [5, x f32, y f32, z f32, radius f32, ms u16]
    constexpr uint8_t kOpPage = 6;          // the class's catalog for the talent page: see Page
    constexpr uint8_t kOpSpellTweaks = 7;   // the class spells as the server fitted them: see SpellTweak
    // Client to server. (A kit's picks travel with the kit itself: ClassLoadout::talentPicks,
    // saved through the loadout message near a trainer or innkeeper, Kits.h.)
    constexpr uint8_t kOpAskState = 11;     // [11]

    enum StateFlags : uint8_t
    {
        STATE_CAN_EDIT = 0x01,      // near a trainer or innkeeper, the field editor open, a GM: kits may change
        STATE_ADMIN = 0x02,         // a GM: every option may be picked
    };
    // op 1: [1, class u8, level u8, kit u8, flags u8, picks u16 * 6, n u8, learned u16 * n]: the
    // kit in hand (its slot, 255 none) and its picks as applied, and the options bought.
    struct State
    {
        uint8_t cls = 0;
        uint8_t level = 0;
        uint8_t kit = 0;
        uint8_t flags = 0;
        Picks picks{};
        std::vector<uint16_t> learned;
    };
    std::vector<uint8_t> EncodeState(State const& s);
    bool DecodeState(std::vector<uint8_t> const& body, State& out);
    std::vector<uint8_t> EncodeMeters(std::vector<Meter> const& meters);
    bool DecodeMeters(std::vector<uint8_t> const& body, std::vector<Meter>& out);
    struct Spot
    {
        uint64_t guid = 0;
        uint16_t ms = 0;
    };
    std::vector<uint8_t> EncodeSpotted(std::vector<Spot> const& spots);
    // op 6: the class's core and options as the talent page shows them, so the client needs no
    // copy of the catalog: [6, class u8, 3 x (archetype name: len u8, bytes), n u8, n x (id u16,
    // tier u8, archetype u8, kind u8, capstone u8, spell u32, name: len u8, bytes)]. `spell` is
    // the one whose icon and tooltip stand for it (the game sends the rank the level allows).
    struct PageEntry
    {
        uint16_t id = 0;
        uint8_t tier = 0;
        uint8_t archetype = 0;
        Kind kind = Kind::Ability;
        bool capstone = false;
        uint32_t spell = 0;
        std::string name;
    };
    struct Page
    {
        uint8_t cls = 0;
        std::array<std::string, kArchetypes> archetypes;
        std::vector<PageEntry> entries;     // the core, then the options in tier order
    };
    Page MakePage(uint8_t cls);

    // op 7: the class spells as the server fitted them to the kit system (no stance, no reagent,
    // the catalog's cooldown, cast on oneself...), so the client's own Spell.dbc copy gates and
    // times them the same: [7, n u16, n x (spell u32, attributes u32, attributes_ex u32,
    // stances u32, recovery u32, category_recovery u32, cast_index u16, range_index u16,
    // flags u8)]. Sent at login, in pieces under the channel's size.
    enum SpellTweakFlags : uint8_t
    {
        TWEAK_NO_REAGENTS = 0x01,   // no reagents, no totems
        TWEAK_NO_COST = 0x02,       // no mana or energy cost
        TWEAK_ON_SELF = 0x04,       // cast on oneself (its effects' targets the caster)
        TWEAK_NO_ITEM = 0x08,       // needs no equipped weapon
        TWEAK_ANY_CREATURE = 0x10,  // no creature type limit (TargetCreatureType)
        TWEAK_NO_AURA_STATE = 0x20, // no caster or target aura state (Judgement without a seal)
        TWEAK_NO_FOCUS = 0x40,      // no spell focus object nearby
        TWEAK_NO_RANK = 0x80,       // one entry per ability: no "Rank N" under its name
    };
    struct SpellTweak
    {
        uint32_t spell = 0;
        uint32_t attributes = 0;
        uint32_t attributesEx = 0;
        uint32_t stances = 0;
        uint32_t recoveryMs = 0;
        uint32_t categoryRecoveryMs = 0;
        uint16_t castIndex = 0;
        uint16_t rangeIndex = 0;
        uint8_t flags = 0;
    };
    constexpr size_t kSpellTweakBytes = 29;
    // The tweaks in bodies no longer than `maxBody` bytes each.
    std::vector<std::vector<uint8_t>> EncodeSpellTweaks(std::vector<SpellTweak> const& tweaks, size_t maxBody);
    bool DecodeSpellTweaks(std::vector<uint8_t> const& body, std::vector<SpellTweak>& out);
    std::vector<uint8_t> EncodePage(Page const& p);
    bool DecodePage(std::vector<uint8_t> const& body, Page& out);
    std::vector<uint8_t> EncodePickResult(PickError e, uint8_t tier);
    std::vector<uint8_t> EncodeSmoke(Smoke const& s, uint16_t ms);
}

#endif
