/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CATALOG_H
#define FUSION_CATALOG_H

#include "fusion/Weapon.h"

#include <string>
#include <vector>

namespace Fusion
{
    // Where an attachment goes on a gun (MW2's own groups, as its Bling allowed them): a gun
    // carries at most one of each kind, so never two sights or two underbarrels. Sights: red dot,
    // holographic, ACOG, thermal. Underbarrel: grenade launcher, shotgun, grip. Muzzle: the
    // silencer. Ammo: FMJ. Magazine: extended mags (MW2 pairs FMJ with extended mags, so they are
    // kinds of their own). Fire rate, akimbo, the tactical knife and the heartbeat sensor each
    // their own.
    enum class AttachmentKind : uint8_t
    {
        Sight,
        Underbarrel,
        Muzzle,
        Ammo,
        Magazine,
        FireRate,
        Akimbo,
        Knife,
        Sensor,
        Other,      // an id this table does not know: no kind shared with any other
    };
    // An MW2 attachment id's kind.
    AttachmentKind KindOfAttachment(std::string const& id);
    // How the kit window names a kind in "Already has a sight" ("a sight").
    char const* KindName(AttachmentKind k);
    // Whether a kind shows on the gun's model (FMJ, extended mags and rapid fire do not).
    bool KindShows(AttachmentKind k);

    // An MW2 attachment, by MW2's own id (the suffix in its weapon names: "acog", "xmags").
    struct Attachment
    {
        std::string id;
        std::string name;
        std::string icon;                       // the install's material name
        std::vector<std::string> incompatible;  // attachment ids it cannot go with
        uint8_t mw2UnlockRank = 1;
        AttachmentKind kind = AttachmentKind::Other;    // KindOfAttachment(id)
    };

    // An MW2 camo: the gun's model is viewmodel_<gun><suffix>.
    struct Camo
    {
        uint8_t id = 0;
        std::string name;
        std::string suffix;
    };

    // How a perk taken in more than one of a kit's three perk slots adds up (WOMW design,
    // section 3, "Perk stacking"): not at all (one slot only), each copy adding the same again,
    // each copy adding less, or a rule of its own (Tinker's Rails: one more attachment a copy).
    enum class PerkStack : uint8_t
    {
        None,
        Additive,
        Diminishing,
        Special,
    };

    // An MW2 perk (tier 1-3) or deathstreak (tier 0, with the deaths that trigger it).
    struct Perk
    {
        uint16_t id = 0;
        uint8_t tier = 0;
        std::string name;
        uint8_t deaths = 0;
        uint8_t mw2UnlockRank = 1;
        // catalog.json's "stack" and "max_stack": how copies in several slots add up, and the
        // most slots it may fill (1 when it does not stack).
        PerkStack stack = PerkStack::None;
        uint8_t maxStack = 1;
        // catalog.json's "pro_kills": the kills with the perk in the kit that earn its Pro (MW2's
        // Pro challenge); 0 for none (deathstreaks).
        uint32_t proKills = 0;

        bool HasPro() const { return proKills > 0; }
    };

    // A kit's perk slots hold a perk at most this many times (any stacking perk's max_stack is
    // held to it: there are three slots).
    constexpr uint8_t kMaxPerkStack = 3;

    // The perk ids (MW2 design spec 3.3).
    namespace PerkId
    {
        constexpr uint16_t Marathon = 1, SleightOfHand = 2, Scavenger = 3, Bling = 4, OneManArmy = 5,
            StoppingPower = 6, Lightweight = 7, Hardline = 8, ColdBlooded = 9, DangerClose = 10,
            Commando = 11, SteadyAim = 12, Scrambler = 13, Ninja = 14, SitRep = 15, LastStand = 16,
            Copycat = 21, Painkiller = 22, Martyrdom = 23, FinalStand = 24;
    }

    // The content catalog shared by server and client (MW2 design spec 1.1): catalog.json
    // version 2, or a version 1 weapons.json read as guns only.
    class Catalog
    {
    public:
        bool LoadJson(std::string const& text, std::string& error);

        WeaponTable const& Weapons() const { return m_weapons; }
        std::vector<Attachment> const& Attachments() const { return m_attachments; }
        std::vector<Camo> const& Camos() const { return m_camos; }
        Attachment const* FindAttachment(std::string const& id) const;
        Camo const* FindCamo(uint8_t id) const;
        Perk const* FindPerk(uint16_t id) const;            // perks and deathstreaks
        std::vector<Perk> const& Perks() const { return m_perks; }

        // The MW2 weapon definition for a gun with these attachments (any order): the gun's
        // own name with none, "" when the gun has no such combination.
        std::string VariantName(WeaponProfile const& gun, std::vector<std::string> attachments) const;

        // The definition a gun with these attachments is held as: VariantName's for up to two;
        // for three or four (Tinker's Rails; MW2 has no such definitions) two of them, the gun
        // showing at most two: sights first, then underbarrels, then the other kinds that show
        // (KindShows), those that don't (FMJ, extended mags, rapid fire) last, each kind in the
        // catalog's order; the first pair in that order the gun has. "" for no such pair.
        std::string HeldVariant(WeaponProfile const& gun, std::vector<std::string> const& attachments) const;

        // The gun with its attachments' numbers (MW2's own, per combination: a silencer's
        // shorter ranges, extended mags' magazine, rapid fire's fire time); the gun itself for
        // none or a combination with no changes. Same id and name: it is the gun the client fires.
        // Three or four attachments (no MW2 definition): each one's own numbers, in the catalog's
        // order.
        WeaponProfile Effective(WeaponProfile const& gun, std::vector<std::string> attachments) const;

    private:
        // The combination's key: attachment ids in the catalog's order, joined by "+"; "" for
        // none or an unknown attachment.
        std::string VariantKey(std::vector<std::string> attachments) const;
        // An attachment's place in the catalog's order (past the end for an unknown one).
        size_t AttachmentRank(std::string const& id) const;
    public:

    private:
        WeaponTable m_weapons;
        std::vector<Attachment> m_attachments;
        std::vector<Camo> m_camos;
        std::vector<Perk> m_perks;
    };
}

#endif
