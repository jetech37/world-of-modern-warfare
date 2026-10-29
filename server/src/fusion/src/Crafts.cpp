/*
 * World of Modern Warfare: Inscription and Jewelcrafting (fusion/Crafts.h). GPL-2.0-or-later,
 * see Common.h.
 */

#include "fusion/Crafts.h"

#include <algorithm>
#include <cmath>

namespace Fusion::Crafts
{
    namespace
    {
        using Stats::Out;

        TierDef const kTiers[] = {
            {Tier::None, "", 0, 0, 0, 0},
            {Tier::Apprentice, "Apprentice", 75, 0, 5, 100},
            {Tier::Journeyman, "Journeyman", 150, 50, 10, 500},
            {Tier::Expert, "Expert", 225, 125, 20, 2500},
            {Tier::Artisan, "Artisan", 300, 200, 35, 10000},
        };

        // ── Wire helpers ──
        void PutU16(std::vector<uint8_t>& b, uint16_t v)
        {
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }
        void PutStr(std::vector<uint8_t>& b, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            b.push_back(uint8_t(n));
            b.insert(b.end(), s.begin(), s.begin() + long(n));
        }

        struct Reader
        {
            std::vector<uint8_t> const& in;
            size_t pos = 0;
            bool ok = true;
            bool Need(size_t n)
            {
                if (!ok || in.size() - pos < n)
                    ok = false;
                return ok;
            }
            uint8_t U8() { return Need(1) ? in[pos++] : 0; }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t const v = uint16_t(in[pos] | in[pos + 1] << 8);
                pos += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(in[pos + size_t(i)]) << (8 * i);
                pos += 4;
                return v;
            }
            std::string Str()
            {
                uint8_t const n = U8();
                if (!Need(n))
                    return {};
                std::string s(in.begin() + long(pos), in.begin() + long(pos + n));
                pos += n;
                return s;
            }
            bool Done() const { return ok && pos == in.size(); }
        };

        bool ValidTrade(uint8_t t) { return t >= 1 && t <= kTrades; }
        bool ValidTier(uint8_t t) { return t <= uint8_t(Tier::Artisan); }

        // ── The data ──
        Mods Handling(std::initializer_list<std::pair<Out, float>> list)
        {
            Mods m;
            for (auto const& [o, v] : list)
                m.handling[size_t(o)] = v;
            return m;
        }

        Mods With(Mods m, float Mods::*field, float v)
        {
            m.*field = v;
            return m;
        }

        std::vector<Recipe> BuildRecipes()
        {
            std::vector<Recipe> out;
            uint16_t nextId = 1;
            auto add = [&](Recipe r) {
                r.id = nextId++;
                out.push_back(std::move(r));
            };
            auto make = [&](Trade t, char const* group, char const* name, uint16_t req, uint32_t cost, uint32_t product,
                            std::vector<Material> reagents, uint8_t count = 1, uint32_t tool = 0, bool starter = false) {
                Recipe r;
                r.trade = t;
                r.kind = Kind::Make;
                r.group = group;
                r.name = name;
                r.required = req;
                r.learnCost = starter ? 0 : cost;
                r.starter = starter;
                r.product = product;
                r.productCount = count;
                r.reagents = std::move(reagents);
                r.tool = tool;
                add(std::move(r));
            };

            // ── Inscription ──
            constexpr Trade I = Trade::Inscription;
            // Milling: five of one herb into its pigment, two or three (Wrath's), each herb from
            // the skill its colour family asks. Every mill comes with the profession.
            struct HerbGroup
            {
                uint16_t req;
                uint32_t pigment;
                std::vector<std::pair<uint32_t, char const*>> herbs;
            };
            std::vector<HerbGroup> const herbs = {
                {1, Item::AlabasterPigment, {{2447, "Mill Peacebloom"}, {765, "Mill Silverleaf"}, {2449, "Mill Earthroot"}}},
                {25, Item::DuskyPigment, {{785, "Mill Mageroyal"}, {2450, "Mill Briarthorn"}, {2452, "Mill Swiftthistle"},
                    {2453, "Mill Bruiseweed"}, {3820, "Mill Stranglekelp"}}},
                {75, Item::GoldenPigment, {{3355, "Mill Wild Steelbloom"}, {3369, "Mill Grave Moss"}, {3356, "Mill Kingsblood"},
                    {3357, "Mill Liferoot"}}},
                {125, Item::EmeraldPigment, {{3818, "Mill Fadeleaf"}, {3821, "Mill Goldthorn"}, {3358, "Mill Khadgar's Whisker"},
                    {3819, "Mill Wintersbite"}}},
                {175, Item::VioletPigment, {{4625, "Mill Firebloom"}, {8831, "Mill Purple Lotus"}, {8836, "Mill Arthas' Tears"},
                    {8838, "Mill Sungrass"}, {8839, "Mill Blindweed"}, {8845, "Mill Ghost Mushroom"}, {8846, "Mill Gromsblood"}}},
                {225, Item::SilveryPigment, {{13464, "Mill Golden Sansam"}, {13463, "Mill Dreamfoil"},
                    {13465, "Mill Mountain Silversage"}, {13466, "Mill Plaguebloom"}, {13467, "Mill Icecap"}, {13468, "Mill Black Lotus"}}},
            };
            for (HerbGroup const& g : herbs)
                for (auto const& [herb, name] : g.herbs)
                {
                    Recipe r;
                    r.trade = I;
                    r.kind = Kind::Mill;
                    r.group = "Milling";
                    r.name = name;
                    r.required = g.req;
                    r.starter = true;
                    r.reagents = {{herb, 5}};
                    r.yields = {{g.pigment, 256 - 1, 2, 3}};
                    add(std::move(r));
                }

            // Inks: two pigments each.
            make(I, "Inks", "Moonglow Ink", 1, 0, Item::MoonglowInk, {{Item::AlabasterPigment, 2}}, 1, 0, true);
            make(I, "Inks", "Midnight Ink", 75, 500, Item::MidnightInk, {{Item::DuskyPigment, 2}});
            make(I, "Inks", "Lion's Ink", 100, 1000, Item::LionsInk, {{Item::GoldenPigment, 2}});
            make(I, "Inks", "Jadefire Ink", 150, 2500, Item::JadefireInk, {{Item::EmeraldPigment, 2}});
            make(I, "Inks", "Celestial Ink", 200, 5000, Item::CelestialInk, {{Item::VioletPigment, 2}});
            make(I, "Inks", "Shimmering Ink", 250, 10000, Item::ShimmeringInk, {{Item::SilveryPigment, 2}});

            // Scrolls: vanilla's own, from ink and parchment.
            struct ScrollTier
            {
                uint16_t req;
                uint32_t cost;
                uint32_t ink;
                uint32_t parchment;
                char const* suffix;
                std::array<uint32_t, 6> items;  // Stamina, Intellect, Spirit, Strength, Agility, Protection
            };
            char const* const kScroll[] = {"Stamina", "Intellect", "Spirit", "Strength", "Agility", "Protection"};
            std::vector<ScrollTier> const scrolls = {
                {15, 50, Item::MoonglowInk, Item::LightParchment, "", {1180, 955, 1181, 954, 3012, 3013}},
                {75, 800, Item::MidnightInk, Item::LightParchment, " II", {1711, 2290, 1712, 2289, 1477, 1478}},
                {125, 2000, Item::LionsInk, Item::ResilientParchment, " III", {4422, 4419, 4424, 4426, 4425, 4421}},
                {190, 5000, Item::JadefireInk, Item::ResilientParchment, " IV", {10307, 10308, 10306, 10310, 10309, 10305}},
            };
            static std::vector<std::string> scrollNames;   // the names outlive the table
            scrollNames.reserve(scrolls.size() * 6);
            for (ScrollTier const& t : scrolls)
                for (size_t i = 0; i < 6; ++i)
                {
                    scrollNames.push_back(std::string("Scroll of ") + kScroll[i] + t.suffix);
                    make(I, "Scrolls", scrollNames.back().c_str(), uint16_t(t.req + i * 5), t.cost, t.items[i],
                        {{t.ink, 1}, {t.parchment, 1}}, 1, 0, t.req == 15 && i < 3);
                }

            // Glyphs: in Glyphs()'s order, each at its own skill with its ink.
            struct GlyphRecipe
            {
                uint32_t item;
                uint16_t req;
                uint32_t ink;
                uint8_t inks;
            };
            std::vector<GlyphRecipe> const glyphs = {
                {94350, 50, Item::MoonglowInk, 2},      // Long Road
                {94351, 60, Item::MoonglowInk, 3},      // the Feather
                {94330, 80, Item::MidnightInk, 2},      // Quick Draw
                {94331, 90, Item::MidnightInk, 2},      // Steady Hand
                {94332, 105, Item::LionsInk, 2},        // Iron Skin
                {94352, 115, Item::LionsInk, 2},        // the Gravedigger
                {94333, 130, Item::LionsInk, 3},        // Second Wind
                {94334, 145, Item::JadefireInk, 2},     // the Sapper
                {94353, 155, Item::JadefireInk, 2},     // the Field Editor
                {94335, 165, Item::JadefireInk, 3},     // the Quartermaster
                {94336, 185, Item::JadefireInk, 3},     // Last Rites
                {94354, 195, Item::CelestialInk, 2},    // the Veteran
                {94337, 205, Item::CelestialInk, 2},    // the Hunter's Eye
                {94338, 220, Item::CelestialInk, 3},    // the Shadow
                {94340, 235, Item::CelestialInk, 3},    // Quickened Words
                {94341, 235, Item::CelestialInk, 3},    // the Bladesworn
                {94342, 235, Item::CelestialInk, 3},    // the Tracker
                {94355, 245, Item::ShimmeringInk, 2},   // the Sprinter
                {94339, 260, Item::ShimmeringInk, 3},   // the Heavy Gunner
            };
            for (GlyphRecipe const& g : glyphs)
            {
                GlyphDef const* d = GlyphOf(g.item);
                if (!d)
                    continue;
                make(I, d->kind == GlyphKind::Major ? "Major Glyphs" : "Minor Glyphs", d->name, g.req,
                    uint32_t(g.req) * 40, g.item,
                    {{g.ink, g.inks}, {g.req >= 125 ? Item::ResilientParchment : Item::LightParchment, 1}});
            }

            // ── Jewelcrafting ──
            constexpr Trade J = Trade::Jewelcrafting;
            // Prospecting: five of one ore into one or two gems (Wrath's odds, 1.12's gems).
            auto prospect = [&](char const* name, uint16_t req, uint32_t ore, std::vector<Yield> yields) {
                Recipe r;
                r.trade = J;
                r.kind = Kind::Prospect;
                r.group = "Prospecting";
                r.name = name;
                r.required = req;
                r.starter = true;
                r.reagents = {{ore, 5}};
                r.yields = std::move(yields);
                add(std::move(r));
            };
            prospect("Prospect Copper Ore", 20, 2770, {{818, 128, 1, 1}, {774, 128, 1, 1}, {1210, 128, 1, 1}});
            prospect("Prospect Tin Ore", 50, 2771, {{1210, 96, 1, 1}, {1206, 96, 1, 1}, {1705, 96, 1, 1}, {1529, 20, 1, 1}});
            prospect("Prospect Iron Ore", 125, 2772, {{1529, 96, 1, 1}, {1705, 64, 1, 1}, {3864, 96, 1, 1}, {7909, 32, 1, 1}});
            prospect("Prospect Mithril Ore", 175, 3858, {{3864, 96, 1, 1}, {7909, 96, 1, 1}, {7910, 64, 1, 1}, {12361, 16, 1, 1}});
            prospect("Prospect Thorium Ore", 250, 10620, {{7910, 96, 1, 1}, {12361, 80, 1, 1}, {12799, 80, 1, 1},
                {12364, 64, 1, 1}, {12800, 32, 1, 1}, {12363, 16, 1, 1}});

            // Settings and wire, from bars.
            make(J, "Materials", "Delicate Copper Wire", 1, 0, Item::CopperWire, {{2840, 2}}, 1, Item::JewelersKit, true);
            make(J, "Materials", "Bronze Setting", 50, 300, Item::BronzeSetting, {{2841, 2}}, 1, Item::JewelersKit);
            make(J, "Materials", "Mithril Filigree", 170, 2000, Item::MithrilFiligree, {{3860, 2}}, 1, Item::JewelersKit);
            make(J, "Materials", "Thorium Setting", 240, 5000, Item::ThoriumSetting, {{12359, 2}}, 1, Item::JewelersKit);

            // Cut gems, for the kits' guns (Gems()).
            struct Cut
            {
                uint32_t gem;
                uint16_t req;
                std::vector<Material> reagents;
            };
            std::vector<Cut> const cuts = {
                {94450, 20, {{818, 1}}},
                {94451, 20, {{774, 1}}},
                {94452, 50, {{1210, 1}}},
                {94453, 80, {{1206, 1}}},
                {94454, 100, {{1705, 1}}},
                {94455, 120, {{1529, 1}}},
                {94456, 150, {{3864, 1}}},
                {94457, 160, {{3864, 1}, {818, 1}}},
                {94458, 180, {{7909, 1}}},
                {94459, 200, {{7910, 1}}},
                {94460, 240, {{12799, 1}}},
                {94461, 250, {{12364, 1}}},
                {94462, 260, {{12361, 1}}},
                {94463, 270, {{7910, 2}}},
                {94464, 285, {{12363, 1}}},
                {94465, 290, {{12800, 1}, {12363, 1}}},
                {94466, 295, {{12800, 1}, {Item::ThoriumSetting, 1}}},
            };
            for (Cut const& c : cuts)
                if (GemDef const* g = GemOf(c.gem))
                    make(J, (g->color & GEM_META) ? "Meta Gems" : "Gems", g->name, c.req, uint32_t(c.req) * 30, c.gem,
                        c.reagents, 1, Item::JewelersKit, c.req <= 20);

            // Jewelry: vanilla stats on rings and necklaces (35_inscription_jewelcrafting.sql).
            make(J, "Jewelry", "Braided Copper Ring", 1, 0, 94490, {{Item::CopperWire, 2}}, 1, Item::JewelersKit, true);
            make(J, "Jewelry", "Woven Copper Ring", 30, 200, 94491, {{Item::CopperWire, 2}, {2840, 1}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Tigerseye Band", 55, 400, 94492, {{Item::CopperWire, 2}, {818, 1}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Malachite Pendant", 70, 600, 94493, {{Item::CopperWire, 2}, {774, 2}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Bronze Band of Force", 95, 900, 94494, {{Item::BronzeSetting, 1}, {1206, 1}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Jade Pendant of Guarding", 130, 1500, 94495, {{Item::BronzeSetting, 2}, {1529, 1}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Citrine Ring of Swiftness", 165, 3000, 94496, {{Item::MithrilFiligree, 1}, {3864, 2}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Aquamarine Signet", 200, 5000, 94497, {{Item::MithrilFiligree, 2}, {7909, 1}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Star Ruby Choker", 230, 8000, 94498, {{Item::MithrilFiligree, 2}, {7910, 2}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Sapphire Band of the Sage", 265, 12000, 94499, {{Item::ThoriumSetting, 1}, {12361, 2}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Opal Pendant of the Hunt", 280, 15000, 94500, {{Item::ThoriumSetting, 2}, {12799, 2}}, 1, Item::JewelersKit);
            make(J, "Jewelry", "Emerald Signet of Endurance", 300, 20000, 94501, {{Item::ThoriumSetting, 2}, {12364, 2}, {12800, 1}}, 1, Item::JewelersKit);
            return out;
        }

        std::vector<GlyphDef> BuildGlyphs()
        {
            std::vector<GlyphDef> g;
            auto major = [&](uint32_t item, char const* name, char const* text, Mods m, uint32_t classMask = 0) {
                g.push_back({item, GlyphKind::Major, name, text, classMask, m});
            };
            auto minor = [&](uint32_t item, char const* name, char const* text, Mods m) {
                g.push_back({item, GlyphKind::Minor, name, text, 0, m});
            };
            major(94330, "Glyph of the Quick Draw", "Weapon swaps 12% quicker; aiming down sights 4% slower.",
                Handling({{Out::SwapTime, -0.12f}, {Out::AdsTime, 0.04f}}));
            major(94331, "Glyph of the Steady Hand", "Hip fire 8% tighter; out of a sprint 5% slower.",
                Handling({{Out::HipSpread, -0.08f}, {Out::SprintOutTime, 0.05f}}));
            major(94332, "Glyph of Iron Skin", "15% less flinch from rounds that hit you; weapon swaps 5% slower.",
                Handling({{Out::Flinch, -0.15f}, {Out::SwapTime, 0.05f}}));
            major(94333, "Glyph of Second Wind", "Regeneration starts sooner (15% of its delay) but runs 5% slower.",
                Handling({{Out::RegenDelay, 0.15f}, {Out::RegenRate, -0.05f}}));
            major(94334, "Glyph of the Sapper", "Your flashes, stuns and smoke 10% stronger.",
                Handling({{Out::EquipmentStrength, 0.10f}}));
            major(94335, "Glyph of the Quartermaster", "The field rearm is ready a quarter sooner.",
                With(Mods{}, &Mods::fieldRearm, 0.75f));
            {
                Mods m;
                m.lastStandMs = 5000;
                major(94336, "Glyph of Last Rites", "Down in Last Stand, you bleed out 5 seconds later.", m);
            }
            major(94337, "Glyph of the Hunter's Eye", "You see stealth 3 yards farther.",
                With(Mods{}, &Mods::stealthDetectYards, 3.f));
            major(94338, "Glyph of the Shadow", "Creatures notice you 7% later.", With(Mods{}, &Mods::detection, 0.93f));
            major(94339, "Glyph of the Heavy Gunner", "An LMG's or sniper's weight slows you 10% less.",
                Handling({{Out::HeavyGunRelief, 0.10f}}));
            {
                Mods m = Handling({{Out::ReloadTime, 0.05f}});
                m.castTime = 0.94f;
                major(94340, "Glyph of Quickened Words", "Your spells cast 6% quicker; reloads 5% slower. Casters only.", m, kCasters);
            }
            {
                Mods m = Handling({{Out::AdsTime, 0.04f}});
                m.melee = 1.06f;
                major(94341, "Glyph of the Bladesworn", "Your weapon's melee hits 6% harder; aiming down sights 4% slower. "
                    "Warriors, paladins and rogues only.", m, ClassBit(1) | ClassBit(2) | ClassBit(4));
            }
            {
                Mods m;
                m.stealthDetectYards = 5.f;
                m.detection = 0.95f;
                major(94342, "Glyph of the Tracker", "You see stealth 5 yards farther and creatures notice you 5% later. "
                    "Hunters only.", m, ClassBit(3));
            }
            minor(94350, "Glyph of the Long Road", "Out of combat you run 3% faster.", With(Mods{}, &Mods::outOfCombatSpeed, 1.03f));
            minor(94351, "Glyph of the Feather", "Falls hurt 20% less.", With(Mods{}, &Mods::fallDamage, 0.8f));
            minor(94352, "Glyph of the Gravedigger", "10% more gold looted from creatures.", With(Mods{}, &Mods::goldLoot, 1.1f));
            {
                Mods m;
                m.fieldEditorSeconds = 600;
                minor(94353, "Glyph of the Field Editor", "The field editor is ready 10 minutes sooner.", m);
            }
            minor(94354, "Glyph of the Veteran", "3% more experience from kills.", With(Mods{}, &Mods::killXp, 1.03f));
            minor(94355, "Glyph of the Sprinter", "Out of a sprint 4% quicker.", Handling({{Out::SprintOutTime, -0.04f}}));
            return g;
        }

        std::vector<GemDef> BuildGems()
        {
            auto one = [](Out o, float v) { return std::array<GemEffect, 2>{GemEffect{o, v}, GemEffect{}}; };
            auto two = [](Out a, float x, Out b, float y) { return std::array<GemEffect, 2>{GemEffect{a, x}, GemEffect{b, y}}; };
            return {
                {94450, "Bold Tigerseye", GEM_RED, one(Out::AdsTime, -0.03f), "Aim down sights 3% quicker."},
                {94451, "Solid Malachite", GEM_BLUE, one(Out::Recoil, -0.03f), "3% less recoil."},
                {94452, "Shifting Shadowgem", GEM_RED | GEM_BLUE, two(Out::HipSpread, -0.02f, Out::Flinch, -0.02f),
                    "Hip fire 2% tighter, 2% less flinch."},
                {94453, "Jagged Moss Agate", GEM_YELLOW | GEM_BLUE, two(Out::SprintOutTime, -0.03f, Out::Sway, -0.02f),
                    "Out of a sprint 3% quicker, 2% less sway."},
                {94454, "Lustrous Moonstone", GEM_BLUE, one(Out::Sway, -0.04f), "4% less scope sway."},
                {94455, "Quick Jade", GEM_YELLOW, one(Out::ReloadTime, -0.03f), "Reloads 3% quicker."},
                {94456, "Rigid Citrine", GEM_YELLOW, one(Out::SwapTime, -0.04f), "Weapon swaps 4% quicker."},
                {94457, "Inscribed Citrine", GEM_RED | GEM_YELLOW, two(Out::AdsTime, -0.02f, Out::ReloadTime, -0.02f),
                    "Aim 2% quicker, reload 2% quicker."},
                {94458, "Sparkling Aquamarine", GEM_BLUE, one(Out::Recoil, -0.05f), "5% less recoil."},
                {94459, "Bright Star Ruby", GEM_RED, one(Out::AdsTime, -0.05f), "Aim down sights 5% quicker."},
                {94460, "Smooth Large Opal", GEM_YELLOW, one(Out::ReloadTime, -0.05f), "Reloads 5% quicker."},
                {94461, "Radiant Huge Emerald", GEM_YELLOW | GEM_BLUE, two(Out::SprintOutTime, -0.04f, Out::Bloom, -0.03f),
                    "Out of a sprint 4% quicker, 3% less bloom."},
                {94462, "Stormy Blue Sapphire", GEM_BLUE, one(Out::Flinch, -0.08f), "8% less flinch."},
                {94463, "Runed Star Ruby", GEM_RED, one(Out::HipSpread, -0.05f), "Hip fire 5% tighter."},
                {94464, "Prismatic Arcane Crystal", GEM_PRISMATIC, one(Out::Bloom, -0.04f),
                    "4% less bloom. Counts as every colour."},
                {94465, "Relentless Azerothian Diamond", GEM_META, one(Out::RegenDelay, 0.08f),
                    "Regeneration starts sooner (8% of its delay). Wakes with gems of two colours in the gun."},
                {94466, "Swift Azerothian Diamond", GEM_META, one(Out::WeakSpotTaken, 0.05f),
                    "5% of weak-spot hits on you turned to body hits. Wakes with gems of two colours in the gun."},
            };
        }

        bool IsMultiplier(Out o) { return uint8_t(o) < uint8_t(Out::HeavyGunRelief); }
    }

    // ── Trades and tiers ──
    uint16_t SkillKey(Trade t)
    {
        switch (t)
        {
            case Trade::Inscription: return kSkillInscription;
            case Trade::Jewelcrafting: return kSkillJewelcrafting;
            default: return 0;
        }
    }

    Trade TradeOfSkill(uint16_t key)
    {
        if (key == kSkillInscription)
            return Trade::Inscription;
        if (key == kSkillJewelcrafting)
            return Trade::Jewelcrafting;
        return Trade::None;
    }

    char const* TradeName(Trade t)
    {
        switch (t)
        {
            case Trade::Inscription: return "Inscription";
            case Trade::Jewelcrafting: return "Jewelcrafting";
            default: return "";
        }
    }

    Trade TradeOfTrainer(uint32_t entry)
    {
        if (entry == kTrainerInscription)
            return Trade::Inscription;
        if (entry == kTrainerJewelcrafting)
            return Trade::Jewelcrafting;
        return Trade::None;
    }

    TierDef const& TierOf(Tier t)
    {
        size_t const i = size_t(t);
        return kTiers[i < sizeof(kTiers) / sizeof(kTiers[0]) ? i : 0];
    }

    uint16_t SkillCap(Tier t) { return TierOf(t).cap; }

    TrainCheck CanTrainTier(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, uint32_t freeSlots, bool gm)
    {
        if (want == Tier::None || uint8_t(have) >= uint8_t(want))
            return TrainCheck::AlreadyKnown;
        if (want != Construction::Next(have))
            return TrainCheck::NotNext;
        if (gm)
            return TrainCheck::Ok;
        TierDef const& d = TierOf(want);
        if (have == Tier::None && freeSlots == 0)
            return TrainCheck::NoFreeSlot;
        if (skill < d.skillToLearn)
            return TrainCheck::SkillTooLow;
        if (level < d.levelToLearn)
            return TrainCheck::LevelTooLow;
        if (money < d.costCopper)
            return TrainCheck::NotEnoughMoney;
        return TrainCheck::Ok;
    }

    Tier TierHolding(uint16_t skill)
    {
        Tier t = Tier::Apprentice;
        while (SkillCap(t) < skill && t != Tier::Artisan)
            t = Construction::Next(t);
        return t;
    }

    std::vector<Ware> const& Wares(Trade t)
    {
        static std::vector<Ware> const inscription = {{Item::LightParchment, 15, 1}, {Item::ResilientParchment, 120, 1}};
        static std::vector<Ware> const jewelcrafting = {{Item::JewelersKit, 300, 1}};
        static std::vector<Ware> const none;
        switch (t)
        {
            case Trade::Inscription: return inscription;
            case Trade::Jewelcrafting: return jewelcrafting;
            default: return none;
        }
    }

    // ── Recipes ──
    std::vector<Recipe> const& Recipes()
    {
        static std::vector<Recipe> const all = BuildRecipes();
        return all;
    }

    Recipe const* RecipeOf(uint16_t id)
    {
        std::vector<Recipe> const& all = Recipes();
        // Ids are 1.. in order.
        if (id >= 1 && id <= all.size() && all[id - 1].id == id)
            return &all[id - 1];
        for (Recipe const& r : all)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    std::vector<Recipe const*> RecipesOf(Trade t)
    {
        std::vector<Recipe const*> out;
        for (Recipe const& r : Recipes())
            if (r.trade == t)
                out.push_back(&r);
        return out;
    }

    Thresholds SkillOf(Recipe const& r) { return Construction::Spaced(r.required); }

    bool Known(Recipe const& r, std::vector<uint16_t> const& learned)
    {
        return r.starter || std::find(learned.begin(), learned.end(), r.id) != learned.end();
    }

    LearnCheck CanLearn(Recipe const& r, Trade at, Tier tier, uint16_t skill, bool known, uint32_t money, bool gm)
    {
        if (r.trade != at)
            return LearnCheck::WrongTrade;
        if (tier == Tier::None)
            return LearnCheck::NotLearned;
        if (known || r.starter)
            return LearnCheck::Known;
        if (gm)
            return LearnCheck::Ok;
        if (skill < r.required)
            return LearnCheck::SkillTooLow;
        if (money < r.learnCost)
            return LearnCheck::NotEnoughMoney;
        return LearnCheck::Ok;
    }

    MakeCheck CanMake(Recipe const& r, Tier tier, bool known, uint16_t skill, Inventory const& bags, bool gm)
    {
        if (tier == Tier::None && !gm)
            return MakeCheck::NotLearned;
        if (!known && !gm)
            return MakeCheck::Unknown;
        if (skill < r.required && !gm)
            return MakeCheck::SkillTooLow;
        if (gm)
            return MakeCheck::Ok;
        if (r.tool)
        {
            auto it = bags.find(r.tool);
            if (it == bags.end() || it->second == 0)
                return MakeCheck::NoTool;
        }
        if (!Construction::CanAfford(bags, r.reagents))
            return MakeCheck::NoReagents;
        return MakeCheck::Ok;
    }

    uint16_t MakeSkillUp(Recipe const& r, uint16_t skill, uint16_t cap, float roll)
    {
        return Construction::ApplySkillUp(skill, cap, SkillOf(r), roll);
    }

    size_t RollBytes(Recipe const& r) { return r.yields.size() * 2; }

    std::vector<Draw> Roll(Recipe const& r, std::vector<uint8_t> const& rolls)
    {
        std::vector<Draw> out;
        if (r.yields.empty())
            return out;
        auto byte = [&](size_t i) -> uint8_t { return i < rolls.size() ? rolls[i] : 0; };
        auto count = [&](Yield const& y, uint8_t b) {
            uint32_t const span = uint32_t(std::max(y.max, y.min) - y.min) + 1;
            return uint32_t(y.min) + uint32_t(b) % span;
        };
        for (size_t i = 0; i < r.yields.size(); ++i)
        {
            Yield const& y = r.yields[i];
            if (byte(i * 2) < y.chance)
                out.push_back({y.item, count(y, byte(i * 2 + 1))});
        }
        if (out.empty())
            out.push_back({r.yields[0].item, count(r.yields[0], byte(1))});
        // Prospecting gives at most two gems.
        if (r.kind == Kind::Prospect && out.size() > 2)
            out.resize(2);
        return out;
    }

    // ── Mods ──
    bool Mods::operator==(Mods const& o) const
    {
        return handling == o.handling && castTime == o.castTime && melee == o.melee && detection == o.detection
            && stealthDetectYards == o.stealthDetectYards && fallDamage == o.fallDamage && goldLoot == o.goldLoot
            && outOfCombatSpeed == o.outOfCombatSpeed && killXp == o.killXp && fieldRearm == o.fieldRearm
            && fieldEditorSeconds == o.fieldEditorSeconds && lastStandMs == o.lastStandMs;
    }

    void Combine(Mods& into, Mods const& m)
    {
        for (size_t i = 0; i < into.handling.size(); ++i)
            into.handling[i] += m.handling[i];
        into.castTime *= m.castTime;
        into.melee *= m.melee;
        into.detection *= m.detection;
        into.stealthDetectYards += m.stealthDetectYards;
        into.fallDamage *= m.fallDamage;
        into.goldLoot *= m.goldLoot;
        into.outOfCombatSpeed *= m.outOfCombatSpeed;
        into.killXp *= m.killXp;
        into.fieldRearm *= m.fieldRearm;
        into.fieldEditorSeconds += m.fieldEditorSeconds;
        into.lastStandMs += m.lastStandMs;
    }

    // ── Glyphs ──
    std::vector<GlyphDef> const& Glyphs()
    {
        static std::vector<GlyphDef> const all = BuildGlyphs();
        return all;
    }

    GlyphDef const* GlyphOf(uint32_t item)
    {
        for (GlyphDef const& g : Glyphs())
            if (g.item == item)
                return &g;
        return nullptr;
    }

    uint8_t SlotLevel(GlyphKind k, uint8_t slot)
    {
        if (slot >= kGlyphSlots)
            return 255;
        return k == GlyphKind::Major ? kMajorLevels[slot] : kMinorLevels[slot];
    }

    GlyphCheck CanInscribe(KitGlyphs const& kit, GlyphKind kind, uint8_t slot, uint32_t item, uint8_t level, uint8_t wowClass)
    {
        if (slot >= kGlyphSlots || uint8_t(kind) > uint8_t(GlyphKind::Minor))
            return GlyphCheck::BadSlot;
        GlyphDef const* g = GlyphOf(item);
        if (!g)
            return GlyphCheck::NotAGlyph;
        if (g->kind != kind)
            return GlyphCheck::WrongKind;
        if (level < SlotLevel(kind, slot))
            return GlyphCheck::SlotLocked;
        if (g->classMask && !(g->classMask & ClassBit(wowClass)))
            return GlyphCheck::WrongClass;
        auto const& slots = kind == GlyphKind::Major ? kit.major : kit.minor;
        for (uint8_t i = 0; i < kGlyphSlots; ++i)
            if (i != slot && slots[i] == item)
                return GlyphCheck::AlreadyInKit;
        return GlyphCheck::Ok;
    }

    Mods GlyphMods(KitGlyphs const& kit, uint8_t level, uint8_t wowClass)
    {
        Mods m;
        auto add = [&](std::array<uint32_t, kGlyphSlots> const& slots, GlyphKind kind) {
            std::vector<uint32_t> seen;
            for (uint8_t i = 0; i < kGlyphSlots; ++i)
            {
                GlyphDef const* g = GlyphOf(slots[i]);
                if (!g || g->kind != kind || level < SlotLevel(kind, i))
                    continue;
                if (g->classMask && !(g->classMask & ClassBit(wowClass)))
                    continue;
                if (std::find(seen.begin(), seen.end(), g->item) != seen.end())
                    continue;
                seen.push_back(g->item);
                Combine(m, g->mods);
            }
        };
        add(kit.major, GlyphKind::Major);
        add(kit.minor, GlyphKind::Minor);
        return m;
    }

    // ── Gems ──
    std::vector<GemDef> const& Gems()
    {
        static std::vector<GemDef> const all = BuildGems();
        return all;
    }

    GemDef const* GemOf(uint32_t item)
    {
        for (GemDef const& g : Gems())
            if (g.item == item)
                return &g;
        return nullptr;
    }

    bool Tunes(GemDef const& g, Stats::Out o)
    {
        return std::any_of(g.effects.begin(), g.effects.end(), [&](GemEffect const& e) { return e.out == o; });
    }

    bool SocketExists(GunSlot g, uint8_t socket, uint16_t jewelcrafting)
    {
        if (g == GunSlot::Secondary)
            return socket == 0;
        if (socket == kJewelerSocket)
            return jewelcrafting >= kJewelerSocketSkill;
        return socket < kSockets;
    }

    bool Scoped(WeaponClass c, std::vector<std::string> const& attachments)
    {
        if (c == WeaponClass::Sniper)
            return true;
        return std::any_of(attachments.begin(), attachments.end(),
            [](std::string const& a) { return a == "acog" || a == "thermal"; });
    }

    namespace
    {
        // Two gems are of one kind when they tune any handling alike.
        bool SameKind(GemDef const& a, GemDef const& b)
        {
            for (GemEffect const& e : a.effects)
                if (e.out != Out::Count && Tunes(b, e.out))
                    return true;
            return false;
        }

        bool SwayGem(GemDef const& g) { return Tunes(g, Out::Sway); }
    }

    GemCheck CanSocket(KitGems const& kit, GunSlot g, uint8_t socket, uint32_t item, GunInfo const& gun, uint16_t jewelcrafting)
    {
        GemDef const* gem = GemOf(item);
        if (!gem)
            return GemCheck::NotAGem;
        if (!gun.present)
            return GemCheck::NoGun;
        if (socket >= kSockets || !SocketExists(g, socket, jewelcrafting))
            return GemCheck::NoSocket;
        bool const meta = (gem->color & GEM_META) != 0;
        if (socket == kMetaSocket && !meta)
            return GemCheck::MetaOnly;
        if (socket != kMetaSocket && meta)
            return GemCheck::NotMeta;
        if (SwayGem(*gem) && !gun.scoped)
            return GemCheck::NeedsScope;
        auto const& sockets = kit.Of(g);
        for (uint8_t i = 0; i < kSockets; ++i)
        {
            if (i == socket)
                continue;
            GemDef const* other = GemOf(sockets[i]);
            if (other && SameKind(*gem, *other))
                return GemCheck::SameKind;
        }
        return GemCheck::Ok;
    }

    bool MetaAwake(std::array<uint32_t, kSockets> const& sockets)
    {
        uint8_t colors = 0;
        for (uint8_t i = 0; i < kSockets; ++i)
            if (i != kMetaSocket)
                if (GemDef const* g = GemOf(sockets[i]))
                    colors |= g->color & GEM_PRISMATIC;
        int n = 0;
        for (uint8_t bit : {GEM_RED, GEM_YELLOW, GEM_BLUE})
            n += (colors & bit) ? 1 : 0;
        return n >= 2;
    }

    Mods GemMods(KitGems const& kit, GunSlot g, GunInfo const& gun, uint16_t jewelcrafting)
    {
        Mods m;
        if (!gun.present)
            return m;
        auto const& sockets = kit.Of(g);
        bool const awake = MetaAwake(sockets);
        std::vector<GemDef const*> counted;
        for (uint8_t i = 0; i < kSockets; ++i)
        {
            GemDef const* gem = GemOf(sockets[i]);
            if (!gem || !SocketExists(g, i, jewelcrafting))
                continue;
            bool const meta = (gem->color & GEM_META) != 0;
            if (meta != (i == kMetaSocket) || (meta && !awake))
                continue;
            if (SwayGem(*gem) && !gun.scoped)
                continue;
            // One of each kind: a second gem tuning the same handling counts nothing.
            if (std::any_of(counted.begin(), counted.end(), [&](GemDef const* c) { return SameKind(*c, *gem); }))
                continue;
            counted.push_back(gem);
            for (GemEffect const& e : gem->effects)
                if (e.out != Out::Count)
                    m.handling[size_t(e.out)] += e.amount;
        }
        return m;
    }

    void ApplyHandling(Stats::Derived& d, Mods const& m)
    {
        constexpr float kMaxMultiplier = 1.25f;
        for (size_t i = 0; i < size_t(Out::Count); ++i)
        {
            float const v = m.handling[i];
            if (v == 0.f)
                continue;
            Out const o = Out(i);
            float const limit = Stats::OutputLimit(o);
            if (IsMultiplier(o))
                d.value[i] = std::clamp(d.value[i] * (1.f + v), limit, std::max(kMaxMultiplier, d.value[i]));
            else
            {
                float const was = d.value[i];
                d.value[i] = std::clamp(was + v, 0.f, std::max(limit, was));
                float const moved = d.value[i] - was;
                if (o == Out::RegenDelay)
                    d.regenDelay = std::max(Stats::kRegenMinDelay, d.regenDelay - Stats::kRegenDelay * moved);
                else if (o == Out::RegenRate && 1.f + was > 0.f)
                    d.regenRate *= (1.f + d.value[i]) / (1.f + was);
            }
        }
    }

    // ── The wire ──
    std::vector<uint8_t> EncodeWindow(Window const& w)
    {
        std::vector<uint8_t> b{kOpWindow, uint8_t(w.trade), uint8_t(w.open), uint8_t(w.trainer), uint8_t(w.tier)};
        PutU16(b, w.skill);
        PutU16(b, SkillCap(w.tier));
        b.push_back(w.tone);
        PutStr(b, w.message);
        size_t const n = std::min(w.services.size(), kMaxServices);
        b.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Service const& s = w.services[i];
            b.push_back(uint8_t(s.action));
            PutU32(b, s.arg);
            PutU32(b, s.cost);
            b.push_back(uint8_t(s.enabled));
            b.push_back(s.hue);
            PutStr(b, s.label);
            PutStr(b, s.note);
        }
        return b;
    }

    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out)
    {
        Reader r{body};
        if (r.U8() != kOpWindow)
            return false;
        Window w;
        uint8_t const trade = r.U8();
        w.open = r.U8() != 0;
        w.trainer = r.U8() != 0;
        uint8_t const tier = r.U8();
        if (!ValidTrade(trade) || !ValidTier(tier))
            return false;
        w.trade = Trade(trade);
        w.tier = Tier(tier);
        w.skill = r.U16();
        r.U16();
        w.tone = r.U8();
        w.message = r.Str();
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Service s;
            s.action = Action(r.U8());
            s.arg = r.U32();
            s.cost = r.U32();
            s.enabled = r.U8() != 0;
            s.hue = r.U8();
            s.label = r.Str();
            s.note = r.Str();
            w.services.push_back(std::move(s));
        }
        if (!r.Done())
            return false;
        out = std::move(w);
        return true;
    }

    std::vector<uint8_t> EncodeAct(Trade t, Action a, uint32_t arg)
    {
        std::vector<uint8_t> b{kOpAct, uint8_t(t), uint8_t(a)};
        PutU32(b, arg);
        return b;
    }

    bool DecodeAct(std::vector<uint8_t> const& body, Trade& t, Action& a, uint32_t& arg)
    {
        Reader r{body};
        if (r.U8() != kOpAct)
            return false;
        uint8_t const trade = r.U8();
        uint8_t const action = r.U8();
        uint32_t const v = r.U32();
        if (!r.Done() || !ValidTrade(trade) || action < 1 || action > uint8_t(Action::Unlearn))
            return false;
        t = Trade(trade);
        a = Action(action);
        arg = v;
        return true;
    }

    uint32_t MakeArg(uint16_t recipe, uint16_t count) { return uint32_t(recipe) | uint32_t(count) << 16; }
    uint16_t MakeRecipeOf(uint32_t arg) { return uint16_t(arg & 0xFFFF); }
    uint16_t MakeCountOf(uint32_t arg) { return std::clamp<uint16_t>(uint16_t(arg >> 16), 1, kMaxMakeBatch); }

    std::vector<BookEntry> Book(Trade t, std::vector<uint16_t> const& learned)
    {
        std::vector<BookEntry> out;
        for (Recipe const* r : RecipesOf(t))
        {
            BookEntry e;
            e.id = r->id;
            e.kind = r->kind;
            e.skill = SkillOf(*r);
            e.known = Known(*r, learned);
            e.product = r->kind == Kind::Make ? r->product : (r->yields.empty() ? 0 : r->yields[0].item);
            e.count = r->productCount;
            e.group = r->group;
            e.name = r->name;
            e.reagents = r->reagents;
            e.tool = r->tool;
            out.push_back(std::move(e));
        }
        return out;
    }

    std::vector<uint8_t> EncodeBook(Trade t, std::vector<BookEntry> const& book)
    {
        std::vector<uint8_t> b{kOpBook, uint8_t(t)};
        size_t const n = std::min<size_t>(book.size(), 0xFFFF);
        PutU16(b, uint16_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            BookEntry const& e = book[i];
            PutU16(b, e.id);
            b.push_back(uint8_t(e.kind));
            PutU16(b, e.skill.required);
            PutU16(b, e.skill.yellow);
            PutU16(b, e.skill.green);
            PutU16(b, e.skill.gray);
            b.push_back(uint8_t(e.known));
            PutU32(b, e.product);
            b.push_back(e.count);
            PutStr(b, e.group);
            PutStr(b, e.name);
            size_t const k = std::min<size_t>(e.reagents.size(), 255);
            b.push_back(uint8_t(k));
            for (size_t j = 0; j < k; ++j)
            {
                PutU32(b, e.reagents[j].item);
                PutU16(b, uint16_t(std::min<uint32_t>(e.reagents[j].count, 0xFFFF)));
            }
            PutU32(b, e.tool);
        }
        return b;
    }

    bool DecodeBook(std::vector<uint8_t> const& body, Trade& t, std::vector<BookEntry>& out)
    {
        Reader r{body};
        if (r.U8() != kOpBook)
            return false;
        uint8_t const trade = r.U8();
        if (!ValidTrade(trade))
            return false;
        std::vector<BookEntry> book;
        uint16_t const n = r.U16();
        for (uint16_t i = 0; i < n && r.ok; ++i)
        {
            BookEntry e;
            e.id = r.U16();
            e.kind = Kind(r.U8());
            e.skill.required = r.U16();
            e.skill.yellow = r.U16();
            e.skill.green = r.U16();
            e.skill.gray = r.U16();
            e.known = r.U8() != 0;
            e.product = r.U32();
            e.count = r.U8();
            e.group = r.Str();
            e.name = r.Str();
            uint8_t const k = r.U8();
            for (uint8_t j = 0; j < k && r.ok; ++j)
            {
                Material m;
                m.item = r.U32();
                m.count = r.U16();
                e.reagents.push_back(m);
            }
            e.tool = r.U32();
            book.push_back(std::move(e));
        }
        if (!r.Done())
            return false;
        t = Trade(trade);
        out = std::move(book);
        return true;
    }

    std::vector<uint8_t> EncodeSkill(Trade t, Tier tier, uint16_t skill)
    {
        std::vector<uint8_t> b{kOpSkill, uint8_t(t), uint8_t(tier)};
        PutU16(b, skill);
        PutU16(b, SkillCap(tier));
        return b;
    }

    bool DecodeSkill(std::vector<uint8_t> const& body, Trade& t, Tier& tier, uint16_t& skill, uint16_t& cap)
    {
        Reader r{body};
        if (r.U8() != kOpSkill)
            return false;
        uint8_t const trade = r.U8();
        uint8_t const tr = r.U8();
        uint16_t const s = r.U16();
        uint16_t const c = r.U16();
        if (!r.Done() || !ValidTrade(trade) || !ValidTier(tr))
            return false;
        t = Trade(trade);
        tier = Tier(tr);
        skill = s;
        cap = c;
        return true;
    }

    std::vector<uint8_t> EncodeAugment(AugmentAsk const& a)
    {
        std::vector<uint8_t> b{kOpAugment, a.what, a.kind, a.slot};
        PutU32(b, a.item);
        return b;
    }

    bool DecodeAugment(std::vector<uint8_t> const& body, AugmentAsk& out)
    {
        Reader r{body};
        if (r.U8() != kOpAugment)
            return false;
        AugmentAsk a;
        a.what = r.U8();
        a.kind = r.U8();
        a.slot = r.U8();
        a.item = r.U32();
        if (!r.Done() || a.what > 1 || a.kind > 1)
            return false;
        if (a.what == 0 ? a.slot >= kGlyphSlots : a.slot >= kSockets)
            return false;
        out = a;
        return true;
    }

    std::vector<uint8_t> EncodeAugments(Augments const& a)
    {
        std::vector<uint8_t> b{kOpAugments, uint8_t(a.open), a.kit, uint8_t(a.canEdit), a.level};
        PutU16(b, a.jewelcrafting);
        for (uint32_t g : a.glyphs.major)
            PutU32(b, g);
        for (uint32_t g : a.glyphs.minor)
            PutU32(b, g);
        for (uint32_t g : a.gems.primary)
            PutU32(b, g);
        for (uint32_t g : a.gems.secondary)
            PutU32(b, g);
        b.push_back(a.flags);
        b.push_back(a.tone);
        PutStr(b, a.message);
        return b;
    }

    bool DecodeAugments(std::vector<uint8_t> const& body, Augments& out)
    {
        Reader r{body};
        if (r.U8() != kOpAugments)
            return false;
        Augments a;
        a.open = r.U8() != 0;
        a.kit = r.U8();
        a.canEdit = r.U8() != 0;
        a.level = r.U8();
        a.jewelcrafting = r.U16();
        for (uint32_t& g : a.glyphs.major)
            g = r.U32();
        for (uint32_t& g : a.glyphs.minor)
            g = r.U32();
        for (uint32_t& g : a.gems.primary)
            g = r.U32();
        for (uint32_t& g : a.gems.secondary)
            g = r.U32();
        a.flags = r.U8();
        a.tone = r.U8();
        a.message = r.Str();
        if (!r.Done())
            return false;
        out = std::move(a);
        return true;
    }
}
