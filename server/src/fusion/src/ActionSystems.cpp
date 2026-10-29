/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/ActionSystems.h"

namespace Fusion::ActionSystems
{
    Range RangeOf(System s)
    {
        switch (s)
        {
            case System::Classic: return {0, 59};
            case System::Utility: return {60, 71};
            case System::Modern: default: return {192, 209};
        }
    }

    std::optional<System> SystemOfButton(uint16_t button)
    {
        for (System s : {System::Modern, System::Classic, System::Utility})
            if (RangeOf(s).Holds(button))
                return s;
        if (kUtilityExtra.Holds(button))
            return System::Utility;
        return std::nullopt;
    }

    bool KnownAbility(uint32_t id)
    {
        return id >= uint32_t(Ability::Primary) && id <= kLastAbility;
    }

    bool AbilityOn(uint32_t id, System s)
    {
        if (!KnownAbility(id) || IsCompanionSlot(id))
            return false;
        if (id == uint32_t(Ability::BareHands))
            return s == System::Classic || s == System::Utility;
        if (IsTool(id) || id == uint32_t(Ability::BuildStructure))
            return s == System::Utility;
        return s == System::Modern;
    }

    bool ButtonAllows(uint16_t button, uint32_t action, uint8_t type)
    {
        if (button >= kButtons)
            return false;
        if (kCompanionBar.Holds(button))
            return type == kFusionButtonType && IsCompanionSlot(action);
        std::optional<System> const s = SystemOfButton(button);
        if (type == kFusionButtonType)
            return s && AbilityOn(action, *s);
        return !s || *s == System::Classic;
    }

    std::optional<System> DecodeSystem(std::vector<uint8_t> const& body)
    {
        if (body.size() != 2 || body[0] != kOpSystem || body[1] > uint8_t(System::Utility))
            return std::nullopt;
        return System(body[1]);
    }

    std::vector<uint8_t> EncodeSystem(System s)
    {
        return {kOpSystem, uint8_t(s)};
    }

    std::vector<uint8_t> EncodeTools(uint16_t mask)
    {
        mask &= kAllTools;
        return {kOpTools, uint8_t(mask & 0xFF), uint8_t(mask >> 8)};
    }

    std::optional<uint16_t> DecodeTools(std::vector<uint8_t> const& body)
    {
        if (body.size() != 3 || body[0] != kOpTools)
            return std::nullopt;
        uint16_t const mask = uint16_t(body[1] | (body[2] << 8));
        if (mask & ~kAllTools)
            return std::nullopt;
        return mask;
    }

    std::vector<uint8_t> EncodePose(Pose const& p)
    {
        return {kOpPose, p.system, p.flags, p.stance, uint8_t(p.pitch), uint8_t(p.gun & 0xFF),
            uint8_t(p.gun >> 8), p.mainSubclass, p.offSubclass, p.tool};
    }

    std::optional<Pose> DecodePose(std::vector<uint8_t> const& body)
    {
        if (body.size() != kPoseBytes + 1 || body[0] != kOpPose)
            return std::nullopt;
        Pose p;
        p.system = body[1];
        p.flags = body[2];
        p.stance = body[3];
        p.pitch = int8_t(body[4]);
        p.gun = uint16_t(body[5] | (body[6] << 8));
        p.mainSubclass = body[7];
        p.offSubclass = body[8];
        p.tool = body[9];
        // Stand, crouch, prone, slide (Packets.h Stance).
        if (p.system > uint8_t(System::Utility) || p.stance > 3 || p.pitch < -90 || p.pitch > 90)
            return std::nullopt;
        return p;
    }

    std::vector<uint8_t> EncodePoseOf(uint64_t guid, Pose const& p)
    {
        std::vector<uint8_t> out{kOpPoseOf};
        for (int i = 0; i < 8; ++i)
            out.push_back(uint8_t(guid >> (8 * i)));
        std::vector<uint8_t> const body = EncodePose(p);
        out.insert(out.end(), body.begin() + 1, body.end());
        return out;
    }

    std::optional<Act> DecodeAct(std::vector<uint8_t> const& body)
    {
        if (body.size() != 3 || body[0] != kOpAct || body[1] == 0 || body[1] > kActLast)
            return std::nullopt;
        return Act{body[1], body[2]};
    }

    std::vector<uint8_t> EncodeActOf(uint64_t guid, Act a)
    {
        std::vector<uint8_t> out{kOpActOf};
        for (int i = 0; i < 8; ++i)
            out.push_back(uint8_t(guid >> (8 * i)));
        out.push_back(a.kind);
        out.push_back(a.arg);
        return out;
    }

    std::optional<uint32_t> DecodeTool(std::vector<uint8_t> const& body)
    {
        if (body.size() != 2 || body[0] != kOpTool || (body[1] != 0 && !IsTool(body[1])))
            return std::nullopt;
        return uint32_t(body[1]);
    }

    std::vector<uint8_t> EncodeTool(uint32_t tool)
    {
        return {kOpTool, uint8_t(tool)};
    }

    std::optional<StrikeRequest> DecodeStrike(std::vector<uint8_t> const& body)
    {
        if (body.size() != 10 || body[0] != kOpStrike || !IsTool(body[1]))
            return std::nullopt;
        StrikeRequest r;
        r.tool = body[1];
        for (int i = 0; i < 8; ++i)
            r.target |= uint64_t(body[2 + i]) << (8 * i);
        return r;
    }

    std::vector<uint8_t> EncodeStrike(StrikeRequest const& r)
    {
        std::vector<uint8_t> o{kOpStrike, uint8_t(r.tool)};
        for (int i = 0; i < 8; ++i)
            o.push_back(uint8_t(r.target >> (8 * i)));
        return o;
    }

    std::vector<uint8_t> EncodeStruck(uint32_t tool, uint8_t outcome, uint8_t done, uint8_t needed)
    {
        return {kOpStruck, uint8_t(tool), outcome, done, needed};
    }
}
