/*
 * World of Modern Warfare: Inscription and Jewelcrafting, the layer's two primary professions
 * (design/GAME_DESIGN.md section 12, "Inscription and Jewelcrafting"). GPL-2.0-or-later, see
 * Common.h.
 *
 * Both work as the stock trades and as Minstrelsy do: a trainer of each (creature kTrainer...)
 * trains vanilla's four tiers (skill 1 to 300), teaches recipes and sells the trade's basic
 * goods; the recipes are made at the trade window (the stock TradeSkillFrame the client mirrors)
 * with the skill-up rolled by the recipe's colour. Unlike Construction and Minstrelsy they are
 * PRIMARY professions: learning one takes one of the character's two primary profession slots
 * (vanilla's free profession points), and unlearning gives it back. The skill is kept in the
 * fusion's own skill table (`character_fusion_skill`, keys kSkillInscription 9003 and
 * kSkillJewelcrafting 9004, beside Construction's 9001 and Minstrelsy's 9002).
 *
 * Inscription mills herbs into pigments (five of one herb), grinds pigments into inks, and writes
 * vanilla's scrolls and the layer's GLYPHS. Jewelcrafting prospects ore into gems (five of one
 * ore), cuts them, and makes jewelry (rings and necklaces of vanilla's stats). Wrath's two trades
 * are the model, on 1.12's herbs, ores and gems.
 *
 * GLYPHS modify a kit (Kits.h): each kit has three Major and three Minor glyph slots, opened at
 * character levels 15/30/50 and 20/40/60. A glyph is inscribed into the kit carried at a kit
 * station (any trainer or innkeeper), the field editor or as a GM, the glyph item used up; a kit
 * holds a glyph once. Majors tweak the kit's handling, equipment, field tools or the class's own
 * abilities (class glyphs ask the WoW class), most with a small cost elsewhere; Minors are
 * conveniences. Nothing makes a gun hit harder (design section 1).
 *
 * GEMS are augments for a kit's guns, fitted as the attachments are (section 3): the primary has
 * two sockets and a meta socket, the secondary one; a jeweler (Jewelcrafting 275) sets a third in
 * their own primary. As attachments never overlap, a gun takes one gem of each kind (no two gems
 * on a gun tune the same handling); a sway gem needs a scope (a sniper, the ACOG or the Thermal);
 * the meta gem wakes only when the gun's other sockets hold gems of at least two colours (red,
 * yellow, blue; a prismatic gem is every colour). A gem counts only while its gun is in hand, and
 * only on the kit's own guns (never one picked up nor a killstreak's). Socketing a new gem
 * destroys the one in the socket (as Wrath's). Gems tune handling (Stats::Out) only.
 *
 * The game (game/Fusion/FusionCrafts.cpp) keeps the skills, recipes, glyphs and gems and applies
 * the Mods; world data in sql/custom/azeroth_warfare/35_inscription_jewelcrafting.sql.
 */

#ifndef FUSION_CRAFTS_H
#define FUSION_CRAFTS_H

#include "fusion/Common.h"
#include "fusion/Construction.h"
#include "fusion/Stats.h"
#include "fusion/Weapon.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fusion::Crafts
{
    using Construction::Color;
    using Construction::Inventory;
    using Construction::Material;
    using Construction::Thresholds;
    using Construction::Tier;
    using Construction::TrainResult;

    enum class Trade : uint8_t
    {
        None = 0,
        Inscription = 1,
        Jewelcrafting = 2,
    };
    constexpr uint8_t kTrades = 2;

    // Their keys in `character_fusion_skill` (Construction's table).
    constexpr uint16_t kSkillInscription = 9003;
    constexpr uint16_t kSkillJewelcrafting = 9004;
    uint16_t SkillKey(Trade t);
    Trade TradeOfSkill(uint16_t key);
    char const* TradeName(Trade t);

    // The trainers (35_inscription_jewelcrafting.sql).
    constexpr uint32_t kTrainerInscription = 94300;
    constexpr uint32_t kTrainerJewelcrafting = 94450;
    Trade TradeOfTrainer(uint32_t entry);

    // ── Tiers: vanilla's (Apprentice 75 at level 5 ... Artisan 300 at 35) ──
    struct TierDef
    {
        Tier tier;
        char const* name;
        uint16_t cap;
        uint16_t skillToLearn;
        uint8_t levelToLearn;
        uint32_t costCopper;
    };
    TierDef const& TierOf(Tier t);
    uint16_t SkillCap(Tier t);
    // One tier at a time, in order. A first tier also asks a free primary profession slot.
    // A game master skips the skill, level, money and slot.
    enum class TrainCheck : uint8_t { Ok, AlreadyKnown, NotNext, SkillTooLow, LevelTooLow, NotEnoughMoney, NoFreeSlot };
    TrainCheck CanTrainTier(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, uint32_t freeSlots, bool gm = false);
    // The tier that holds `skill` (a GM's .profession set).
    Tier TierHolding(uint16_t skill);

    // ── Items (item_template, 94300-94599) ──
    namespace Item
    {
        // Inscription: pigments, inks, parchment, glyphs (94330-94379).
        constexpr uint32_t AlabasterPigment = 94300, DuskyPigment = 94301, GoldenPigment = 94302,
            EmeraldPigment = 94303, VioletPigment = 94304, SilveryPigment = 94305;
        constexpr uint32_t MoonglowInk = 94310, MidnightInk = 94311, LionsInk = 94312, JadefireInk = 94313,
            CelestialInk = 94314, ShimmeringInk = 94315;
        constexpr uint32_t LightParchment = 94320, ResilientParchment = 94321;
        constexpr uint32_t kGlyphFirst = 94330, kGlyphLast = 94379;
        // Jewelcrafting: cut gems (94450-94479), wire and settings, jewelry.
        constexpr uint32_t kGemFirst = 94450, kGemLast = 94479;
        constexpr uint32_t CopperWire = 94480, BronzeSetting = 94481, MithrilFiligree = 94482, ThoriumSetting = 94483;
        constexpr uint32_t JewelersKit = 94484;     // the tool every cut asks (sold by the trainer)
        constexpr uint32_t kJewelryFirst = 94490, kJewelryLast = 94519;
    }

    // What a trainer sells in its window (the trade's basic goods).
    struct Ware
    {
        uint32_t item = 0;
        uint32_t price = 0;     // copper
        uint8_t count = 1;
    };
    std::vector<Ware> const& Wares(Trade t);

    // ── Recipes ──
    enum class Kind : uint8_t
    {
        Make = 0,       // reagents in, the product out
        Mill = 1,       // five of one herb: pigments (Inscription)
        Prospect = 2,   // five of one ore: gems (Jewelcrafting)
    };

    // What milling or prospecting may give: `chance` of 256 per draw, `min`..`max` of it.
    struct Yield
    {
        uint32_t item = 0;
        uint8_t chance = 0;
        uint8_t min = 1;
        uint8_t max = 1;
    };

    struct Recipe
    {
        uint16_t id = 0;
        Trade trade = Trade::None;
        Kind kind = Kind::Make;
        char const* name = "";
        char const* group = "";         // the trade window's header
        uint16_t required = 1;          // the skill it asks; its colours are Construction::Spaced's
        uint32_t learnCost = 0;         // copper at the trainer; 0 with `starter`
        bool starter = false;           // known with the profession (milling and prospecting too)
        uint32_t product = 0;
        uint8_t productCount = 1;
        std::vector<Material> reagents;
        uint32_t tool = 0;              // an item it asks for in the bags, not used up
        std::vector<Yield> yields;      // Mill and Prospect
    };
    std::vector<Recipe> const& Recipes();
    Recipe const* RecipeOf(uint16_t id);
    std::vector<Recipe const*> RecipesOf(Trade t);
    Thresholds SkillOf(Recipe const& r);
    // Known: a starter, or learned.
    bool Known(Recipe const& r, std::vector<uint16_t> const& learned);

    enum class LearnCheck : uint8_t { Ok, NotLearned, Known, SkillTooLow, NotEnoughMoney, WrongTrade };
    LearnCheck CanLearn(Recipe const& r, Trade at, Tier tier, uint16_t skill, bool known, uint32_t money, bool gm = false);
    enum class MakeCheck : uint8_t { Ok, NotLearned, Unknown, SkillTooLow, NoReagents, NoTool };
    MakeCheck CanMake(Recipe const& r, Tier tier, bool known, uint16_t skill, Inventory const& bags, bool gm = false);
    // The skill after making one (`roll` in [0, 1)), never past `cap`.
    uint16_t MakeSkillUp(Recipe const& r, uint16_t skill, uint16_t cap, float roll);

    // Milling and prospecting: each yield drawn once (`rolls` gives a byte a draw, then one for
    // its count), at least one draw paying (the first yield when every draw misses, as Wrath's
    // milling never comes up empty).
    struct Draw
    {
        uint32_t item = 0;
        uint32_t count = 0;
    };
    std::vector<Draw> Roll(Recipe const& r, std::vector<uint8_t> const& rolls);
    // How many bytes Roll reads (two a yield).
    size_t RollBytes(Recipe const& r);

    // ── Glyphs ──
    enum class GlyphKind : uint8_t { Major = 0, Minor = 1 };
    constexpr uint8_t kGlyphSlots = 3;          // of each kind
    // The character level each slot opens at.
    constexpr std::array<uint8_t, kGlyphSlots> kMajorLevels{15, 30, 50};
    constexpr std::array<uint8_t, kGlyphSlots> kMinorLevels{20, 40, 60};
    uint8_t SlotLevel(GlyphKind k, uint8_t slot);

    // A WoW class bit (1 << (class - 1)): 1 Warrior, 2 Paladin, 4 Hunter, 8 Rogue, 16 Priest,
    // 64 Shaman, 128 Mage, 256 Warlock, 1024 Druid. 0: every class.
    constexpr uint32_t ClassBit(uint8_t wowClass) { return wowClass ? 1u << (wowClass - 1) : 0u; }
    constexpr uint32_t kCasters = ClassBit(5) | ClassBit(7) | ClassBit(8) | ClassBit(9) | ClassBit(11);
    constexpr uint32_t kFighters = ClassBit(1) | ClassBit(2) | ClassBit(4);

    // What glyphs and gems change: handling (Stats::Out: a multiplier's share off, negative, or
    // a share's added, positive) and the classic-world hooks the perks use (Perks.h).
    struct Mods
    {
        std::array<float, size_t(Stats::Out::Count)> handling{};    // 0: unchanged
        float castTime = 1.f;           // spells' cast time (Sleight of Hand's hook)
        float melee = 1.f;              // the hands' melee (Stopping Power's hook)
        float detection = 1.f;          // creatures' notice distance (Ninja's hook)
        float stealthDetectYards = 0.f; // SitRep's hook
        float fallDamage = 1.f;
        float goldLoot = 1.f;
        float outOfCombatSpeed = 1.f;
        float killXp = 1.f;
        float fieldRearm = 1.f;         // the field rearm's cooldown (One Man Army's)
        uint32_t fieldEditorSeconds = 0;    // off the field editor's cooldown
        uint32_t lastStandMs = 0;       // more time down before bleeding out

        bool operator==(Mods const& o) const;
        bool operator!=(Mods const& o) const { return !(*this == o); }
    };
    // Two sets of mods together (multipliers multiply, shares and yards add).
    void Combine(Mods& into, Mods const& m);

    struct GlyphDef
    {
        uint32_t item = 0;
        GlyphKind kind = GlyphKind::Major;
        char const* name = "";
        char const* text = "";          // what it does, for the tooltip and the window
        uint32_t classMask = 0;         // 0: every class
        Mods mods;
    };
    std::vector<GlyphDef> const& Glyphs();
    GlyphDef const* GlyphOf(uint32_t item);

    // A kit's glyphs: item ids, 0 for an empty slot.
    struct KitGlyphs
    {
        std::array<uint32_t, kGlyphSlots> major{};
        std::array<uint32_t, kGlyphSlots> minor{};
        bool operator==(KitGlyphs const& o) const { return major == o.major && minor == o.minor; }
    };
    enum class GlyphCheck : uint8_t { Ok, NotAGlyph, WrongKind, SlotLocked, WrongClass, AlreadyInKit, BadSlot };
    // Whether `item` may go into the kit's slot (`slot` 0..2 of `kind`) for a character of
    // `level` and WoW class `wowClass`; the slot's own glyph may be replaced.
    GlyphCheck CanInscribe(KitGlyphs const& kit, GlyphKind kind, uint8_t slot, uint32_t item, uint8_t level, uint8_t wowClass);
    // The kit's glyphs that play: in an open slot, of the class.
    Mods GlyphMods(KitGlyphs const& kit, uint8_t level, uint8_t wowClass);

    // ── Gems ──
    enum GemColor : uint8_t
    {
        GEM_RED = 0x01,
        GEM_YELLOW = 0x02,
        GEM_BLUE = 0x04,
        GEM_PRISMATIC = GEM_RED | GEM_YELLOW | GEM_BLUE,
        GEM_META = 0x08,
    };

    struct GemEffect
    {
        Stats::Out out = Stats::Out::Count;     // Count: none
        float amount = 0.f;
    };

    struct GemDef
    {
        uint32_t item = 0;
        char const* name = "";
        uint8_t color = 0;
        std::array<GemEffect, 2> effects{};
        char const* text = "";
    };
    std::vector<GemDef> const& Gems();
    GemDef const* GemOf(uint32_t item);
    // Whether a gem tunes `o`.
    bool Tunes(GemDef const& g, Stats::Out o);

    // A gun's sockets: the primary's 0..2 plain (2: the jeweler's own, Jewelcrafting 275) and 3
    // the meta; the secondary's 0 alone.
    enum class GunSlot : uint8_t { Primary = 0, Secondary = 1 };
    constexpr uint8_t kSockets = 4;
    constexpr uint8_t kMetaSocket = 3;
    constexpr uint8_t kJewelerSocket = 2;
    constexpr uint16_t kJewelerSocketSkill = 275;
    struct KitGems
    {
        std::array<uint32_t, kSockets> primary{};
        std::array<uint32_t, kSockets> secondary{};
        bool operator==(KitGems const& o) const { return primary == o.primary && secondary == o.secondary; }
        std::array<uint32_t, kSockets> const& Of(GunSlot s) const { return s == GunSlot::Primary ? primary : secondary; }
        std::array<uint32_t, kSockets>& Of(GunSlot s) { return s == GunSlot::Primary ? primary : secondary; }
    };
    // Whether a socket exists on that gun (the jeweler's only with the skill).
    bool SocketExists(GunSlot g, uint8_t socket, uint16_t jewelcrafting);

    // The gun the gems go in, as far as the rules care.
    struct GunInfo
    {
        bool present = false;           // the kit has a gun there
        WeaponClass weaponClass = WeaponClass::AssaultRifle;
        bool scoped = false;            // an ACOG or a Thermal on it, or a sniper
    };
    // Whether a gun counts as scoped: a sniper, or carrying the ACOG or the Thermal.
    bool Scoped(WeaponClass c, std::vector<std::string> const& attachments);

    enum class GemCheck : uint8_t { Ok, NotAGem, NoGun, NoSocket, MetaOnly, NotMeta, SameKind, NeedsScope };
    // Whether `item` may go into the socket; the socket's own gem may be replaced.
    GemCheck CanSocket(KitGems const& kit, GunSlot g, uint8_t socket, uint32_t item, GunInfo const& gun, uint16_t jewelcrafting);
    // Whether the gun's meta gem is awake: its other sockets hold gems of two colours or more.
    bool MetaAwake(std::array<uint32_t, kSockets> const& sockets);
    // The gun's gems that play (a sway gem without a scope sleeps, the meta asleep counts nothing).
    Mods GemMods(KitGems const& kit, GunSlot g, GunInfo const& gun, uint16_t jewelcrafting);

    // ── What the game applies ──
    // The handling with the mods: a multiplier times (1 + share), held to its OutputLimit and
    // 1.25; a share plus its amount, held to 0..OutputLimit; the regeneration's delay and rate
    // moved with their shares.
    void ApplyHandling(Stats::Derived& d, Mods const& m);

    // ── The wire: the professions channel 12 (Construction::kChannelProfessions), ops 20-34 ──
    constexpr uint8_t kOpAsk = 20;          // client: [20, trade u8]: the window (a trainer's when near)
    constexpr uint8_t kOpAct = 21;          // client: [21, trade u8, action u8, arg u32]
    constexpr uint8_t kOpWindow = 22;       // server: see EncodeWindow
    constexpr uint8_t kOpBook = 23;         // server: see EncodeBook
    constexpr uint8_t kOpSkill = 24;        // server: [24, trade u8, tier u8, skill u16, cap u16]
    constexpr uint8_t kOpAskAugments = 30;  // client: [30]: the kit's glyphs and gems
    constexpr uint8_t kOpAugment = 31;      // client: [31, what u8 (0 glyph, 1 gem), kind/gun u8, slot u8, item u32]
    constexpr uint8_t kOpAugments = 32;     // server: see EncodeAugments

    enum class Action : uint8_t { None = 0, Train = 1, Buy = 2, Learn = 3, Make = 4, Unlearn = 5 };
    struct Service
    {
        Action action = Action::None;
        uint32_t arg = 0;
        uint32_t cost = 0;
        bool enabled = true;
        uint8_t hue = 0;                // Homestead::Hue
        std::string label;
        std::string note;
    };
    // [22, trade u8, open u8, trainer u8, tier u8, skill u16, cap u16, tone u8, message str8,
    //  n u8, n x (action u8, arg u32, cost u32, enabled u8, hue u8, label str8, note str8)]
    struct Window
    {
        Trade trade = Trade::None;
        bool open = false;
        bool trainer = false;
        Tier tier = Tier::None;
        uint16_t skill = 0;
        uint8_t tone = 0;               // Homestead::Tone
        std::string message;
        std::vector<Service> services;
    };
    constexpr size_t kMaxServices = 40;
    std::vector<uint8_t> EncodeWindow(Window const& w);
    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out);

    std::vector<uint8_t> EncodeAct(Trade t, Action a, uint32_t arg);
    bool DecodeAct(std::vector<uint8_t> const& body, Trade& t, Action& a, uint32_t& arg);
    // Make's arg: the recipe in the low 16 bits, how many in the high (held to 1..kMaxMakeBatch).
    constexpr uint16_t kMaxMakeBatch = 20;
    uint32_t MakeArg(uint16_t recipe, uint16_t count);
    uint16_t MakeRecipeOf(uint32_t arg);
    uint16_t MakeCountOf(uint32_t arg);

    // [23, trade u8, n u16, n x (id u16, kind u8, required yellow green gray u16, known u8,
    //  product u32, count u8, group str8, name str8, reagents u8, reagents x (item u32, count u16),
    //  tool u32)]: every recipe of the trade, known or not.
    struct BookEntry
    {
        uint16_t id = 0;
        Kind kind = Kind::Make;
        Thresholds skill;
        bool known = false;
        uint32_t product = 0;
        uint8_t count = 1;
        std::string group;
        std::string name;
        std::vector<Material> reagents;
        uint32_t tool = 0;
    };
    std::vector<BookEntry> Book(Trade t, std::vector<uint16_t> const& learned);
    std::vector<uint8_t> EncodeBook(Trade t, std::vector<BookEntry> const& book);
    bool DecodeBook(std::vector<uint8_t> const& body, Trade& t, std::vector<BookEntry>& out);

    std::vector<uint8_t> EncodeSkill(Trade t, Tier tier, uint16_t skill);
    bool DecodeSkill(std::vector<uint8_t> const& body, Trade& t, Tier& tier, uint16_t& skill, uint16_t& cap);

    // [31, what u8, kind u8, slot u8, item u32]: inscribe a glyph (what 0, kind GlyphKind) or set a
    // gem (what 1, kind GunSlot); item 0 clears the slot (its glyph or gem destroyed).
    struct AugmentAsk
    {
        uint8_t what = 0;
        uint8_t kind = 0;
        uint8_t slot = 0;
        uint32_t item = 0;
    };
    std::vector<uint8_t> EncodeAugment(AugmentAsk const& a);
    bool DecodeAugment(std::vector<uint8_t> const& body, AugmentAsk& out);

    // [32, open u8, kit u8 (255 none), canEdit u8, level u8, jewelcrafting u16,
    //  majors 3 x u32, minors 3 x u32, primary 4 x u32, secondary 4 x u32,
    //  flags u8 (1 primary present, 2 secondary present, 4 primary scoped, 8 secondary scoped,
    //  16 the primary's meta awake, 32 the secondary's), tone u8, message str8]
    struct Augments
    {
        bool open = false;
        uint8_t kit = 255;
        bool canEdit = false;
        uint8_t level = 1;
        uint16_t jewelcrafting = 0;
        KitGlyphs glyphs;
        KitGems gems;
        uint8_t flags = 0;
        uint8_t tone = 0;
        std::string message;
    };
    constexpr uint8_t kAugPrimary = 0x01, kAugSecondary = 0x02, kAugPrimaryScoped = 0x04,
        kAugSecondaryScoped = 0x08, kAugPrimaryMeta = 0x10, kAugSecondaryMeta = 0x20;
    std::vector<uint8_t> EncodeAugments(Augments const& a);
    bool DecodeAugments(std::vector<uint8_t> const& body, Augments& out);
}

#endif
