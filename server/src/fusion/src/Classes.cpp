/*
 * World of Modern Warfare: the class framework. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Classes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fusion::Classes
{
    namespace
    {
        constexpr Route A = Route::A;
        constexpr Route B = Route::B;
        constexpr Route C = Route::C;
        constexpr Kind Ab = Kind::Ability;
        constexpr Kind Pa = Kind::Passive;
        constexpr Kind Fo = Kind::Form;

        // One class's rows: its three core abilities and its six tiers of three.
        struct ClassRows
        {
            uint8_t cls;
            std::array<char const*, kArchetypes> archetypes;
            struct Row
            {
                Route route;
                Kind kind;
                char const* name;
                std::array<uint32_t, 4> spells;
                bool capstone;
            };
            std::array<Row, 3> core;
            std::array<std::array<Row, kArchetypes>, kTiers> tiers;
            std::vector<uint32_t> utility;
            uint32_t basic;     // the basic damaging spell (BasicSpellOf), 0 none
        };

        // design/CLASS_CATALOG.md, sections 1-9. Spell ids are the highest rank of a ranked chain;
        // donors that would collide inside a class (two options on one spell) were moved to a
        // look-alike copy: Light Fingers 5167 (a Pick Pocket copy: Pick Pocket 921 is Utility), Blade Blink 14514 (a Blink copy), Raise
        // Ghoul 28353 (a Raise Dead copy), Hall of Mirrors 24681 (Summon Nightmare Illusions),
        // Bane Track Undead 19884 and Overwatch Track Hidden 19885 (both out of the hunter's
        // Utility trackings), Death's Harvest Explode 28433 (Corpse Explosion's own is the tier 2
        // option's). The warrior's Chain Hook is Magnetic Pull 28337 (the catalog's alternative
        // look): Hooked Net 13608 needs an enemy, and the hunter's Grappling Hook casts it on
        // itself. The last column is the class's basic damaging spell (BasicSpellOf).
        std::vector<ClassRows> const& Rows()
        {
            static std::vector<ClassRows> const rows = {
                {kWarrior, {"Berserker", "Vanguard", "Warlord"},
                 {{{A, Ab, "Charge", {11578}, false},
                   {A, Ab, "Bloodrage", {2687}, false},
                   {A, Ab, "Disarm", {676}, false}}},
                 {{{{{A, Ab, "Bloodthirst", {23894}, false},
                     {B, Ab, "Bulwark", {2565}, false},
                     {B, Ab, "Battle Standard", {23034}, false}}},
                   {{{A, Ab, "Intercept", {20617}, false},
                     {B, Ab, "Chain Hook", {28337}, false},
                     {B, Ab, "Rallying Cry", {25289}, false}}},
                   {{{A, Ab, "Whirlwind", {1680}, false},
                     {B, Ab, "Draw Fire", {1161}, false},
                     {A, Ab, "Demoralizing Shout", {11556}, false}}},
                   {{{B, Pa, "Blood Frenzy", {12970}, false},
                     {A, Ab, "Shield Wall", {871}, false},
                     {A, Ab, "Intimidating Shout", {5246}, false}}},
                   {{{A, Ab, "Death Wish", {12328}, false},
                     {B, Ab, "Shield Charge", {19131}, false},
                     {B, Pa, "War Banners", {23538}, false}}},
                   {{{B, Ab, "Rampage", {1719}, true},
                     {B, Ab, "Unbreakable", {12975}, true},
                     {B, Ab, "Warlord's Command", {22888}, true}}}}},
                 {}, 0},
                {kPaladin, {"Crusader", "Protector", "Lightwarden"},
                 {{{A, Ab, "Divine Protection", {5573}, false},
                   {A, Ab, "Hammer of Justice", {10308}, false},
                   {A, Ab, "Blessing of Freedom", {1044}, false}}},
                 {{{{{A, Ab, "Crusader Strike", {10337}, false},
                     {B, Ab, "Holy Shield", {20928}, false},
                     {A, Ab, "Flash of Light", {19943}, false}}},
                   {{{A, Ab, "Judgement", {20271}, false},
                     {A, Ab, "Consecration", {20924}, false},
                     {A, Ab, "Holy Light", {25292}, false}}},
                   {{{B, Pa, "Zealotry", {20920}, false},
                     {A, Ab, "Blessing of Protection", {10278}, false},
                     {A, Ab, "Holy Shock", {20930}, false}}},
                   {{{B, Ab, "Divine Storm", {10318}, false},
                     {A, Ab, "Blessing of Sacrifice", {20729}, false},
                     {A, Ab, "Rekindle", {20773}, false}}},
                   {{{A, Ab, "Repentance", {20066}, false},
                     {B, Pa, "Sacred Ground", {20218}, false},
                     {A, Ab, "Lay on Hands", {10310}, false}}},
                   {{{B, Ab, "Avenging Crusade", {20216}, true},
                     {C, Ab, "Bastion of Light", {1020}, true},
                     {B, Ab, "Solar Ascension", {19752}, true}}}}},
                 {20773, 13819, 23214}, 10314},
                {kHunter, {"Scout", "Trapper", "Slayer"},
                 {{{B, Ab, "Mark Prey", {14325}, false},
                   {A, Ab, "Feign Death", {5384}, false},
                   {B, Ab, "Disengage", {14273}, false}}},
                 {{{{{B, Ab, "Field Glass", {6197}, false},
                     {A, Ab, "Freezing Trap", {14311}, false},
                     {B, Ab, "Bane", {19884}, false}}},
                   {{{A, Ab, "Flare", {1543}, false},
                     {B, Ab, "Net Launcher", {6533}, false},
                     {B, Ab, "Throwing Axes", {16075}, false}}},
                   {{{B, Ab, "Camouflage", {5858}, false},
                     {A, Ab, "Tar Trap", {13809}, false},
                     {A, Ab, "Purge", {19801}, false}}},
                   {{{C, Ab, "Grappling Hook", {13608}, false},
                     {B, Ab, "Bear Trap", {9437}, false},
                     {A, Ab, "Scatter", {19503}, false}}},
                   {{{B, Ab, "Smoke Pellet", {7964}, false},
                     {B, Ab, "Noise-maker", {4071}, false},
                     {B, Ab, "Hex Ward", {6346}, false}}},
                   {{{B, Ab, "Overwatch", {19885}, true},
                     {B, Ab, "Killing Field", {14317}, true},
                     {B, Ab, "Witchfire", {14301}, true}}}}},
                 {5118, 1494, 19883, 19880, 19878, 19882, 19879}, 0},
                {kRogue, {"Assassin", "Trickster", "Duelist"},
                 {{{A, Ab, "Stealth", {1787}, false},
                   {A, Ab, "Sprint", {11305}, false},
                   {A, Ab, "Vanish", {1857}, false}}},
                 {{{{{A, Ab, "Ambush", {11269}, false},
                     {B, Ab, "Deadly Poison", {25351}, false},
                     {A, Ab, "Sinister Strike", {11294}, false}}},
                   {{{A, Ab, "Cheap Shot", {1833}, false},
                     {B, Ab, "Smoke Bomb", {7964}, false},
                     {B, Ab, "Lunge", {22120}, false}}},
                   {{{A, Ab, "Garrote", {11290}, false},
                     {B, Ab, "Poison Satchel", {11202}, false},
                     {B, Ab, "Riposte", {14251}, false}}},
                   {{{B, Ab, "Distract", {1725}, false},
                     {B, Ab, "Light Fingers", {5167}, false},
                     {B, Ab, "Blade Flurry", {13877}, false}}},
                   {{{B, Ab, "Preparation", {14185}, false},
                     {B, Ab, "Decoy", {17773}, false},
                     {B, Pa, "Relentless", {14177}, false}}},
                   {{{B, Ab, "Deathmark", {17348}, true},
                     {B, Ab, "Hall of Mirrors", {24681}, true},
                     {B, Ab, "Adrenaline Rush", {13750}, true}}}}},
                 {921, 1804, 1842}, 0},
                {kPriest, {"Shadow", "Cleric", "Oracle"},
                 {{{A, Ab, "Power Word: Shield", {10901}, false},
                   {A, Ab, "Desperate Prayer", {19243}, false},
                   {A, Ab, "Fade", {10942}, false}}},
                 {{{{{A, Ab, "Shadow Word: Pain", {10894}, false},
                     {A, Ab, "Flash Heal", {10917}, false},
                     {C, Ab, "Barrier", {16051}, false}}},
                   {{{A, Ab, "Mind Blast", {10947}, false},
                     {A, Ab, "Renew", {25315}, false},
                     {B, Ab, "Pain Suppression", {14751}, false}}},
                   {{{A, Ab, "Vampiric Embrace", {15286}, false},
                     {A, Ab, "Prayer of Healing", {25316}, false},
                     {A, Ab, "Psychic Scream", {10890}, false}}},
                   {{{A, Ab, "Devouring Plague", {19280}, false},
                     {A, Ab, "Lightwell", {27871}, false},
                     {B, Pa, "Premonition", {19293}, false}}},
                   {{{A, Ab, "Mind Control", {10912}, false},
                     {A, Pa, "Spirit of Redemption", {20711}, false},
                     {A, Ab, "Silence", {15487}, false}}},
                   {{{A, Ab, "Shadowform", {15473}, true},
                     {B, Ab, "Divine Hymn", {27805}, true},
                     {C, Ab, "Sanctum", {22850}, true}}}}},
                 {20770, 1706}, 10934},
                {kShaman, {"Elementalist", "Earthbreaker", "Totemist"},
                 {{{A, Ab, "Ghost Wolf", {2645}, false},
                   {A, Ab, "Purge", {8012}, false},
                   {A, Pa, "Reincarnation", {20608}, false}}},
                 {{{{{A, Ab, "Lightning Bolt", {15208}, false},
                     {B, Ab, "Stone Fists", {16316}, false},
                     {A, Ab, "Healing Stream Totem", {10463}, false}}},
                   {{{A, Ab, "Chain Lightning", {10605}, false},
                     {A, Ab, "Stormstrike", {17364}, false},
                     {A, Ab, "Earthbind Totem", {2484}, false}}},
                   {{{A, Ab, "Frost Shock", {10473}, false},
                     {A, Ab, "Earth Shock", {10414}, false},
                     {A, Ab, "Grounding Totem", {8177}, false}}},
                   {{{A, Ab, "Elemental Mastery", {16166}, false},
                     {B, Ab, "Ground Smash", {12734}, false},
                     {A, Ab, "Chain Heal", {10623}, false}}},
                   {{{B, Pa, "Overcharge", {10432}, false},
                     {B, Pa, "Stoneskin", {10408}, false},
                     {A, Ab, "Windwall Totem", {15112}, false}}},
                   {{{B, Ab, "Earthquake", {19798}, true},
                     {B, Ab, "Avatar of Stone", {4934}, true},
                     {B, Ab, "Ancestral Circle", {17359}, true}}}}},
                 {20777, 556, 546, 131, 6196}, 15208},
                {kMage, {"Wizard", "Spellsword", "Artificer"},
                 {{{A, Ab, "Blink", {1953}, false},
                   {A, Ab, "Ice Block", {11958}, false},
                   {A, Ab, "Polymorph", {12826}, false}}},
                 {{{{{A, Ab, "Fireball", {25306}, false},
                     {B, Ab, "Flame Blade", {10199}, false},
                     {C, Ab, "Warding Rune", {28773}, false}}},
                   {{{A, Ab, "Frostbolt", {25304}, false},
                     {B, Ab, "Blade Blink", {14514}, false},
                     {B, Ab, "Healing Glyph", {24815}, false}}},
                   {{{A, Ab, "Arcane Missiles", {25345}, false},
                     {A, Ab, "Frost Nova", {10230}, false},
                     {B, Ab, "Blink Pads", {18960}, false}}},
                   {{{A, Ab, "Blast Wave", {13021}, false},
                     {A, Ab, "Mana Shield", {10193}, false},
                     {A, Ab, "Conjure Supplies", {10054}, false}}},
                   {{{A, Ab, "Combustion", {11129}, false},
                     {B, Pa, "Arcane Edge", {12536}, false},
                     {B, Ab, "Arcane Familiar", {10721}, false}}},
                   {{{B, Ab, "Meteor", {24340}, true},
                     {B, Ab, "Arcane Avatar", {12042}, true},
                     {B, Ab, "Grand Array", {12051}, true}}}}},
                 // Scorch and Cone of Cold first: the fire stream and the frost spray (fusion/Spellcasting.h
                 // SprayRows), from level 1.
                 {10207, 10161, 28612, 10140, 3561, 3562, 3565, 3567, 3563, 3566, 10059, 11416, 11419, 11417, 11418, 11420, 130, 475}, 25306},
                {kWarlock, {"Afflicter", "Felsworn", "Necromancer"},
                 {{{A, Ab, "Fear", {6215}, false},
                   {A, Ab, "Life Tap", {11689}, false},
                   {A, Ab, "Soulstone", {20757}, false}}},
                 {{{{{A, Ab, "Corruption", {25311}, false},
                     {B, Ab, "Fel Hide", {11735}, false},
                     {B, Ab, "Raise Skeleton", {17473}, false}}},
                   {{{A, Ab, "Curse of Exhaustion", {18223}, false},
                     {B, Ab, "Fel Leap", {24530}, false},
                     {B, Ab, "Corpse Explosion", {17616}, false}}},
                   {{{A, Ab, "Drain Life", {11700}, false},
                     {A, Ab, "Hellfire", {11684}, false},
                     {B, Ab, "Bone Armor", {11445}, false}}},
                   {{{A, Ab, "Howl of Terror", {17928}, false},
                     {A, Ab, "Death Coil", {17926}, false},
                     {B, Ab, "Raise Ghoul", {28353}, false}}},
                   {{{B, Pa, "Soul Siphon", {18881}, false},
                     {B, Pa, "Demonic Pact", {19028}, false},
                     {C, Pa, "Death's Harvest", {28433}, false}}},
                   {{{A, Ab, "Curse of Doom", {603}, true},
                     {B, Ab, "Demon Form", {8872}, true},
                     {B, Ab, "Abomination", {28422}, true}}}}},
                 {11730, 698, 5697, 126, 5784, 23161}, 25307},
                {kDruid, {"Shapeshifter", "Grovekeeper", "Starcaller"},
                 {{{A, Ab, "Barkskin", {22812}, false},
                   {A, Ab, "Entangling Roots", {9853}, false},
                   {A, Ab, "Travel Form", {783}, false}}},
                 {{{{{A, Fo, "Cat Form", {768, 9913, 9827, 9904}, false},
                     {A, Ab, "Rejuvenation", {25299}, false},
                     {A, Ab, "Moonfire", {9835}, false}}},
                   {{{A, Fo, "Bear Form", {9634, 9881, 8983, 6795}, false},
                     {A, Ab, "Regrowth", {9858}, false},
                     {B, Ab, "Lunar Strike", {25298}, false}}},
                   {{{B, Fo, "Stag Form", {21936, 16979}, false},
                     {A, Ab, "Swiftmend", {18562}, false},
                     {A, Ab, "Faerie Fire", {9907}, false}}},
                   {{{B, Fo, "Crow Form", {8153}, false},
                     {A, Ab, "Rebirth", {20748}, false},
                     {A, Ab, "Hurricane", {17402}, false}}},
                   {{{B, Fo, "Spider Form", {24084, 12252}, false},
                     {A, Ab, "Tranquility", {9863}, false},
                     {B, Pa, "Lunar Grace", {24907}, false}}},
                   {{{B, Fo, "Worgen Form", {7621, 16979}, true},
                     {A, Ab, "Tree of Life", {775}, true},
                     {B, Ab, "Starfall", {20687}, true}}}}},
                 {1066, 18960, 2782, 2893}, 9912},
            };
            return rows;
        }

        ClassRows const* RowsOf(uint8_t cls)
        {
            for (ClassRows const& r : Rows())
                if (r.cls == cls)
                    return &r;
            return nullptr;
        }

        std::vector<Option> Build()
        {
            std::vector<Option> out;
            for (ClassRows const& c : Rows())
            {
                for (uint8_t i = 0; i < 3; ++i)
                {
                    ClassRows::Row const& r = c.core[i];
                    out.push_back({OptionId(c.cls, kCoreTier, i), c.cls, kCoreTier, kCoreArchetype, r.route, r.kind,
                        false, r.name, r.spells});
                }
                for (uint8_t t = 0; t < kTiers; ++t)
                    for (uint8_t a = 0; a < kArchetypes; ++a)
                    {
                        ClassRows::Row const& r = c.tiers[t][a];
                        out.push_back({OptionId(c.cls, uint8_t(t + 1), a), c.cls, uint8_t(t + 1), a, r.route, r.kind,
                            r.capstone, r.name, r.spells});
                    }
            }
            return out;
        }

        constexpr std::array<uint8_t, kTiers + 1> kLevels = {1, 10, 20, 30, 40, 50, 60};
        constexpr std::array<uint32_t, kTiers + 1> kGold = {0, 1, 5, 12, 22, 35, 60};
        constexpr uint32_t kCopperPerGold = 10000;
    }

    bool IsPlayableClass(uint8_t cls)
    {
        return RowsOf(cls) != nullptr;
    }

    std::vector<Option> const& Catalog()
    {
        static std::vector<Option> const all = Build();
        return all;
    }

    Option const* FindOption(uint16_t id)
    {
        if (!id)
            return nullptr;
        for (Option const& o : Catalog())
            if (o.id == id)
                return &o;
        return nullptr;
    }

    std::vector<Option const*> OptionsOf(uint8_t cls)
    {
        std::vector<Option const*> out;
        for (Option const& o : Catalog())
            if (o.cls == cls && o.tier != kCoreTier)
                out.push_back(&o);
        return out;
    }

    std::array<Option const*, kArchetypes> TierOptions(uint8_t cls, uint8_t tier)
    {
        std::array<Option const*, kArchetypes> out{};
        for (uint8_t a = 0; a < kArchetypes; ++a)
            out[a] = tier >= 1 && tier <= kTiers ? FindOption(OptionId(cls, tier, a)) : nullptr;
        return out;
    }

    std::array<Option const*, 3> CoreOf(uint8_t cls)
    {
        std::array<Option const*, 3> out{};
        for (uint8_t i = 0; i < 3; ++i)
            out[i] = FindOption(OptionId(cls, kCoreTier, i));
        return out;
    }

    std::vector<uint32_t> const& UtilityOf(uint8_t cls)
    {
        static std::vector<uint32_t> const none;
        ClassRows const* r = RowsOf(cls);
        return r ? r->utility : none;
    }

    char const* ArchetypeName(uint8_t cls, uint8_t archetype)
    {
        ClassRows const* r = RowsOf(cls);
        if (!r)
            return "";
        if (archetype == kCoreArchetype)
            return "Core";
        return archetype < kArchetypes ? r->archetypes[archetype] : "";
    }

    char const* ClassName(uint8_t cls)
    {
        switch (cls)
        {
            case kWarrior: return "Warrior";
            case kPaladin: return "Paladin";
            case kHunter: return "Hunter";
            case kRogue: return "Rogue";
            case kPriest: return "Priest";
            case kShaman: return "Shaman";
            case kMage: return "Mage";
            case kWarlock: return "Warlock";
            case kDruid: return "Druid";
            default: return "";
        }
    }

    Option const* OptionBySpell(uint8_t cls, uint32_t spell)
    {
        if (!spell)
            return nullptr;
        for (Option const& o : Catalog())
        {
            if (o.cls != cls)
                continue;
            for (uint32_t s : o.spells)
                if (s == spell)
                    return &o;
        }
        return nullptr;
    }

    bool LearnsSpells(Option const& o)
    {
        return o.kind != Kind::Passive || o.route == Route::A;
    }

    uint32_t BasicSpellOf(uint8_t cls)
    {
        ClassRows const* r = RowsOf(cls);
        return r ? r->basic : 0;
    }

    std::vector<uint32_t> const& RetiredSpells(uint8_t cls)
    {
        static std::vector<uint32_t> const none;
        // The warrior's Chain Hook was Hooked Net (now the hunter's Grappling Hook alone).
        static std::vector<uint32_t> const warrior = {13608};
        // The Bard's Lullaby was Sleep. The Bard archetype is gone (2026-10-21: music is the
        // Minstrelsy profession, the Duelist took the Bard's slot): its instruments are no options
        // any more, nor the Assassin's Backstab and Kidney Shot or the Trickster's Shadowstep and
        // Blind (RetiredReplacement says what a bar button holding one gets instead).
        static std::vector<uint32_t> const rogue = {700, 13159, 19779, 17512, 10293, 18946, 26188,
            25300, 8643, 21655, 2094};
        return cls == kWarrior ? warrior : cls == kRogue ? rogue : none;
    }

    uint32_t RetiredReplacement(uint8_t cls, uint32_t spell)
    {
        if (cls != kRogue)
            return 0;
        switch (spell)
        {
            case 13159: return 11294;   // Hand Drum -> Sinister Strike (tier 10)
            case 19779: return 22120;   // Lute -> Lunge (20)
            case 17512: return 14251;   // Flute -> Riposte (30)
            case 10293: return 13877;   // Harp -> Blade Flurry (40)
            case 18946: return 0;       // Bagpipes -> Relentless, a passive: no button
            case 26188: return 13750;   // Thunder Drums -> Adrenaline Rush (60)
            case 25300: return 11269;   // Backstab -> Ambush (Assassin 10)
            case 8643: return 1725;     // Kidney Shot -> Distract (Assassin 40)
            case 21655: return 25351;   // Shadowstep -> Deadly Poison (Trickster 10)
            case 2094: return 11202;    // Blind -> Poison Satchel (Trickster 30)
            default: return 0;
        }
    }

    std::vector<std::pair<uint8_t, uint32_t>> MigrateRetiredButtons(uint8_t cls,
        std::vector<std::pair<uint8_t, uint32_t>> const& buttons, std::function<uint32_t(uint32_t)> const& topOf,
        std::function<bool(uint32_t)> const& known)
    {
        std::vector<uint32_t> const& retired = RetiredSpells(cls);
        std::vector<std::pair<uint8_t, uint32_t>> out;
        for (auto const& [slot, spell] : buttons)
        {
            uint32_t const top = topOf ? topOf(spell) : spell;
            if (std::find(retired.begin(), retired.end(), top ? top : spell) == retired.end())
                continue;
            uint32_t const next = RetiredReplacement(cls, top ? top : spell);
            out.push_back({slot, next && known && known(next) ? next : 0u});
        }
        return out;
    }

    float MomentumMultiplier(uint16_t points)
    {
        return 1.f + kMomentumDamagePerPoint * float(std::min<uint16_t>(points, kMomentumRelentless));
    }

    uint16_t MomentumAfter(uint16_t points, uint32_t idleMs)
    {
        if (idleMs <= kMomentumHoldMs)
            return points;
        uint32_t const lost = (idleMs - kMomentumHoldMs) / 1000 + 1;
        return lost >= points ? 0 : uint16_t(points - lost);
    }

    namespace
    {
        constexpr Status W = Status::Working;
        constexpr Status P = Status::Partial;
        constexpr Ends In = Ends::Instant;
        constexpr Ends Ti = Ends::Timed;
        constexpr Ends To = Ends::Toggle;
        constexpr Ends Ch = Ends::Channel;
        constexpr Ends Pv = Ends::Passive;
        constexpr Ends Fm = Ends::Form;
        constexpr Resource R0 = Resource::None;

        // The audit of every core ability and option (CLASS_CATALOG.md section 15): what it is
        // now, how its state ends, its cooldown and what it spends. Durations are those of the
        // effect the player sees (an aura, a zone, a minion).
        std::vector<Spec> const& SpecRows()
        {
            static std::vector<Spec> const rows = {
                // ── Warrior ──
                {101, W, In, 0, 15000, R0, 0, 0, "Usable in combat. Roots the target 1 s as vanilla Charge stuns it."},
                {102, W, Ti, 6000, 45000, R0, 0, 0, "Costs no health: heals 15% over 6 s instead."},
                {103, W, Ti, 4000, 40000, R0, 0, 20, "4 s on NPCs, 3 s on players; no shot or swing while disarmed (an NPC gunner's too)."},
                {111, W, In, 0, 6000, R0, 0, 30, "A hit has a 20% chance to send the Berserker into a Frenzy."},
                {112, W, Ti, 6000, 15000, R0, 0, 10, "Needs a shield."},
                {113, W, Ti, 30000, 45000, R0, 0, 0, ""},
                {121, W, In, 0, 20000, R0, 0, 10, "A Frenzy refunds it."},
                {122, W, In, 0, 20000, R0, 0, 0, "30 yd, Magnetic Pull's look."},
                {123, W, Ti, 8000, 60000, R0, 0, 10, "A downed ally in range gets 10 s more to be revived instead of the heal."},
                {131, W, In, 0, 10000, R0, 0, 25, "Needs a melee weapon."},
                {132, W, Ti, 6000, 30000, R0, 0, 5, "NPCs are taunted 6 s (vanilla); players are marked 4 s."},
                {133, W, Ti, 10000, 30000, R0, 0, 10, "Its cut replaces vanilla's attack power cut, on NPCs and players."},
                {141, W, Pv, 0, 0, R0, 0, 0, ""},
                {142, W, Ti, 10000, 120000, R0, 0, 0, "Needs a shield; lasts 10 s."},
                {143, W, Ti, 8000, 90000, R0, 0, 25, ""},
                {151, W, Ti, 15000, 120000, R0, 0, 10, "No armor cut: +30% in melee, no fear, 20% fewer bullets hurt."},
                {152, W, In, 0, 25000, R0, 0, 0, "The vanilla charge and knockback; enemies within 2.5 yd of its path are swept."},
                {153, W, Pv, 0, 0, R0, 0, 0, "Banners take turns: Rally, Ward, Pursuit."},
                {161, W, Ti, 20000, 180000, R0, 0, 0, ""},
                {162, W, Ti, 12000, 180000, R0, 0, 0, "+50% health on Last Stand's buff."},
                {163, W, Ti, 20000, 300000, R0, 0, 0, ""},
                // ── Paladin ──
                {201, W, Ti, 8000, 120000, R0, 0, 0, "Half damage taken, not immunity."},
                {202, W, Ti, 5000, 45000, R0, 0, 0, "5 s on NPCs, 3 s on players."},
                {203, W, Ti, 10000, 25000, R0, 0, 0, "Lasts 10 s."},
                {211, W, In, 0, 5000, R0, 0, 0, "Needs a melee weapon; ranks at 10, 22, 34, 46 and 58."},
                {212, W, Ti, 10000, 15000, R0, 0, 0, "Needs no shield."},
                {213, W, In, 0, 2000, R0, 0, 0, ""},
                {221, W, In, 0, 8000, R0, 0, 0, "Needs no seal."},
                {222, W, Ti, 8000, 20000, R0, 0, 0, ""},
                {223, W, In, 0, 4000, R0, 0, 0, ""},
                {231, W, Pv, 0, 0, R0, 0, 0, ""},
                {232, W, Ti, 6000, 180000, R0, 0, 0, "Lasts 6 s; the ally cannot fire or swing."},
                {233, W, In, 0, 10000, R0, 0, 0, ""},
                {241, W, In, 0, 12000, R0, 0, 0, ""},
                {242, W, Ti, 12000, 30000, R0, 0, 0, "No buff icon on the ally (a message says it)."},
                {243, W, In, 0, 600000, R0, 0, 0, "Out of combat it is Redemption, with no cooldown."},
                {251, W, Ti, 6000, 45000, R0, 0, 0, "Humanoids only (vanilla)."},
                {252, W, Pv, 0, 0, R0, 0, 0, ""},
                {253, W, In, 0, 300000, R0, 0, 0, "Stands a downed ally up at full health; drains no mana."},
                {261, W, Ti, 20000, 180000, R0, 0, 0, "Divine Favor's glow, not wings."},
                {262, W, Ti, 8000, 180000, R0, 0, 0, ""},
                {263, W, Ti, 20000, 180000, R0, 0, 0, "Its heals still stop to cast."},
                // ── Hunter ──
                {301, P, Ti, 30000, 10000, R0, 0, 0, "Spotting, speed and the kill reset; no trail yet."},
                {302, W, Ti, 6000, 30000, R0, 0, 0, "Moving ends it; the enemy players aiming at you see a kill."},
                {303, W, In, 0, 15000, R0, 0, 0, ""},
                {311, P, In, 0, 1000, R0, 0, 0, "Spots the enemies in a narrow cone ahead on each press; no zoom."},
                {312, W, Ti, 60000, 15000, R0, 0, 0, "Up to 4 traps (8 in a Killing Field); one going off sets off those within 6 yd."},
                {313, W, Ti, 60000, 20000, R0, 0, 0, ""},
                {321, W, Ti, 30000, 20000, R0, 0, 0, "Lands on the crosshair's enemy or 30 yd ahead; its vanilla light shows at your feet."},
                {322, W, Ti, 10000, 25000, R0, 0, 0, ""},
                {323, W, In, 0, 6000, R0, 0, 0, ""},
                {331, W, To, 0, 1000, R0, 0, 0, "Off again at a press; after it breaks it waits 10 s."},
                {332, W, Ti, 60000, 15000, R0, 0, 0, "Jumping is not locked."},
                {333, W, In, 0, 15000, R0, 0, 0, ""},
                {341, W, In, 0, 20000, R0, 0, 0, "Reels you to the target, or to the ground 30 yd ahead."},
                {342, W, Ti, 60000, 15000, R0, 0, 0, "The vanilla bear trap, with the bleed and the sprint lock."},
                {343, W, Ti, 4000, 30000, R0, 0, 0, ""},
                {351, W, Ti, 10000, 40000, R0, 0, 0, ""},
                {352, P, Ti, 15000, 40000, R0, 0, 0, "The vanilla target dummy NPCs attack; no radar ping."},
                {353, W, Ti, 8000, 45000, R0, 0, 0, ""},
                {361, W, Ti, 20000, 180000, R0, 0, 0, ""},
                {362, W, Ti, 60000, 180000, R0, 0, 0, "The ring is Freezing, Tar and Bear Traps as bought; Freezing for the rest."},
                {363, W, Ti, 30000, 180000, R0, 0, 0, ""},
                // ── Rogue ──
                {401, W, To, 0, 6000, R0, 0, 0, "Not usable in combat (Vanish is)."},
                {402, W, Ti, 15000, 60000, R0, 0, 0, "Lasts 15 s."},
                {403, W, In, 0, 120000, R0, 0, 0, ""},
                {411, W, In, 0, 8000, R0, 0, 0, "An opener: from stealth or the opening, from behind (Distract lifts that); any weapon."},
                {412, W, Ti, 60000, 15000, R0, 0, 0, "Blows stack a poison (up to 3), 1.5 %EH/s each, 12 s."},
                {413, W, In, 0, 5000, R0, 0, 0, "A second blow from the off hand; 2 Momentum."},
                {421, W, Ti, 4000, 20000, R0, 0, 0, "An opener; 4 s / 2.5 s, a second more from behind."},
                {422, W, Ti, 8000, 40000, R0, 0, 0, "You restealth in the smoke, even in combat; untargetable 1 s."},
                {423, W, In, 0, 12000, R0, 0, 0, "A dash at the crosshair's enemy, then a blow (the Duelist's only lunge)."},
                {431, W, Ti, 18000, 20000, R0, 0, 0, "An opener, from any side; the silence a second longer from behind."},
                {432, W, Ti, 60000, 3000, R0, 0, 0, "Pressed again it switches: Crippling, Mind-numbing, Wound."},
                {433, W, Ti, 1500, 10000, R0, 0, 0, "Melee blows on you while it holds are parried and struck back."},
                {441, W, Ti, 8000, 30000, R0, 0, 0, "Creatures near the point turn to it; your openers count as from behind for 8 s."},
                {442, W, In, 0, 20000, R0, 0, 0, "Pickpocket loot straight to your bags, in combat too; Pick Lock instant and +50 skill."},
                {443, W, Ti, 12000, 30000, R0, 0, 0, "Each blow also strikes another enemy within 8 yd for half."},
                {451, W, In, 0, 180000, R0, 0, 0, "Readies Stealth, Vanish, Sprint and every Assassin ability."},
                {452, P, Ti, 6000, 45000, R0, 0, 0, "The copy runs; you are unseen 3 s."},
                {453, W, Pv, 0, 0, R0, 0, 0, "Momentum holds 8; at 5 or more, 15% fewer bullets hurt."},
                {461, W, Ti, 10000, 120000, R0, 0, 0, ""},
                {462, W, Ti, 12000, 120000, R0, 0, 0, "The copies follow you and fire nothing; the button swaps."},
                {463, W, Ti, 15000, 120000, R0, 0, 0, "Momentum full and held; Lunge has no cooldown."},
                // ── Priest ──
                {501, W, Ti, 30000, 6000, R0, 0, 0, ""},
                {502, W, In, 0, 120000, R0, 0, 0, "Usable while downed: stands you up."},
                {503, P, Ti, 10000, 30000, R0, 0, 0, "Vanilla Fade; no radar hide yet."},
                {511, W, Ti, 18000, 3000, R0, 0, 0, ""},
                {512, W, In, 0, 2000, R0, 0, 0, ""},
                {513, W, Ti, 8000, 25000, R0, 0, 0, "Breaks after taking half your health in rounds."},
                {521, W, In, 0, 8000, R0, 0, 0, ""},
                {522, W, Ti, 15000, 4000, R0, 0, 0, ""},
                {523, W, Ti, 8000, 60000, R0, 0, 0, ""},
                {531, W, Ti, 20000, 30000, R0, 0, 0, "On the priest, not a debuff on the target."},
                {532, W, In, 0, 10000, R0, 0, 0, ""},
                {533, W, Ti, 8000, 30000, R0, 0, 0, ""},
                {541, W, Ti, 24000, 60000, R0, 0, 0, "Jumps when you land the killing blow."},
                {542, W, Ti, 60000, 60000, R0, 0, 0, "Heals by itself its first minute; clicking it works its vanilla 3 min."},
                {543, W, Pv, 0, 0, R0, 0, 0, ""},
                {551, W, Ch, 0, 90000, R0, 0, 0, "A player is held 5 s and cannot fire; an NPC 30 s."},
                {552, W, Pv, 0, 0, R0, 0, 0, "Once every 2 min."},
                {553, W, Ti, 5000, 40000, R0, 0, 0, ""},
                {561, W, Ti, 25000, 120000, R0, 0, 0, "Spreads when you land the killing blow."},
                {562, W, Ti, 8000, 180000, R0, 0, 0, "Not a channel: moving does not stop it."},
                {563, W, Ti, 10000, 180000, R0, 0, 0, ""},
                // ── Shaman ──
                {701, W, To, 0, 5000, R0, 0, 0, ""},
                {702, W, In, 0, 8000, R0, 0, 0, ""},
                {703, W, Pv, 0, 0, R0, 0, 0, "Stands you up 2 s after going down; the vanilla one (an Ankh) still works on death."},
                {711, W, In, 0, 2000, R0, 0, 0, "Every shaman knows Lightning Bolt; this adds Static and Overload."},
                {712, W, Ti, 60000, 10000, R0, 0, 0, ""},
                {713, W, Ti, 30000, 20000, R0, 0, 0, ""},
                {721, W, In, 0, 8000, R0, 0, 0, ""},
                {722, W, Ti, 12000, 10000, R0, 0, 0, ""},
                {723, W, Ti, 30000, 20000, R0, 0, 0, ""},
                {731, W, Ti, 8000, 8000, R0, 0, 0, ""},
                {732, W, In, 0, 12000, R0, 0, 0, ""},
                {733, W, Ti, 15000, 25000, R0, 0, 0, "Catches one grenade or flash going off within 10 yd."},
                {741, W, Ti, 15000, 60000, R0, 0, 0, ""},
                {742, W, In, 0, 20000, R0, 0, 0, ""},
                {743, W, In, 0, 4000, R0, 0, 0, ""},
                {751, W, Pv, 0, 0, R0, 0, 0, ""},
                {752, W, Pv, 0, 0, R0, 0, 0, ""},
                {753, W, Ti, 20000, 30000, R0, 0, 0, ""},
                {761, W, Ti, 10000, 120000, R0, 0, 0, ""},
                {762, W, Ti, 20000, 180000, R0, 0, 0, ""},
                {763, W, Ti, 15000, 180000, R0, 0, 0, "One totem of each element, as in vanilla."},
                // ── Mage ──
                {801, W, In, 0, 15000, R0, 0, 0, ""},
                {802, W, Ti, 10000, 180000, R0, 0, 0, "Click the aura off to leave it early."},
                {803, W, Ti, 20000, 30000, R0, 0, 0, ""},
                {811, W, In, 0, 2000, R0, 0, 0, "Every mage knows Fireball; this adds Spellweave's switch bonus."},
                {812, W, Ti, 60000, 10000, R0, 0, 0, ""},
                {813, W, Ti, 10000, 20000, R0, 0, 0, "Breaks after taking 40% of your health in rounds."},
                {821, W, In, 0, 2000, R0, 0, 0, ""},
                {822, W, In, 0, 15000, R0, 0, 0, ""},
                {823, W, Ti, 15000, 30000, R0, 0, 0, ""},
                {831, W, Ch, 0, 6000, R0, 0, 0, ""},
                {832, W, Ti, 8000, 20000, R0, 0, 0, ""},
                {833, W, Ti, 45000, 60000, R0, 0, 0, ""},
                {841, W, In, 0, 30000, R0, 0, 0, ""},
                {842, W, Ti, 15000, 25000, R0, 0, 0, "No shield glow."},
                {843, W, In, 0, 60000, R0, 0, 0, "Up to four carried; a stim heals 25% and speeds you 20% for 4 s."},
                {851, W, Ti, 60000, 90000, R0, 0, 0, "Lasts up to 60 s."},
                {852, W, Pv, 0, 0, R0, 0, 0, ""},
                {853, W, Ti, 30000, 45000, R0, 0, 0, ""},
                {861, W, Ti, 8000, 90000, R0, 0, 0, ""},
                {862, W, Ti, 20000, 180000, R0, 0, 0, ""},
                {863, W, Ti, 30000, 180000, R0, 0, 0, ""},
                // ── Warlock ──
                {901, W, Ti, 20000, 20000, R0, 0, 0, ""},
                {902, W, In, 0, 10000, R0, 0, 0, "Also a soul shard (up to 5)."},
                {903, W, In, 0, 900000, R0, 0, 0, "On the crosshair's ally or yourself; up 2 s after going down."},
                {911, W, Ti, 18000, 3000, R0, 0, 0, ""},
                {912, W, Ti, 20000, 30000, R0, 0, 0, ""},
                {913, W, Ti, 60000, 3000, Resource::LifeForce, 20, 0, "Needs a corpse within 30 yd."},
                {921, W, Ti, 12000, 8000, R0, 0, 0, "Aiming is not slowed."},
                {922, W, In, 0, 20000, R0, 0, 0, ""},
                {923, W, In, 0, 6000, Resource::LifeForce, 15, 0, "Needs a corpse or a minion within 30 yd."},
                {931, W, Ch, 0, 10000, R0, 0, 0, ""},
                {932, W, Ti, 10000, 20000, R0, 0, 0, "The burn has no fire of its own to see."},
                {933, W, Ti, 20000, 20000, Resource::LifeForce, 20, 0, ""},
                {941, W, Ti, 8000, 40000, R0, 0, 0, ""},
                {942, W, Ti, 3000, 45000, R0, 0, 0, ""},
                {943, W, Ti, 60000, 10000, Resource::LifeForce, 30, 0, ""},
                {951, W, Pv, 0, 0, R0, 0, 0, "Spreads when you land the killing blow."},
                {952, W, Pv, 0, 0, R0, 0, 0, ""},
                {953, W, Pv, 0, 0, R0, 0, 0, ""},
                {961, W, Ti, 10000, 60000, R0, 0, 0, "Spreads when you land the killing blow."},
                {962, W, Ti, 30000, 180000, R0, 0, 0, ""},
                {963, W, Ti, 45000, 120000, Resource::LifeForce, 80, 0, ""},
                // ── Druid (the forms are reworked separately) ──
                {1101, W, Ti, 15000, 60000, R0, 0, 0, ""},
                {1102, W, Ti, 27000, 20000, R0, 0, 0, ""},
                {1103, W, Fm, 0, 5000, R0, 0, 0, ""},
                {1111, W, Fm, 0, 0, R0, 0, 0, "No energy or combo points."},
                {1112, W, Ti, 12000, 3000, R0, 0, 0, ""},
                {1113, W, Ti, 12000, 4000, R0, 0, 0, ""},
                {1121, W, Fm, 0, 0, R0, 0, 0, "No rage."},
                {1122, W, Ti, 21000, 4000, R0, 0, 0, ""},
                {1123, W, In, 0, 6000, R0, 0, 0, ""},
                {1131, P, Fm, 0, 1500, R0, 0, 0, "Travel Form in the stag's look; no passenger seat."},
                {1132, W, In, 0, 12000, R0, 0, 0, ""},
                {1133, W, Ti, 40000, 10000, R0, 0, 0, ""},
                {1141, P, Fm, 0, 1500, R0, 0, 0, "Owl look, slow fall and spotting below; no lift."},
                {1142, W, In, 0, 600000, R0, 0, 0, ""},
                {1143, P, Ch, 0, 60000, R0, 0, 0, "Centred on you (no ground cursor)."},
                {1151, P, Fm, 0, 1500, R0, 0, 0, "Mar'li's look and Web Spray; no wall crawl or Venom Bite."},
                {1152, W, Ch, 0, 180000, R0, 0, 0, ""},
                {1153, W, Pv, 0, 0, R0, 0, 0, ""},
                {1161, P, Fm, 0, 1500, R0, 0, 0, "Health, lifesteal, Rending Leap's hit, momentum; no Frenzy yet."},
                {1162, W, Ti, 30000, 180000, R0, 0, 0, "Regrowth still stops to cast."},
                {1163, W, Ti, 10000, 120000, R0, 0, 0, ""},
            };
            return rows;
        }
    }

    std::vector<Spec> const& Specs()
    {
        return SpecRows();
    }

    Spec const* SpecOf(uint16_t id)
    {
        for (Spec const& s : SpecRows())
            if (s.id == id)
                return &s;
        return nullptr;
    }

    bool Available(uint16_t id)
    {
        Spec const* s = SpecOf(id);
        return s && s->status != Status::Unavailable;
    }

    char const* StatusName(Status s)
    {
        switch (s)
        {
            case Status::Working: return "working";
            case Status::Partial: return "partial";
            case Status::Unavailable: return "unavailable";
        }
        return "";
    }

    namespace
    {
        constexpr uint32_t SR = REFIT_SELF | REFIT_REPLACE;

        std::vector<Refit> const& RefitRows()
        {
            // CLASS_CATALOG.md's cooldowns and tweaks; every rank of a chain is fitted alike.
            static std::vector<Refit> const rows = {
                // Warrior.
                {11578, 15000, -1, -1, REFIT_IN_COMBAT | REFIT_KEEP_COST},  // Charge
                {2687, 45000, -1, -1, 0},                   // Bloodrage
                {676, 40000, -1, -1, REFIT_KEEP_COST},      // Disarm
                {23894, 6000, -1, -1, REFIT_KEEP_COST},     // Bloodthirst
                {2565, 15000, -1, -1, 0},                   // Bulwark (Shield Block)
                {23034, 45000, -1, -1, 0},                  // Battle Standard (its banner stays vanilla)
                {20617, 20000, -1, -1, REFIT_KEEP_COST},    // Intercept
                {28337, 20000, 1, 4, REFIT_REPLACE},        // Chain Hook (Magnetic Pull): 30 yd
                {25289, 60000, -1, -1, REFIT_REPLACE},      // Rallying Cry (Battle Shout)
                {1680, 10000, -1, -1, REFIT_KEEP_COST},     // Whirlwind
                {1161, 30000, -1, -1, 0},                   // Draw Fire (Challenging Shout)
                {11556, 30000, -1, -1, REFIT_REPLACE},      // Demoralizing Shout: its own cut on players and NPCs
                {871, 120000, -1, -1, 0},                   // Shield Wall
                {5246, 90000, -1, -1, 0},                   // Intimidating Shout
                {12328, 120000, -1, -1, REFIT_REPLACE_THIRD}, // Death Wish: no armor cut
                {19131, 25000, -1, -1, 0},                  // Shield Charge
                {1719, 180000, -1, -1, REFIT_REPLACE},      // Rampage (Recklessness)
                {12975, 180000, -1, -1, REFIT_REPLACE},     // Unbreakable (Last Stand): +50%, not +30%
                {22888, 300000, -1, -1, SR},                // Warlord's Command
                // Paladin.
                {5573, 120000, -1, -1, REFIT_REPLACE},      // Divine Protection: half damage, not immunity
                {10308, 45000, -1, 3, 0},                   // Hammer of Justice: 20 yd
                {1044, 25000, -1, -1, 0},                   // Blessing of Freedom
                {10337, 5000, -1, -1, 0},                   // Crusader Strike
                {20928, 15000, -1, -1, REFIT_REPLACE | REFIT_NO_WEAPON},    // Holy Shield: no shield needed
                {19943, 2000, 4, -1, 0},                    // Flash of Light: 1 s
                {20271, 8000, -1, -1, REFIT_REPLACE | REFIT_NO_AURA_STATE}, // Judgement: no seal, spends Holy Power
                {20924, 20000, -1, -1, 0},                  // Consecration
                {25292, 4000, 16, -1, 0},                   // Holy Light: 1.5 s
                {10278, 180000, -1, -1, 0},                 // Blessing of Protection
                {20930, 10000, -1, -1, 0},                  // Holy Shock
                {10318, 12000, 1, -1, SR},                  // Divine Storm (Holy Wrath)
                {20729, 30000, -1, -1, REFIT_REPLACE},      // Blessing of Sacrifice: half of it, 12 s
                {20773, -1, -1, -1, REFIT_IN_COMBAT},       // Rekindle (Redemption): in combat with it in the kit
                {20066, 45000, -1, -1, 0},                  // Repentance
                {10310, 300000, -1, -1, REFIT_NO_DRAIN},    // Lay on Hands
                {20216, 180000, -1, -1, REFIT_REPLACE},     // Avenging Crusade (Divine Favor)
                {1020, 180000, -1, -1, SR},                 // Bastion of Light (Divine Shield)
                {19752, 180000, -1, -1, SR},                // Solar Ascension (Divine Intervention)
                // Hunter.
                {14325, 10000, -1, -1, 0},                  // Mark Prey (Hunter's Mark)
                {5384, 30000, -1, -1, 0},                   // Feign Death
                {14273, 15000, -1, -1, SR | REFIT_NO_WEAPON},   // Disengage
                {6197, 1000, -1, -1, SR | REFIT_NOT_CHANNEL},   // Field Glass (Eagle Eye)
                {14311, 15000, -1, -1, REFIT_IN_COMBAT},    // Freezing Trap
                {19884, 20000, -1, -1, 0},                  // Bane (Track Undead)
                {1543, 20000, -1, -1, REFIT_AT_FEET},       // Flare: at the hunter's feet
                {6533, 25000, 1, -1, 0},                    // Net Launcher (Net)
                {16075, 6000, 1, -1, REFIT_REPLACE},        // Throwing Axes
                {5858, 1000, -1, -1, SR},                   // Camouflage: still to fade
                {13809, 15000, -1, -1, REFIT_IN_COMBAT},    // Tar Trap (Frost Trap)
                {19801, 15000, 1, -1, REFIT_REPLACE | REFIT_NO_WEAPON},     // Purge (Tranquilizing Shot)
                {13608, 20000, 1, -1, SR},                  // Grappling Hook (Hooked Net, on oneself)
                {9437, 15000, 1, -1, REFIT_SELF},           // Bear Trap (its trap stays vanilla)
                {19503, 30000, 1, -1, REFIT_NO_WEAPON},     // Scatter (Scatter Shot)
                {7964, 40000, -1, -1, SR},                  // Smoke Pellet / Smoke Bomb
                {4071, 40000, 1, -1, REFIT_SELF},           // Noise-maker (Target Dummy: the dummy stays vanilla)
                {6346, 45000, -1, -1, SR},                  // Hex Ward (Fear Ward)
                {19885, 180000, -1, -1, 0},                 // Overwatch (Track Hidden)
                {14317, 180000, -1, -1, REFIT_IN_COMBAT},   // Killing Field (Explosive Trap)
                {14301, 180000, -1, -1, SR},                // Witchfire (Immolation Trap Effect)
                // Rogue.
                {1787, 6000, -1, -1, 0},                    // Stealth
                {11305, 60000, -1, -1, 0},                  // Sprint
                {1857, 120000, -1, -1, 0},                  // Vanish
                {11269, 8000, -1, -1, REFIT_NO_WEAPON | REFIT_ANY_SIDE},    // Ambush: the opener's gate is ours
                {25351, 15000, 1, -1, SR},                  // Deadly Poison (Deadly Poison V)
                {11294, 5000, -1, -1, REFIT_NO_WEAPON},     // Sinister Strike
                {1833, 20000, -1, -1, REFIT_NO_COMBO},      // Cheap Shot
                {22120, 12000, 1, 4, SR},                   // Lunge (Charge): 30 yd, our dash
                {11290, 20000, -1, -1, REFIT_ANY_SIDE},     // Garrote
                {11202, 3000, 1, -1, SR},                   // Poison Satchel (Crippling Poison)
                {14251, 10000, -1, -1, SR | REFIT_NO_AURA_STATE},   // Riposte: a guard, no parry needed
                {1725, 30000, -1, -1, SR},                  // Distract: a point ahead, ours
                {5167, 20000, 1, -1, SR},                   // Light Fingers (Pick Pocket's copy)
                {13877, 30000, -1, -1, SR},                 // Blade Flurry
                {14185, 180000, -1, -1, SR},                // Preparation
                {17773, 45000, -1, -1, SR},                 // Decoy (Summon Illusions)
                {17348, 120000, -1, -1, 0},                 // Deathmark (Hemorrhage)
                {24681, 120000, -1, -1, SR},                // Hall of Mirrors
                {13750, 120000, -1, -1, SR},                // Adrenaline Rush
                // Priest.
                {10901, 6000, -1, -1, 0},                   // Power Word: Shield
                {19243, 120000, -1, -1, 0},                 // Desperate Prayer
                {10942, 30000, -1, -1, 0},                  // Fade
                {10894, 3000, -1, -1, 0},                   // Shadow Word: Pain
                {10917, 2000, 4, -1, 0},                    // Flash Heal: 1 s
                {16051, 25000, 1, -1, SR},                  // Barrier (Barrier of Light)
                {10947, 8000, -1, -1, 0},                   // Mind Blast
                {25315, 4000, -1, -1, 0},                   // Renew
                {14751, 60000, -1, -1, REFIT_REPLACE},      // Pain Suppression (Inner Focus)
                {15286, 30000, -1, -1, SR},                 // Vampiric Embrace: on the priest, orbs for time
                {25316, 10000, 5, -1, 0},                   // Prayer of Healing: 2 s
                {10890, 30000, -1, -1, 0},                  // Psychic Scream
                {19280, 60000, -1, -1, 0},                  // Devouring Plague
                {27871, 60000, -1, -1, 0},                  // Lightwell
                {10912, 90000, 5, -1, 0},                   // Mind Control: 2 s
                {15487, 40000, -1, -1, 0},                  // Silence
                {15473, 120000, -1, -1, REFIT_REPLACE_THIRD}, // Shadowform: 30% less of all, not 15% of physical
                {27805, 180000, -1, -1, SR},                // Divine Hymn (Holy Nova)
                {22850, 180000, -1, -1, SR},                // Sanctum (Sanctuary)
                // Shaman.
                {2645, 5000, 1, -1, 0},                     // Ghost Wolf: instant
                {8012, 8000, -1, -1, 0},                    // Purge
                {15208, 2000, 16, -1, 0},                   // Lightning Bolt: 1.5 s (the basic spell too)
                {16316, 10000, -1, -1, SR},                 // Stone Fists (Rockbiter Weapon)
                {10463, 20000, -1, -1, 0},                  // Healing Stream Totem
                {10605, 8000, 5, -1, 0},                    // Chain Lightning: 2 s
                {17364, 10000, -1, -1, 0},                  // Stormstrike
                {2484, 20000, -1, -1, 0},                   // Earthbind Totem
                {10473, 8000, -1, -1, 0},                   // Frost Shock
                {10414, 12000, -1, -1, 0},                  // Earth Shock
                {8177, 25000, -1, -1, 0},                   // Grounding Totem
                {16166, 60000, -1, -1, 0},                  // Elemental Mastery
                {12734, 20000, -1, -1, SR},                 // Ground Smash
                {10623, 4000, 5, -1, 0},                    // Chain Heal: 2 s
                {15112, 30000, -1, -1, 0},                  // Windwall Totem
                {19798, 120000, 5, -1, SR},                 // Earthquake
                {4934, 180000, -1, -1, REFIT_SELF},         // Avatar of Stone (its stone form stays vanilla)
                {17359, 180000, -1, -1, SR},                // Ancestral Circle (Mana Tide Totem)
                // Mage.
                {1953, 15000, -1, -1, 0},                   // Blink
                {11958, 180000, -1, -1, 0},                 // Ice Block
                {12826, 30000, -1, -1, 0},                  // Polymorph
                {25306, 2000, 5, -1, 0},                    // Fireball: 2 s (the basic spell too)
                {10199, 10000, -1, -1, SR},                 // Flame Blade (Fire Blast)
                {28773, 20000, -1, -1, SR},                 // Warding Rune (Glyph of Deflection)
                {25304, 2000, 16, -1, 0},                   // Frostbolt: 1.5 s
                {14514, 15000, -1, -1, REFIT_REPLACE},      // Blade Blink (a Blink copy)
                {24815, 30000, 1, -1, SR | REFIT_NOT_CHANNEL},  // Healing Glyph (Draw Ancient Glyphs)
                {25345, 6000, -1, -1, 0},                   // Arcane Missiles
                {10230, 20000, -1, -1, 0},                  // Frost Nova
                {18960, 60000, 1, -1, SR | REFIT_IN_COMBAT},    // Blink Pads (Teleport: Moonglade)
                {13021, 30000, -1, -1, 0},                  // Blast Wave
                {10193, 25000, -1, -1, REFIT_REPLACE},      // Mana Shield: a pool, not mana
                {10054, 60000, 1, -1, REFIT_REPLACE},       // Conjure Supplies (Conjure Mana Ruby): Arcane Stims
                {11129, 90000, -1, -1, REFIT_REPLACE},      // Combustion: three spells crit and weave
                {10721, 45000, 1, -1, SR},                  // Arcane Familiar (Summon Elven Wisp)
                {24340, 90000, 5, -1, SR},                  // Meteor: 2 s
                {12042, 180000, -1, -1, 0},                 // Arcane Avatar (Arcane Power)
                {12051, 180000, -1, -1, SR | REFIT_NOT_CHANNEL},    // Grand Array (Evocation)
                // Warlock.
                {6215, 20000, -1, -1, 0},                   // Fear
                {11689, 10000, -1, -1, 0},                  // Life Tap
                {20757, 900000, 1, -1, REFIT_KEEP_REAGENT | REFIT_REPLACE},  // Soulstone: a stone on an ally, no item
                {25311, 3000, 1, -1, 0},                    // Corruption: instant
                {11735, 30000, -1, -1, 0},                  // Fel Hide (Demon Armor)
                {17473, 3000, 1, -1, SR},                   // Raise Skeleton (Raise Dead)
                {18223, 8000, -1, -1, 0},                   // Curse of Exhaustion
                {24530, 20000, -1, -1, SR},                 // Fel Leap (Felfire): no target needed
                {17616, 6000, -1, -1, SR},                  // Corpse Explosion: the custom part finds the corpse
                {11700, 10000, -1, -1, 0},                  // Drain Life
                {11684, 20000, -1, -1, SR | REFIT_NOT_CHANNEL},  // Hellfire: 10 s on the move
                {11445, 20000, -1, -1, REFIT_REPLACE},      // Bone Armor
                {17928, 40000, -1, -1, 0},                  // Howl of Terror
                {17926, 45000, -1, -1, 0},                  // Death Coil
                {28353, 10000, 1, -1, SR},                  // Raise Ghoul (a Raise Dead copy)
                {603, 60000, -1, -1, 0},                    // Curse of Doom
                {8872, 180000, -1, -1, REFIT_SELF},         // Demon Form (its fel guard look stays vanilla)
                {28422, 120000, -1, -1, SR},                // Abomination (Summon Type B)
                // Druid. The new forms' spells are rewritten as shapeshifts besides (fusion/DruidForms.h).
                {22812, 60000, -1, -1, 0},                  // Barkskin
                {9853, 20000, 1, -1, 0},                    // Entangling Roots: instant
                {783, 5000, -1, -1, 0},                     // Travel Form
                {25299, 3000, -1, -1, 0},                   // Rejuvenation
                {9835, 4000, -1, -1, 0},                    // Moonfire
                {9858, 4000, 16, -1, 0},                    // Regrowth: 1.5 s
                {25298, 6000, 1, 2, REFIT_REPLACE},         // Lunar Strike (Starfire): a melee strike, instant
                {21936, 1500, -1, -1, 0},                   // Stag Form (Reindeer, refitted: DruidForms)
                {18562, 12000, -1, -1, 0},                  // Swiftmend
                {9907, 10000, -1, -1, 0},                   // Faerie Fire
                {8153, 1500, 1, -1, 0},                     // Crow Form (Owl Form, refitted): instant
                {20748, 600000, 5, -1, 0},                  // Rebirth: 2 s
                {17402, 60000, -1, -1, REFIT_AT_FEET},      // Hurricane: centred on the druid
                {24084, 1500, -1, -1, 0},                   // Spider Form (Mar'li Transform, refitted)
                {12252, 20000, -1, -1, 0},                  // Web Spray
                {9863, 180000, -1, -1, 0},                  // Tranquility
                {7621, 1500, -1, -1, 0},                    // Worgen Form (Arugal's Curse, refitted: no charm, no curse)
                // Cat and bear abilities: no energy, rage or combo points, kept in their form.
                {9913, 6000, -1, -1, REFIT_KEEP_STANCE},    // Prowl
                {9827, 20000, -1, -1, REFIT_KEEP_STANCE},   // Pounce: 3 s from Prowl
                {9904, 6000, -1, -1, REFIT_KEEP_STANCE},    // Rake
                {9881, 6000, -1, -1, REFIT_KEEP_STANCE | REFIT_FREE | REFIT_REPLACE | REFIT_NOT_NEXT_SWING},  // Maul: 12 %EH at once
                {8983, 45000, -1, -1, REFIT_KEEP_STANCE | REFIT_FREE},  // Bash
                {6795, 10000, -1, -1, REFIT_KEEP_STANCE | REFIT_FREE},  // Growl
                {16979, 15000, -1, -1, 0},                  // Feral Charge (the stag's charge, the worgen's leap)
                {775, 180000, -1, -1, SR},                  // Tree of Life: a treant's look, not Tree Form's stun
                {20687, 120000, -1, -1, SR | REFIT_NOT_CHANNEL},    // Starfall
                // The basic spells (BasicSpellOf; Fireball's and Lightning Bolt's are above).
                {10314, 15000, -1, -1, REFIT_ANY_TARGET},   // Exorcism: any enemy, not only undead and demons
                {10934, 2000, -1, -1, 0},                   // Smite
                {25307, 2000, -1, -1, 0},                   // Shadow Bolt
                {9912, 2000, -1, -1, 0},                    // Wrath
            };
            return rows;
        }
    }

    std::vector<Refit> const& Refits()
    {
        return RefitRows();
    }

    uint8_t ReplaceMask(uint32_t flags)
    {
        if (flags & REFIT_REPLACE)
            return 0x7;
        return uint8_t((flags & REFIT_REPLACE_FIRST ? 0x1 : 0) | (flags & REFIT_REPLACE_SECOND ? 0x2 : 0)
            | (flags & REFIT_REPLACE_THIRD ? 0x4 : 0));
    }

    Refit const* RefitOf(uint32_t spell)
    {
        for (Refit const& r : RefitRows())
            if (r.spell == spell)
                return &r;
        return nullptr;
    }

    uint8_t TierLevel(uint8_t tier)
    {
        return tier <= kTiers ? kLevels[tier] : 255;
    }

    uint32_t TierPrice(uint8_t tier)
    {
        return tier <= kTiers ? kGold[tier] * kCopperPerGold : 0;
    }

    uint32_t PureBuildPrice()
    {
        uint32_t sum = 0;
        for (uint8_t t = 1; t <= kTiers; ++t)
            sum += TierPrice(t);
        return sum;
    }

    BuyError CanBuy(uint8_t cls, uint8_t level, uint64_t money, std::set<uint16_t> const& learned, uint16_t id)
    {
        Option const* o = FindOption(id);
        if (!o)
            return BuyError::UnknownOption;
        if (o->cls != cls)
            return BuyError::WrongClass;
        if (o->tier == kCoreTier)
            return BuyError::Core;
        if (!Available(id))
            return BuyError::Unavailable;
        if (learned.count(id))
            return BuyError::AlreadyLearned;
        if (level < TierLevel(o->tier))
            return BuyError::TooLow;
        if (money < TierPrice(o->tier))
            return BuyError::NoMoney;
        return BuyError::None;
    }

    PickError ValidatePick(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, uint8_t tier, uint16_t id,
        bool admin)
    {
        if (!id)
            return PickError::None;
        Option const* o = FindOption(id);
        if (!o)
            return PickError::UnknownOption;
        if (o->cls != cls)
            return PickError::WrongClass;
        if (o->tier != tier || tier == kCoreTier)
            return PickError::WrongTier;
        if (admin)
            return PickError::None;
        if (!Available(id))
            return PickError::Unavailable;
        if (level < TierLevel(tier))
            return PickError::TierLocked;
        if (!learned.count(id))
            return PickError::NotLearned;
        return PickError::None;
    }

    PickError ValidatePicks(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, Picks const& picks,
        uint8_t* badTier, bool admin)
    {
        for (uint8_t i = 0; i < kTiers; ++i)
        {
            PickError e = ValidatePick(cls, level, learned, uint8_t(i + 1), picks[i], admin);
            if (e != PickError::None)
            {
                if (badTier)
                    *badTier = uint8_t(i + 1);
                return e;
            }
        }
        return PickError::None;
    }

    Picks Sanitized(uint8_t cls, uint8_t level, std::set<uint16_t> const& learned, Picks const& picks, bool admin)
    {
        Picks out = picks;
        for (uint8_t i = 0; i < kTiers; ++i)
            if (ValidatePick(cls, level, learned, uint8_t(i + 1), out[i], admin) != PickError::None)
                out[i] = 0;
        return out;
    }

    bool AutoSlot(Picks& picks, Option const& o)
    {
        if (o.tier < 1 || o.tier > kTiers || picks[o.tier - 1])
            return false;
        picks[o.tier - 1] = o.id;
        return true;
    }

    std::vector<Option const*> KitOptions(uint8_t cls, Picks const& picks)
    {
        std::vector<Option const*> out;
        for (Option const* o : CoreOf(cls))
            if (o)
                out.push_back(o);
        for (uint8_t i = 0; i < kTiers; ++i)
            if (Option const* o = FindOption(picks[i]); o && o->cls == cls && o->tier == i + 1)
                out.push_back(o);
        return out;
    }

    bool InKit(uint8_t cls, Picks const& picks, uint16_t id)
    {
        for (Option const* o : KitOptions(cls, picks))
            if (o->id == id)
                return true;
        return false;
    }

    uint32_t RankForLevel(std::vector<Rank> const& chain, uint8_t level)
    {
        if (chain.empty())
            return 0;
        uint32_t best = chain.front().spell;
        for (Rank const& r : chain)
            if (r.level <= level)
                best = r.spell;
        return best;
    }

    std::vector<std::pair<uint32_t, uint32_t>> Successors(std::vector<Rank> const& chain)
    {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        for (size_t i = 0; i + 1 < chain.size(); ++i)
            if (chain[i].spell && chain[i + 1].spell)
                out.push_back({chain[i].spell, chain[i + 1].spell});
        return out;
    }

    bool InChain(std::vector<Rank> const& chain, uint32_t spell)
    {
        return spell && std::any_of(chain.begin(), chain.end(), [&](Rank const& r) { return r.spell == spell; });
    }

    uint32_t SingleEntry(std::vector<Rank> const& chain, uint32_t spell, std::function<bool(uint32_t)> const& known)
    {
        if (!InChain(chain, spell))
            return 0;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            if (known(it->spell))
                return it->spell;
        return 0;
    }

    std::vector<std::pair<uint8_t, uint32_t>> CollapseBar(std::vector<std::pair<uint8_t, uint32_t>> const& buttons,
        std::vector<std::vector<Rank>> const& chains, uint8_t level, std::function<bool(uint32_t)> const& known)
    {
        std::vector<std::pair<uint8_t, uint32_t>> moves;
        for (auto const& [slot, spell] : buttons)
            for (std::vector<Rank> const& chain : chains)
            {
                if (!InChain(chain, spell))
                    continue;
                uint32_t const entry = RankForLevel(chain, level);
                if (entry != spell && known(entry))
                    moves.push_back({slot, entry});
                break;
            }
        return moves;
    }

    std::array<uint32_t, 12> BarLayout(uint8_t cls, Picks const& picks)
    {
        std::array<uint32_t, 12> bar{};
        // A passive has no button; a core passive (Reincarnation) leaves its slot empty so the
        // picks start at 3 for every class (at 4 for a caster: its basic spell is in 3).
        std::array<Option const*, 3> const core = CoreOf(cls);
        for (size_t i = 0; i < core.size(); ++i)
            if (core[i] && core[i]->kind != Kind::Passive)
                bar[i] = core[i]->spells[0];
        size_t slot = 3;
        uint32_t const basic = BasicSpellOf(cls);
        if (basic)
            bar[slot++] = basic;
        for (Option const* o : KitOptions(cls, picks))
            if (o->tier != kCoreTier && o->kind != Kind::Passive && slot < bar.size()
                && o->spells[0] != basic)       // a pick of the basic spell (Lightning Bolt) has its button
                bar[slot++] = o->spells[0];
        for (uint32_t s : UtilityOf(cls))
        {
            if (slot >= bar.size())
                break;
            bar[slot++] = s;
        }
        return bar;
    }

    bool KeepSpell(SpellFacts const& f)
    {
        if (f.kit || f.revive)
            return true;
        if (f.profession || f.racial || f.proficiency)
            return true;
        return !f.classSkillLine && !f.talent;
    }

    char const* ResourceName(Resource r)
    {
        switch (r)
        {
            case Resource::HolyPower: return "Holy Power";
            case Resource::Static: return "Static";
            case Resource::LifeForce: return "Life Force";
            case Resource::Momentum: return "Momentum";
            default: return "";
        }
    }

    std::vector<Resource> AllResources()
    {
        return {Resource::HolyPower, Resource::Static, Resource::LifeForce, Resource::Momentum};
    }

    std::pair<Resource, uint16_t> CastCost(uint16_t option)
    {
        Spec const* s = SpecOf(option);
        if (!s || s->resource == Resource::None || !s->cost)
            return {Resource::None, 0};
        return {s->resource, s->cost};
    }

    std::vector<Resource> ArchetypeResources(uint8_t cls, uint8_t archetype)
    {
        switch (cls * 10 + archetype)
        {
            case kPaladin * 10 + 0: return {Resource::HolyPower};
            case kShaman * 10 + 0: return {Resource::Static};
            case kWarlock * 10 + 2: return {Resource::LifeForce};
            case kRogue * 10 + 2: return {Resource::Momentum};
            default: return {};
        }
    }

    std::vector<Resource> KitResources(uint8_t cls, Picks const& picks)
    {
        std::vector<Resource> out;
        for (uint8_t i = 0; i < kTiers; ++i)
        {
            Option const* o = FindOption(picks[i]);
            if (!o || o->cls != cls || o->tier != i + 1)
                continue;
            for (Resource r : ArchetypeResources(cls, o->archetype))
                if (std::find(out.begin(), out.end(), r) == out.end())
                    out.push_back(r);
        }
        return out;
    }

    uint16_t ResourceMax(Resource r, uint8_t cls, Picks const& picks)
    {
        auto has = [&](uint16_t id) { return InKit(cls, picks, id); };
        switch (r)
        {
            case Resource::HolyPower: return has(OptionId(kPaladin, 3, 0)) ? 5 : 3;
            case Resource::Static: return has(OptionId(kShaman, 5, 0)) ? 8 : 5;
            case Resource::LifeForce: return has(OptionId(kWarlock, 5, 2)) ? 150 : 100;
            case Resource::Momentum: return has(OptionId(kRogue, 5, 2)) ? kMomentumRelentless : kMomentumMax;
            default: return 0;
        }
    }

    uint16_t Meter::Add(uint16_t n)
    {
        uint16_t const before = value;
        value = uint16_t(std::min<uint32_t>(uint32_t(value) + n, max));
        return uint16_t(value - before);
    }

    bool Meter::Spend(uint16_t n)
    {
        if (value < n)
            return false;
        value = uint16_t(value - n);
        return true;
    }

    uint16_t Meter::SpendAll()
    {
        uint16_t const v = value;
        value = 0;
        return v;
    }

    namespace
    {
        // Where the segment p + t d (t in 0..1) first meets the sphere, if it does.
        bool SegmentSphere(Vec3 p, Vec3 q, Vec3 c, float r, float& tHit)
        {
            Vec3 const d = q - p;
            Vec3 const m = p - c;
            float const a = d.Dot(d);
            float const b = m.Dot(d);
            float const cc = m.Dot(m) - r * r;
            if (cc <= 0.f)
            {
                tHit = 0.f;
                return true;
            }
            if (a <= 1e-9f)
                return false;
            float const disc = b * b - a * cc;
            if (disc < 0.f)
                return false;
            float const t = (-b - std::sqrt(disc)) / a;
            if (t < 0.f || t > 1.f)
                return false;
            tHit = t;
            return true;
        }
    }

    bool InArc(Vec3 holder, float facing, float halfAngle, Vec3 from)
    {
        float const dx = from.x - holder.x, dy = from.y - holder.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f)
            return true;
        float const cosTo = (dx * std::cos(facing) + dy * std::sin(facing)) / len;
        return cosTo >= std::cos(halfAngle);
    }

    bool Blocks(Blocker const& b, Vec3 from, Vec3 to, float* at)
    {
        if (b.broken)
            return false;
        switch (b.shape)
        {
            case BlockShape::Arc:
            {
                // The holder is hit; the shot is stopped if it came from in front.
                if (!InArc(b.center, b.facing, b.halfAngle, from))
                    return false;
                if (at)
                    *at = 1.f;
                return true;
            }
            case BlockShape::Dome:
            {
                // Fired from inside: out it goes (allies fire out).
                if ((from - b.center).Length() <= b.radius)
                    return false;
                float t = 0.f;
                if (!SegmentSphere(from, to, b.center, b.radius, t))
                    return false;
                if (at)
                    *at = t;
                return true;
            }
            case BlockShape::Wall:
            {
                // The wall's front normal; a round must cross the plane from the front.
                Vec3 const n{std::cos(b.facing), std::sin(b.facing), 0.f};
                float const df = (from - b.center).Dot(n);
                float const dt = (to - b.center).Dot(n);
                if (df <= 0.f || dt > 0.f)
                    return false;
                float const t = df / (df - dt);
                Vec3 const hit = from + (to - from) * t;
                Vec3 const side{-n.y, n.x, 0.f};
                float const lateral = std::fabs((hit - b.center).Dot(side));
                float const up = hit.z - b.center.z;
                if (lateral > b.radius || up < -0.5f || up > b.height)
                    return false;
                if (at)
                    *at = t;
                return true;
            }
        }
        return false;
    }

    bool Wear(Blocker& b, float damage)
    {
        if (b.health <= 0.f)
            return false;           // unbreakable
        b.health -= std::max(0.f, damage);
        if (b.health > 0.f)
            return false;
        b.health = 0.f;
        b.untilMs = 0;
        b.broken = true;
        return true;
    }

    bool SightBlocked(Smoke const& s, Vec3 a, Vec3 b)
    {
        float t = 0.f;
        if ((b - s.center).Length() <= s.radius)
            return true;
        return SegmentSphere(a, b, s.center, s.radius, t);
    }

    namespace
    {
        void Put8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
        void Put16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }
        void Put32(std::vector<uint8_t>& o, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void Put64(std::vector<uint8_t>& o, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void PutF(std::vector<uint8_t>& o, float f)
        {
            uint32_t v = 0;
            std::memcpy(&v, &f, 4);
            Put32(o, v);
        }

        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            uint8_t U8()
            {
                if (at + 1 > b.size())
                    return ok = false, uint8_t(0);
                return b[at++];
            }
            uint16_t U16()
            {
                if (at + 2 > b.size())
                    return ok = false, uint16_t(0);
                uint16_t v = uint16_t(b[at] | (b[at + 1] << 8));
                at += 2;
                return v;
            }
        };
    }

    std::vector<uint8_t> EncodeState(State const& s)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpState);
        Put8(o, s.cls);
        Put8(o, s.level);
        Put8(o, s.kit);
        Put8(o, s.flags);
        for (uint16_t p : s.picks)
            Put16(o, p);
        uint8_t const n = uint8_t(std::min<size_t>(s.learned.size(), 255));
        Put8(o, n);
        for (size_t i = 0; i < n; ++i)
            Put16(o, s.learned[i]);
        return o;
    }

    bool DecodeState(std::vector<uint8_t> const& body, State& out)
    {
        Reader r{body};
        if (r.U8() != kOpState)
            return false;
        out.cls = r.U8();
        out.level = r.U8();
        out.kit = r.U8();
        out.flags = r.U8();
        for (uint16_t& p : out.picks)
            p = r.U16();
        uint8_t const n = r.U8();
        out.learned.clear();
        for (uint8_t i = 0; i < n; ++i)
            out.learned.push_back(r.U16());
        return r.ok && r.at == body.size();
    }

    std::vector<uint8_t> EncodeMeters(std::vector<Meter> const& meters)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpMeters);
        uint8_t const n = uint8_t(std::min<size_t>(meters.size(), 255));
        Put8(o, n);
        for (size_t i = 0; i < n; ++i)
        {
            Put8(o, uint8_t(meters[i].resource));
            Put16(o, meters[i].value);
            Put16(o, meters[i].max);
        }
        return o;
    }

    bool DecodeMeters(std::vector<uint8_t> const& body, std::vector<Meter>& out)
    {
        Reader r{body};
        if (r.U8() != kOpMeters)
            return false;
        uint8_t const n = r.U8();
        out.clear();
        for (uint8_t i = 0; i < n; ++i)
        {
            Meter m;
            m.resource = Resource(r.U8());
            m.value = r.U16();
            m.max = r.U16();
            if (!r.ok)
            {
                // A count the message doesn't hold: nothing made of it.
                out.clear();
                return false;
            }
            out.push_back(m);
        }
        return r.ok && r.at == body.size();
    }

    std::vector<uint8_t> EncodeSpotted(std::vector<Spot> const& spots)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpSpotted);
        uint8_t const n = uint8_t(std::min<size_t>(spots.size(), 255));
        Put8(o, n);
        for (size_t i = 0; i < n; ++i)
        {
            Put64(o, spots[i].guid);
            Put16(o, spots[i].ms);
        }
        return o;
    }

    std::vector<uint8_t> EncodePickResult(PickError e, uint8_t tier)
    {
        return {kOpPickResult, uint8_t(e), tier};
    }

    std::vector<uint8_t> EncodeSmoke(Smoke const& s, uint16_t ms)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpSmoke);
        PutF(o, s.center.x);
        PutF(o, s.center.y);
        PutF(o, s.center.z);
        PutF(o, s.radius);
        Put16(o, ms);
        return o;
    }

    Page MakePage(uint8_t cls)
    {
        Page p;
        p.cls = cls;
        for (uint8_t a = 0; a < kArchetypes; ++a)
            p.archetypes[a] = ArchetypeName(cls, a);
        auto add = [&](Option const* o)
        {
            if (o)
                p.entries.push_back({o->id, o->tier, o->archetype, o->kind, o->capstone, o->spells[0],
                    Available(o->id) ? std::string(o->name) : std::string(o->name) + " (unavailable)"});
        };
        for (Option const* o : CoreOf(cls))
            add(o);
        for (Option const* o : OptionsOf(cls))
            add(o);
        return p;
    }

    namespace
    {
        void PutString(std::vector<uint8_t>& o, std::string const& s)
        {
            uint8_t const n = uint8_t(std::min<size_t>(s.size(), 255));
            o.push_back(n);
            o.insert(o.end(), s.begin(), s.begin() + n);
        }

        bool GetString(Reader& r, std::string& out)
        {
            uint8_t const n = r.U8();
            if (!r.ok || r.at + n > r.b.size())
                return r.ok = false, false;
            out.assign(r.b.begin() + long(r.at), r.b.begin() + long(r.at + n));
            r.at += n;
            return true;
        }
    }

    std::vector<uint8_t> EncodePage(Page const& p)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpPage);
        Put8(o, p.cls);
        for (std::string const& a : p.archetypes)
            PutString(o, a);
        uint8_t const n = uint8_t(std::min<size_t>(p.entries.size(), 255));
        Put8(o, n);
        for (size_t i = 0; i < n; ++i)
        {
            PageEntry const& e = p.entries[i];
            Put16(o, e.id);
            Put8(o, e.tier);
            Put8(o, e.archetype);
            Put8(o, uint8_t(e.kind));
            Put8(o, e.capstone ? 1 : 0);
            Put32(o, e.spell);
            PutString(o, e.name);
        }
        return o;
    }

    bool DecodePage(std::vector<uint8_t> const& body, Page& out)
    {
        Reader r{body};
        if (r.U8() != kOpPage)
            return false;
        out = Page{};
        out.cls = r.U8();
        for (std::string& a : out.archetypes)
            if (!GetString(r, a))
                return false;
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            PageEntry e;
            e.id = r.U16();
            e.tier = r.U8();
            e.archetype = r.U8();
            e.kind = Kind(r.U8());
            e.capstone = r.U8() != 0;
            e.spell = uint32_t(r.U16()) | (uint32_t(r.U16()) << 16);
            if (!GetString(r, e.name))
                return false;
            out.entries.push_back(std::move(e));
        }
        return r.ok && r.at == body.size();
    }
    std::vector<std::vector<uint8_t>> EncodeSpellTweaks(std::vector<SpellTweak> const& tweaks, size_t maxBody)
    {
        std::vector<std::vector<uint8_t>> bodies;
        size_t const perBody = std::max<size_t>(1, (maxBody - 3) / kSpellTweakBytes);
        for (size_t first = 0; first < tweaks.size(); first += perBody)
        {
            size_t const n = std::min(perBody, tweaks.size() - first);
            std::vector<uint8_t> o;
            Put8(o, kOpSpellTweaks);
            Put16(o, uint16_t(n));
            for (size_t i = first; i < first + n; ++i)
            {
                SpellTweak const& t = tweaks[i];
                Put32(o, t.spell);
                Put32(o, t.attributes);
                Put32(o, t.attributesEx);
                Put32(o, t.stances);
                Put32(o, t.recoveryMs);
                Put32(o, t.categoryRecoveryMs);
                Put16(o, t.castIndex);
                Put16(o, t.rangeIndex);
                Put8(o, t.flags);
            }
            bodies.push_back(std::move(o));
        }
        return bodies;
    }

    bool DecodeSpellTweaks(std::vector<uint8_t> const& body, std::vector<SpellTweak>& out)
    {
        Reader r{body};
        if (r.U8() != kOpSpellTweaks)
            return false;
        uint16_t const n = r.U16();
        out.clear();
        auto u32 = [&r]() { return uint32_t(r.U16()) | (uint32_t(r.U16()) << 16); };
        for (uint16_t i = 0; i < n && r.ok; ++i)
        {
            SpellTweak t;
            t.spell = u32();
            t.attributes = u32();
            t.attributesEx = u32();
            t.stances = u32();
            t.recoveryMs = u32();
            t.categoryRecoveryMs = u32();
            t.castIndex = r.U16();
            t.rangeIndex = r.U16();
            t.flags = r.U8();
            out.push_back(t);
        }
        return r.ok && r.at == body.size();
    }
}
