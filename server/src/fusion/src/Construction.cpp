/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Construction.h"

#include <algorithm>
#include <cmath>

namespace Fusion { namespace Construction
{
    namespace
    {
        // Vanilla's trainers: the tier's cap, the skill and level to learn it, its price.
        TierDef const kTiers[] = {
            {Tier::None, "None", 0, 0, 0, 0},
            {Tier::Apprentice, "Apprentice", 75, 0, 5, 1000},
            {Tier::Journeyman, "Journeyman", 150, 50, 10, 5000},
            {Tier::Expert, "Expert", 225, 125, 20, 30000},
            {Tier::Artisan, "Artisan", 300, 200, 35, 100000},
        };

        using Build::PieceType;

        Recipe Make(PieceType t, uint16_t required, std::vector<Material> m)
        {
            return {t, Spaced(required), std::move(m)};
        }

        PieceType Prop(uint8_t id) { return PieceType(id); }

        // WoW's props and the whole structures: half again what they first cost, each count
        // rounded up, so a structure costs about what its pieces would.
        Recipe Pricier(PieceType t, uint16_t required, std::vector<Material> m)
        {
            for (Material& x : m)
                x.count = (x.count * 3 + 1) / 2;
            return Make(t, required, std::move(m));
        }

        // What a style adds to its pieces' recipes: Stormwind's dressed stone and slate, Ironforge's
        // copper fittings, the orcs' hides and lashings, the night elves' woven trim; two for a
        // wall, a floor, a roof, stairs or a door, one for the smaller things.
        Material StyleAccent(PieceType base, uint8_t style)
        {
            static uint32_t const items[Build::kStyleCount] = {0, kCoarseStone, kCopperBar, kLightLeather, kWoolCloth};
            bool small = false;
            switch (base)
            {
                case PieceType::WoodHalfWall:
                case PieceType::WoodBeam:
                case PieceType::StoneHalfWall:
                case PieceType::Sandbags:
                case PieceType::Campfire:
                case PieceType::Torch:
                case PieceType::Bedroll:
                    small = true;
                    break;
                default:
                    break;
            }
            return {items[style < Build::kStyleCount ? style : 0], small ? 1u : 2u};
        }

        // Wood early, from the first lumber; stone and the smiths' parts from Journeyman; the
        // door (a hinge) and the big props late, so a builder has something new to make at every
        // tier and a recipe to level on all the way to 300. Building is materially expensive:
        // there is no cap on pieces, so their cost and their upkeep (they decay and want repair,
        // Maintenance.h) are what keeps the world from filling up. A wood wall is five to eight
        // trees' lumber and a handful of the smith's nails, a stone one a pile of stone and
        // brackets; the camp's small things stay cheap. (The sentry gun is a killstreak now.)
        std::vector<Recipe> MakeRecipes()
        {
            std::vector<Recipe> r;
            r.push_back(Make(PieceType::WoodFloor, 1, {{kRoughLumber, 12}, {kCopperNails, 3}}));
            r.push_back(Make(PieceType::WoodWall, 20, {{kRoughLumber, 16}, {kCopperNails, 4}}));
            r.push_back(Make(PieceType::WoodHalfWall, 10, {{kRoughLumber, 10}, {kCopperNails, 2}}));
            r.push_back(Make(PieceType::WoodDoorway, 50, {{kRoughLumber, 16}, {kCopperNails, 6}}));
            r.push_back(Make(PieceType::WoodWindow, 60, {{kRoughLumber, 16}, {kCopperNails, 6}}));
            r.push_back(Make(PieceType::WoodRoof, 75, {{kSturdyLumber, 12}, {kCopperNails, 6}}));
            r.push_back(Make(PieceType::WoodStairs, 40, {{kRoughLumber, 16}, {kCopperNails, 6}}));
            r.push_back(Make(PieceType::WoodBeam, 1, {{kRoughLumber, 6}, {kCopperNails, 1}}));
            r.push_back(Make(PieceType::StoneFloor, 150, {{kHeavyStone, 20}, {kSturdyLumber, 6}}));
            r.push_back(Make(PieceType::StoneWall, 175, {{kSolidStone, 20}, {kIronBracket, 5}}));
            r.push_back(Make(PieceType::StoneHalfWall, 160, {{kHeavyStone, 16}, {kIronBracket, 3}}));
            r.push_back(Make(PieceType::Sandbags, 50, {{kLinenCloth, 12}, {kCoarseStone, 8}}));
            r.push_back(Make(PieceType::Campfire, 1, {{kRoughLumber, 4}, {kRoughStone, 3}}));
            r.push_back(Make(PieceType::Torch, 1, {{kRoughLumber, 1}, {kLinenCloth, 1}}));
            r.push_back(Make(PieceType::Bedroll, 25, {{kLinenCloth, 6}, {kLightLeather, 3}}));
            r.push_back(Make(PieceType::WoodDoor, 125, {{kSturdyLumber, 14}, {kBronzeHinge, 2}, {kCopperNails, 8}}));
            // The styled pieces (Build.h Styles), in type order: each its plain piece's recipe at the
            // same skill, and a little of its culture's trade goods.
            std::vector<Recipe> const plain = r;
            for (uint8_t style = 1; style < Build::kStyleCount; ++style)
                for (PieceType base : Build::StyledPieces())
                    for (Recipe const& b : plain)
                        if (b.piece == base)
                        {
                            Recipe styled = b;
                            styled.piece = Build::Styled(base, style);
                            Material const accent = StyleAccent(base, style);
                            bool merged = false;
                            for (Material& m : styled.materials)
                                if (m.item == accent.item)
                                {
                                    m.count += accent.count;
                                    merged = true;
                                }
                            if (!merged)
                                styled.materials.push_back(accent);
                            r.push_back(styled);
                        }
            // WoW's props, by id (Build.h Props()).
            r.push_back(Pricier(Prop(100), 1, {{kRoughLumber, 4}, {kRoughStone, 2}}));            // Campfire
            r.push_back(Pricier(Prop(101), 100, {{kSturdyLumber, 6}, {kWoolCloth, 4}}));           // Bed
            r.push_back(Pricier(Prop(102), 30, {{kRoughLumber, 4}, {kCopperNails, 2}}));           // Barrel
            r.push_back(Pricier(Prop(103), 15, {{kRoughLumber, 3}, {kCopperNails, 1}}));           // Crate
            r.push_back(Pricier(Prop(104), 80, {{kSturdyLumber, 4}, {kCopperNails, 2}}));          // Big crate
            r.push_back(Pricier(Prop(105), 20, {{kLinenCloth, 3}}));                               // Grain sack
            r.push_back(Pricier(Prop(106), 5, {{kRoughLumber, 2}, {kLinenCloth, 1}}));             // Haystack
            r.push_back(Pricier(Prop(107), 200, {{kHardwoodPlank, 10}, {kIronBracket, 4}}));       // Hay wagon
            r.push_back(Pricier(Prop(108), 220, {{kIronBar, 6}, {kHardwoodPlank, 4}, {kIronBracket, 2}}));  // Mine cart
            r.push_back(Pricier(Prop(109), 130, {{kIronBar, 2}, {kSturdyLumber, 2}, {kBronzeHinge, 1}}));   // Lamp post
            r.push_back(Pricier(Prop(110), 190, {{kIronBar, 6}, {kHeavyStone, 2}}));               // Anvil
            r.push_back(Pricier(Prop(111), 65, {{kRoughLumber, 5}, {kCopperNails, 2}}));           // Round table
            r.push_back(Pricier(Prop(112), 35, {{kRoughLumber, 4}, {kCopperNails, 2}}));           // Wood fence
            r.push_back(Pricier(Prop(113), 5, {{kRoughLumber, 1}}));                               // Fence post
            r.push_back(Pricier(Prop(114), 185, {{kSolidStone, 8}}));                              // Stone fence
            r.push_back(Pricier(Prop(115), 150, {{kHeavyStone, 2}}));                              // Stone fence post
            r.push_back(Pricier(Prop(116), 260, {{kIronwoodPlank, 12}, {kReinforcedPlate, 2}}));   // Spike wall
            r.push_back(Pricier(Prop(117), 70, {{kWoolCloth, 3}, {kSturdyLumber, 1}}));            // Banner
            r.push_back(Pricier(Prop(118), 240, {{kBoltOfMageweave, 6}, {kHardwoodPlank, 6}}));    // Large tent
            r.push_back(Pricier(Prop(119), 170, {{kBoltOfSilk, 6}, {kHardwoodPlank, 4}}));         // Tent
            r.push_back(Pricier(Prop(120), 180, {{kBoltOfSilk, 8}, {kHardwoodPlank, 4}}));         // Orc tent
            r.push_back(Pricier(Prop(121), 175, {{kBoltOfSilk, 6}, {kHardwoodPlank, 4}, {kBronzeHinge, 1}}));  // Goblin tent
            r.push_back(Pricier(Prop(122), 210, {{kHardwoodPlank, 10}, {kSolidStone, 6}}));        // Bonfire
            r.push_back(Pricier(Prop(123), 275, {{kDenseStone, 10}, {kThoriumBar, 2}}));           // Rock wall
            r.push_back(Pricier(Prop(124), 285, {{kIronwoodPlank, 8}, {kFortifiedPlate, 2}, {kIronBracket, 2}}));  // Gate
            r.push_back(Pricier(Prop(125), 250, {{kIronwoodPlank, 10}, {kBoltOfMageweave, 4}}));   // Water hut
            r.push_back(Pricier(Prop(126), 290, {{kDenseStone, 10}, {kIronwoodPlank, 6}, {kThoriumBar, 2}}));  // Well
            r.push_back(Pricier(Prop(127), 120, {{kBronzeHinge, 1}, {kSturdyLumber, 1}, {kLinenCloth, 1}}));   // Wall lantern
            // The portal (Build.h kPortalProp, Portals.h): an Artisan's work, ironwood and dense stone for its
            // arch, the smiths' heaviest plate, and the arcane goods that hold a way open.
            r.push_back(Make(Prop(128), 275, {{kIronwoodPlank, 10}, {kDenseStone, 20}, {kFortifiedPlate, 2}, {kArcaneCrystal, 2}, {kLargeBrilliantShard, 4}}));  // Portal
            // Whole structures (Build.h Structures()): a building's worth of lumber, stone and the
            // smiths' hardware, from a woodshed at Apprentice to a stone watchtower at Artisan.
            r.push_back(Pricier(Prop(140), 175, {{kSturdyLumber, 40}, {kHardwoodPlank, 10}, {kCopperNails, 20}, {kIronBracket, 4}}));  // Cottage
            r.push_back(Pricier(Prop(141), 60, {{kRoughLumber, 24}, {kCopperNails, 8}}));                                    // Shack
            r.push_back(Pricier(Prop(142), 265, {{kSolidStone, 40}, {kDenseStone, 20}, {kIronwoodPlank, 10}, {kFortifiedPlate, 2}}));  // Watchtower
            r.push_back(Pricier(Prop(143), 140, {{kSturdyLumber, 30}, {kRoughLumber, 20}, {kCopperNails, 12}, {kBronzeHinge, 2}}));    // Lookout tower
            r.push_back(Pricier(Prop(144), 200, {{kHardwoodPlank, 30}, {kSturdyLumber, 20}, {kCopperNails, 24}, {kIronBracket, 6}}));  // Barn
            r.push_back(Pricier(Prop(145), 160, {{kSturdyLumber, 30}, {kHardwoodPlank, 8}, {kCopperNails, 16}, {kIronBracket, 4}}));   // Stable
            r.push_back(Pricier(Prop(146), 225, {{kSolidStone, 30}, {kHardwoodPlank, 16}, {kIronBar, 10}, {kIronBracket, 6}}));        // Smithy
            r.push_back(Pricier(Prop(147), 180, {{kHeavyStone, 16}, {kIronBar, 6}, {kBronzeBar, 4}}));                         // Forge
            r.push_back(Pricier(Prop(148), 75, {{kRoughLumber, 12}, {kWoolCloth, 6}, {kCopperNails, 6}}));                     // Market stall
            r.push_back(Pricier(Prop(149), 110, {{kSturdyLumber, 16}, {kRoughLumber, 12}, {kCopperNails, 8}}));                // Palisade
            r.push_back(Pricier(Prop(150), 130, {{kSturdyLumber, 24}, {kRoughLumber, 12}, {kCopperNails, 10}, {kBronzeHinge, 2}}));    // Palisade gate
            r.push_back(Pricier(Prop(151), 120, {{kSturdyLumber, 24}, {kRoughLumber, 10}, {kCopperNails, 14}}));               // Footbridge
            r.push_back(Pricier(Prop(152), 240, {{kSolidStone, 40}, {kHeavyStone, 20}, {kIronBracket, 6}}));                   // Stone bridge
            r.push_back(Pricier(Prop(153), 90, {{kCoarseStone, 20}, {kRoughLumber, 6}, {kCopperBar, 2}}));                     // Village well
            r.push_back(Pricier(Prop(154), 150, {{kHeavyStone, 30}, {kSturdyLumber, 10}, {kIronBracket, 2}}));                 // Guard post
            r.push_back(Pricier(Prop(155), 30, {{kRoughLumber, 18}, {kCopperNails, 6}}));                                     // Woodshed
            // The second catalogue: a town's, a farm's and a camp's real buildings, each at the tier
            // its size and trade ask (a gazebo at Journeyman, an inn and a lighthouse at Artisan).
            r.push_back(Pricier(Prop(156), 270, {{kHardwoodPlank, 40}, {kSturdyLumber, 30}, {kSolidStone, 20}, {kIronBracket, 8}, {kBoltOfMageweave, 6}}));  // Inn
            r.push_back(Pricier(Prop(157), 215, {{kSturdyLumber, 36}, {kSolidStone, 24}, {kBronzeBar, 4}, {kCopperNails, 20}}));          // Chapel
            r.push_back(Pricier(Prop(158), 190, {{kSturdyLumber, 30}, {kHeavyStone, 16}, {kWoolCloth, 10}, {kBronzeHinge, 4}}));         // Windmill
            r.push_back(Pricier(Prop(159), 185, {{kSturdyLumber, 40}, {kHardwoodPlank, 8}, {kCopperNails, 24}, {kIronBracket, 4}}));      // Farmhouse
            r.push_back(Pricier(Prop(160), 205, {{kSolidStone, 20}, {kIronBar, 12}, {kSturdyLumber, 10}, {kIronBracket, 4}}));            // Blacksmith
            r.push_back(Pricier(Prop(161), 165, {{kSturdyLumber, 30}, {kRoughLumber, 20}, {kIronBar, 4}, {kBronzeHinge, 2}}));            // Lumber yard
            r.push_back(Pricier(Prop(162), 280, {{kDenseStone, 40}, {kSolidStone, 20}, {kHardwoodPlank, 10}, {kThoriumBar, 4}}));         // Lighthouse
            r.push_back(Pricier(Prop(163), 230, {{kSolidStone, 30}, {kHeavyStone, 16}, {kIronBracket, 4}}));                              // Gate arch
            r.push_back(Pricier(Prop(164), 245, {{kDenseStone, 24}, {kSolidStone, 16}, {kHardwoodPlank, 6}, {kIronBracket, 4}}));         // City wall
            r.push_back(Pricier(Prop(165), 125, {{kSturdyLumber, 28}, {kRoughLumber, 16}, {kCopperNails, 16}}));                          // Dock
            r.push_back(Pricier(Prop(166), 100, {{kSturdyLumber, 16}, {kRoughLumber, 10}, {kCopperNails, 10}, {kWoolCloth, 4}}));         // Gazebo
            r.push_back(Pricier(Prop(167), 195, {{kHardwoodPlank, 20}, {kLightLeather, 16}, {kSturdyLumber, 16}, {kIronBracket, 4}}));    // Orcish longhouse
            r.push_back(Pricier(Prop(168), 255, {{kDenseStone, 20}, {kSolidStone, 16}, {kBoltOfSilk, 6}, {kMithrilBar, 4}}));             // Moonwell shrine
            // Furniture (Furniture.h All()): a chair is a few planks and nails at Apprentice, a canopy bed or
            // a stuffed bear an Artisan's work; every one has a recipe to level on.
            r.push_back(Pricier(Prop(200), 1, {{kRoughLumber, 4}, {kCopperNails, 1}}));    // Wooden chair
            r.push_back(Pricier(Prop(201), 90, {{kSturdyLumber, 4}, {kWoolCloth, 3}, {kCopperNails, 2}}));    // Cushioned chair
            r.push_back(Pricier(Prop(202), 20, {{kRoughLumber, 6}, {kCopperNails, 2}}));    // Inn bench
            r.push_back(Pricier(Prop(203), 130, {{kHeavyStone, 6}, {kSturdyLumber, 2}}));    // Stormwind bench
            r.push_back(Pricier(Prop(204), 160, {{kHardwoodPlank, 4}, {kIronBracket, 1}, {kWoolCloth, 2}}));    // Dwarven chair
            r.push_back(Pricier(Prop(205), 5, {{kRoughLumber, 5}}));    // Log bench
            r.push_back(Pricier(Prop(206), 210, {{kHardwoodPlank, 5}, {kBoltOfSilk, 2}, {kBronzeHinge, 1}}));    // High-backed chair
            r.push_back(Pricier(Prop(207), 60, {{kSturdyLumber, 6}, {kWoolCloth, 4}, {kCopperNails, 3}}));    // Inn bed
            r.push_back(Pricier(Prop(208), 240, {{kHardwoodPlank, 10}, {kBoltOfMageweave, 6}, {kIronBracket, 2}}));    // Canopy bed
            r.push_back(Pricier(Prop(209), 110, {{kSturdyLumber, 10}, {kWoolCloth, 6}, {kCopperNails, 4}}));    // Bunk bed
            r.push_back(Pricier(Prop(210), 180, {{kHardwoodPlank, 8}, {kBoltOfSilk, 4}, {kIronBracket, 2}}));    // Dwarven bed
            r.push_back(Pricier(Prop(211), 30, {{kRoughLumber, 8}, {kCopperNails, 2}}));    // Inn table
            r.push_back(Pricier(Prop(212), 10, {{kRoughLumber, 5}, {kCopperNails, 1}}));    // Small table
            r.push_back(Pricier(Prop(213), 70, {{kSturdyLumber, 8}, {kCopperNails, 3}}));    // Long table
            r.push_back(Pricier(Prop(214), 150, {{kHardwoodPlank, 6}, {kBronzeHinge, 1}, {kCopperNails, 3}}));    // Writing desk
            r.push_back(Pricier(Prop(215), 40, {{kSturdyLumber, 6}, {kBronzeHinge, 2}, {kCopperNails, 4}}));    // Chest
            r.push_back(Pricier(Prop(216), 15, {{kRoughLumber, 6}, {kLightLeather, 2}, {kCopperNails, 2}}));    // Footlocker
            r.push_back(Pricier(Prop(217), 170, {{kHardwoodPlank, 10}, {kBronzeHinge, 2}, {kIronBracket, 2}}));    // Wardrobe
            r.push_back(Pricier(Prop(218), 50, {{kSturdyLumber, 8}, {kCopperNails, 3}}));    // Bookshelf
            r.push_back(Pricier(Prop(219), 200, {{kHardwoodPlank, 10}, {kIronBracket, 2}}));    // Abbey bookshelf
            r.push_back(Pricier(Prop(220), 25, {{kLinenCloth, 2}, {kLightLeather, 2}}));    // Book stack
            r.push_back(Pricier(Prop(221), 120, {{kSturdyLumber, 4}, {kLightLeather, 2}, {kCopperNails, 2}}));    // Lectern
            r.push_back(Pricier(Prop(222), 140, {{kIronBar, 8}, {kHeavyStone, 2}}));    // Smith's anvil
            r.push_back(Pricier(Prop(223), 185, {{kSolidStone, 12}, {kIronBar, 4}, {kBronzeBar, 2}}));    // Forge
            r.push_back(Pricier(Prop(224), 35, {{kRoughLumber, 4}, {kCoarseStone, 4}, {kCopperBar, 1}}));    // Cooking fire
            r.push_back(Pricier(Prop(225), 125, {{kSturdyLumber, 8}, {kWoolCloth, 4}, {kBronzeHinge, 1}}));    // Loom
            r.push_back(Pricier(Prop(226), 195, {{kHardwoodPlank, 6}, {kIronBar, 2}, {kBoltOfSilk, 2}}));    // Alchemy table
            r.push_back(Pricier(Prop(227), 100, {{kIronBar, 4}, {kSturdyLumber, 2}, {kBronzeHinge, 1}}));    // Mailbox
            r.push_back(Pricier(Prop(228), 45, {{kRoughLumber, 8}, {kLinenCloth, 4}}));    // Training dummy
            r.push_back(Pricier(Prop(229), 220, {{kSolidStone, 10}, {kMithrilBar, 2}}));    // Trophy plinth
            r.push_back(Pricier(Prop(230), 155, {{kSturdyLumber, 30}, {kCopperNails, 12}, {kWoolCloth, 4}}));    // Bard's stage
            r.push_back(Pricier(Prop(231), 65, {{kWoolCloth, 4}, {kSturdyLumber, 1}}));    // Stormwind banner
            r.push_back(Pricier(Prop(232), 65, {{kWoolCloth, 4}, {kSturdyLumber, 1}}));    // Horde banner
            r.push_back(Pricier(Prop(233), 80, {{kWoolCloth, 3}, {kLightLeather, 2}, {kSturdyLumber, 1}}));    // Clan banner
            r.push_back(Pricier(Prop(234), 55, {{kWoolCloth, 6}}));    // Stormwind rug
            r.push_back(Pricier(Prop(235), 230, {{kBoltOfMageweave, 6}}));    // Red rug
            r.push_back(Pricier(Prop(236), 115, {{kLightLeather, 8}}));    // Bearskin rug
            r.push_back(Pricier(Prop(237), 75, {{kWoolCloth, 4}, {kLightLeather, 2}}));    // Tauren rug
            r.push_back(Pricier(Prop(238), 1, {{kLinenCloth, 1}}));    // Candle
            r.push_back(Pricier(Prop(239), 85, {{kBronzeBar, 2}, {kLinenCloth, 2}}));    // Candelabra
            r.push_back(Pricier(Prop(240), 165, {{kIronBar, 4}, {kSolidStone, 2}}));    // Brazier
            r.push_back(Pricier(Prop(241), 135, {{kBronzeBar, 3}, {kLinenCloth, 2}}));    // Wall candelabra
            r.push_back(Pricier(Prop(242), 95, {{kBronzeHinge, 1}, {kCopperBar, 2}, {kLinenCloth, 1}}));    // Hanging lantern
            r.push_back(Pricier(Prop(243), 5, {{kLinenCloth, 1}}));    // Flowers
            r.push_back(Pricier(Prop(244), 105, {{kHeavyStone, 6}, {kRoughLumber, 2}}));    // Planter
            r.push_back(Pricier(Prop(245), 45, {{kRoughLumber, 4}, {kCopperBar, 1}}));    // Beer keg
            r.push_back(Pricier(Prop(246), 1, {{kRoughLumber, 1}}));    // Mug
            r.push_back(Pricier(Prop(247), 145, {{kSturdyLumber, 6}, {kIronBracket, 1}}));    // Weapon rack
            r.push_back(Pricier(Prop(248), 175, {{kIronBar, 4}, {kSturdyLumber, 1}}));    // Wall shield
            r.push_back(Pricier(Prop(249), 100, {{kLightLeather, 4}, {kSturdyLumber, 2}}));    // Mounted boar head
            r.push_back(Pricier(Prop(250), 250, {{kLightLeather, 16}, {kHardwoodPlank, 4}}));    // Stuffed bear
            r.push_back(Pricier(Prop(251), 190, {{kBoltOfSilk, 2}, {kHardwoodPlank, 2}}));    // Painting
            r.push_back(Pricier(Prop(252), 205, {{kBronzeBar, 3}, {kHardwoodPlank, 2}}));    // Globe
            r.push_back(Pricier(Prop(253), 215, {{kIronBar, 4}, {kHardwoodPlank, 2}}));    // Armor stand
            r.push_back(Pricier(Prop(254), 260, {{kIronBar, 10}, {kThoriumBar, 2}}));    // Potbelly stove
            return r;
        }

        // Lumber nodes: required, yellow, green, gray as mining's veins space them (a copper vein
        // is gray at 100), so a node of a tier keeps teaching into the next.
        std::vector<NodeDef> MakeNodes()
        {
            return {
                {Node::Softwood, 191001, 191001, "Softwood Tree", {1, 25, 50, 100}, kRoughLumber, 2, 4, 300, 450},
                {Node::SturdyTimber, 191002, 191002, "Sturdy Timber", {75, 100, 125, 175}, kSturdyLumber, 2, 4, 450, 600},
                {Node::Hardwood, 191003, 191003, "Hardwood Tree", {150, 175, 200, 250}, kHardwoodPlank, 2, 3, 600, 900},
                {Node::Ironwood, 191004, 191004, "Ironwood Tree", {225, 250, 275, 325}, kIronwoodPlank, 1, 3, 900, 1200},
            };
        }

        void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        uint16_t Get16(uint8_t const* b) { return uint16_t(b[0] | b[1] << 8); }
        uint32_t Get32(uint8_t const* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
    }

    TierDef const& TierInfo(Tier t)
    {
        size_t const i = size_t(t);
        return kTiers[i < sizeof(kTiers) / sizeof(kTiers[0]) ? i : 0];
    }

    uint16_t Cap(Tier t) { return TierInfo(t).cap; }

    Tier Next(Tier t)
    {
        return t == Tier::Artisan ? Tier::None : Tier(uint8_t(t) + 1);
    }

    TrainResult CanTrain(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, bool gm)
    {
        if (want == Tier::None || uint8_t(have) >= uint8_t(want))
            return TrainResult::AlreadyKnown;
        if (want != Next(have))
            return TrainResult::NotNext;
        if (gm)
            return TrainResult::Ok;
        TierDef const& d = TierInfo(want);
        if (skill < d.skillToLearn)
            return TrainResult::SkillTooLow;
        if (level < d.levelToLearn)
            return TrainResult::LevelTooLow;
        if (money < d.costCopper)
            return TrainResult::NotEnoughMoney;
        return TrainResult::Ok;
    }

    Thresholds Spaced(uint16_t required)
    {
        return {required, uint16_t(required + 25), uint16_t(required + 50), uint16_t(required + 75)};
    }

    Color ColorOf(uint16_t skill, Thresholds const& t)
    {
        if (skill < t.required)
            return Color::Red;
        if (skill >= t.gray)
            return Color::Gray;
        if (skill >= t.green)
            return Color::Green;
        if (skill >= t.yellow)
            return Color::Yellow;
        return Color::Orange;
    }

    float SkillUpChance(uint16_t skill, Thresholds const& t)
    {
        switch (ColorOf(skill, t))
        {
            case Color::Orange: return 1.f;
            case Color::Yellow:
            case Color::Green:
                return t.gray > t.yellow ? Clamp(float(t.gray - skill) / float(t.gray - t.yellow), 0.f, 1.f) : 0.f;
            default: return 0.f;
        }
    }

    uint16_t ApplySkillUp(uint16_t skill, uint16_t cap, Thresholds const& t, float roll)
    {
        if (skill >= cap)
            return skill;
        return roll < SkillUpChance(skill, t) ? uint16_t(skill + 1) : skill;
    }

    std::vector<Recipe> const& Recipes()
    {
        static std::vector<Recipe> const recipes = MakeRecipes();
        return recipes;
    }

    Recipe const* RecipeFor(Build::PieceType t)
    {
        if (!Build::Known(uint8_t(t)))
            return nullptr;
        for (Recipe const& r : Recipes())
            if (r.piece == t)
                return &r;
        return nullptr;
    }

    bool CanAfford(Inventory const& bags, std::vector<Material> const& materials)
    {
        return Missing(bags, materials).empty();
    }

    std::vector<Material> Missing(Inventory const& bags, std::vector<Material> const& materials)
    {
        // The same item twice in a list asks for the sum.
        std::unordered_map<uint32_t, uint32_t> want;
        std::vector<uint32_t> order;
        for (Material const& m : materials)
        {
            if (!want.count(m.item))
                order.push_back(m.item);
            want[m.item] += m.count;
        }
        std::vector<Material> out;
        for (uint32_t item : order)
        {
            auto it = bags.find(item);
            uint32_t const have = it == bags.end() ? 0 : it->second;
            if (have < want[item])
                out.push_back({item, want[item] - have});
        }
        return out;
    }

    PlaceCheck CheckPlace(Build::PieceType t, Tier tier, uint16_t skill, Inventory const& bags, bool free)
    {
        Recipe const* r = RecipeFor(t);
        if (!r)
            return PlaceCheck::Unknown;
        if (free)
            return PlaceCheck::Ok;
        if (tier == Tier::None)
            return PlaceCheck::NotLearned;
        if (skill < r->skill.required)
            return PlaceCheck::SkillTooLow;
        if (!CanAffordBuild(bags, r->materials))
            return PlaceCheck::NoMaterials;
        return PlaceCheck::Ok;
    }

    uint16_t PlacementSkillUp(Build::PieceType t, uint16_t skill, Tier tier, bool paid, float roll)
    {
        Recipe const* r = RecipeFor(t);
        if (!r || !paid || tier == Tier::None)
            return skill;
        return ApplySkillUp(skill, Cap(tier), r->skill, roll);
    }

    std::vector<Material> Refund(Build::PieceType t, float healthFraction)
    {
        std::vector<Material> out;
        Recipe const* r = RecipeFor(t);
        if (!r)
            return out;
        float const scale = kRefundShare * Clamp(healthFraction, 0.f, 1.f);
        for (Material const& m : r->materials)
            if (uint32_t const n = uint32_t(std::floor(float(m.count) * scale + 1e-4f)); n > 0)
                out.push_back({m.item, n});
        return out;
    }

    bool CouldTeach(Build::PieceType t, uint16_t skill, Tier tier)
    {
        Recipe const* r = RecipeFor(t);
        return r && tier != Tier::None && skill < Cap(tier) && SkillUpChance(skill, r->skill) > 0.f;
    }

    std::vector<SalvagePair> const& SalvageTable()
    {
        // Ids fixed for good: an item in someone's bags keeps its entry. 24_construction_rules.sql
        // makes each from its fresh item's row.
        static std::vector<SalvagePair> const table = {
            {kRoughLumber, 93600}, {kSturdyLumber, 93601}, {kHardwoodPlank, 93602}, {kIronwoodPlank, 93603},
            {kCopperNails, 93604}, {kBronzeHinge, 93605}, {kIronBracket, 93606}, {kReinforcedPlate, 93607},
            {kFortifiedPlate, 93608},
            {kRoughStone, 93610}, {kCoarseStone, 93611}, {kHeavyStone, 93612}, {kSolidStone, 93613}, {kDenseStone, 93614},
            {kCopperBar, 93615}, {kBronzeBar, 93616}, {kIronBar, 93617}, {kMithrilBar, 93618}, {kThoriumBar, 93619},
            {kLinenCloth, 93620}, {kWoolCloth, 93621}, {kBoltOfSilk, 93622}, {kBoltOfMageweave, 93623}, {kLightLeather, 93624},
            {kArcaneCrystal, 93625}, {kLargeBrilliantShard, 93626},
        };
        return table;
    }

    uint32_t SalvagedOf(uint32_t fresh)
    {
        for (SalvagePair const& p : SalvageTable())
            if (p.fresh == fresh)
                return p.salvaged;
        return 0;
    }

    uint32_t FreshOf(uint32_t salvaged)
    {
        if (salvaged < kSalvagedFirst || salvaged > kSalvagedLast)
            return 0;
        for (SalvagePair const& p : SalvageTable())
            if (p.salvaged == salvaged)
                return p.fresh;
        return 0;
    }

    std::vector<Material> AsSalvaged(std::vector<Material> const& fresh)
    {
        std::vector<Material> out;
        for (Material const& m : fresh)
        {
            if (!m.count)
                continue;
            uint32_t const item = SalvagedOf(m.item) ? SalvagedOf(m.item) : m.item;
            auto it = std::find_if(out.begin(), out.end(), [item](Material const& o) { return o.item == item; });
            if (it != out.end())
                it->count += m.count;
            else
                out.push_back({item, m.count});
        }
        return out;
    }

    namespace
    {
        // A cost summed by item, in the order first met (the same item twice asks for the sum).
        std::vector<Material> Summed(std::vector<Material> const& materials)
        {
            std::vector<Material> out;
            for (Material const& m : materials)
            {
                auto it = std::find_if(out.begin(), out.end(), [&](Material const& o) { return o.item == m.item; });
                if (it != out.end())
                    it->count += m.count;
                else
                    out.push_back(m);
            }
            return out;
        }

        uint32_t Have(Inventory const& bags, uint32_t item)
        {
            if (!item)
                return 0;
            auto it = bags.find(item);
            return it == bags.end() ? 0 : it->second;
        }
    }

    Payment PlanPayment(std::vector<Material> const& cost, Inventory const& bags, bool preferFresh)
    {
        Payment p;
        for (Material const& m : Summed(cost))
        {
            uint32_t const salvagedItem = SalvagedOf(m.item);
            uint32_t const fresh = Have(bags, m.item);
            uint32_t const salvaged = Have(bags, salvagedItem);
            if (uint64_t(fresh) + salvaged < m.count)
                return Payment();
            // The preferred kind first, the rest from the other.
            uint32_t const fromFirst = std::min(m.count, preferFresh ? fresh : salvaged);
            uint32_t const fromSecond = m.count - fromFirst;
            uint32_t const freshTaken = preferFresh ? fromFirst : fromSecond;
            uint32_t const salvagedTaken = preferFresh ? fromSecond : fromFirst;
            if (freshTaken)
                p.take.push_back({m.item, freshTaken});
            if (salvagedTaken)
            {
                p.take.push_back({salvagedItem, salvagedTaken});
                p.usedSalvage = true;
            }
        }
        p.ok = true;
        return p;
    }

    std::vector<Material> MissingForBuild(Inventory const& bags, std::vector<Material> const& materials)
    {
        std::vector<Material> out;
        for (Material const& m : Summed(materials))
        {
            uint64_t const have = uint64_t(Have(bags, m.item)) + Have(bags, SalvagedOf(m.item));
            if (have < m.count)
                out.push_back({m.item, uint32_t(m.count - have)});
        }
        return out;
    }

    bool CanAffordBuild(Inventory const& bags, std::vector<Material> const& materials)
    {
        return MissingForBuild(bags, materials).empty();
    }

    std::vector<uint32_t> ItemsToCount(std::vector<Material> const& materials)
    {
        std::vector<uint32_t> out;
        for (Material const& m : materials)
            for (uint32_t item : {m.item, SalvagedOf(m.item)})
                if (item && std::find(out.begin(), out.end(), item) == out.end())
                    out.push_back(item);
        return out;
    }

    namespace
    {
        // A share of each material, rounded down; the empty ones dropped.
        void AddShare(std::vector<Material>& out, std::vector<Material> const& in, float share)
        {
            for (Material const& m : in)
                if (uint32_t const n = uint32_t(std::floor(float(m.count) * share + 1e-4f)); n > 0)
                {
                    auto it = std::find_if(out.begin(), out.end(), [&](Material const& o) { return o.item == m.item; });
                    if (it != out.end())
                        it->count += n;
                    else
                        out.push_back({m.item, n});
                }
        }

        std::vector<ReinforceTier> const& ReinforceTiers()
        {
            // Very expensive: the first tier is a smith's day of iron, the second a stack of
            // mithril plate, the third thorium and the fortified plate only an Artisan smith makes.
            static std::vector<ReinforceTier> const tiers = {
                {1, "Braced", 150, {{kIronBracket, 4}, {kIronBar, 6}}},
                {2, "Plated", 225, {{kReinforcedPlate, 3}, {kMithrilBar, 8}}},
                {3, "Fortified", 275, {{kFortifiedPlate, 2}, {kThoriumBar, 6}, {kDenseStone, 8}}},
            };
            return tiers;
        }
    }

    std::vector<Material> Refund(Build::PieceType t, float healthFraction, uint8_t reinforcement)
    {
        return Refund(t, healthFraction, reinforcement, Build::kPaidAll);
    }

    std::vector<Material> Refund(Build::PieceType t, float healthFraction, uint8_t reinforcement, uint8_t paid)
    {
        std::vector<Material> out;
        if (paid & Build::kPaidPiece)
            out = Refund(t, healthFraction);
        if (RecipeFor(t) && Reinforceable(t))
            AddShare(out, ReinforcementSpent(reinforcement, paid), kRefundShare * Clamp(healthFraction, 0.f, 1.f));
        return out;
    }

    std::vector<Material> Refund(Build::Piece const& p)
    {
        float const full = Build::MaxHealth(p);
        return Refund(p.type, full > 0.f ? p.health / full : 0.f, p.reinforcement, p.paid);
    }

    bool Buildable(Build::PieceType t)
    {
        return RecipeFor(t) != nullptr;
    }

    std::vector<Material> Salvage(Build::PieceType t, uint8_t reinforcement)
    {
        return Salvage(t, reinforcement, Build::kPaidAll);
    }

    std::vector<Material> Salvage(Build::PieceType t, uint8_t reinforcement, uint8_t paid)
    {
        std::vector<Material> out;
        Recipe const* r = RecipeFor(t);
        if (!r)
            return out;
        if (paid & Build::kPaidPiece)
        {
            AddShare(out, r->materials, kSalvageShare);
            if (!r->materials.empty())
            {
                uint32_t const main = r->materials.front().item;
                if (std::none_of(out.begin(), out.end(), [main](Material const& m) { return m.item == main; }))
                    out.insert(out.begin(), {main, 1});
            }
        }
        if (Reinforceable(t))
            AddShare(out, ReinforcementSpent(reinforcement, paid), kSalvageShare);
        return out;
    }

    std::vector<std::vector<Material>> SplitSalvage(std::vector<Material> const& salvage, size_t members, size_t start)
    {
        std::vector<std::vector<Material>> out(members);
        if (!members)
            return out;
        size_t next = start % members;
        for (Material const& m : salvage)
            for (uint32_t k = 0; k < m.count; ++k)
            {
                std::vector<Material>& to = out[next];
                next = (next + 1) % members;
                auto it = std::find_if(to.begin(), to.end(), [&](Material const& o) { return o.item == m.item; });
                if (it != to.end())
                    ++it->count;
                else
                    to.push_back({m.item, 1});
            }
        return out;
    }

    ReinforceTier const* ReinforceInfo(uint8_t tier)
    {
        for (ReinforceTier const& r : ReinforceTiers())
            if (r.tier == tier)
                return &r;
        return nullptr;
    }

    bool Reinforceable(Build::PieceType t)
    {
        switch (Build::BaseOf(t))
        {
            case PieceType::WoodFloor:
            case PieceType::WoodWall:
            case PieceType::WoodHalfWall:
            case PieceType::WoodDoorway:
            case PieceType::WoodWindow:
            case PieceType::WoodRoof:
            case PieceType::WoodStairs:
            case PieceType::WoodBeam:
            case PieceType::StoneFloor:
            case PieceType::StoneWall:
            case PieceType::StoneHalfWall:
            case PieceType::Sandbags:
            case PieceType::WoodDoor:
                return true;
            default:
                return false;
        }
    }

    ReinforceCheck CheckReinforce(Build::PieceType t, uint8_t current, Tier tier, uint16_t skill, Inventory const& bags, bool free)
    {
        if (!Reinforceable(t))
            return ReinforceCheck::NotReinforceable;
        ReinforceTier const* next = current < Build::kMaxReinforcement ? ReinforceInfo(uint8_t(current + 1)) : nullptr;
        if (!next)
            return ReinforceCheck::MaxTier;
        if (free)
            return ReinforceCheck::Ok;
        if (tier == Tier::None)
            return ReinforceCheck::NotLearned;
        if (skill < next->skill)
            return ReinforceCheck::SkillTooLow;
        if (!CanAffordBuild(bags, next->materials))
            return ReinforceCheck::NoMaterials;
        return ReinforceCheck::Ok;
    }

    std::vector<Material> ReinforcementSpent(uint8_t tier)
    {
        return ReinforcementSpent(tier, Build::kPaidAll);
    }

    std::vector<Material> ReinforcementSpent(uint8_t tier, uint8_t paid)
    {
        std::vector<Material> out;
        for (uint8_t t = 1; t <= tier && t <= Build::kMaxReinforcement; ++t)
            if (paid & Build::PaidTier(t))
                if (ReinforceTier const* r = ReinforceInfo(t))
                    AddShare(out, r->materials, 1.f);
        return out;
    }

    bool DecodeReinforce(std::vector<uint8_t> const& body, uint32_t& id)
    {
        if (body.size() != 5 || body[0] != kOpReinforce)
            return false;
        id = Get32(&body[1]);
        return true;
    }

    std::vector<uint8_t> EncodeReinforcements()
    {
        std::vector<uint8_t> o{kOpReinforcements, uint8_t(ReinforceTiers().size())};
        for (ReinforceTier const& r : ReinforceTiers())
        {
            o.push_back(r.tier);
            Put16(o, r.skill);
            Put16(o, uint16_t(Build::ReinforcedHealthMultiplier(r.tier) * 100.f + 0.5f));
            o.push_back(uint8_t(Build::ReinforcedBulletReduction(r.tier) * 100.f + 0.5f));
            o.push_back(uint8_t(r.materials.size()));
            for (Material const& m : r.materials)
            {
                Put32(o, m.item);
                Put16(o, uint16_t(std::min<uint32_t>(m.count, 0xFFFF)));
            }
        }
        return o;
    }

    std::vector<NodeDef> const& Nodes()
    {
        static std::vector<NodeDef> const nodes = MakeNodes();
        return nodes;
    }

    NodeDef const* NodeFor(uint32_t gameobjectEntry)
    {
        for (NodeDef const& n : Nodes())
            if (n.gameobject == gameobjectEntry)
                return &n;
        return nullptr;
    }

    NodeDef const* NodeDefOf(Node node)
    {
        for (NodeDef const& n : Nodes())
            if (n.node == node)
                return &n;
        return nullptr;
    }

    GatherCheck CanGather(uint32_t gameobjectEntry, Tier tier, uint16_t skill, bool hasAxe, bool gm)
    {
        NodeDef const* n = NodeFor(gameobjectEntry);
        if (!n)
            return GatherCheck::NotANode;
        if (gm)
            return GatherCheck::Ok;
        if (tier == Tier::None)
            return GatherCheck::NotLearned;
        if (!hasAxe)
            return GatherCheck::NoAxe;
        if (skill < n->skill.required)
            return GatherCheck::SkillTooLow;
        return GatherCheck::Ok;
    }

    uint32_t NodeYield(NodeDef const& n, float roll)
    {
        uint32_t const span = uint32_t(n.maxYield - n.minYield) + 1;
        uint32_t const k = std::min(span - 1, uint32_t(Clamp(roll, 0.f, 1.f) * float(span)));
        return n.minYield + k;
    }

    uint16_t GatherSkillUp(NodeDef const& n, uint16_t skill, Tier tier, float roll)
    {
        if (tier == Tier::None)
            return skill;
        return ApplySkillUp(skill, Cap(tier), n.skill, roll);
    }

    CraftCheck CanCraft(CraftRecipe const& r, uint16_t skill, Inventory const& bags, bool gm)
    {
        if (!gm && skill < r.skill.required)
            return CraftCheck::SkillTooLow;
        if (!CanAfford(bags, r.reagents))
            return CraftCheck::NoReagents;
        return CraftCheck::Ok;
    }

    std::vector<uint8_t> EncodeRecipes(std::vector<Recipe> const& recipes)
    {
        std::vector<uint8_t> o{uint8_t(ServerOp::Recipes)};
        Put16(o, uint16_t(recipes.size()));
        for (Recipe const& r : recipes)
        {
            o.push_back(uint8_t(r.piece));
            Put16(o, r.skill.required);
            Put16(o, r.skill.yellow);
            Put16(o, r.skill.green);
            Put16(o, r.skill.gray);
            o.push_back(uint8_t(std::min<size_t>(r.materials.size(), 255)));
            for (size_t i = 0; i < r.materials.size() && i < 255; ++i)
            {
                Put32(o, r.materials[i].item);
                Put16(o, uint16_t(std::min<uint32_t>(r.materials[i].count, 0xFFFF)));
            }
        }
        return o;
    }

    bool DecodeRecipes(std::vector<uint8_t> const& body, std::vector<Recipe>& out)
    {
        out.clear();
        if (body.size() < 3 || body[0] != uint8_t(ServerOp::Recipes))
            return false;
        size_t at = 1;
        uint16_t const count = Get16(&body[at]);
        at += 2;
        for (uint16_t i = 0; i < count; ++i)
        {
            if (at + 10 > body.size())
                return false;
            Recipe r;
            r.piece = Build::PieceType(body[at]);
            r.skill = {Get16(&body[at + 1]), Get16(&body[at + 3]), Get16(&body[at + 5]), Get16(&body[at + 7])};
            uint8_t const n = body[at + 9];
            at += 10;
            if (at + size_t(n) * 6 > body.size())
                return false;
            for (uint8_t k = 0; k < n; ++k, at += 6)
                r.materials.push_back({Get32(&body[at]), Get16(&body[at + 4])});
            out.push_back(std::move(r));
        }
        return at == body.size();
    }

    std::vector<uint8_t> EncodeSkill(Tier tier, uint16_t skill)
    {
        std::vector<uint8_t> o{uint8_t(ServerOp::Skill), uint8_t(tier)};
        Put16(o, skill);
        Put16(o, Cap(tier));
        return o;
    }
    uint32_t KitsOf(Kits const& k, Build::PieceType t)
    {
        auto it = k.find(uint8_t(t));
        return it == k.end() ? 0 : it->second;
    }

    bool TakeKit(Kits& k, Build::PieceType t)
    {
        auto it = k.find(uint8_t(t));
        if (it == k.end() || it->second == 0)
            return false;
        if (--it->second == 0)
            k.erase(it);
        return true;
    }

    uint32_t AddKits(Kits& k, Build::PieceType t, uint32_t n)
    {
        if (!RecipeFor(t) || n == 0)
            return 0;
        uint32_t& held = k[uint8_t(t)];
        uint32_t const room = held >= kMaxKits ? 0 : kMaxKits - held;
        uint32_t const added = std::min(n, room);
        held += added;
        if (held == 0)
            k.erase(uint8_t(t));
        return added;
    }

    KitCheck CheckKit(Build::PieceType t, Tier tier, uint16_t skill, Inventory const& bags, uint32_t held, bool free)
    {
        if (!Buildable(t))
            return KitCheck::Unknown;
        if (held >= kMaxKits)
            return KitCheck::Full;
        switch (CheckPlace(t, tier, skill, bags, free))
        {
            case PlaceCheck::Ok: return KitCheck::Ok;
            case PlaceCheck::NotLearned: return KitCheck::NotLearned;
            case PlaceCheck::SkillTooLow: return KitCheck::SkillTooLow;
            case PlaceCheck::NoMaterials: return KitCheck::NoMaterials;
            default: return KitCheck::Unknown;
        }
    }

    uint16_t CraftBatch(uint16_t asked)
    {
        return std::clamp<uint16_t>(asked, 1, kMaxCraftBatch);
    }

    bool DecodeCraftKit(std::vector<uint8_t> const& body, Build::PieceType& t, uint16_t& count)
    {
        if (body.size() != 4 || body[0] != kOpCraftKit)
            return false;
        t = Build::PieceType(body[1]);
        count = Get16(&body[2]);
        return RecipeFor(t) != nullptr;
    }

    std::vector<uint8_t> EncodeCraftKit(Build::PieceType t, uint16_t count)
    {
        std::vector<uint8_t> o{kOpCraftKit, uint8_t(t)};
        Put16(o, count);
        return o;
    }

    std::vector<uint8_t> EncodeKits(Kits const& k)
    {
        std::vector<uint8_t> o{kOpKits};
        uint16_t n = 0;
        for (auto const& kv : k)
            if (kv.second)
                ++n;
        Put16(o, n);
        for (auto const& kv : k)
            if (kv.second)
            {
                o.push_back(kv.first);
                Put16(o, uint16_t(std::min<uint32_t>(kv.second, 0xFFFF)));
            }
        return o;
    }

    bool DecodeKits(std::vector<uint8_t> const& body, Kits& out)
    {
        out.clear();
        if (body.size() < 3 || body[0] != kOpKits)
            return false;
        uint16_t const n = Get16(&body[1]);
        if (body.size() != 3 + size_t(n) * 3)
            return false;
        for (size_t i = 0, at = 3; i < n; ++i, at += 3)
            out[body[at]] = Get16(&body[at + 1]);
        return true;
    }
}}
