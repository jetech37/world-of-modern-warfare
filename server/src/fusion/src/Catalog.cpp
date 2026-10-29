/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Catalog.h"
#include "fusion/Json.h"

#include <algorithm>
#include <map>
#include <utility>

namespace Fusion
{
    AttachmentKind KindOfAttachment(std::string const& id)
    {
        static struct { char const* id; AttachmentKind kind; } const kKinds[] = {
            {"reflex", AttachmentKind::Sight}, {"eotech", AttachmentKind::Sight},
            {"acog", AttachmentKind::Sight}, {"thermal", AttachmentKind::Sight},
            {"gl", AttachmentKind::Underbarrel}, {"shotgun", AttachmentKind::Underbarrel},
            {"grip", AttachmentKind::Underbarrel},
            {"silencer", AttachmentKind::Muzzle},
            {"fmj", AttachmentKind::Ammo},
            {"xmags", AttachmentKind::Magazine},
            {"rof", AttachmentKind::FireRate},
            {"akimbo", AttachmentKind::Akimbo},
            {"tactical", AttachmentKind::Knife},
            {"heartbeat", AttachmentKind::Sensor},
        };
        for (auto const& k : kKinds)
            if (id == k.id)
                return k.kind;
        return AttachmentKind::Other;
    }

    char const* KindName(AttachmentKind k)
    {
        switch (k)
        {
            case AttachmentKind::Sight: return "a sight";
            case AttachmentKind::Underbarrel: return "an underbarrel attachment";
            case AttachmentKind::Muzzle: return "a muzzle attachment";
            case AttachmentKind::Ammo: return "special rounds";
            case AttachmentKind::Magazine: return "a magazine attachment";
            case AttachmentKind::FireRate: return "rapid fire";
            case AttachmentKind::Akimbo: return "akimbo";
            case AttachmentKind::Knife: return "a tactical knife";
            case AttachmentKind::Sensor: return "a heartbeat sensor";
            default: return "an attachment of that kind";
        }
    }

    bool KindShows(AttachmentKind k)
    {
        return k != AttachmentKind::Ammo && k != AttachmentKind::Magazine && k != AttachmentKind::FireRate;
    }

    bool Catalog::LoadJson(std::string const& text, std::string& error)
    {
        WeaponTable weapons;
        if (!weapons.LoadJson(text, error))
            return false;
        Json::Value root;
        if (!Json::Parse(text, root, error))
            return false;

        std::vector<Attachment> attachments;
        if (Json::Value const* list = root.Get("attachments"); list && list->IsArray())
        {
            for (Json::Value const& a : list->array)
            {
                Attachment att;
                att.id = a.StringOr("id", "");
                if (att.id.empty())
                {
                    error = "an attachment lacks an id";
                    return false;
                }
                att.name = a.StringOr("name", att.id);
                att.icon = a.StringOr("icon", "");
                att.mw2UnlockRank = uint8_t(a.NumberOr("unlock_rank", 1));
                att.kind = KindOfAttachment(att.id);
                if (Json::Value const* inc = a.Get("incompatible"); inc && inc->IsArray())
                    for (Json::Value const& id : inc->array)
                        att.incompatible.push_back(id.string);
                attachments.push_back(std::move(att));
            }
        }
        std::vector<Camo> camos;
        if (Json::Value const* list = root.Get("camos"); list && list->IsArray())
        {
            for (Json::Value const& c : list->array)
            {
                Camo camo;
                camo.id = uint8_t(c.NumberOr("id", 0));
                camo.name = c.StringOr("name", "");
                camo.suffix = c.StringOr("suffix", "");
                if (camo.id == 0 || camo.suffix.empty())
                {
                    error = "a camo lacks an id or suffix";
                    return false;
                }
                camos.push_back(std::move(camo));
            }
        }

        std::vector<Perk> perks;
        for (char const* key : {"perks", "deathstreaks"})
        {
            Json::Value const* list = root.Get(key);
            if (!list || !list->IsArray())
                continue;
            for (Json::Value const& p : list->array)
            {
                Perk perk;
                perk.id = uint16_t(p.NumberOr("id", 0));
                perk.tier = uint8_t(p.NumberOr("tier", 0));
                perk.name = p.StringOr("name", "");
                perk.deaths = uint8_t(p.NumberOr("deaths", 0));
                perk.mw2UnlockRank = uint8_t(p.NumberOr("unlock_rank", 1));
                std::string const stack = p.StringOr("stack", "none");
                perk.stack = stack == "additive" ? PerkStack::Additive
                    : stack == "diminishing" ? PerkStack::Diminishing
                    : stack == "special" ? PerkStack::Special
                    : PerkStack::None;
                double const most = p.NumberOr("max_stack", perk.stack == PerkStack::None ? 1 : kMaxPerkStack);
                perk.maxStack = perk.stack == PerkStack::None ? uint8_t(1)
                    : uint8_t(std::clamp(most, 1.0, double(kMaxPerkStack)));
                perk.proKills = uint32_t(std::max(0.0, p.NumberOr("pro_kills", 0)));
                bool const deathstreak = std::string(key) == "deathstreaks";
                if (perk.id == 0 || (deathstreak ? perk.tier != 0 : (perk.tier < 1 || perk.tier > 3)))
                {
                    error = std::string("a bad entry in ") + key;
                    return false;
                }
                // A deathstreak has one slot of its own: it neither stacks nor has a Pro.
                if (deathstreak)
                {
                    perk.stack = PerkStack::None;
                    perk.maxStack = 1;
                    perk.proKills = 0;
                }
                perks.push_back(std::move(perk));
            }
        }

        auto known = [&](std::string const& id) {
            return std::any_of(attachments.begin(), attachments.end(),
                [&](Attachment const& a) { return a.id == id; });
        };
        for (WeaponProfile const& w : weapons.All())
        {
            for (std::string const& id : w.attachments)
            {
                if (!known(id))
                {
                    error = "weapon " + w.name + " allows unknown attachment " + id;
                    return false;
                }
            }
            for (auto const& kv : w.variants)
            {
                size_t start = 0;
                while (start <= kv.first.size())
                {
                    size_t const end = std::min(kv.first.find('+', start), kv.first.size());
                    std::string const id = kv.first.substr(start, end - start);
                    if (!known(id))
                    {
                        error = "weapon " + w.name + " variant " + kv.second + " names unknown attachment " + id;
                        return false;
                    }
                    start = end + 1;
                }
            }
        }

        m_weapons = std::move(weapons);
        m_attachments = std::move(attachments);
        m_camos = std::move(camos);
        m_perks = std::move(perks);
        return true;
    }

    Attachment const* Catalog::FindAttachment(std::string const& id) const
    {
        for (Attachment const& a : m_attachments)
            if (a.id == id)
                return &a;
        return nullptr;
    }

    Camo const* Catalog::FindCamo(uint8_t id) const
    {
        for (Camo const& c : m_camos)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    Perk const* Catalog::FindPerk(uint16_t id) const
    {
        for (Perk const& p : m_perks)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    std::string Catalog::VariantKey(std::vector<std::string> attachments) const
    {
        auto rank = [&](std::string const& id) {
            for (size_t i = 0; i < m_attachments.size(); ++i)
                if (m_attachments[i].id == id)
                    return i;
            return m_attachments.size();
        };
        for (std::string const& id : attachments)
            if (rank(id) == m_attachments.size())
                return {};
        std::sort(attachments.begin(), attachments.end(),
            [&](std::string const& a, std::string const& b) { return rank(a) < rank(b); });
        std::string key;
        for (std::string const& id : attachments)
            key += (key.empty() ? "" : "+") + id;
        return key;
    }

    WeaponProfile Catalog::Effective(WeaponProfile const& gun, std::vector<std::string> attachments) const
    {
        WeaponProfile p = gun;
        p.fmj = std::find(attachments.begin(), attachments.end(), "fmj") != attachments.end();
        p.silenced = std::find(attachments.begin(), attachments.end(), "silencer") != attachments.end();
        // Akimbo: MW2 gives each hand its gun's magazine and fires them in turn.
        bool const akimbo = std::find(attachments.begin(), attachments.end(), "akimbo") != attachments.end();
        p.akimbo = akimbo;
        float const hands = akimbo ? 2.f : 1.f;
        p.magSize = uint16_t(p.magSize * hands);
        p.fireTime /= hands;
        // The combination's own numbers; three or four attachments (no MW2 definition): each one's.
        std::vector<std::map<std::string, double> const*> sets;
        auto it = gun.variantStats.find(VariantKey(attachments));
        if (it != gun.variantStats.end())
            sets.push_back(&it->second);
        else if (attachments.size() > 2)
        {
            std::vector<std::string> ordered = attachments;
            std::stable_sort(ordered.begin(), ordered.end(),
                [&](std::string const& a, std::string const& b) { return AttachmentRank(a) < AttachmentRank(b); });
            for (std::string const& a : ordered)
            {
                auto single = gun.variantStats.find(VariantKey({a}));
                if (single != gun.variantStats.end())
                    sets.push_back(&single->second);
            }
        }
        for (auto const* set : sets)
        for (auto const& [field, value] : *set)
        {
            if (field == "damage") p.damage = float(value);
            else if (field == "min_damage") p.minDamage = float(value);
            else if (field == "max_damage_range") p.maxDamageRange = float(value);
            else if (field == "min_damage_range") p.minDamageRange = float(value);
            else if (field == "fire_time" && value > 0.0) p.fireTime = float(value) / hands;
            else if (field == "mag_size" && value > 0.0) p.magSize = uint16_t(value * hands);
            else if (field == "max_reserve") p.maxReserve = uint16_t(value);
        }
        // The reload: the quickest of every definition the client may hold this combination as
        // (its own; with three attachments, one of its pairs, HeldVariant). The server holds a
        // reload to a share of it (Reload.h), so it must never be longer than the client's.
        auto within = [&](std::string const& key) {
            size_t start = 0;
            while (start <= key.size())
            {
                size_t const end = std::min(key.find('+', start), key.size());
                if (std::find(attachments.begin(), attachments.end(), key.substr(start, end - start)) == attachments.end())
                    return false;
                start = end + 1;
            }
            return true;
        };
        auto quicker = [](float& at, double value) {
            if (value > 0.0 && (at <= 0.f || float(value) < at))
                at = float(value);
        };
        for (auto const& [key, stats] : gun.variantStats)
        {
            if (key.empty() || !within(key))
                continue;
            for (auto const& [field, value] : stats)
            {
                if (field == "reload_time") quicker(p.reloadTime, value);
                else if (field == "reload_empty_time") quicker(p.reloadEmptyTime, value);
            }
        }
        return p;
    }

    size_t Catalog::AttachmentRank(std::string const& id) const
    {
        for (size_t i = 0; i < m_attachments.size(); ++i)
            if (m_attachments[i].id == id)
                return i;
        return m_attachments.size();
    }

    std::string Catalog::HeldVariant(WeaponProfile const& gun, std::vector<std::string> const& attachments) const
    {
        if (attachments.size() <= 2)
            return VariantName(gun, attachments);
        // The order they are shown in: sights, underbarrels, the other kinds that show, then
        // those that don't; each kind in the catalog's order.
        auto place = [&](std::string const& id) {
            AttachmentKind const k = KindOfAttachment(id);
            int const group = k == AttachmentKind::Sight ? 0 : k == AttachmentKind::Underbarrel ? 1 : KindShows(k) ? 2 : 3;
            return std::pair{group, AttachmentRank(id)};
        };
        std::vector<std::string> order = attachments;
        std::stable_sort(order.begin(), order.end(),
            [&](std::string const& a, std::string const& b) { return place(a) < place(b); });
        // The first pair in that order the gun has a definition for.
        for (size_t i = 0; i < order.size(); ++i)
            for (size_t j = i + 1; j < order.size(); ++j)
            {
                std::string name = VariantName(gun, {order[i], order[j]});
                if (!name.empty())
                    return name;
            }
        return {};
    }

    std::string Catalog::VariantName(WeaponProfile const& gun, std::vector<std::string> attachments) const
    {
        if (attachments.empty())
            return gun.name;
        // The key lists attachments in the catalog's order.
        auto rank = [&](std::string const& id) {
            for (size_t i = 0; i < m_attachments.size(); ++i)
                if (m_attachments[i].id == id)
                    return i;
            return m_attachments.size();
        };
        for (std::string const& id : attachments)
            if (rank(id) == m_attachments.size())
                return {};
        std::sort(attachments.begin(), attachments.end(),
            [&](std::string const& a, std::string const& b) { return rank(a) < rank(b); });
        std::string key;
        for (std::string const& id : attachments)
            key += (key.empty() ? "" : "+") + id;
        auto it = gun.variants.find(key);
        return it == gun.variants.end() ? std::string() : it->second;
    }
}
