/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Catalog.h"
#include "fusion/Loadout.h"

using namespace Fusion;

namespace
{
    char const* const kV1 = R"({"version": 1, "weapons": [
        {"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30},
        {"id": 30, "name": "usp_mp", "class": "pistol", "fire_time": 0.15, "mag_size": 12}]})";

    char const* const kV2 = R"({"version": 2,
        "weapons": [
            {"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30,
             "attachments": ["acog", "silencer", "gl", "shotgun"], "camos": true,
             "variants": {"acog": "m4_acog_mp", "silencer": "m4_silencer_mp",
                          "acog+silencer": "m4_acog_silencer_mp", "gl": "m4_gl_mp"}},
            {"id": 30, "name": "usp_mp", "class": "pistol", "fire_time": 0.15, "mag_size": 12}],
        "attachments": [
            {"id": "acog", "name": "ACOG Scope", "icon": "weapon_attachment_acog", "incompatible": ["eotech"], "unlock_rank": 1},
            {"id": "eotech", "name": "Holographic Sight", "icon": "weapon_attachment_eotech", "incompatible": ["acog"]},
            {"id": "silencer", "name": "Silencer", "icon": "weapon_attachment_silencer"},
            {"id": "gl", "name": "Grenade Launcher", "icon": "weapon_attachment_m203", "incompatible": ["shotgun"]},
            {"id": "shotgun", "name": "Shotgun", "icon": "weapon_attachment_shotgun", "incompatible": ["gl"]}],
        "camos": [{"id": 1, "name": "Woodland", "suffix": "_woodland"},
                  {"id": 6, "name": "Red Tiger", "suffix": "_red_tiger"}]})";
}

TEST(catalog_loads_version_1_as_guns_only)
{
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(kV1, error));
    CHECK(c.Weapons().Size() == 2);
    CHECK(c.Attachments().empty());
    WeaponProfile const* m4 = c.Weapons().Find(std::string("m4_mp"));
    CHECK(m4 && m4->attachments.empty() && !m4->camos);
    CHECK(m4 && m4->slot == WeaponSlot::Primary);
    WeaponProfile const* usp = c.Weapons().Find(std::string("usp_mp"));
    CHECK(usp && usp->slot == WeaponSlot::Secondary);   // MW2's secondaries: pistols and the like
}

TEST(catalog_v2_reads_attachments_camos_and_variants)
{
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(kV2, error));
    CHECK(c.Attachments().size() == 5);
    Attachment const* acog = c.FindAttachment("acog");
    CHECK(acog && acog->name == "ACOG Scope" && acog->incompatible.size() == 1);
    CHECK(c.FindCamo(1) && c.FindCamo(1)->suffix == "_woodland");
    CHECK(!c.FindCamo(2));
    WeaponProfile const* m4 = c.Weapons().Find(std::string("m4_mp"));
    CHECK(m4 && m4->camos && m4->attachments.size() == 4);
}

TEST(variant_name_orders_attachments_by_catalog_order)
{
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(kV2, error));
    WeaponProfile const& m4 = *c.Weapons().Find(std::string("m4_mp"));
    CHECK(c.VariantName(m4, {"silencer", "acog"}) == "m4_acog_silencer_mp");
    CHECK(c.VariantName(m4, {}) == "m4_mp");
    CHECK(c.VariantName(m4, {"gl", "shotgun"}).empty());    // no such definition
    CHECK(c.VariantName(m4, {"laser"}).empty());
}

TEST(catalog_rejects_a_variant_naming_an_unknown_attachment)
{
    std::string text = kV2;
    text.replace(text.find("\"acog+silencer\""), 15, "\"acog+laser\"");
    Catalog c;
    std::string error;
    CHECK(!c.LoadJson(text, error));
    CHECK(error.find("laser") != std::string::npos);
}

TEST(a_combination_takes_its_own_numbers)
{
    char const* const text = R"({"version": 2,
        "weapons": [{"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30,
            "max_reserve": 180, "max_damage_range": 41.67, "min_damage_range": 55.56,
            "attachments": ["silencer", "xmags"],
            "variants": {"silencer": "m4_silencer_mp", "xmags": "m4_xmags_mp", "silencer+xmags": "m4_silencer_xmags_mp"},
            "variant_stats": {"silencer": {"max_damage_range": 13.89, "min_damage_range": 27.78},
                              "xmags": {"mag_size": 45},
                              "silencer+xmags": {"max_damage_range": 13.89, "min_damage_range": 27.78, "mag_size": 45}}}],
        "attachments": [{"id": "silencer"}, {"id": "xmags"}]})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(text, error));
    WeaponProfile const& m4 = *c.Weapons().Find(std::string("m4_mp"));
    WeaponProfile s = c.Effective(m4, {"silencer"});
    CHECK(s.maxDamageRange == 13.89f && s.minDamageRange == 27.78f && s.magSize == 30);
    WeaponProfile x = c.Effective(m4, {"xmags", "silencer"});
    CHECK(x.magSize == 45 && x.maxDamageRange == 13.89f);
    CHECK(x.id == m4.id && x.name == m4.name);      // still the gun the client fires
    WeaponProfile none = c.Effective(m4, {});
    CHECK(none.magSize == 30 && none.maxDamageRange == 41.67f);
    CHECK(!none.fmj && !x.fmj);
    CHECK(c.Effective(m4, {"fmj"}).fmj);    // FMJ changes no number, but the rounds go deeper
    // Akimbo: a magazine in each hand, the two guns firing in turn.
    WeaponProfile two = c.Effective(m4, {"akimbo"});
    CHECK(two.magSize == 2 * m4.magSize);
    CHECK(two.akimbo && !none.akimbo && !x.akimbo);
    CHECK_NEAR(two.fireTime, m4.fireTime * 0.5f, 1e-6);
}

TEST(an_underbarrel_weapon_belongs_to_its_gun)
{
    char const* text = R"({"version": 2, "weapons": [
        {"id": 2, "name": "m4_mp", "class": "assault_rifle", "attachments": ["gl", "acog"],
         "variants": {"gl": "m4_gl_mp", "acog": "m4_acog_mp", "acog+gl": "m4_acog_gl_mp"}},
        {"id": 1002, "name": "gl_m4_mp", "class": "launcher", "slot": "alternate",
         "alternate_of": 2, "attachment": "gl", "explosion_radius": 8.33,
         "explosion_inner": 155, "explosion_outer": 25}],
        "attachments": [{"id": "gl"}, {"id": "acog"}]})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(text, error));
    WeaponProfile const* gl = c.Weapons().Find(uint16_t(1002));
    CHECK(gl && gl->slot == WeaponSlot::Alternate && gl->alternateOf == 2 && gl->alternateAttachment == "gl");
    CHECK(gl && gl->explosionRadius > 8.f && gl->explosionInner == 155.f);
    // Only a class whose primary carries the launcher fires it.
    CHECK(gl && AlternateAllowed(*gl, 2, {"acog", "gl"}));
    CHECK(gl && !AlternateAllowed(*gl, 2, {"acog"}));
    CHECK(gl && !AlternateAllowed(*gl, 9, {"gl"}));
    // And it is never a class's primary or secondary.
    ClassLoadout l;
    l.primary = 1002;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongSlot);
}

TEST(three_attachments_take_each_ones_numbers)
{
    char const* const json = R"({"version": 2,
        "weapons": [{"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30,
            "max_damage_range": 30, "min_damage_range": 50,
            "attachments": ["silencer", "acog", "xmags"],
            "variants": {"silencer": "m4_silencer_mp", "acog": "m4_acog_mp", "xmags": "m4_xmags_mp",
                         "silencer+acog": "m4_acog_silencer_mp", "silencer+xmags": "m4_silencer_xmags_mp",
                         "acog+xmags": "m4_acog_xmags_mp"},
            "variant_stats": {"silencer": {"max_damage_range": 14, "min_damage_range": 28},
                              "xmags": {"mag_size": 45},
                              "silencer+xmags": {"max_damage_range": 14, "min_damage_range": 28, "mag_size": 45}}}],
        "attachments": [{"id": "silencer"}, {"id": "acog"}, {"id": "xmags"}]})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(json, error));
    WeaponProfile const& m4 = *c.Weapons().Find(2);
    WeaponProfile const e = c.Effective(m4, {"xmags", "acog", "silencer"});
    CHECK(e.magSize == 45 && e.silenced);
    CHECK_NEAR(e.maxDamageRange, 14.f, 1e-4);
    // Held as the two that show: extended mags left out.
    CHECK(c.HeldVariant(m4, {"xmags", "acog", "silencer"}) == "m4_acog_silencer_mp");
    CHECK(c.HeldVariant(m4, {"acog"}) == "m4_acog_mp");
}

TEST(four_attachments_show_the_sight_and_the_underbarrel)
{
    // Tinker's Rails three times: four attachments, the gun shows two of them, a sight and an
    // underbarrel first; each one's numbers count.
    char const* const json = R"({"version": 2,
        "weapons": [{"id": 2, "name": "m4_mp", "class": "assault_rifle", "fire_time": 0.07, "mag_size": 30,
            "attachments": ["reflex", "silencer", "gl", "xmags"],
            "variants": {"reflex+silencer": "m4_reflex_silencer_mp", "reflex+gl": "m4_gl_reflex_mp",
                         "reflex+xmags": "m4_reflex_xmags_mp", "silencer+gl": "m4_gl_silencer_mp",
                         "silencer+xmags": "m4_silencer_xmags_mp", "gl+xmags": "m4_gl_xmags_mp"},
            "variant_stats": {"xmags": {"mag_size": 45}, "silencer": {"max_damage_range": 14}}}],
        "attachments": [{"id": "reflex"}, {"id": "silencer"}, {"id": "gl"}, {"id": "xmags"}]})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(json, error));
    WeaponProfile const& m4 = *c.Weapons().Find(2);
    CHECK(c.HeldVariant(m4, {"xmags", "silencer", "gl", "reflex"}) == "m4_gl_reflex_mp");
    // Three: the sight and the silencer, extended mags never shown while a pair without it exists.
    CHECK(c.HeldVariant(m4, {"xmags", "silencer", "reflex"}) == "m4_reflex_silencer_mp");
    // No sight: the underbarrel and the silencer.
    CHECK(c.HeldVariant(m4, {"xmags", "silencer", "gl"}) == "m4_gl_silencer_mp");
    WeaponProfile const e = c.Effective(m4, {"xmags", "silencer", "gl", "reflex"});
    CHECK(e.magSize == 45 && e.silenced);
    CHECK_NEAR(e.maxDamageRange, 14.f, 1e-4);
}
