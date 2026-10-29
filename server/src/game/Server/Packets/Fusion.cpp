/*
 * Azeroth Warfare: the fusion's client packets. GPL-2.0-or-later, see Fusion.h.
 */

#include "Fusion.h"
#include "WorldPacket.h"

namespace
{
    // The whole remaining body: the rules library decodes and validates it.
    std::vector<uint8_t> TakeBody(WorldPacket& recv_data)
    {
        size_t const from = recv_data.rpos();
        if (from >= recv_data.size())
            return {};
        std::vector<uint8_t> body(recv_data.contents() + from, recv_data.contents() + recv_data.size());
        recv_data.rpos(recv_data.size());
        return body;
    }
}

void WorldPackets::Fusion::Shots::ReadFromWorldPacket(WorldPacket& recv_data)
{
    valid = ::Fusion::Packets::Decode(TakeBody(recv_data), batch);
}

void WorldPackets::Fusion::State::ReadFromWorldPacket(WorldPacket& recv_data)
{
    valid = ::Fusion::Packets::Decode(TakeBody(recv_data), state);
}

void WorldPackets::Fusion::Loadout::ReadFromWorldPacket(WorldPacket& recv_data)
{
    valid = ::Fusion::Packets::Decode(TakeBody(recv_data), request);
}

void WorldPackets::Fusion::Channel::ReadFromWorldPacket(WorldPacket& recv_data)
{
    valid = ::Fusion::Packets::Decode(TakeBody(recv_data), message);
}
