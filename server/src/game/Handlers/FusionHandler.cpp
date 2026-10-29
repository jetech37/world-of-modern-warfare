/*
 * Azeroth Warfare: the fusion's opcode handlers. GPL-2.0-or-later, see Fusion/FusionMgr.h.
 */

#include "WorldSession.h"
#include "Player.h"
#include "Log.h"
#include "Fusion/FusionMgr.h"

void WorldSession::HandleFusionShotsOpcode(WorldPackets::Fusion::Shots const& packet)
{
    if (!sFusionMgr.IsEnabled())
        return;
    if (!packet.valid)
    {
        sLog.Out(LOG_NETWORK, LOG_LVL_DEBUG, "Fusion: malformed CMSG_FUSION_SHOTS from %s", GetPlayerName());
        return;
    }
    sFusionMgr.HandleShots(GetPlayer(), packet.batch);
}

void WorldSession::HandleFusionStateOpcode(WorldPackets::Fusion::State const& packet)
{
    if (!sFusionMgr.IsEnabled() || !packet.valid)
        return;
    sFusionMgr.HandleState(GetPlayer(), packet.state);
}

void WorldSession::HandleFusionLoadoutOpcode(WorldPackets::Fusion::Loadout const& packet)
{
    if (!sFusionMgr.IsEnabled())
        return;
    if (!packet.valid)
    {
        sLog.Out(LOG_NETWORK, LOG_LVL_DEBUG, "Fusion: malformed CMSG_FUSION_LOADOUT from %s", GetPlayerName());
        return;
    }
    sFusionMgr.HandleLoadout(GetPlayer(), packet.request);
}

void WorldSession::HandleFusionChannelOpcode(WorldPackets::Fusion::Channel const& packet)
{
    if (!sFusionMgr.IsEnabled() || !packet.valid)
        return;
    sFusionMgr.HandleChannel(GetPlayer(), packet.message.channel, packet.message.body);
}
