/*
 * World of Modern Warfare: door and portal locks, portals and the shared hearthstone cooldown
 * (fusion/Locks.h, fusion/Portals.h) in the game. A door's or portal's owner sets its lock from a
 * small dialog (the build channel's op 4); anyone else meets it: their party or guild with the
 * owner's, a code on a keypad (op 5, rate-limited), or nothing. A portal (F on it, op 6) lists its
 * user's faction's capitals and their homestead; stepping through (op 7) shares the hearthstone's
 * cooldown, as the homestead courier's way back does (FusionConstruction.cpp). See
 * src/fusion/docs/CONSTRUCTION.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <random>

namespace
{
    namespace Locks = Fusion::Locks;
    namespace Portals = Fusion::Portals;
    using Fusion::Build::Piece;
    using Fusion::Build::Refusal;

    int64 Now() { return int64(time(nullptr)); }

    Fusion::Vec3 Feet(Player const* p) { return {p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()}; }

    // 16 bytes for a code's salt. Not secret, only never the same twice.
    std::vector<uint8_t> NewSalt()
    {
        static std::mutex lock;
        static std::random_device device;
        static std::mt19937_64 rng(device());
        std::lock_guard<std::mutex> guard(lock);
        std::vector<uint8_t> salt(16);
        uint64_t a = rng() ^ uint64_t(device()), b = rng() ^ (uint64_t(device()) << 32);
        for (int i = 0; i < 8; ++i)
        {
            salt[i] = uint8_t(a >> (8 * i));
            salt[8 + i] = uint8_t(b >> (8 * i));
        }
        return salt;
    }

    // Whether a door standing as `p` would shut a player inside it (as FusionMgr.cpp's check
    // for placing: Build.h Traps).
    bool TrapsAPlayer(Map* map, Piece const& p)
    {
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* q = it->getSource();
            if (!q || !q->IsInWorld() || q->GetTransport())
                continue;
            Fusion::Vec3 const feet = Feet(q);
            if ((feet - p.pos).Length() <= 60.f && Fusion::Build::Traps(p, feet))
                return true;
        }
        return false;
    }

    Fusion::Team TeamOf(Player const* p)
    {
        switch (p->GetTeam())
        {
            case ALLIANCE: return Fusion::Team::Alliance;
            case HORDE: return Fusion::Team::Horde;
            default: return Fusion::Team::Neutral;
        }
    }
}

// ── The shared hearthstone cooldown ─────────────────────────────────────────────────────────

uint32 FusionMgr::HearthCooldownLeft(Player* player) const
{
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(Portals::kHearthSpell);
    if (!spell)
        return 0;
    return player->GetSpellCooldownLeftMs(spell, sObjectMgr.GetItemPrototype(Portals::kHearthItem));
}

void FusionMgr::StartHearthCooldown(Player* player)
{
    if (IsAdmin(player))
        return;
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(Portals::kHearthSpell);
    if (!spell || HearthCooldownLeft(player))
        return;
    // As the hearthstone itself starts it (Player::TeleportToHomebind), and the client's button
    // shown the same wait.
    player->AddCooldown(spell, sObjectMgr.GetItemPrototype(Portals::kHearthItem));
    if (uint32 const left = HearthCooldownLeft(player))
        player->SendSpellCooldown(Portals::kHearthSpell, Milliseconds(left), player->GetObjectGuid());
}

// ── Locks ───────────────────────────────────────────────────────────────────────────────────

Locks::Verdict FusionMgr::LockVerdict(Player const* player, Piece const& piece)
{
    Locks::Visitor v;
    v.gm = IsAdmin(player);
    uint32 const me = player->GetGUIDLow();
    ObjectGuid const owner(HIGHGUID_PLAYER, piece.owner);
    // The owner, or another character of the owner's account (alts are one player).
    v.owner = piece.owner == me
        || (piece.owner && player->GetSession() && sObjectMgr.GetPlayerAccountIdByGUID(owner) == player->GetSession()->GetAccountId());
    // On a plot its owner and those they permit pass every lock (Plots::CanUseDoor).
    v.household = PlotAllowsDoor(player, piece);
    if (Group const* group = player->GetGroup())
        v.party = piece.owner && group->IsMember(owner);
    if (uint32 const guild = player->GetGuildId())
        if (Guild* theirs = sGuildMgr.GetPlayerGuild(piece.owner))
            v.guild = theirs->GetId() == guild;
    if (piece.lock == uint8(Locks::Mode::Password))
    {
        auto secret = m_lockSecrets.find(piece.id);
        FusionPlayerData const& d = player->GetFusionConst();
        auto known = d.codesKnown.find(piece.id);
        v.knowsCode = secret != m_lockSecrets.end() && known != d.codesKnown.end() && known->second == secret->second.hash;
    }
    return Locks::MayOpen(Locks::ValidMode(piece.lock) ? Locks::Mode(piece.lock) : Locks::kDefault, v);
}

void FusionMgr::SendLockRefusal(Player* player, Piece const& piece, Locks::Verdict v)
{
    if (v == Locks::Verdict::NeedsCode)
        SendChannel(player, Fusion::Packets::kChannelBuild, Locks::EncodeAskCode(piece.id, Fusion::Build::IsPortal(piece.type)));
    else if (v == Locks::Verdict::Locked)
        SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(Refusal::Locked));
}

bool FusionMgr::LockablePieceNear(Player* player, uint32 pieceId, float reach, Piece& out)
{
    Map* map = player->GetMap();
    if (!map)
        return false;
    std::lock_guard<std::mutex> lock(m_buildLock);
    auto it = m_pieces.find(map->GetId());
    if (it == m_pieces.end())
        return false;
    auto pit = std::find_if(it->second.begin(), it->second.end(), [pieceId](Piece const& p) { return p.id == pieceId; });
    if (pit == it->second.end() || !Locks::Lockable(pit->type))
        return false;
    out = *pit;
    Fusion::Vec3 const eye = Feet(player) + Fusion::Vec3{0.f, 0.f, 1.f};
    return Fusion::Build::Distance(*pit, eye) <= reach;
}

void FusionMgr::SwingDoor(Player* player, uint32 id)
{
    using namespace Fusion::Build;
    Map* map = player->GetMap();
    if (!map)
        return;
    Piece door;
    Piece refused;
    bool found = false;
    bool blocked = false;
    Locks::Verdict verdict = Locks::Verdict::Open;
    std::optional<Refusal> why;     // a door there, but not for this player now
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        for (Piece& q : list)
        {
            if (q.id != id || BaseOf(q.type) != PieceType::WoodDoor)
                continue;
            if ((Center(q) - (Feet(player) + Fusion::Vec3{0.f, 0.f, 1.f})).Length() > kDoorReach)
            {
                why = Refusal::TooFar;
                continue;
            }
            // Its lock (and the plot under it) has its say.
            verdict = LockVerdict(player, q);
            if (verdict != Locks::Verdict::Open)
            {
                refused = q;
                continue;
            }
            // A door is not shut on anyone standing in its way.
            Piece shut = q;
            shut.open = false;
            if (q.open && TrapsAPlayer(map, shut))
            {
                blocked = true;
                continue;
            }
            q.open = !q.open;
            door = q;
            found = true;
        }
    }
    if (verdict != Locks::Verdict::Open)
        return SendLockRefusal(player, refused, verdict);
    if (blocked)
        SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(Refusal::Traps));
    else if (!found && why)
        SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(*why));
    if (found)
    {
        uint32 const mapId = map->GetId();
        Broadcast(map, door.pos, [&](Player* viewer) {
            if (viewer->GetFusionConst().knownMap != mapId)
                return std::vector<uint8_t>();
            return EncodePieces({door}, viewer->GetGUIDLow());
        });
    }
}

void FusionMgr::HandleLockChannel(Player* player, std::vector<uint8> const& body)
{
    if (body.empty() || !player->IsInWorld())
        return;
    switch (body[0])
    {
        case Locks::kOpSetLock:
        {
            Locks::SetLock set;
            if (Locks::DecodeSetLock(body, set))
                SetPieceLock(player, set);
            break;
        }
        case Locks::kOpTryCode:
        {
            uint32 piece = 0;
            std::string code;
            if (Locks::DecodeTryCode(body, piece, code))
                TryLockCode(player, piece, code);
            break;
        }
        case Portals::kOpAsk:
        {
            uint32 piece = 0;
            if (Portals::DecodeAsk(body, piece))
                OpenPortalMenu(player, piece);
            break;
        }
        case Portals::kOpTravel:
        {
            uint32 piece = 0;
            uint8_t destination = 0;
            if (Portals::DecodeTravel(body, piece, destination))
                UsePortal(player, piece, destination);
            break;
        }
        default:
            break;
    }
}

void FusionMgr::SetPieceLock(Player* player, Locks::SetLock const& set)
{
    auto refuse = [&](Refusal why) { SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(why)); };
    Piece piece;
    if (!LockablePieceNear(player, set.piece, Fusion::Build::kReach + 5.f, piece))
        return refuse(Refusal::TooFar);
    // Its owner (or an alt of theirs), the owner of the plot it stands on, or a game master.
    bool const gm = IsAdmin(player);
    uint32 const account = player->GetSession()->GetAccountId();
    bool const owner = piece.owner == player->GetGUIDLow()
        || (piece.owner && sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, piece.owner)) == account);
    bool plotOwner = false;
    if (uint32 const plot = Fusion::Plots::PlotOf(PlotGrid(), piece))
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        plotOwner = Fusion::Plots::AccessOf(PlotActor(player), m_plots.Find(plot), Now()) >= Fusion::Plots::Access::Owner;
    }
    if (!Locks::MaySet(owner, plotOwner, gm))
        return refuse(Refusal::NotYours);

    Locks::Secret secret;
    bool keep = false;
    if (set.mode == Locks::Mode::Password)
    {
        if (set.code.empty())
        {
            // No new code: a code lock keeps the one it has; anything else must be given one.
            std::lock_guard<std::mutex> lock(m_buildLock);
            auto it = m_lockSecrets.find(piece.id);
            if (piece.lock != uint8(Locks::Mode::Password) || it == m_lockSecrets.end())
                return refuse(Refusal::WrongCode);
            keep = true;
        }
        else
        {
            if (!Locks::ValidCode(set.code))
                return refuse(Refusal::WrongCode);
            secret = Locks::MakeSecret(set.code, NewSalt());
            if (!secret.Set())
                return refuse(Refusal::Unknown);
        }
    }
    Piece changed;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[player->GetMapId()];
        auto pit = std::find_if(list.begin(), list.end(), [&](Piece const& p) { return p.id == piece.id; });
        if (pit == list.end())
            return;
        pit->lock = uint8(set.mode);
        if (set.mode != Locks::Mode::Password)
            m_lockSecrets.erase(piece.id);
        else if (!keep)
            m_lockSecrets[piece.id] = secret;
        changed = *pit;
    }
    if (set.mode != Locks::Mode::Password)
        CharacterDatabase.PExecute("UPDATE `fusion_build_piece` SET `lock_mode` = %u, `lock_salt` = '', `lock_hash` = '' WHERE `id` = %u",
            uint32(set.mode), piece.id);
    else if (!keep)
        // Hex only (Locks::MakeSecret): nothing to escape.
        CharacterDatabase.PExecute("UPDATE `fusion_build_piece` SET `lock_mode` = %u, `lock_salt` = '%s', `lock_hash` = '%s' WHERE `id` = %u",
            uint32(set.mode), secret.salt.c_str(), secret.hash.c_str(), piece.id);
    // The setter knows the code they just gave.
    if (set.mode == Locks::Mode::Password && !keep)
        player->GetFusion().codesKnown[piece.id] = secret.hash;
    // Everyone near sees its new mode (only the mode: never a code).
    uint32 const mapId = player->GetMapId();
    Broadcast(player->GetMap(), changed.pos, [&](Player* viewer) {
        if (viewer->GetFusionConst().knownMap != mapId)
            return std::vector<uint8_t>();
        return Fusion::Build::EncodePieces({changed}, viewer->GetGUIDLow());
    });
}

void FusionMgr::TryLockCode(Player* player, uint32 pieceId, std::string const& code)
{
    auto refuse = [&](Refusal why) { SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(why)); };
    Piece piece;
    // A little more than a door's reach or a portal's use: the keypad was opened standing there.
    if (!LockablePieceNear(player, pieceId, Portals::kUseReach + 1.5f, piece))
        return refuse(Refusal::TooFar);
    bool const portal = Fusion::Build::IsPortal(piece.type);
    if (piece.lock != uint8(Locks::Mode::Password))
    {
        // Not a code lock (any more): as a plain use.
        if (portal)
            OpenPortalMenu(player, pieceId);
        else
            SwingDoor(player, pieceId);
        return;
    }
    Locks::Secret secret;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_lockSecrets.find(pieceId);
        if (it == m_lockSecrets.end())
            return refuse(Refusal::Locked);
        secret = it->second;
    }
    int64 const t = Now();
    uint64_t const who = Locks::PlayerKey(player->GetGUIDLow());
    uint64_t const what = Locks::LockKey(pieceId);
    {
        std::lock_guard<std::mutex> lock(m_codeLock);
        m_codeLimiter.Forget(t);
        if (m_codeLimiter.WaitFor(who, t) > 0 || m_codeLimiter.WaitFor(what, t) > 0)
            return refuse(Refusal::TooManyTries);
    }
    if (!Locks::CheckCode(secret, code))
    {
        int64 wait = 0;
        {
            std::lock_guard<std::mutex> lock(m_codeLock);
            wait = std::max(m_codeLimiter.Miss(who, t), m_codeLimiter.Miss(what, t));
        }
        if (wait >= Locks::kMaxLockout)
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %s (account %u) keeps trying codes at lock %u.",
                player->GetName(), player->GetSession()->GetAccountId(), pieceId);
        return refuse(wait > 0 ? Refusal::TooManyTries : Refusal::WrongCode);
    }
    {
        std::lock_guard<std::mutex> lock(m_codeLock);
        m_codeLimiter.Clear(who);
        m_codeLimiter.Clear(what);
    }
    player->GetFusion().codesKnown[pieceId] = secret.hash;
    if (portal)
        OpenPortalMenu(player, pieceId);
    else
        SwingDoor(player, pieceId);
}

// ── Portals ─────────────────────────────────────────────────────────────────────────────────

void FusionMgr::OpenPortalMenu(Player* player, uint32 pieceId)
{
    Piece piece;
    if (!LockablePieceNear(player, pieceId, Portals::kUseReach + 1.f, piece) || !Fusion::Build::IsPortal(piece.type))
        return SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(Refusal::TooFar));
    Locks::Verdict verdict;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        verdict = LockVerdict(player, piece);
    }
    if (verdict != Locks::Verdict::Open)
        return SendLockRefusal(player, piece, verdict);
    bool home = false;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        home = m_plots.OwnedBy(player->GetSession()->GetAccountId()) != nullptr;
    }
    Portals::Menu m;
    m.piece = pieceId;
    m.cooldownSeconds = (HearthCooldownLeft(player) + 999) / 1000;
    m.choices = Portals::ChoicesFor(TeamOf(player), home);
    SendChannel(player, Fusion::Packets::kChannelBuild, Portals::EncodeMenu(m));
}

void FusionMgr::UsePortal(Player* player, uint32 pieceId, uint8 destination)
{
    auto refuse = [&](Refusal why) { SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(why)); };
    Piece piece;
    if (!LockablePieceNear(player, pieceId, Portals::kUseReach + 1.f, piece) || !Fusion::Build::IsPortal(piece.type))
        return refuse(Refusal::TooFar);
    Locks::Verdict verdict;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        verdict = LockVerdict(player, piece);
    }
    if (verdict != Locks::Verdict::Open)
        return SendLockRefusal(player, piece, verdict);
    bool home = false;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        home = m_plots.OwnedBy(player->GetSession()->GetAccountId()) != nullptr;
    }
    // The server's say, whatever the list showed: never an enemy capital.
    if (!Portals::Allowed(TeamOf(player), destination, home))
        return refuse(Refusal::Unknown);
    Portals::Traveller t;
    t.alive = player->IsAlive();
    t.inCombat = player->IsInCombat();
    t.travelling = player->IsTaxiFlying() || player->GetTransport() || player->IsBeingTeleported();
    t.cooldownLeftMs = IsAdmin(player) ? 0 : HearthCooldownLeft(player);
    switch (Portals::CanTravel(t))
    {
        case Portals::TravelCheck::Ok:
            break;
        case Portals::TravelCheck::CoolingDown:
            return refuse(Refusal::CoolingDown);
        case Portals::TravelCheck::InCombat:
            return refuse(Refusal::InCombat);
        default:
            return refuse(Refusal::Unknown);
    }
    if (destination == Portals::kHomestead)
    {
        // As the steward's "take me to my homestead": the plot's arrival spot.
        Said const went = TeleportToPlot(player);
        if (!went.ok)
            return refuse(Refusal::Unknown);
        StartHearthCooldown(player);
        return;
    }
    Portals::Destination const* d = Portals::CapitalById(destination);
    if (!d)
        return refuse(Refusal::Unknown);
    uint32 map = d->map;
    float x = d->x, y = d->y, z = d->z, o = d->o;
    // Where the mages' own teleport lands, when the world DB has it.
    if (SpellTargetPosition const* at = sSpellMgr.GetSpellTargetPosition(d->spell))
    {
        map = at->mapId;
        x = at->x;
        y = at->y;
        z = at->z;
        o = at->o;
    }
    StartHearthCooldown(player);
    player->TeleportTo(map, x, y, z, o);
}
