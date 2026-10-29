/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_NPC_GUNS_H
#define FUSION_NPC_GUNS_H

#include "fusion/Common.h"
#include "fusion/Creatures.h"
#include "fusion/Movement.h"
#include "fusion/Weapon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Fusion
{
    // Which NPCs carry MW2 guns. A gun needs hands and a fighter: humanoids (naga, gnolls, ogres
    // and the like count) that are not casters (they wait for the spell system). Those already
    // shooting (a bow, a gun, a thrown weapon) all swap to guns; labourers (miners, peons, slaves)
    // stay melee trash; of the other fighters a share carries guns, fixed per kind.
    enum class GunRole : uint8_t
    {
        None,           // untouched: casters, beasts, everything without hands for a gun
        Gunner,
        MeleeTrash,     // a humanoid fighting with its hands (runs faster at gunmen)
    };

    struct GunFacts
    {
        CreatureType type = CreatureType::Humanoid;
        bool caster = false;
        bool rangedWeapon = false;      // its ranged slot holds a bow, gun, crossbow or thrown weapon
        std::string name;
        uint32_t entry = 0;
        uint32_t faction = 0;           // its faction template
        bool boss = false;              // a boss keeps its script's ways (no gun)
    };

    // The gunner camps (design doc, section 10): the humanoid factions that plausibly carry
    // firearms (the Defias, the Scarlet Crusade, the Syndicate, the Dark Irons, Venture Co., the
    // pirates and the like), by faction template, or by name where they share a template (the
    // world's monsters, 16). Most of their fighters carry guns; of the wild humanoids (murlocs,
    // kobolds, gnolls, troggs, quilboar, centaurs, trolls, ogres) only a few.
    struct GunnerCamp
    {
        char const* name;
        std::vector<uint32_t> factions;
        std::vector<char const*> names;     // a part of the creature's name
        uint32_t sharePercent;
    };
    std::vector<GunnerCamp> const& GunnerCamps();
    // The camp a creature belongs to, by its faction template or its name; null for none.
    GunnerCamp const* CampOf(uint32_t faction, std::string const& name);
    // The share of the wild humanoids' fighters that carry guns.
    constexpr uint32_t kWildGunnerSharePercent = 20;
    GunRole ClassifyGunner(GunFacts const& f);

    // The MW2 gun a kind of NPC carries (its catalog name), by its level and rank, fixed per
    // kind: pistols, machine pistols and SMGs low; SMGs, rifles and shotguns in the middle levels;
    // rifles and the odd LMG high; an elite two steps up (LMGs from 40); a rare elite a sniper
    // (from level 20). And the WoW gun its model holds.
    std::string NpcGunFor(uint32_t entry, uint32_t level, CreatureRank rank);
    std::string NpcGunFor(uint32_t entry, uint32_t level);
    uint32_t NpcGunItemFor(uint32_t level);

    // A gunner closes in until it sees its target within its gun's hold range, then stands and
    // fires from there (it shoots whenever it sees the target within kGunnerReach, on the move
    // too); it advances again when it loses sight or the target draws past that range. Only a
    // target inside kGunnerKnifeRange brings it to hand to hand.
    constexpr float kGunnerReach = 45.f;
    constexpr float kGunnerKnifeRange = 3.f;
    // Where a gunner stands off: about 10 yards with a shotgun, SMG or pistol, 20 with a rifle or
    // an LMG, 35 with a sniper.
    float GunnerHoldRange(WeaponClass c);
    // Whether a gunner `distance` from its target keeps chasing: it lost sight, the target is
    // past `holdRange`, or within the knife's reach.
    bool GunnerShouldAdvance(float distance, bool lineOfSight, float holdRange);

    // Where an NPC's round lands and whether it hits: the aim cone of Gunfights.h (NpcAimCone,
    // NpcConeHitChance).

    // An NPC round's damage to a player on the CoD scale: the gun's CoD damage times the
    // creature's multiplier (Scale::CreatureGunMultiplier), less the player's armor cut.
    uint32_t NpcHitDamage(float codDamage, float creatureMultiplier, float armorCut);

    // ── What players see of NPC gunners: SMSG_FUSION_CHANNEL (kChannelWorld) bodies, little-endian,
    // raw WoW coordinates (x, y, z up), told to every player within kNpcTellRadius of the NPC.
    // op 32, 24 bytes: [32, npc guid u64, gun id u16 (its MW2 gun's catalog id), hit u8,
    //                   to x y z f32]: a round it fired, ending at `to` (the target's chest on a
    //                   hit, else beside the target, or on the cover that took it)
    // op 33: [33, count u8, then count x (npc guid u64, gun id u16)], at most kMaxNpcGunsListed:
    //        every kNpcGunsEveryMs, the gunners (alive, in the world) near a player
    // op 34, 36 bytes: [34, shooter guid u64, gun id u16, hit u8, from x y z f32, to x y z f32]:
    //        a player's round (its first pellet), from the shooter's eye to the nearest body it
    //        hit, else the wall, else the gun's range; told to everyone within kNpcTellRadius of
    //        the shooter but the shooter (who draws its own)
    constexpr float kNpcTellRadius = 100.f;
    constexpr uint8_t kOpNpcFire = 32;
    constexpr uint8_t kOpNpcGuns = 33;
    constexpr uint8_t kOpPlayerRound = 34;
    constexpr uint8_t kMaxNpcGunsListed = 200;
    constexpr uint32_t kNpcGunsEveryMs = 2000;
    struct NpcGunEntry
    {
        uint64_t guid = 0;
        uint16_t gun = 0;
    };
    std::vector<uint8_t> EncodeNpcFire(uint64_t npc, uint16_t gun, bool hit, Vec3 to);
    std::vector<uint8_t> EncodeNpcGuns(std::vector<NpcGunEntry> const& gunners);
    std::vector<uint8_t> EncodePlayerRound(uint64_t shooter, uint16_t gun, bool hit, Vec3 from, Vec3 to);
    // Where a missed round goes: 1 to 2 yards (by `radius01` in [0, 1]) beside `chest`, square to
    // the line of fire from `muzzle`, around it by `angle01` in [0, 1).
    Vec3 NpcMissPoint(Vec3 muzzle, Vec3 chest, float angle01, float radius01);
    // The same, `offYards` beside the chest (Gunfights.h NpcMissOffset).
    Vec3 NpcMissPointAt(Vec3 muzzle, Vec3 chest, float angle01, float offYards);

    // An NPC's trigger: bursts of `burst` rounds `gapMs` apart, `pauseMs` between bursts.
    struct NpcTrigger
    {
        uint32_t burstLeft = 0;
        uint32_t nextMs = 0;
        bool started = false;
        bool Fire(uint32_t nowMs, uint32_t burst, uint32_t gapMs, uint32_t pauseMs);
    };

    // ── Soldiering, after MW2's campaign and Spec Ops AI ──
    // A gunner in a fight thinks about once every kGunnerThinkMs (sight tests are not free). In the
    // open it never stands still for long: every kStrafeMinMs..kStrafeMaxMs it takes a new firing
    // spot a few yards to the side, in sight of its target, and fires on the move. Hurt (it lost
    // kHurtShare of its health within kHurtWindowMs) or shot at (a round passed near it within
    // kShotAtWindowMs) it runs for cover within kCoverRadius: a reachable spot the target cannot
    // see. It stays kCoverMinMs..kCoverMaxMs, peeks out to a spot in sight for kPeekMs to fire a
    // burst, and goes back while it is still pressed, at most kMaxPeeks times; with no cover to be
    // had it strafes instead. Out of sight or past its hold range it closes in as before.
    constexpr uint32_t kGunnerThinkMs = 1000;
    constexpr uint32_t kStrafeMinMs = 2000, kStrafeMaxMs = 4000;
    constexpr float kHurtShare = 30.f;              // health percent
    constexpr uint32_t kHurtWindowMs = 5000;
    constexpr uint32_t kShotAtWindowMs = 2000;
    constexpr float kCoverRadius = 12.f;
    constexpr uint32_t kCoverMinMs = 2000, kCoverMaxMs = 4000;
    constexpr uint32_t kPeekMs = 1800;
    constexpr uint8_t kMaxPeeks = 3;
    // After a cover cycle it fights in the open this long before it hides again.
    constexpr uint32_t kCoverCooldownMs = 6000;
    // A gunner runs about this fast to a spot (the time it allows to get there).
    constexpr float kGunnerRunSpeed = 7.f;

    enum class GunnerMode : uint8_t { Chase, Fight, Cover, Peek };
    // What the glue does next: nothing new, its WoW chase, or a run to a spot it picks.
    enum class GunnerAct : uint8_t { None, Chase, Strafe, TakeCover, Peek, ReturnToCover };

    struct GunnerSense
    {
        float distance = 0.f;
        bool sight = false;
        float holdRange = 20.f;
        bool hurt = false;
        bool shotAt = false;
    };

    // What a gunner remembers of its fight.
    struct GunnerMind
    {
        GunnerMode mode = GunnerMode::Chase;
        uint32_t thinkMs = 0;           // the next think
        uint32_t untilMs = 0;           // the current spot (or hold in cover) ends
        uint32_t coverReadyMs = 0;      // it may take cover again from here
        uint8_t peeks = 0;
        bool hasCover = false;
        Vec3 cover;
        bool hasAnchor = false;
        float anchor = 0.f;             // its preferred bearing from the target (radians)
    };

    // The next act, at most once a think; but a gunner closing in stands off the moment it sees
    // its target within its hold range, between thinks.
    GunnerAct GunnerThink(GunnerMind& m, GunnerSense const& s, uint32_t nowMs);
    // The glue's report on an act: whether it found a spot, the yards to it, and a random number
    // for the hold's length. A failed cover or peek leaves it fighting in the open (the glue then
    // strafes).
    void GunnerMoved(GunnerMind& m, GunnerAct act, bool found, Vec3 spot, float travel, uint32_t nowMs, uint32_t roll);

    // Squads spread out: each gunner's preferred bearing about the target, fixed by its guid, is
    // offset from where it came in: up to 30 degrees either way, or for one in three (the
    // flankers) 45 to 90 degrees round to one side. Radians.
    float GunnerBearingOffset(uint64_t guid);

    // A spot the glue tested: walkable and in the open from the gunner (reachable), and whether
    // the target's eye sees a body's chest there.
    struct GunnerSpot
    {
        Vec3 pos;
        bool reachable = false;
        bool inSight = false;
    };
    // Candidate spots (feet, z as given; the glue finds the ground). Firing spots: on the circle
    // about the target at its fighting distance, 3 to 6 yards to either side of the gunner and a
    // step toward its preferred bearing (8). Cover: rings 5 and 10 yards about the gunner (10).
    // Peeks: rings 2.5 and 4 yards about its cover (8).
    std::vector<Vec3> FiringSpots(Vec3 self, Vec3 target, float holdRange, float preferred);
    std::vector<Vec3> CoverSpots(Vec3 self, uint32_t roll);
    std::vector<Vec3> PeekSpots(Vec3 cover);
    // The choices (an index into `spots`, -1 none). A firing spot must be reachable and in sight;
    // it leans to the preferred bearing and away from squadmates (`squad`: their feet) by place
    // and by line, with `roll` breaking ties. Cover must be reachable and hidden, not too near
    // the target, the nearest that does not close in. A peek: the nearest reachable in sight.
    int ChooseFiringSpot(std::vector<GunnerSpot> const& spots, Vec3 self, Vec3 target, float preferred,
        std::vector<Vec3> const& squad, uint32_t roll);
    int ChooseCoverSpot(std::vector<GunnerSpot> const& spots, Vec3 self, Vec3 target);
    int ChoosePeekSpot(std::vector<GunnerSpot> const& spots, Vec3 cover);

    // Whether it was hurt badly of late: the share of health lost since the highest point within
    // kHurtWindowMs.
    struct HurtWindow
    {
        float markPct = 0.f;
        uint32_t markMs = 0;
        bool started = false;
        bool Update(float healthPct, uint32_t nowMs);
    };

    // Where a gunner last saw its target and since when it has (or has not): its grenades go
    // where the target hid. Its aim is NpcAimTrack's (Gunfights.h).
    struct SightTrack
    {
        uint64_t target = 0;
        bool seeing = false;
        bool everSeen = false;
        uint32_t sinceMs = 0;           // seeing: unbroken since; else hidden since
        Vec3 lastSeen;
        void Update(uint64_t targetGuid, bool sight, bool fast, Vec3 targetFeet, uint32_t nowMs);
        uint32_t HiddenMs(uint32_t nowMs) const;    // 0 while seen (or never seen)
    };

    // Grenades: a target behind cover (unseen kNpcGrenadeHiddenMs) whose last seen place is
    // kNpcGrenadeMin..Max yards off draws a frag there, by kNpcGrenadeChancePercent a think, at
    // most one every kNpcGrenadeCooldownMs per gunner and one at a time among gunners near each
    // other. The frag flies the launcher's arc (Launch::FlyArc), lobbed to land in about
    // NpcLobSeconds.
    constexpr uint32_t kNpcGrenadeHiddenMs = 5000;
    constexpr float kNpcGrenadeMin = 8.f, kNpcGrenadeMax = 25.f;
    constexpr uint32_t kNpcGrenadeChancePercent = 30;
    constexpr uint32_t kNpcGrenadeCooldownMs = 15000;
    // Gunners this near count as one group for grenades and for spacing.
    constexpr float kSquadRadius = 20.f;
    bool NpcShouldThrowGrenade(uint32_t hiddenMs, float distance, bool cooledDown, bool squadThrowing, uint32_t roll);
    float NpcLobSeconds(float distance);
}

#endif
