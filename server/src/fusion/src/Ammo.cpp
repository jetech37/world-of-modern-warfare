/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Ammo.h"

#include <algorithm>
#include <cmath>

namespace Fusion
{
    namespace
    {
        AmmoRules& Rules()
        {
            static AmmoRules r;
            return r;
        }
    }

    AmmoRules const& CurrentAmmoRules() { return Rules(); }
    void SetAmmoRules(AmmoRules const& r) { Rules() = r; }

    float BodyScavengeMags(float mags, bool scavenger, AmmoRules const& r)
    {
        return std::max(0.f, mags) * (scavenger ? std::max(1.f, r.scavengerBodyScale) : 1.f);
    }

    uint32_t GunAmmo::FieldTarget(AmmoRules const& r) const
    {
        float const want = r.limited ? float(m_magSize) * std::max(0.f, r.fieldFloorMags)
                                     : float(m_maxReserve) * r.legacyFieldShare;
        return std::min<uint32_t>(m_maxReserve, uint32_t(want + 0.5f));
    }

    void GunAmmo::RespawnResupply(AmmoRules const& r)
    {
        m_mag = m_magSize;
        TopUp(r.limited ? r.respawnReserveShare : 1.f);
    }

    void GunAmmo::TopUp(float share)
    {
        share = Clamp(share, 0.f, 1.f);
        uint32_t const mag = uint32_t(float(m_magSize) * share + 0.5f);
        uint32_t const res = uint32_t(float(m_maxReserve) * share + 0.5f);
        m_mag = std::max(m_mag, std::min<uint32_t>(mag, m_magSize));
        m_reserve = std::max(m_reserve, std::min<uint32_t>(res, m_maxReserve));
    }

    uint32_t GunAmmo::MissingRounds() const
    {
        uint32_t const mag = m_magSize > m_mag ? m_magSize - m_mag : 0;
        uint32_t const res = m_maxReserve > m_reserve ? m_maxReserve - m_reserve : 0;
        return mag + res;
    }

    uint32_t GunAmmo::Restock(uint32_t rounds)
    {
        uint32_t const toMag = std::min<uint32_t>(rounds, m_magSize > m_mag ? m_magSize - m_mag : 0);
        m_mag += toMag;
        uint32_t const toRes = std::min<uint32_t>(rounds - toMag, m_maxReserve > m_reserve ? m_maxReserve - m_reserve : 0);
        m_reserve += toRes;
        return toMag + toRes;
    }

    uint32_t GunAmmo::ScavengeMags(float mags)
    {
        if (mags <= 0.f || m_reserve >= m_maxReserve)
            return 0;
        uint32_t const want = std::max<uint32_t>(1, uint32_t(float(m_magSize) * mags + 0.5f));
        uint32_t const add = std::min<uint32_t>(want, m_maxReserve - m_reserve);
        m_reserve += add;
        return add;
    }

    void GunAmmo::Scavenge()
    {
        m_reserve = std::min<uint32_t>(m_reserve + m_magSize, m_maxReserve);
    }

    void GunAmmo::Reset(WeaponProfile const& w)
    {
        m_magSize = w.akimbo ? uint16_t(w.magSize / 2) : w.magSize;
        m_maxReserve = w.maxReserve;
        m_baseReserve = w.maxReserve;
        m_mag = m_magSize;
        m_reserve = w.maxReserve;
        m_trickle = 0.f;
        m_loaded = AmmoType::Basic;
        m_special.fill(0);
    }

    void GunAmmo::Set(uint32_t mag, uint32_t reserve)
    {
        m_mag = std::min<uint32_t>(mag, m_magSize);
        m_reserve = std::min<uint32_t>(reserve, m_maxReserve);
        m_trickle = 0.f;
    }

    uint32_t GunAmmo::AddCharges(uint32_t n, uint32_t cap)
    {
        if (m_mag >= cap)
            return 0;
        uint32_t const added = std::min(n, cap - m_mag);
        m_mag += added;
        return added;
    }

    void GunAmmo::SetReserveBonus(float share, bool fill)
    {
        float const scaled = float(m_baseReserve) * (1.f + std::max(0.f, share));
        m_maxReserve = uint16_t(std::min(65535.f, scaled + 0.5f));
        if (fill || m_reserve > m_maxReserve)
            m_reserve = m_maxReserve;
    }

    AmmoType GunAmmo::NextRound() const
    {
        if (m_loaded != AmmoType::Basic && m_special[size_t(m_loaded)] > 0)
            return m_loaded;
        return AmmoType::Basic;
    }

    bool GunAmmo::Fire()
    {
        if (m_mag == 0)
            return false;
        --m_mag;
        AmmoType round = NextRound();
        if (round != AmmoType::Basic)
            --m_special[size_t(round)];
        return true;
    }

    uint32_t GunAmmo::Reload()
    {
        uint32_t want = m_magSize - std::min<uint32_t>(m_mag, m_magSize);
        uint32_t moved = std::min(want, m_reserve);
        m_mag += moved;
        m_reserve -= moved;
        return moved;
    }

    uint32_t GunAmmo::ReloadOne()
    {
        if (m_mag >= m_magSize || m_reserve == 0)
            return 0;
        ++m_mag;
        --m_reserve;
        return 1;
    }

    void GunAmmo::FieldRearm(float secondsOutOfCombat, float dt, AmmoRules const& r)
    {
        if (secondsOutOfCombat < kFieldRearmDelay)
        {
            m_trickle = 0.f;
            return;
        }
        uint32_t const kit = FieldTarget(r);
        if (m_reserve >= kit)
            return;
        m_trickle += (r.limited ? std::max(0.f, r.fieldRoundsPerSecond) : float(m_maxReserve) * kFieldRearmPerSecond) * dt;
        uint32_t add = uint32_t(m_trickle);
        m_trickle -= float(add);
        m_reserve = std::min(kit, m_reserve + add);
    }

    void GunAmmo::FullResupply()
    {
        m_mag = m_magSize;
        m_reserve = m_maxReserve;
    }

    void GunAmmo::QuartermasterRearm(uint32_t level)
    {
        for (size_t t = 1; t < size_t(AmmoType::Count); ++t)
        {
            if (level >= AmmoUnlockLevel(AmmoType(t)))
                m_special[t] = SpecialCap();
        }
    }

    void GunAmmo::AddSpecial(AmmoType t, uint32_t rounds)
    {
        if (t == AmmoType::Basic || t == AmmoType::Count)
            return;
        m_special[size_t(t)] = std::min(SpecialCap(), m_special[size_t(t)] + rounds);
    }

    uint32_t QuartermasterFee(uint32_t level)
    {
        // 1.12 quest rewards run from about 1 silver at level 10 to a few gold at 60.
        float l = float(std::max<uint32_t>(level, 1));
        return uint32_t(20.f * l + 1.f * l * l);   // 3 silver at 10, 48 silver at 60
    }
}
