/*
 * World of Modern Warfare: guns against classic combat, a numeric model (fusion/Balance.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Balance.h"
#include "fusion/Ammo.h"
#include "fusion/ClassEffects.h"
#include "fusion/ClassTuning.h"
#include "fusion/Classes.h"
#include "fusion/Danger.h"
#include "fusion/Melee.h"
#include "fusion/Scale.h"
#include "fusion/Spellcasting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace Fusion::Balance
{
    namespace SC = Fusion::Spellcasting;

    float TargetPool(uint32_t level, bool pvp)
    {
        if (pvp)
            return Scale::PlayerMaxHealth(level, 0.f);
        Scale::CreatureFacts f;
        f.level = level;
        return Danger::CreatureMaxHealth(f, 0);
    }

    float EqualHealth(uint32_t level)
    {
        return Scale::PlayerMaxHealth(level, 0.f);
    }

    namespace
    {
        // A shotgun's reload_time under a second is a shell's (it loads shell by shell).
        float ReloadCycle(WeaponProfile const& w, uint32_t mag)
        {
            if (w.weaponClass == WeaponClass::Shotgun && w.reloadTime < 1.f)
                return w.reloadTime * float(mag);
            return w.reloadTime;
        }
    }

    GunResult Gun(GunCase const& g, uint32_t level, bool pvp, Assumptions const& a)
    {
        return GunAgainst(g, level, a.poolOverride > 0.f ? a.poolOverride : TargetPool(level, pvp), pvp, a);
    }

    GunResult GunAgainst(GunCase const& g, uint32_t level, float pool, bool pvp, Assumptions const& a)
    {
        GunResult r;
        WeaponProfile const& w = g.weapon;
        float const head = Clamp(a.headShare, 0.f, 1.f);
        float const locMix = (1.f - head) + head * w.LocationMultiplier(HitLocation::Head);
        float pellet = CodDamageAtRange(w, g.range) * locMix * Scale::LevelMultiplier(level)
            * (pvp ? a.coefPvp : a.coefPve) * (g.stoppingPower ? 1.4f : 1.f);
        if (pvp)
            pellet *= 1.f - Clamp(a.pvpArmorCut, 0.f, Scale::kMaxArmorCut);
        float const acc = Clamp(pvp ? a.pvpAccuracy : a.pveAccuracy, 0.05f, 1.f);
        float const pellets = float(std::max<uint8_t>(1, w.pellets));
        float const perRound = pellets * acc * pellet;      // expected, a round fired
        r.hitPct = pellets * pellet / pool * 100.f;
        if (pellets <= 1.f)
            r.roundsPerKill = std::ceil(pool / std::max(0.01f, pellet) - 1e-4f) / acc;
        else
            r.roundsPerKill = std::ceil(pool / std::max(0.01f, perRound) - 1e-4f);
        uint32_t const mag = std::max<uint32_t>(1, uint32_t(float(w.magSize) * g.magScale + 0.5f));
        r.load = mag + w.maxReserve;
        float const reload = ReloadCycle(w, mag);
        float const shots = std::ceil(r.roundsPerKill - 1e-4f);
        r.ttk = std::max(0.f, shots - 1.f) * w.fireTime + std::floor(std::max(0.f, shots - 1.f) / float(mag)) * reload;
        r.burstPct = perRound / std::max(0.01f, w.fireTime) / pool * 100.f;
        r.sustainedPct = float(mag) * perRound / (float(mag) * w.fireTime + reload) / pool * 100.f;
        r.killsPerMag = float(mag) / r.roundsPerKill;
        r.killsPerLoad = float(r.load) / r.roundsPerKill;
        float const mags = std::ceil(float(r.load) / float(mag));
        r.triggerSecondsPerLoad = float(r.load) * w.fireTime + std::max(0.f, mags - 1.f) * reload;
        return r;
    }

    namespace
    {
        // The catalog's ids for each rotation and move (the tuning rows are by option), and each
        // archetype's duel kit: the audit of how it fights a gunner, from CLASS_CATALOG.md and the
        // game's effects (FusionClassAbilities.cpp) at level 60 against a player.
        void FillIdentity(std::vector<Rotation>& v)
        {
            namespace C = Fusion::Classes;
            static char const* const basics[][2] = {{"Priest", "Smite"}, {"Druid", "Wrath"}, {"Warlock", "Shadow Bolt"},
                {"Mage", "Fireball"}, {"Shaman", "Lightning Bolt"}, {"Paladin", "Exorcism"}};
            for (Rotation& r : v)
            {
                for (uint8_t c : {C::kWarrior, C::kPaladin, C::kHunter, C::kRogue, C::kPriest, C::kShaman, C::kMage,
                         C::kWarlock, C::kDruid})
                    if (r.cls == C::ClassName(c))
                        r.clsId = c;
                for (uint8_t a = 0; a < C::kArchetypes; ++a)
                    if (r.archetype == C::ArchetypeName(r.clsId, a))
                        r.archetypeIndex = int8_t(a);
                for (Move& m : r.moves)
                {
                    std::string const name = m.name.substr(0, m.name.find(" ("));
                    // A form's abilities are its option's.
                    std::string const owner = name == "Rake" ? "Cat Form" : name == "Maul" ? "Bear Form" : name;
                    for (C::Option const* o : C::OptionsOf(r.clsId))
                        if (owner == o->name)
                            m.option = o->id;
                    for (auto const& b : basics)
                        if (r.cls == b[0] && name == b[1])
                            m.basic = true;
                    if (name == "Frostbolt")
                        m.chills = true;
                    // Spells that reach 20 yd or more (not the strikes, not the areas about the caster).
                    m.ranged = !m.melee && name != "Consecration" && name != "Hellfire" && name != "Blast Wave"
                        && name != "Corpse Explosion" && name != "Fel Leap";
                    if (name == "Throwing Axes")
                        m.ranged = true;
                }
                r.shatter = r.cls == "Mage" && r.archetype == "Wizard";
            }
            auto kit = [&](char const* cls, char const* arch) -> DuelKit& {
                for (Rotation& r : v)
                    if (r.cls == cls && r.archetype == arch)
                        return r.duel;
                static DuelKit none;
                return none;
            };
            using Ctl = Control;
            // Shared cores.
            Ctl const disarm{1, 3.f, 40.f, false, false};          // Warrior: Disarm, 3 s on a player (no gun)
            Ctl const hammer{1, 3.f, 45.f, false, true};           // Paladin: Hammer of Justice, 20 yd
            Ctl const sheep{1, 6.f, 30.f, true, true, false, 1.5f};    // Mage: Polymorph
            Ctl const fear{1, 6.f, 20.f, true, true, false, 1.5f};     // Warlock: Fear
            auto melee = [](DuelKit& k, float speed) { k.gap = 25.f; k.runSpeed = speed; };
            // Warrior: Charge (30 yd; its root does not stop a gun), Bloodrage's 15% over 6 s / 45 s.
            for (char const* a : {"Berserker", "Vanguard", "Warlord"})
            {
                DuelKit& k = kit("Warrior", a);
                melee(k, 7.f);
                k.closerYards = 30.f;
                k.closerGuard = true;
                k.control.push_back(disarm);
                k.healPerSec = 0.33f;
            }
            kit("Warrior", "Berserker").control.push_back({20, 2.f, 20.f, false, false});     // Intercept's stun
            kit("Warrior", "Berserker").bulletCut = 0.85f;      // Frenzy, Death Wish
            kit("Warrior", "Berserker").healPerSec += 1.6f;     // Bloodthirst's 10% every 6 s
            kit("Warrior", "Vanguard").bulletCut = 0.6f;        // Bulwark up while it closes, the raised shield
            kit("Warrior", "Vanguard").control.push_back({20, 1.f, 20.f, false, true});       // Chain Hook
            kit("Warrior", "Warlord").bulletCut = 0.87f;        // Demoralizing Shout
            kit("Warrior", "Warlord").control.push_back({40, 4.f, 90.f, true, false});        // Intimidating Shout
            kit("Warrior", "Warlord").wardPct = 10.f;           // Rallying Cry
            kit("Warrior", "Warlord").wardCdSec = 60.f;
            kit("Warrior", "Warlord").wardLevel = 20;
            kit("Warrior", "Warlord").healPerSec += 1.f;        // the banner's 1.5%/s, 30 s of 45
            // Paladin: walks in (Blessing of Freedom), Hammer of Justice, Divine Protection.
            for (char const* a : {"Crusader", "Protector", "Lightwarden"})
            {
                DuelKit& k = kit("Paladin", a);
                melee(k, 7.f);
                k.control.push_back(hammer);
                k.bulletCut = 0.95f;
            }
            kit("Paladin", "Crusader").control.push_back({20, 1.5f, 16.f, false, true});      // Judgement at 3 Holy Power
            kit("Paladin", "Crusader").control.push_back({50, 4.f, 45.f, true, true});        // Repentance
            kit("Paladin", "Crusader").bulletCut = 0.88f;       // Avenging Crusade
            kit("Paladin", "Protector").wardPct = 20.f;         // Holy Shield
            kit("Paladin", "Protector").wardCdSec = 15.f;
            kit("Paladin", "Protector").wardLevel = 10;
            kit("Paladin", "Protector").bulletCut = 0.8f;       // Bastion, the raised shield
            kit("Paladin", "Lightwarden").healPerSec = 4.f;     // Flash of Light, Holy Light between blows
            // Hunter: melee hunters walk in; Disengage, Feign Death.
            // Mark Prey: 15% faster toward the prey. A companion at the hunter's side (an adult
            // fighter's share of a hunter pet, Companions.h: about 0.7 %EH a second at 60).
            for (char const* a : {"Scout", "Trapper", "Slayer"})
            {
                melee(kit("Hunter", a), 7.f * 1.15f);
                kit("Hunter", a).petPctPerSec = 0.7f;
                kit("Hunter", a).bulletCut = 0.95f;     // Disengage's 30% for 2 s of 15, the pet drawing fire
            }
            kit("Hunter", "Scout").stealth = true;      // Camouflage: still, it fades and waits
            kit("Hunter", "Trapper").control.push_back(
                {10, float(ClassTuning::kTrapSnapMs) / 1000.f, 15.f, false, true, true});   // the trap's snap (the pass)
            kit("Hunter", "Trapper").control.push_back({10, 4.f, 15.f, true, true});          // Freezing Trap
            kit("Hunter", "Slayer").control.push_back({40, 4.f, 30.f, true, true});           // Scatter
            // Rogue: Stealth (the Assassin and Trickster open from it), Sprint.
            kit("Rogue", "Assassin").stealth = true;
            kit("Rogue", "Assassin").control.push_back({20, 3.f, 20.f, false, false});        // Cheap Shot (from behind)
            kit("Rogue", "Assassin").control.push_back(
                {10, float(ClassTuning::kSunderDisarmMs) / 1000.f, 8.f, false, false, true}); // Ambush's sundering
            kit("Rogue", "Trickster").stealth = true;
            kit("Rogue", "Trickster").control.push_back({20, 3.f, 40.f, false, true});        // Smoke Bomb: sight lost
            kit("Rogue", "Trickster").bulletCut = 0.85f;        // Slip
            melee(kit("Rogue", "Duelist"), 11.9f);              // Sprint
            kit("Rogue", "Duelist").closerYards = 30.f;         // Lunge
            kit("Rogue", "Duelist").closerLevel = 20;
            kit("Rogue", "Duelist").closerGuard = true;
            kit("Rogue", "Duelist").bulletCut = 0.9f;           // Relentless
            // Priest: Power Word: Shield (Weakened Soul: every 15 s), Desperate Prayer.
            for (char const* a : {"Shadow", "Cleric", "Oracle"})
            {
                DuelKit& k = kit("Priest", a);
                k.wardPct = 20.f;
                k.wardCdSec = 15.f;
                k.healPerSec = 0.25f;
            }
            kit("Priest", "Shadow").bulletCut = 0.8f;           // Shadowform
            kit("Priest", "Shadow").drainShare = 0.3f;          // Devouring Plague, Vampiric Embrace
            kit("Priest", "Cleric").healPerSec += 4.f;          // Renew and Flash Heal between Smites
            kit("Priest", "Oracle").wardPct = 50.f;             // Barrier (a wall of half the priest's HP)
            kit("Priest", "Oracle").wardCdSec = 25.f;
            kit("Priest", "Oracle").healPerSec += 1.f;
            // Shaman: Ghost Wolf.
            kit("Shaman", "Elementalist").control.push_back({60, 2.f, 120.f, false, true});   // Earthquake's knock-downs
            melee(kit("Shaman", "Earthbreaker"), 9.8f);       // Ghost Wolf
            kit("Shaman", "Earthbreaker").closingBulletCut = ClassTuning::kGhostWolfBulletCut;
            kit("Shaman", "Earthbreaker").bulletCut = 0.85f;    // Stoneskin
            kit("Shaman", "Earthbreaker").control.push_back({40, 2.f, 20.f, false, false});   // Ground Smash
            kit("Shaman", "Totemist").bulletCut = 0.75f;        // Windwall Totem
            kit("Shaman", "Totemist").healPerSec = 4.f;         // Healing Stream, Chain Heal
            // Mage: Polymorph, Blink.
            for (char const* a : {"Wizard", "Spellsword", "Artificer"})
                kit("Mage", a).control.push_back(sheep);
            melee(kit("Mage", "Spellsword"), 7.f);
            kit("Mage", "Spellsword").gap = 20.f;
            kit("Mage", "Spellsword").closerYards = 20.f;       // Blade Blink
            kit("Mage", "Spellsword").closerLevel = 20;
            kit("Mage", "Spellsword").closerGuard = true;
            kit("Mage", "Spellsword").wardPct = 25.f;           // Mana Shield
            kit("Mage", "Spellsword").wardCdSec = 25.f;
            kit("Mage", "Spellsword").wardLevel = 40;
            kit("Mage", "Artificer").wardPct = 40.f;            // Warding Rune
            kit("Mage", "Artificer").wardCdSec = 20.f;
            kit("Mage", "Artificer").healPerSec = 1.f;          // Healing Glyph
            // Warlock: Fear.
            for (char const* a : {"Afflicter", "Felsworn", "Necromancer"})
                kit("Warlock", a).control.push_back(fear);
            kit("Warlock", "Afflicter").drainShare = 0.3f;      // Drain Life
            kit("Warlock", "Felsworn").bulletCut = 0.72f;       // Fel Hide, Demonic Pact
            kit("Warlock", "Felsworn").control.push_back({40, 3.f, 45.f, false, true});       // Death Coil's horror
            kit("Warlock", "Felsworn").drainShare = 0.1f;
            kit("Warlock", "Necromancer").bulletCut = 0.7f;     // minions draw fire
            kit("Warlock", "Necromancer").wardPct = 25.f;       // Bone Armor
            kit("Warlock", "Necromancer").wardCdSec = 20.f;
            kit("Warlock", "Necromancer").wardLevel = 30;
            // Druid: Barkskin.
            for (char const* a : {"Shapeshifter", "Grovekeeper", "Starcaller"})
                kit("Druid", a).bulletCut = 0.95f;
            kit("Druid", "Shapeshifter").stealth = true;        // Prowl
            kit("Druid", "Shapeshifter").control.push_back({10, 3.f, 600.f, false, false});   // Pounce, once
            kit("Druid", "Shapeshifter").control.push_back({20, 3.f, 45.f, false, false});    // Bash
            kit("Druid", "Shapeshifter").drainShare = 0.15f;    // Worgen's lifesteal, part of the time
            kit("Druid", "Grovekeeper").healPerSec = 4.5f;      // Rejuvenation, Regrowth, Swiftmend
            kit("Druid", "Starcaller").bulletCut = 0.85f;       // Lunar Grace
        }
    }

    std::vector<Rotation> const& Rotations()
    {
        using M = Move;
        // Busy times: the hands' charges (Spellcasting.h): instant 0.3, quick 0.45, medium 0.6,
        // heavy 0.8; a blow 0.3; a channel its length. Basic spells: no mana, 2 s cooldown.
        static std::vector<Rotation> const rotations = [] {
            std::vector<Rotation> v;
            auto add = [&](Rotation r) { v.push_back(std::move(r)); };
            namespace ML = Melee;
            M const smite{"Smite", 1, 15.f, 0, 0, 2.f, 0.6f};
            M const wrath{"Wrath", 1, 14.f, 0, 0, 2.f, 0.6f};
            M const shadowBolt{"Shadow Bolt", 1, 20.f, 0, 0, 2.f, 0.8f};
            M const fireball{"Fireball", 1, 18.f, 1.f, 6.f, 2.f, 0.6f};
            M const lightning{"Lightning Bolt", 1, 15.f, 0, 0, 2.f, 0.45f};
            M exorcism{"Exorcism", 1, 15.f, 0, 0, 15.f, 0.45f};
            exorcism.rankLevel = 20;

            // Warrior
            add({"Warrior", "Berserker", {{"Bloodthirst", 10, 18.f, 0, 0, 6.f, 0.3f, true}, {"Whirlwind", 30, 14.f, 0, 0, 10.f, 0.3f, true}},
                true, ML::Sword2, 0, 0, 1.f, 0, 0, "Frenzy: 25% fewer bullets, Intercept, Death Wish"});
            add({"Warrior", "Vanguard", {{"Shield Charge", 50, 8.f, 0, 0, 25.f, 0.3f, true}},
                true, ML::Sword, 0, 0, 1.f, 0, 0, "Bulwark: frontal bullets stopped; Unbreakable dome"});
            add({"Warrior", "Warlord", {}, true, ML::Mace, 0, 0, 1.f, 0, 0, "Banners (Ward: 15% fewer bullets), shouts"});
            // Paladin
            add({"Paladin", "Crusader", {{"Crusader Strike", 10, 15.f, 0, 0, 5.f, 0.3f, true}, {"Judgement (2 HP)", 20, 16.f, 0, 0, 8.f, 0.3f}, exorcism},
                true, ML::Mace2, 0, 0, 1.f, 0, 0, "Avenging Crusade: 20% fewer bullets"});
            add({"Paladin", "Protector", {{"Consecration", 20, 0.f, 2.f, 8.f, 20.f, 0.3f}, exorcism},
                true, ML::Mace, 0, 0, 1.f, 0, 0, "Holy Shield absorbs bullets; Bastion dome"});
            add({"Paladin", "Lightwarden", {{"Holy Shock", 30, 12.f, 0, 0, 10.f, 0.3f}, exorcism},
                true, ML::Mace, 0, 0, 1.f, 0, 0, "Heals 20-45% HP; Rekindle"});
            // Hunter
            add({"Hunter", "Scout", {}, true, ML::Axe, 0, 0, 1.f, 0, 0, "Spotting, Camouflage: the gunner's support"});
            add({"Hunter", "Trapper", {{"Bear Trap", 40, 0.f, 3.f, 6.f, 15.f, 0.3f}}, true, ML::Axe, 0, 0, 1.f, 0, 0, "Traps and nets: roots, freezes"});
            add({"Hunter", "Slayer", {{"Throwing Axes", 20, 8.f, 0, 0, 6.f, 0.3f, true}}, true, ML::Axe, 0, 0, 1.3f, 10, 0,
                "Bane +30%; Hex Ward; Purge strips shields"});
            // Rogue
            add({"Rogue", "Assassin", {{"Ambush", 10, 25.f, 0, 0, 8.f, 0.3f, true}, {"Garrote", 30, 0.f, 2.f, 18.f, 20.f, 0.3f, true}},
                true, ML::Dagger, 0, 0, 2.f, 10, 4.f, "Stealth, Vanish: wins the first 4 s"});
            add({"Rogue", "Trickster", {{"Deadly Poison (3 stacks)", 10, 0.f, 4.5f, 60.f, 60.f, 0.3f, true}},
                true, ML::Dagger, 0, 0, 1.f, 0, 0, "Slip: untargetable 0.5 s an ability; smoke"});
            add({"Rogue", "Duelist", {{"Sinister Strike", 10, 6.f, 0, 0, 5.f, 0.3f, true}, {"Lunge", 20, 12.f, 0, 0, 12.f, 0.3f, true}},
                true, ML::Sword, 0, 0, 1.25f, 10, 0, "Momentum +25%; Relentless: 15% fewer bullets"});
            // Priest
            add({"Priest", "Shadow", {{"Shadow Word: Pain", 10, 0.f, 1.5f, 18.f, 3.f, 0.3f}, {"Mind Blast", 20, 20.f, 0, 0, 8.f, 0.45f},
                {"Devouring Plague", 40, 0.f, 2.f, 24.f, 60.f, 0.3f}, smite}, false, 0, 0, 0, 1.f, 0, 0, "Shadowform: 30% less of everything"});
            add({"Priest", "Cleric", {smite}, false, 0, 0, 0, 1.f, 0, 0, "Power Word: Shield, heals"});
            add({"Priest", "Oracle", {smite}, false, 0, 0, 0, 1.f, 0, 0, "Barrier wall stops bullets; Premonition"});
            // Shaman
            add({"Shaman", "Elementalist", {lightning, {"Chain Lightning", 20, 12.f, 0, 0, 8.f, 0.6f}, {"Frost Shock", 30, 10.f, 0, 0, 8.f, 0.3f}},
                false, 0, 0, 0, 1.f, 0, 0, "Range and slows; Earthquake knock-downs"});
            add({"Shaman", "Earthbreaker", {{"Stormstrike", 20, 20.f, 0, 0, 10.f, 0.3f, true}, {"Earth Shock", 30, 10.f, 0, 0, 12.f, 0.3f},
                {"Ground Smash", 40, 15.f, 0, 0, 20.f, 0.3f, true}}, true, ML::Mace, 8.f, 10, 1.f, 0, 0, "Stoneskin: 15% fewer bullets"});
            add({"Shaman", "Totemist", {lightning}, false, 0, 0, 0, 1.f, 0, 0, "Totems: ground, heals"});
            // Mage
            add({"Mage", "Wizard", {fireball, {"Frostbolt", 20, 14.f, 0, 0, 2.f, 0.45f}, {"Arcane Missiles", 30, 30.f, 0, 0, 6.f, 3.f},
                {"Blast Wave", 40, 15.f, 0, 0, 30.f, 0.3f}}, false, 0, 0, 0, 1.3f, 20, 0, "Spellweave +30%; Ice Block, Blink"});
            add({"Mage", "Spellsword", {{"Blade Blink", 20, 20.f, 0, 0, 15.f, 0.3f, true}, fireball}, true, ML::Sword, 12.f, 10, 1.f, 0, 0,
                "Mana Shield 25% HP; Arcane Avatar"});
            add({"Mage", "Artificer", {fireball}, false, 0, 0, 0, 1.f, 0, 0, "Warding Rune: a bullet wall"});
            // Warlock
            add({"Warlock", "Afflicter", {{"Corruption", 10, 0.f, 1.5f, 18.f, 3.f, 0.3f}, {"Drain Life", 30, 0.f, 3.f, 5.f, 10.f, 5.f},
                shadowBolt}, false, 0, 0, 0, 1.f, 0, 0, "Drains heal; Curse of Doom"});
            add({"Warlock", "Felsworn", {{"Fel Leap", 20, 10.f, 0, 0, 20.f, 0.3f}, {"Hellfire", 30, 0.f, 3.f, 10.f, 20.f, 0.3f},
                {"Death Coil", 40, 15.f, 0, 0, 45.f, 0.3f}, shadowBolt}, false, 0, 0, 0, 1.f, 0, 0, "Fel Hide: 20% fewer bullets"});
            add({"Warlock", "Necromancer", {{"Corpse Explosion", 20, 20.f, 0, 0, 20.f, 0.3f}, shadowBolt}, false, 0, 0, 0, 1.f, 0, 0,
                "Minions draw fire"});
            // Druid
            add({"Druid", "Shapeshifter", {{"Rake", 10, 10.f, 0, 0, 6.f, 0.3f, true}, {"Maul", 20, 12.f, 0, 0, 6.f, 0.3f, true}},
                true, Melee::kNoWeapon, 0, 0, 1.f, 0, 0, "Forms: Bear's absorb, Cat's stealth"});
            add({"Druid", "Grovekeeper", {wrath}, false, 0, 0, 0, 1.f, 0, 0, "Heals, Tree of Life: 20% less damage"});
            add({"Druid", "Starcaller", {{"Moonfire", 10, 8.f, 1.f, 9.f, 4.f, 0.3f}, {"Lunar Strike", 20, 12.f, 0, 0, 6.f, 0.3f, true},
                {"Hurricane", 40, 0.f, 2.f, 6.f, 60.f, 6.f}, wrath}, false, 0, 0, 0, 1.f, 0, 0, "Range, Hurricane widens spread"});
            // Custom strikes and self-centred areas: no ranged-spell power (Classes.h).
            for (Rotation& r : v)
                for (Move& m : r.moves)
                    for (char const* n : {"Consecration", "Hellfire", "Blast Wave", "Bear Trap", "Fel Leap", "Corpse Explosion",
                             "Deadly Poison (3 stacks)", "Garrote", "Ambush"})
                        if (m.name == n)
                            m.unscaled = true;
            FillIdentity(v);
            return v;
        }();
        return rotations;
    }

    ClassResult Classic(Rotation const& r, uint32_t level, bool pvp, bool deep, Assumptions const& a,
        std::vector<float>* curve)
    {
        ClassResult out;
        float const pool = a.poolOverride > 0.f ? a.poolOverride : TargetPool(level, pvp);
        float const conv = EqualHealth(level) / pool;       // %EH to % of this pool
        float const dt = kStep;
        if (curve)
            curve->clear();
        float chillUntil = -1.f;
        float const horizon = std::max(a.seconds, 5.f);

        // A plain swing, as % of the pool.
        Melee::Profile const prof = Melee::ProfileFor(r.weapon);
        float swingPct = 0.f;
        if (r.swings)
        {
            float const typicalDps = 3.f + 0.9f * float(level);     // a level's weapon: factor 1
            float cod = Melee::Damage(prof, typicalDps, level, r.weapon != Melee::kNoWeapon,
                Scale::LevelMultiplier(level) * a.gear) * (pvp ? a.coefPvp : a.coefPve);
            if (pvp)
                cod *= 1.f - Clamp(a.pvpArmorCut, 0.f, Scale::kMaxArmorCut);
            swingPct = cod / pool * 100.f;
            if (r.swingBonusPct > 0.f && level >= r.swingBonusLevel)
                swingPct += r.swingBonusPct * conv;
        }
        float const swingSec = float(prof.swingMs) / 1000.f;

        std::vector<float> ready(r.moves.size(), 0.f);
        std::vector<float> dotUntil(r.moves.size(), -1.f);
        std::vector<float> dotRate(r.moves.size(), 0.f);
        float busyUntil = 0.f;
        float lastSwing = -100.f;
        Melee::Combo combo;
        float dealt = 0.f;
        bool killed = false;
        float total = 0.f;
        auto signature = [&](float t) {
            if (level < r.signatureLevel || r.signature == 1.f)
                return 1.f;
            return (r.signatureSeconds <= 0.f || t < r.signatureSeconds) ? r.signature : 1.f;
        };
        std::vector<std::pair<float, float>> events;
        auto land = [&](float t, float pct) {
            if (curve)
                events.push_back({t, pct});
            total += pct;
            if (t < 5.f)
                out.burst5 += pct;
            dealt += pct;
            if (!killed && dealt >= 100.f)
            {
                killed = true;
                out.ttk = t;
            }
        };

        for (float t = 0.f; t < horizon || !killed; t += dt)
        {
            if (t > 600.f)
                break;
            // Damage over time ticks.
            for (size_t i = 0; i < r.moves.size(); ++i)
                if (t < dotUntil[i])
                    land(t, dotRate[i] * dt);
            if (t + 1e-4f < busyUntil)
                continue;
            // The best ready ability: the most it deals.
            int best = -1;
            float bestValue = 0.f;
            for (size_t i = 0; i < r.moves.size(); ++i)
            {
                Move const& m = r.moves[i];
                if (level < m.level || t + 1e-4f < ready[i])
                    continue;
                // A damage-over-time still running is not refreshed early.
                if (m.pct <= 0.f && t < dotUntil[i] - 0.5f)
                    continue;
                float const value = m.pct + m.dotPerSec * m.dotSec;
                if (value > bestValue)
                {
                    bestValue = value;
                    best = int(i);
                }
            }
            if (best >= 0)
            {
                Move const& m = r.moves[size_t(best)];
                float busy = m.busySec;
                float power = 1.f;
                // A full charge where the cooldown leaves the time for it (a spell, not a channel)
                // and nothing worth more comes ready meanwhile; a class that swings taps.
                if (deep && !r.swings && !m.melee && busy < 1.f && busy * SC::kFullChargeFactor <= std::max(m.cdSec, busy))
                {
                    float const held = busy * SC::kFullChargeFactor;
                    bool better = false;
                    for (size_t j = 0; j < r.moves.size(); ++j)
                        if (j != size_t(best) && level >= r.moves[j].level && ready[j] < t + held
                            && r.moves[j].pct + r.moves[j].dotPerSec * r.moves[j].dotSec > bestValue)
                            better = true;
                    if (!better)
                    {
                        busy = held;
                        power = SC::kFullChargePower;
                    }
                }
                if (deep && !m.melee && !m.unscaled)
                {
                    power *= a.rangedSpellPower;
                    if (a.tuned && !pvp)
                        power *= ClassTuning::kCreatureSpellPower;
                }
                float const rank = m.rankLevel ? SC::BelowRankScale(level, m.rankLevel) : 1.f;
                float const acc = m.melee ? a.meleeAccuracy : a.spellAccuracy;
                float k = conv * rank * power * acc * signature(t);
                float pct = m.pct;
                if (a.tuned)
                {
                    k *= ClassTuning::DamageScale(m.option);
                    if (m.basic)
                        k *= ClassTuning::BasicSpellScale(r.clsId, r.archetypeIndex);
                    if (r.shatter && t < chillUntil)
                        k *= ClassTuning::kShatterBonus;
                }
                if (m.chills)
                    chillUntil = t + busy + 6.f;
                busyUntil = t + busy;
                ready[size_t(best)] = t + std::max(m.cdSec, busy);
                if (pct > 0.f)
                    land(t + busy, pct * k);
                if (m.dotPerSec > 0.f)
                {
                    dotUntil[size_t(best)] = t + busy + m.dotSec;
                    dotRate[size_t(best)] = m.dotPerSec * k;
                }
                continue;
            }
            if (r.swings && swingPct > 0.f)
            {
                float mult = 1.f;
                if (deep)
                {
                    uint32_t const nowMs = uint32_t(t * 1000.f + 0.5f);
                    if (t - lastSwing > swingSec + float(Melee::kComboWindowMs) / 1000.f)
                        Melee::ComboBreak(combo);
                    mult = Melee::ComboHit(combo, nowMs, prof.swingMs);
                }
                lastSwing = t;
                busyUntil = t + swingSec;
                land(t, swingPct * mult * a.meleeAccuracy * signature(t));
            }
        }
        out.sustainedPct = 0.f;
        // The sustained rate over the window (re-run is not needed: `total` counts all of it up
        // to the horizon or the kill, whichever was later; scale to the window).
        float const span = std::max(horizon, out.ttk);
        out.sustainedPct = total / span;
        if (!killed)
            out.ttk = 600.f;
        if (curve)
        {
            // What has landed by each step (a cast lands when its charge ends).
            std::sort(events.begin(), events.end());
            float const end = std::max(horizon, out.ttk) + 1.f;
            size_t e = 0;
            float sum = 0.f;
            for (float t = 0.f; t < end; t += dt)
            {
                while (e < events.size() && events[e].first <= t + 1e-4f)
                    sum += events[e++].second;
                curve->push_back(sum);
            }
        }
        return out;
    }

    Engagement Fight(GunResult const& gun, ClassResult const& classic, uint32_t targets)
    {
        Engagement e;
        e.targets = targets;
        e.gunOnly = gun.ttk * float(targets);
        e.classicOnly = classic.ttk * float(targets);
        uint32_t const byGun = gun.roundsPerKill > 0.f
            ? std::min<uint32_t>(targets, uint32_t(float(gun.load) / gun.roundsPerKill + 1e-4f)) : 0;
        e.gunKills = byGun;
        e.mixed = gun.ttk * float(byGun) + classic.ttk * float(targets - byGun);
        return e;
    }

    std::vector<GunCase> RepresentativeGuns(WeaponTable const& table, uint32_t level)
    {
        struct Pick { char const* cls; char const* names[3]; float range; bool sp; float mag; };
        static Pick const picks[] = {
            {"AR", {"famas_mp", "fal_mp", "ak47_mp"}, 25.f, true, 1.f},
            {"AR +ext mags", {"famas_mp", "fal_mp", "ak47_mp"}, 25.f, true, 1.5f},
            {"SMG", {"ump45_mp", "p90_mp", "uzi_mp"}, 15.f, true, 1.f},
            {"LMG", {"sa80_mp", "mg4_mp", "m240_mp"}, 30.f, true, 1.f},
            {"Sniper", {"barrett_mp", "wa2000_mp", "m21_mp"}, 60.f, true, 1.f},
            {"Shotgun", {"spas12_mp", "aa12_mp", "m1014_mp"}, 7.f, true, 1.f},
            {"Pistol", {"usp_mp", "coltanaconda_mp", "deserteagle_mp"}, 12.f, true, 1.f},
            {"Machine pistol", {"pp2000_mp", "glock_mp", "tmp_mp"}, 10.f, true, 1.f},
        };
        std::vector<GunCase> out;
        for (Pick const& p : picks)
        {
            WeaponProfile const* chosen = nullptr;
            for (char const* n : p.names)
                if (WeaponProfile const* w = table.Find(std::string(n)); w && UnlockLevel(w->mw2UnlockRank) <= level)
                    chosen = w;
            if (!chosen)
                continue;
            GunCase g;
            g.label = std::string(p.cls) + " (" + chosen->name.substr(0, chosen->name.size() - 3) + ")";
            g.weapon = *chosen;
            g.range = p.range;
            g.stoppingPower = p.sp;
            g.magScale = p.mag;
            out.push_back(g);
        }
        return out;
    }

    namespace
    {
        std::string F(float v, int prec = 1)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", prec, double(v));
            return b;
        }
    }

    std::string Report(WeaponTable const& table, Assumptions const& a)
    {
        std::string o;
        uint32_t const levels[] = {10, 30, 60};
        for (bool pvp : {false, true})
        {
            o += pvp ? "### PvP: an equal-level player\n\n" : "### PvE: a normal creature of the level\n\n";
            for (uint32_t level : levels)
            {
                o += "**Level " + std::to_string(level) + "** (pool " + F(TargetPool(level, pvp), 0) + ")\n\n";
                o += "| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |\n";
                o += "|---|---|---|---|---|---|---|---|---|\n";
                for (GunCase const& g : RepresentativeGuns(table, level))
                {
                    GunResult const r = Gun(g, level, pvp, a);
                    o += "| " + g.label + " | " + F(r.hitPct) + " | " + F(r.roundsPerKill) + " | " + F(r.ttk, 2) + " | "
                        + F(r.burstPct, 0) + " | " + F(r.sustainedPct, 0) + " | " + F(r.killsPerMag) + " | "
                        + F(r.killsPerLoad) + " | " + F(r.triggerSecondsPerLoad) + " |\n";
                }
                o += "\n| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |\n";
                o += "|---|---|---|---|---|---|---|---|\n";
                Assumptions tunedA = a;
                tunedA.tuned = true;
                for (Rotation const& r : Rotations())
                {
                    ClassResult const c = Classic(r, level, pvp, false, a);
                    ClassResult const d = Classic(r, level, pvp, true, a);
                    ClassResult const p = Classic(r, level, pvp, true, tunedA);
                    o += "| " + r.cls + " " + r.archetype + " | " + F(c.sustainedPct) + " | " + F(c.ttk) + " | " + F(c.burst5, 0)
                        + " | " + F(d.sustainedPct) + " | " + F(d.ttk) + " | " + F(p.ttk) + " | " + r.answer + " |\n";
                }
                o += "\n";
            }
        }
        // Engagements without a restock: a quest's 30 normal creatures alone, and a dungeon's 40
        // elite trash in a group of five (each player's share of each). The rifle, against the
        // median archetype (deepened); "gun then class" spends the load, then fights on with
        // the class.
        o += "### Long engagements without restocking (level 60, AK-47 with Stopping Power, median archetype)\n\n";
        o += "| Engagement | Rifle alone, endless ammo (s) | Kills one load covers | Rifle then class (s) | Class alone (s) | Rounds needed |\n";
        o += "|---|---|---|---|---|---|\n";
        {
            std::vector<GunCase> const guns = RepresentativeGuns(table, 60);
            struct E { char const* name; uint32_t targets; float poolShare; CreatureRank rank; Scale::Place place; };
            E const es[] = {{"30 normal creatures, solo", 30, 1.f, CreatureRank::Normal, Scale::Place::World},
                {"40 dungeon elites, a fifth each", 40, 0.2f, CreatureRank::Elite, Scale::Place::Dungeon}};
            for (E const& e : es)
            {
                if (guns.empty())
                    break;
                Scale::CreatureFacts f;
                f.level = 60;
                f.rank = e.rank;
                f.place = e.place;
                Assumptions b = a;
                b.poolOverride = Danger::CreatureMaxHealth(f, e.place == Scale::Place::World ? 0u : 36u) * e.poolShare;
                GunResult const rifle = Gun(guns.front(), 60, false, b);
                std::vector<float> mixed, alone;
                for (Rotation const& r : Rotations())
                {
                    Engagement const en = Fight(rifle, Classic(r, 60, false, true, b), e.targets);
                    mixed.push_back(en.mixed);
                    alone.push_back(en.classicOnly);
                }
                std::sort(mixed.begin(), mixed.end());
                std::sort(alone.begin(), alone.end());
                o += std::string("| ") + e.name + " | " + F(rifle.ttk * float(e.targets)) + " | " + F(rifle.killsPerLoad)
                    + " | " + F(mixed[mixed.size() / 2]) + " | " + F(alone[alone.size() / 2]) + " | "
                    + F(rifle.roundsPerKill * float(e.targets), 0) + " |\n";
            }
        }
        o += "\n";
        // What a load is worth against the harder things (level 60, the rifle).
        {
            std::vector<GunCase> const guns = RepresentativeGuns(table, 60);
            if (!guns.empty())
            {
                o += "### One rifle load at level 60 against harder targets\n\n| Target | Pool | Rounds/kill | Kills/load |\n|---|---|---|---|\n";
                struct T { char const* name; CreatureRank rank; Scale::Place place; bool boss; };
                T const ts[] = {{"Normal (open world)", CreatureRank::Normal, Scale::Place::World, false},
                    {"Elite (open world)", CreatureRank::Elite, Scale::Place::World, false},
                    {"Elite (dungeon trash)", CreatureRank::Elite, Scale::Place::Dungeon, false},
                    {"Dungeon boss", CreatureRank::Elite, Scale::Place::Dungeon, true}};
                for (T const& t : ts)
                {
                    Scale::CreatureFacts f;
                    f.level = 60;
                    f.rank = t.rank;
                    f.place = t.place;
                    f.boss = t.boss;
                    float const pool = Danger::CreatureMaxHealth(f, t.place == Scale::Place::World ? 0u : 36u);
                    GunResult const r = GunAgainst(guns.front(), 60, pool, false, a);
                    o += std::string("| ") + t.name + " | " + F(pool, 0) + " | " + F(r.roundsPerKill) + " | " + F(r.killsPerLoad, 2) + " |\n";
                }
                o += "\n";
            }
        }
        // How a load comes back (Ammo.h, the limited model): a kit station refills it free.
        AmmoRules const& rules = CurrentAmmoRules();
        o += "### Restocking a load (limited model)\n\nA trainer or innkeeper refills every load free; the trip is the cost.\n\n"
             "| Level | Rifle load (rounds) | Field floor (rounds) | A gunner's body (rounds) | With Scavenger |\n|---|---|---|---|---|\n";
        for (uint32_t level : levels)
        {
            std::vector<GunCase> const guns = RepresentativeGuns(table, level);
            if (guns.empty())
                continue;
            GunResult const rifle = Gun(guns.front(), level, false, a);
            float const mag = float(guns.front().weapon.magSize);
            o += "| " + std::to_string(level) + " | " + std::to_string(rifle.load) + " | "
                + F(mag * rules.fieldFloorMags, 0) + " | "
                + F(mag * BodyScavengeMags(rules.gunnerScavengeMags, false, rules), 0) + " | "
                + F(mag * BodyScavengeMags(rules.gunnerScavengeMags, true, rules), 0) + " |\n";
        }
        o += "\n";
        return o;
    }

    DuelResult Duel(Rotation const& r, GunCase const& gun, uint32_t level, Assumptions const& a)
    {
        DuelResult out;
        DuelKit const& k = r.duel;
        bool const tuned = a.tuned;
        // The class's damage on the gunner from the moment it is in reach (its own deepened
        // rotation; the tuned rows when the pass is on).
        std::vector<float> curve;
        ClassResult const c = Classic(r, level, true, true, a, &curve);
        out.classTtk = c.ttk;
        // ... and what reaches while it closes: its spells of 20 yd and more, no swings.
        std::vector<float> farCurve;
        if (k.gap > 0.f)
        {
            Rotation far = r;
            far.swings = false;
            far.moves.clear();
            for (Move const& m : r.moves)
                if (m.ranged)
                    far.moves.push_back(m);
            if (!far.moves.empty())
                Classic(far, level, true, true, a, &farCurve);
        }
        float const pet = k.petPctPerSec
            * (tuned && ClassTuning::HunterBond(r.clsId) ? ClassTuning::kHunterBondDamage * ClassTuning::kBondPreyBonus : 1.f);
        auto at = [](std::vector<float> const& v, float sec) {
            if (v.empty() || sec < 0.f)
                return 0.f;
            return v[std::min(v.size() - 1, size_t(sec / kStep + 0.5f))];
        };
        float farAtReach = 0.f;
        GunResult const g = Gun(gun, level, true, a);
        float const gunRate = g.sustainedPct;      // % of the class's pool a second while firing
        bool const healerLead = ClassTuning::IsHealerArchetype(r.clsId, r.archetypeIndex);

        float classHp = 100.f, gunnerHp = 100.f;
        float gap = k.stealth ? 0.f : (k.gap > 0.f && a.duelGap > 0.f ? a.duelGap : k.gap);
        bool closerUsed = false;
        float gunnerHeldUntil = k.stealth ? 0.6f : 0.3f;   // its reaction
        float guardUntil = -1.f;
        float ward = 0.f, wardReady = 0.f;
        std::vector<float> ctlReady(k.control.size(), 0.f);
        float inReach = -1.f;           // when the class came into reach
        float reachAt = -1.f;           // a gap-closer in flight lands then
        float drUntil = -1.f, drScale = 1.f;
        struct Pending { float at; float held; };
        std::vector<Pending> pending;   // controls being cast
        float dealtBefore = 0.f;
        for (float t = 0.f; t < 60.f; t += kStep)
        {
            // The class's ward on its cooldown.
            if (k.wardPct > 0.f && level >= k.wardLevel && t >= wardReady)
            {
                ward = k.wardPct;
                wardReady = t + std::max(k.wardCdSec, 1.f);
            }
            // Closing in.
            if (gap > 0.f)
            {
                if (reachAt >= 0.f)
                {
                    if (t >= reachAt)
                    {
                        gap = 0.f;
                        gunnerHeldUntil = std::max(gunnerHeldUntil, t + k.closerStun);
                    }
                }
                else if (k.closerYards > 0.f && level >= k.closerLevel && !closerUsed && gap <= k.closerYards)
                {
                    closerUsed = true;
                    reachAt = t + kCloserTravelSec;
                    if (tuned && k.closerGuard)
                        guardUntil = t + float(ClassTuning::kGapGuardMs) / 1000.f;
                }
                else
                    gap -= k.runSpeed * kStep;
            }
            bool const reach = gap <= 0.f;
            if (reach && inReach < 0.f)
                inReach = t;
            // A control whose cast ends now lands (diminishing returns on it).
            for (size_t i = 0; i < pending.size();)
            {
                if (t + 1e-4f < pending[i].at)
                {
                    ++i;
                    continue;
                }
                drScale = t < drUntil ? drScale * 0.5f : 1.f;
                drUntil = t + kControlDrSec;
                gunnerHeldUntil = std::max(gunnerHeldUntil, t + pending[i].held * drScale);
                pending.erase(pending.begin() + std::ptrdiff_t(i));
            }
            // Control on its cooldown: the first ready one, when the gunner is free and nothing is
            // being cast.
            if (t >= gunnerHeldUntil && pending.empty())
                for (size_t i = 0; i < k.control.size(); ++i)
                {
                    Control const& ctl = k.control[i];
                    if (level < ctl.level || t < ctlReady[i] || (ctl.tunedOnly && !tuned) || (!ctl.ranged && !reach) || gap > 25.f)
                        continue;
                    float const held = ctl.breaksOnDamage ? ctl.seconds * 0.5f : ctl.seconds;
                    pending.push_back({t + ctl.castSec, held});
                    ctlReady[i] = t + ctl.cdSec;
                    break;
                }
            // The gun.
            if (t >= gunnerHeldUntil)
            {
                float hit = gunRate * kStep * k.bulletCut;
                if (tuned && gap > 0.f)
                    hit *= k.closingBulletCut;
                if (t < guardUntil)
                    hit *= ClassTuning::kGapGuardBulletCut;
                float const soaked = Fusion::Classes::WardAbsorb(ward, hit, true);
                classHp -= hit - soaked;
            }
            // The class's heals and damage.
            classHp = std::min(100.f, classHp + k.healPerSec * kStep);
            {
                if (!reach)
                    farAtReach = at(farCurve, t);
                float const dealt = (reach ? farAtReach + at(curve, t - inReach) : farAtReach)
                    + pet * std::max(0.f, t - 0.5f);
                float const fresh = std::max(0.f, dealt - dealtBefore);
                dealtBefore = dealt;
                gunnerHp = 100.f - dealt;
                float back = fresh * k.drainShare;
                // Atonement: a healer's basic spell heals its caster (most of a healer's damage is
                // its basic spell).
                if (tuned)
                    back += ClassTuning::AtonementHeal(fresh, healerLead);
                classHp = std::min(100.f, classHp + back);
            }
            if (gunnerHp <= 0.f || classHp <= 0.f)
            {
                out.classWins = gunnerHp <= 0.f && classHp > 0.f;
                out.seconds = t;
                break;
            }
            out.seconds = t;
        }
        out.classLeft = std::max(0.f, classHp);
        out.gunnerLeft = std::max(0.f, gunnerHp);
        return out;
    }

    std::string DuelReport(WeaponTable const& table, uint32_t level)
    {
        std::vector<GunCase> const guns = RepresentativeGuns(table, level);
        if (guns.empty())
            return {};
        GunCase const& rifle = guns.front();
        Assumptions before;
        Assumptions after;
        after.tuned = true;
        Assumptions farBefore = before, farAfter = after;
        farBefore.duelGap = farAfter.duelGap = 40.f;
        GunResult const g = Gun(rifle, level, true, before);
        std::string o = "### Duels against an equal gunner (level " + std::to_string(level) + ", " + rifle.label
            + "; the gunner kills a player who never fights back in " + F(g.ttk, 2) + " s)\n\n";
        o += "| Archetype | Class TTK before (s) | Class TTK after (s) | 25 yd before | 25 yd after | 40 yd before | 40 yd after |\n"
             "|---|---|---|---|---|---|---|\n";
        auto cell = [](DuelResult const& d) {
            std::string s = d.classWins ? "**wins** " : "loses ";
            s += F(d.seconds, 1) + " s, ";
            s += d.classWins ? F(d.classLeft, 0) + "% left" : "gunner at " + F(d.gunnerLeft, 0) + "%";
            return s;
        };
        for (Rotation const& r : Rotations())
        {
            DuelResult const b = Duel(r, rifle, level, before);
            DuelResult const t = Duel(r, rifle, level, after);
            DuelResult const fb = Duel(r, rifle, level, farBefore);
            DuelResult const ft = Duel(r, rifle, level, farAfter);
            o += "| " + r.cls + " " + r.archetype + " | " + F(b.classTtk) + " | " + F(t.classTtk) + " | " + cell(b) + " | "
                + cell(t) + " | " + cell(fb) + " | " + cell(ft) + " |\n";
        }
        o += "\n";
        return o;
    }
}
