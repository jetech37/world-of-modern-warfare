/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

namespace Fusion::Companions
{
    namespace
    {
        using K = AbilityKind;
        using T = AbilityAim;

        constexpr Ability A(uint32_t spell, char const* name, K kind, T aim, uint16_t pct = 0, uint16_t dot = 0,
            uint8_t cd = 0, uint8_t dur = 0)
        {
            Ability a;
            a.spell = spell;
            a.name = name;
            a.kind = kind;
            a.aim = aim;
            a.pct = pct;
            a.dotPct = dot;
            a.cooldown = cd;
            a.duration = dur;
            return a;
        }

        // Abilities several families share (design/COMPANION_CATALOG.md section 2).
        constexpr Ability kGrowl = A(2649, "Growl", K::Taunt, T::Enemy, 0, 0, 8, 3);
        constexpr Ability kTorment = A(3716, "Torment", K::Taunt, T::Enemy, 0, 0, 8, 3);
        constexpr Ability kDash = A(23099, "Dash", K::GapCloser, T::Self, 0, 0, 30, 8);
        constexpr Ability kDive = A(23145, "Dive", K::GapCloser, T::Self, 0, 0, 30, 15);
        constexpr Ability kScreech = A(24423, "Screech", K::AoE, T::Around, 60, 0, 15, 4);
        constexpr Ability kTerrifyingScreech = A(6605, "Terrifying Screech", K::Fear, T::Around, 0, 0, 45, 4);
        constexpr Ability kSonicBurst = A(8281, "Sonic Burst", K::Interrupt, T::Around, 0, 0, 30, 3);
        constexpr Ability kFatalBite = A(7938, "Fatal Bite", K::Burst, T::Enemy, 200, 0, 20);
        constexpr Ability kThrash = A(3391, "Thrash", K::Damage, T::Enemy, 0, 0, 12);
        constexpr Ability kInfectedBite = A(7367, "Infected Bite", K::Debuff, T::Enemy, 0, 50, 15, 15);
        constexpr Ability kTendonRip = A(3604, "Tendon Rip", K::Root, T::Enemy, 0, 0, 20, 8);
        constexpr Ability kFuriousHowl = A(24604, "Furious Howl", K::Buff, T::Around, 0, 0, 20);
        constexpr Ability kPiercingHowl = A(23600, "Piercing Howl", K::Root, T::Around, 0, 0, 30, 6);
        constexpr Ability kCharge = A(22911, "Charge", K::GapCloser, T::Enemy, 100, 0, 20, 2);
        constexpr Ability kGore = A(4102, "Gore", K::Debuff, T::Enemy, 0, 120, 30, 15);
        constexpr Ability kKnockdown = A(5164, "Knockdown", K::Stun, T::Enemy, 0, 0, 15, 2);
        constexpr Ability kKnockdownGolem = A(5951, "Knockdown", K::Stun, T::Enemy, 0, 0, 15, 2);
        constexpr Ability kMuscleTear = A(12166, "Muscle Tear", K::Damage, T::Enemy, 150, 0, 20, 5);
        constexpr Ability kHardenSkin = A(22693, "Harden Skin", K::Defensive, T::Self, 0, 0, 45, 10);
        constexpr Ability kShellShield = A(26064, "Shell Shield", K::Defensive, T::Self, 0, 0, 120, 12);
        constexpr Ability kHeadButt = A(6730, "Head Butt", K::Stun, T::Enemy, 0, 0, 20, 2);
        constexpr Ability kKnockAway = A(10101, "Knock Away", K::Damage, T::Enemy, 100, 0, 20);
        constexpr Ability kDemoRoar = A(15971, "Demoralizing Roar", K::Debuff, T::Around, 0, 0, 30, 20);
        constexpr Ability kWarStomp = A(11876, "War Stomp", K::AoEStun, T::Around, 50, 0, 30, 2);
        constexpr Ability kEnrage = A(8599, "Enrage", K::Buff, T::Self, 0, 0, 60, 12);
        constexpr Ability kTrample = A(5568, "Trample", K::AoE, T::Around, 100, 0, 15);
        constexpr Ability kSunder = A(11971, "Sunder Armor", K::Debuff, T::Enemy, 0, 0, 6, 30);
        constexpr Ability kPierceArmor = A(6016, "Pierce Armor", K::Debuff, T::Enemy, 0, 0, 45, 20);
        constexpr Ability kVenomSpit = A(6917, "Venom Spit", K::Damage, T::Enemy, 80, 60, 12, 10);
        constexpr Ability kWeb = A(4167, "Web", K::Root, T::Enemy, 0, 0, 20, 4);
        constexpr Ability kPoison = A(744, "Poison", K::Debuff, T::Enemy, 0, 60, 8, 12);
        constexpr Ability kRake = A(1822, "Rake", K::Damage, T::Enemy, 50, 90, 10, 9);
        constexpr Ability kSwoop = A(5708, "Swoop", K::AoEStun, T::Around, 50, 0, 30, 2);
        constexpr Ability kRabies = A(3150, "Rabies", K::Debuff, T::Enemy, 0, 0, 30, 60);
        constexpr Ability kWildRegen = A(7948, "Wild Regeneration", K::Heal, T::Owner, 0, 12, 30, 20);
        constexpr Ability kRoarFortitude = A(4238, "Roar of Fortitude", K::Buff, T::Around, 0, 0, 180, 120);
        constexpr Ability kFirebolt = A(3110, "Firebolt", K::Damage, T::Enemy, 150, 0, 8);
        constexpr Ability kFireShield = A(2947, "Fire Shield", K::Buff, T::Self, 0, 0, 10, 180);
        constexpr Ability kFlameBuffet = A(9574, "Flame Buffet", K::Damage, T::Enemy, 120, 0, 10, 20);
        constexpr Ability kFireNova = A(11969, "Fire Nova", K::AoE, T::Around, 100, 0, 15);
        constexpr Ability kFireballVolley = A(15285, "Fireball Volley", K::AoE, T::Around, 100, 0, 20);
        constexpr Ability kRenew = A(8362, "Renew", K::Heal, T::Owner, 0, 10, 20, 20);
        constexpr Ability kSlow = A(246, "Slow", K::Root, T::Enemy, 0, 0, 20, 10);
        constexpr Ability kFrostbolt = A(9672, "Frostbolt", K::Damage, T::Enemy, 120, 0, 6, 4);
        constexpr Ability kFrostNova = A(11831, "Frost Nova", K::Root, T::Around, 0, 0, 25, 4);
        constexpr Ability kHealingWave = A(11986, "Healing Wave", K::Heal, T::Owner, 15, 0, 30);
        constexpr Ability kIceBlast = A(11264, "Ice Blast", K::AoE, T::Around, 80, 0, 30, 4);
        constexpr Ability kHealingStream = A(5394, "Healing Stream Totem", K::Heal, T::Around, 0, 30, 60, 30);
        constexpr Ability kRockSkin = A(8314, "Rock Skin", K::Defensive, T::Self, 0, 0, 120, 60);
        constexpr Ability kGroundSmash = A(12734, "Ground Smash", K::AoEStun, T::Around, 120, 0, 30, 2);
        constexpr Ability kLightningBolt = A(9532, "Lightning Bolt", K::Damage, T::Enemy, 120, 0, 6);
        constexpr Ability kGustOfWind = A(6982, "Gust of Wind", K::AoEStun, T::Around, 0, 0, 45, 2);
        constexpr Ability kLightningCloud = A(6535, "Lightning Cloud", K::AoE, T::Ground, 0, 200, 40, 15);
        constexpr Ability kChainLightning = A(12058, "Chain Lightning", K::Damage, T::Enemy, 120, 0, 10);
        constexpr Ability kEntanglingRoots = A(12747, "Entangling Roots", K::Root, T::Enemy, 0, 40, 20, 5);
        constexpr Ability kAcidSplash = A(6306, "Acid Splash", K::AoE, T::Around, 0, 60, 20);
        constexpr Ability kCorrosiveAcid = A(8245, "Corrosive Acid", K::Debuff, T::Around, 0, 0, 30, 30);
        constexpr Ability kAcidBreath = A(12533, "Acid Breath", K::AoE, T::Around, 60, 40, 20);
        constexpr Ability kDiseaseCloud = A(12187, "Disease Cloud", K::AoE, T::Self, 0, 0, 60);
        constexpr Ability kCleave = A(15496, "Cleave", K::AoE, T::Enemy, 110, 0, 6);
        constexpr Ability kMortalStrike = A(13737, "Mortal Strike", K::Burst, T::Enemy, 150, 0, 10, 5);
        constexpr Ability kShieldBash = A(11972, "Shield Bash", K::Interrupt, T::Enemy, 50, 0, 12, 4);
        constexpr Ability kDisarm = A(6713, "Disarm", K::Debuff, T::Enemy, 0, 0, 45, 5);
        constexpr Ability kWhirlwind = A(15589, "Whirlwind", K::AoE, T::Around, 100, 0, 12);
        constexpr Ability kSweepingSlam = A(12887, "Sweeping Slam", K::AoE, T::Around, 120, 0, 20);
        constexpr Ability kElectrifiedNet = A(11820, "Electrified Net", K::Root, T::Enemy, 0, 0, 30, 5);
        constexpr Ability kSnapKick = A(8646, "Snap Kick", K::Stun, T::Enemy, 0, 0, 15, 1);
        constexpr Ability kRushingCharge = A(6268, "Rushing Charge", K::GapCloser, T::Enemy, 0, 0, 15, 1);
        constexpr Ability kThrow = A(10277, "Throw", K::Damage, T::Enemy, 120, 0, 8);
        constexpr Ability kHamstring = A(9080, "Hamstring", K::Root, T::Enemy, 0, 0, 10, 10);
        constexpr Ability kBattleShout = A(9128, "Battle Shout", K::Buff, T::Around, 0, 0, 120, 120);
        constexpr Ability kBloodlust = A(6742, "Bloodlust", K::Buff, T::Owner, 0, 0, 60, 15);
        constexpr Ability kEarthbind = A(15786, "Earthbind Totem", K::Root, T::Around, 0, 0, 45, 20);
        constexpr Ability kBomb = A(9143, "Bomb", K::AoE, T::Ground, 150, 0, 20);
        constexpr Ability kRejuvenation = A(12160, "Rejuvenation", K::Heal, T::Owner, 0, 10, 15, 12);
        constexpr Ability kThorns = A(782, "Thorns", K::Buff, T::Owner, 0, 0, 30, 0);
        constexpr Ability kKick = A(11978, "Kick", K::Interrupt, T::Enemy, 0, 0, 12, 4);
        constexpr Ability kRend = A(13443, "Rend", K::Debuff, T::Enemy, 0, 75, 12, 15);
        constexpr Ability kInfectedWound = A(3427, "Infected Wound", K::Debuff, T::Enemy, 0, 0, 20, 20);
        constexpr Ability kTailSweep = A(15847, "Tail Sweep", K::AoE, T::Around, 120, 0, 25);
        constexpr Ability kSkitterWeb = A(12023, "Skitter Web", K::Mounted, T::Around, 0, 0, 30, 3);
        constexpr Ability kSpellLock = A(19244, "Spell Lock", K::Interrupt, T::Enemy, 0, 0, 24, 3);
        constexpr Ability kSkySurge = A(130, "Launch", K::Mounted, T::Self, 0, 0, kSkySurgeCooldown);
        static_assert(kSkySurgeSpell == 130, "Launch's icon spell");
        constexpr Ability kWingBuffet = A(18500, "Wing Buffet", K::AoE, T::Around, 60, 0, 25);
        constexpr Ability kFlameBreath = A(9573, "Flame Breath", K::AoE, T::Around, 180, 0, 20);
        constexpr Ability kNone{};

        using Stages = std::array<std::array<Ability, 2>, kMaxStages>;

        MovesetInfo M(Moveset id, char const* name, uint32_t basic, char const* trait, Stages stages, Ability mounted = kNone)
        {
            MovesetInfo m;
            m.id = id;
            m.name = name;
            m.basicSpell = basic;
            m.trait = trait;
            m.stages = stages;
            m.mounted = mounted;
            return m;
        }

        std::vector<MovesetInfo> Build()
        {
            std::vector<MovesetInfo> v(static_cast<size_t>(Moveset::Count));
            auto put = [&v](MovesetInfo m) { v[size_t(m.id)] = m; };
            put(M(Moveset::Critter, "Critter", 0, "Small and quick: hard to hit", {{{kDash, kGrowl}, {kThrash, kHamstring}, {kKnockdown, kEnrage}}}));
            put(M(Moveset::Spider, "Spider", 17253, "Silk-spinner: its first hit on a new target roots it 1.5 s, once per 20 s",
                {{{kWeb, kPoison}, {kVenomSpit, kInfectedBite},
                  {A(745, "Web Volley", K::Root, T::Around, 0, 0, 30, 4), kFatalBite}}}, kSkitterWeb));
            put(M(Moveset::Bear, "Bear", 16827, "Thick fur: -10% physical damage taken",
                {{{kGrowl, A(12161, "Maul", K::Damage, T::Enemy, 60, 0, 6)},
                  {A(9754, "Swipe", K::AoE, T::Around, 100, 0, 8), kDemoRoar},
                  {A(5211, "Bash", K::Stun, T::Enemy, 0, 0, 30, 3), kEnrage}}},
                A(5568, "Overrun", K::Mounted, T::Around, 80, 0, 20, 1)));
            put(M(Moveset::Cat, "Cat", 16827, "Cat's grace: +10% dodge, +10% run speed",
                {{{kRake, A(24450, "Prowl", K::Buff, T::Self, 0, 0, 10)},
                  {A(9005, "Pounce", K::GapCloser, T::Enemy, 0, 0, 25, 2), kDash},
                  {A(3242, "Ravage", K::Burst, T::Enemy, 250, 0, 30, 2), A(19451, "Frenzy", K::Buff, T::Self, 0, 0, 30, 8)}}},
                A(23110, "Pounce Leap", K::Mounted, T::Self, 0, 0, 20, 3)));
            put(M(Moveset::Wolf, "Wolf", 17253, "Pack hunter: +5% damage per other ally hitting its target (max +15%)",
                {{{kFuriousHowl, kTendonRip}, {A(23109, "Dash", K::GapCloser, T::Self, 0, 0, 30, 8), kPiercingHowl},
                  {kCharge, A(7481, "Howling Rage", K::Buff, T::Around, 0, 0, 60, 15)}}},
                A(23110, "Run Them Down", K::Mounted, T::Self, 0, 0, 25, 3)));
            put(M(Moveset::Raptor, "Raptor", 17253, "Ripping claws: crits add Rend (30% BH over 15 s)",
                {{{kThrash, kRend}, {kInfectedWound, A(3258, "Savage Rage", K::Buff, T::Self, 0, 0, 20, 4)},
                  {A(3446, "Ravage", K::Burst, T::Enemy, 250, 0, 40, 2), kTerrifyingScreech}}},
                A(3242, "Leaping Rake", K::Mounted, T::Enemy, 150, 0, 20, 1)));
            put(M(Moveset::Scorpid, "Scorpid", 16827, "Chitin: -10% physical damage, immune to poison",
                {{{A(24640, "Scorpid Poison", K::Debuff, T::Enemy, 0, 20, 4, 10), A(7357, "Poisonous Stab", K::Debuff, T::Enemy, 0, 0, 30, 15)},
                  {kKnockdown, kVenomSpit},
                  {kMuscleTear, A(24583, "Venom Burst", K::Burst, T::Enemy, 60, 0, 20)}}},
                A(15847, "Tail Sweep", K::Mounted, T::Around, 120, 0, 25)));
            put(M(Moveset::Crocolisk, "Crocolisk", 17253, "Ambusher: +20% damage on its first attack after standing still or swimming 3 s",
                {{{kGrowl, kGore}, {kKnockdown, kMuscleTear},
                  {A(11650, "Death Roll", K::Stun, T::Enemy, 150, 0, 40, 3), kHardenSkin}}},
                A(23364, "Tail Lash", K::Mounted, T::Around, 0, 0, 25, 2)));
            put(M(Moveset::Turtle, "Turtle", 17253, "Shell: -15% damage from the front",
                {{{kGrowl, kShellShield}, {kHeadButt, kHardenSkin},
                  {A(10101, "Shell Bash", K::Damage, T::Enemy, 100, 0, 20), A(17735, "Snapping Challenge", K::Taunt, T::Around, 0, 0, 60, 3)}}},
                A(5568, "Shell Ram", K::Mounted, T::Around, 80, 0, 45)));
            put(M(Moveset::Gorilla, "Gorilla", 16827, "Silverback: +10% max health; its owner within 10 yd +5% armor",
                {{{A(26090, "Thunderstomp", K::AoE, T::Around, 60, 0, 20), kGrowl}, {kKnockAway, kDemoRoar}, {kWarStomp, kEnrage}}}));
            put(M(Moveset::Boar, "Boar", 17253, "Bristleback: +10% run speed out of combat; attackers take 5% of its BH",
                {{{A(3385, "Boar Charge", K::GapCloser, T::Self, 50, 0, 15, 4), kGrowl}, {kRushingCharge, kGore}, {kTrample, kEnrage}}},
                A(3385, "Boar Charge", K::Mounted, T::Self, 50, 0, 15, 4)));
            put(M(Moveset::Crab, "Crab", 16827, "Carapace: +15% armor",
                {{{kGrowl, kPierceArmor}, {kKnockdown, kHardenSkin}, {kShellShield, kSunder}}}));
            put(M(Moveset::WindSerpent, "Wind Serpent", 24844, "Storm-scaled: +5% spell crit, +10% nature resistance",
                {{{A(25008, "Lightning Breath", K::Damage, T::Enemy, 150, 0, 8), kDive}, {kLightningCloud, kGustOfWind},
                  {kChainLightning, A(18500, "Wing Buffet", K::AoE, T::Around, 60, 0, 25)}}}));
            put(M(Moveset::CarrionBird, "Carrion Bird", 16827, "Carrion feeder: heals 5% when an enemy within 20 yd dies",
                {{{kScreech, kDive}, {kSwoop, A(11019, "Wing Flap", K::AoE, T::Around, 0, 0, 20)},
                  {kTerrifyingScreech, A(18500, "Wing Buffet", K::AoE, T::Around, 60, 0, 25)}}}));
            put(M(Moveset::Roc, "Roc", 16827, "Carrion feeder: heals 5% when an enemy within 20 yd dies",
                {{{kScreech, kDive}, {kSwoop, A(11019, "Wing Flap", K::AoE, T::Around, 0, 0, 20)}, {kFlameBuffet, kTerrifyingScreech}}}));
            put(M(Moveset::Hyena, "Hyena", 17253, "Scavenger's laugh: +10% damage to targets below 35%",
                {{{kTendonRip, kFuriousHowl}, {kRabies, kInfectedBite}, {kFatalBite, kThrash}}}));
            put(M(Moveset::Owl, "Owl", 16827, "Night eyes: sees stealth within 15 yd; +10% damage at night",
                {{{kScreech, kDive}, {kSonicBurst, kRake}, {kTerrifyingScreech, kSwoop}}}));
            put(M(Moveset::Bat, "Bat", 17253, "Echolocation: immune to blind; sees stealth within 10 yd",
                {{{kScreech, kDive}, {kSonicBurst, kFatalBite}, {kTerrifyingScreech, A(19451, "Blood Frenzy", K::Buff, T::Self, 0, 0, 30, 8)}}}));
            put(M(Moveset::Tallstrider, "Tallstrider", 8646, "Long stride: its owner +5% run speed while it is out",
                {{{kSnapKick, kDash}, {kWildRegen, A(6507, "Battle Roar", K::Buff, T::Around, 0, 0, 60, 60)}, {kRoarFortitude, kThrash}}},
                A(23110, "Stride", K::Mounted, T::Self, 0, 0, 45, 6)));
            put(M(Moveset::Kodo, "Kodo", 0, "Herd bond: party within 15 yd +5% armor",
                {{{kGrowl, kHeadButt}, {A(6266, "Kodo Stomp", K::AoEStun, T::Around, 0, 0, 30, 2), kRoarFortitude},
                  {A(19482, "War Stomp", K::AoEStun, T::Around, 100, 0, 30, 2), kTrample}}},
                A(6266, "Kodo Stomp", K::Mounted, T::Around, 0, 0, 30, 2)));
            put(M(Moveset::Basilisk, "Basilisk", 17253, "Crystalline hide: +20% armor, -10% run speed",
                {{{A(3635, "Crystal Gaze", K::Stun, T::Enemy, 0, 0, 45, 3), A(5810, "Stone Skin", K::Defensive, T::Self, 0, 0, 30, 6)},
                  {A(11020, "Petrify", K::Stun, T::Enemy, 0, 0, 40, 4), A(3636, "Crystalline Slumber", K::Stun, T::Enemy, 0, 0, 45, 8)},
                  {A(5106, "Crystal Flash", K::AoEStun, T::Around, 0, 0, 45, 3), kRockSkin}}},
                A(5106, "Crystal Flash", K::Mounted, T::Around, 0, 0, 45, 3)));
            put(M(Moveset::Threshadon, "Threshadon", 17253, "Amphibious: +40% swim speed; +20% damage in water",
                {{{kGrowl, kGore}, {kThrash, kKnockdown}, {kTailSweep, kFatalBite}}}));
            put(M(Moveset::Hydra, "Hydra", 9591, "Many heads: the basic attack hits a second enemy for 50%",
                {{{A(15653, "Acid Spit", K::Damage, T::Enemy, 150, 0, 8), A(3129, "Frost Breath", K::AoE, T::Around, 80, 0, 12)},
                  {A(3132, "Chilling Breath", K::Debuff, T::Enemy, 0, 160, 30, 12), kAcidBreath},
                  {A(16359, "Corrosive Acid Breath", K::AoE, T::Around, 150, 0, 30), kFireballVolley}}}));
            put(M(Moveset::Dragonkin, "Dragonkin", 3110, "Draconic: +10% all resistances",
                {{{kFirebolt, kFireShield}, {kFlameBuffet, A(5010, "Scorch Breath", K::AoE, T::Around, 0, 200, 60, 10)},
                  {A(9573, "Flame Breath", K::AoE, T::Around, 180, 0, 20), A(18431, "Bellowing Roar", K::Fear, T::Around, 0, 0, 60, 3)}}},
                A(9573, "Flame Breath", K::Mounted, T::Around, 180, 0, 20)));
            put(M(Moveset::FaerieDragon, "Faerie Dragon", 24844, "Blink: dodges the first ranged attack every 10 s",
                {{{kRenew, kSlow}, {kSonicBurst, A(15253, "Arcane Explosion", K::AoE, T::Around, 80, 0, 15)}, {kFrostNova, kRejuvenation}}}));
            put(M(Moveset::FireElemental, "Fire Elemental", 3110, "Burning core: attackers take 10% of its BH as fire",
                {{{kFireShield, kFirebolt}, {kFireNova, kFlameBuffet},
                  {kFireballVolley, A(348, "Immolate", K::Damage, T::Enemy, 60, 120, 10, 15)}}}));
            put(M(Moveset::WaterElemental, "Water Elemental", 9672, "Tidal: its owner +1% health per 5 s while it is out of combat",
                {{{kFrostbolt, kRenew}, {kFrostNova, kHealingWave}, {kIceBlast, kHealingStream}}}));
            put(M(Moveset::EarthElemental, "Earth Elemental", 0, "Stoneform: immune to bleed, -10% physical damage",
                {{{kGrowl, kRockSkin}, {kKnockAway, A(9483, "Boulder", K::Damage, T::Enemy, 150, 0, 12)}, {kGroundSmash, kHardenSkin}}}));
            put(M(Moveset::AirElemental, "Air Elemental", 9532, "Windborne: immune to snares",
                {{{kLightningBolt, kGustOfWind}, {kLightningCloud, kSlow},
                  {kChainLightning, A(12550, "Lightning Shield", K::Buff, T::Self, 0, 0, 30)}}}));
            put(M(Moveset::BogBeast, "Bog Beast", 6607, "Rooted: cannot be knocked back",
                {{{kEntanglingRoots, kGrowl}, {A(8142, "Grasping Vines", K::Root, T::Around, 0, 0, 40, 4), kThorns},
                  {A(7948, "Wild Regeneration", K::Heal, T::Self, 0, 25, 60, 20), A(6607, "Lash", K::Stun, T::Enemy, 0, 0, 20, 2)}}}));
            put(M(Moveset::PoisonElemental, "Poison Elemental", 7951, "Toxic: immune to poison; its damage over time lasts 20% longer",
                {{{A(21067, "Poison Bolt", K::Damage, T::Enemy, 100, 40, 8), kAcidSplash}, {kVenomSpit, kCorrosiveAcid},
                  {A(3256, "Plague Cloud", K::Debuff, T::Ground, 0, 0, 60, 30), kAcidBreath}}}));
            put(M(Moveset::Ooze, "Ooze", 0, "Amorphous: immune to bleeds and roots, -20% run speed",
                {{{A(9459, "Corrosive Ooze", K::Debuff, T::Enemy, 0, 0, 20, 20), A(9591, "Acid Spit", K::Damage, T::Enemy, 100, 0, 8)},
                  {kCorrosiveAcid, kDiseaseCloud},
                  {kAcidSplash, A(7279, "Black Sludge", K::Debuff, T::Around, 0, 0, 45, 15)}}}));
            put(M(Moveset::Infernal, "Infernal", 0, "Infernal: immune to fire; attackers take 10% of its BH as fire",
                {{{kTorment, A(11350, "Fire Shield", K::AoE, T::Self, 0, 100, 30, 15)}, {kWarStomp, kFireNova},
                  {A(8285, "Rampage", K::AoEStun, T::Around, 100, 0, 25, 2), kEnrage}}}));
            put(M(Moveset::Imp, "Imp", 3110, "Phase-born: phased out of combat until it acts",
                {{{kFireShield, A(4511, "Phase Shift", K::Defensive, T::Self, 0, 0, 10)},
                  {A(9053, "Fireball", K::Damage, T::Enemy, 160, 0, 8), kFireNova},
                  {kFireballVolley, A(4436, "Immolation", K::Buff, T::Self, 0, 0, 30)}}}));
            put(M(Moveset::Felhound, "Felhound", 17253, "Mana hunter: +20% damage to targets with mana",
                {{{A(19505, "Devour Magic", K::Dispel, T::Enemy, 0, 0, 8), kTendonRip},
                  {kSpellLock, A(25779, "Mana Burn", K::Damage, T::Enemy, 0, 0, 15)},
                  {A(17012, "Devour Magic", K::Dispel, T::Enemy, 0, 0, 8), A(5484, "Howl of Terror", K::Fear, T::Around, 0, 0, 40, 3)}}},
                A(19244, "Spell Lock", K::Mounted, T::Enemy, 0, 0, 24, 3)));
            put(M(Moveset::Felguard, "Felguard", 0, "Demonic armor: +10% armor, +1% health per 5 s",
                {{{kTorment, kSunder}, {kCleave, A(19134, "Intimidating Shout", K::Fear, T::Enemy, 0, 0, 60, 4)}, {kWarStomp, kMortalStrike}}}));
            put(M(Moveset::Darkhound, "Darkhound", 17253, "Plaguefang: 15% chance on hit to apply Infected Bite",
                {{{kTendonRip, kFatalBite}, {kInfectedBite, kPiercingHowl}, {kCharge, kRabies}}}));
            put(M(Moveset::Skeleton, "Skeleton", 0, "Bones: immune to bleed and fear",
                {{{kShieldBash, kSunder}, {A(17014, "Bone Shards", K::Buff, T::Self, 0, 0, 30, 10), kDisarm},
                  {A(11445, "Bone Armor", K::Defensive, T::Self, 0, 0, 60, 30), kMortalStrike}}}));
            put(M(Moveset::Ghoul, "Ghoul", 16827, "Unliving: immune to fear and poison",
                {{{A(12541, "Ghoul Rot", K::Debuff, T::Enemy, 0, 0, 20, 20), kTorment},
                  {A(16458, "Ghoul Plague", K::Debuff, T::Enemy, 0, 0, 30, 15), kInfectedBite}, {kDiseaseCloud, kCleave}}}));
            put(M(Moveset::Banshee, "Banshee", 9613, "Incorporeal: -15% physical damage taken",
                {{{A(5884, "Banshee Curse", K::Debuff, T::Enemy, 0, 0, 15, 12), A(11639, "Shadow Word: Pain", K::Debuff, T::Enemy, 0, 120, 10, 18)},
                  {A(17173, "Drain Life", K::Damage, T::Enemy, 0, 100, 15, 5), A(6726, "Silence", K::Interrupt, T::Enemy, 0, 0, 30, 4)},
                  {A(13704, "Psychic Scream", K::Fear, T::Around, 0, 0, 45, 3), A(16565, "Banshee Wail", K::Burst, T::Enemy, 200, 0, 15)}}}));
            put(M(Moveset::CryptFiend, "Crypt Fiend", 16827, "Carapace: -10% physical damage",
                {{{A(745, "Web", K::Root, T::Enemy, 0, 0, 20, 5), kPoison}, {kInfectedBite, A(4167, "Web", K::Root, T::Enemy, 0, 0, 25, 4)},
                  {kFatalBite, kVenomSpit}}},
                kSkitterWeb));
            put(M(Moveset::HarvestGolem, "Harvest Golem", 0, "Machine: immune to poison, bleed and fear; repairs out of combat",
                {{{kGrowl, kKnockdownGolem}, {kWhirlwind, kCleave}, {kSweepingSlam, kHardenSkin}}}));
            put(M(Moveset::Shredder, "Shredder", 0, "Machine: immune to poison, bleed and fear; repairs out of combat",
                {{{kGrowl, kSunder}, {kWhirlwind, kCleave}, {kSweepingSlam, kElectrifiedNet}}},
                A(15589, "Saw", K::Mounted, T::Around, 100, 0, 15)));
            put(M(Moveset::SpiderTank, "Spider Tank", 6660, "Machine: immune to poison, bleed and fear; repairs out of combat",
                {{{A(10733, "Flame Spray", K::AoE, T::Around, 0, 150, 15, 3), kElectrifiedNet},
                  {A(15575, "Flame Cannon", K::Damage, T::Enemy, 160, 0, 10), kFrostNova}, {kIceBlast, kBomb}}},
                A(10733, "Flame Spray", K::Mounted, T::Around, 0, 150, 15, 3)));
            put(M(Moveset::Mechanostrider, "Mechanostrider", 8646, "Machine: immune to poison, bleed and fear",
                {{{kSnapKick, kDash}, {kElectrifiedNet, kRushingCharge}, {kKnockdownGolem, kBomb}}},
                A(23110, "Overdrive", K::Mounted, T::Self, 0, 0, 45, 6)));
            put(M(Moveset::Horse, "Horse", 0, "Steadfast: +10% run speed",
                {{{kCharge, kDash}, {kTrample, kHeadButt}, {kWarStomp, kBattleShout}}}, A(5568, "Trample", K::Mounted, T::Around, 100, 0, 20)));
            put(M(Moveset::SkeletalHorse, "Skeletal Horse", 0, "Unliving: immune to fear and poison",
                {{{kCharge, kDash}, {kTrample, kHeadButt}, {kDiseaseCloud, kTerrifyingScreech}}},
                A(16458, "Ghoul Plague", K::Mounted, T::Around, 0, 0, 30, 15)));
            put(M(Moveset::Ram, "Ram", 6730, "Ram-headed: immune to knockback",
                {{{kCharge, kDash}, {kTrample, kHeadButt}, {kKnockAway, kDemoRoar}}}, A(6730, "Head Butt", K::Mounted, T::Enemy, 0, 0, 20, 2)));
            put(M(Moveset::Gnoll, "Gnoll", 0, "Pack cur: +10% damage when another ally is near its target",
                {{{kHamstring, kThrow}, {kPierceArmor, kThrash}, {kBattleShout, kCharge}}}));
            put(M(Moveset::Kobold, "Kobold", 0, "Tunnel rat: -20% threat; +10% loot money",
                {{{kThrow, kKick}, {A(8800, "Dynamite", K::AoE, T::Ground, 150, 0, 20), kDisarm}, {kEarthbind, kBomb}}}));
            put(M(Moveset::Trogg, "Trogg", 0, "Thick-headed: -25% stun duration",
                {{{kKnockdownGolem, kTorment}, {A(8078, "Thunderclap", K::AoE, T::Around, 60, 0, 15, 10), kDisarm}, {kGroundSmash, kEnrage}}}));
            put(M(Moveset::Ogre, "Ogre", 0, "Ogre-size: +10% health, immune to knockback",
                {{{kKnockAway, A(13730, "Demoralizing Shout", K::Debuff, T::Around, 0, 0, 30, 30)}, {kCleave, kBloodlust}, {kWarStomp, kMortalStrike}}}));
            put(M(Moveset::Centaur, "Centaur", 6660, "Four legs: +15% run speed",
                {{{A(17174, "Concussive Shot", K::Root, T::Enemy, 0, 0, 12, 4), kThrow},
                  {A(14443, "Multi-Shot", K::AoE, T::Enemy, 100, 0, 10), kTrample}, {kWarStomp, kCharge}}}));
            put(M(Moveset::Quilboar, "Quilboar", 0, "Bristles: attackers take 5% of its BH",
                {{{kBattleShout, kThorns}, {kHealingWave, A(13281, "Earth Shock", K::Interrupt, T::Enemy, 120, 0, 12)}, {kEarthbind, kBloodlust}}}));
            put(M(Moveset::Furbolg, "Furbolg", 0, "Ursine: +10% health",
                {{{kRejuvenation, kBattleShout}, {kHealingWave, kEntanglingRoots},
                  {kHealingStream, A(4987, "Cleanse", K::Dispel, T::Owner, 0, 0, 10)}}}));
            put(M(Moveset::Harpy, "Harpy", 16827, "Winged: falls slowly",
                {{{kLightningBolt, kSlow}, {A(12548, "Frost Shock", K::Damage, T::Enemy, 120, 0, 12, 8), kChainLightning},
                  {kTerrifyingScreech, kLightningCloud}}}));
            put(M(Moveset::Troll, "Troll", 10277, "Regeneration: +1% health per 3 s in combat",
                {{{kHamstring, A(22887, "Throw", K::Damage, T::Enemy, 180, 0, 10)},
                  {A(16707, "Hex", K::Stun, T::Enemy, 0, 0, 40, 5), kBloodlust}, {kWhirlwind, kEnrage}}}));
            put(M(Moveset::Naga, "Naga", 0, "Amphibious: +40% swim speed; +20% damage in water",
                {{{kSunder, kFrostbolt}, {kFrostNova, kShieldBash}, {kCleave, kChainLightning}}}));
            put(M(Moveset::Makrura, "Makrura", 16827, "Shell: -15% damage from the front",
                {{{kGrowl, kPierceArmor}, {kKnockdown, kHardenSkin}, {kShellShield, kSweepingSlam}}}));
            put(M(Moveset::Moonkin, "Moonkin", 0, "Moonlit: +10% damage at night",
                {{{A(15798, "Moonfire", K::Damage, T::Enemy, 80, 60, 8, 12), A(9739, "Wrath", K::Damage, T::Enemy, 140, 0, 6)},
                  {kRejuvenation, kEntanglingRoots},
                  {A(15588, "Thunderclap", K::AoE, T::Around, 60, 0, 15), A(2637, "Hibernate", K::Stun, T::Enemy, 0, 0, 45, 10)}}}));
            put(M(Moveset::Worgen, "Worgen", 16827, "Moonrage: +15% attack speed below 35% health",
                {{{kRend, kHamstring}, {kInfectedWound, A(12021, "Fixate", K::Taunt, T::Enemy, 0, 0, 45, 6)},
                  {kEnrage, A(7399, "Terrify", K::Fear, T::Enemy, 0, 0, 40, 4)}}}));
            put(M(Moveset::Murloc, "Murloc", 0, "Mrrgl: +10% swim speed; +5% damage per other murloc within 20 yd",
                {{{A(12024, "Net", K::Root, T::Enemy, 0, 0, 25, 5), kThrow}, {kHealingWave, kFrostNova}, {kBloodlust, kBattleShout}}}));
            // The flying mounts (design/COMPANION_CATALOG.md section 1g): ridden, each leaps.
            put(M(Moveset::Gryphon, "Gryphon", 16827, "Aerie-bred: +10% damage to a target its owner is fighting",
                {{{kScreech, kDive}, {kSwoop, kRend},
                  {A(8078, "Thunderclap", K::AoE, T::Around, 60, 0, 15, 10), kWingBuffet}}},
                kSkySurge));
            put(M(Moveset::WindRider, "Wind Rider", 17253, "Stinger: 10% chance on hit to poison (20% BH over 10 s)",
                {{{kDive, kPoison}, {kVenomSpit, kSwoop},
                  {A(24583, "Venom Burst", K::Burst, T::Enemy, 60, 0, 20), kTerrifyingScreech}}},
                kSkySurge));
            put(M(Moveset::Hippogryph, "Hippogryph", 16827, "Moon-blessed: +10% healing taken",
                {{{kScreech, kDive}, {kSwoop, kRejuvenation}, {kWingBuffet, kEntanglingRoots}}},
                kSkySurge));
            put(M(Moveset::Dragonhawk, "Dragonhawk", 3110, "Sunfire: +10% fire damage",
                {{{kFirebolt, kDive}, {kFlameBuffet, kFireShield}, {kFlameBreath, kSwoop}}},
                kSkySurge));
            put(M(Moveset::Drake, "Drake", 3110, "Draconic: +10% all resistances",
                {{{kFirebolt, kFireShield}, {kFlameBuffet, A(5010, "Scorch Breath", K::AoE, T::Around, 0, 200, 60, 10)},
                  {kFlameBreath, A(18431, "Bellowing Roar", K::Fear, T::Around, 0, 0, 60, 3)}}},
                kSkySurge));
            put(M(Moveset::Phoenix, "Phoenix", 3110, "Burning plumage: attackers take 10% of its BH as fire",
                {{{kFirebolt, kFireShield}, {kFireNova, kFlameBuffet},
                  {kFireballVolley, A(348, "Immolate", K::Damage, T::Enemy, 60, 120, 10, 15)}}},
                kSkySurge));
            return v;
        }
    }

    MovesetInfo const& MovesetData(Moveset m)
    {
        static std::vector<MovesetInfo> const all = Build();
        static MovesetInfo const none;
        size_t const i = size_t(m);
        return i < all.size() && all[i].id == m ? all[i] : none;
    }

    Moveset MovesetOf(CompanionSpecies const& sp)
    {
        switch (sp.id)
        {
            case 125: return Moveset::Roc;
            case 137: return Moveset::EarthElemental;     // the stone golems fight as earth
            case 149: return Moveset::HarvestGolem;
            case 150: return Moveset::Shredder;
            case 151: return Moveset::SpiderTank;
            case 28: return Moveset::Critter;             // the clockwork squirrel
            case 182: case 187: case 188: case 189: case 190:
                return Moveset::Critter;                  // the vanilla clockwork pets
            case 219: case 220: case 221: case 223:
                return Moveset::Drake;                    // the ridden drakes: they leap
            case 222: return Moveset::Phoenix;
            default: break;
        }
        switch (sp.family)
        {
            case Family::Critter: return Moveset::Critter;
            case Family::Dragon: return Moveset::Dragonkin;
            case Family::FaerieDragon: return Moveset::FaerieDragon;
            case Family::Wolf: return Moveset::Wolf;
            case Family::Cat: return Moveset::Cat;
            case Family::Bear: return Moveset::Bear;
            case Family::Spider: return Moveset::Spider;
            case Family::Raptor: return Moveset::Raptor;
            case Family::Crocolisk: return Moveset::Crocolisk;
            case Family::Gorilla: return Moveset::Gorilla;
            case Family::Owl: return Moveset::Owl;
            case Family::Bat: return Moveset::Bat;
            case Family::Tallstrider: return Moveset::Tallstrider;
            case Family::Scorpid: return Moveset::Scorpid;
            case Family::FireElemental: return Moveset::FireElemental;
            case Family::WaterElemental: return Moveset::WaterElemental;
            case Family::EarthElemental: return Moveset::EarthElemental;
            case Family::AirElemental: return Moveset::AirElemental;
            case Family::Murloc: return Moveset::Murloc;
            case Family::Infernal: return Moveset::Infernal;
            case Family::Ooze: return Moveset::Ooze;
            case Family::Mechanical: return Moveset::HarvestGolem;
            case Family::Kodo: return Moveset::Kodo;
            case Family::Horse: return Moveset::Horse;
            case Family::Mechanostrider: return Moveset::Mechanostrider;
            case Family::SkeletalHorse: return Moveset::SkeletalHorse;
            case Family::Ram: return Moveset::Ram;
            case Family::Hyena: return Moveset::Hyena;
            case Family::Boar: return Moveset::Boar;
            case Family::Crab: return Moveset::Crab;
            case Family::Turtle: return Moveset::Turtle;
            case Family::WindSerpent: return Moveset::WindSerpent;
            case Family::CarrionBird: return Moveset::CarrionBird;
            case Family::Basilisk: return Moveset::Basilisk;
            case Family::Threshadon: return Moveset::Threshadon;
            case Family::Hydra: return Moveset::Hydra;
            case Family::BogBeast: return Moveset::BogBeast;
            case Family::Imp: return Moveset::Imp;
            case Family::Felhound: return Moveset::Felhound;
            case Family::Felguard: return Moveset::Felguard;
            case Family::Skeleton: return Moveset::Skeleton;
            case Family::Ghoul: return Moveset::Ghoul;
            case Family::Banshee: return Moveset::Banshee;
            case Family::Gnoll: return Moveset::Gnoll;
            case Family::Kobold: return Moveset::Kobold;
            case Family::Trogg: return Moveset::Trogg;
            case Family::Ogre: return Moveset::Ogre;
            case Family::Centaur: return Moveset::Centaur;
            case Family::Quilboar: return Moveset::Quilboar;
            case Family::Furbolg: return Moveset::Furbolg;
            case Family::Harpy: return Moveset::Harpy;
            case Family::Troll: return Moveset::Troll;
            case Family::Naga: return Moveset::Naga;
            case Family::Makrura: return Moveset::Makrura;
            case Family::Moonkin: return Moveset::Moonkin;
            case Family::Worgen: return Moveset::Worgen;
            case Family::PoisonElemental: return Moveset::PoisonElemental;
            case Family::Darkhound: return Moveset::Darkhound;
            case Family::CryptFiend: return Moveset::CryptFiend;
            case Family::Gryphon: return Moveset::Gryphon;
            case Family::WindRider: return Moveset::WindRider;
            case Family::Hippogryph: return Moveset::Hippogryph;
            case Family::Dragonhawk: return Moveset::Dragonhawk;
        }
        return Moveset::None;
    }

    bool IsSkySurge(Ability const& a) { return a.kind == AbilityKind::Mounted && a.spell == kSkySurgeSpell; }

    // ── Picks ───────────────────────────────────────────────────────────────────────────────

    int PendingPick(CompanionSpecies const& sp, CompanionState const& s)
    {
        MovesetInfo const& m = MovesetData(MovesetOf(sp));
        for (uint8_t st = 0; st <= s.stage && st < sp.stageCount && st < kMaxStages; ++st)
            if (!s.picks[st] && m.stages[st][0].Exists())
                return st;
        return -1;
    }

    bool Pick(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t option)
    {
        if (stage >= kMaxStages || stage > s.stage || stage >= sp.stageCount || s.picks[stage] || option < 1 || option > 2)
            return false;
        if (!MovesetData(MovesetOf(sp)).stages[stage][option - 1].Exists())
            return false;
        s.picks[stage] = option;
        return true;
    }

    Ability AbilityInSlot(CompanionSpecies const& sp, CompanionState const& s, uint8_t slot)
    {
        MovesetInfo const& m = MovesetData(MovesetOf(sp));
        if (slot >= PetBarSlot::Ability1 && slot <= PetBarSlot::Ability3)
        {
            uint8_t const st = uint8_t(slot - PetBarSlot::Ability1);
            if (st > s.stage || st >= sp.stageCount || !s.picks[st] || s.picks[st] > 2)
                return Ability{};
            return m.stages[st][s.picks[st] - 1];
        }
        if (slot == PetBarSlot::Twin && HasTrait(s.traits, Trait::TwinSouled) && s.twinStage >= 1 && s.twinStage <= kMaxStages)
        {
            uint8_t const st = uint8_t(s.twinStage - 1);
            if (st > s.stage || st >= sp.stageCount || !s.picks[st] || s.picks[st] > 2)
                return Ability{};
            return m.stages[st][2 - s.picks[st]];   // the option not picked
        }
        return Ability{};
    }

    std::vector<Ability> ActiveAbilities(CompanionSpecies const& sp, CompanionState const& s)
    {
        std::vector<Ability> out;
        for (uint8_t slot : {PetBarSlot::Ability1, PetBarSlot::Ability2, PetBarSlot::Ability3, PetBarSlot::Twin})
        {
            Ability const a = AbilityInSlot(sp, s, slot);
            if (a.Exists())
                out.push_back(a);
        }
        return out;
    }

    Ability MountedMove(CompanionSpecies const& sp, CompanionState const& s)
    {
        if (s.stage >= sp.stageCount || !sp.stages[s.stage].rideable)
            return Ability{};
        return MovesetData(MovesetOf(sp)).mounted;
    }

    // ── The basic attack ────────────────────────────────────────────────────────────────────

    bool IsRangedBasic(uint32_t spell)
    {
        switch (spell)
        {
            case 3110:      // Firebolt: whelps, drakes, fire elementals, imps
            case 24844:     // Lightning Breath: wind serpents, faerie dragons' arcane spit
            case 9672:      // Frostbolt: water elementals
            case 9532:      // Lightning Bolt: air elementals
            case 9591:      // Acid Spit: hydras
            case 7951:      // Toxic Spit: poison elementals
            case 9613:      // Shadow Bolt: banshees and shades
            case 6660:      // Shoot: spider tanks, centaurs (and a Gunner's gun)
            case 10277:     // Throw: trolls
                return true;
            default:
                return false;
        }
    }

    uint32_t RangedBasicSpell(CompanionSpecies const& sp)
    {
        if (sp.role != Role::Ranged)
            return 0;
        uint32_t const basic = MovesetData(MovesetOf(sp)).basicSpell;
        return IsRangedBasic(basic) ? basic : 0;
    }

    uint32_t RangedAttackSpell(CompanionSpecies const& sp, CompanionState const& s)
    {
        if (HasTrait(s.traits, Trait::Gunner) && TraitEligible(Trait::Gunner, sp))
            return kGunnerShotSpell;
        return RangedBasicSpell(sp);
    }

    // ── Numbers ─────────────────────────────────────────────────────────────────────────────

    uint32_t AbilityDirect(Ability const& a, CompanionStats const& st, uint32_t healedMaxHealth)
    {
        if (a.kind == AbilityKind::Heal)
            return uint32_t(std::lround(float(healedMaxHealth) * float(a.pct) / 100.f));
        return uint32_t(std::lround(BasicHit(st) * float(a.pct) / 100.f));
    }

    uint32_t AbilityOverTime(Ability const& a, CompanionStats const& st, uint32_t healedMaxHealth)
    {
        if (a.kind == AbilityKind::Heal)
            return uint32_t(std::lround(float(healedMaxHealth) * float(a.dotPct) / 100.f));
        return uint32_t(std::lround(BasicHit(st) * float(a.dotPct) / 100.f));
    }

    uint32_t AbilityCooldownMs(Ability const& a, bool primal)
    {
        uint32_t const ms = uint32_t(a.cooldown) * 1000u;
        return primal ? ms * 4u / 5u : ms;
    }

    // ── Autocast ────────────────────────────────────────────────────────────────────────────

    bool ShouldAutocast(Ability const& a, AutocastContext const& c)
    {
        if (!a.Exists() || c.riding)
            return false;
        switch (a.kind)
        {
            case AbilityKind::Mounted:
                return false;
            case AbilityKind::Taunt:
                // Pull what hits the owner (or a hurt ally) onto itself; never what already
                // fights it.
                return c.hasTarget && !c.targetAttacksCompanion && (c.ownerHitByOther || c.allyUnderHalfAttacked);
            case AbilityKind::Heal:
                if (a.aim == AbilityAim::Around)
                    return c.alliesUnder70 >= 2;
                if (c.hotFromIt)
                    return false;
                return a.aim == AbilityAim::Self ? c.companionHealth < 0.5f
                                                 : (c.ownerHealth < 0.5f || c.companionHealth < 0.5f);
            case AbilityKind::Root:
                return c.hasTarget && !c.targetHasIt &&
                    (c.targetApproachingOwner || (c.targetFleeing && c.targetHealth < 0.2f));
            case AbilityKind::Interrupt:
                return c.hasTarget && c.targetCasting;
            case AbilityKind::Stun:
                return c.hasTarget && !c.targetHasIt && (c.targetCasting || c.targetInMeleeWithOwner);
            case AbilityKind::AoEStun:
                return c.enemiesNear >= 2 && !c.wouldBreakCC;
            case AbilityKind::AoE:
                return c.enemiesNear >= 2 && !c.wouldBreakCC;
            case AbilityKind::Damage:
                return c.hasTarget;
            case AbilityKind::Burst:
                return c.hasTarget && (c.targetHealth < 0.5f || c.targetIsBoss);
            case AbilityKind::Debuff:
                return c.hasTarget &&
                    (!c.targetHasIt || c.targetStacks < c.maxStacks || c.targetItExpiresS < 3.f);
            case AbilityKind::Buff:
                return c.ownerInCombat && !c.ownerHasBuff;
            case AbilityKind::Defensive:
                return c.companionHealth < 0.4f || c.attackersOnCompanion >= 3;
            case AbilityKind::Dispel:
                return c.allyDispellable || c.enemyDispellable;
            case AbilityKind::GapCloser:
                return c.hasTarget && c.targetDistance > 8.f && !c.targetApproachingOwner;
            case AbilityKind::Fear:
                return c.enemiesInMeleeWithUs >= 2 && !c.dungeonOtherPacksNear;
        }
        return false;
    }
}
