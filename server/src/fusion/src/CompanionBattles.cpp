/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionBattles.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace Fusion::CompanionBattles
{
    using Companions::AbilityKind;
    using Companions::Family;

    // ── Families ────────────────────────────────────────────────────────────────────────────

    char const* FamilyName(BattleFamily f)
    {
        static char const* const kNames[] = {"Humanoid", "Dragonkin", "Flying", "Undead", "Critter", "Magic",
            "Elemental", "Beast", "Aquatic", "Mechanical"};
        return uint8_t(f) < uint8_t(BattleFamily::Count) ? kNames[uint8_t(f)] : "Critter";
    }


    BattleFamily BattleFamilyOf(Family f)
    {
        switch (f)
        {
            case Family::Murloc: case Family::Gnoll: case Family::Kobold: case Family::Trogg: case Family::Ogre:
            case Family::Centaur: case Family::Quilboar: case Family::Furbolg: case Family::Troll: case Family::Naga:
            case Family::Moonkin: case Family::Worgen: case Family::Felguard:
                return BattleFamily::Humanoid;
            case Family::Dragon: case Family::FaerieDragon: case Family::Hydra: case Family::WindSerpent:
                return BattleFamily::Dragonkin;
            case Family::Owl: case Family::Bat: case Family::CarrionBird: case Family::Harpy:
            case Family::Gryphon: case Family::WindRider: case Family::Hippogryph: case Family::Dragonhawk:
                return BattleFamily::Flying;
            case Family::Skeleton: case Family::Ghoul: case Family::Banshee: case Family::CryptFiend:
            case Family::SkeletalHorse:
                return BattleFamily::Undead;
            case Family::Critter: case Family::Tallstrider: case Family::Boar:
                return BattleFamily::Critter;
            case Family::Imp: case Family::Felhound: case Family::Darkhound: case Family::Infernal: case Family::Ooze:
                return BattleFamily::Magic;
            case Family::FireElemental: case Family::WaterElemental: case Family::EarthElemental:
            case Family::AirElemental: case Family::PoisonElemental: case Family::BogBeast: case Family::Basilisk:
                return BattleFamily::Elemental;
            case Family::Crocolisk: case Family::Turtle: case Family::Crab: case Family::Threshadon:
            case Family::Makrura:
                return BattleFamily::Aquatic;
            case Family::Mechanical: case Family::Mechanostrider:
                return BattleFamily::Mechanical;
            default:
                return BattleFamily::Beast;
        }
    }

    char const* ArchetypeName(Archetype a)
    {
        static char const* const kNames[] = {"Feline", "Ursine", "Avian", "Draconic", "Undead", "Venom", "Pack",
            "Elemental", "Fel", "Humanoid", "Shell", "Mechanical", "Critter"};
        return uint8_t(a) < uint8_t(Archetype::Count) ? kNames[uint8_t(a)] : "Any";
    }

    Archetype ArchetypeOf(Family f)
    {
        switch (f)
        {
            case Family::Cat: case Family::Raptor:
                return Archetype::Feline;
            case Family::Bear: case Family::Gorilla: case Family::Boar: case Family::Kodo:
                return Archetype::Ursine;
            case Family::Owl: case Family::Bat: case Family::CarrionBird: case Family::Harpy: case Family::Tallstrider:
            case Family::Gryphon: case Family::WindRider: case Family::Hippogryph: case Family::Dragonhawk:
                return Archetype::Avian;
            case Family::Dragon: case Family::FaerieDragon: case Family::Hydra: case Family::WindSerpent:
                return Archetype::Draconic;
            case Family::Skeleton: case Family::Ghoul: case Family::Banshee: case Family::CryptFiend:
            case Family::SkeletalHorse:
                return Archetype::Undead;
            case Family::Spider: case Family::Scorpid: case Family::Basilisk: case Family::PoisonElemental:
            case Family::BogBeast: case Family::Ooze:
                return Archetype::Venom;
            case Family::Wolf: case Family::Worgen: case Family::Hyena: case Family::Horse: case Family::Ram:
                return Archetype::Pack;
            case Family::FireElemental: case Family::WaterElemental: case Family::EarthElemental:
            case Family::AirElemental: case Family::Infernal:
                return Archetype::Elemental;
            case Family::Imp: case Family::Felhound: case Family::Darkhound: case Family::Felguard:
                return Archetype::Fel;
            case Family::Crocolisk: case Family::Turtle: case Family::Crab: case Family::Threshadon:
            case Family::Makrura:
                return Archetype::Shell;
            case Family::Mechanical: case Family::Mechanostrider:
                return Archetype::Mechanical;
            case Family::Critter:
                return Archetype::Critter;
            default:
                return Archetype::Humanoid;
        }
    }

    // ── Statuses ────────────────────────────────────────────────────────────────────────────

    char const* StatusName(Status s)
    {
        static char const* const kNames[] = {"Exposed", "Weakened", "Poison", "Bleed", "Strength", "Agility",
            "Thorns", "Evasion", "Regeneration", "Draconic Might", "Fortify", "Plague", "Insight", "Plating",
            "Frenzy", "Undying", "Drained", "Attunement", "Bloodlust"};
        return uint8_t(s) < uint8_t(Status::Count) ? kNames[uint8_t(s)] : "Effect";
    }

    bool IsDebuff(Status s)
    {
        return s == Status::Exposed || s == Status::Weakened || s == Status::Poison || s == Status::Bleed
            || s == Status::Drained;
    }

    // ── Cards ───────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct B
        {
            Card c;
            B(uint16_t id, char const* name, CardType t, uint8_t cost, char const* icon, Archetype a = Archetype::Count)
            {
                c.id = id;
                c.name = name;
                c.type = t;
                c.cost = cost;
                c.icon = icon;
                c.archetype = a;
            }
            B& Dmg(uint16_t d, uint8_t h = 1) { c.damage = d; c.hits = h; return *this; }
            B& Blk(uint16_t b) { c.block = b; return *this; }
            B& Draw(uint8_t n) { c.draw = n; return *this; }
            B& Gain(uint8_t n) { c.energy = n; return *this; }
            B& Heal(uint8_t n) { c.heal = n; return *this; }
            B& Pay(uint8_t n) { c.selfHurt = n; return *this; }
            B& On(Status s, int8_t n) { c.enemy[uint8_t(s)] = n; return *this; }
            B& Me(Status s, int8_t n) { c.self[uint8_t(s)] = n; return *this; }
            B& Sp(Special s, uint8_t x = 0) { c.special = s; c.extra = x; return *this; }
            B& Ex() { c.exhaust = true; return *this; }
            B& Basic() { c.basic = true; return *this; }
            operator Card() const { return c; }
        };
        constexpr CardType A = CardType::Attack;
        constexpr CardType S = CardType::Skill;
        constexpr CardType P = CardType::Power;
        using St = Status;
        using Ar = Archetype;
        using X = Special;

        std::vector<Card> BuildTable()
        {
            std::vector<Card> t = {
                // The basic attacks (one per moveset's basic spell) and Guard: every deck has them.
                B(1, "Bite", A, 1, "Ability_Racial_Cannibalize").Dmg(6).Basic(),
                B(2, "Claw", A, 1, "Ability_Druid_Rake").Dmg(6).Basic(),
                B(3, "Firebolt", A, 1, "Spell_Fire_FireBolt").Dmg(6).Basic(),
                B(4, "Lightning Breath", A, 1, "Spell_Nature_Lightning").Dmg(6).Basic(),
                B(5, "Acid Spit", A, 1, "Spell_Nature_Acid_01").Dmg(6).Basic(),
                B(6, "Frostbolt", A, 1, "Spell_Frost_FrostBolt02").Dmg(6).Basic(),
                B(7, "Lightning Bolt", A, 1, "Spell_Nature_Lightning").Dmg(6).Basic(),
                B(8, "Snap Kick", A, 1, "Ability_Kick").Dmg(6).Basic(),
                B(9, "Strike", A, 1, "Ability_MeleeDamage").Dmg(6).Basic(),
                B(10, "Guard", S, 1, "Ability_Defend").Blk(5).Basic(),
                // Lessons a Companion Battle Trainer sells.
                B(20, "Bandage", S, 1, "INV_Misc_Bandage_08").Heal(6).Ex(),
                B(21, "Quick Jab", A, 0, "Ability_Gouge").Dmg(3),
                B(22, "Battle Shout", S, 1, "Ability_Warrior_BattleShout").Me(St::Strength, 1).Blk(3).Ex(),
                B(23, "Sunder", A, 2, "Ability_Warrior_Sunder").Dmg(8).On(St::Exposed, 2),
                B(24, "Shield Wall", S, 2, "Ability_Warrior_ShieldWall").Blk(13),
                B(25, "Feint", S, 0, "Ability_Rogue_Feint").On(St::Weakened, 1).Draw(1),
                B(26, "Cleave", A, 1, "Ability_Warrior_Cleave").Dmg(8),
                B(27, "Cleanse", S, 1, "Spell_Holy_DispelMagic").Sp(X::Cleanse).Blk(4),
                // What a tamer's daily reward may teach.
                B(30, "Mortal Strike", A, 2, "Ability_Warrior_SavageBlow").Dmg(14),
                B(31, "Adrenaline Rush", S, 0, "Spell_Shadow_ShadowWordDominate").Gain(2).Draw(1).Ex(),
                B(32, "Blade Flurry", P, 2, "Ability_Warrior_PunishingBlow").Me(St::Frenzy, 3),
                B(33, "Inner Fire", P, 1, "Spell_Holy_InnerFire").Me(St::Strength, 1).Me(St::Fortify, 2),
                B(34, "Thorns", S, 1, "Spell_Nature_Thorns").Me(St::Thorns, 2).Ex(),
                B(35, "Healing Potion", S, 0, "INV_Potion_51").Heal(10).Ex(),
                // The traits' cards.
                B(40, "Scorch", A, 1, "Spell_Fire_SoulBurn").Dmg(8),
                B(41, "Frost Nova", S, 1, "Spell_Frost_FrostNova").On(St::Weakened, 2).Blk(4),
                B(42, "Shadow Bite", A, 1, "Spell_Shadow_ShadowBolt").Dmg(6).Heal(2),
                B(43, "Static Charge", A, 1, "Spell_Nature_Lightning").Dmg(3, 3),
                B(44, "Gunshot", A, 1, "INV_Weapon_Rifle_06").Dmg(8),

                // Feline: bleed and combos.
                B(100, "Rake", A, 1, "Ability_Druid_Disembowel", Ar::Feline).Dmg(3).On(St::Bleed, 3),
                B(101, "Pounce", A, 0, "Ability_Druid_SupriseAttack", Ar::Feline).Dmg(3).Sp(X::Combo, 4),
                B(102, "Shred", A, 1, "Spell_Shadow_VampiricAura", Ar::Feline).Dmg(6).Sp(X::Combo, 4),
                B(103, "Prowl", S, 1, "Ability_Ambush", Ar::Feline).Me(St::Evasion, 1).Draw(1),
                B(104, "Rip", A, 2, "Ability_GhoulFrenzy", Ar::Feline).Dmg(8).Sp(X::BleedBurst),
                B(105, "Savage Roar", P, 1, "Ability_Druid_DemoralizingRoar", Ar::Feline).Me(St::Frenzy, 2),
                B(106, "Ferocious Bite", A, 1, "Ability_Druid_FerociousBite", Ar::Feline).Dmg(4, 2).Sp(X::Combo, 2),
                B(107, "Tiger's Fury", S, 0, "Ability_Mount_JungleTiger", Ar::Feline).On(St::Bleed, 5).Draw(1).Ex(),
                // Ursine: block and thorns.
                B(120, "Maul", A, 2, "Ability_Druid_Maul", Ar::Ursine).Dmg(10).Blk(4),
                B(121, "Bristle", S, 1, "Spell_Nature_Thorns", Ar::Ursine).Blk(5).Me(St::Thorns, 1),
                B(122, "Demoralizing Roar", S, 1, "Ability_Druid_DemoralizingRoar", Ar::Ursine).On(St::Weakened, 2).Blk(3),
                B(123, "Swipe", A, 1, "INV_Misc_MonsterClaw_03", Ar::Ursine).Dmg(8),
                B(124, "Spiked Hide", P, 1, "Spell_Nature_Thorns", Ar::Ursine).Me(St::Thorns, 2),
                B(125, "Barkskin", P, 2, "Spell_Nature_StoneClawTotem", Ar::Ursine).Me(St::Fortify, 4),
                B(126, "Hibernate", S, 2, "Spell_Nature_Sleep", Ar::Ursine).Blk(14).Heal(5).Ex(),
                B(127, "Frenzied Regeneration", S, 1, "Ability_BullRush", Ar::Ursine).Me(St::Regen, 5).Ex(),
                // Avian: evasion and draw.
                B(140, "Wing Buffet", A, 1, "Spell_Nature_Cyclone", Ar::Avian).Dmg(8).Draw(1).Sp(X::EvasionDraw),
                B(141, "Take Wing", S, 1, "Spell_Magic_FeatherFall", Ar::Avian).Me(St::Evasion, 1).Blk(6).Draw(1),
                B(142, "Screech", S, 0, "Spell_Shadow_DeathScream", Ar::Avian).On(St::Weakened, 1).Draw(1),
                B(143, "Dive", A, 1, "Ability_Hunter_SniperShot", Ar::Avian).Dmg(10),
                B(144, "Keen Eyes", P, 1, "Ability_Hunter_EagleEye", Ar::Avian).Me(St::Insight, 1),
                B(145, "Flurry of Talons", A, 2, "INV_Misc_MonsterClaw_04", Ar::Avian).Dmg(5, 4),
                B(146, "Updraft", S, 0, "Spell_Nature_Invisibilty", Ar::Avian).Me(St::Evasion, 2).Ex(),
                B(147, "Gust", A, 0, "Ability_Hunter_Pet_WindSerpent", Ar::Avian).Dmg(5).Draw(1),
                // Draconic: powers that grow.
                B(160, "Flame Breath", A, 2, "Spell_Fire_Fire", Ar::Draconic).Dmg(12).On(St::Exposed, 2),
                B(161, "Draconic Might", P, 1, "INV_Misc_Head_Dragon_01", Ar::Draconic).Me(St::Ritual, 1).Me(St::Strength, 1),
                B(162, "Dragonscale", P, 1, "INV_Misc_MonsterScales_15", Ar::Draconic).Me(St::Agility, 3).Blk(4),
                B(163, "Tail Swipe", A, 1, "INV_Misc_MonsterTail_03", Ar::Draconic).Dmg(8).On(St::Weakened, 1),
                B(164, "Wing Storm", A, 2, "Ability_Whirlwind", Ar::Draconic).Dmg(6, 2).Blk(6),
                B(165, "Ancient Fury", P, 1, "Spell_Fire_Incinerate", Ar::Draconic).Me(St::Strength, 2),
                B(166, "Dragonfire", A, 2, "Spell_Fire_FlameBolt", Ar::Draconic).Dmg(15),
                B(167, "Hoard's Ward", S, 1, "INV_Misc_Coin_02", Ar::Draconic).Blk(10),
                // Undead: exhaust and recursion.
                B(180, "Grave Claw", A, 0, "Spell_Shadow_AnimateDead", Ar::Undead).Dmg(7).Ex(),
                B(181, "Unending", P, 1, "Spell_Shadow_RaiseDead", Ar::Undead).Me(St::Undying, 4),
                B(182, "Raise Dead", S, 0, "Spell_Shadow_RaiseDead", Ar::Undead).Sp(X::Recall).Draw(1).Ex(),
                B(183, "Bone Shield", S, 1, "INV_Misc_Bone_01", Ar::Undead).Blk(11).Ex(),
                B(184, "Death Coil", A, 1, "Spell_Shadow_DeathCoil", Ar::Undead).Dmg(8).Heal(4).Ex(),
                B(185, "Corpse Explosion", A, 1, "Spell_Shadow_CorpseExplode", Ar::Undead).Dmg(3).Sp(X::PerExhausted, 2).Ex(),
                B(186, "Plague Strike", A, 1, "Spell_Shadow_CallofBone", Ar::Undead).Dmg(5).On(St::Poison, 2),
                B(187, "Unholy Frenzy", S, 1, "Spell_Shadow_UnholyFrenzy", Ar::Undead).Me(St::Strength, 1).Draw(1).Ex(),
                // Venom: poison.
                B(200, "Poison Fang", A, 1, "Ability_PoisonSting", Ar::Venom).Dmg(4).On(St::Poison, 2),
                B(201, "Toxic Spit", S, 1, "Spell_Nature_CorrosiveBreath", Ar::Venom).On(St::Poison, 3),
                B(202, "Web", S, 1, "Spell_Nature_Web", Ar::Venom).On(St::Weakened, 2).Blk(4),
                B(203, "Crippling Sting", A, 1, "Spell_Nature_SlowPoison", Ar::Venom).Dmg(5).On(St::Exposed, 1).On(St::Poison, 2),
                B(204, "Catalyst", S, 2, "Spell_Nature_NullifyPoison", Ar::Venom).Sp(X::PoisonDouble).Ex(),
                B(205, "Noxious Cloud", P, 3, "Spell_Nature_AbolishMagic", Ar::Venom).Me(St::Plague, 1),
                B(206, "Paralytic Venom", S, 1, "Spell_Nature_NullifyDisease", Ar::Venom).On(St::Drained, 1).Draw(1).Ex(),
                B(207, "Carapace", S, 1, "Spell_Nature_SpiritArmor", Ar::Venom).Blk(8),
                // Pack: strength and many hits.
                B(220, "Pack Tactics", A, 1, "Ability_Hunter_Pet_Wolf", Ar::Pack).Dmg(4, 2),
                B(221, "Howl", P, 1, "Spell_Nature_SpiritWolf", Ar::Pack).Me(St::Strength, 2),
                B(222, "Savage Bite", A, 2, "Ability_Hunter_Pet_Hyena", Ar::Pack).Dmg(12),
                B(223, "Hamstring", A, 1, "Ability_ShockWave", Ar::Pack).Dmg(5).On(St::Weakened, 2),
                B(224, "Tear", A, 1, "Ability_Warrior_SavageBlow", Ar::Pack).Dmg(2, 3),
                B(225, "Lunge", A, 0, "Ability_Rogue_Sprint", Ar::Pack).Dmg(4).Draw(1),
                B(226, "Bloodlust", P, 3, "Spell_Nature_BloodLust", Ar::Pack).Me(St::Bloodlust, 1),
                B(227, "Rally", S, 1, "Ability_Warrior_BattleShout", Ar::Pack).Blk(6).Draw(1),
                // Elemental: energy and big spells.
                B(240, "Flame Lash", A, 1, "Spell_Fire_Fireball", Ar::Elemental).Dmg(9),
                B(241, "Mana Surge", S, 0, "Spell_Nature_Purge", Ar::Elemental).Gain(2).Ex(),
                B(242, "Frost Shock", A, 1, "Spell_Frost_FrostShock", Ar::Elemental).Dmg(6).On(St::Weakened, 2),
                B(243, "Chain Lightning", A, 2, "Spell_Nature_ChainLightning", Ar::Elemental).Dmg(6, 3),
                B(244, "Stoneskin", S, 2, "Spell_Nature_StoneSkinTotem", Ar::Elemental).Blk(18),
                B(245, "Lava Burst", A, 3, "Spell_Fire_Volcano", Ar::Elemental).Dmg(28),
                B(246, "Attunement", P, 2, "Spell_Frost_SummonWaterElemental", Ar::Elemental).Me(St::Attunement, 1),
                B(247, "Earth Shock", A, 1, "Spell_Nature_EarthShock", Ar::Elemental).Dmg(5).Blk(5),
                // Fel: curses and drain.
                B(260, "Shadow Bolt", A, 1, "Spell_Shadow_ShadowBolt", Ar::Fel).Dmg(7).On(St::Exposed, 1),
                B(261, "Curse of Weakness", S, 1, "Spell_Shadow_CurseOfMannoroth", Ar::Fel).On(St::Weakened, 2).On(St::Exposed, 1),
                B(262, "Drain Life", A, 1, "Spell_Shadow_LifeDrain02", Ar::Fel).Dmg(7).Heal(3),
                B(263, "Corruption", S, 1, "Spell_Shadow_AbominationExplosion", Ar::Fel).On(St::Poison, 4),
                B(264, "Demon Skin", P, 1, "Spell_Shadow_RagingScream", Ar::Fel).Me(St::Fortify, 3),
                B(265, "Doom", S, 2, "Spell_Shadow_AuraOfDarkness", Ar::Fel).On(St::Exposed, 3).On(St::Weakened, 3).Ex(),
                B(266, "Felfire", A, 2, "Spell_Fire_Fireball02", Ar::Fel).Dmg(12).On(St::Exposed, 1),
                B(267, "Life Tap", S, 0, "Spell_Shadow_BurningSpirit", Ar::Fel).Pay(3).Gain(1).Draw(1),
                // Humanoid: tactics and draw.
                B(280, "Quick Shot", A, 0, "Ability_Marksmanship", Ar::Humanoid).Dmg(4),
                B(281, "Tactics", S, 1, "Ability_Warrior_DefensiveStance", Ar::Humanoid).Draw(3),
                B(282, "Shield Bash", A, 1, "Ability_Warrior_ShieldBash", Ar::Humanoid).Dmg(6).Blk(5),
                B(283, "Ambush", A, 2, "Ability_Rogue_Ambush", Ar::Humanoid).Dmg(10).On(St::Exposed, 2),
                B(284, "War Stomp", S, 1, "Ability_WarStomp", Ar::Humanoid).On(St::Weakened, 2).On(St::Drained, 1).Ex(),
                B(285, "Battle Stance", P, 1, "Ability_Warrior_OffensiveStance", Ar::Humanoid).Me(St::Strength, 1).Me(St::Agility, 1),
                B(286, "Second Wind", S, 1, "Spell_Nature_Rejuvenation", Ar::Humanoid).Heal(8).Ex(),
                B(287, "Throw Spear", A, 1, "INV_ThrowingAxe_01", Ar::Humanoid).Dmg(9),
                // Shell: block that hits back.
                B(300, "Shell Up", S, 1, "Ability_Hunter_Pet_Turtle", Ar::Shell).Blk(7),
                B(301, "Shell Slam", A, 1, "INV_Shield_04", Ar::Shell).Sp(X::BlockStrike).Dmg(4),
                B(302, "Death Roll", A, 2, "Ability_Hunter_Pet_Crocolisk", Ar::Shell).Dmg(7, 2).On(St::Weakened, 2),
                B(303, "Crushing Claw", A, 1, "Ability_Hunter_Pet_Crab", Ar::Shell).Dmg(7).On(St::Exposed, 1),
                B(304, "Hardened Shell", P, 1, "INV_Misc_Shell_02", Ar::Shell).Me(St::Plating, 1),
                B(305, "Healing Tides", S, 1, "Spell_Nature_HealingWaveGreater", Ar::Shell).Me(St::Regen, 5).Ex(),
                B(306, "Snap", A, 0, "Ability_Hunter_Pet_Crab", Ar::Shell).Dmg(4).Blk(4),
                B(307, "Undertow", A, 2, "Spell_Frost_SummonWaterElemental", Ar::Shell).Dmg(8).Blk(8),
                // Mechanical: overclock and plating.
                B(320, "Overclock", S, 0, "INV_Gizmo_02", Ar::Mechanical).Gain(1).Pay(1),
                B(321, "Reinforce", S, 1, "Ability_Warrior_ShieldWall", Ar::Mechanical).Blk(8).Me(St::Agility, 1),
                B(322, "Rocket Barrage", A, 2, "Spell_Fire_SelfDestruct", Ar::Mechanical).Dmg(5, 3),
                B(323, "Saw Blade", A, 1, "INV_Misc_Gear_01", Ar::Mechanical).Dmg(6).On(St::Bleed, 3),
                B(324, "Shield Generator", P, 2, "Spell_Nature_LightningShield", Ar::Mechanical).Me(St::Fortify, 5),
                B(325, "Steam Engine", P, 1, "INV_Gizmo_01", Ar::Mechanical).Me(St::Insight, 1),
                B(326, "Death Ray", A, 3, "INV_Wand_01", Ar::Mechanical).Dmg(28).Pay(3),
                B(327, "Bolt Gun", A, 1, "INV_Weapon_Rifle_06", Ar::Mechanical).Dmg(9),
                // Critter: cheap and quick.
                B(340, "Scurry", S, 0, "Ability_Rogue_Sprint", Ar::Critter).Blk(3).Draw(1),
                B(341, "Nibble", A, 0, "INV_Misc_Food_02", Ar::Critter).Dmg(3),
                B(342, "Squeak", S, 0, "Spell_Shadow_PsychicScream", Ar::Critter).On(St::Weakened, 1).On(St::Exposed, 1),
                B(343, "Burrow", S, 1, "Ability_Stealth", Ar::Critter).Me(St::Evasion, 1).Blk(5),
                B(344, "Frenzied Nibbles", A, 1, "Ability_Racial_Cannibalize", Ar::Critter).Dmg(3, 3),
                B(345, "Lucky Charm", P, 1, "INV_Misc_Rune_01", Ar::Critter).Me(St::Insight, 1),
                B(346, "Underdog", A, 1, "Ability_Hunter_BeastTraining", Ar::Critter).Dmg(6).Sp(X::Underdog, 7),
                B(347, "Hide", S, 1, "Ability_Vanish", Ar::Critter).Blk(8).Draw(1),
            };
            for (Card& c : t)
                if (c.type == CardType::Attack && c.hits == 0)
                    c.hits = 1;
            return t;
        }

        Card const kNoCard{};
    }

    std::vector<Card> const& CardTable()
    {
        static std::vector<Card> const kTable = BuildTable();
        return kTable;
    }

    uint16_t AbilityCardId(Companions::Moveset m, uint8_t stage, uint8_t option)
    {
        return uint16_t(kAbilityCardBase | (uint16_t(m) << 3) | ((stage & 3) << 1) | (option & 1));
    }

    Card CardFromAbility(Companions::Ability const& a, uint16_t id)
    {
        // Strength from the catalog's numbers: a basic hit's share (pct) and over-time share.
        uint16_t const pts = uint16_t(std::clamp<int>((int(a.pct) + int(a.dotPct)) * 6 / 100, 0, 30));
        Card c;
        c.id = id;
        c.name = a.name;
        c.spell = a.spell;
        c.cost = 1;
        c.type = CardType::Attack;
        c.hits = 1;
        switch (a.kind)
        {
            case AbilityKind::Damage:
                c.icon = "Ability_MeleeDamage";
                c.damage = std::max<uint16_t>(pts, 8);
                if (a.dotPct)
                    c.damage = std::max<uint16_t>(4, c.damage / 2), c.enemy[uint8_t(Status::Bleed)] = 3;
                break;
            case AbilityKind::Burst:
                c.icon = "Ability_Warrior_Decisivestrike";
                c.cost = 2;
                c.damage = std::clamp<uint16_t>(uint16_t(pts + 4), 13, 18);
                break;
            case AbilityKind::AoE:
                c.icon = "Ability_Whirlwind";
                c.hits = 2;
                c.damage = std::clamp<uint16_t>(uint16_t(pts / 2), 4, 6);
                break;
            case AbilityKind::Heal:
                c.icon = "Spell_Nature_HealingTouch";
                c.type = CardType::Skill;
                c.heal = uint8_t(std::clamp<int>(a.pct ? a.pct / 3 : 8, 6, 12));
                c.exhaust = true;
                break;
            case AbilityKind::Root:
                c.icon = "Spell_Nature_StrangleVines";
                c.damage = 5;
                c.enemy[uint8_t(Status::Weakened)] = 2;
                break;
            case AbilityKind::Interrupt:
                c.icon = "Ability_Kick";
                c.cost = 0;
                c.damage = 4;
                c.enemy[uint8_t(Status::Weakened)] = 1;
                break;
            case AbilityKind::Stun:
            case AbilityKind::AoEStun:
                c.icon = "Spell_Frost_Stun";
                c.type = CardType::Skill;
                c.hits = 0;
                c.enemy[uint8_t(Status::Drained)] = 1;
                c.block = 4;
                c.exhaust = true;
                break;
            case AbilityKind::Fear:
                c.icon = "Spell_Shadow_PsychicScream";
                c.type = CardType::Skill;
                c.hits = 0;
                c.enemy[uint8_t(Status::Weakened)] = 2;
                c.enemy[uint8_t(Status::Exposed)] = 1;
                break;
            case AbilityKind::Debuff:
                c.icon = "Ability_Warrior_Sunder";
                c.damage = 4;
                c.enemy[uint8_t(Status::Exposed)] = 2;
                break;
            case AbilityKind::Buff:
                c.icon = "Ability_Warrior_BattleShout";
                c.type = CardType::Skill;
                c.hits = 0;
                c.self[uint8_t(Status::Strength)] = 2;
                c.exhaust = true;
                break;
            case AbilityKind::Defensive:
                c.icon = "Ability_Defend";
                c.type = CardType::Skill;
                c.hits = 0;
                c.cost = 1;
                c.block = 10;
                break;
            case AbilityKind::Taunt:
                c.icon = "Spell_Nature_Reincarnation";
                c.type = CardType::Skill;
                c.hits = 0;
                c.block = 6;
                c.self[uint8_t(Status::Thorns)] = 1;
                break;
            case AbilityKind::Dispel:
                c.icon = "Spell_Holy_DispelMagic";
                c.damage = 5;
                c.special = Special::Cleanse;
                break;
            case AbilityKind::GapCloser:
                c.icon = "Ability_Warrior_Charge";
                c.damage = 6;
                c.draw = 1;
                break;
            default:
                return Card{};
        }
        if (c.type != CardType::Attack)
            c.hits = 0;
        return c;
    }

    Card const& CardById(uint16_t id)
    {
        if (id >= kAbilityCardBase)
        {
            // Made once per id and kept (the catalog never changes while running).
            static std::vector<Card> cache(size_t(Companions::Moveset::Count) << 3);
            uint16_t const k = uint16_t(id - kAbilityCardBase);
            uint8_t const m = uint8_t(k >> 3), stage = uint8_t((k >> 1) & 3), option = uint8_t(k & 1);
            if (m == 0 || m >= uint8_t(Companions::Moveset::Count) || stage >= Companions::kMaxStages)
                return kNoCard;
            Card& c = cache[k];
            if (!c.Exists())
            {
                static std::mutex lock;
                std::lock_guard<std::mutex> g(lock);
                if (!c.Exists())
                {
                    Companions::Ability const& a = Companions::MovesetData(Companions::Moveset(m)).stages[stage][option];
                    if (!a.Exists() || a.kind == AbilityKind::Mounted)
                        return kNoCard;
                    Card made = CardFromAbility(a, id);
                    if (!made.Exists())
                        return kNoCard;
                    c = made;
                }
            }
            return c;
        }
        std::vector<Card> const& t = CardTable();
        auto it = std::lower_bound(t.begin(), t.end(), id, [](Card const& c, uint16_t v) { return c.id < v; });
        return it != t.end() && it->id == id ? *it : kNoCard;
    }

    std::string CardText(Card const& c)
    {
        std::string s;
        auto add = [&s](std::string const& part) {
            if (!s.empty())
                s += ' ';
            s += part;
        };
        auto n = [](int v) { return std::to_string(v); };
        if (c.type == CardType::Attack)
        {
            if (c.special == Special::BlockStrike)
                add("Deal damage equal to your Block.");
            else if (c.hits > 1)
                add("Deal " + n(c.damage) + " damage " + n(c.hits) + " times.");
            else
                add("Deal " + n(c.damage) + " damage.");
        }
        switch (c.special)
        {
            case Special::Combo: add("Combo: +" + n(c.extra) + " after another attack."); break;
            case Special::BleedBurst: add("+ the target's Bleed."); break;
            case Special::PoisonDouble: add("Double the target's Poison."); break;
            case Special::Recall: add("Return the card you exhausted last to your hand."); break;
            case Special::PerExhausted: add("+" + n(c.extra) + " damage for each card in your exhaust pile."); break;
            case Special::EvasionDraw: add("If you have Evasion, draw 1 card."); break;
            case Special::Underdog: add("+" + n(c.extra) + " damage if you have less health than the target."); break;
            case Special::Cleanse: add("Remove your debuffs."); break;
            default: break;
        }
        if (c.block)
            add("Gain " + n(c.block) + " Block.");
        for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
            if (c.enemy[i])
                add("Apply " + n(c.enemy[i]) + " " + StatusName(Status(i)) + ".");
        for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
        {
            if (!c.self[i])
                continue;
            Status const st = Status(i);
            switch (st)
            {
                case Status::Ritual: add("At the start of each turn, gain " + n(c.self[i]) + " Strength."); break;
                case Status::Fortify: add("At the start of each turn, gain " + n(c.self[i]) + " Block."); break;
                case Status::Plague: add("At the start of each turn, apply " + n(c.self[i]) + " Poison to the enemy."); break;
                case Status::Insight: add("Draw " + n(c.self[i]) + " more card each turn."); break;
                case Status::Plating: add("Your Block no longer falls away at the start of your turn."); break;
                case Status::Frenzy: add("Each attack after your first each turn deals " + n(c.self[i]) + " more damage."); break;
                case Status::Undying: add("Whenever one of your cards is exhausted, gain " + n(c.self[i]) + " Block."); break;
                case Status::Attunement: add("Gain " + n(c.self[i]) + " more energy each turn."); break;
                case Status::Bloodlust: add("At the start of each turn, lose 2 health and gain " + n(c.self[i]) + " Strength."); break;
                case Status::Regen: add("Heal " + n(c.self[i]) + " at the end of your turn, 1 less each turn."); break;
                default: add("Gain " + n(c.self[i]) + " " + StatusName(st) + "."); break;
            }
        }
        if (c.heal)
            add("Heal " + n(c.heal) + ".");
        if (c.energy)
            add("Gain " + n(c.energy) + " energy.");
        if (c.draw)
            add("Draw " + n(c.draw) + (c.draw == 1 ? " card." : " cards."));
        if (c.selfHurt)
            add("Lose " + n(c.selfHurt) + " health.");
        if (c.exhaust)
            add("Exhaust.");
        return s;
    }

    uint16_t BasicAttackCard(Companions::MovesetInfo const& m)
    {
        switch (m.basicSpell)
        {
            case 17253: return 1;
            case 16827: return 2;
            case 3110: return 3;
            case 24844: return 4;
            case 9591: return 5;
            case 9672: return 6;
            case 9532: return 7;
            case 8646: return 8;
            default: return 9;
        }
    }

    // ── Decks ───────────────────────────────────────────────────────────────────────────────

    std::vector<uint16_t> ArchetypeCards(Archetype a)
    {
        if (uint8_t(a) >= uint8_t(Archetype::Count))
            return {};
        uint16_t const first = uint16_t(100 + uint16_t(a) * 20);
        std::vector<uint16_t> out;
        for (uint16_t k = 0; k < 8; ++k)
            out.push_back(uint16_t(first + k));
        return out;
    }

    namespace
    {
        // The ability cards a companion picked (both options of a Twin-souled stage).
        std::vector<uint16_t> AbilityCards(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s)
        {
            std::vector<uint16_t> out;
            Companions::Moveset const m = Companions::MovesetOf(sp);
            uint8_t const top = std::min<uint8_t>(s.stage, uint8_t(std::max<uint8_t>(sp.stageCount, 1) - 1));
            bool const twin = Companions::HasTrait(s.traits, Companions::Trait::TwinSouled);
            for (uint8_t st = 0; st <= top && st < Companions::kMaxStages; ++st)
            {
                uint8_t const pick = s.picks[st];
                if (pick == 0)
                    continue;
                for (uint8_t opt = 0; opt < 2; ++opt)
                {
                    bool const chosen = opt == (pick == 2 ? 1 : 0);
                    if (!chosen && !(twin && s.twinStage == st + 1))
                        continue;
                    uint16_t const id = AbilityCardId(m, st, opt);
                    if (CardById(id).Exists())
                        out.push_back(id);
                }
            }
            return out;
        }

        std::vector<uint16_t> TraitCards(uint32_t traits)
        {
            using Companions::Trait;
            std::vector<uint16_t> out;
            if (Companions::HasTrait(traits, Trait::FireTouched)) out.push_back(40);
            if (Companions::HasTrait(traits, Trait::FrostTouched)) out.push_back(41);
            if (Companions::HasTrait(traits, Trait::ShadowTouched)) out.push_back(42);
            if (Companions::HasTrait(traits, Trait::StormTouched)) out.push_back(43);
            if (Companions::HasTrait(traits, Trait::Gunner)) out.push_back(44);
            return out;
        }

        void Know(std::vector<LibraryCard>& lib, uint16_t card, uint8_t copies, CardSource src)
        {
            if (!CardById(card).Exists())
                return;
            for (LibraryCard& l : lib)
                if (l.card == card)
                {
                    l.copies = std::max(l.copies, copies);
                    return;
                }
            lib.push_back({card, copies, uint8_t(src)});
        }
    }

    std::vector<LibraryCard> Library(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
        std::vector<uint16_t> const& learned, std::vector<Companions::CompanionSpecies> const& all)
    {
        std::vector<LibraryCard> lib;
        Know(lib, BasicAttackCard(Companions::MovesetData(Companions::MovesetOf(sp))), kMaxBasicCopies, CardSource::Basic);
        Know(lib, kGuardCard, kMaxBasicCopies, CardSource::Basic);
        Archetype const a = ArchetypeOf(sp.family);
        std::vector<uint16_t> const own = ArchetypeCards(a);
        Know(lib, own[0], kMaxCopies, CardSource::Family);
        Know(lib, own[1], kMaxCopies, CardSource::Family);
        for (size_t k = 0; k < kLibraryLevels.size() && k + 2 < own.size(); ++k)
            if (s.level >= kLibraryLevels[k])
                Know(lib, own[k + 2], kMaxCopies, CardSource::Level);
        for (uint16_t c : AbilityCards(sp, s))
            Know(lib, c, 1, CardSource::Ability);
        for (uint16_t c : TraitCards(s.traits))
            Know(lib, c, 1, CardSource::Trait);
        if (s.generation > 0)
            for (uint16_t parent : {s.parentA, s.parentB})
                if (Companions::CompanionSpecies const* p = parent ? Companions::FindSpecies(all, parent) : nullptr)
                    if (ArchetypeOf(p->family) != a)
                        Know(lib, ArchetypeCards(ArchetypeOf(p->family))[0], 1, CardSource::Bred);
        for (uint16_t c : learned)
            Know(lib, c, kMaxCopies, CardSource::Learned);
        return lib;
    }

    std::vector<uint16_t> StarterDeck(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s)
    {
        std::vector<uint16_t> d;
        uint16_t const basic = BasicAttackCard(Companions::MovesetData(Companions::MovesetOf(sp)));
        d.insert(d.end(), 5, basic);
        d.insert(d.end(), 4, kGuardCard);
        std::vector<uint16_t> const own = ArchetypeCards(ArchetypeOf(sp.family));
        d.push_back(own[0]);
        d.push_back(own[0]);
        d.push_back(own[1]);
        for (uint16_t c : AbilityCards(sp, s))
            d.push_back(c);
        for (uint16_t c : TraitCards(s.traits))
            d.push_back(c);
        return d;
    }

    std::vector<uint16_t> BuiltDeck(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s)
    {
        std::vector<uint16_t> d = StarterDeck(sp, s);
        std::vector<uint16_t> const own = ArchetypeCards(ArchetypeOf(sp.family));
        size_t learnt = 0;
        for (size_t k = 0; k < kLibraryLevels.size() && k + 2 < own.size(); ++k)
            if (s.level >= kLibraryLevels[k])
                d.push_back(own[k + 2]), ++learnt;
        // A grown deck trims a basic attack and a Guard for every two cards it learned.
        for (size_t k = 0; k < learnt / 2; ++k)
        {
            auto g = std::find(d.begin(), d.end(), kGuardCard);
            if (g != d.end() && std::count(d.begin(), d.end(), kGuardCard) > 2)
                d.erase(g);
            auto b = std::find(d.begin(), d.end(), d.front());
            if (std::count(d.begin(), d.end(), d.front()) > 2)
                d.erase(b);
        }
        return d;
    }

    char const* DeckProblemText(DeckProblem p)
    {
        switch (p)
        {
            case DeckProblem::TooSmall: return "A deck needs at least 12 cards.";
            case DeckProblem::TooLarge: return "A deck holds at most 30 cards.";
            case DeckProblem::Unknown: return "Your companion doesn't know one of those cards.";
            case DeckProblem::TooManyCopies: return "Too many copies of one card.";
            default: return "";
        }
    }

    DeckProblem CheckDeck(std::vector<LibraryCard> const& library, std::vector<uint16_t> const& deck)
    {
        if (deck.size() < kMinDeck)
            return DeckProblem::TooSmall;
        if (deck.size() > kMaxDeck)
            return DeckProblem::TooLarge;
        for (uint16_t c : deck)
        {
            auto it = std::find_if(library.begin(), library.end(), [c](LibraryCard const& l) { return l.card == c; });
            if (it == library.end())
                return DeckProblem::Unknown;
            if (size_t(std::count(deck.begin(), deck.end(), c)) > it->copies)
                return DeckProblem::TooManyCopies;
        }
        return DeckProblem::None;
    }

    std::vector<uint16_t> DuelDeck(std::vector<LibraryCard> const& library, std::vector<uint16_t> const& saved,
        Companions::CompanionSpecies const& sp, Companions::CompanionState const& s)
    {
        if (!saved.empty() && CheckDeck(library, saved) == DeckProblem::None)
            return saved;
        return StarterDeck(sp, s);
    }

    std::string JoinCards(std::vector<uint16_t> const& cards)
    {
        std::string s;
        for (uint16_t c : cards)
        {
            if (!s.empty())
                s += ',';
            s += std::to_string(c);
        }
        return s;
    }

    std::vector<uint16_t> SplitCards(std::string const& text)
    {
        std::vector<uint16_t> out;
        uint32_t v = 0;
        bool any = false;
        for (char ch : text + ",")
        {
            if (ch >= '0' && ch <= '9')
            {
                v = std::min<uint32_t>(v * 10 + uint32_t(ch - '0'), 0xFFFF);
                any = true;
            }
            else if (ch == ',')
            {
                if (any && v)
                    out.push_back(uint16_t(v));
                v = 0;
                any = false;
            }
        }
        return out;
    }

    std::vector<uint16_t> const& TrainerCards()
    {
        static std::vector<uint16_t> const k = {20, 21, 22, 23, 24, 25, 26, 27};
        return k;
    }

    uint32_t TrainerPrice(uint16_t card, uint8_t ownerLevel)
    {
        Card const& c = CardById(card);
        return uint32_t(500 + 100 * uint32_t(ownerLevel)) * (c.cost >= 2 ? 2u : 1u);
    }

    std::vector<uint16_t> const& DropCards()
    {
        static std::vector<uint16_t> const k = {30, 31, 32, 33, 34, 35};
        return k;
    }

    uint16_t RollDropCard(std::vector<LibraryCard> const& library, uint32_t roll)
    {
        std::vector<uint16_t> fresh;
        for (uint16_t c : DropCards())
            if (std::none_of(library.begin(), library.end(), [c](LibraryCard const& l) { return l.card == c; }))
                fresh.push_back(c);
        return fresh.empty() ? 0 : fresh[roll % fresh.size()];
    }

    // ── A companion in a duel ───────────────────────────────────────────────────────────────

    uint16_t DuelHealth(Companions::Role role, uint8_t stage, uint8_t level, uint8_t rarity, uint32_t traits)
    {
        float h = 60.f + float(std::clamp<uint8_t>(level, 1, Companions::kMaxLevel)) * 0.6f + 10.f * float(std::min<uint8_t>(stage, 2))
            + 2.f * float(std::min<uint8_t>(rarity, 4));
        if (role == Companions::Role::Tank)
            h *= 1.05f;
        else if (role == Companions::Role::Ranged)
            h *= 0.95f;
        if (Companions::HasTrait(traits, Companions::Trait::Giant))
            h += 6.f;
        return uint16_t(std::lround(h));
    }

    BattlePet MakePet(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s, uint64_t item,
        std::vector<uint16_t> deck, uint8_t level)
    {
        BattlePet p;
        p.item = item;
        p.species = sp.id;
        p.name = sp.name;
        p.family = sp.family;
        p.battleFamily = BattleFamilyOf(sp.family);
        p.archetype = ArchetypeOf(sp.family);
        p.stage = std::min<uint8_t>(s.stage, uint8_t(std::max<uint8_t>(sp.stageCount, 1) - 1));
        p.level = level ? level : s.level;
        p.rarity = s.rarity;
        p.traits = s.traits;
        p.maxHealth = p.health = DuelHealth(sp.role, p.stage, p.level, s.rarity, s.traits);
        p.deck = std::move(deck);
        return p;
    }

    BattlePet MakePet(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s, uint64_t item,
        uint8_t level)
    {
        return MakePet(sp, s, item, StarterDeck(sp, s), level);
    }

    // ── The duel ────────────────────────────────────────────────────────────────────────────

    Companions::DuelEnd DuelEndOf(End e)
    {
        switch (e)
        {
            case End::Defeated: return Companions::DuelEnd::Beaten;
            case End::Forfeit: return Companions::DuelEnd::Forfeit;
            case End::Turns: return Companions::DuelEnd::Timeout;
            case End::Abandoned: return Companions::DuelEnd::Cancelled;
            default: return Companions::DuelEnd::None;
        }
    }

    char const* RefusalText(Refusal r)
    {
        switch (r)
        {
            case Refusal::NotYourTurn: return "It isn't your turn.";
            case Refusal::NoSuchCard: return "That card isn't in your hand.";
            case Refusal::NoEnergy: return "Not enough energy.";
            case Refusal::Over: return "The duel is over.";
            case Refusal::Stale: return "Your hand changed; try again.";
            default: return "";
        }
    }

    namespace
    {
        using Companions::HasTrait;
        using Companions::Trait;

        int16_t& Of(SideState& s, Status st) { return s.status[uint8_t(st)]; }

        // A status put on a companion, unless its traits keep it off.
        void Apply(SideState& s, BattlePet const& p, Status st, int16_t n)
        {
            if (n <= 0)
                return;
            if (st == Status::Weakened && HasTrait(p.traits, Trait::ThickSkulled))
                return;
            if (st == Status::Exposed && HasTrait(p.traits, Trait::Spellward))
                return;
            Of(s, st) = int16_t(std::min<int>(Of(s, st) + n, 999));
        }
    }

    Battle::Battle(std::vector<BattlePet> a, std::vector<BattlePet> b, uint32_t seed, std::array<bool, 2> ai, int first)
    {
        m_team = {std::move(a), std::move(b)};
        m_ai = ai;
        m_rng = seed ? seed : 1;
        if (m_team[0].empty() || m_team[1].empty())
        {
            m_phase = Phase::Over;
            m_end = End::Abandoned;
            return;
        }
        m_toPlay = first < 0 ? int(Roll(2)) : (first & 1);
        Begin();
    }

    Battle Battle::Fork() const
    {
        Battle c;
        c.m_team = m_team;
        c.m_active = m_active;
        c.m_side = m_side;
        c.m_ai = m_ai;
        c.m_phase = m_phase;
        c.m_end = m_end;
        c.m_winner = m_winner;
        c.m_toPlay = m_toPlay;
        c.m_turn = m_turn;
        c.m_rng = m_rng;
        c.m_quiet = true;
        return c;
    }

    uint32_t Battle::Roll(uint32_t n)
    {
        // xorshift32: the duel's own sequence.
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return n ? m_rng % n : 0;
    }

    void Battle::Shuffle(std::vector<uint16_t>& v)
    {
        for (size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[Roll(uint32_t(i))]);
    }

    void Battle::Ready(int side)
    {
        // A companion steps in: its deck shuffled, a fresh hand, its traits' gifts.
        side &= 1;
        BattlePet& p = Me(side);
        SideState fresh;
        fresh.timeouts = m_side[side].timeouts;
        m_side[side] = fresh;
        SideState& s = m_side[side];
        p.fought = true;
        s.draw = p.deck;
        Shuffle(s.draw);
        if (HasTrait(p.traits, Trait::Ferocious) || HasTrait(p.traits, Trait::Alpha))
            Of(s, Status::Strength) += 1;
        if (HasTrait(p.traits, Trait::Primal))
            Of(s, Status::Strength) += 1, Of(s, Status::Agility) += 1;
        if (HasTrait(p.traits, Trait::Ironhide))
            Of(s, Status::Agility) += 1;
        if (HasTrait(p.traits, Trait::Runt))
            Of(s, Status::Evasion) += 1;
        BattlePet const& foe = Active(side ^ 1);
        if (p.level > foe.level)
            Of(s, Status::Strength) += int16_t(std::min(3, (p.level - foe.level) / kLevelsPerStrength));
        uint8_t hand = kHandSize;
        if (HasTrait(p.traits, Trait::Swift))
            ++hand;
        // Two players: the side going first starts with one card fewer.
        if (m_turn == 1 && side == m_toPlay && !m_ai[0] && !m_ai[1])
            --hand;
        DrawCards(side, hand);
        if (m_ai[side])
            Plan(side);
    }

    void Battle::Begin()
    {
        m_active = {0, 0};
        Ready(0);
        Ready(1);
        if (!m_quiet)
            Say(m_team[0][0].name + " and " + m_team[1][0].name + " square up.");
        StartTurn(m_toPlay);
    }

    void Battle::DrawCards(int side, uint8_t n)
    {
        SideState& s = m_side[side & 1];
        for (uint8_t i = 0; i < n && s.hand.size() < kMaxHand; ++i)
        {
            if (s.draw.empty())
            {
                if (s.discard.empty())
                    return;
                s.draw.swap(s.discard);
                Shuffle(s.draw);
            }
            s.hand.push_back(s.draw.back());
            s.draw.pop_back();
        }
    }

    void Battle::StartTurn(int side)
    {
        side &= 1;
        m_starting = true;
        SideState& s = m_side[side];
        if (Of(s, Status::Plating) <= 0)
            s.block = 0;
        auto tick = [&](Status st, char const* what) {
            int16_t const n = Of(s, st);
            if (n <= 0)
                return;
            if (!m_quiet)
                Say(Me(side).name + " suffers " + std::to_string(n) + " from " + what + ".");
            Hurt(side, uint16_t(n), false);
        };
        tick(Status::Poison, "poison");
        if (Of(s, Status::Poison) > 0)
            --Of(s, Status::Poison);
        tick(Status::Bleed, "bleeding");
        Of(s, Status::Bleed) = int16_t(Of(s, Status::Bleed) / 2);
        if (Of(s, Status::Bloodlust) > 0)
        {
            Hurt(side, 2, false);
            Of(s, Status::Strength) += Of(s, Status::Bloodlust);
        }
        Of(s, Status::Strength) += std::max<int16_t>(0, Of(s, Status::Ritual));
        if (Of(s, Status::Fortify) > 0)
            s.block = uint16_t(std::min<int>(s.block + Of(s, Status::Fortify), 999));
        if (Of(s, Status::Plague) > 0)
            Apply(m_side[side ^ 1], Active(side ^ 1), Status::Poison, Of(s, Status::Plague));
        CheckBeaten();
        m_starting = false;
        if (m_phase == Phase::Over)
            return;
        // Whoever fights now (the same one, or one that just stepped in) gets the turn.
        SideState& now = m_side[side];
        int const energy = int(kEnergy) + std::max<int16_t>(0, Of(now, Status::Attunement)) - std::max<int16_t>(0, Of(now, Status::Drained))
            + (now.firstTurn && HasTrait(Me(side).traits, Trait::Loyal) ? 1 : 0);
        now.energy = uint8_t(std::clamp(energy, 0, 9));
        Of(now, Status::Drained) = 0;
        now.attacksThisTurn = 0;
        now.firstTurn = false;
        m_passTurn = false;
    }

    void Battle::EndTurnOf(int side, bool fresh)
    {
        side &= 1;
        if (m_phase == Phase::Over)
            return;
        SideState& s = m_side[side];
        if (!fresh)
        {
            if (Of(s, Status::Regen) > 0)
            {
                BattlePet& p = Me(side);
                p.health = uint16_t(std::min<int>(p.maxHealth, p.health + Of(s, Status::Regen)));
                --Of(s, Status::Regen);
            }
            s.discard.insert(s.discard.end(), s.hand.begin(), s.hand.end());
            s.hand.clear();
            if (Of(s, Status::Exposed) > 0)
                --Of(s, Status::Exposed);
            if (Of(s, Status::Weakened) > 0)
                --Of(s, Status::Weakened);
            s.attacksThisTurn = 0;
            DrawCards(side, uint8_t(kHandSize + std::clamp<int16_t>(Of(s, Status::Insight), 0, 3)));
            if (m_ai[side])
                Plan(side);
        }
        if (++m_turn > kMaxTurns)
        {
            auto share = [this](int k) {
                uint32_t h = 0, m = 0;
                for (BattlePet const& p : m_team[k])
                    h += p.beaten ? 0 : p.health, m += p.maxHealth;
                return m ? float(h) / float(m) : 0.f;
            };
            float const a = share(0), b = share(1);
            Finish(End::Turns, std::fabs(a - b) < 0.01f ? -1 : a > b ? 0 : 1);
            return;
        }
        m_toPlay = side ^ 1;
        StartTurn(m_toPlay);
    }

    void Battle::Hurt(int side, uint16_t amount, bool)
    {
        side &= 1;
        BattlePet& p = Me(side);
        if (p.beaten || amount == 0)
            return;
        bool const wasHigh = p.health * 2 > p.maxHealth;
        if (amount >= p.health)
        {
            if (HasTrait(p.traits, Trait::PhoenixBlooded) && !p.phoenixUsed)
            {
                p.phoenixUsed = true;
                p.health = uint16_t(std::max(1, p.maxHealth / 4));
                if (!m_quiet)
                    Say(p.name + " rises from the ashes!");
                return;
            }
            // A duel never kills: the companion stays at 1 health, beaten.
            p.health = 1;
            p.beaten = true;
            return;
        }
        p.health = uint16_t(p.health - amount);
        if (wasHigh && p.health * 2 <= p.maxHealth && HasTrait(p.traits, Trait::Berserk))
        {
            Of(m_side[side], Status::Strength) += 2;
            if (!m_quiet)
                Say(p.name + " goes berserk!");
        }
    }

    void Battle::GainBlock(int side, uint16_t amount)
    {
        SideState& s = m_side[side & 1];
        s.block = uint16_t(std::min<int>(s.block + amount, 999));
    }

    void Battle::Hit(int side, uint16_t damage)
    {
        int const t = side ^ 1;
        SideState& ts = m_side[t];
        if (Of(ts, Status::Evasion) > 0)
        {
            --Of(ts, Status::Evasion);
            if (!m_quiet)
                Say(Me(t).name + " evades.");
            return;
        }
        uint16_t const absorbed = std::min(ts.block, damage);
        ts.block = uint16_t(ts.block - absorbed);
        uint16_t const through = uint16_t(damage - absorbed);
        Hurt(t, through, true);
        if (through && HasTrait(Me(side).traits, Trait::Bloodthirsty))
        {
            BattlePet& me = Me(side);
            me.health = uint16_t(std::min<int>(me.maxHealth, me.health + 1));
        }
        if (Of(ts, Status::Thorns) > 0)
        {
            SideState& ms = m_side[side];
            uint16_t const thorns = uint16_t(Of(ts, Status::Thorns));
            uint16_t const blocked = std::min(ms.block, thorns);
            ms.block = uint16_t(ms.block - blocked);
            Hurt(side, uint16_t(thorns - blocked), false);
        }
    }

    void Battle::Exhausted(int side, uint16_t card)
    {
        SideState& s = m_side[side & 1];
        s.exhaust.push_back(card);
        if (Of(s, Status::Undying) > 0)
            GainBlock(side, uint16_t(Of(s, Status::Undying)));
    }

    uint16_t Battle::CardDamage(int side, Card const& c) const
    {
        side &= 1;
        if (c.type != CardType::Attack)
            return 0;
        SideState const& s = m_side[side];
        SideState const& t = m_side[side ^ 1];
        int d = int(c.damage) + s.Get(Status::Strength);
        if (s.attacksThisTurn > 0)
            d += std::max<int16_t>(0, s.Get(Status::Frenzy)) + (c.special == Special::Combo ? c.extra : 0);
        switch (c.special)
        {
            case Special::BleedBurst: d += std::max<int16_t>(0, t.Get(Status::Bleed)); break;
            case Special::BlockStrike: d += s.block; break;
            case Special::PerExhausted: d += int(c.extra) * int(s.exhaust.size()); break;
            case Special::Underdog:
                if (Active(side).health < Active(side ^ 1).health)
                    d += c.extra;
                break;
            default: break;
        }
        d = std::max(d, 0);
        if (s.Get(Status::Weakened) > 0)
            d = d * 3 / 4;
        if (t.Get(Status::Exposed) > 0)
            d = d * 3 / 2;
        return uint16_t(std::min(d, 999));
    }

    uint16_t Battle::CardBlock(int side, Card const& c) const
    {
        if (!c.block)
            return 0;
        return uint16_t(std::max(0, int(c.block) + m_side[side & 1].Get(Status::Agility)));
    }

    Refusal Battle::Check(int side, Action a) const
    {
        side &= 1;
        if (m_phase == Phase::Over)
            return Refusal::Over;
        if (a.kind == ActionKind::Forfeit)
            return Refusal::None;
        if (m_toPlay != side)
            return Refusal::NotYourTurn;
        if (a.kind == ActionKind::EndTurn)
            return Refusal::None;
        if (a.kind != ActionKind::Play)
            return Refusal::NoSuchCard;
        SideState const& s = m_side[side];
        if (a.index >= s.hand.size())
            return Refusal::NoSuchCard;
        if (a.card && s.hand[a.index] != a.card)
            return Refusal::Stale;
        if (CardById(s.hand[a.index]).cost > s.energy)
            return Refusal::NoEnergy;
        return Refusal::None;
    }

    Refusal Battle::Choose(int side, Action a)
    {
        side &= 1;
        Refusal const r = Check(side, a);
        if (r != Refusal::None)
            return r;
        switch (a.kind)
        {
            case ActionKind::Forfeit:
                if (!m_quiet)
                    Say(Active(side).name + "'s side yields.");
                Finish(End::Forfeit, side ^ 1);
                break;
            case ActionKind::EndTurn:
                m_side[side].timeouts = 0;
                EndTurnOf(side);
                break;
            case ActionKind::Play:
                m_side[side].timeouts = 0;
                Play(side, a.index);
                break;
            default:
                break;
        }
        return Refusal::None;
    }

    void Battle::Play(int side, uint8_t slot)
    {
        side &= 1;
        int const t = side ^ 1;
        SideState& s = m_side[side];
        uint16_t const id = s.hand[slot];
        Card const& c = CardById(id);
        s.energy = uint8_t(s.energy - std::min(s.energy, c.cost));
        s.hand.erase(s.hand.begin() + slot);
        uint8_t const mine = m_active[side], theirs = m_active[t];
        auto same = [&] { return m_phase != Phase::Over && m_active[side] == mine && m_active[t] == theirs
            && !Me(side).beaten && !Me(t).beaten; };
        if (!m_quiet)
            Say(Me(side).name + " plays " + c.name + ".");
        bool cut = false;
        if (c.type == CardType::Attack)
        {
            uint16_t const before = Me(t).health;
            for (uint8_t h = 0; h < std::max<uint8_t>(c.hits, 1); ++h)
            {
                Hit(side, CardDamage(side, c));
                if (!same())
                {
                    cut = true;
                    break;
                }
            }
            if (!cut && !m_quiet && Me(t).health < before)
                Say(Me(t).name + " takes " + std::to_string(before - Me(t).health) + ".");
            if (!cut && s.attacksThisTurn == 0 && HasTrait(Me(side).traits, Trait::Venomous))
                Apply(m_side[t], Me(t), Status::Poison, 1);
            ++s.attacksThisTurn;
        }
        if (!cut)
        {
            if (c.block)
                GainBlock(side, CardBlock(side, c));
            for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
                if (c.enemy[i])
                    Apply(m_side[t], Me(t), Status(i), c.enemy[i]);
            for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
                if (c.self[i])
                    Of(s, Status(i)) = int16_t(std::min<int>(Of(s, Status(i)) + c.self[i], 999));
            switch (c.special)
            {
                case Special::PoisonDouble:
                    Of(m_side[t], Status::Poison) = int16_t(std::min<int>(Of(m_side[t], Status::Poison) * 2, 999));
                    break;
                case Special::Recall:
                    if (!s.exhaust.empty() && s.hand.size() < kMaxHand)
                    {
                        s.hand.push_back(s.exhaust.back());
                        s.exhaust.pop_back();
                    }
                    break;
                case Special::Cleanse:
                    for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
                        if (IsDebuff(Status(i)))
                            s.status[i] = 0;
                    break;
                case Special::EvasionDraw:
                    if (Of(s, Status::Evasion) > 0)
                        DrawCards(side, 1);
                    break;
                default:
                    break;
            }
            if (c.heal)
            {
                BattlePet& p = Me(side);
                p.health = uint16_t(std::min<int>(p.maxHealth, p.health + c.heal));
            }
            s.energy = uint8_t(std::min<int>(s.energy + c.energy, 9));
            if (c.draw)
                DrawCards(side, c.draw);
            if (c.selfHurt)
                Hurt(side, c.selfHurt, false);
        }
        // Where the card goes (its companion's piles, unless that companion is out now).
        if (m_active[side] == mine && !Me(side).beaten)
        {
            if (c.type == CardType::Power)
                ;   // in play for the rest of the duel
            else if (c.exhaust)
                Exhausted(side, id);
            else
                s.discard.push_back(id);
        }
        CheckBeaten();
        if (m_passTurn && m_phase != Phase::Over && m_toPlay == side)
            EndTurnOf(side, true);
    }

    void Battle::CheckBeaten()
    {
        for (int k = 0; k < 2 && m_phase != Phase::Over; ++k)
        {
            int const side = (m_toPlay + k) & 1;
            if (!Me(side).beaten)
                continue;
            if (!m_quiet)
                Say(Me(side).name + " is beaten.");
            auto const& team = m_team[side];
            uint8_t next = uint8_t(team.size());
            for (uint8_t i = 0; i < team.size(); ++i)
                if (!team[i].beaten)
                {
                    next = i;
                    break;
                }
            if (next >= team.size())
            {
                Finish(End::Defeated, side ^ 1);
                return;
            }
            m_active[side] = next;
            Ready(side);
            if (!m_quiet)
                Say(Me(side).name + " steps in.");
            // Beaten on its own turn (thorns, its own price): the turn passes, its new hand kept.
            if (side == m_toPlay && !m_starting)
                m_passTurn = true;
        }
    }

    void Battle::Timeout()
    {
        if (m_phase == Phase::Over)
            return;
        int const side = m_toPlay;
        if (++m_side[side].timeouts >= kMaxTimeouts)
        {
            if (!m_quiet)
                Say(Active(side).name + "'s side ran out of time.");
            Finish(End::Forfeit, side ^ 1);
            return;
        }
        EndTurnOf(side);
    }

    void Battle::Abandon()
    {
        if (m_phase != Phase::Over)
            Finish(End::Abandoned, -1);
    }

    void Battle::Finish(End end, int winner)
    {
        if (m_phase == Phase::Over)
            return;
        m_phase = Phase::Over;
        m_end = end;
        m_winner = winner;
        if (!m_quiet && winner >= 0)
            Say(Active(winner).name + "'s side wins the duel.");
    }

    uint8_t Battle::Defeated(int side) const
    {
        uint8_t n = 0;
        for (BattlePet const& p : m_team[(side & 1) ^ 1])
            if (p.beaten)
                ++n;
        return n;
    }

    void Battle::Plan(int side)
    {
        m_side[side & 1].plan.clear();
        if (m_quiet)
            return;     // a look-ahead copy plans nothing
        m_side[side & 1].plan = PlanTurn(*this, side);
    }

    void Battle::PlayAiTurn()
    {
        if (m_phase == Phase::Over)
            return;
        int const side = m_toPlay;
        uint8_t const mine = m_active[side];
        std::vector<uint16_t> const plan = m_side[side].plan;
        for (uint16_t id : plan)
        {
            if (m_phase == Phase::Over || m_toPlay != side || m_active[side] != mine)
                return;
            SideState const& s = m_side[side];
            for (uint8_t i = 0; i < s.hand.size(); ++i)
                if (s.hand[i] == id && CardById(id).cost <= s.energy)
                {
                    Play(side, i);
                    break;
                }
        }
        if (m_phase != Phase::Over && m_toPlay == side)
            EndTurnOf(side);
    }

    Intent Battle::IntentOf(int side) const
    {
        // Its planned turn played out on a copy, each card's numbers taken as it comes (so a
        // power or a combo early in the turn shows in the hits after it).
        side &= 1;
        Intent in;
        if (m_phase == Phase::Over)
            return in;
        Battle f = Fork();
        if (f.m_toPlay != side)
        {
            f.m_toPlay = side;
            f.StartTurn(side);
        }
        uint8_t const mine = f.m_active[side];
        uint16_t total = 0, per = 0;
        uint8_t attacks = 0, hits = 0;
        for (uint16_t id : m_side[side].plan)
        {
            if (f.m_phase == Phase::Over || f.m_toPlay != side || f.m_active[side] != mine)
                break;
            SideState const& s = f.m_side[side];
            auto it = std::find(s.hand.begin(), s.hand.end(), id);
            Card const& c = CardById(id);
            if (it == s.hand.end() || c.cost > s.energy)
                continue;
            if (c.type == CardType::Attack)
            {
                per = f.CardDamage(side, c);
                hits = std::max<uint8_t>(c.hits, 1);
                total = uint16_t(total + per * hits);
                ++attacks;
            }
            in.block = uint16_t(in.block + f.CardBlock(side, c));
            if (c.type == CardType::Power)
                in.power = true;
            for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
            {
                if (c.enemy[i])
                    in.debuff = true;
                if (c.self[i] && c.type != CardType::Power)
                    in.buff = true;
            }
            f.Play(side, uint8_t(it - s.hand.begin()));
        }
        if (attacks == 1)
            in.damage = per, in.hits = hits;
        else if (attacks > 1)
            in.damage = total, in.hits = 1;
        return in;
    }

    // ── The AI ──────────────────────────────────────────────────────────────────────────────

    namespace
    {
        // What a card is roughly worth to play now (the AI's sense of a hand).
        float Worth(Battle const& b, int side, Card const& c)
        {
            float w = float(b.CardDamage(side, c)) * float(std::max<uint8_t>(c.hits, c.type == CardType::Attack ? 1 : 0));
            w += float(b.CardBlock(side, c)) * 0.75f;
            static constexpr float kEnemy[] = {3.5f, 3.f, 1.5f, 1.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.f, 0, 0};
            static constexpr float kSelf[] = {0, 0, 0, 0, 6.f, 4.f, 3.f, 5.f, 1.5f, 10.f, 5.f, 6.f, 6.f, 10.f, 4.f, 2.f, 0, 14.f, 8.f};
            for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
                w += float(c.enemy[i]) * kEnemy[i] + float(c.self[i]) * kSelf[i];
            w += float(c.heal) * 0.8f + float(c.energy) * 3.f + float(c.draw) * 2.5f - float(c.selfHurt);
            if (c.special == Special::PoisonDouble)
                w += float(std::max<int16_t>(0, b.State(side ^ 1).Get(Status::Poison))) * 1.5f;
            if (c.special == Special::Recall)
                w += b.State(side).exhaust.empty() ? 0.f : 5.f;
            return w;
        }

        float Value(Battle const& b, int side)
        {
            int const t = side ^ 1;
            SideState const& s = b.State(side);
            SideState const& e = b.State(t);
            BattlePet const& me = b.Active(side);
            BattlePet const& them = b.Active(t);
            if (b.GetPhase() == Phase::Over)
                return b.Winner() == side ? 1e6f : -1e6f;
            float v = float(me.health) - float(them.health) * 1.1f;
            v += 150.f * float(b.Defeated(side)) - 150.f * float(b.Defeated(t));
            // Block against what is likely coming (it falls away at the start of the next turn).
            float const threat = 9.f + 2.f * float(std::max<int16_t>(0, e.Get(Status::Strength)))
                - (e.Get(Status::Weakened) > 0 ? 3.f : 0.f);
            float const blk = float(s.block);
            v += std::min(blk, threat) * 0.85f + std::max(0.f, blk - threat) * 0.1f;
            auto on = [&](SideState const& x, Status st) { return float(std::max<int16_t>(0, x.Get(st))); };
            float const left = std::clamp(float(kMaxTurns - b.Turn()) / 2.f, 1.f, 10.f);
            float const grow = std::min(left, 8.f) / 8.f;
            // What sits on the enemy.
            float const p = on(e, Status::Poison);
            v += p * (p + 1.f) / 2.f * 0.7f;
            v += on(e, Status::Bleed) * 1.6f;
            v += std::min(on(e, Status::Exposed), 3.f) * 3.5f + std::min(on(e, Status::Weakened), 3.f) * 3.f;
            v += on(e, Status::Drained) * 5.f;
            v -= on(e, Status::Evasion) * 5.f + on(e, Status::Thorns) * 2.5f;
            // And on itself.
            float const mp = on(s, Status::Poison);
            v -= mp * (mp + 1.f) / 2.f * 0.7f + on(s, Status::Bleed) * 1.6f;
            v -= std::min(on(s, Status::Exposed), 3.f) * 3.f + std::min(on(s, Status::Weakened), 3.f) * 3.f;
            v += on(s, Status::Strength) * 6.f * grow + on(s, Status::Agility) * 4.f * grow;
            v += on(s, Status::Thorns) * 3.f * grow + on(s, Status::Evasion) * 5.f;
            float const r = on(s, Status::Regen);
            v += r * (r + 1.f) / 2.f * 0.6f;
            v += on(s, Status::Ritual) * 10.f * grow + on(s, Status::Fortify) * 4.5f * left / 4.f;
            v += on(s, Status::Plague) * 6.f * grow + on(s, Status::Insight) * 6.f * grow;
            v += on(s, Status::Plating) * 10.f * grow + on(s, Status::Frenzy) * 4.f * grow;
            v += on(s, Status::Undying) * 2.f * grow + on(s, Status::Attunement) * 14.f * grow;
            v += on(s, Status::Bloodlust) * 8.f * grow;
            // What it can still play this turn with the energy it has left (half its worth: the
            // hand is discarded at the end of the turn).
            if (b.ToPlay() == side)
            {
                std::vector<std::pair<float, uint8_t>> cards;
                for (uint16_t id : s.hand)
                {
                    Card const& c = CardById(id);
                    cards.push_back({Worth(b, side, c), c.cost});
                }
                auto rate = [](std::pair<float, uint8_t> const& x) { return x.second ? x.first / float(x.second) : x.first * 10.f; };
                std::sort(cards.begin(), cards.end(), [&](auto const& x, auto const& y) { return rate(x) > rate(y); });
                int energy = s.energy;
                float pot = 0.f;
                for (auto const& [w, cost] : cards)
                    if (cost <= energy && w > 0.f)
                        energy -= cost, pot += w;
                v += pot * 0.5f;
            }
            return v;
        }
    }

    std::vector<uint16_t> PlanTurn(Battle const& b, int side)
    {
        side &= 1;
        std::vector<uint16_t> plan;
        if (b.GetPhase() == Phase::Over)
            return plan;
        // Its next turn as it will start (the block falls away, the ticks, its energy).
        Battle cur = b.Fork();
        cur.m_toPlay = side;
        cur.StartTurn(side);
        uint8_t const mine = cur.m_active[side];
        for (int step = 0; step < 12 && cur.GetPhase() != Phase::Over && cur.m_toPlay == side && cur.m_active[side] == mine; ++step)
        {
            float const base = Value(cur, side);
            float best = 0.25f;
            int pick = -1;
            Battle bestNext;
            SideState const& s = cur.m_side[side];
            for (uint8_t i = 0; i < s.hand.size(); ++i)
            {
                Card const& c = CardById(s.hand[i]);
                if (c.cost > s.energy)
                    continue;
                if (std::find(s.hand.begin(), s.hand.begin() + i, s.hand[i]) != s.hand.begin() + i)
                    continue;   // the same card twice tries the same
                Battle next = cur.Fork();
                next.Play(side, i);
                float const gain = Value(next, side) - base;
                if (gain > best)
                {
                    best = gain;
                    pick = i;
                    bestNext = std::move(next);
                }
            }
            if (pick < 0)
                break;
            plan.push_back(s.hand[uint8_t(pick)]);
            cur = std::move(bestNext);
        }
        return plan;
    }

    // ── Experience ──────────────────────────────────────────────────────────────────────────

    std::vector<uint32_t> XpEarned(Battle const& b, int side, uint32_t factor)
    {
        side &= 1;
        auto const& team = b.Team(side);
        std::vector<uint32_t> out(team.size(), 0);
        if (b.GetPhase() != Phase::Over || b.Winner() != side || !Companions::DuelPaysXp(DuelEndOf(b.GetEnd())))
            return out;
        for (size_t i = 0; i < team.size(); ++i)
        {
            BattlePet const& p = team[i];
            if (!p.fought || p.beaten)
                continue;
            for (BattlePet const& e : b.Team(side ^ 1))
                if (e.beaten)
                    out[i] += Companions::KillXp(p.level, e.level, false) * factor;
        }
        return out;
    }

    uint32_t DailyBonusXp(uint8_t level)
    {
        return Companions::XpToNext(level) / 2;
    }

    uint32_t DailyMoney(uint8_t tamerLevel, uint8_t teamSize)
    {
        return uint32_t(tamerLevel) * 25u * std::max<uint8_t>(teamSize, 1);
    }

    uint8_t PvpLevelCap(std::vector<uint8_t> const& a, std::vector<uint8_t> const& b)
    {
        uint8_t ma = 1, mb = 1;
        for (uint8_t l : a)
            ma = std::max(ma, l);
        for (uint8_t l : b)
            mb = std::max(mb, l);
        return std::min(ma, mb);
    }


    // ── Tamers ──────────────────────────────────────────────────────────────────────────────

    uint8_t StageForLevel(uint8_t level)
    {
        return level < 20 ? 0 : level < 40 ? 1 : 2;
    }

    namespace
    {
        Tamer MakeTamer(uint16_t id, char const* name, uint32_t zone, uint8_t side, uint8_t level, char const* place,
            std::vector<uint16_t> species, bool grand = false)
        {
            Tamer t;
            t.id = id;
            t.entry = kTamerEntryFirst + id - 1;
            t.name = name;
            t.zone = zone;
            t.side = side;
            t.level = level;
            t.grand = grand;
            t.place = place;
            uint8_t slot = 0;
            for (uint16_t s : species)
            {
                TamerPet p;
                p.species = s;
                p.level = level;
                p.stage = StageForLevel(level);
                p.picks = {uint8_t(1 + (id + slot) % 2), uint8_t(1 + (id + slot + 1) % 2), uint8_t(1 + id % 2)};
                t.team.push_back(p);
                ++slot;
            }
            return t;
        }

        uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        std::vector<TamerPet> PickTeam(std::vector<uint16_t> const& pool, uint32_t seed, uint8_t level, uint8_t count)
        {
            std::vector<TamerPet> out;
            uint32_t h = Hash(seed);
            for (uint8_t tries = 0; out.size() < count && tries < 32; ++tries)
            {
                h = Hash(h + tries);
                uint16_t const s = pool[h % pool.size()];
                if (std::any_of(out.begin(), out.end(), [s](TamerPet const& p) { return p.species == s; }))
                    continue;
                TamerPet p;
                p.species = s;
                p.level = std::clamp<uint8_t>(level, 1, Companions::kMaxLevel);
                p.stage = StageForLevel(p.level);
                p.picks = {uint8_t(1 + (h >> 8) % 2), uint8_t(1 + (h >> 9) % 2), uint8_t(1 + (h >> 10) % 2)};
                out.push_back(p);
            }
            return out;
        }
    }

    std::vector<Tamer> const& Tamers()
    {
        static std::vector<Tamer> const kTamers = {
            MakeTamer(1, "Julia Stevens", 12, 1, 5, "Goldshire", {26, 11}),
            MakeTamer(2, "Zunta", 14, 2, 5, "Razor Hill", {115, 111}),
            MakeTamer(3, "Old MacDonald", 40, 1, 13, "Sentinel Hill", {183, 121, 182}),
            MakeTamer(4, "Dagra the Fierce", 17, 2, 14, "the Crossroads", {17, 108, 157}),
            MakeTamer(5, "Lindsay", 44, 1, 19, "Lakeshire", {153, 104, 127}),
            MakeTamer(6, "Analynn", 331, 1, 22, "Astranaar", {135, 158, 5}),
            MakeTamer(7, "Shademaster Kiryn", 406, 2, 22, "Sun Rock Retreat", {159, 137, 28}),
            MakeTamer(8, "Eric Davidson", 10, 1, 25, "Darkshire", {144, 16, 145}),
            MakeTamer(9, "Kela Grimtotem", 400, 0, 30, "Freewind Post", {125, 130, 131}),
            MakeTamer(10, "Merda Stronghoof", 405, 0, 34, "Shadowprey Village", {116, 156, 29}),
            MakeTamer(11, "Steven Lisbane", 33, 0, 36, "Nesingwary's Expedition", {9, 106, 14}),
            MakeTamer(12, "Grazzle the Great", 15, 0, 38, "Brackenwall Village", {114, 134, 117}),
            MakeTamer(13, "Everessa", 8, 0, 38, "Stonard", {139, 13, 185}),
            MakeTamer(14, "Bill Buckler", 33, 0, 42, "Booty Bay", {206, 174, 162}),
            MakeTamer(15, "Traitor Gluk", 357, 0, 44, "Camp Mojache", {120, 160, 127}),
            MakeTamer(16, "David Kosse", 47, 0, 46, "Aerie Peak", {12, 15, 123}),
            MakeTamer(17, "Kortas Darkhammer", 51, 0, 48, "Thorium Point", {21, 19, 154}),
            MakeTamer(18, "Zoltan", 361, 0, 52, "Talonbranch Glade", {141, 136, 142}),
            MakeTamer(19, "Durin Darkhammer", 46, 0, 54, "Morgan's Vigil", {1, 24, 140}),
            MakeTamer(20, "Elena Flutterfly", 493, 0, 55, "Nighthaven", {5, 163, 20}),
            MakeTamer(21, "Deiza Plaguehorn", 139, 0, 57, "Light's Hope Chapel", {146, 147, 148}),
            MakeTamer(22, "Grand Master Lydia Accoste", 41, 0, 60, "Karazhan", {145, 147, 32}, true),
            MakeTamer(23, "Grand Master Trixxy", 618, 0, 60, "Everlook", {8, 22, 105}, true),
        };
        return kTamers;
    }

    Tamer const* FindTamer(std::vector<Tamer> const& all, uint16_t id)
    {
        for (Tamer const& t : all)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    Tamer const* TamerByEntry(std::vector<Tamer> const& all, uint32_t entry)
    {
        for (Tamer const& t : all)
            if (t.entry == entry)
                return &t;
        return nullptr;
    }

    std::vector<Tamer const*> NextTamers(std::vector<Tamer> const& all, uint8_t side, uint8_t level, size_t count)
    {
        std::vector<Tamer const*> out;
        for (Tamer const& t : all)
            if ((t.side == 0 || t.side == side) && int(t.level) + 2 >= int(level))
                out.push_back(&t);
        std::stable_sort(out.begin(), out.end(), [](Tamer const* a, Tamer const* b) { return a->level < b->level; });
        if (out.size() > count)
            out.resize(count);
        return out;
    }

    std::vector<TamerPet> PracticeTeam(uint8_t level, uint32_t seed)
    {
        static std::vector<uint16_t> const kPool = {26, 27, 6, 10, 19, 28, 15, 13, 23, 25, 20, 16};
        return PickTeam(kPool, seed, level, 3);
    }

    bool IsWandererTamer(uint32_t seed)
    {
        return Hash(seed ^ 0x7A3E11u) % 5 == 0;
    }

    std::vector<TamerPet> WandererTeam(uint32_t seed, uint8_t level)
    {
        static std::vector<uint16_t> const kPool = {6, 7, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 25,
            26, 27, 28, 104, 106, 109, 111, 112, 115, 118, 121, 122, 124, 126, 127, 135, 138, 141, 145, 152, 153,
            158, 165, 172, 177, 181, 183, 185};
        uint8_t const count = level < 8 ? 1 : level < 16 ? 2 : 3;
        return PickTeam(kPool, seed, level, count);
    }


    // ── Wire ────────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct Writer
        {
            std::vector<uint8_t> b;
            void u8(uint8_t v) { b.push_back(v); }
            void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
            void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
            void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
            void str8(std::string const& s)
            {
                size_t const n = std::min<size_t>(s.size(), 255);
                u8(uint8_t(n));
                b.insert(b.end(), s.begin(), s.begin() + n);
            }
            void str16(std::string const& s)
            {
                size_t const n = std::min<size_t>(s.size(), 2000);
                u16(uint16_t(n));
                b.insert(b.end(), s.begin(), s.begin() + n);
            }
        };

        uint8_t Cap8(size_t n, size_t cap) { return uint8_t(std::min(n, cap)); }

        void WriteCard(Writer& w, Card const& c, bool playable, uint16_t damage, uint16_t block)
        {
            w.u16(c.id);
            w.u8(uint8_t(c.type));
            w.u8(c.cost);
            w.u8(playable ? 1 : 0);
            w.u16(damage);
            w.u16(block);
            w.u32(c.spell);
            w.str8(c.icon);
            w.str8(c.name);
            w.str16(CardText(c));
        }
    }

    std::vector<uint8_t> EncodeState(Battle const& b, View const& v)
    {
        Writer w;
        int const me = v.side & 1;
        w.u8(kVersion);
        w.u8(kOpState);
        w.u32(v.battle);
        w.u8(uint8_t(b.GetPhase()));
        w.u16(b.Turn());
        w.u8(uint8_t(b.GetEnd()));
        int8_t const winner = b.Winner() < 0 ? -1 : b.Winner() == me ? 0 : 1;
        w.u8(uint8_t(winner));
        w.u8(b.NeedsChoice(me) ? 1 : 0);
        w.u8(uint8_t(std::min<uint32_t>(v.secondsLeft, 255)));
        SideState const& mine = b.State(me);
        w.u8(mine.energy);
        w.u8(uint8_t(kEnergy + std::max<int16_t>(0, mine.Get(Status::Attunement))));
        uint8_t const hand = Cap8(mine.hand.size(), kMaxHand);
        w.u8(hand);
        for (uint8_t i = 0; i < hand; ++i)
        {
            Card const& c = CardById(mine.hand[i]);
            bool const ok = b.Check(me, {ActionKind::Play, i, c.id}) == Refusal::None;
            WriteCard(w, c, ok, b.CardDamage(me, c), b.CardBlock(me, c));
        }
        for (int k = 0; k < 2; ++k)
        {
            int const side = k == 0 ? me : me ^ 1;
            SideState const& s = b.State(side);
            w.str8(v.owners[k]);
            w.u8(b.ActiveIndex(side));
            w.u8(Cap8(s.hand.size(), 255));
            w.u8(Cap8(s.draw.size(), 255));
            w.u8(Cap8(s.discard.size(), 255));
            w.u8(Cap8(s.exhaust.size(), 255));
            w.u16(s.block);
            uint8_t n = 0;
            for (int16_t x : s.status)
                n = uint8_t(n + (x != 0 ? 1 : 0));
            w.u8(n);
            for (uint8_t i = 0; i < uint8_t(Status::Count); ++i)
                if (s.status[i])
                {
                    w.u8(i);
                    w.u16(uint16_t(s.status[i]));
                }
            // An AI's intent; a player's turn is nobody's to know.
            bool const known = b.IsAi(side) && b.GetPhase() != Phase::Over;
            Intent const in = known ? b.IntentOf(side) : Intent{};
            w.u8(known ? 1 : 0);
            w.u16(in.damage);
            w.u8(in.hits);
            w.u16(in.block);
            w.u8(uint8_t((in.buff ? 1 : 0) | (in.debuff ? 2 : 0) | (in.power ? 4 : 0)));
            uint8_t const count = Cap8(b.Team(side).size(), kTeamSize);
            w.u8(count);
            for (uint8_t i = 0; i < count; ++i)
            {
                BattlePet const& p = b.Team(side)[i];
                w.u16(p.species);
                w.u8(uint8_t(p.family));
                w.u8(uint8_t(p.battleFamily));
                w.u8(uint8_t(p.archetype));
                w.u8(p.stage);
                w.u8(p.level);
                w.u16(p.health);
                w.u16(p.maxHealth);
                w.u8(p.beaten ? 1 : 0);
                w.str8(p.name);
            }
        }
        uint8_t const lines = Cap8(v.newLines.size(), 60);
        w.u8(lines);
        for (size_t i = v.newLines.size() - lines; i < v.newLines.size(); ++i)
            w.str16(v.newLines[i]);
        w.str16(v.rewards);
        return w.b;
    }

    std::vector<uint8_t> EncodeChallenged(uint64_t challenger, std::string const& name)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpChallenged);
        w.u64(challenger);
        w.str8(name);
        return w.b;
    }

    std::vector<uint8_t> EncodeTell(std::string const& text)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpTell);
        w.str16(text);
        return w.b;
    }

    std::vector<uint8_t> EncodeDeck(DeckView const& d)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpDeck);
        w.u64(d.item);
        w.str8(d.name);
        w.u8(kMinDeck);
        w.u8(kMaxDeck);
        uint8_t const n = Cap8(d.library.size(), 64);
        w.u8(n);
        for (uint8_t i = 0; i < n; ++i)
        {
            LibraryCard const& l = d.library[i];
            Card const& c = CardById(l.card);
            WriteCard(w, c, true, c.damage, c.block);
            w.u8(l.copies);
            w.u8(l.source);
            w.u8(uint8_t(std::count(d.deck.begin(), d.deck.end(), l.card)));
        }
        w.str16(d.note);
        return w.b;
    }

    std::optional<Request> DecodeRequest(std::vector<uint8_t> const& body)
    {
        if (body.size() < 2 || body[0] != kVersion)
            return std::nullopt;
        Request r;
        r.op = body[1];
        auto u64at = [&body](size_t at) {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= uint64_t(body[at + i]) << (8 * i);
            return v;
        };
        switch (r.op)
        {
            case kOpChallenge:
            case kOpAskDeck:
                if (body.size() != 10)
                    return std::nullopt;
                r.target = u64at(2);
                return r;
            case kOpAnswer:
                if (body.size() != 3)
                    return std::nullopt;
                r.accept = body[2] != 0;
                return r;
            case kOpAction:
            {
                if (body.size() != 6)
                    return std::nullopt;
                uint8_t const k = body[2];
                if (k != uint8_t(ActionKind::Play) && k != uint8_t(ActionKind::EndTurn) && k != uint8_t(ActionKind::Forfeit))
                    return std::nullopt;
                r.action.kind = ActionKind(k);
                r.action.index = body[3];
                r.action.card = uint16_t(body[4] | (body[5] << 8));
                return r;
            }
            case kOpAsk:
                if (body.size() != 2)
                    return std::nullopt;
                return r;
            case kOpSaveDeck:
            {
                if (body.size() < 11)
                    return std::nullopt;
                r.target = u64at(2);
                size_t const n = body[10];
                if (n > kMaxDeck || body.size() != 11 + n * 2)
                    return std::nullopt;
                for (size_t i = 0; i < n; ++i)
                    r.cards.push_back(uint16_t(body[11 + i * 2] | (body[12 + i * 2] << 8)));
                return r;
            }
            default:
                return std::nullopt;
        }
    }

    std::vector<uint8_t> EncodeChallenge(uint64_t target)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpChallenge);
        w.u64(target);
        return w.b;
    }

    std::vector<uint8_t> EncodeAnswer(bool accept)
    {
        return {kVersion, kOpAnswer, uint8_t(accept ? 1 : 0)};
    }

    std::vector<uint8_t> EncodeAction(Action a)
    {
        return {kVersion, kOpAction, uint8_t(a.kind), a.index, uint8_t(a.card), uint8_t(a.card >> 8)};
    }

    std::vector<uint8_t> EncodeAskDeck(uint64_t item)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpAskDeck);
        w.u64(item);
        return w.b;
    }

    std::vector<uint8_t> EncodeSaveDeck(uint64_t item, std::vector<uint16_t> const& cards)
    {
        Writer w;
        w.u8(kVersion);
        w.u8(kOpSaveDeck);
        w.u64(item);
        size_t const n = std::min<size_t>(cards.size(), kMaxDeck);
        w.u8(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
            w.u16(cards[i]);
        return w.b;
    }
}
