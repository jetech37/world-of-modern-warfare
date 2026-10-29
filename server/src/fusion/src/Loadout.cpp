/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Loadout.h"
#include "fusion/Access.h"
#include "fusion/Killstreak.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace Fusion
{
    namespace
    {
        std::vector<std::string> Attachments(std::initializer_list<std::string const*> slots)
        {
            std::vector<std::string> out;
            for (std::string const* s : slots)
                if (!s->empty())
                    out.push_back(*s);
            return out;
        }

        bool Incompatible(Catalog const& catalog, std::string const& a, std::string const& b)
        {
            if (a == b)
                return true;
            for (auto [x, y] : {std::pair{&a, &b}, std::pair{&b, &a}})
                if (Attachment const* att = catalog.FindAttachment(*x))
                    if (std::find(att->incompatible.begin(), att->incompatible.end(), *y) != att->incompatible.end())
                        return true;
            return false;
        }

        LoadoutError CheckGun(Catalog const& catalog, uint16_t id, WeaponSlot slot,
            std::vector<std::string> const& atts, uint32_t level, bool isGm, uint8_t room)
        {
            WeaponProfile const* gun = catalog.Weapons().Find(id);
            if (!gun)
                return LoadoutError::UnknownWeapon;
            if (gun->slot != slot)
                return LoadoutError::WrongSlot;
            if (!CanUseWeapon(level, isGm, *gun))
                return LoadoutError::Locked;
            for (std::string const& id : atts)
            {
                Attachment const* a = catalog.FindAttachment(id);
                if (!a)
                    return LoadoutError::UnknownAttachment;
                if (std::find(gun->attachments.begin(), gun->attachments.end(), id) == gun->attachments.end())
                    return LoadoutError::AttachmentNotOnGun;
                if (EffectiveLevel(level, isGm) < UnlockLevel(a->mw2UnlockRank))
                    return LoadoutError::Locked;
            }
            // More than one takes Tinker's Rails (MW2's Bling), a third two, a fourth three.
            if (atts.size() > room)
                return LoadoutError::TwoAttachmentsNeedBling;
            // No two of one kind (two sights, two underbarrels), none the other can't go with.
            for (size_t i = 0; i < atts.size(); ++i)
                for (size_t j = i + 1; j < atts.size(); ++j)
                {
                    AttachmentKind const kind = KindOfAttachment(atts[i]);
                    bool const sameKind = kind != AttachmentKind::Other && kind == KindOfAttachment(atts[j]);
                    if (sameKind || Incompatible(catalog, atts[i], atts[j]))
                        return LoadoutError::IncompatibleAttachments;
                }
            // Up to two: a weapon definition the gun has. Three or four (MW2 has no such
            // definitions): every two of them one.
            if (atts.size() <= 2)
            {
                if (catalog.VariantName(*gun, atts).empty())
                    return LoadoutError::AttachmentNotOnGun;
            }
            else
            {
                for (size_t i = 0; i < atts.size(); ++i)
                    for (size_t j = i + 1; j < atts.size(); ++j)
                        if (catalog.VariantName(*gun, {atts[i], atts[j]}).empty())
                            return LoadoutError::AttachmentNotOnGun;
            }
            return LoadoutError::None;
        }
    }

    uint8_t PerkCount(ClassLoadout const& l, uint16_t perk)
    {
        if (!perk)
            return 0;
        return uint8_t((l.perk1 == perk) + (l.perk2 == perk) + (l.perk3 == perk));
    }

    bool PerkSlotPro(ClassLoadout const& l, uint16_t perk)
    {
        uint16_t const slots[3] = {l.perk1, l.perk2, l.perk3};
        for (int i = 0; i < 3; ++i)
            if (perk && slots[i] == perk && (l.perkPro & (1u << i)))
                return true;
        return false;
    }

    AttachmentRoom AttachmentsAllowed(ClassLoadout const& l, bool allPro)
    {
        uint8_t const rails = PerkCount(l, PerkId::Bling);
        AttachmentRoom room;
        room.primary = uint8_t(std::min<int>(kMaxPrimaryAttachments, 1 + rails));
        bool const pro = rails > 0 && (allPro || PerkSlotPro(l, PerkId::Bling));
        room.secondary = uint8_t(std::min<int>(kMaxSecondaryAttachments, 1 + (pro ? 1 : 0)));
        return room;
    }

    std::vector<std::string> PrimaryAttachments(ClassLoadout const& l)
    {
        return Attachments({&l.primaryAtt1, &l.primaryAtt2, &l.primaryAtt3, &l.primaryAtt4});
    }

    std::vector<std::string> SecondaryAttachments(ClassLoadout const& l)
    {
        return Attachments({&l.secondaryAtt, &l.secondaryAtt2});
    }

    std::string CleanClassName(std::string const& name)
    {
        std::string out;
        size_t i = 0;
        while (i < name.size())
        {
            uint8_t const b = uint8_t(name[i]);
            // The sequence's length and its second byte's range (RFC 3629: no overlongs, no
            // surrogates, nothing past U+10FFFF).
            size_t len = 0;
            uint8_t lo = 0x80, hi = 0xBF;
            if (b < 0x80)
                len = 1;
            else if (b >= 0xC2 && b <= 0xDF)
                len = 2;
            else if (b >= 0xE0 && b <= 0xEF)
            {
                len = 3;
                lo = b == 0xE0 ? 0xA0 : 0x80;
                hi = b == 0xED ? 0x9F : 0xBF;
            }
            else if (b >= 0xF0 && b <= 0xF4)
            {
                len = 4;
                lo = b == 0xF0 ? 0x90 : 0x80;
                hi = b == 0xF4 ? 0x8F : 0xBF;
            }
            bool ok = len > 0 && i + len <= name.size();
            for (size_t k = 1; ok && k < len; ++k)
            {
                uint8_t const c = uint8_t(name[i + k]);
                ok = k == 1 ? c >= lo && c <= hi : c >= 0x80 && c <= 0xBF;
            }
            if (!ok)
            {
                ++i;
                continue;
            }
            // Control characters: C0, DEL and C1 (U+0080-U+009F, encoded C2 80-9F).
            bool const control = (len == 1 && (b < 0x20 || b == 0x7F))
                || (len == 2 && b == 0xC2 && uint8_t(name[i + 1]) < 0xA0);
            if (!control)
                out.append(name, i, len);
            i += len;
        }
        return out;
    }

    LoadoutError ValidateLoadout(Catalog const& catalog, ClassLoadout const& l, uint32_t level, bool isGm,
        bool pro)
    {
        if (l.name.size() > kClassNameMax)
            return LoadoutError::NameTooLong;
        // Talent picks are the class's (Classes.h ValidatePicks, which needs the character's
        // class, level and bought options): the game checks them beside this.
        // An attachment needs its gun.
        if ((!l.primary && !PrimaryAttachments(l).empty()) || (!l.secondary && !SecondaryAttachments(l).empty()))
            return LoadoutError::AttachmentNotOnGun;
        // Any three perks, from any tier (WOMW: not one per tier as MW2); the same perk in more
        // than one slot only as its stacking allows (Catalog.h PerkStack); the fourth slot is a
        // deathstreak.
        uint16_t const perks[4] = {l.perk1, l.perk2, l.perk3, l.deathstreak};
        for (int i = 0; i < 4; ++i)
        {
            if (!perks[i])
                continue;
            Perk const* p = catalog.FindPerk(perks[i]);
            bool const deathstreak = i == 3;
            if (!p || (deathstreak ? p->tier != 0 : p->tier == 0))
                return LoadoutError::WrongPerk;
            if (!deathstreak && PerkCount(l, perks[i]) > std::min(p->maxStack, kMaxPerkStack))
                return LoadoutError::PerkDoesNotStack;
            if (EffectiveLevel(level, isGm) < UnlockLevel(p->mw2UnlockRank))
                return LoadoutError::Locked;
        }
        // A Pro bit on an empty slot, past the three, or on a perk without a Pro.
        if (l.perkPro & ~kPerkProMask)
            return LoadoutError::ProLocked;
        for (int i = 0; i < 3; ++i)
        {
            if (!(l.perkPro & (1u << i)))
                continue;
            Perk const* p = catalog.FindPerk(perks[i]);
            if (!p || !p->HasPro())
                return LoadoutError::ProLocked;
        }
        AttachmentRoom const room = AttachmentsAllowed(l, pro);
        if (l.primary)
        {
            LoadoutError e = CheckGun(catalog, l.primary, WeaponSlot::Primary, PrimaryAttachments(l), level, isGm,
                room.primary);
            if (e != LoadoutError::None)
                return e;
        }
        if (l.primaryCamo)
        {
            WeaponProfile const* gun = catalog.Weapons().Find(l.primary);
            if (!gun || !gun->camos || !catalog.FindCamo(l.primaryCamo))
                return LoadoutError::UnknownCamo;
        }
        if (l.secondary)
        {
            // Two on the secondary take the Rails' Pro.
            LoadoutError e = CheckGun(catalog, l.secondary, WeaponSlot::Secondary, SecondaryAttachments(l), level,
                isGm, room.secondary);
            if (e != LoadoutError::None)
                return e;
        }
        // The equipment: each in its own slot, unlocked.
        for (auto [id, slot] : {std::pair{l.lethal, WeaponSlot::Lethal}, std::pair{l.tactical, WeaponSlot::Tactical}})
        {
            if (!id)
                continue;
            WeaponProfile const* e = catalog.Weapons().Find(id);
            if (!e)
                return LoadoutError::UnknownWeapon;
            if (e->slot != slot)
                return LoadoutError::WrongSlot;
            if (!CanUseWeapon(level, isGm, *e))
                return LoadoutError::Locked;
        }
        // The killstreaks: rewards of the ladder, each once (none chosen: the whole ladder).
        uint16_t const streaks[3] = {l.streak1, l.streak2, l.streak3};
        std::vector<StreakReward> const ladder = DefaultStreakLadder();
        for (int i = 0; i < 3; ++i)
        {
            if (!streaks[i])
                continue;
            bool const known = std::any_of(ladder.begin(), ladder.end(),
                [&](StreakReward const& r) { return r.id == streaks[i]; });
            if (!known)
                return LoadoutError::UnknownStreak;
            for (int j = 0; j < i; ++j)
                if (streaks[j] == streaks[i])
                    return LoadoutError::UnknownStreak;
        }
        return LoadoutError::None;
    }

    EquipDecision EquipRule(bool inCombat, bool alive)
    {
        if (!alive)
            return EquipDecision::WaitRespawn;
        return inCombat ? EquipDecision::WaitOutOfCombat : EquipDecision::ApplyNow;
    }

    EquipOutcome PlanEquip(EquipState& s, uint8_t slot, bool changed, bool inCombat, bool alive)
    {
        if (slot == kNoClass)
            return EquipOutcome::Saved;
        s.pending = kNoClass;
        if (slot == s.equipped && !changed)
            return EquipOutcome::Applied;
        if (EquipRule(inCombat, alive) == EquipDecision::ApplyNow)
        {
            s.equipped = slot;
            return EquipOutcome::Applied;
        }
        s.pending = slot;
        return EquipOutcome::Pending;
    }

    std::string PrimaryVariant(Catalog const& catalog, ClassLoadout const& l)
    {
        WeaponProfile const* gun = catalog.Weapons().Find(l.primary);
        return gun ? catalog.HeldVariant(*gun, PrimaryAttachments(l)) : std::string();
    }

    std::string SecondaryVariant(Catalog const& catalog, ClassLoadout const& l)
    {
        WeaponProfile const* gun = catalog.Weapons().Find(l.secondary);
        return gun ? catalog.HeldVariant(*gun, SecondaryAttachments(l)) : std::string();
    }
}
