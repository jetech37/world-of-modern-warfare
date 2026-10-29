/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Loadout.h"

using namespace Fusion;

namespace
{
    char const* const kCatalog = R"({"version": 2,
        "weapons": [
            {"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30, "unlock_rank": 1,
             "attachments": ["reflex", "silencer", "acog", "gl", "fmj", "xmags", "eotech"], "camos": true,
             "variants": {"reflex": "m4_reflex_mp", "silencer": "m4_silencer_mp", "acog": "m4_acog_mp",
                          "gl": "m4_gl_mp", "fmj": "m4_fmj_mp", "silencer+acog": "m4_acog_silencer_mp",
                          "acog+fmj": "m4_acog_fmj_mp", "silencer+fmj": "m4_fmj_silencer_mp",
                          "silencer+gl": "m4_gl_silencer_mp", "gl+fmj": "m4_fmj_gl_mp",
                          "xmags": "m4_xmags_mp", "silencer+xmags": "m4_silencer_xmags_mp",
                          "acog+xmags": "m4_acog_xmags_mp", "fmj+xmags": "m4_fmj_xmags_mp",
                          "gl+xmags": "m4_gl_xmags_mp",
                          "eotech": "m4_eotech_mp", "reflex+eotech": "m4_reflex_eotech_test"}},
            {"id": 9, "name": "ak47_mp", "class": "assault_rifle", "fire_time": 0.085, "mag_size": 30, "unlock_rank": 70},
            {"id": 30, "name": "usp_mp", "class": "pistol", "fire_time": 0.15, "mag_size": 12,
             "attachments": ["silencer", "fmj"],
             "variants": {"silencer": "usp_silencer_mp", "fmj": "usp_fmj_mp", "silencer+fmj": "usp_fmj_silencer_mp"}}],
        "attachments": [
            {"id": "reflex", "incompatible": ["acog"], "unlock_rank": 4},
            {"id": "silencer", "unlock_rank": 10},
            {"id": "acog", "incompatible": ["reflex"], "unlock_rank": 16},
            {"id": "gl", "unlock_rank": 20},
            {"id": "fmj", "unlock_rank": 1},
            {"id": "xmags", "unlock_rank": 1},
            {"id": "eotech", "unlock_rank": 1}],
        "camos": [{"id": 1, "name": "Woodland", "suffix": "_woodland"}],
        "perks": [
            {"id": 1, "tier": 1, "name": "Marathon", "unlock_rank": 4, "stack": "additive", "max_stack": 3,
             "pro_kills": 150},
            {"id": 4, "tier": 1, "name": "Bling", "unlock_rank": 21, "stack": "special", "max_stack": 3,
             "pro_kills": 200},
            {"id": 2, "tier": 1, "name": "Sleight of Hand", "unlock_rank": 1, "stack": "diminishing",
             "max_stack": 2},
            {"id": 6, "tier": 2, "name": "Stopping Power", "unlock_rank": 1},
            {"id": 11, "tier": 3, "name": "Commando", "unlock_rank": 1}],
        "deathstreaks": [{"id": 22, "name": "Painkiller", "deaths": 3, "unlock_rank": 1}]})";

    Catalog Load()
    {
        Catalog c;
        std::string error;
        c.LoadJson(kCatalog, error);
        return c;
    }

    ClassLoadout AdminClass()
    {
        ClassLoadout l;
        l.name = "Admin";
        l.primary = 2;
        l.primaryAtt1 = "acog";
        l.primaryCamo = 1;
        l.secondary = 30;
        return l;
    }
}

TEST(a_full_admin_class_is_valid)
{
    Catalog const c = Load();
    CHECK(ValidateLoadout(c, AdminClass(), 60, true) == LoadoutError::None);
    ClassLoadout bare;              // no primary, no secondary: the per-WoW-class gun
    CHECK(ValidateLoadout(c, bare, 1, false) == LoadoutError::None);
}

TEST(unknown_pieces_are_rejected)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primary = 999;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::UnknownWeapon);
    l = AdminClass();
    l.primaryCamo = 9;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::UnknownCamo);
    l = AdminClass();
    l.primaryAtt1 = "laser";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::UnknownAttachment);
}

TEST(a_pistol_cannot_be_primary)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primary = 30;
    l.primaryAtt1.clear();
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongSlot);
    l = AdminClass();
    l.secondary = 9;                // a rifle in the secondary slot
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongSlot);
}

TEST(two_attachments_need_bling_and_compatibility)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primaryAtt2 = "silencer";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::TwoAttachmentsNeedBling); // a GM too
    l.perk1 = 4;                    // Bling
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.primaryAtt2 = "reflex";       // two sights
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::IncompatibleAttachments);
    l = AdminClass();
    l.perk1 = 4;
    l.primaryAtt2 = "gl";           // compatible, but the install has no m4_acog_gl_mp here
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::AttachmentNotOnGun);
    l = AdminClass();
    l.secondaryAtt = "acog";        // not on the pistol
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::AttachmentNotOnGun);
    l.secondaryAtt = "silencer";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l = AdminClass();
    l.primaryAtt1.clear();
    l.primaryAtt2 = "acog";         // the second without the first is the same attachment
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
}

TEST(bling_pro_puts_a_second_attachment_on_the_secondary)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.secondaryAtt = "silencer";
    l.secondaryAtt2 = "fmj";
    CHECK(ValidateLoadout(c, l, 60, true, true) == LoadoutError::TwoAttachmentsNeedBling); // no Bling
    l.perk1 = PerkId::Bling;
    CHECK(ValidateLoadout(c, l, 60, true, false) == LoadoutError::TwoAttachmentsNeedBling); // not Pro
    CHECK(ValidateLoadout(c, l, 60, true, true) == LoadoutError::None);
    CHECK(SecondaryVariant(c, l).find("fmj") != std::string::npos);
    l.secondary = 0;    // an attachment needs its gun
    l.secondaryAtt.clear();
    CHECK(ValidateLoadout(c, l, 60, true, true) == LoadoutError::AttachmentNotOnGun);
}

TEST(a_low_level_player_cannot_take_locked_pieces)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primary = 9;
    l.primaryAtt1.clear();
    l.primaryCamo = 0;
    CHECK(ValidateLoadout(c, l, 10, false) == LoadoutError::Locked);
    CHECK(ValidateLoadout(c, l, 10, true) == LoadoutError::None);    // a GM passes
    l = AdminClass();               // the ACOG unlocks at MW2 rank 16, level 14
    CHECK(ValidateLoadout(c, l, 10, false) == LoadoutError::Locked);
    CHECK(ValidateLoadout(c, l, 14, false) == LoadoutError::None);
}

TEST(a_long_name_is_rejected)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.name = std::string(kClassNameMax + 1, 'x');
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::NameTooLong);
}

TEST(a_camo_on_a_gun_without_camos_is_rejected)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primary = 9;
    l.primaryAtt1.clear();
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::UnknownCamo);
}

TEST(equipping_waits_for_out_of_combat_and_respawn)
{
    CHECK(EquipRule(false, true) == EquipDecision::ApplyNow);
    CHECK(EquipRule(true, true) == EquipDecision::WaitOutOfCombat);
    CHECK(EquipRule(false, false) == EquipDecision::WaitRespawn);
    CHECK(EquipRule(true, false) == EquipDecision::WaitRespawn);   // dead: the respawn applies it
}

TEST(editing_the_equipped_class_in_combat_waits)
{
    EquipState s{0, kNoClass};
    // Class 1 (slot 0) is in hand; the player changes it mid-fight.
    CHECK(PlanEquip(s, 0, true, true, true) == EquipOutcome::Pending);
    CHECK(s.equipped == 0 && s.pending == 0);
    // Out of combat the same save applies at once.
    EquipState t{0, kNoClass};
    CHECK(PlanEquip(t, 0, true, false, true) == EquipOutcome::Applied);
    CHECK(t.equipped == 0 && t.pending == kNoClass);
    // Re-equipping the unchanged class in hand is a no-op, in combat or not.
    EquipState u{0, kNoClass};
    CHECK(PlanEquip(u, 0, false, true, true) == EquipOutcome::Applied);
    CHECK(u.pending == kNoClass);
}

TEST(a_new_equip_replaces_a_pending_one)
{
    EquipState s{0, kNoClass};
    CHECK(PlanEquip(s, 1, true, true, true) == EquipOutcome::Pending);
    CHECK(s.pending == 1);
    // Changing their mind back to the class in hand: nothing waits any more.
    CHECK(PlanEquip(s, 0, false, true, true) == EquipOutcome::Applied);
    CHECK(s.equipped == 0 && s.pending == kNoClass);
    // A save with nothing to equip leaves a waiting equip alone.
    EquipState t{0, 1};
    CHECK(PlanEquip(t, kNoClass, false, true, true) == EquipOutcome::Saved);
    CHECK(t.pending == 1);
    // Dead: the respawn applies it.
    EquipState d{kNoClass, kNoClass};
    CHECK(PlanEquip(d, 2, true, false, false) == EquipOutcome::Pending);
    CHECK(d.pending == 2);
}

TEST(any_three_perks_each_once_and_checked_for_unlock)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.perk1 = 1;            // Marathon, tier 1
    l.perk2 = 6;            // Stopping Power, tier 2
    l.perk3 = 11;           // Commando, tier 3
    l.deathstreak = 22;     // Painkiller
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perk1 = 4;            // two tier 1 perks: any three perks may share a tier (WOMW)
    l.perk2 = 1;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perk3 = 1;            // twice: Marathon stacks (catalog.json "stack")
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perk3 = 6;            // Stopping Power twice does not
    l.perk2 = 6;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::PerkDoesNotStack);
    l.perk2 = 6;
    l.perk3 = 11;
    l.perk1 = 22;           // a deathstreak is not a perk
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongPerk);
    l.perk1 = 99;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongPerk);
    l.perk1 = 1;
    l.deathstreak = 6;      // a perk is not a deathstreak
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongPerk);
    l.deathstreak = 0;
    l.primaryAtt1.clear();
    l.primaryCamo = 0;
    l.perk1 = 1;
    l.perk3 = 4;            // Bling, in any slot, unlocks at MW2 rank 21 (level 18)
    CHECK(ValidateLoadout(c, l, 17, false) == LoadoutError::Locked);
    CHECK(ValidateLoadout(c, l, 18, false) == LoadoutError::None);
}

TEST(an_attachment_on_an_empty_gun_slot_is_rejected)
{
    Catalog const c = Load();
    ClassLoadout l;                 // no guns, but attachment strings for them
    l.primaryAtt1 = "acog";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::AttachmentNotOnGun);
    l = ClassLoadout{};
    l.secondaryAtt = "silencer";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::AttachmentNotOnGun);
}

TEST(a_class_name_keeps_only_printable_utf8)
{
    CHECK(CleanClassName("Sniper 1") == "Sniper 1");
    CHECK(CleanClassName("Sni\x01per\n") == "Sniper");             // control characters go
    CHECK(CleanClassName("Caf\xc3\xa9") == "Caf\xc3\xa9");          // UTF-8 stays
    CHECK(CleanClassName("a\xff" "b\xc3") == "ab");                 // broken bytes go
    CHECK(CleanClassName("\xe2\x82") == "");                        // a cut-off sequence
    CHECK(CleanClassName("x\xc0\xaf") == "x");                      // an overlong encoding
}

TEST(a_perk_fills_several_slots_only_as_its_stacking_allows)
{
    Catalog const c = Load();
    CHECK(c.FindPerk(1)->stack == PerkStack::Additive && c.FindPerk(1)->maxStack == 3);
    CHECK(c.FindPerk(4)->stack == PerkStack::Special && c.FindPerk(4)->HasPro());
    CHECK(c.FindPerk(6)->stack == PerkStack::None && c.FindPerk(6)->maxStack == 1 && !c.FindPerk(6)->HasPro());
    CHECK(c.FindPerk(22)->maxStack == 1 && !c.FindPerk(22)->HasPro());    // a deathstreak
    ClassLoadout l;
    l.perk1 = l.perk2 = l.perk3 = 1;    // Marathon three times: additive, up to three
    CHECK(PerkCount(l, 1) == 3);
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perk1 = l.perk2 = l.perk3 = 2;    // Sleight of Hand: two at most here
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::PerkDoesNotStack);
    l.perk3 = 0;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perk1 = l.perk2 = 11;             // Commando does not stack
    l.perk3 = 0;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::PerkDoesNotStack);
    l.perk2 = 6;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
}

TEST(tinkers_rails_make_room_for_attachments)
{
    ClassLoadout l;
    auto room = [&] { return AttachmentsAllowed(l); };
    CHECK(room().primary == 1 && room().secondary == 1);
    l.perk1 = PerkId::Bling;            // MW2's Bling: two on the primary
    CHECK(room().primary == 2 && room().secondary == 1);
    l.perk2 = PerkId::Bling;            // twice: three on the primary
    CHECK(room().primary == 3 && room().secondary == 1);
    l.perk3 = PerkId::Bling;            // three times: four on the primary, the secondary still one
    CHECK(room().primary == 4 && room().secondary == 1);
    l.perkPro = 0x07;                   // the Rails' Pro: the secondary's second, as Bling Pro
    CHECK(room().primary == 4 && room().secondary == 2);
    l.perk2 = l.perk3 = 0;
    l.perkPro = 0x01;
    CHECK(room().primary == 2 && room().secondary == 2);
    l.perkPro = 0;
    CHECK(AttachmentsAllowed(l, true).secondary == 2);    // a GM's Pro switch
    l.perk1 = 0;
    CHECK(AttachmentsAllowed(l, true).secondary == 1);    // the Pro needs the Rails in the kit
}

TEST(rails_stack_to_four_attachments)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.primaryAtt1 = "silencer";
    l.primaryAtt2 = "acog";
    l.primaryAtt3 = "fmj";
    l.perk1 = PerkId::Bling;            // one Rails: two attachments
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::TwoAttachmentsNeedBling);
    l.perk2 = PerkId::Bling;            // two: three
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.primaryAtt4 = "xmags";            // a fourth wants a third Rails
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::TwoAttachmentsNeedBling);
    l.perk3 = PerkId::Bling;            // three: four
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    CHECK(PrimaryAttachments(l).size() == 4);
    // Held as the two that show (FMJ and extended mags left out), the sight first.
    CHECK(PrimaryVariant(c, l) == "m4_acog_silencer_mp");
    // Three Rails no longer give the secondary a second attachment; its Pro does.
    l.secondaryAtt = "silencer";
    l.secondaryAtt2 = "fmj";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::TwoAttachmentsNeedBling);
    l.perkPro = 0x07;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    l.perkPro = 0;
    l.secondaryAtt2.clear();
    // Every two of the four must be a combination the gun has.
    l.primaryAtt4 = "gl";               // acog+gl is not one here
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::AttachmentNotOnGun);
    l.primaryAtt4 = "reflex";           // two sights (the data says so too)
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::IncompatibleAttachments);
    l.primaryAtt4 = "silencer";         // the same one twice
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::IncompatibleAttachments);
    // A low level still can't take a locked attachment.
    l.primaryAtt4 = "xmags";
    l.primaryAtt3 = "gl";
    l.primaryAtt2 = "fmj";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    CHECK(ValidateLoadout(c, l, 10, false) == LoadoutError::Locked);
    // Silencer, grenade launcher, FMJ, extended mags: the launcher shows with the silencer.
    CHECK(PrimaryVariant(c, l) == "m4_gl_silencer_mp");
}

TEST(no_two_attachments_of_one_kind)
{
    // MW2's Bling: never two of a kind, even where a gun's data would pair them.
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.perk1 = PerkId::Bling;
    l.primaryAtt1 = "reflex";
    l.primaryAtt2 = "eotech";           // a reflex+eotech definition exists, but both are sights
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::IncompatibleAttachments);
    l.primaryAtt1 = "acog";
    l.primaryAtt2 = "silencer";
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    CHECK(KindOfAttachment("reflex") == AttachmentKind::Sight && KindOfAttachment("thermal") == AttachmentKind::Sight);
    CHECK(KindOfAttachment("gl") == AttachmentKind::Underbarrel && KindOfAttachment("grip") == AttachmentKind::Underbarrel);
    CHECK(KindOfAttachment("fmj") != KindOfAttachment("xmags"));     // MW2 pairs them
    CHECK(KindOfAttachment("nonsense") == AttachmentKind::Other);
    CHECK(std::string(KindName(AttachmentKind::Sight)) == "a sight");
    CHECK(!KindShows(AttachmentKind::Ammo) && KindShows(AttachmentKind::Muzzle));
    CHECK(c.FindAttachment("acog")->kind == AttachmentKind::Sight);
}

TEST(a_pro_bit_needs_a_perk_with_a_pro)
{
    Catalog const c = Load();
    ClassLoadout l = AdminClass();
    l.perkPro = 0x01;                   // an empty slot
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::ProLocked);
    l.perk1 = 11;                       // Commando: no Pro in this catalog
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::ProLocked);
    l.perk1 = PerkId::Bling;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::None);
    CHECK(PerkSlotPro(l, PerkId::Bling) && !PerkSlotPro(l, 1));
    l.perkPro = 0x08;                   // past the three slots
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::ProLocked);
    // The Rails' Pro puts the second attachment on the secondary without the GM switch.
    l.perkPro = 0x01;
    l.secondaryAtt = "silencer";
    l.secondaryAtt2 = "fmj";
    CHECK(ValidateLoadout(c, l, 60, true, false) == LoadoutError::None);
    l.perkPro = 0;
    CHECK(ValidateLoadout(c, l, 60, true, false) == LoadoutError::TwoAttachmentsNeedBling);
}
