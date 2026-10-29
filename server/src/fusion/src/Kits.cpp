/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Kits.h"
#include "fusion/Homestead.h"

#include <algorithm>

namespace Fusion
{
    bool IsKitStation(uint32_t npcFlags)
    {
        return (npcFlags & (kNpcFlagTrainer | kNpcFlagInnkeeper)) != 0;
    }

    bool KitStationAllows(uint32_t npcFlags, bool alive, bool hostile, float distance)
    {
        return IsKitStation(npcFlags) && alive && !hostile && distance >= 0.f && distance <= kKitStationReach;
    }

    KitAccess KitAccessOf(bool isGm, bool atStation, bool fieldOpen)
    {
        if (atStation)
            return KitAccess::Station;
        if (fieldOpen)
            return KitAccess::Field;
        return isGm ? KitAccess::Gm : KitAccess::View;
    }

    bool CanChangeKits(KitAccess a)
    {
        return a != KitAccess::View;
    }

    bool KitRequestChanges(bool saves, uint8_t equipSlot, uint8_t equippedSlot)
    {
        return saves || (equipSlot != kNoClass && equipSlot != equippedSlot);
    }

    bool OpenerMayEdit(KitOpener o)
    {
        return o == KitOpener::Station || o == KitOpener::Field;
    }

    std::vector<uint8_t> OpenKitEditor(KitOpener opener, uint16_t fieldSeconds)
    {
        std::vector<uint8_t> out = {kOpOpenClasses, uint8_t(opener)};
        if (opener == KitOpener::Field)
        {
            out.push_back(uint8_t(fieldSeconds));
            out.push_back(uint8_t(fieldSeconds >> 8));
        }
        return out;
    }

    bool DecodeOpenKitEditor(std::vector<uint8_t> const& body, KitOpener& opener, uint16_t& fieldSeconds)
    {
        if (body.size() < 2 || body[0] != kOpOpenClasses || body[1] > uint8_t(KitOpener::Field))
            return false;
        opener = KitOpener(body[1]);
        fieldSeconds = 0;
        if (opener == KitOpener::Field)
        {
            if (body.size() != 4)
                return false;
            fieldSeconds = uint16_t(body[2] | body[3] << 8);
            return true;
        }
        return body.size() == 2;
    }

    FieldEditorUse UseFieldEditor(FieldEditor& f, uint64_t now)
    {
        if (now < f.readyAt)
            return FieldEditorUse::OnCooldown;
        // Already open: the window runs on, not anew (it is not a way round the cooldown).
        if (FieldEditorOpen(f, now))
            return FieldEditorUse::Opened;
        f.openUntil = now + kFieldEditorWindowSeconds;
        f.spent = false;
        return FieldEditorUse::Opened;
    }

    bool FieldEditorOpen(FieldEditor const& f, uint64_t now)
    {
        return now < f.openUntil;
    }

    StationRearm PlanStationRearm(uint32_t money, uint32_t fee)
    {
        return money >= fee ? StationRearm::Rounds : StationRearm::NoMoney;
    }

    std::string RearmGossipText(uint32_t fee)
    {
        return "Rearm: ammunition, equipment and special rounds (" + Homestead::MoneyText(fee) + ")";
    }

    std::string RearmResultText(StationRearm r, uint32_t fee)
    {
        if (r == StationRearm::Rounds)
            return "Rearmed: ammunition and equipment refilled, special rounds topped up for " + Homestead::MoneyText(fee) + ".";
        return "Rearmed: ammunition and equipment refilled. Special rounds cost " + Homestead::MoneyText(fee)
            + ", more than you have.";
    }

    bool SpendFieldEditor(FieldEditor& f, uint64_t now, uint32_t cooldown)
    {
        if (f.spent)
            return false;
        f.spent = true;
        f.readyAt = now + cooldown;
        return true;
    }

    uint32_t FieldEditorSecondsLeft(FieldEditor const& f, uint64_t now)
    {
        return now < f.readyAt ? uint32_t(f.readyAt - now) : 0;
    }

    uint32_t FieldEditorWindowLeft(FieldEditor const& f, uint64_t now)
    {
        return now < f.openUntil ? uint32_t(f.openUntil - now) : 0;
    }

    bool DecodeUseLedger(std::vector<uint8_t> const& body, uint64_t& guid)
    {
        if (body.size() != 9 || body[0] != kOpUseLedger)
            return false;
        guid = 0;
        for (int i = 0; i < 8; ++i)
            guid |= uint64_t(body[1 + i]) << (8 * i);
        return true;
    }

    KitStatus KitStatusOf(KitAccess access, FieldEditor const& f, uint64_t now)
    {
        KitStatus s;
        if (CanChangeKits(access))
            s.flags |= kKitStatusCanEdit;
        if (FieldEditorOpen(f, now))
            s.flags |= kKitStatusFieldOpen;
        if (access == KitAccess::Gm)
            s.flags |= kKitStatusGm;
        s.fieldReadyIn = FieldEditorSecondsLeft(f, now);
        s.fieldOpenFor = uint16_t(std::min<uint32_t>(FieldEditorWindowLeft(f, now), 0xFFFF));
        return s;
    }

    std::vector<uint8_t> EncodeKitStatus(KitStatus const& s)
    {
        std::vector<uint8_t> out = {kOpKitStatus, s.flags};
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(s.fieldReadyIn >> (8 * i)));
        out.push_back(uint8_t(s.fieldOpenFor));
        out.push_back(uint8_t(s.fieldOpenFor >> 8));
        return out;
    }

    bool DecodeKitStatus(std::vector<uint8_t> const& body, KitStatus& out)
    {
        if (body.size() != 8 || body[0] != kOpKitStatus)
            return false;
        out.flags = body[1];
        out.fieldReadyIn = 0;
        for (int i = 0; i < 4; ++i)
            out.fieldReadyIn |= uint32_t(body[2 + i]) << (8 * i);
        out.fieldOpenFor = uint16_t(body[6] | body[7] << 8);
        return true;
    }

    std::vector<StreakReward> KitStreaks(ClassLoadout const* kit, std::vector<StreakReward> const& ladder,
        UnlockSet const& unlocks)
    {
        std::vector<StreakReward> out;
        bool const chose = kit && (kit->streak1 || kit->streak2 || kit->streak3);
        for (StreakReward const& r : ladder)
        {
            if (!unlocks.IsKillstreakUnlocked(r.id))
                continue;
            if (chose && r.id != kit->streak1 && r.id != kit->streak2 && r.id != kit->streak3)
                continue;
            out.push_back(r);
        }
        return out;
    }
}
