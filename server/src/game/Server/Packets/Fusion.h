/*
 * Azeroth Warfare: the fusion's client packets (opcodes 828 and 830). The wire format and its
 * decoder live in the standalone rules library (fusion/Packets.h), shared with its tests.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef MANGOS_PACKETS_FUSION_H
#define MANGOS_PACKETS_FUSION_H

#include "Packet.h"
#include "fusion/Packets.h"

namespace WorldPackets { namespace Fusion
{
    class Shots final : public ClientPacket
    {
    public:
        ::Fusion::Packets::ShotBatch batch;
        bool valid = false;

        explicit Shots() : ClientPacket(CMSG_FUSION_SHOTS) {}
        void ReadFromWorldPacket(WorldPacket& recv_data) override;
    };

    class State final : public ClientPacket
    {
    public:
        ::Fusion::Packets::State state;
        bool valid = false;

        explicit State() : ClientPacket(CMSG_FUSION_STATE) {}
        void ReadFromWorldPacket(WorldPacket& recv_data) override;
    };

    class Channel final : public ClientPacket
    {
    public:
        ::Fusion::Packets::ChannelMessage message;
        bool valid = false;

        explicit Channel() : ClientPacket(CMSG_FUSION_CHANNEL) {}
        void ReadFromWorldPacket(WorldPacket& recv_data) override;
    };

    class Loadout final : public ClientPacket
    {
    public:
        ::Fusion::Packets::LoadoutRequest request;
        bool valid = false;

        explicit Loadout() : ClientPacket(CMSG_FUSION_LOADOUT) {}
        void ReadFromWorldPacket(WorldPacket& recv_data) override;
    };
}}

#endif
