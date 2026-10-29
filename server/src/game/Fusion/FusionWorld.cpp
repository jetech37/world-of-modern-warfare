/*
 * Azeroth Warfare: open-world construction's game glue beyond FusionConstruction.cpp: spells that
 * hit built pieces, and the Sentry Gun killstreak's sentry (fusion/Build.h, fusion/Defense.h).
 * GPL-2.0-or-later.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Creature.h"
#include "WorldSession.h"
#include "DBCStores.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "Timer.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace
{
    Fusion::Vec3 At(Unit const* u, float up = 0.f)
    {
        return Fusion::Vec3{u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + up};
    }
}

void FusionMgr::SpellHitsPieces(Spell* spell)
{
    using namespace Fusion::Build;
    if (!m_enabled || !spell || !spell->m_spellInfo)
        return;
    SpellEntry const* info = spell->m_spellInfo;
    Unit* caster = spell->GetAffectiveCaster();
    if (!caster || !caster->IsInWorld())
        return;
    // Players' spells, and their pets' and totems': a creature's own spells leave pieces be (its
    // sieges break them by hand).
    Player* owner = caster->GetCharmerOrOwnerPlayerOrPlayerItself();
    Map* map = caster->GetMap();
    if (!owner || !map || map->Instanceable())
        return;
    uint32 const mapId = map->GetId();
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it == m_pieces.end() || it->second.empty())
            return;
    }
    Unit* target = spell->m_targets.getUnitTarget();
    bool const toGround = (spell->m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) != 0;
    std::unordered_map<uint32, float> hurt;
    for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // Direct damage, and a damaging area left on the ground (Blizzard, Rain of Fire,
        // Flamestrike's fire, Consecration): every tick at once.
        bool const direct = info->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE;
        bool const ground = info->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA && info->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE;
        if (!direct && !ground)
            continue;
        float const amount = spell->CalculateDamage(SpellEffectIndex(i), nullptr);
        float damage = SpellPieceDamage(amount, caster->GetLevel(), ground);
        if (ground)
        {
            uint32 const amplitude = info->EffectAmplitude[i];
            int32 const duration = info->GetDuration();
            damage *= float(amplitude && duration > 0 ? std::max<int32>(1, duration / int32(amplitude)) : 1);
        }
        if (damage <= 0.f)
            continue;
        float radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(info->EffectRadiusIndex[i]));
        Fusion::Vec3 centre;
        if (toGround)
            centre = Fusion::Vec3{spell->m_targets.m_destX, spell->m_targets.m_destY, spell->m_targets.m_destZ};
        else if (radius > 0.f || !target)
            centre = At(caster);            // about the caster: Arcane Explosion, Thunder Clap, Frost Nova
        else
        {
            // A single target: the pieces right about it, and the one between caster and target
            // (the bolt strikes the cover).
            centre = At(target, 1.f);
            radius = kSpellSingleRadius;
            if (target != caster)
            {
                Fusion::Vec3 const from = At(caster, 1.6f);
                Fusion::Vec3 const span = centre - from;
                float const length = span.Length();
                float t = 0.f;
                if (length > 0.5f)
                    if (uint32 const piece = PieceOnRay(mapId, from, span * (1.f / length), length, t))
                        hurt[piece] += damage;
            }
        }
        if (radius <= 0.f)
            continue;
        for (Piece const& p : PiecesNear(mapId, centre, radius + 6.f))
            if (Distance(p, centre) <= radius && !hurt.count(p.id))
                hurt[p.id] += damage;
    }
    Fusion::Plots::Actor const by = PlotActor(owner);
    for (auto const& [id, damage] : hurt)
        DamagePiece(map, id, damage, DamageKind::Spell, by);
}

void FusionMgr::DeployStreakSentry(Player* player)
{
    using namespace Fusion::Build;
    Map* map = player->GetMap();
    if (!map || map->Instanceable())
        return;
    uint32 const now = WorldTimer::getMSTime();
    float const facing = player->GetOrientation();
    Fusion::Vec3 const feet = At(player);
    Piece p;
    p.type = PieceType::SentryGun;
    p.pos = Fusion::Defense::StreakSentrySpot(feet, facing);
    // On the ground in front (or at the player's feet when that is a drop or a wall).
    float const ground = map->GetHeight(p.pos.x, p.pos.y, p.pos.z + 2.f, true, 4.f);
    if (ground > INVALID_HEIGHT && std::fabs(ground - feet.z) < 1.5f
        && map->isInLineOfSight(feet.x, feet.y, feet.z + 1.f, p.pos.x, p.pos.y, ground + 1.f, true, true))
        p.pos.z = ground;
    else
        p.pos = feet;
    p.yaw = facing;
    p.map = map->GetId();
    p.owner = player->GetGUIDLow();
    p.health = MaxHealth(p.type, 0);
    // Never saved: it is gone in 90 s, or when broken, or at a restart.
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        p.id = m_nextPieceId++;
        m_pieces[p.map].push_back(p);
        m_streakSentries[p.id] = {p.map, now + Fusion::Defense::kStreakSentryMs};
    }
    Piece const placed = p;
    Broadcast(map, p.pos, [placed](Player* viewer) {
        FusionPlayerData& vd = viewer->GetFusion();
        if (vd.knownMap != placed.map)
            return std::vector<uint8_t>();
        vd.knownPieces.insert(placed.id);
        return EncodePieces({placed}, viewer->GetGUIDLow());
    });
    ChatHandler(player).PSendSysMessage("Sentry gun deployed: it covers your front for %u seconds.", Fusion::Defense::kStreakSentryMs / 1000);
}

void FusionMgr::ExpireStreakSentries(Map* map, uint32 now)
{
    if (!map)
        return;
    std::vector<uint32> done;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        for (auto const& [id, at] : m_streakSentries)
            if (at.first == map->GetId() && int32(now - at.second) >= 0)
                done.push_back(id);
    }
    for (uint32 id : done)
        RemovePieces(map, id, Fusion::Build::RemoveCause::TakenDown);
}

bool FusionMgr::IsStreakSentry(uint32 pieceId) const
{
    std::lock_guard<std::mutex> lock(m_buildLock);
    return m_streakSentries.count(pieceId) != 0;
}

// `.fusion world`: for testing (admins). With a creature selected, its danger (kind, health and
// damage multipliers, its health now) and whether it carries a gun (its camp and gun). `.fusion
// world sentry` sets a killstreak turret down in front of you, as the Sentry Gun reward does
// (`.streak sentry` hands the reward itself over).
bool ChatHandler::HandleFusionWorldCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may use .fusion world.");
        SetSentErrorMessage(true);
        return false;
    }
    std::istringstream in(args ? args : "");
    std::string what;
    in >> what;
    if (what == "sentry")
    {
        // The Sentry Gun killstreak's turret (FusionSkyStreaks.cpp), 3 yards in front, as if
        // pointed at.
        float const o = player->GetOrientation();
        Fusion::Vec3 const eye = At(player, 1.6f);
        Fusion::Vec3 const ground{player->GetPositionX() + 3.f * std::cos(o), player->GetPositionY() + 3.f * std::sin(o),
            player->GetPositionZ()};
        sFusionMgr.CallSkyStreak(player, Fusion::StreakId::SentryGun, eye, (ground - eye).Normalized());
        return true;
    }
    Creature* c = GetSelectedCreature();
    if (!c)
    {
        SendSysMessage("Usage: select a creature for its danger and gun, or .fusion world sentry");
        return true;
    }
    Fusion::GunFacts const f = sFusionMgr.GunFactsOf(c);
    Fusion::GunnerCamp const* camp = Fusion::CampOf(f.faction, f.name);
    char const* role = c->m_fusionGunRole == uint8(Fusion::GunRole::Gunner) ? "gunner"
        : c->m_fusionGunRole == uint8(Fusion::GunRole::MeleeTrash) ? "melee"
        : c->m_fusionGunRole == 0xFF ? "not yet classified" : "none";
    PSendSysMessage("%s (entry %u, level %u, faction %u%s): health %u / %u, hits x%.2f, gun x%.2f",
        c->GetName(), c->GetEntry(), c->GetLevel(), f.faction, f.boss ? ", a boss" : "", c->GetHealth(), c->GetMaxHealth(),
        sFusionMgr.CreatureDangerDamage(c, false), sFusionMgr.CreatureDangerDamage(c, true));
    PSendSysMessage("Guns: %s, camp %s, gun id %u.", role, camp ? camp->name : "none (wild)", uint32(c->m_fusionGunId));
    return true;
}
