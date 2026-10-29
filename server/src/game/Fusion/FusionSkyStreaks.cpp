/*
 * Azeroth Warfare: the lore killstreaks' game side (fusion/SkyStreaks.h). GPL-2.0-or-later.
 *
 * Each streak's craft is a creature summoned for the caller (sql/custom/azeroth_warfare/
 * 23_lore_streaks.sql: passive, seen from 200 yards), wearing a 1.12 model for the caller's side,
 * flown by the server on flying splines. Every client sees it the way it sees any creature, so
 * nothing new is drawn for the craft; the shots and blasts go out on the world channel the guns
 * already use (op 34 tracers, op 31 projectiles, op 30 explosions through FusionMgr::Explode,
 * which the ragdolls throw from), and the owner's HUD and every bombing run's ground marks on
 * op 39 (Sky::News).
 *
 *  - The Goblin Rocket (predator missile): the server starts it over the mark and tells the
 *    caller (op 37); the client flies the camera and reports where the rocket is and where it
 *    struck (op 38); the server holds it to the rocket's speed, tests each leg of its flight
 *    against the world itself (a wall the client flew through stops it there), and sets the
 *    blast off. The creature follows the reports so the others see it fall.
 *  - The gunships (attack helicopter, chopper gunner, the harrier's hover) fly in from the
 *    horizon, circle on a ring and shoot what is exposed under them, then fly away; the bombers
 *    fly a straight pass dropping bombs on a line everyone near is shown first.
 *  - The turret stands where the player pointed, sets itself up, sweeps its arc and turns on
 *    what is in it.
 *  - Every craft but the rocket can be shot down: it is owned by the caller (so the caller's PvP
 *    rules decide who may shoot it), has health on the CoD scale (Sky::CraftMaxHealth), and
 *    bursts and falls when it runs out.
 */

#include "FusionMgr.h"
#include "FusionClasses.h"

#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "Util.h"
#include "Utilities/Random.h"
#include "WorldSession.h"
#include "fusion/Arsenal.h"
#include "fusion/NpcGuns.h"
#include "fusion/Scale.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using SkyCraft = Fusion::Sky::Craft;

namespace
{
    using namespace Fusion::Sky;

    Fusion::Vec3 V(float x, float y, float z) { return {x, y, z}; }
    Fusion::Vec3 At(Unit const* u, float up = 0.f)
    {
        return {u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + up};
    }
    float Flat(Fusion::Vec3 a, Fusion::Vec3 b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    }
    bool Due(uint32 now, uint32 at) { return int32(now - at) >= 0; }

    Fusion::Team TeamOf(Player const* p)
    {
        return p->GetTeam() == HORDE ? Fusion::Team::Horde : Fusion::Team::Alliance;
    }

    // As FusionMgr.cpp's: the shooter's stats for the damage rules.
    Fusion::AttackerStats StatsOf(Player const* p)
    {
        Fusion::AttackerStats a;
        a.level = p->GetLevel();
        a.attackPower = std::max(p->GetTotalAttackPowerValue(BASE_ATTACK), p->GetTotalAttackPowerValue(RANGED_ATTACK));
        if (Item const* ranged = p->GetWeaponForAttack(RANGED_ATTACK))
            a.rangedItemLevel = float(ranged->GetProto()->ItemLevel);
        if (Item const* main = p->GetWeaponForAttack(BASE_ATTACK))
            a.mainHandItemLevel = float(main->GetProto()->ItemLevel);
        a.critChance = p->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE) / 100.f;
        return a;
    }

    // The gunship's and the turret's rounds: an LMG's, as the old sentry's.
    Fusion::WeaponProfile MakeGun(uint16 id, char const* name, float damage)
    {
        Fusion::WeaponProfile w;
        w.id = id;
        w.name = name;
        w.weaponClass = Fusion::WeaponClass::Lmg;
        w.damage = damage;
        w.minDamage = damage * 0.8f;
        w.maxDamageRange = 40.f;
        w.minDamageRange = 90.f;
        return w;
    }
    Fusion::WeaponProfile const& RiderGun()
    {
        static Fusion::WeaponProfile const w = MakeGun(3000 + Fusion::StreakId::AttackHelicopter, "cobra_player_minigun_mp", kRiderDamage);
        return w;
    }
    Fusion::WeaponProfile const& TurretGun()
    {
        static Fusion::WeaponProfile const w = MakeGun(3000 + Fusion::StreakId::SentryGun, "sentry_gun_mp", kTurretDamage);
        return w;
    }

    // The ground between two points, the terrain alone (vmaps' sight sees only models): a hill
    // between a gunship and its target hides the target.
    bool TerrainClear(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b)
    {
        Fusion::Vec3 const span = b - a;
        float const length = span.Length();
        if (length < 1.f)
            return true;
        for (float t = 1.f; t < length - 1.f; t += 2.f)
        {
            Fusion::Vec3 const p = a + span * (t / length);
            float const ground = map->GetTerrain()->GetHeightStatic(p.x, p.y, p.z, false);
            if (ground > INVALID_HEIGHT && p.z < ground - 0.3f)
                return false;
        }
        return true;
    }

    bool Clear(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b)
    {
        return map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, true, true) && TerrainClear(map, a, b);
    }

    // The ground under a point (models and terrain), searching down from it.
    std::optional<float> GroundUnder(Map const* map, float x, float y, float fromZ, float reach)
    {
        float const h = map->GetHeight(x, y, fromZ, true, reach);
        if (h <= INVALID_HEIGHT || !std::isfinite(h))
            return std::nullopt;
        return h;
    }

    // The highest ground on the way from a to b (every 10 yards), or `floor`.
    float HighestGround(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b, float floor)
    {
        float top = floor;
        Fusion::Vec3 const span = b - a;
        float const length = span.Length();
        for (float t = 0.f; t <= length; t += 10.f)
        {
            Fusion::Vec3 const p = a + span * (length > 0.f ? t / length : 0.f);
            float const h = map->GetTerrain()->GetHeightStatic(p.x, p.y, floor + 300.f, true, 600.f);
            if (h > INVALID_HEIGHT && std::isfinite(h))
                top = std::max(top, h);
        }
        return top;
    }

    void FlyPath(Creature* c, std::vector<Fusion::Vec3> const& points, float speed)
    {
        if (points.empty())
            return;
        Movement::PointsArray path;
        // The first point is where the creature is (Launch puts it there).
        path.push_back(Movement::Vector3(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ()));
        for (Fusion::Vec3 const& p : points)
            path.push_back(Movement::Vector3(p.x, p.y, p.z));
        Movement::MoveSplineInit init(*c, "FusionSkyStreaks");
        init.MovebyPath(path);
        init.SetFly();
        init.SetVelocity(std::max(1.f, speed));
        init.Launch();
    }

    // The facing a shot's direction gives (the player's own when it points straight up or down).
    float FacingOf(Fusion::Vec3 dir, float fallback)
    {
        return std::fabs(dir.x) + std::fabs(dir.y) > 1e-3f ? std::atan2(dir.y, dir.x) : fallback;
    }

    uint32 RingMs(SkyCraft craft)
    {
        switch (craft)
        {
            case SkyCraft::Rider: return kRiderMs;
            case SkyCraft::Gyrocopter: return kGyroMs;
            case SkyCraft::Hornet: return kHornetMs;
            default: return 0;
        }
    }

    // A craft's weapon id (3000 + its streak): the catalog's blast for it, the kill feed's name.
    uint16 CraftWeapon(SkyCraft craft)
    {
        return uint16(3000 + StreakOf(craft));
    }

    bool Flyer(SkyCraft craft)
    {
        return craft != SkyCraft::Turret && craft != SkyCraft::Rocket;
    }

    // The player who owns a craft creature (FusionMgr::SummonCraft), if in the world.
    Player* OwnerOf(Creature const* c)
    {
        ObjectGuid const owner = c->GetOwnerGuid();
        return owner.IsPlayer() ? ObjectAccessor::FindPlayer(owner) : nullptr;
    }

    FusionSkyCraft* FindCraft(Player* owner, ObjectGuid guid)
    {
        if (!owner || !owner->HasFusion())
            return nullptr;
        for (FusionSkyCraft& k : owner->GetFusion().sky)
            if (k.guid == guid)
                return &k;
        return nullptr;
    }

    // A rocket flight's sequence for the ring's start (a second caller's ring starts elsewhere).
    uint32 g_skySequence = 0;
}

Creature* FusionMgr::SummonCraft(Player* player, SkyCraft craft, Fusion::Vec3 at, float facing, uint32 lifeMs)
{
    Look const look = LookOf(craft, TeamOf(player));
    Creature* c = player->SummonCreature(look.entry, at.x, at.y, at.z, facing, TEMPSUMMON_TIMED_DESPAWN, lifeMs, true);
    if (!c)
        return nullptr;
    c->SetDisplayId(look.display);
    c->SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, look.mount);
    c->SetObjectScale(look.scale);
    c->SetFactionTemplateId(player->GetFactionTemplateId());
    c->SetLevel(player->GetLevel());
    c->SetCreatorGuid(player->GetObjectGuid());
    c->SetRespawnDelay(0);
    // Never one of the world's gunners (a rider is a humanoid, and would be given a gun).
    c->m_fusionGunRole = uint8(Fusion::GunRole::None);
    if (!CraftShootable(craft))
    {
        c->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        return c;
    }
    // Fair game for the caller's enemies. Owned by the caller, every attack check (a shot, a
    // blast, a spell) takes the caller's PvP rules: whoever may fight the caller may shoot it
    // down, nobody else. The turret, on the ground, takes the hostile creatures' blows too; the
    // flyers are out of their reach, so they ignore them.
    c->SetOwnerGuid(player->GetObjectGuid());
    c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    if (craft != SkyCraft::Turret)
        c->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
    // Health: so many fresh players of the caller's level, on the CoD scale; a spell takes the
    // share of it that it would of so many players (the WoW pool the spells are scaled from).
    uint32 const level = player->GetLevel();
    uint32 const health = CraftMaxHealth(craft, Fusion::Scale::PlayerMaxHealth(level, 0.f));
    c->m_fusionWowMaxHealth = std::max<uint32>(1, uint32(m_par.MaxHealth(level) * CraftToughness(craft) + 0.5f));
    c->SetMaxHealth(health);
    c->SetHealth(health);
    return c;
}

std::vector<Target> FusionMgr::SkyTargets(Player* player, Creature* craft, FusionSkyCraft const& k,
    Fusion::Vec3 around, float radius, std::unordered_map<uint64, Unit*>& byGuid) const
{
    std::vector<Target> out;
    // Searched from the craft, not from the caller (who may be across the map from it).
    float const reach = radius + (At(craft) - around).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(craft, player, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(craft, searcher, reach);
    uint32 const now = WorldTimer::getMSTime();
    FusionPlayerData const& d = player->GetFusionConst();
    bool const ownerHurtLately = d.lastHurtBy && now - d.lastHurtByMs < 5000;
    bool const craftHitLately = k.hitBy && now - k.hitByMs < 5000;
    for (Unit* u : units)
    {
        if (u == player || u == craft || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        Creature* c = u->ToCreature();
        // MW2's killstreaks turn on the enemy players the caller may fight, and the hostile
        // creatures; not a pet, a totem or a townsperson; of the enemy's craft, only a turret
        // (on the ground, where the guns look).
        bool const enemyPlayer = u->IsPlayer() && PvpAllows(player, u);
        if (u->IsPlayer() && !enemyPlayer)
            continue;
        if (!enemyPlayer && (!c || c->IsPet() || c->IsCivilian() || c->IsTotem()))
            continue;
        if (c)
            if (std::optional<SkyCraft> const other = CraftOfEntry(c->GetEntry()); other && *other != SkyCraft::Turret)
                continue;
        Target t;
        t.guid = u->GetObjectGuid().GetRawValue();
        t.chest = At(u, 1.f);
        if ((t.chest - around).Length() > radius + 5.f)
            continue;
        t.alive = true;
        t.hostile = enemyPlayer || (c && (c->IsHostileTo(player) || c->GetVictim() == player));
        t.inCombat = u->IsInCombat();
        t.player = enemyPlayer;
        t.threat = u->GetVictim() == player || (ownerHurtLately && d.lastHurtBy == t.guid);
        t.atCraft = u->GetVictim() == craft || (craftHitLately && k.hitBy == t.guid);
        out.push_back(t);
        byGuid[t.guid] = u;
    }
    return out;
}

bool FusionMgr::SkyRound(Player* player, Creature* craft, Unit* target, Fusion::Vec3 gun,
    Fusion::WeaponProfile const& w, float hitChance)
{
    Fusion::Vec3 const chest = At(target, 1.f);
    bool const hit = roll_chance_f(hitChance);
    bool killed = false;
    if (hit)
    {
        Fusion::HitInput in;
        in.weapon = &w;
        in.distance = (chest - gun).Length();
        in.location = Fusion::HitLocation::TorsoUpper;
        in.ammo = Fusion::AmmoType::Basic;
        in.attacker = StatsOf(player);
        uint32 const damage = std::max<uint32>(1, uint32(HealthDamage(player, target, in) + 0.5f));
        uint64 const guid = target->GetObjectGuid().GetRawValue();
        FusionHitSource source;
        source.weaponId = w.id;
        Hit(player, target, damage, source);
        killed = !target->IsAlive();
        Fusion::Packets::Events ev;
        uint8 const flags = killed ? Fusion::Packets::EVENT_FATAL : 0;
        ev.events.push_back({Fusion::Packets::EventKind::Hit, flags, guid, damage, w.id});
        if (killed)
            ev.events.push_back({Fusion::Packets::EventKind::Kill, flags, guid, 0, w.id});
        SendEvents(player, ev);
        // The one hit hears where it came from (its damage direction and the killcam).
        if (Player* tp = target->ToPlayer(); tp && tp->HasFusion())
        {
            Fusion::Packets::Events hurt;
            hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, craft->GetObjectGuid().GetRawValue(), damage, w.id});
            SendEvents(tp, hurt);
        }
    }
    else
        ShotNear(target, WorldTimer::getMSTime());
    // The tracer, from the craft's gun (an M240's report, so the client has a sound for it).
    Fusion::Vec3 const to = hit ? chest : Fusion::NpcMissPoint(gun, chest, frand(0.f, 1.f), frand(0.f, 1.f));
    static uint16 const reportGun = [this] {
        for (Fusion::WeaponProfile const& g : m_catalog.Weapons().All())
            if (g.name == "m240_mp")
                return g.id;
        return uint16(0);
    }();
    std::vector<uint8_t> const round = Fusion::EncodePlayerRound(craft->GetObjectGuid().GetRawValue(),
        reportGun ? reportGun : w.id, hit, gun, to);
    TellNear(player->GetMap(), gun, [&](Player*) { return round; });
    return killed;
}

void FusionMgr::NoteCraftKill(Player* owner, uint32 streakId, Unit* victim)
{
    if (!owner || !victim || !CraftOf(streakId))
        return;
    // The newest of the owner's craft of that streak takes the count for the HUD.
    if (owner->HasFusion())
    {
        std::vector<FusionSkyCraft>& sky = owner->GetFusion().sky;
        for (auto it = sky.rbegin(); it != sky.rend(); ++it)
            if (StreakOf(it->craft) == streakId)
            {
                ++it->kills;
                break;
            }
    }
    SendWorld(owner, EncodeCraftKill(streakId, victim->GetName()));
}

void FusionMgr::OnCraftHurt(Unit* attacker, Creature* craft)
{
    if (!attacker || attacker == craft)
        return;
    if (FusionSkyCraft* k = FindCraft(OwnerOf(craft), craft->GetObjectGuid()))
    {
        Unit* by = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        k->hitBy = (by ? by : attacker)->GetObjectGuid().GetRawValue();
        k->hitByMs = WorldTimer::getMSTime();
    }
}

void FusionMgr::OnCraftKilled(Unit* killer, Creature* craft)
{
    std::optional<SkyCraft> const kind = CraftOfEntry(craft->GetEntry());
    Map* map = craft->GetMap();
    if (!kind || !map)
        return;
    uint32 const now = WorldTimer::getMSTime();
    uint32 const streak = StreakOf(*kind);
    Player* owner = OwnerOf(craft);
    Player* by = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    Fusion::Vec3 const at = At(craft, 0.5f);
    Fusion::Team const team = owner ? TeamOf(owner) : Fusion::Team::Alliance;
    // The burst where it was hit, its own death noise, and (a flyer) its wreck falling to the
    // ground and bursting there. Seen and heard, hurting nobody: MW2's downed helicopters don't.
    std::vector<uint8_t> const burst = Fusion::Launch::EncodeExplosion(at, *kind == SkyCraft::Turret ? 3.f : 5.f, CraftWeapon(*kind));
    TellNear(map, at, [&](Player*) { return burst; });
    if (uint32 const sound = SoundsOf(*kind, team).destroyed)
        craft->PlayDistanceSound(sound);
    if (Flyer(*kind))
    {
        Fusion::Vec3 ground = at - V(0.f, 0.f, 40.f);
        if (std::optional<float> const g = GroundUnder(map, at.x, at.y, at.z, 200.f))
            ground = V(at.x, at.y, *g + 0.3f);
        // Falling under the grenades' gravity, at least a beat.
        float const drop = std::max(1.f, at.z - ground.z);
        uint16 const fall = uint16(std::clamp(std::sqrt(2.f * drop / Fusion::Launch::kGrenadeGravity) * 1000.f, 400.f, 4000.f));
        std::vector<uint8_t> const wreck = Fusion::Launch::EncodeLaunch(craft->GetObjectGuid().GetRawValue(), kBombVisual, at, ground, fall);
        TellNear(map, at, [&](Player*) { return wreck; });
        if (owner && owner->HasFusion())
            owner->GetFusion().wrecks.push_back({now + fall, ground});
    }
    std::string const ownerName = owner ? owner->GetName() : std::string("an enemy");
    std::string const byName = by ? by->GetName() : killer ? killer->GetName() : std::string("the enemy");
    if (FusionSkyCraft* k = FindCraft(owner, craft->GetObjectGuid()))
        k->downed = true;
    if (owner)
        SendWorld(owner, EncodeCraftDowned(streak, true, byName));
    if (by && by != owner && by->HasFusion())
    {
        SendWorld(by, EncodeCraftDowned(streak, false, ownerName));
        CountStat(by, {Fusion::Stat::StreaksDowned, Fusion::kAnyKey});
        CountStat(by, {Fusion::Stat::StreaksDowned, streak});
    }
}

void FusionMgr::SendCraftUp(Player* owner, Creature* c, FusionSkyCraft& k, uint32 now)
{
    uint8 const health = CraftShootable(k.craft) ? HealthPercent(c->GetHealth(), c->GetMaxHealth()) : 100;
    if (health == k.sentHealth && !Due(now, k.nextNewsMs))
        return;
    k.sentHealth = health;
    k.nextNewsMs = now + 5000;
    uint32 const left = Due(now, k.untilMs) ? 0 : (k.untilMs - now + 999) / 1000;
    SendWorld(owner, EncodeCraftUp(StreakOf(k.craft), k.guid.GetRawValue(), uint16(std::min<uint32>(left, 0xFFFF)), health));
}

void FusionMgr::CraftSounds(Map* map, Creature* c, FusionSkyCraft& k, uint32 now)
{
    Fusion::Sky::CraftSounds const s = SoundsOf(k.craft, k.team);
    bool const loopDue = s.loop && Due(now, k.nextHearMs);
    bool const beatDue = s.beat && Due(now, k.nextBeatMs);
    bool const cryDue = s.arrive && !k.cried && Due(now, k.startMs + 1200);
    if (!loopDue && !beatDue && !cryDue)
        return;
    if (loopDue)
        k.nextHearMs = now + 1000;
    if (beatDue)
        k.nextBeatMs = now + s.beatMs;
    if (cryDue)
        k.cried = true;
    Fusion::Vec3 const at = At(c);
    // Only to a player whose client has the craft: a sound for a unit it doesn't know plays
    // flat, everywhere, and a loop of that never stops.
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->getSource();
        if (!p || !p->IsInWorld() || !p->GetSession())
            continue;
        uint64 const guid = p->GetObjectGuid().GetRawValue();
        float const d = (At(p) - at).Length();
        auto heard = std::find(k.heard.begin(), k.heard.end(), guid);
        if (loopDue && heard != k.heard.end() && d > kLoopForget)
        {
            k.heard.erase(heard);
            heard = k.heard.end();
        }
        if (d > 200.f || !p->IsInVisibleList(c))
            continue;
        if (loopDue && heard == k.heard.end() && d <= kLoopHear)
        {
            c->PlayDistanceSound(s.loop, p);
            k.heard.push_back(guid);
        }
        if (beatDue)
            c->PlayDistanceSound(s.beat, p);
        if (cryDue)
            c->PlayDistanceSound(s.arrive, p);
    }
}

void FusionMgr::LeaveCraft(Map* map, Creature* c, FusionSkyCraft& k, uint32 now)
{
    k.leaving = true;
    k.orbiting = false;
    k.target = 0;
    float const facing = k.orbitStartMs ? OrbitFacing(k.orbit, now - k.orbitStartMs) : c->GetOrientation();
    Fusion::Vec3 const from = At(c);
    Fusion::Vec3 to = LeaveTo(from, facing);
    // Over the hills on its way out.
    float const top = HighestGround(map, from, to, from.z - 200.f);
    to.z = std::max(to.z, top + 30.f);
    FlyPath(c, {to}, kLeaveSpeed);
    k.goneMs = now + FlightMs(from, to, kLeaveSpeed);
}

bool FusionMgr::GrantStreak(Player* player, uint32 streakId)
{
    FusionPlayerData& d = player->GetFusion();
    for (Fusion::StreakReward const& r : Fusion::DefaultStreakLadder())
    {
        if (r.id != streakId)
            continue;
        d.streak.Grant(r);
        // The client's "ready" for it, as a streak that earned it says (FusionMgr::AwardStreak).
        Fusion::Packets::Events ev;
        ev.events.push_back({Fusion::Packets::EventKind::StreakEarned, 0, 0, r.kills, uint16(r.id)});
        SendEvents(player, ev);
        return true;
    }
    return false;
}

bool FusionMgr::CallSkyStreak(Player* player, uint32 streakId, Fusion::Vec3 origin, Fusion::Vec3 dir)
{
    std::optional<SkyCraft> const craft = CraftOf(streakId);
    Map* map = player->GetMap();
    if (!craft || !map || !player->IsInWorld())
        return false;
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    Fusion::Vec3 const feet = At(player);
    if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z))
        dir = V(0.f, 0.f, 0.f);
    float const facing = FacingOf(dir, player->GetOrientation());
    if (!(dir.Length() >= 0.5f))
        dir = V(std::cos(facing), std::sin(facing), -0.3f).Normalized();
    // The eye the client sent must be the player's, give or take the latency's tolerance (the
    // shots' own clamp, HitValidation.h ShotOrigin).
    origin = Fusion::ShotOrigin(origin, EyeOf(player), ShotOriginTolerance(player));
    ChatHandler chat(player);
    FusionSkyCraft k;
    k.craft = *craft;
    k.team = TeamOf(player);
    k.mapId = map->GetId();
    k.startMs = now;

    switch (*craft)
    {
        case SkyCraft::Rocket:
        {
            if (d.rocket.active)
            {
                chat.SendSysMessage("Your goblin rocket is already in the air.");
                return false;
            }
            Fusion::Vec3 const mark = AimPoint(player, origin, dir, 200.f);
            RocketPose pose = RocketStart(mark, facing);
            // An open sky over the mark: from right over it if the
            // slanted start is in a hill or a roof.
            if (!Clear(map, mark + V(0.f, 0.f, 1.f), pose.pos))
            {
                pose.pos = mark + V(0.f, 0.f, kRocketHeight);
                pose.pitch = kRocketPitchMin;
                if (!Clear(map, mark + V(0.f, 0.f, 1.f), pose.pos))
                {
                    chat.SendSysMessage("No open sky over that mark for the goblin rocket.");
                    return false;
                }
            }
            Creature* c = SummonCraft(player, SkyCraft::Rocket, pose.pos, pose.yaw, kRocketLifeMs + kRocketGraceMs + 2000);
            FusionRocket& r = d.rocket;
            r = FusionRocket();
            r.active = true;
            r.ticket = d.nextRocketTicket++;
            r.vehicle = c ? c->GetObjectGuid() : ObjectGuid();
            r.mapId = map->GetId();
            r.startMs = now;
            r.start = pose.pos;
            r.last = pose.pos;
            r.lastMs = now;
            r.movedMs = now;
            SendWorld(player, EncodeRocket(true, r.ticket, r.vehicle.GetRawValue(), pose, uint16(kRocketLifeMs)));
            return true;
        }
        case SkyCraft::Rider:
        case SkyCraft::Gyrocopter:
        {
            bool const rider = *craft == SkyCraft::Rider;
            Orbit o;
            o.centre = feet;
            o.radius = rider ? kRiderRadius : kGyroRadius;
            o.height = rider ? kRiderHeight : kGyroHeight;
            o.speed = rider ? kRiderSpeed : kGyroSpeed;
            o.startAngle = StartAngle(player->GetOrientation(), ++g_skySequence);
            // Over the hills on the ring: 25 yards over the highest of it at the least.
            float top = feet.z;
            for (int i = 0; i < 12; ++i)
            {
                float const a = float(i) / 12.f * 2.f * kPi;
                float const h = map->GetTerrain()->GetHeightStatic(feet.x + o.radius * std::cos(a),
                    feet.y + o.radius * std::sin(a), feet.z + 200.f, true, 400.f);
                if (h > INVALID_HEIGHT && std::isfinite(h))
                    top = std::max(top, h);
            }
            o.height = std::max(o.height, top - feet.z + 25.f);
            if (!Clear(map, feet + V(0.f, 0.f, 2.f), feet + V(0.f, 0.f, o.height)))
            {
                chat.SendSysMessage("No open sky here for a gunship.");
                return false;
            }
            // In from the horizon, over the hills on the way.
            Fusion::Vec3 const ring = OrbitAt(o, 0);
            Fusion::Vec3 from = ArriveFrom(o);
            from.z = std::max(from.z, HighestGround(map, from, ring, feet.z) + 25.f);
            uint32 const arrive = FlightMs(from, ring, kArriveSpeed);
            uint32 const life = RingMs(*craft);
            uint32 const leave = FlightMs(V(0.f, 0.f, 0.f), V(kLeaveTo, 0.f, kLeaveRise), kLeaveSpeed);
            Creature* c = SummonCraft(player, *craft, from, std::atan2(ring.y - from.y, ring.x - from.x),
                arrive + life + leave + 5000);
            if (!c)
                return false;
            FlyPath(c, {ring}, kArriveSpeed);
            k.guid = c->GetObjectGuid();
            k.orbiting = true;
            k.orbit = o;
            k.orbitStartMs = now + arrive;
            k.untilMs = k.orbitStartMs + life;
            k.nextShotMs = k.orbitStartMs + 600;
            k.nextPathMs = k.orbitStartMs;
            d.sky.push_back(k);
            SendCraftUp(player, c, d.sky.back(), now);
            chat.SendSysMessage(rider ? "Sky Rider Gunship inbound." : "Gyrocopter Gunship inbound.");
            return true;
        }
        case SkyCraft::Hornet:
        case SkyCraft::Bomber:
        {
            bool const hornet = *craft == SkyCraft::Hornet;
            Fusion::Vec3 const mark = AimPoint(player, origin, dir, kBomberReach);
            if (Flat(mark, feet) > kBomberReach + 5.f)
                return false;
            BombRun run = hornet
                ? MakeBombRun(mark, facing, kHornetBombs, kHornetLine, kHornetPassHeight, kHornetPassSpeed, kHornetLead)
                : MakeBombRun(mark, facing);
            // High enough over every hill on the way.
            float const top = HighestGround(map, RunStart(run) - V(0.f, 0.f, run.height), RunEnd(run) - V(0.f, 0.f, run.height), mark.z);
            run.height = std::max(run.height, top - mark.z + 25.f);
            // Where each bomb comes down, on the ground under it.
            for (uint32 i = 0; i < run.bombs; ++i)
            {
                Fusion::Vec3 p = BombPoint(run, i);
                if (std::optional<float> const ground = GroundUnder(map, p.x, p.y, mark.z + run.height - 1.f, run.height + 60.f))
                    p.z = *ground + 0.2f;
                k.marks.push_back(p);
            }
            // The bomber flies on past its line and away; the hornet stays over the mark.
            Fusion::Vec3 const end = RunEnd(run);
            Fusion::Vec3 away = LeaveTo(end, facing);
            away.z = std::max(away.z, HighestGround(map, end, away, mark.z) + 30.f);
            uint32 const leave = FlightMs(end, away, run.speed);
            Creature* c = SummonCraft(player, *craft, RunStart(run), facing,
                RunMs(run) + kBombFallMs + (hornet ? kHornetMs : leave) + leave + 5000);
            if (!c)
                return false;
            if (hornet)
                FlyPath(c, {end}, run.speed);
            else
                FlyPath(c, {end, away}, run.speed);
            k.guid = c->GetObjectGuid();
            k.bombing = true;
            k.run = run;
            k.runStartMs = now;
            k.untilMs = now + RunMs(run);
            if (hornet)
            {
                // After its pass it hunts over the mark.
                k.orbit.centre = mark;
                k.orbit.radius = kHornetRadius;
                k.orbit.height = std::max(kHornetHeight, run.height);
                k.orbit.speed = kHornetSpeed;
                k.orbit.startAngle = facing;
                k.untilMs = now + RunMs(run) + kHornetMs;
            }
            else
            {
                // Already on its way out: gone once over the horizon.
                k.leaving = true;
                k.goneMs = now + RunMs(run) + leave;
            }
            // Everyone near sees where the bombs will fall before they do: the caller's own in
            // their colour, an enemy's in red.
            uint16 const first = uint16(std::min<uint32>(LandMs(run, 0), 0xFFFF));
            uint16 const last = uint16(std::min<uint32>(LandMs(run, run.bombs - 1), 0xFFFF));
            TellNear(map, mark, [&](Player* p) {
                uint8 const flags = p == player ? kLineMine : p->GetTeam() != player->GetTeam() ? kLineHostile : 0;
                return EncodeStrikeLine(flags, first, last, k.marks);
            });
            d.sky.push_back(k);
            SendCraftUp(player, c, d.sky.back(), now);
            chat.SendSysMessage(hornet ? "Gnomish Sky Hornets inbound." : "Siege Bomber inbound.");
            return true;
        }
        case SkyCraft::Turret:
        {
            uint32 standing = 0;
            for (FusionSkyCraft const& s : d.sky)
                if (s.craft == SkyCraft::Turret)
                    ++standing;
            Spot s;
            s.feet = feet;
            s.spot = AimPoint(player, origin, dir, 9.f);
            s.ground = GroundUnder(map, s.spot.x, s.spot.y, s.spot.z + 1.f, 4.f);
            if (s.ground)
            {
                float const dx[4] = {0.5f, -0.5f, 0.f, 0.f};
                float const dy[4] = {0.f, 0.f, 0.5f, -0.5f};
                for (int i = 0; i < 4; ++i)
                {
                    std::optional<float> const h = GroundUnder(map, s.spot.x + dx[i], s.spot.y + dy[i], *s.ground + 1.f, 4.f);
                    s.corner[i] = h.has_value();
                    s.corners[i] = h.value_or(0.f);
                }
                s.sight = map->isInLineOfSight(feet.x, feet.y, feet.z + 1.5f, s.spot.x, s.spot.y, *s.ground + 0.8f, true, true);
            }
            s.standing = standing;
            Placement const p = CheckPlacement(s);
            if (p != Placement::Ok)
            {
                chat.SendSysMessage(PlacementText(p));
                return false;
            }
            Fusion::Vec3 const at = V(s.spot.x, s.spot.y, *s.ground);
            float const yaw = player->GetOrientation();
            Creature* c = SummonCraft(player, SkyCraft::Turret, at, yaw, kTurretDeployMs + kTurretMs + 2000);
            if (!c)
                return false;
            k.guid = c->GetObjectGuid();
            k.at = at;
            k.facing = yaw;
            k.shownYaw = yaw;
            k.activeMs = now + kTurretDeployMs;
            k.untilMs = k.activeMs + kTurretMs;
            k.nextShotMs = k.activeMs;
            k.turnMs = k.activeMs;
            d.sky.push_back(k);
            SendCraftUp(player, c, d.sky.back(), now);
            chat.PSendSysMessage("Dwarven Sentry Turret set down: it covers your front for %u seconds.", kTurretMs / 1000);
            return true;
        }
    }
    return false;
}

void FusionMgr::EndRocket(Player* player, bool tellClient)
{
    FusionRocket& r = player->GetFusion().rocket;
    if (!r.active)
        return;
    r.active = false;
    if (Map* map = player->GetMap())
        if (Creature* c = map->GetCreature(r.vehicle))
            c->DespawnOrUnsummon();
    if (tellClient)
    {
        RocketPose pose;
        pose.pos = r.last;
        SendWorld(player, EncodeRocket(false, r.ticket, 0, pose, 0));
    }
}

void FusionMgr::HandleRocketReport(Player* player, std::vector<uint8> const& body)
{
    std::optional<RocketReportMsg> const m = DecodeRocketReport(body);
    FusionPlayerData& d = player->GetFusion();
    FusionRocket& r = d.rocket;
    if (!m || !r.active || m->ticket != r.ticket)
        return;
    if (m->kind == RocketReport::Fly && !r.reports.Allow(WorldTimer::getMSTime()))
        return;
    Map* map = player->GetMap();
    if (!map || map->GetId() != r.mapId || !player->IsAlive())
    {
        EndRocket(player, true);
        return;
    }
    uint32 const now = WorldTimer::getMSTime();
    // Held to the rocket's speed: a report it could not have reached stands where the last one
    // left it.
    bool const reachable = RocketStepOk(r.last, m->at, now - r.lastMs) && RocketReachOk(r.start, m->at, now - r.startMs);
    Fusion::Vec3 const at = reachable ? m->at : r.last;
    // The rocket's blast where it struck: a little short of the surface so the blast sees out of
    // it, never under the ground.
    auto detonate = [&](Fusion::Vec3 hit) {
        if (std::optional<float> const ground = GroundUnder(map, hit.x, hit.y, hit.z + 2.f, 10.f); ground && hit.z < *ground)
            hit.z = *ground + 0.2f;
        uint16 const weapon = uint16(3000 + Fusion::StreakId::PredatorMissile);
        Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(weapon);
        sFusionClasses.GroundingCatch(player, r.mapId, hit);
        EndRocket(player, true);
        Explode(player, hit, w ? w->explosionRadius : 12.f, w ? w->explosionInner : 600.f,
            w ? w->explosionOuter : 300.f, weapon);
    };
    switch (m->kind)
    {
        case RocketReport::Fly:
        {
            if (!reachable)
                return;
            // Each leg is tested against the same world the client flies in (the terrain, the
            // buildings' and the doodads' hulls, the built pieces): one the client flew through
            // (a building it hadn't loaded) ends there, as the client would have.
            if (std::optional<float> const wall = WorldOnSegment(map, r.last, at))
            {
                Fusion::Vec3 const dirn = (at - r.last).Normalized();
                detonate(r.last + dirn * std::max(0.f, *wall - 0.3f));
                return;
            }
            r.last = at;
            r.lastMs = now;
            // The others see it fall: the creature follows, five times a second.
            if (now - r.movedMs >= 200)
                if (Creature* c = map->GetCreature(r.vehicle))
                {
                    float const secs = std::max(0.1f, float(now - r.movedMs) / 1000.f);
                    float const speed = Fusion::Clamp((at - At(c)).Length() / secs, kRocketSpeed * 0.5f, kRocketBoost * 1.5f);
                    FlyPath(c, {at}, speed);
                    r.movedMs = now;
                }
            return;
        }
        case RocketReport::Impact:
        {
            // The server finds the first thing in the way itself, with the same test as each leg
            // of the flight (the terrain, the hulls, the built pieces): a wall the client flew
            // through stops it there; a body or the ground the client met ends it where it said.
            Fusion::Vec3 const span = at - r.last;
            float const length = span.Length();
            Fusion::Vec3 hit = at;
            if (length > 0.1f)
            {
                Fusion::Vec3 const dirn = span * (1.f / length);
                if (std::optional<float> const wall = WorldOnSegment(map, r.last, at + dirn * 0.5f))
                    hit = r.last + dirn * std::max(0.f, std::min(*wall, length) - 0.3f);
            }
            detonate(hit);
            return;
        }
        case RocketReport::Bail:
            EndRocket(player, true);
            return;
    }
}

void FusionMgr::DismissSkyCraft(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (Map* map = player->GetMap())
        for (FusionSkyCraft const& k : d.sky)
        {
            if (k.mapId == map->GetId())
                if (Creature* c = map->GetCreature(k.guid))
                    c->DespawnOrUnsummon();
            SendWorld(player, EncodeCraftGone(k.guid.GetRawValue(), GoneWhy::Dismissed));
        }
    d.sky.clear();
    EndRocket(player, true);
}

void FusionMgr::UpdateSkyStreaks(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    if (!map)
        return;
    // The rocket: lost with its pilot, on another map, or out of fuel (no blast).
    if (d.rocket.active && (!player->IsAlive() || map->GetId() != d.rocket.mapId
        || Due(now, d.rocket.startMs + kRocketLifeMs + kRocketGraceMs)))
        EndRocket(player, true);

    // Shot-down craft's wrecks hitting the ground: a burst to see, hurting nobody.
    for (auto it = d.wrecks.begin(); it != d.wrecks.end();)
    {
        if (!Due(now, it->first))
        {
            ++it;
            continue;
        }
        std::vector<uint8_t> const burst = Fusion::Launch::EncodeExplosion(it->second, 6.f, kBombVisual);
        TellNear(map, it->second, [&](Player*) { return burst; });
        it = d.wrecks.erase(it);
    }

    std::vector<FusionSkyCraft> crafts;
    crafts.swap(d.sky);
    std::vector<FusionSkyCraft> keep;
    for (FusionSkyCraft& k : crafts)
    {
        Creature* c = k.mapId == map->GetId() ? map->GetCreature(k.guid) : nullptr;
        // Left behind on another map, shot down, gone, or flown away.
        bool const gone = !c || !c->IsAlive() || k.downed || (k.leaving && Due(now, k.goneMs) && k.falling.empty());
        if (gone)
        {
            if (c)
                c->DespawnOrUnsummon();
            if (!k.leaving || k.downed)
                SendWorld(player, EncodeCraftGone(k.guid.GetRawValue(), k.downed || (c && !c->IsAlive()) ? GoneWhy::ShotDown : GoneWhy::Done));
            continue;
        }
        Fusion::Vec3 const gun = At(c, -0.5f);
        // The caller too far from it to hear it (130 yards): it flies on but
        // holds fire, and no far grid is searched.
        bool const inReach = (At(player) - gun).Length() < 150.f;
        uint16 const weapon = CraftWeapon(k.craft);

        // A bombing pass: each bomb as the craft is over its spot, down in kBombFallMs.
        if (k.bombing)
        {
            uint32 const into = now - k.runStartMs;
            while (k.bombsDropped < k.run.bombs && into >= ReleaseMs(k.run, k.bombsDropped))
            {
                Fusion::Vec3 const p = k.bombsDropped < k.marks.size() ? k.marks[k.bombsDropped] : BombPoint(k.run, k.bombsDropped);
                Fusion::Vec3 const from = BomberAt(k.run, ReleaseMs(k.run, k.bombsDropped));
                ++k.bombsDropped;
                // A bomb to see falling (op 31's bomb), and its blast when it lands.
                std::vector<uint8_t> const launch = Fusion::Launch::EncodeLaunch(c->GetObjectGuid().GetRawValue(), kBombVisual,
                    from, p, uint16(kBombFallMs));
                TellNear(map, p, [&](Player*) { return launch; });
                k.falling.push_back({now + kBombFallMs, p});
            }
            if (into >= RunMs(k.run))
            {
                k.bombing = false;
                if (k.craft == SkyCraft::Hornet)
                {
                    k.orbiting = true;
                    k.orbitStartMs = now;
                    k.nextPathMs = now;
                    k.nextShotMs = now + 800;
                }
                else
                    SendWorld(player, EncodeCraftGone(k.guid.GetRawValue(), GoneWhy::Done));    // its run done
            }
        }
        for (auto it = k.falling.begin(); it != k.falling.end();)
        {
            if (!Due(now, it->first))
            {
                ++it;
                continue;
            }
            Fusion::Vec3 at = it->second;
            it = k.falling.erase(it);
            Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(weapon);
            sFusionClasses.GroundingCatch(player, k.mapId, at);
            Explode(player, at, w ? w->explosionRadius : 12.f, w ? w->explosionInner : 500.f, w ? w->explosionOuter : 150.f, weapon);
        }

        // Its work done: a flyer heads for the horizon, a turret is taken down.
        if (!k.leaving && !k.bombing && Due(now, k.untilMs))
        {
            if (k.craft == SkyCraft::Turret)
            {
                c->DespawnOrUnsummon();
                SendWorld(player, EncodeCraftGone(k.guid.GetRawValue(), GoneWhy::Done));
                continue;
            }
            LeaveCraft(map, c, k, now);
            SendWorld(player, EncodeCraftGone(k.guid.GetRawValue(), GoneWhy::Done));
        }

        CraftSounds(map, c, k, now);
        if (!k.leaving)
            SendCraftUp(player, c, k, now);

        // A ring: the next stretch of it when the last is nearly flown.
        if (k.orbiting && !k.leaving && Due(now, k.nextPathMs) && Due(now, k.orbitStartMs))
        {
            uint32 const t = now - k.orbitStartMs;
            FlyPath(c, OrbitPath(k.orbit, t + 250, 4000, 500), k.orbit.speed);
            k.nextPathMs = now + 3800;
        }

        // The guns: not while it is still flying in, setting up, or leaving.
        bool const armed = !k.leaving && (k.craft == SkyCraft::Turret ? Due(now, k.activeMs) : k.orbiting && Due(now, k.orbitStartMs));
        if (inReach && armed && Due(now, k.nextShotMs))
        {
            std::unordered_map<uint64, Unit*> byGuid;
            auto sight = [map](Fusion::Vec3 a, Fusion::Vec3 b) { return Clear(map, a, b); };
            uint64 const was = k.target;
            if (k.craft == SkyCraft::Turret)
            {
                Fusion::Vec3 const muzzle = k.at + V(0.f, 0.f, kTurretMuzzle);
                Unit* target = k.target ? map->GetUnit(ObjectGuid(k.target)) : nullptr;
                if (target && (!target->IsAlive() || !player->IsValidAttackTarget(target)
                    || !TurretCovers(muzzle, k.facing, At(target, 1.f)) || !Clear(map, muzzle, At(target, 1.f))))
                    target = nullptr;
                if (!target || Due(now, k.retargetMs))
                {
                    std::vector<Target> const ts = SkyTargets(player, c, k, k.at, kTurretRange + 2.f, byGuid);
                    std::optional<uint64_t> const pick = TurretTarget(muzzle, k.facing, ts, sight, k.target);
                    target = pick ? byGuid[*pick] : nullptr;
                    k.retargetMs = now + kRetargetMs;
                }
                k.target = target ? target->GetObjectGuid().GetRawValue() : 0;
                if (target)
                {
                    // On a new target: swing onto it and spin the barrel up before the first round.
                    if (k.target != was)
                    {
                        k.engageMs = now + kTurretSpinUpMs;
                        k.roundsOnTarget = 0;
                    }
                    float const want = TurretAimYaw(k.at, At(target));
                    if (TurretShouldTurn(k.shownYaw, want) && Due(now, k.turnMs))
                    {
                        c->SetFacingTo(want);
                        k.shownYaw = want;
                        k.turnMs = now + 150;
                    }
                    if (Due(now, k.engageMs))
                    {
                        if (SkyRound(player, c, target, muzzle, TurretGun(), WalkInChance(80.f, k.roundsOnTarget++)))
                            NoteCraftKill(player, StreakOf(k.craft), target);
                        k.nextShotMs = now + kTurretShotMs;
                    }
                    else
                        k.nextShotMs = std::min(k.engageMs, now + kTurretShotMs);
                }
                else
                {
                    // Nobody in its arc: it sweeps it.
                    if (Due(now, k.turnMs))
                    {
                        float const want = TurretIdleYaw(k.facing, now - k.activeMs);
                        if (TurretShouldTurn(k.shownYaw, want))
                        {
                            c->SetFacingTo(want);
                            k.shownYaw = want;
                        }
                        k.turnMs = now + 350;
                    }
                    k.nextShotMs = now + 300;
                }
            }
            else if (k.orbiting)
            {
                // The one it is on while it stays fair game (alive, under the ring, in reach and
                // in the open); a fresh look twice a second.
                Unit* target = k.target ? map->GetUnit(ObjectGuid(k.target)) : nullptr;
                if (target && (!target->IsAlive() || !player->IsValidAttackTarget(target)
                    || Flat(At(target), k.orbit.centre) > kPatrolReach || (At(target, 1.f) - gun).Length() > kGunReach
                    || !Clear(map, gun, At(target, 1.f))))
                    target = nullptr;
                if (!target || Due(now, k.retargetMs))
                {
                    std::vector<Target> const ts = SkyTargets(player, c, k, k.orbit.centre, kPatrolReach, byGuid);
                    std::optional<uint64_t> const pick = AirTarget(gun, k.orbit.centre, kPatrolReach, kGunReach, ts, sight, k.target);
                    target = pick ? byGuid[*pick] : nullptr;
                    k.retargetMs = now + kRetargetMs;
                }
                k.target = target ? target->GetObjectGuid().GetRawValue() : 0;
                if (k.target != was)
                    k.roundsOnTarget = 0;
                if (!target)
                    k.nextShotMs = now + 400;
                else if (k.craft == SkyCraft::Rider)
                {
                    if (SkyRound(player, c, target, gun, RiderGun(), WalkInChance(75.f, k.roundsOnTarget++)))
                        NoteCraftKill(player, StreakOf(k.craft), target);
                    k.nextShotMs = now + kRiderShotMs;
                }
                else
                {
                    // The gyrocopter's 20 mm shells and the hornet's rockets: a round on its way
                    // (op 31) and its blast when it gets there; the first few walk onto the target.
                    bool const gyro = k.craft == SkyCraft::Gyrocopter;
                    Fusion::Vec3 to = At(target, 0.3f);
                    if (!roll_chance_f(WalkInChance(100.f, k.roundsOnTarget++)))
                    {
                        float const a = frand(0.f, 2.f * kPi);
                        float const off = frand(3.f, 5.f);
                        to = to + V(std::cos(a) * off, std::sin(a) * off, 0.f);
                        if (std::optional<float> const g = GroundUnder(map, to.x, to.y, to.z + 3.f, 8.f))
                            to.z = *g + 0.2f;
                    }
                    float const speed = gyro ? 140.f : 90.f;
                    uint16 const flight = uint16(std::min(3000.f, (to - gun).Length() / speed * 1000.f));
                    std::vector<uint8_t> const launch = Fusion::Launch::EncodeLaunch(c->GetObjectGuid().GetRawValue(), weapon, gun, to, flight);
                    TellNear(map, gun, [&](Player*) { return launch; });
                    FusionCharge b;
                    b.kind = FusionCharge::Kind::Blast;
                    b.weaponId = weapon;
                    b.pos = to;
                    Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(weapon);
                    b.radius = gyro ? (w ? w->explosionRadius : 3.f) : 2.5f;
                    b.inner = gyro ? (w ? w->explosionInner : 120.f) : 90.f;
                    b.outer = gyro ? (w ? w->explosionOuter : 60.f) : 40.f;
                    b.mapId = map->GetId();
                    b.atMs = now + flight;
                    d.charges.push_back(b);
                    k.nextShotMs = now + (gyro ? kGyroShotMs : kHornetShotMs);
                }
            }
        }
        keep.push_back(std::move(k));
    }
    // Anything called in while these flew stays too.
    for (FusionSkyCraft& k : d.sky)
        keep.push_back(std::move(k));
    d.sky.swap(keep);
}

// `.streak <name>` and `.streak list` (GM level 3 and up): any killstreak of the ladder, ready to
// call in with the killstreak slot, without the kills.
bool ChatHandler::HandleStreakCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !player->HasFusion())
    {
        SendSysMessage("Fusion: .streak needs a character in the world.");
        SetSentErrorMessage(true);
        return false;
    }
    std::string const what = args ? std::string(args) : std::string();
    std::vector<uint32_t> ids;
    for (Fusion::StreakReward const& r : Fusion::DefaultStreakLadder())
        ids.push_back(r.id);
    std::string trimmed = what;
    trimmed.erase(0, trimmed.find_first_not_of(' '));
    trimmed.erase(trimmed.find_last_not_of(' ') + 1);
    if (trimmed.empty() || trimmed == "list")
    {
        SendSysMessage("Killstreaks (.streak <name or id>, then the killstreak slot calls it in; .streak clear takes yours down):");
        for (Fusion::StreakReward const& r : Fusion::DefaultStreakLadder())
            PSendSysMessage("  %u  %s (%s, %u kills)", r.id, Fusion::Arsenal::StreakName(r.id).c_str(),
                Fusion::Arsenal::StreakKey(r.id), r.kills);
        return true;
    }
    if (trimmed == "clear")
    {
        sFusionMgr.DismissSkyCraft(player);
        SendSysMessage("Your killstreak craft are dismissed.");
        return true;
    }
    std::optional<uint32_t> const id = Fusion::Sky::StreakByName(trimmed, ids);
    if (!id)
    {
        PSendSysMessage("No killstreak called \"%s\". Try .streak list", trimmed.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    sFusionMgr.GrantStreak(player, *id);
    PSendSysMessage("%s is ready: use the killstreak slot on your Modern bar to call it in.", Fusion::Arsenal::StreakName(*id).c_str());
    return true;
}
