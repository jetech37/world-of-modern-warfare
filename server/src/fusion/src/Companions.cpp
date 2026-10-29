/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Companions.h"
#include "fusion/CompanionTraits.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

namespace Fusion::Companions
{
    namespace
    {
        EvolutionRule Rule(uint8_t level, uint32_t item, uint8_t count, std::vector<uint32_t> areas,
            Condition condition, CreatureType type, uint16_t kills)
        {
            EvolutionRule r;
            r.level = level;
            r.item = item;
            r.itemCount = count;
            r.areas = std::move(areas);
            r.condition = condition;
            r.conditionType = type;
            r.conditionCount = kills;
            return r;
        }

        void Add(std::vector<CompanionSpecies>& v, uint16_t id, char const* name, uint32_t item, Family family,
            Role role, std::initializer_list<StageInfo> stages, EvolutionRule first, EvolutionRule second,
            std::vector<uint32_t> dropEntries, CreatureType dropType, uint8_t dropFamily)
        {
            CompanionSpecies s;
            s.id = id;
            s.name = name;
            s.item = item;
            s.family = family;
            s.role = role;
            s.stageCount = 0;
            for (StageInfo const& st : stages)
                if (s.stageCount < kMaxStages)
                    s.stages[s.stageCount++] = st;
            s.evolutions[0] = std::move(first);
            s.evolutions[1] = std::move(second);
            s.drops.entries = std::move(dropEntries);
            s.drops.type = dropType;
            s.drops.petFamily = dropFamily;
            v.push_back(std::move(s));
        }

        std::vector<CompanionSpecies> BuildStarter()
        {
            std::vector<CompanionSpecies> v;
            // Generated from the same table as 02_companions.sql's fusion_companion_species rows
            // (CompanionTests holds them together). Creature entries are vanilla 1.12
            // creature_template rows; area ids are AreaTable ids (zone or sub-area).
            Add(v, 1, "Black Whelpling", 90001, Family::Dragon, Role::Ranged, {{7543, 1.0f}, {441, 1.0f}, {7044, 0.6f}},
                Rule(20, 0, 0, {25, 46, 51, 254, 511}, Condition::None, CreatureType::None, 0),
                Rule(45, 90101, 1, {25, 254, 1583, 511}, Condition::None, CreatureType::None, 0),
                {441, 4323, 4324, 7047, 7048, 10161, 11262}, CreatureType::None, 0);
            Add(v, 2, "Crimson Whelpling", 90002, Family::Dragon, Role::Ranged, {{7544, 1.0f}, {1069, 1.0f}, {13976, 0.6f}},
                Rule(20, 0, 0, {11, 1037, 1038}, Condition::None, CreatureType::None, 0),
                Rule(45, 90101, 1, {1037}, Condition::None, CreatureType::None, 0),
                {1042, 1043, 1044, 1045, 1046, 1069}, CreatureType::None, 0);
            Add(v, 3, "Emerald Whelpling", 90003, Family::Dragon, Role::Ranged, {{7545, 1.0f}, {741, 1.0f}, {742, 0.7f}},
                Rule(20, 0, 0, {856, 438, 1111, 356}, Condition::None, CreatureType::None, 0),
                Rule(45, 90101, 1, {856, 438, 1111, 356}, Condition::Night, CreatureType::None, 0),
                {740, 741, 742, 5277, 5280, 5283, 8319, 8776}, CreatureType::None, 0);
            Add(v, 4, "Azure Whelpling", 90004, Family::Dragon, Role::Ranged, {{7547, 1.0f}, {10659, 1.0f}, {10660, 1.0f}},
                Rule(25, 0, 0, {618, 16}, Condition::None, CreatureType::None, 0),
                Rule(50, 90101, 1, {2245}, Condition::None, CreatureType::None, 0),
                {193, 7227, 7435, 10659, 10660, 12200}, CreatureType::None, 0);
            Add(v, 5, "Sprite Darter Hatchling", 90005, Family::FaerieDragon, Role::Ranged, {{9662, 1.0f}, {5278, 0.75f}, {5278, 1.0f}},
                Rule(35, 0, 0, {357}, Condition::Night, CreatureType::None, 0),
                Rule(50, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {5278, 7997}, CreatureType::None, 0);
            Add(v, 6, "Wolf Pup", 90006, Family::Wolf, Role::Melee, {{10259, 1.0f}, {565, 1.0f}, {358, 1.0f, true, 580, 23250}},
                Rule(10, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(30, 90102, 1, {10}, Condition::Night, CreatureType::None, 0),
                {}, CreatureType::Beast, 1);
            Add(v, 7, "Nightsaber Cub", 90007, Family::Cat, Role::Melee, {{2031, 1.0f}, {2042, 1.0f}, {7690, 1.0f, true, 10793, 23219}},
                Rule(10, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(30, 90102, 1, {493}, Condition::Night, CreatureType::None, 0),
                {2031, 2042, 2033}, CreatureType::Beast, 2);
            Add(v, 8, "Frostsaber Cub", 90008, Family::Cat, Role::Melee, {{7430, 1.0f}, {7431, 1.0f}, {6074, 1.0f, true, 8394, 23221}},
                Rule(25, 0, 0, {618}, Condition::None, CreatureType::None, 0),
                Rule(50, 90102, 1, {618}, Condition::Night, CreatureType::None, 0),
                {7430, 7431, 7433}, CreatureType::Beast, 2);
            Add(v, 9, "Panther Cub", 90009, Family::Cat, Role::Melee, {{683, 0.6f}, {736, 1.0f}, {684, 1.0f}},
                Rule(20, 0, 0, {33}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {33}, Condition::KillType, CreatureType::Humanoid, 30),
                {683, 736, 684}, CreatureType::Beast, 2);
            Add(v, 10, "Bear Cub", 90010, Family::Bear, Role::Tank, {{6789, 1.0f}, {3809, 1.0f}, {3810, 1.0f}},
                Rule(15, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(35, 90102, 1, {331}, Condition::None, CreatureType::None, 0),
                {6789, 822, 1128, 1186, 1196, 3809, 3810}, CreatureType::Beast, 4);
            Add(v, 11, "Young Web Spider", 90011, Family::Spider, Role::Melee, {{1504, 1.0f}, {217, 1.0f}, {2001, 1.0f}},
                Rule(12, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(30, 90102, 1, {}, Condition::KillType, CreatureType::Beast, 40),
                {30, 43, 217, 930, 1504, 1505, 2001, 4263, 4413}, CreatureType::Beast, 3);
            Add(v, 12, "Raptor Hatchling", 90012, Family::Raptor, Role::Melee, {{3122, 0.6f}, {3123, 1.0f}, {7708, 1.0f, true, 10799, 23241}},
                Rule(12, 0, 0, {17, 14}, Condition::None, CreatureType::None, 0),
                Rule(35, 90102, 1, {33}, Condition::None, CreatureType::None, 0),
                {107, 685, 855, 856, 3122, 3123}, CreatureType::Beast, 11);
            Add(v, 13, "Young Crocolisk", 90013, Family::Crocolisk, Role::Tank, {{1417, 0.4f}, {1151, 0.8f}, {1151, 1.0f}},
                Rule(30, 0, 0, {15, 11, 8}, Condition::None, CreatureType::None, 0),
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {1084, 1151, 1417}, CreatureType::Beast, 6);
            Add(v, 14, "Infant Gorilla", 90014, Family::Gorilla, Role::Tank, {{1108, 0.55f}, {1557, 1.0f}, {1558, 1.0f}},
                Rule(30, 0, 0, {33}, Condition::None, CreatureType::None, 0),
                Rule(45, 90102, 1, {490}, Condition::None, CreatureType::None, 0),
                {1108, 1511, 1557, 1558, 6514}, CreatureType::Beast, 9);
            Add(v, 15, "Owlet", 90015, Family::Owl, Role::Melee, {{7555, 1.0f}, {7456, 1.0f}, {7456, 0.8f}},
                Rule(40, 0, 0, {618, 493}, Condition::Night, CreatureType::None, 0),
                Rule(55, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7456}, CreatureType::Beast, 26);
            Add(v, 16, "Duskbat Pup", 90016, Family::Bat, Role::Melee, {{1512, 1.0f}, {8600, 0.8f}, {8600, 1.15f}},
                Rule(40, 0, 0, {28, 139}, Condition::Night, CreatureType::None, 0),
                Rule(55, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {1512, 1513, 8600}, CreatureType::Beast, 24);
            Add(v, 17, "Plainstrider Chick", 90017, Family::Tallstrider, Role::Melee, {{2955, 0.6f}, {2956, 1.0f}, {2957, 1.0f}},
                Rule(5, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(10, 0, 0, {215}, Condition::None, CreatureType::None, 0),
                {2321, 2955, 2956, 2957}, CreatureType::Beast, 12);
            Add(v, 18, "Scorpid Hatchling", 90018, Family::Scorpid, Role::Tank, {{3124, 0.6f}, {4140, 1.0f}, {5422, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {440, 1377}, Condition::None, CreatureType::None, 0),
                {3124, 4140, 4697, 5422, 5423}, CreatureType::Beast, 20);
            Add(v, 19, "Ember Spark", 90019, Family::FireElemental, Role::Ranged, {{17003, 0.5f}, {6296, 1.0f}, {575, 1.0f}},
                Rule(20, 0, 0, {51, 46, 537}, Condition::None, CreatureType::None, 0),
                Rule(40, 90103, 1, {537}, Condition::None, CreatureType::None, 0),
                {575, 5850, 5852, 5855, 6296, 6520, 7031, 7032, 11321, 12076}, CreatureType::None, 0);
            Add(v, 20, "Water Mote", 90020, Family::WaterElemental, Role::Ranged, {{6748, 0.6f}, {691, 1.0f}, {510, 1.0f}},
                Rule(20, 0, 0, {16, 1227, 15}, Condition::None, CreatureType::None, 0),
                Rule(45, 90103, 1, {1227}, Condition::None, CreatureType::None, 0),
                {510, 691, 3917, 5461, 6748, 10756, 10757}, CreatureType::None, 0);
            Add(v, 21, "Pebble", 90021, Family::EarthElemental, Role::Tank, {{2735, 0.45f}, {92, 1.0f}, {2736, 1.0f}},
                Rule(20, 0, 0, {3}, Condition::None, CreatureType::None, 0),
                Rule(45, 90103, 1, {3, 51}, Condition::None, CreatureType::None, 0),
                {92, 329, 2593, 2735, 2736, 2791}, CreatureType::None, 0);
            Add(v, 22, "Zephyr", 90022, Family::AirElemental, Role::Ranged, {{5898, 0.6f}, {11576, 1.0f}, {11744, 1.0f}},
                Rule(25, 0, 0, {400, 1377}, Condition::None, CreatureType::None, 0),
                Rule(50, 90103, 1, {1377}, Condition::None, CreatureType::None, 0),
                {832, 5898, 11576, 11577, 11744}, CreatureType::None, 0);
            Add(v, 23, "Murloc Hatchling", 90023, Family::Murloc, Role::Melee, {{15186, 1.0f}, {735, 1.0f}, {127, 1.0f}},
                Rule(10, 0, 0, {40, 148, 11, 38}, Condition::None, CreatureType::None, 0),
                Rule(25, 0, 0, {}, Condition::KillType, CreatureType::Humanoid, 30),
                {46, 126, 127, 171, 285, 456, 458, 513, 515, 548, 578, 735}, CreatureType::None, 0);
            Add(v, 24, "Fel Cinder", 90024, Family::Infernal, Role::Tank, {{4676, 0.5f}, {6073, 1.0f}, {89, 0.8f}},
                Rule(30, 90104, 1, {4, 361}, Condition::None, CreatureType::None, 0),
                Rule(50, 90104, 1, {4}, Condition::None, CreatureType::None, 0),
                {89, 4676, 6073}, CreatureType::Demon, 0);
            Add(v, 25, "Oozeling", 90025, Family::Ooze, Role::Melee, {{15429, 1.0f}, {8257, 1.0f}, {12387, 0.7f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(45, 0, 0, {28, 139}, Condition::None, CreatureType::None, 0),
                {364, 1030, 1806, 3928, 6218, 8257, 10697, 12387}, CreatureType::None, 0);
            Add(v, 26, "Snow Tiger Kitten", 90026, Family::Critter, Role::Melee, {{7381, 1.0f}, {7381, 1.45f}, {7381, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {2110, 4075}, CreatureType::None, 0);
            Add(v, 27, "Snowshoe Rabbit", 90027, Family::Critter, Role::Melee, {{7560, 1.0f}, {7560, 1.8f}, {7560, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {721, 5951}, CreatureType::Critter, 0);
            Add(v, 28, "Spider Tank Bot", 90028, Family::Mechanical, Role::Ranged, {{2671, 1.0f}, {2671, 1.05f}, {2671, 0.75f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {}, CreatureType::Mechanical, 0);
            // The mount families: each grows into a vanilla mount (the riding creature a mount
            // spell shows, its spells for 60% and 100%) that walks, fights and can be ridden.
            Add(v, 29, "Kodo Calf", 90029, Family::Kodo, Role::Tank, {{2972, 0.7f}, {3236, 1.0f}, {11689, 1.0f, true, 18990, 23249}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17}, Condition::None, CreatureType::None, 0),
                {2972, 2973, 2974, 3234, 3236, 3237, 4700, 4701, 4702}, CreatureType::None, 0);
            Add(v, 30, "Foal", 90030, Family::Horse, Role::Melee, {{385, 0.4f}, {385, 0.75f}, {284, 1.0f, true, 458, 23229}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {12}, Condition::Day, CreatureType::None, 0),
                {385}, CreatureType::None, 0);
            Add(v, 31, "Mechanostrider Runt", 90031, Family::Mechanostrider, Role::Melee, {{7749, 0.35f}, {7749, 0.6f}, {7749, 1.0f, true, 10969, 23223}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {1, 721}, Condition::None, CreatureType::None, 0),
                {}, CreatureType::Mechanical, 0);
            Add(v, 32, "Skeletal Foal", 90032, Family::SkeletalHorse, Role::Melee, {{11153, 0.4f}, {11153, 0.75f}, {11153, 1.0f, true, 17462, 17465}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {85}, Condition::Night, CreatureType::None, 0),
                {}, CreatureType::Undead, 0);
            Add(v, 33, "Ram Kid", 90033, Family::Ram, Role::Tank, {{2098, 0.4f}, {2098, 0.8f}, {4710, 1.0f, true, 6777, 23239}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {1}, Condition::None, CreatureType::None, 0),
                {2098}, CreatureType::None, 0);
            // The catalog's size rule over the starter set (08_companion_lines.sql's UPDATEs).
            struct Rescale
            {
                uint16_t id;
                std::array<float, kMaxStages> scale;
            };
            static Rescale const kRescale[] = {
                {1, {0.90f, 1.25f, 0.40f}},
                {2, {1.00f, 1.15f, 0.40f}},
                {3, {1.00f, 0.85f, 1.35f}},
                {4, {1.00f, 1.00f, 1.25f}},
                {5, {1.00f, 0.85f}},
                {6, {0.95f, 0.85f, 1.00f}},
                {7, {0.60f, 1.20f, 1.00f}},
                {8, {0.85f, 0.80f, 1.00f}},
                {9, {0.50f, 1.00f, 1.50f}},
                {10, {0.80f, 1.00f, 1.70f}},
                {11, {0.60f, 0.80f, 0.85f}},
                {12, {0.65f, 1.20f, 1.00f}},
                {13, {1.00f, 0.80f}},
                {14, {0.75f, 0.85f, 1.65f}},
                {15, {1.00f, 0.80f}},
                {16, {1.00f, 1.15f}},
                {17, {0.60f, 1.20f, 2.05f}},
                {18, {0.60f, 1.00f, 1.70f}},
                {19, {1.00f, 0.80f, 2.25f}},
                {20, {0.50f, 1.00f, 2.00f}},
                {21, {0.35f, 1.00f, 1.25f}},
                {22, {0.50f, 1.25f, 1.40f}},
                {23, {0.85f, 1.25f, 1.05f}},
                {24, {0.30f, 1.00f, 1.65f}},
                {25, {1.00f, 1.25f, 0.40f}},
                {29, {0.20f, 0.35f, 1.00f}},
                {30, {0.40f, 1.00f}},
                {31, {0.40f, 1.00f}},
                {32, {0.40f, 1.00f}},
                {33, {0.40f, 1.00f}},
            };
            for (Rescale const& r : kRescale)
                for (CompanionSpecies& sp : v)
                    if (sp.id == r.id)
                        for (uint8_t i = 0; i < sp.stageCount; ++i)
                            sp.stages[i].scale = r.scale[i];
            // The new lines (design/COMPANION_CATALOG.md section 1a, 08_companion_lines.sql):
            // stage 1 -> 2 at 20 anywhere, 2 -> 3 at 40 with the family catalyst in the zones the
            // line drops in (45 / 55 for lines that drop above 40). Rideable final forms ride on
            // the wolf's spells and show their own display.
            Add(v, 101, "Webwood Spiderling", 91101, Family::Spider, Role::Melee, {{1986, 0.7f}, {442, 1.25f}, {769, 1.0f, true, 580, 23250, 336}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {141, 148, 10, 331}, Condition::None, CreatureType::None, 0),
                {1986, 442, 769}, CreatureType::Beast, 3);
            Add(v, 102, "Darkmist Hatchling", 91102, Family::Spider, Role::Melee, {{4377, 0.55f}, {4378, 0.8f}, {4380, 1.0f, true, 580, 23250, 2537}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {15}, Condition::None, CreatureType::None, 0),
                {4377, 4378, 4380, 4376, 4379, 4411, 4412, 4413, 4414, 4415}, CreatureType::None, 0);
            Add(v, 103, "Cinderweb Spiderling", 91103, Family::Spider, Role::Melee, {{5856, 0.4f}, {5857, 0.85f}, {5858, 1.0f, true, 580, 23250, 7510}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90102, 1, {51}, Condition::None, CreatureType::None, 0),
                {5856, 5857, 5858}, CreatureType::None, 0);
            Add(v, 104, "Black Bear Cub", 91104, Family::Bear, Role::Tank, {{1128, 0.65f}, {1186, 0.9f}, {1189, 1.0f, true, 580, 23250, 762}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {1, 38, 267}, Condition::None, CreatureType::None, 0),
                {1128, 1186, 1189}, CreatureType::Beast, 4);
            Add(v, 105, "Ironfur Cub", 91105, Family::Bear, Role::Tank, {{1196, 0.6f}, {5268, 0.9f}, {5272, 1.0f, true, 580, 23250, 8838}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {357, 618}, Condition::None, CreatureType::None, 0),
                {1196, 5268, 5272, 5274, 7443, 7444}, CreatureType::None, 0);
            Add(v, 106, "Stranglethorn Kitten", 91106, Family::Cat, Role::Melee, {{681, 0.45f}, {682, 0.9f}, {1085, 1.0f, true, 580, 23250, 698}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {33}, Condition::None, CreatureType::None, 0),
                {681, 682, 1085}, CreatureType::None, 0);
            Add(v, 107, "Mountain Lion Cub", 91107, Family::Cat, Role::Melee, {{2384, 0.5f}, {2406, 0.85f}, {2407, 1.0f, true, 580, 23250, 2300}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {267, 36, 45, 400}, Condition::None, CreatureType::None, 0),
                {2384, 2406, 2407, 2385, 2961, 3035, 4124}, CreatureType::None, 0);
            Add(v, 108, "Savannah Cub", 91108, Family::Cat, Role::Melee, {{3425, 0.4f}, {3241, 0.7f}, {5828, 1.0f, true, 580, 23250, 4424}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17}, Condition::None, CreatureType::None, 0),
                {3425, 3241, 5828, 3243, 3415, 3416, 5766}, CreatureType::None, 0);
            Add(v, 109, "Bloodsnout Pup", 91109, Family::Wolf, Role::Melee, {{1765, 0.55f}, {1923, 1.0f}, {9697, 1.0f, true, 580, 23250, 9370}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {130, 44, 46}, Condition::None, CreatureType::None, 0),
                {1765, 1923, 9697, 1766, 3861, 3862, 5058, 7055, 9416, 9690, 9694, 9696, 10221}, CreatureType::None, 0);
            Add(v, 110, "Silvermane Pup", 91110, Family::Wolf, Role::Melee, {{2923, 0.5f}, {2924, 1.0f}, {2926, 1.0f, true, 580, 23250, 11418}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90102, 1, {47}, Condition::None, CreatureType::None, 0),
                {2923, 2924, 2926, 2925}, CreatureType::None, 0);
            Add(v, 111, "Timber Wolf Pup", 91111, Family::Wolf, Role::Melee, {{834, 0.5f}, {833, 1.0f}, {2729, 1.0f, true, 580, 23250, 1164}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {40, 3}, Condition::None, CreatureType::None, 0),
                {834, 833, 2729, 2727, 2728, 2730}, CreatureType::None, 0);
            Add(v, 112, "Sunscale Hatchling", 91112, Family::Raptor, Role::Melee, {{3254, 0.45f}, {3255, 0.9f}, {3256, 1.0f, true, 580, 23250, 4442}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17}, Condition::None, CreatureType::None, 0),
                {3254, 3255, 3256}, CreatureType::None, 0);
            Add(v, 113, "Deviate Hatchling", 91113, Family::Raptor, Role::Melee, {{3634, 0.45f}, {3633, 0.9f}, {3637, 1.0f, true, 580, 23250, 755}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {718, 17}, Condition::None, CreatureType::None, 0),
                {3634, 3633, 3637, 3636}, CreatureType::None, 0);
            Add(v, 114, "Bloodfen Hatchling", 91114, Family::Raptor, Role::Melee, {{4351, 0.4f}, {4355, 0.8f}, {4357, 1.0f, true, 580, 23250, 2573}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {15}, Condition::None, CreatureType::None, 0),
                {4351, 4355, 4357, 4352, 4356}, CreatureType::None, 0);
            Add(v, 115, "Venomtail Scorpling", 91115, Family::Scorpid, Role::Melee, {{3125, 0.45f}, {3127, 0.9f}, {4139, 1.0f, true, 580, 23250, 2491}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {14, 17, 3}, Condition::None, CreatureType::None, 0),
                {3125, 3127, 4139}, CreatureType::Beast, 20);
            Add(v, 116, "Stonelash Scorpling", 91116, Family::Scorpid, Role::Melee, {{7078, 1.0f}, {11735, 0.8f}, {11737, 1.0f, true, 580, 23250, 15385}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {440, 1377}, Condition::None, CreatureType::None, 0),
                {7078, 11735, 11737, 11736}, CreatureType::None, 0);
            Add(v, 117, "Drywallow Hatchling", 91117, Family::Crocolisk, Role::Tank, {{4341, 0.4f}, {4343, 0.8f}, {4345, 1.0f, true, 580, 23250, 807}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {15, 33}, Condition::None, CreatureType::None, 0),
                {4341, 4343, 4345}, CreatureType::Beast, 6);
            Add(v, 118, "Snapjaw Hatchling", 91118, Family::Turtle, Role::Tank, {{3461, 1.0f}, {2408, 1.2f}, {2505, 1.0f, true, 580, 23250, 5027}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17, 267, 440}, Condition::None, CreatureType::None, 0),
                {3461, 2408, 2505}, CreatureType::Beast, 21);
            Add(v, 119, "Surf Glider Hatchling", 91119, Family::Turtle, Role::Tank, {{4142, 0.55f}, {5431, 1.0f}, {5432, 1.0f, true, 580, 23250, 5127}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {400, 440}, Condition::None, CreatureType::None, 0),
                {4142, 5431, 5432, 4143, 4144}, CreatureType::None, 0);
            Add(v, 120, "Groddoc Infant", 91120, Family::Gorilla, Role::Tank, {{5260, 0.5f}, {5262, 0.9f}, {6513, 1.55f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90102, 1, {357, 490}, Condition::None, CreatureType::None, 0),
                {5260, 5262, 6513}, CreatureType::Beast, 9);
            Add(v, 121, "Goretusk Piglet", 91121, Family::Boar, Role::Tank, {{454, 0.45f}, {157, 0.8f}, {547, 1.0f, true, 580, 23250, 3035}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {40, 44, 491}, Condition::None, CreatureType::None, 0),
                {454, 157, 547}, CreatureType::Beast, 5);
            Add(v, 122, "Pygmy Crawler", 91122, Family::Crab, Role::Tank, {{3106, 0.6f}, {2235, 1.0f}, {1088, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {14, 40, 11, 148, 267, 33}, Condition::None, CreatureType::None, 0),
                {3106, 2235, 1088}, CreatureType::Beast, 8);
            Add(v, 123, "Cloud Serpent Hatchling", 91123, Family::WindSerpent, Role::Ranged, {{3247, 0.65f}, {4117, 1.0f}, {4119, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {400, 406, 357}, Condition::None, CreatureType::None, 0),
                {3247, 4117, 4119}, CreatureType::Beast, 27);
            Add(v, 124, "Swoop Fledgling", 91124, Family::CarrionBird, Role::Melee, {{2969, 0.65f}, {2970, 1.25f}, {2971, 1.35f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {215, 17, 3}, Condition::None, CreatureType::None, 0),
                {2969, 2970, 2971}, CreatureType::Beast, 7);
            Add(v, 125, "Roc Chick", 91125, Family::CarrionBird, Role::Ranged, {{5428, 0.5f}, {5429, 1.0f}, {5430, 0.9f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90102, 1, {440, 51}, Condition::None, CreatureType::None, 0),
                {5428, 5429, 5430}, CreatureType::None, 0);
            Add(v, 126, "Hecklefang Pup", 91126, Family::Hyena, Role::Melee, {{4127, 0.55f}, {4129, 1.15f}, {4128, 1.7f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17, 400, 405, 440}, Condition::None, CreatureType::None, 0),
                {4127, 4129, 4128}, CreatureType::Beast, 25);
            Add(v, 127, "Strigid Owlet", 91127, Family::Owl, Role::Melee, {{1995, 0.5f}, {1996, 1.0f}, {1997, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {141, 361, 618}, Condition::None, CreatureType::None, 0),
                {1995, 1996, 1997}, CreatureType::None, 0);
            Add(v, 128, "Kraul Batling", 91128, Family::Bat, Role::Melee, {{1554, 1.0f}, {4538, 1.0f}, {4539, 1.3f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {85, 491, 1337}, Condition::None, CreatureType::None, 0),
                {1554, 4538, 4539, 1512, 1513, 1553}, CreatureType::None, 0);
            Add(v, 129, "Foreststrider Fledgling", 91129, Family::Tallstrider, Role::Melee, {{2321, 0.5f}, {2322, 1.0f}, {2323, 1.0f, true, 580, 23250, 1283}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {148}, Condition::None, CreatureType::None, 0),
                {2321, 2322, 2323}, CreatureType::None, 0);
            Add(v, 130, "Aged Kodo Calf", 91130, Family::Kodo, Role::Melee, {{3234, 0.45f}, {4700, 0.85f}, {4702, 1.0f, true, 580, 23250, 2767}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {17, 405}, Condition::None, CreatureType::None, 0),
                {3234, 4700, 4702, 4701}, CreatureType::None, 0);
            Add(v, 131, "Gritjaw Hatchling", 91131, Family::Basilisk, Role::Melee, {{4044, 0.6f}, {4728, 0.9f}, {4729, 1.0f, true, 580, 23250, 7345}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {3, 33, 406, 4, 440}, Condition::None, CreatureType::None, 0),
                {4044, 4728, 4729, 688, 689, 690, 1550, 1551, 4041, 4042, 4147, 4863, 5419, 5990, 11785}, CreatureType::None, 0);
            Add(v, 132, "Glasshide Hatchling", 91132, Family::Basilisk, Role::Melee, {{4147, 0.6f}, {5419, 1.0f}, {5421, 1.0f, true, 580, 23250, 12339}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {400, 440}, Condition::None, CreatureType::None, 0),
                {4147, 5419, 5421, 4150, 4151, 5420}, CreatureType::None, 0);
            Add(v, 133, "Thresher Fry", 91133, Family::Threshadon, Role::Tank, {{1224, 0.55f}, {2187, 1.15f}, {4390, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {148, 11, 15}, Condition::None, CreatureType::None, 0),
                {1224, 2187, 4390, 2185, 2188, 4388, 4389, 4827, 14357}, CreatureType::None, 0);
            Add(v, 134, "Mystlash Hydraling", 91134, Family::Hydra, Role::Ranged, {{3721, 0.4f}, {3721, 1.0f}, {4374, 0.8f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {331, 16}, Condition::None, CreatureType::None, 0),
                {3721, 4374}, CreatureType::None, 0);
            Add(v, 135, "Timberling Sprout", 91135, Family::BogBeast, Role::Melee, {{2022, 0.65f}, {1955, 1.15f}, {764, 1.6f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90103, 1, {141, 11, 8}, Condition::None, CreatureType::None, 0),
                {2022, 1955, 764, 765, 1040, 1956, 2025, 2027, 2029, 4382}, CreatureType::None, 0);
            Add(v, 136, "Toxic Mote", 91136, Family::PoisonElemental, Role::Ranged, {{5897, 0.8f}, {6220, 1.15f}, {8519, 1.5f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90103, 1, {17, 721, 28, 139}, Condition::None, CreatureType::None, 0),
                {5897, 6220, 8519, 7132}, CreatureType::None, 0);
            Add(v, 137, "Cracked Golemling", 91137, Family::EarthElemental, Role::Tank, {{2156, 0.55f}, {2723, 1.0f}, {5854, 1.4f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90103, 1, {36, 3, 46}, Condition::None, CreatureType::None, 0),
                {2156, 2723, 5854, 2749, 2751, 5853, 8279, 8906, 8908}, CreatureType::None, 0);
            Add(v, 138, "Ectoplasm Blob", 91138, Family::Ooze, Role::Melee, {{3638, 0.75f}, {1030, 1.25f}, {1033, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {11, 718}, Condition::None, CreatureType::None, 0),
                {3638, 1030, 1033, 1032, 3640}, CreatureType::None, 0);
            Add(v, 139, "Creeping Sludgeling", 91139, Family::Ooze, Role::Melee, {{12222, 1.0f}, {2655, 1.25f}, {2656, 1.2f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 0, 0, {2100, 357, 490}, Condition::None, CreatureType::None, 0),
                {12222, 2655, 2656}, CreatureType::None, 0);
            Add(v, 140, "Black Drake Hatchling", 91140, Family::Dragon, Role::Ranged, {{4323, 0.45f}, {7048, 0.8f}, {7045, 1.0f, true, 580, 23250, 9585}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90101, 1, {51, 46, 1583}, Condition::None, CreatureType::None, 0),
                {4323, 7048, 7045, 2725, 4324}, CreatureType::None, 0);
            Add(v, 141, "Imp Familiar", 91141, Family::Imp, Role::Ranged, {{416, 1.0f}, {9776, 1.0f}, {9778, 1.45f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90104, 1, {46, 361}, Condition::None, CreatureType::None, 0),
                {416, 9776, 9778, 9777, 13276}, CreatureType::None, 0);
            Add(v, 142, "Felstalker Pup", 91142, Family::Felhound, Role::Melee, {{3102, 1.0f}, {6071, 1.2f}, {4685, 1.0f, true, 580, 23250, 6173}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90104, 1, {14, 4, 2557}, Condition::None, CreatureType::None, 0),
                {3102, 6071, 4685, 6010}, CreatureType::None, 0);
            Add(v, 143, "Doomguard Spawn", 91143, Family::Felguard, Role::Tank, {{3772, 0.5f}, {6011, 0.9f}, {12396, 0.85f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90104, 1, {331, 361, 4, 16}, Condition::None, CreatureType::None, 0),
                {3772, 6011, 12396, 8717}, CreatureType::None, 0);
            Add(v, 144, "Darkhound Whelp", 91144, Family::Darkhound, Role::Melee, {{1547, 0.55f}, {1548, 1.1f}, {8598, 1.3f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90104, 1, {85, 130, 139}, Condition::None, CreatureType::None, 0),
                {1547, 1548, 8598, 1549, 8596, 8597}, CreatureType::None, 0);
            Add(v, 145, "Rattlecage Bones", 91145, Family::Skeleton, Role::Tank, {{1890, 0.45f}, {1520, 1.25f}, {1788, 1.2f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90105, 1, {85, 10, 28, 139}, Condition::None, CreatureType::None, 0),
                {1890, 1520, 1788, 48, 531, 7343, 10478, 10816, 11258}, CreatureType::None, 0);
            Add(v, 146, "Ghoul Thrall", 91146, Family::Ghoul, Role::Tank, {{1866, 0.45f}, {1791, 0.85f}, {8543, 0.6f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90105, 1, {130, 28, 139}, Condition::None, CreatureType::None, 0),
                {1866, 1791, 8543, 511, 846, 1793, 1794, 7347, 7348, 8530, 8531, 8545, 10405, 10406, 10407, 10495, 10801, 16244, 16447}, CreatureType::None, 0);
            Add(v, 147, "Wailing Shade", 91147, Family::Banshee, Role::Ranged, {{1534, 0.55f}, {6116, 1.0f}, {8541, 1.3f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90105, 1, {85, 16, 28, 139}, Condition::None, CreatureType::None, 0),
                {1534, 6116, 8541, 2178, 3877, 10463, 10464, 11466, 12377}, CreatureType::None, 0);
            Add(v, 148, "Crypt Scuttler", 91148, Family::CryptFiend, Role::Melee, {{8556, 0.4f}, {8555, 0.8f}, {8558, 1.0f, true, 580, 23250, 9758}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90105, 1, {139, 2017}, Condition::None, CreatureType::None, 0),
                {8556, 8555, 8558, 8557, 10412, 10413, 15978}, CreatureType::None, 0);
            Add(v, 149, "Servo", 91149, Family::Mechanical, Role::Tank, {{2922, 0.9f}, {36, 1.0f}, {115, 1.25f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90106, 1, {40, 721}, Condition::None, CreatureType::None, 0),
                {2922, 36, 115, 114, 480, 2676}, CreatureType::None, 0);
            Add(v, 150, "Compact Reaper", 91150, Family::Mechanical, Role::Tank, {{2676, 1.0f}, {11684, 1.05f}, {4260, 1.0f, true, 580, 23250, 1269}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90106, 1, {406, 331, 33}, Condition::None, CreatureType::None, 0),
                {2676, 11684, 4260, 115, 573, 642}, CreatureType::None, 0);
            Add(v, 151, "Mechano-Tankling", 91151, Family::Mechanical, Role::Ranged, {{6225, 0.35f}, {6226, 0.8f}, {6227, 1.0f, true, 580, 23250, 6891}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90106, 1, {721}, Condition::None, CreatureType::None, 0),
                {6225, 6226, 6227}, CreatureType::None, 0);
            Add(v, 152, "Riverpaw Runt", 91152, Family::Gnoll, Role::Melee, {{97, 0.55f}, {124, 1.0f}, {445, 1.35f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {40, 44, 10, 267, 357}, Condition::None, CreatureType::None, 0),
                {97, 124, 445, 98, 117, 123, 125, 423, 424, 426, 430, 446, 452, 478, 500, 580, 712, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 1426, 5249, 5254}, CreatureType::None, 0);
            Add(v, 153, "Kobold Vermin", 91153, Family::Kobold, Role::Melee, {{6, 0.75f}, {475, 1.25f}, {2574, 1.25f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {12, 40, 45, 400}, Condition::None, CreatureType::None, 0),
                {6, 475, 2574, 40, 80, 257, 476, 1172, 1174, 1176, 1177, 1236, 2572, 2573}, CreatureType::None, 0);
            Add(v, 154, "Rockjaw Trogg", 91154, Family::Trogg, Role::Tank, {{707, 0.75f}, {1163, 1.0f}, {4855, 0.85f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {1, 38, 3, 1337}, Condition::None, CreatureType::None, 0),
                {707, 1163, 4855, 724, 1115, 1116, 1117, 1118, 1161, 1164, 1165, 1718, 2892, 2893, 2894, 4850, 4851, 4852, 4853, 4856, 6733, 7175, 7320, 7321, 10987, 11318}, CreatureType::None, 0);
            Add(v, 155, "Ogre Runt", 91155, Family::Ogre, Role::Tank, {{1178, 0.5f}, {2566, 1.0f}, {5241, 0.85f}},
                Rule(45, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(55, 90107, 1, {10, 45, 36, 357, 46}, Condition::None, CreatureType::None, 0),
                {1178, 2566, 5241, 212, 889, 891, 892, 1180, 1251, 1487, 2562, 2564, 2567, 2569, 2570, 2571, 5229, 5232, 5234, 5236, 5240}, CreatureType::None, 0);
            Add(v, 156, "Kolkar Drudge", 91156, Family::Centaur, Role::Ranged, {{3119, 0.55f}, {4093, 1.0f}, {5601, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {17, 400, 405}, Condition::None, CreatureType::None, 0),
                {3119, 4093, 5601, 2967, 2968, 3120, 3272, 3273, 3274, 3275, 3397, 4095, 4632, 4634, 4635, 4638, 4639, 4640, 4642, 4643, 4644, 4646, 4648, 4651, 4652, 7404, 10617, 11559}, CreatureType::None, 0);
            Add(v, 157, "Bristleback Piglet", 91157, Family::Quilboar, Role::Ranged, {{2952, 0.55f}, {3266, 0.8f}, {4436, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {215, 17, 491}, Condition::None, CreatureType::None, 0),
                {2952, 3266, 4436, 2953, 3111, 3112, 3113, 3114, 3232, 3258, 3260, 3261, 3263, 3265, 3267, 3268, 3269, 3271, 3456, 3457, 3458, 3459, 4435, 4437, 4438, 4440, 4442, 4520, 4522, 4523, 4525, 4530, 4531, 4532, 6035, 6132, 7873, 7874}, CreatureType::None, 0);
            Add(v, 158, "Gnarlpine Cub", 91158, Family::Furbolg, Role::Melee, {{2006, 0.6f}, {3921, 1.0f}, {7440, 0.8f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {141, 148, 331, 361, 618}, Condition::None, CreatureType::None, 0),
                {2006, 3921, 7440, 2007, 2008, 2009, 2011, 2012, 2014, 2152, 2167, 2168, 2169, 2170, 2171, 2324, 3743, 3745, 3746, 3922, 3923, 3924, 3925, 3926, 7153, 7154, 7155, 7156, 7235, 7438, 7439, 7442, 10916}, CreatureType::None, 0);
            Add(v, 159, "Windfury Fledgling", 91159, Family::Harpy, Role::Ranged, {{2962, 0.55f}, {2964, 1.15f}, {2965, 1.5f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {215, 17, 406, 405, 357}, Condition::None, CreatureType::None, 0),
                {2962, 2964, 2965, 2963, 3276, 3277, 3278, 3279, 3280, 4022, 4023, 12579}, CreatureType::None, 0);
            Add(v, 160, "Troll Whelp", 91160, Family::Troll, Role::Ranged, {{706, 0.55f}, {597, 1.0f}, {9268, 1.1f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {1, 33, 47, 1583}, Condition::None, CreatureType::None, 0),
                {706, 597, 9268, 587, 588, 667, 669, 672, 699, 702, 781, 783, 946, 1120, 1121, 1122, 1123, 1124, 1397, 2552, 2553, 2554, 2555, 2556, 2557, 2558, 2639, 2640, 2641, 2642, 2643, 2644, 2645, 2646, 2647, 2648, 2649, 2650, 2651, 2652, 2653, 2654, 4465, 4466, 4467, 9239, 9240, 9241, 9265, 9266, 9267, 9269}, CreatureType::None, 0);
            Add(v, 161, "Stormscale Wave Rider", 91161, Family::Naga, Role::Tank, {{2179, 0.55f}, {3711, 1.0f}, {4366, 1.1f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {148, 331, 16}, Condition::None, CreatureType::None, 0),
                {2179, 3711, 4366, 2180, 2181, 2182, 2183, 3712, 3713, 3715, 4364, 4368, 4370, 4371, 4711, 4712, 4713, 4714, 4715, 4716, 4718, 4719, 6190, 6194, 6195, 7885}, CreatureType::None, 0);
            Add(v, 162, "Makrura Clacker", 91162, Family::Makrura, Role::Tank, {{3103, 0.7f}, {4401, 1.0f}, {6350, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {14, 15, 16}, Condition::None, CreatureType::None, 0),
                {3103, 4401, 6350, 3104, 3105, 3141, 4402, 4403, 4404, 6370, 6372}, CreatureType::None, 0);
            Add(v, 163, "Young Moonkin", 91163, Family::Moonkin, Role::Ranged, {{10159, 0.5f}, {10158, 1.0f}, {7452, 1.5f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {148, 47, 618}, Condition::None, CreatureType::None, 0),
                {10159, 10158, 7452, 2927, 2928, 2929, 7450, 7451, 10157, 10160}, CreatureType::None, 0);
            Add(v, 164, "Moonrage Whelp", 91164, Family::Worgen, Role::Melee, {{1769, 0.6f}, {1782, 0.9f}, {920, 1.2f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90107, 1, {130, 10, 209}, Condition::None, CreatureType::None, 0),
                {1769, 1782, 920, 205, 206, 533, 898, 1770, 1779, 1924}, CreatureType::None, 0);
            // The vanilla 1.12 pets with no line yet (31_vanilla_companions.sql): each one's first
            // form is the pet itself; a second form where a bigger one makes sense.
            Add(v, 165, "Shadowmaw Kitten", 91165, Family::Critter, Role::Melee, {{7385, 1.0f}, {7385, 1.45f}, {7385, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7385}, CreatureType::None, 0);
            Add(v, 166, "Ridge Stalker Kitten", 91166, Family::Critter, Role::Melee, {{7384, 1.0f}, {7384, 1.45f}, {7384, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7384}, CreatureType::None, 0);
            Add(v, 167, "Jungle Tiger Kitten", 91167, Family::Critter, Role::Melee, {{7382, 1.0f}, {7382, 1.45f}, {7382, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7382}, CreatureType::None, 0);
            Add(v, 168, "Winterspring Kitten", 91168, Family::Critter, Role::Melee, {{7386, 1.0f}, {7386, 1.45f}, {7386, 0.85f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7386}, CreatureType::None, 0);
            Add(v, 169, "Ice Tiger Kitten", 91169, Family::Critter, Role::Melee, {{7380, 1.0f}, {7380, 1.45f}, {7380, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7380}, CreatureType::None, 0);
            Add(v, 170, "Nightstalker Kitten", 91170, Family::Critter, Role::Melee, {{7383, 1.0f}, {7383, 1.45f}, {7383, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7383}, CreatureType::None, 0);
            Add(v, 171, "Corrupted Kitten", 91171, Family::Critter, Role::Melee, {{9936, 1.0f}, {9936, 1.45f}, {9936, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {9936}, CreatureType::None, 0);
            Add(v, 172, "Green Wing Macaw", 91172, Family::Critter, Role::Melee, {{7387, 1.0f}, {7387, 0.8f}, {7387, 0.6f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7387}, CreatureType::None, 0);
            Add(v, 173, "Hyacinth Macaw", 91173, Family::Critter, Role::Melee, {{7391, 1.0f}, {7391, 0.8f}, {7391, 0.7f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7391}, CreatureType::None, 0);
            Add(v, 174, "Senegal Parrot", 91174, Family::Critter, Role::Melee, {{7389, 1.0f}, {7389, 0.8f}, {7389, 0.6f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7389}, CreatureType::None, 0);
            Add(v, 175, "Cockatiel", 91175, Family::Critter, Role::Melee, {{7390, 1.0f}, {7390, 0.8f}, {7390, 0.6f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7390}, CreatureType::None, 0);
            Add(v, 176, "Snowy Owlet", 91176, Family::Owl, Role::Melee, {{7553, 1.0f}, {7456, 0.6f}, {7456, 0.8f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7553}, CreatureType::None, 0);
            Add(v, 177, "Olive Serpent Hatchling", 91177, Family::Critter, Role::Melee, {{7565, 1.0f}, {7565, 1.85f}, {7565, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7565}, CreatureType::None, 0);
            Add(v, 178, "Brown Snake", 91178, Family::Critter, Role::Melee, {{7562, 1.0f}, {7562, 1.85f}, {7562, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7562}, CreatureType::None, 0);
            Add(v, 179, "Crimson Snake", 91179, Family::Critter, Role::Melee, {{7567, 1.0f}, {7567, 1.85f}, {7567, 1.4f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7567}, CreatureType::None, 0);
            Add(v, 180, "Cockroach", 91180, Family::Critter, Role::Melee, {{7395, 0.6f}, {7395, 1.4f}, {7395, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7395}, CreatureType::None, 0);
            Add(v, 181, "Prairie Dog", 91181, Family::Critter, Role::Melee, {{14421, 1.0f}, {14421, 1.1f}, {14421, 0.9f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14421}, CreatureType::None, 0);
            Add(v, 182, "Mechanostrider Hatchling", 91182, Family::Mechanical, Role::Melee, {{8376, 1.0f}, {8376, 0.65f}, {8376, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {8376}, CreatureType::None, 0);
            Add(v, 183, "Ivory Tallstrider Chick", 91183, Family::Critter, Role::Melee, {{7394, 1.0f}, {7394, 2.3f}, {7394, 1.25f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7394}, CreatureType::None, 0);
            Add(v, 184, "Gray Tallstrider Chick", 91184, Family::Critter, Role::Melee, {{7392, 1.0f}, {7392, 2.3f}, {7392, 1.25f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7392}, CreatureType::None, 0);
            Add(v, 185, "Tree Frog", 91185, Family::Critter, Role::Melee, {{7549, 0.7f}, {7549, 1.4f}, {7549, 2.5f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7549}, CreatureType::None, 0);
            Add(v, 186, "Wood Frog", 91186, Family::Critter, Role::Melee, {{7550, 0.7f}, {7550, 1.4f}, {7550, 2.5f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {7550}, CreatureType::None, 0);
            Add(v, 187, "Pet Bombling", 91187, Family::Mechanical, Role::Melee, {{9656, 1.0f}, {9656, 1.0f}, {9656, 0.75f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {9656}, CreatureType::None, 0);
            Add(v, 188, "Lil' Smoky", 91188, Family::Mechanical, Role::Melee, {{9657, 0.8f}, {9657, 0.6f}, {9657, 1.2f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {9657}, CreatureType::None, 0);
            Add(v, 189, "Lifelike Toad", 91189, Family::Mechanical, Role::Melee, {{12419, 0.7f}, {12419, 1.4f}, {12419, 2.5f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {12419}, CreatureType::None, 0);
            Add(v, 190, "Tranquil Mechanical Yeti", 91190, Family::Mechanical, Role::Melee, {{15699, 1.0f}, {15699, 0.5f}, {15699, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {15699}, CreatureType::None, 0);
            Add(v, 191, "Smolderweb Hatchling", 91191, Family::Spider, Role::Melee, {{10598, 0.9f}, {5857, 0.5f}, {5857, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {10598}, CreatureType::None, 0);
            Add(v, 192, "Zergling", 91192, Family::Critter, Role::Melee, {{11327, 1.0f}, {11327, 1.2f}, {11327, 0.65f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {11327}, CreatureType::None, 0);
            Add(v, 193, "Panda Cub", 91193, Family::Critter, Role::Melee, {{11325, 1.0f}, {11325, 1.6f}, {11325, 1.1f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {11325}, CreatureType::None, 0);
            Add(v, 194, "Mini Diablo", 91194, Family::Critter, Role::Melee, {{11326, 1.0f}, {11326, 1.0f}, {11326, 0.8f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {11326}, CreatureType::None, 0);
            Add(v, 195, "Albino Snapjaw", 91195, Family::Turtle, Role::Tank, {{14633, 1.0f}, {2408, 0.8f}, {2408, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14633}, CreatureType::None, 0);
            Add(v, 196, "Loggerhead Snapjaw", 91196, Family::Turtle, Role::Tank, {{14629, 1.0f}, {2408, 0.8f}, {2408, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14629}, CreatureType::None, 0);
            Add(v, 197, "Hawksbill Snapjaw", 91197, Family::Turtle, Role::Tank, {{14632, 1.0f}, {2408, 0.8f}, {2408, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14632}, CreatureType::None, 0);
            Add(v, 198, "Leatherback Snapjaw", 91198, Family::Turtle, Role::Tank, {{14630, 1.0f}, {2408, 0.8f}, {2408, 0.95f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14630}, CreatureType::None, 0);
            Add(v, 199, "Olive Snapjaw", 91199, Family::Turtle, Role::Tank, {{14631, 1.0f}, {2408, 0.6f}, {2408, 0.8f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14631}, CreatureType::None, 0);
            Add(v, 200, "Tiny Red Dragon", 91200, Family::Dragon, Role::Ranged, {{14756, 0.75f}, {1069, 0.3f}, {13976, 0.2f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14756}, CreatureType::None, 0);
            Add(v, 201, "Tiny Green Dragon", 91201, Family::Dragon, Role::Ranged, {{14755, 0.75f}, {741, 0.3f}, {742, 0.2f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14755}, CreatureType::None, 0);
            Add(v, 202, "Jubling", 91202, Family::Critter, Role::Melee, {{14878, 0.65f}, {14878, 1.65f}, {14878, 1.15f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {14878}, CreatureType::None, 0);
            Add(v, 203, "Murki", 91203, Family::Murloc, Role::Melee, {{15361, 1.0f}, {735, 1.25f}, {735, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {15361}, CreatureType::None, 0);
            Add(v, 204, "Gurky", 91204, Family::Murloc, Role::Melee, {{16069, 1.0f}, {735, 1.25f}, {735, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16069}, CreatureType::None, 0);
            Add(v, 205, "Terky", 91205, Family::Murloc, Role::Melee, {{16445, 1.0f}, {735, 1.25f}, {735, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16445}, CreatureType::None, 0);
            Add(v, 206, "Baby Shark", 91206, Family::Threshadon, Role::Tank, {{15661, 1.0f}, {2187, 0.7f}, {2187, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {15661}, CreatureType::None, 0);
            Add(v, 207, "Peddlefeet", 91207, Family::Critter, Role::Melee, {{16085, 1.0f}, {16085, 1.0f}, {16085, 1.65f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16085}, CreatureType::None, 0);
            Add(v, 208, "Poley", 91208, Family::Bear, Role::Tank, {{16456, 1.0f}, {1196, 0.9f}, {1196, 1.3f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16456}, CreatureType::None, 0);
            Add(v, 209, "Speedy", 91209, Family::Turtle, Role::Tank, {{16547, 1.0f}, {2408, 1.0f}, {2408, 1.55f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16547}, CreatureType::None, 0);
            Add(v, 210, "Mr. Wiggles", 91210, Family::Boar, Role::Tank, {{16548, 1.0f}, {157, 1.2f}, {157, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16548}, CreatureType::None, 0);
            Add(v, 211, "Whiskers the Rat", 91211, Family::Critter, Role::Melee, {{16549, 0.75f}, {16549, 1.6f}, {16549, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16549}, CreatureType::None, 0);
            Add(v, 212, "Spirit of Summer", 91212, Family::FireElemental, Role::Ranged, {{16701, 0.3f}, {6296, 0.8f}, {6296, 2.25f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {16701}, CreatureType::None, 0);
            Add(v, 213, "White Tiger Cub", 91213, Family::Cat, Role::Melee, {{17254, 1.0f}, {7431, 0.8f}, {7431, 1.0f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {17254}, CreatureType::None, 0);
            Add(v, 214, "Hippogryph Hatchling", 91214, Family::Critter, Role::Melee, {{17255, 1.0f}, {17255, 0.55f}, {17255, 1.05f}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 0, 0, {}, Condition::None, CreatureType::None, 0),
                {17255}, CreatureType::None, 0);
            // The flying mounts' lines (35_flying_companions.sql, design/COMPANION_CATALOG.md
            // section 1g): a young form, a grown one, and a last form ridden as the Wrath riding
            // model (kFlyingLooks below) on the wolf's spells; it skyrides (Skyriding.h), its mounted
            // move Launch (CompanionMoves.h, kSkySurge). 1 -> 2 at 20
            // anywhere, 2 -> 3 at 40 with the family's catalyst where the line lives.
            Add(v, 215, "Gryphon Hatchling", 91215, Family::Gryphon, Role::Melee, {{2347, 1.0f}, {2658, 0.6f}, {2658, 1.0f, true, 580, 23250, 40020}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {47}, Condition::None, CreatureType::None, 0),
                {2347, 2658}, CreatureType::None, 0);
            Add(v, 216, "Wind Rider Cub", 91216, Family::WindRider, Role::Melee, {{4012, 1.0f}, {4107, 0.65f}, {4107, 1.0f, true, 580, 23250, 40022}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {400, 406}, Condition::None, CreatureType::None, 0),
                {4012, 4107}, CreatureType::None, 0);
            Add(v, 217, "Frayfeather Hatchling", 91217, Family::Hippogryph, Role::Melee, {{5300, 1.0f}, {5300, 0.55f}, {6375, 1.0f, true, 580, 23250, 40023}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {357, 16}, Condition::None, CreatureType::None, 0),
                {5300, 6375}, CreatureType::None, 0);
            Add(v, 218, "Dragonhawk Hatchling", 91218, Family::Dragonhawk, Role::Ranged, {{6375, 0.9f}, {6375, 0.4f}, {6375, 1.0f, true, 580, 23250, 40026}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90102, 1, {16}, Condition::None, CreatureType::None, 0),
                {6198, 6199, 8581}, CreatureType::None, 0);
            Add(v, 219, "Netherwhelp", 91219, Family::Dragon, Role::Ranged, {{7044, 1.0f}, {7045, 1.0f}, {7046, 1.0f, true, 580, 23250, 40029}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90101, 1, {4}, Condition::None, CreatureType::None, 0),
                {6011, 8716}, CreatureType::None, 0);
            Add(v, 220, "Proto-Drake Whelp", 91220, Family::Dragon, Role::Ranged, {{9166, 0.35f}, {9167, 0.3f}, {9167, 1.0f, true, 580, 23250, 40032}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90101, 1, {490}, Condition::None, CreatureType::None, 0),
                {9166, 9167}, CreatureType::None, 0);
            Add(v, 221, "Frostbrood Whelp", 91221, Family::Dragon, Role::Ranged, {{8600, 0.5f}, {8600, 0.5f}, {8600, 1.0f, true, 580, 23250, 40035}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90105, 1, {139}, Condition::Night, CreatureType::None, 0),
                {8525, 8527, 8529, 10411}, CreatureType::None, 0);
            Add(v, 222, "Phoenix Hatchling", 91222, Family::FireElemental, Role::Ranged, {{6521, 0.4f}, {6520, 0.5f}, {6521, 1.0f, true, 580, 23250, 40036}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90103, 1, {537, 490}, Condition::None, CreatureType::None, 0),
                {6520, 6521, 5850}, CreatureType::None, 0);
            Add(v, 223, "Bronze Whelpling", 91223, Family::Dragon, Role::Ranged, {{7546, 1.0f}, {14025, 0.5f}, {14263, 1.0f, true, 580, 23250, 40037}},
                Rule(20, 0, 0, {}, Condition::None, CreatureType::None, 0),
                Rule(40, 90101, 1, {440}, Condition::None, CreatureType::None, 0),
                {14025, 14263}, CreatureType::None, 0);
            // The look table (29_companion_looks.sql's UPDATEs): every stage's own creature display
            // and its scale on top of that display's own size, chosen from the same family's 1.12
            // displays so each form of a line is its own model or skin and clearly bigger than the
            // one before; a mount line's last form is a mount model at 1.0, the size it is ridden
            // at. The client's companion_ui tests check every id against the install. Lines that
            // ride on the wolf's spells show their last form's display.
            struct Look
            {
                uint16_t id;
                std::array<uint32_t, kMaxStages> display;
                std::array<float, kMaxStages> scale;
            };
            static Look const kLooks[] = {
                {1, {6288, 9586, 8574}, {0.9f, 0.25f, 0.2f}},
                {2, {6290, 6376, 2718}, {1.0f, 0.3f, 0.2f}},
                {3, {6291, 5145, 7863}, {1.0f, 0.3f, 1.35f}},
                {4, {6293, 13971, 1687}, {1.0f, 0.3f, 0.2f}},
                {5, {6294, 9345, 2158}, {1.0f, 0.75f, 1.0f}},
                {6, {9572, 802, 247}, {0.95f, 0.85f, 1.0f}},
                {7, {11454, 9954, 6448}, {0.6f, 0.75f, 1.0f}},
                {8, {16942, 9953, 6080}, {1.15f, 0.8f, 1.0f}},
                {9, {11448, 599, 11450}, {0.6f, 1.0f, 1.5f}},
                {10, {5510, 820, 2281}, {0.65f, 1.0f, 1.9f}},
                {11, {9934, 955, 6808}, {1.8f, 0.8f, 0.85f}},
                {12, {960, 1959, 6473}, {0.5f, 1.2f, 1.0f}},
                {13, {1034, 2548, 807}, {0.55f, 0.8f, 1.0f}},
                {14, {841, 838, 844}, {0.55f, 0.85f, 1.65f}},
                {15, {15534, 4566, 10833}, {0.45f, 1.0f, 0.8f}},
                {16, {4185, 1955, 7894}, {1.0f, 0.8f, 1.15f}},
                {17, {1284, 1220, 1221}, {0.35f, 1.2f, 2.05f}},
                {18, {10987, 3247, 2414}, {0.4f, 1.0f, 1.7f}},
                {19, {8409, 1070, 1405}, {1.6f, 0.8f, 2.25f}},
                {20, {2069, 5561, 525}, {1.4f, 1.0f, 2.0f}},
                {21, {4766, 171, 2533}, {0.18f, 1.0f, 1.25f}},
                {22, {5327, 8714, 8715}, {0.65f, 1.25f, 1.4f}},
                {23, {15369, 527, 1079}, {0.85f, 1.25f, 1.05f}},
                {24, {10906, 21132, 169}, {0.2f, 1.0f, 1.9f}},
                {25, {8573, 7763, 4754}, {0.25f, 1.25f, 0.4f}},
                {26, {748, 16942, 749}, {0.55f, 1.45f, 0.95f}},
                {27, {328, 4626, 16191}, {1.0f, 1.8f, 1.05f}},
                {28, {14379, 8910, 6891}, {0.27f, 1.15f, 0.75f}},
                {29, {1454, 1230, 11641}, {0.2f, 0.45f, 1.0f}},
                {30, {229, 908, 2404}, {0.4f, 0.75f, 1.0f}},
                {31, {10666, 10661, 6569}, {0.35f, 0.6f, 1.0f}},
                {32, {5050, 5228, 10670}, {0.4f, 0.75f, 1.0f}},
                {33, {10000, 10003, 2736}, {0.4f, 0.8f, 1.0f}},
                {101, {709, 366, 336}, {0.7f, 1.25f, 1.0f}},
                {102, {2536, 14853, 2537}, {2.2f, 0.75f, 1.0f}},
                {103, {4457, 963, 7510}, {0.27f, 0.7f, 1.0f}},
                {104, {8843, 11908, 762}, {0.65f, 0.7f, 1.0f}},
                {105, {8840, 865, 8838}, {0.45f, 0.9f, 1.0f}},
                {106, {15151, 1043, 6443}, {0.7f, 0.9f, 1.0f}},
                {107, {1059, 1043, 6442}, {0.5f, 1.0f, 1.0f}},
                {108, {1978, 1977, 9714}, {0.4f, 0.7f, 1.0f}},
                {109, {15204, 741, 2326}, {1.15f, 0.75f, 1.0f}},
                {110, {855, 11419, 14336}, {1.0f, 0.75f, 1.0f}},
                {111, {9572, 3202, 2328}, {1.15f, 0.65f, 1.0f}},
                {112, {675, 1747, 6472}, {0.45f, 0.9f, 1.0f}},
                {113, {674, 949, 4806}, {0.5f, 0.9f, 1.0f}},
                {114, {2571, 788, 6469}, {0.3f, 0.7f, 1.0f}},
                {115, {5905, 2732, 2491}, {1.5f, 0.9f, 1.0f}},
                {116, {13097, 2765, 15385}, {0.5f, 1.1f, 1.0f}},
                {117, {831, 2548, 807}, {0.38f, 0.8f, 1.0f}},
                {118, {14660, 1244, 5027}, {1.5f, 1.15f, 1.0f}},
                {119, {6368, 7837, 5127}, {0.75f, 1.0f, 1.0f}},
                {120, {837, 3188, 5294}, {0.45f, 0.9f, 1.55f}},
                {121, {6807, 704, 3035}, {0.45f, 0.8f, 1.0f}},
                {122, {641, 981, 699}, {0.5f, 1.0f, 1.15f}},
                {123, {3006, 2705, 7829}, {0.35f, 1.0f, 0.7f}},
                {124, {1192, 2305, 10826}, {0.21f, 1.05f, 0.9f}},
                {125, {490, 2305, 10827}, {0.26f, 1.15f, 0.95f}},
                {126, {2726, 2711, 10902}, {0.55f, 1.15f, 1.5f}},
                {127, {15534, 4877, 10828}, {0.45f, 1.0f, 1.15f}},
                {128, {9535, 1955, 11032}, {0.9f, 1.0f, 0.8f}},
                {129, {1281, 178, 6477}, {0.4f, 1.0f, 1.0f}},
                {130, {10916, 1308, 12241}, {0.15f, 0.3f, 1.0f}},
                {131, {1074, 4486, 7345}, {0.22f, 0.9f, 1.0f}},
                {132, {1073, 8797, 12339}, {0.85f, 1.0f, 1.0f}},
                {133, {2425, 994, 6431}, {0.2f, 1.15f, 1.1f}},
                {134, {2728, 10898, 2423}, {0.2f, 1.0f, 0.8f}},
                {135, {5712, 2567, 2024}, {0.58f, 1.15f, 1.6f}},
                {136, {5491, 9449, 5497}, {0.9f, 0.9f, 1.5f}},
                {137, {2690, 10804, 10801}, {0.34f, 1.0f, 1.4f}},
                {138, {7763, 360, 11139}, {1.55f, 1.25f, 1.05f}},
                {139, {14350, 1145, 15686}, {0.65f, 1.25f, 0.25f}},
                {140, {387, 8713, 9585}, {0.6f, 0.95f, 1.0f}},
                {141, {10811, 10812, 12190}, {0.75f, 1.4f, 1.35f}},
                {142, {850, 12168, 6173}, {0.5f, 0.85f, 1.0f}},
                {143, {433, 9017, 4426}, {0.5f, 0.9f, 0.85f}},
                {144, {7892, 9020, 9022}, {0.6f, 1.1f, 1.3f}},
                {145, {1019, 200, 11489}, {0.55f, 1.25f, 1.2f}},
                {146, {3670, 570, 1693}, {0.8f, 0.85f, 0.6f}},
                {147, {985, 10702, 8783}, {0.55f, 1.0f, 1.15f}},
                {148, {9793, 3004, 9758}, {0.22f, 0.8f, 1.0f}},
                {149, {1150, 367, 548}, {0.9f, 1.0f, 1.1f}},
                {150, {1159, 1303, 1269}, {0.7f, 1.05f, 1.0f}},
                {151, {14379, 6890, 6891}, {0.27f, 0.8f, 1.0f}},
                {152, {10791, 384, 609}, {0.55f, 1.0f, 1.35f}},
                {153, {10913, 2299, 511}, {0.75f, 1.1f, 1.25f}},
                {154, {990, 160, 6533}, {0.5f, 1.0f, 1.0f}},
                {155, {11571, 3193, 11561}, {0.28f, 1.0f, 0.85f}},
                {156, {9442, 9414, 9433}, {0.5f, 1.0f, 1.15f}},
                {157, {1967, 1253, 6108}, {0.55f, 0.8f, 1.0f}},
                {158, {6827, 1999, 6828}, {0.45f, 1.0f, 1.15f}},
                {159, {10873, 1255, 10874}, {0.34f, 1.15f, 1.35f}},
                {160, {5606, 4579, 7803}, {0.55f, 1.0f, 1.1f}},
                {161, {4921, 6747, 9134}, {0.45f, 1.0f, 1.1f}},
                {162, {2598, 2426, 1817}, {0.27f, 1.0f, 1.15f}},
                {163, {12234, 5927, 12237}, {0.35f, 1.0f, 1.5f}},
                {164, {657, 1979, 1098}, {0.6f, 0.9f, 1.2f}},
                {165, {6943, 16942, 2437}, {0.75f, 1.45f, 1.15f}},
                {166, {2278, 16942, 1056}, {0.9f, 1.45f, 0.95f}},
                {167, {918, 16942, 1008}, {0.45f, 1.45f, 1.15f}},
                {168, {6248, 16942, 11446}, {0.75f, 1.45f, 0.85f}},
                {169, {9951, 16942, 9949}, {0.37f, 1.45f, 0.95f}},
                {170, {321, 16942, 11449}, {0.4f, 1.45f, 1.15f}},
                {171, {9993, 16942, 9230}, {0.75f, 1.45f, 0.95f}},
                {172, {5866, 8818, 5865}, {1.0f, 0.8f, 0.6f}},
                {173, {6192, 8819, 8816}, {1.0f, 0.8f, 1.4f}},
                {174, {6190, 8816, 8819}, {1.0f, 0.8f, 1.4f}},
                {175, {6191, 8817, 8818}, {1.0f, 0.8f, 1.4f}},
                {176, {15534, 4877, 10833}, {0.45f, 0.6f, 0.8f}},
                {177, {2995, 2955, 14557}, {0.32f, 1.85f, 1.05f}},
                {178, {2957, 2958, 15182}, {1.0f, 1.85f, 1.25f}},
                {179, {6303, 2956, 4312}, {1.0f, 1.85f, 1.4f}},
                {180, {2177, 7470, 157}, {0.6f, 1.4f, 0.95f}},
                {181, {1072, 2838, 4468}, {1.0f, 1.1f, 0.9f}},
                {182, {6569, 6978, 10666}, {0.35f, 0.65f, 1.0f}},
                {183, {1284, 2741, 1221}, {0.35f, 0.55f, 1.25f}},
                {184, {2741, 1219, 1220}, {0.35f, 0.85f, 1.25f}},
                {185, {14932, 6297, 1924}, {0.47f, 1.4f, 2.5f}},
                {186, {901, 6296, 5379}, {0.7f, 1.4f, 1.65f}},
                {187, {8909, 6977, 6890}, {1.0f, 1.0f, 0.75f}},
                {188, {8910, 14379, 1303}, {0.8f, 0.6f, 1.2f}},
                {189, {901, 6297, 5379}, {0.7f, 1.4f, 1.65f}},
                {190, {10269, 3207, 927}, {1.0f, 0.5f, 1.2f}},
                {191, {4458, 963, 7510}, {0.21f, 0.5f, 1.0f}},
                {192, {10993, 9469, 11091}, {1.0f, 1.2f, 0.65f}},
                {193, {10990, 16189, 913}, {1.0f, 1.6f, 1.1f}},
                {194, {10992, 5049, 4426}, {1.0f, 1.0f, 0.8f}},
                {195, {14661, 1244, 10947}, {1.0f, 0.8f, 0.95f}},
                {196, {14657, 1244, 7114}, {1.0f, 0.8f, 1.1f}},
                {197, {14660, 1244, 5027}, {1.0f, 0.8f, 0.95f}},
                {198, {14658, 1244, 7839}, {1.0f, 0.8f, 0.95f}},
                {199, {14659, 7837, 2902}, {1.0f, 0.6f, 0.95f}},
                {200, {14779, 6376, 2718}, {0.75f, 0.3f, 0.2f}},
                {201, {14778, 5145, 2717}, {0.75f, 0.3f, 0.2f}},
                {202, {14938, 6295, 14934}, {0.65f, 1.65f, 1.15f}},
                {203, {15395, 527, 5025}, {1.0f, 1.25f, 0.9f}},
                {204, {15393, 527, 1079}, {1.0f, 1.25f, 1.05f}},
                {205, {15398, 527, 11293}, {1.0f, 1.25f, 0.9f}},
                {206, {15595, 12193, 2851}, {1.0f, 0.7f, 1.05f}},
                {207, {15992, 15989, 15990}, {1.0f, 1.0f, 1.65f}},
                {208, {16189, 8840, 913}, {1.0f, 0.9f, 1.3f}},
                {209, {16259, 16360, 17158}, {1.0f, 1.0f, 1.55f}},
                {210, {16257, 16357, 499}, {1.0f, 1.2f, 0.9f}},
                {211, {4959, 1141, 16143}, {0.95f, 1.6f, 1.05f}},
                {212, {6537, 1070, 1405}, {1.0f, 0.8f, 2.25f}},
                {213, {16942, 9953, 6080}, {1.0f, 0.8f, 1.0f}},
                {214, {16943, 479, 10890}, {1.0f, 0.55f, 1.05f}},
            };
            for (Look const& l : kLooks)
                for (CompanionSpecies& sp : v)
                    if (sp.id == l.id)
                        for (uint8_t i = 0; i < sp.stageCount; ++i)
                        {
                            StageInfo& st = sp.stages[i];
                            st.display = l.display[i];
                            st.scale = l.scale[i];
                            if (st.rideable && st.rideDisplay)
                                st.rideDisplay = st.display;
                        }
            // The later-version forms (33_wotlk_looks.sql's UPDATEs): displays 40001+ that the
            // WotLK patch layer adds (client/src/wotlk/import.rs), one stage each, over the table
            // above. A server whose CreatureDisplayInfo.dbc lacks them falls back to the stage
            // creature's own look when the table is loaded (FusionCompanions.cpp).
            struct WotlkLook
            {
                uint16_t id;
                uint8_t stage; // 1-based
                uint32_t display;
                float scale;
            };
            static WotlkLook const kWotlkLooks[] = {
                {2, 3, 40008, 0.16f}, {3, 3, 40009, 0.16f}, {4, 3, 40010, 0.16f},
                {10, 3, 40011, 1.48f}, {12, 1, 40001, 0.8f}, {13, 1, 40005, 1.0f}, {17, 1, 40017, 1.0f},
                {19, 1, 40018, 0.3f}, {19, 3, 40015, 0.2f},
                {112, 1, 40002, 0.8f}, {113, 1, 40003, 0.8f}, {114, 1, 40004, 0.8f}, {117, 1, 40006, 1.0f},
                {129, 1, 40017, 1.0f}, {208, 3, 40013, 1.0f}, {213, 3, 40014, 0.73f},
            };
            for (WotlkLook const& l : kWotlkLooks)
                for (CompanionSpecies& sp : v)
                    if (sp.id == l.id && l.stage >= 1 && l.stage <= sp.stageCount)
                    {
                        StageInfo& st = sp.stages[l.stage - 1];
                        st.display = l.display;
                        st.scale = l.scale;
                        if (st.rideable && st.rideDisplay)
                            st.rideDisplay = st.display;
                    }
            // The flying mounts' lines (35_flying_companions.sql's UPDATEs): every stage, 40019+
            // from the patch layer and 1.12 displays between; the last form is the riding model at
            // 1, the size it is ridden at, and the display it is ridden with.
            static Look const kFlyingLooks[] = {
                {215, {40019, 1149, 40020}, {1.0f, 0.6f, 1.0f}},
                {216, {40021, 295, 40022}, {1.0f, 0.65f, 1.0f}},
                {217, {16943, 10890, 40023}, {1.0f, 0.55f, 1.0f}},
                {218, {40024, 40025, 40026}, {0.9f, 0.4f, 1.0f}},
                {219, {40027, 40028, 40029}, {1.0f, 1.0f, 1.0f}},
                {220, {40030, 40031, 40032}, {0.35f, 0.3f, 1.0f}},
                {221, {40033, 40034, 40035}, {0.5f, 0.5f, 1.0f}},
                {222, {40018, 40015, 40036}, {0.4f, 0.5f, 1.0f}},
                {223, {6292, 13972, 40037}, {1.0f, 0.5f, 1.0f}},
            };
            for (Look const& l : kFlyingLooks)
                for (CompanionSpecies& sp : v)
                    if (sp.id == l.id)
                        for (uint8_t i = 0; i < sp.stageCount; ++i)
                        {
                            StageInfo& st = sp.stages[i];
                            st.display = l.display[i];
                            st.scale = l.scale[i];
                            if (st.rideable && st.rideDisplay)
                                st.rideDisplay = st.display;
                        }
            return v;
        }

        std::vector<ItemStart> BuildItems()
        {
            // The mounts' companion items: each species at its rideable stage, level 40, so a
            // character that buys one at 40 rides it at once (60%) and grows it from there.
            return {
                {90060, 6, 2, 40},      // Horn of the War Wolf
                {90061, 7, 2, 40},      // Reins of the Striped Nightsaber
                {90062, 8, 2, 40},      // Reins of the Striped Frostsaber
                {90063, 12, 2, 40},     // Whistle of the Violet Raptor
                {90064, 29, 2, 40},     // Brown Kodo
                {90065, 30, 2, 40},     // Brown Horse Bridle
                {90066, 31, 2, 40},     // Blue Mechanostrider
                {90067, 32, 2, 40},     // Red Skeletal Horse
                {90068, 33, 2, 40},     // Gray Ram
            };
        }

        std::vector<VanillaPet> BuildVanillaPets()
        {
            // Every vanilla pet item (an item whose spell summons a critter, SPELL_EFFECT_SUMMON_CRITTER)
            // and the companion item that replaces it. Left out: the Bloodsail Admiral's Hat (a hat),
            // the Moro'gai gems (Zul'Gurub quest items) and the orphan whistles (Children's Week's
            // quest items, the orphan follows for the quest only).
            return {
                {4401, 90028, 28}, // Mechanical Squirrel Box
                {8485, 91165, 165}, // Cat Carrier (Bombay)
                {8486, 91166, 166}, // Cat Carrier (Cornish Rex)
                {8487, 91167, 167}, // Cat Carrier (Orange Tabby)
                {8488, 90026, 26}, // Cat Carrier (Silver Tabby)
                {8489, 91168, 168}, // Cat Carrier (White Kitten)
                {8490, 91169, 169}, // Cat Carrier (Siamese)
                {8491, 91170, 170}, // Cat Carrier (Black Tabby)
                {8492, 91172, 172}, // Parrot Cage (Green Wing Macaw)
                {8494, 91173, 173}, // Parrot Cage (Hyacinth Macaw)
                {8495, 91174, 174}, // Parrot Cage (Senegal)
                {8496, 91175, 175}, // Parrot Cage (Cockatiel)
                {8497, 90027, 27}, // Rabbit Crate (Snowshoe)
                {8498, 90003, 3}, // Tiny Emerald Whelpling
                {8499, 90002, 2}, // Tiny Crimson Whelpling
                {8500, 91176, 176}, // Great Horned Owl
                {8501, 90015, 15}, // Hawk Owl
                {10360, 91177, 177},// Black Kingsnake
                {10361, 91178, 178},// Brown Snake
                {10392, 91179, 179},// Crimson Snake
                {10393, 91180, 180},// Cockroach
                {10394, 91181, 181},// Prairie Dog Whistle
                {10398, 91182, 182},// Mechanical Chicken
                {10822, 90001, 1},// Dark Whelpling
                {11023, 91183, 183},// Ancona Chicken
                {11026, 91185, 185},// Tree Frog Box
                {11027, 91186, 186},// Wood Frog Box
                {11110, 91184, 184},// Chicken Egg
                {11474, 90005, 5},// Sprite Darter Egg
                {11825, 91187, 187},// Pet Bombling
                {11826, 91188, 188},// Lil' Smoky
                {11903, 91171, 171},// Cat Carrier (Corrupted Kitten)
                {12264, 90006, 6},// Worg Carrier
                {12529, 91191, 191},// Smolderweb Carrier
                {13582, 91192, 192},// Zergling Leash
                {13583, 91193, 193},// Panda Collar
                {13584, 91194, 194},// Diablo Stone
                {15996, 91189, 189},// Lifelike Mechanical Toad
                {18963, 91195, 195},// Turtle Egg (Albino)
                {18964, 91196, 196},// Turtle Egg (Loggerhead)
                {18965, 91197, 197},// Turtle Egg (Hawksbill)
                {18966, 91198, 198},// Turtle Egg (Leatherback)
                {18967, 91199, 199},// Turtle Egg (Olive)
                {19054, 91200, 200},// Red Dragon Orb
                {19055, 91201, 201},// Green Dragon Orb
                {19450, 91202, 202},// A Jubling's Tiny Home
                {20371, 90023, 23},// Blue Murloc Egg
                {20651, 91203, 203},// Orange Murloc Egg
                {20769, 90025, 25},// Disgusting Oozeling
                {21168, 91206, 206},// Baby Shark
                {21277, 91190, 190},// Tranquil Mechanical Yeti
                {22114, 91204, 204},// Pink Murloc Egg
                {22235, 91207, 207},// Truesilver Shafted Arrow
                {22780, 91205, 205},// White Murloc Egg
                {22781, 91208, 208},// Polar Bear Collar
                {23002, 91209, 209},// Turtle Box
                {23007, 91210, 210},// Piglet's Collar
                {23015, 91211, 211},// Rat Cage
                {23083, 91212, 212},// Captured Flame
                {23712, 91213, 213},// White Tiger Cub
                {23713, 91214, 214},// Hippogryph Hatchling
            };
        }

        // Little-endian writers and a bounds-checked reader for the channel bodies.
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

        struct Reader
        {
            std::vector<uint8_t> const& in;
            size_t at = 0;

            uint8_t U8() { return in[at++]; }
            uint16_t U16()
            {
                uint16_t v = uint16_t(in[at] | (in[at + 1] << 8));
                at += 2;
                return v;
            }
            uint32_t U32()
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(in[at + i]) << (8 * i);
                at += 4;
                return v;
            }
            uint64_t U64()
            {
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(in[at + i]) << (8 * i);
                at += 8;
                return v;
            }
        };

        constexpr size_t kBarBytes = 27;
        constexpr size_t kEvolvedBytes = 12;
        constexpr size_t kDuelBytes = 12;
    }

    bool IsHumanoidFamily(Family f)
    {
        return f == Family::Murloc || (uint8_t(f) >= uint8_t(Family::Gnoll) && uint8_t(f) <= uint8_t(Family::Worgen));
    }

    std::vector<CompanionSpecies> const& StarterSpecies()
    {
        static std::vector<CompanionSpecies> const all = BuildStarter();
        return all;
    }

    CompanionSpecies const* FindSpecies(std::vector<CompanionSpecies> const& all, uint16_t id)
    {
        for (CompanionSpecies const& s : all)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    CompanionSpecies const* FindByItem(std::vector<CompanionSpecies> const& all, uint32_t itemEntry)
    {
        for (CompanionSpecies const& s : all)
            if (s.item == itemEntry)
                return &s;
        return nullptr;
    }

    std::vector<VanillaPet> const& VanillaPets()
    {
        static std::vector<VanillaPet> const all = BuildVanillaPets();
        return all;
    }

    uint32_t VanillaPetReplacement(uint32_t oldItem)
    {
        for (VanillaPet const& p : VanillaPets())
            if (p.oldItem == oldItem)
                return p.item;
        return 0;
    }

    std::vector<std::string> VanillaPetSwapSql()
    {
        // Every table of the characters database that names an item instance's entry.
        static char const* const kTables[] = {"item_instance", "character_inventory", "mail_items", "auction"};
        std::vector<std::string> out;
        for (VanillaPet const& p : VanillaPets())
            for (char const* t : kTables)
                out.push_back(std::string("UPDATE `") + t + "` SET `item_id` = " + std::to_string(p.item)
                    + " WHERE `item_id` = " + std::to_string(p.oldItem));
        return out;
    }

    std::vector<ItemStart> const& StarterItems()
    {
        static std::vector<ItemStart> const all = BuildItems();
        return all;
    }

    CompanionSpecies const* FreshState(std::vector<CompanionSpecies> const& all, std::vector<ItemStart> const& items,
        uint32_t itemEntry, CompanionState& out)
    {
        out = CompanionState{};
        if (CompanionSpecies const* sp = FindByItem(all, itemEntry))
        {
            out.species = sp->id;
            return sp;
        }
        for (ItemStart const& i : items)
        {
            if (i.item != itemEntry)
                continue;
            CompanionSpecies const* sp = FindSpecies(all, i.species);
            if (!sp)
                return nullptr;
            out.species = sp->id;
            out.stage = std::min<uint8_t>(i.stage, uint8_t(sp->stageCount - 1));
            out.level = std::max<uint8_t>(1, std::min(i.level, kMaxLevel));
            return sp;
        }
        return nullptr;
    }

    // ── Riding ──────────────────────────────────────────────────────────────────────────────

    uint8_t MountSpeed(uint8_t ownerLevel, uint16_t ridingSkill)
    {
        if (ownerLevel >= kSwiftRideLevel && ridingSkill >= kSwiftRideSkill)
            return 100;
        if (ownerLevel >= kRideLevel && ridingSkill >= kRideSkill)
            return 60;
        return 0;
    }

    bool IsRideable(CompanionSpecies const& sp, CompanionState const& s)
    {
        return s.stage < sp.stageCount && sp.stages[s.stage].rideable && sp.stages[s.stage].mountSpell;
    }

    uint32_t RideSpell(StageInfo const& stage, uint8_t speed)
    {
        if (!stage.rideable || !speed)
            return 0;
        return speed >= 100 && stage.swiftMountSpell ? stage.swiftMountSpell : stage.mountSpell;
    }

    RideBlock CanRide(CompanionSpecies const& sp, CompanionState const& s, RideContext const& ctx)
    {
        if (!IsRideable(sp, s))
            return RideBlock::NotRideable;
        if (!ctx.summoned)
            return RideBlock::NotSummoned;
        if (ctx.inCombat)
            return RideBlock::InCombat;
        if (ctx.inDuel)
            return RideBlock::InDuel;
        if (!ctx.mountAllowed)
            return RideBlock::NotHere;
        if (ctx.ownerLevel < kRideLevel)
            return RideBlock::LevelTooLow;
        if (!MountSpeed(ctx.ownerLevel, ctx.ridingSkill))
            return RideBlock::NoSkill;
        // Tireless carries its rider down to 15%.
        if (ctx.healthShare < RideMinHealth(s.traits))
            return RideBlock::Worn;
        return RideBlock::None;
    }

    RideLook LookAtRide(bool hasAura, bool mounted, uint32_t sinceMs, uint32_t castMs, bool leaving)
    {
        if (hasAura)
            return leaving ? RideLook::Off : RideLook::Mounted;
        if (mounted)
            return RideLook::Off;
        // The triggered cast lands on its own clock; until it has had its time, it may still.
        if (sinceMs < castMs + kRideCastGraceMs)
            return RideLook::Waiting;
        return leaving ? RideLook::Off : RideLook::NeverTook;
    }

    uint32_t MountCastMs(float distance, float runSpeed)
    {
        float const run = runSpeed > 0.1f ? runSpeed : 7.f;
        float const ms = std::max(0.f, distance) / run * 1000.f + float(kMountCastSettleMs);
        return uint32_t(std::clamp(ms, float(kMountCastMinMs), float(kMountCastMaxMs)) + 0.5f);
    }

    float MountRunSpeed(float distance, float runSpeed, uint32_t castMs)
    {
        float const run = runSpeed > 0.1f ? runSpeed : 7.f;
        float const secs = float(castMs > kMountCastSettleMs ? castMs - kMountCastSettleMs : 1) / 1000.f;
        return std::max(run, std::max(0.f, distance) / secs);
    }

    Vec3 MountSpot(Vec3 owner, float facing, float petRadius)
    {
        float const d = kMountSpotYards + std::max(0.f, petRadius);
        return {owner.x + std::cos(facing) * d, owner.y + std::sin(facing) * d, owner.z};
    }

    bool MountCastHolds(Vec3 from, Vec3 now)
    {
        return (now - from).Length() <= kMountCastMoveYards;
    }

    bool IsCompanionRideSpell(std::vector<CompanionSpecies> const& all, uint32_t spell)
    {
        if (!spell)
            return false;
        for (CompanionSpecies const& sp : all)
            for (uint8_t st = 0; st < sp.stageCount && st < kMaxStages; ++st)
                if (sp.stages[st].rideable && (sp.stages[st].mountSpell == spell || sp.stages[st].swiftMountSpell == spell))
                    return true;
        return false;
    }

    bool StrayRideAura(bool companionSpell, bool riding, uint32_t rideSpell, uint32_t auraSpell, bool fromItem,
        bool knownSpell)
    {
        if (!companionSpell || fromItem || knownSpell)
            return false;
        return !(riding && rideSpell == auraSpell);
    }

    uint8_t RideFlags(CompanionSpecies const& sp, CompanionState const& s, RideContext const& ctx, bool riding)
    {
        if (!IsRideable(sp, s))
            return 0;
        uint8_t f = BAR_RIDEABLE;
        if (riding)
            f |= BAR_RIDING | BAR_CAN_RIDE;     // getting off is always allowed
        else if (CanRide(sp, s, ctx) == RideBlock::None)
            f |= BAR_CAN_RIDE;
        return f;
    }

    // ── Experience ──────────────────────────────────────────────────────────────────────────

    uint32_t PlayerXpToNext(uint8_t level)
    {
        if (level < 1 || level >= kMaxLevel)
            return 0;
        uint32_t l = level;
        // 1.12's "difficulty" term, which steepens the curve from level 29.
        uint32_t diff = l <= 28 ? 0 : l == 29 ? 1 : l == 30 ? 3 : l == 31 ? 6 : 5 * (l - 30);
        uint32_t raw = (8 * l + diff) * (45 + 5 * l);
        return (raw + 50) / 100 * 100;
    }

    uint32_t XpToNext(uint8_t level)
    {
        uint32_t player = PlayerXpToNext(level);
        if (!player)
            return 0;
        // To the nearest 10, so the bar shows round numbers.
        return uint32_t(std::lround(float(player) * kXpFactor / 10.f)) * 10;
    }

    uint8_t GrayLevel(uint8_t level)
    {
        if (level <= 5)
            return 0;
        if (level <= 39)
            return uint8_t(level - 5 - level / 10);
        if (level <= 59)
            return uint8_t(level - 1 - level / 5);
        return uint8_t(level - 9);
    }

    uint8_t ZeroDifference(uint8_t level)
    {
        if (level < 8) return 5;
        if (level < 10) return 6;
        if (level < 12) return 7;
        if (level < 16) return 8;
        if (level < 20) return 9;
        if (level < 30) return 11;
        if (level < 40) return 12;
        if (level < 45) return 13;
        if (level < 50) return 14;
        if (level < 55) return 15;
        if (level < 60) return 16;
        return 17;
    }

    uint32_t KillXp(uint8_t companionLevel, uint8_t victimLevel, bool victimElite)
    {
        if (companionLevel < 1 || victimLevel < 1 || victimLevel <= GrayLevel(companionLevel))
            return 0;
        uint32_t base = 5u * companionLevel + 45u;
        uint32_t xp;
        if (victimLevel >= companionLevel)
        {
            uint32_t above = std::min<uint32_t>(victimLevel - companionLevel, 4);
            xp = base * (20 + above) / 20;
        }
        else
        {
            uint32_t zd = ZeroDifference(companionLevel);
            uint32_t below = companionLevel - victimLevel;
            xp = below >= zd ? 0 : base * (zd - below) / zd;
        }
        return victimElite ? xp * 2 : xp;
    }

    XpGain GainXp(CompanionState& s, uint32_t amount, uint8_t ownerLevel)
    {
        XpGain g;
        if (s.level >= kMaxLevel)
        {
            g.wasted = amount;
            s.xp = 0;
            return g;
        }
        uint8_t cap = uint8_t(std::min<uint32_t>(std::max<uint8_t>(ownerLevel, 1), kMaxLevel));
        s.xp += amount;
        while (s.level < cap && s.level < kMaxLevel && s.xp >= XpToNext(s.level))
        {
            s.xp -= XpToNext(s.level);
            ++s.level;
            ++g.levels;
        }
        if (s.level >= kMaxLevel)
        {
            g.wasted = s.xp;
            s.xp = 0;
        }
        else if (s.level >= cap)
        {
            // At (or above) the owner's level: bank up to a full bar, ready for the owner's ding.
            uint32_t full = XpToNext(s.level);
            if (s.xp >= full)
            {
                g.wasted = s.xp - full;
                s.xp = full;
                g.capped = true;
            }
        }
        return g;
    }

    bool IsCapped(CompanionState const& s, uint8_t ownerLevel)
    {
        uint32_t const full = XpToNext(s.level);
        return s.level < kMaxLevel && s.level >= ownerLevel && full && s.xp >= full;
    }

    // ── Stats ───────────────────────────────────────────────────────────────────────────────

    CompanionStats HunterPetBaseline(uint8_t level)
    {
        float l = float(std::max<uint8_t>(level, 1));
        CompanionStats s;
        s.attackMs = 2000;
        s.health = uint32_t(40.f + 55.f * l);
        float avg = (1.5f + 0.8f * l) * 2.f;
        s.damageMin = avg * 0.85f;
        s.damageMax = avg * 1.15f;
        s.armor = uint32_t(50.f * l);
        return s;
    }

    RoleShare RoleShareOf(Role r)
    {
        switch (r)
        {
            case Role::Ranged: return {0.40f, 0.55f, 0.35f, 2500};
            case Role::Tank: return {0.70f, 0.35f, 0.80f, 2400};
            case Role::Melee:
            default: return {0.55f, 0.50f, 0.55f, 2000};
        }
    }

    float StageMultiplier(uint8_t stage, uint8_t stageCount)
    {
        // A baby is a little weaker than the adult, an elder (a third stage) a little stronger.
        // A species that never grows (a critter, a gadget) sits between baby and adult.
        if (stageCount <= 1)
            return 0.92f;
        if (stage == 0)
            return 0.85f;
        if (stageCount == 2 || stage == 1)
            return stageCount == 2 ? 1.05f : 1.f;
        return 1.12f;
    }

    CompanionStats Stats(Role role, uint8_t stage, uint8_t stageCount, uint8_t level)
    {
        CompanionStats base = HunterPetBaseline(level);
        RoleShare share = RoleShareOf(role);
        float m = StageMultiplier(stage, stageCount);
        CompanionStats s;
        s.attackMs = share.attackMs;
        s.health = uint32_t(std::lround(float(base.health) * share.health * m));
        float dps = base.Dps() * share.dps * m;
        float avg = dps * float(s.attackMs) / 1000.f;
        s.damageMin = avg * 0.85f;
        s.damageMax = avg * 1.15f;
        s.armor = uint32_t(std::lround(float(base.armor) * share.armor * m));
        s.reach = role == Role::Ranged ? kRangedReach : 0.f;
        return s;
    }

    // ── Evolution ───────────────────────────────────────────────────────────────────────────

    EvolutionRule const* NextRule(CompanionSpecies const& sp, CompanionState const& s)
    {
        if (s.stage + 1 >= sp.stageCount || s.stage >= kMaxStages - 1)
            return nullptr;
        EvolutionRule const& r = sp.evolutions[s.stage];
        return r.Exists() ? &r : nullptr;
    }

    EvolveBlock CanEvolve(EvolutionRule const& rule, CompanionState const& s, EvolveContext const& ctx)
    {
        if (!rule.Exists())
            return EvolveBlock::FinalStage;
        if (!ctx.summoned)
            return EvolveBlock::NotSummoned;
        if (ctx.inCombat)
            return EvolveBlock::InCombat;
        if (ctx.inDuel)
            return EvolveBlock::InDuel;
        if (EffectiveLevel(s, ctx.ownerLevel) < rule.level)
            return EvolveBlock::LevelTooLow;
        if (!rule.areas.empty())
        {
            bool here = false;
            for (uint32_t a : rule.areas)
                here = here || (a != 0 && (a == ctx.zoneId || a == ctx.areaId));
            if (!here)
                return EvolveBlock::WrongPlace;
        }
        if ((rule.condition == Condition::Night && !IsNight(ctx.hour)) ||
            (rule.condition == Condition::Day && IsNight(ctx.hour)))
            return EvolveBlock::WrongTime;
        if (rule.condition == Condition::KillType && s.evolveKills < rule.conditionCount)
            return EvolveBlock::NeedKills;
        if (rule.item && ctx.itemsHeld < std::max<uint8_t>(rule.itemCount, 1))
            return EvolveBlock::MissingItem;
        return EvolveBlock::None;
    }

    EvolveBlock CanEvolve(CompanionSpecies const& sp, CompanionState const& s, EvolveContext const& ctx)
    {
        EvolutionRule const* r = NextRule(sp, s);
        return r ? CanEvolve(*r, s, ctx) : EvolveBlock::FinalStage;
    }

    void Evolve(CompanionState& s)
    {
        if (s.stage + 1 < kMaxStages)
            ++s.stage;
        s.evolveKills = 0;
    }

    bool RecordKill(CompanionSpecies const& sp, CompanionState& s, CreatureType victimType)
    {
        EvolutionRule const* r = NextRule(sp, s);
        if (!r || r->condition != Condition::KillType || r->conditionType != victimType ||
            s.evolveKills >= r->conditionCount)
            return false;
        ++s.evolveKills;
        return true;
    }

    // ── Drops ───────────────────────────────────────────────────────────────────────────────

    float DropChance(CreatureType type, CreatureRank rank, bool isCritter)
    {
        if (type == CreatureType::None || type == CreatureType::Totem)
            return 0.f;
        if (isCritter)
            return 0.005f;
        switch (rank)
        {
            case CreatureRank::Elite: return 0.01f;
            case CreatureRank::RareElite: return 0.05f;
            case CreatureRank::WorldBoss: return 0.03f;
            case CreatureRank::DungeonBoss: return 0.03f;
            case CreatureRank::Rare: return 0.02f;
            case CreatureRank::Normal:
            default: return 0.002f;
        }
    }

    std::vector<uint16_t> DroppableSpecies(std::vector<CompanionSpecies> const& all, DropVictim const& v)
    {
        std::vector<uint16_t> exact, family, type;
        for (CompanionSpecies const& s : all)
        {
            DropSource const& d = s.drops;
            if (std::find(d.entries.begin(), d.entries.end(), v.entry) != d.entries.end())
                exact.push_back(s.id);
            else if (d.type != CreatureType::None && d.type == v.type)
            {
                if (d.petFamily == 0)
                    type.push_back(s.id);
                else if (d.petFamily == v.petFamily)
                    family.push_back(s.id);
            }
        }
        if (!exact.empty())
            return exact;
        if (!family.empty())
            return family;
        return type;
    }

    bool DropsCompanionGoods(DropVictim const& v)
    {
        if (v.summoned || v.type == CreatureType::Totem)
            return false;
        return !v.grayToKiller || v.type == CreatureType::Critter;
    }

    uint16_t RollDrop(std::vector<CompanionSpecies> const& all, DropVictim const& v, float chanceRoll, float pickRoll)
    {
        if (!DropsCompanionGoods(v))
            return 0;
        if (chanceRoll >= DropChance(v.type, v.rank, v.type == CreatureType::Critter))
            return 0;
        std::vector<uint16_t> candidates = DroppableSpecies(all, v);
        if (candidates.empty())
            return 0;
        size_t i = size_t(Clamp(pickRoll, 0.f, 1.f) * float(candidates.size()));
        return candidates[std::min(i, candidates.size() - 1)];
    }

    uint32_t CatalystFor(CreatureType type)
    {
        switch (type)
        {
            case CreatureType::Dragonkin: return Catalyst::DraconicEssence;
            case CreatureType::Beast: return Catalyst::PrimalBeastEssence;
            case CreatureType::Elemental: return Catalyst::ElementalCore;
            case CreatureType::Demon: return Catalyst::FelEmber;
            case CreatureType::Undead: return Catalyst::NecroticResidue;
            case CreatureType::Mechanical: return Catalyst::ArcaneGearwork;
            case CreatureType::Humanoid: return Catalyst::TribalFetish;
            default: return 0;
        }
    }

    float CatalystDropChance(DropVictim const& v, uint8_t victimLevel)
    {
        if (!DropsCompanionGoods(v))
            return 0.f;
        return CatalystDropChance(v.type, v.rank, victimLevel);
    }

    float CatalystDropChance(CreatureType type, CreatureRank rank, uint8_t victimLevel)
    {
        if (!CatalystFor(type) || victimLevel < 25)
            return 0.f;
        switch (rank)
        {
            case CreatureRank::Elite: return 0.015f;
            case CreatureRank::RareElite: return 0.08f;
            case CreatureRank::WorldBoss:
            case CreatureRank::DungeonBoss: return 0.10f;
            case CreatureRank::Rare: return 0.02f;
            case CreatureRank::Normal:
            default: return 0.004f;
        }
    }

    // ── Duels ───────────────────────────────────────────────────────────────────────────────

    std::pair<uint8_t, uint8_t> DuelLevels(uint8_t challengerLevel, uint8_t challengedLevel)
    {
        uint8_t l = std::max<uint8_t>(1, std::min(challengerLevel, challengedLevel));
        return {l, l};
    }

    uint32_t DuelWinXp(uint8_t winnerLevel, uint8_t loserLevel)
    {
        if (winnerLevel >= kMaxLevel || loserLevel <= GrayLevel(winnerLevel))
            return 0;
        return XpToNext(winnerLevel) / 20;
    }

    bool PayDuelWin(DuelWinsToday& w, uint32_t day, uint32_t cap)
    {
        if (w.day != day)
            w = {day, 0};
        if (w.wins >= cap)
            return false;
        ++w.wins;
        return true;
    }

    bool DuelPaysXp(DuelEnd end)
    {
        return end == DuelEnd::Beaten || end == DuelEnd::Timeout;
    }

    bool DuelHolds(DuelPhase phase)
    {
        return phase == DuelPhase::Countdown || phase == DuelPhase::Fighting;
    }

    int CompanionDuel::Side(uint64_t who) const
    {
        if (!who)
            return -1;
        if (who == m_owner[0])
            return 0;
        if (who == m_owner[1])
            return 1;
        return -1;
    }

    void CompanionDuel::Finish(DuelEnd why, int8_t winner)
    {
        m_phase = DuelPhase::Finished;
        m_result.end = why;
        m_result.winner = winner;
    }

    void CompanionDuel::Lose(uint64_t who, DuelEnd why)
    {
        int side = Side(who);
        if (side < 0)
            return;
        if (m_phase == DuelPhase::Fighting)
            Finish(why, int8_t(1 - side));
        else if (m_phase == DuelPhase::Challenged || m_phase == DuelPhase::Countdown)
            Finish(DuelEnd::Cancelled, -1);
    }

    bool CompanionDuel::Challenge(uint64_t challenger, uint64_t challenged, uint32_t nowMs)
    {
        if ((m_phase != DuelPhase::Idle && m_phase != DuelPhase::Finished) || !challenger || !challenged ||
            challenger == challenged)
            return false;
        m_owner = {challenger, challenged};
        m_phase = DuelPhase::Challenged;
        m_result = DuelResult{};
        m_sinceMs = nowMs;
        return true;
    }

    bool CompanionDuel::Accept(uint64_t who, uint32_t nowMs)
    {
        if (m_phase != DuelPhase::Challenged || Side(who) != 1)
            return false;
        m_phase = DuelPhase::Countdown;
        m_sinceMs = nowMs;
        return true;
    }

    bool CompanionDuel::Decline(uint64_t who)
    {
        int side = Side(who);
        if (m_phase != DuelPhase::Challenged || side < 0)
            return false;
        // The challenged saying no is a decline; the challenger taking it back, a cancel.
        Finish(side == 1 ? DuelEnd::Declined : DuelEnd::Cancelled, -1);
        return true;
    }

    void CompanionDuel::Update(uint32_t nowMs, float healthShareChallenger, float healthShareChallenged)
    {
        uint32_t since = nowMs - m_sinceMs;
        if (m_phase == DuelPhase::Challenged && since >= kChallengeTimeoutMs)
            Finish(DuelEnd::Expired, -1);
        else if (m_phase == DuelPhase::Countdown && since >= kCountdownMs)
        {
            m_phase = DuelPhase::Fighting;
            m_sinceMs += kCountdownMs;
        }
        else if (m_phase == DuelPhase::Fighting && since >= kMaxFightMs)
        {
            float d = healthShareChallenger - healthShareChallenged;
            Finish(DuelEnd::Timeout, std::fabs(d) <= 0.01f ? int8_t(-1) : int8_t(d > 0.f ? 0 : 1));
        }
    }

    void CompanionDuel::Beaten(uint64_t who)
    {
        if (m_phase == DuelPhase::Fighting)
            Lose(who, DuelEnd::Beaten);
    }

    void CompanionDuel::Forfeit(uint64_t who) { Lose(who, DuelEnd::Forfeit); }
    void CompanionDuel::Fled(uint64_t who) { Lose(who, DuelEnd::Fled); }
    void CompanionDuel::Interfered(uint64_t who) { Lose(who, DuelEnd::Interfered); }

    uint32_t CompanionDuel::CountdownLeftMs(uint32_t nowMs) const
    {
        if (m_phase != DuelPhase::Countdown)
            return 0;
        uint32_t since = nowMs - m_sinceMs;
        return since >= kCountdownMs ? 0 : kCountdownMs - since;
    }

    // ── Packets ─────────────────────────────────────────────────────────────────────────────

    Bar MakeBar(uint64_t item, CompanionSpecies const& sp, CompanionState const& s, EvolveContext const& ctx,
        uint8_t healthPct)
    {
        Bar b;
        b.item = item;
        b.species = sp.id;
        b.stage = s.stage;
        b.level = s.level;
        b.effectiveLevel = EffectiveLevel(s, ctx.ownerLevel);
        b.xp = s.xp;
        b.xpToNext = XpToNext(s.level);
        b.block = CanEvolve(sp, s, ctx);
        if (ctx.summoned)
            b.flags |= BAR_SUMMONED;
        if (ctx.inDuel)
            b.flags |= BAR_IN_DUEL;
        if (b.block == EvolveBlock::None)
            b.flags |= BAR_CAN_EVOLVE;
        if (b.block == EvolveBlock::FinalStage)
            b.flags |= BAR_FINAL_STAGE;
        if (IsCapped(s, ctx.ownerLevel))
            b.flags |= BAR_CAPPED;
        if (EvolutionRule const* r = NextRule(sp, s))
        {
            b.nextEvolveLevel = r->level;
            b.catalystCount = r->item ? std::max<uint8_t>(r->itemCount, 1) : 0;
        }
        b.healthPct = std::min<uint8_t>(healthPct, 100);
        return b;
    }

    uint32_t ItemSeed(CompanionState const& s)
    {
        uint32_t seed = uint32_t(std::min<uint8_t>(s.level, 63)) | uint32_t(s.stage & 3) << 6 | uint32_t(s.species & 0xFF) << 8;
        // Up to three traits, lowest id first, five bits each.
        uint32_t shift = 16;
        for (uint8_t id = 1; id <= 30 && shift <= 26; ++id)
            if (s.traits & (1u << (id - 1)))
            {
                seed |= uint32_t(id) << shift;
                shift += 5;
            }
        if (s.generation)
            seed |= 1u << 31;
        return seed;
    }

    bool FromItemSeed(uint32_t seed, CompanionState& out)
    {
        uint8_t const level = uint8_t(seed & 0x3F);
        uint8_t const stage = uint8_t(seed >> 6 & 3);
        uint16_t const species = uint16_t(seed >> 8 & 0xFF);
        if (species == 0 || level == 0 || level > kMaxLevel || stage >= kMaxStages)
            return false;
        uint32_t traits = 0;
        for (uint32_t shift = 16; shift <= 26; shift += 5)
        {
            uint32_t const id = seed >> shift & 0x1F;
            if (id > 30)
                return false;
            if (id)
                traits |= 1u << (id - 1);
        }
        out = CompanionState{};
        out.species = species;
        out.stage = stage;
        out.level = level;
        out.traits = traits;
        out.generation = seed >> 31 ? 1 : 0;
        return true;
    }

    std::vector<uint8_t> EncodeBar(Bar const& b)
    {
        std::vector<uint8_t> o;
        o.reserve(kBarBytes);
        Put8(o, kOpBar);
        Put64(o, b.item);
        Put16(o, b.species);
        Put8(o, b.stage);
        Put8(o, b.level);
        Put8(o, b.effectiveLevel);
        Put32(o, b.xp);
        Put32(o, b.xpToNext);
        Put8(o, b.flags);
        Put8(o, uint8_t(b.block));
        Put8(o, b.nextEvolveLevel);
        Put8(o, b.catalystCount);
        Put8(o, b.healthPct);
        return o;
    }

    bool DecodeBar(std::vector<uint8_t> const& body, Bar& out)
    {
        if (body.size() != kBarBytes || body[0] != kOpBar)
            return false;
        Reader r{body, 1};
        out.item = r.U64();
        out.species = r.U16();
        out.stage = r.U8();
        out.level = r.U8();
        out.effectiveLevel = r.U8();
        out.xp = r.U32();
        out.xpToNext = r.U32();
        out.flags = r.U8();
        out.block = EvolveBlock(r.U8());
        out.nextEvolveLevel = r.U8();
        out.catalystCount = r.U8();
        out.healthPct = r.U8();
        return true;
    }

    std::vector<uint8_t> EncodeEvolved(uint64_t item, uint16_t species, uint8_t stage)
    {
        std::vector<uint8_t> o;
        o.reserve(kEvolvedBytes);
        Put8(o, kOpEvolved);
        Put64(o, item);
        Put16(o, species);
        Put8(o, stage);
        return o;
    }

    std::vector<uint8_t> EncodeDuel(DuelUpdate const& d)
    {
        std::vector<uint8_t> o;
        o.reserve(kDuelBytes);
        Put8(o, kOpDuel);
        Put8(o, uint8_t(d.phase));
        Put8(o, uint8_t(d.end));
        Put8(o, uint8_t(d.winner));
        Put64(o, d.opponent);
        return o;
    }

    bool DecodeDuel(std::vector<uint8_t> const& body, DuelUpdate& out)
    {
        if (body.size() != kDuelBytes || body[0] != kOpDuel)
            return false;
        Reader r{body, 1};
        out.phase = DuelPhase(r.U8());
        out.end = DuelEnd(r.U8());
        out.winner = int8_t(r.U8());
        out.opponent = r.U64();
        return true;
    }

    std::vector<uint8_t> EncodeMoves(Moves const& m)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpMoves);
        Put64(o, m.item);
        Put16(o, m.species);
        Put8(o, uint8_t(m.pending));
        Put8(o, m.twinStage);
        Put32(o, m.traits);
        Put16(o, m.autocast);
        for (uint8_t p : m.picks)
            Put8(o, p);
        auto str = [&o](std::string const& s) {
            size_t const n = std::min<size_t>(s.size(), 255);
            Put8(o, uint8_t(n));
            o.insert(o.end(), s.begin(), s.begin() + long(n));
        };
        for (uint8_t st = 0; st < kMaxStages; ++st)
            for (uint8_t i = 0; i < 2; ++i)
            {
                Put32(o, m.spells[st][i]);
                str(m.names[st][i]);
            }
        Put32(o, m.mountedSpell);
        str(m.mountedName);
        return o;
    }

    bool DecodeMoves(std::vector<uint8_t> const& body, Moves& out)
    {
        if (body.size() < 22 || body[0] != kOpMoves)
            return false;
        Reader r{body, 1};
        out = Moves{};
        out.item = r.U64();
        out.species = r.U16();
        out.pending = int8_t(r.U8());
        out.twinStage = r.U8();
        out.traits = r.U32();
        out.autocast = r.U16();
        for (uint8_t& p : out.picks)
            p = r.U8();
        auto str = [&r, &body](std::string& s) {
            if (r.at >= body.size())
                return false;
            size_t const n = r.U8();
            if (r.at + n > body.size())
                return false;
            s.assign(body.begin() + long(r.at), body.begin() + long(r.at + n));
            r.at += n;
            return true;
        };
        for (uint8_t st = 0; st < kMaxStages; ++st)
            for (uint8_t i = 0; i < 2; ++i)
            {
                if (r.at + 4 > body.size())
                    return false;
                out.spells[st][i] = r.U32();
                if (!str(out.names[st][i]))
                    return false;
            }
        if (r.at + 4 > body.size())
            return false;
        out.mountedSpell = r.U32();
        return str(out.mountedName) && r.at == body.size();
    }

    std::vector<uint8_t> EncodeCooldown(uint8_t slot, uint32_t ms)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpCooldown);
        Put8(o, slot);
        Put32(o, ms);
        return o;
    }

    std::vector<uint8_t> EncodeMountCast(MountCastPhase phase, uint32_t ms)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpMountCast);
        Put8(o, uint8_t(phase));
        Put32(o, ms);
        return o;
    }

    std::vector<uint8_t> EncodeLineage(uint32_t seed, CompanionState const& s)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpLineage);
        Put32(o, seed);
        Put8(o, s.generation);
        Put16(o, s.parentA);
        Put16(o, s.parentB);
        Put8(o, s.breedCount);
        Put16(o, s.hybrid);
        return o;
    }

    std::vector<uint8_t> EncodeRequest(Request const& q)
    {
        std::vector<uint8_t> o{q.op};
        switch (q.op)
        {
            case kOpSummon:
            case kOpEvolve:
            case kOpDuelChallenge:
                Put64(o, q.guid);
                break;
            case kOpDuelAnswer:
                Put8(o, q.accept ? 1 : 0);
                break;
            case kOpPick:
                Put64(o, q.guid);
                Put8(o, q.slot);
                Put8(o, q.option);
                break;
            case kOpBarPress:
            {
                Put8(o, q.slot);
                Put64(o, q.guid);
                Put8(o, q.flags);
                for (float f : {q.point.x, q.point.y, q.point.z})
                {
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    Put32(o, bits);
                }
                break;
            }
            case kOpAutocast:
                Put8(o, q.slot);
                Put8(o, q.accept ? 1 : 0);
                break;
            default:
                break;
        }
        return o;
    }

    bool DecodeRequest(std::vector<uint8_t> const& body, Request& out)
    {
        if (body.empty())
            return false;
        out = Request{};
        out.op = body[0];
        Reader r{body, 1};
        switch (out.op)
        {
            case kOpSummon:
            case kOpEvolve:
            case kOpDuelChallenge:
                if (body.size() != 9)
                    return false;
                out.guid = r.U64();
                return true;
            case kOpDuelAnswer:
                if (body.size() != 2)
                    return false;
                out.accept = r.U8() != 0;
                return true;
            case kOpDismiss:
            case kOpDuelForfeit:
            case kOpRide:
                return body.size() == 1;
            case kOpPick:
                if (body.size() != 11)
                    return false;
                out.guid = r.U64();
                out.slot = r.U8();
                out.option = r.U8();
                return out.slot < kMaxStages && out.option >= 1 && out.option <= 2;
            case kOpBarPress:
            {
                if (body.size() != 23)
                    return false;
                out.slot = r.U8();
                out.guid = r.U64();
                out.flags = r.U8();
                float* const xyz[3] = {&out.point.x, &out.point.y, &out.point.z};
                for (float* f : xyz)
                {
                    uint32_t const bits = r.U32();
                    std::memcpy(f, &bits, 4);
                    if (!std::isfinite(*f))
                        return false;
                }
                return out.slot < 10;
            }
            case kOpAutocast:
                if (body.size() != 3)
                    return false;
                out.slot = r.U8();
                out.accept = r.U8() != 0;
                return out.slot < 10;
            default:
                return false;
        }
    }
}
