/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_LOADOUT_H
#define FUSION_LOADOUT_H

#include "fusion/Catalog.h"

#include <array>
#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

namespace Fusion
{
    constexpr size_t kClassNameMax = 24;
    // A character's kits (WOMW design, section 3; Kits.h): up to five, MW2's premade classes gone.
    constexpr size_t kClassSlots = 5;
    constexpr uint8_t kNoClass = 255;
    // A kit's talent picks: one option id per tier (levels 10 to 60), 0 = none. Package W6's class
    // trainers sell the options; until then a kit carries none.
    constexpr size_t kKitTalentTiers = 6;

    // One Create-a-Class class (MW2 design spec 1.4 and 2.1). Guns are catalog ids (0 = none:
    // the per-WoW-class gun), attachments MW2's ids ("" = none), camo a catalog camo id
    // (0 = none). Perks, equipment and streaks are carried as ids for their own phases.
    struct ClassLoadout
    {
        std::string name;
        uint16_t primary = 0;
        std::string primaryAtt1, primaryAtt2;
        uint8_t primaryCamo = 0;
        uint16_t secondary = 0;
        std::string secondaryAtt;
        std::string secondaryAtt2;      // Bling Pro's second attachment on the secondary
        uint16_t lethal = 0, tactical = 0;
        uint16_t perk1 = 0, perk2 = 0, perk3 = 0, deathstreak = 0;
        uint16_t streak1 = 0, streak2 = 0, streak3 = 0;
        std::array<uint16_t, kKitTalentTiers> talentPicks{};
        // The primary's third attachment: room for it takes Tinker's Rails twice (loadout v4).
        std::string primaryAtt3;
        // Which perk slots play their perk's Pro: bit 0 perk1, bit 1 perk2, bit 2 perk3 (loadout
        // v4). A Pro is the account's once its challenge is done (UnlockKind::PerkPro).
        uint8_t perkPro = 0;
        // The primary's fourth attachment: room for it takes Tinker's Rails in all three perk
        // slots (loadout v5).
        std::string primaryAtt4;

        auto Tie() const
        {
            return std::tie(name, primary, primaryAtt1, primaryAtt2, primaryCamo, secondary, secondaryAtt,
                secondaryAtt2, lethal, tactical, perk1, perk2, perk3, deathstreak, streak1, streak2, streak3,
                talentPicks, primaryAtt3, perkPro, primaryAtt4);
        }
        bool operator==(ClassLoadout const& o) const { return Tie() == o.Tie(); }
    };

    enum class LoadoutError : uint8_t
    {
        None,
        UnknownWeapon,
        WrongSlot,
        Locked,
        UnknownAttachment,
        AttachmentNotOnGun,
        TwoAttachmentsNeedBling,
        IncompatibleAttachments,
        UnknownCamo,
        NameTooLong,
        WrongPerk,          // not a perk of that tier, or not a deathstreak
        TooFast,            // saved too often (FusionMgr::HandleLoadout rate limit)
        NotAtKitStation,    // kits are edited and switched only near a trainer or innkeeper (Kits.h)
        UnknownTalent,      // a talent pick the kit can't hold
        UnknownStreak,      // a killstreak not on the ladder, or chosen twice
        PerkDoesNotStack,   // a perk in more slots than its stacking allows (Catalog.h PerkStack)
        ProLocked,          // a perk slot asks for a Pro the account has not earned, or one with none
    };

    // The perk slots' Pro bits (ClassLoadout::perkPro).
    constexpr uint8_t kPerkProMask = 0x07;

    // How many of the kit's three perk slots hold `perk` (0 for none).
    uint8_t PerkCount(ClassLoadout const& l, uint16_t perk);
    // Whether any slot holding `perk` plays its Pro.
    bool PerkSlotPro(ClassLoadout const& l, uint16_t perk);

    // How many attachments each gun of a kit may carry (WOMW design, section 3, "Perk stacking"):
    // the primary one, and one more for each Tinker's Rails (MW2's Bling: one Rails, two
    // attachments), so four with the Rails in all three perk slots; the secondary one, and its
    // second with the Rails' Pro (or `allPro`, a GM's Pro switch) as MW2's Bling Pro gave it. The
    // secondary carries at most two.
    struct AttachmentRoom
    {
        uint8_t primary = 1;
        uint8_t secondary = 1;
    };
    constexpr uint8_t kMaxPrimaryAttachments = 4;
    constexpr uint8_t kMaxSecondaryAttachments = 2;
    AttachmentRoom AttachmentsAllowed(ClassLoadout const& l, bool allPro = false);

    // A kit's attachments on each gun, empty slots left out.
    std::vector<std::string> PrimaryAttachments(ClassLoadout const& l);
    std::vector<std::string> SecondaryAttachments(ClassLoadout const& l);

    // World channel op: open the kit editor, [40, source, ...] (Kits.h OpenKitEditor).
    constexpr uint8_t kOpOpenClasses = 40;

    // Whether a class may be saved and equipped: every gun, attachment, camo, perk and deathstreak
    // in the catalog, in its slot, unlocked at the player's level (a GM passes), the attachment
    // combination one the gun has (three or four attachments: every two of them a combination it
    // has), no two attachments of one kind (AttachmentKind: never two sights), as many attachments
    // as the kit's Tinker's Rails make room for (AttachmentsAllowed), a perk in more than one slot
    // only as far as its stacking allows, a Pro bit only on a perk with one.
    // `pro`: every Pro is on (a GM's `.fusion pro on`), whatever the kit's bits say.
    LoadoutError ValidateLoadout(Catalog const& catalog, ClassLoadout const& l, uint32_t level, bool isGm,
        bool pro = false);

    // A class name as saved: well-formed UTF-8 without control characters (a broken or cut-off
    // sequence, an overlong encoding or a control character is dropped).
    std::string CleanClassName(std::string const& name);

    // When a newly equipped class takes effect (MW2 design spec 2.2): at once when alive and out
    // of combat, else on leaving combat, or on respawn.
    enum class EquipDecision : uint8_t
    {
        ApplyNow,
        WaitOutOfCombat,
        WaitRespawn,
    };

    EquipDecision EquipRule(bool inCombat, bool alive);

    // The class in hand and the one waiting to be (kNoClass for none).
    struct EquipState
    {
        uint8_t equipped = kNoClass;
        uint8_t pending = kNoClass;
    };

    enum class EquipOutcome : uint8_t
    {
        Saved,      // nothing to equip
        Pending,    // waits for out of combat or the respawn
        Applied,    // in hand now
    };

    // A request to equip `slot` (kNoClass: save only). `changed` is whether that class differs
    // from the one in hand (another slot, or the same slot edited). A new equip replaces any
    // waiting one; editing the class in hand waits like any other equip.
    EquipOutcome PlanEquip(EquipState& s, uint8_t slot, bool changed, bool inCombat, bool alive);

    // The MW2 weapon definition a class puts in hand for a slot: the variant for its
    // attachments, "" when the slot is empty.
    std::string PrimaryVariant(Catalog const& catalog, ClassLoadout const& l);
    std::string SecondaryVariant(Catalog const& catalog, ClassLoadout const& l);
}

#endif
