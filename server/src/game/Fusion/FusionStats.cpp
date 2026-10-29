/*
 * World of Modern Warfare: what a character's stats do with guns (design doc section 7), where
 * the rules (src/fusion/include/fusion/Stats.h) meet the game. Gear keeps its stats; here they
 * become handling (sent to the client on fusion channel 6), the armor's cut of bullets, weak-spot
 * hits, MW2's regeneration, equipment strength and item procs from gun hits. Gear never raises a
 * gun's damage: that is the level's alone (GunMultiplier).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Database/DBCStores.h"
#include "Item.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <sstream>

namespace
{
    // How often the table is worked out again (equip, level, buffs and talents show up within it).
    constexpr uint32 kCombatStatsEveryMs = 500;

    // A proc spell a gun hit must not fire: an extra melee swing, or a strike with the weapon in
    // hand (those belong to melee).
    bool MeleeOnly(SpellEntry const* s)
    {
        return s->HasEffect(SPELL_EFFECT_ADD_EXTRA_ATTACKS) || s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE)
            || s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL) || s->HasEffect(SPELL_EFFECT_NORMALIZED_WEAPON_DMG)
            || s->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE);
    }
}

Fusion::Stats::Input FusionMgr::StatsInputOf(Player const* player) const
{
    Fusion::Stats::Input in;
    in.level = player->GetLevel();
    auto stat = [&](Stats s, float& total, float& bonus) {
        total = player->GetStat(s);
        bonus = total - player->GetCreateStat(s);
    };
    stat(STAT_STRENGTH, in.strength, in.strengthBonus);
    stat(STAT_AGILITY, in.agility, in.agilityBonus);
    stat(STAT_STAMINA, in.stamina, in.staminaBonus);
    stat(STAT_INTELLECT, in.intellect, in.intellectBonus);
    stat(STAT_SPIRIT, in.spirit, in.spiritBonus);
    in.armor = float(player->GetArmor());
    in.hitPct = std::max(player->GetWeaponBasedAuraModifier(RANGED_ATTACK, SPELL_AURA_MOD_HIT_CHANCE),
        player->GetWeaponBasedAuraModifier(BASE_ATTACK, SPELL_AURA_MOD_HIT_CHANCE));
    in.critPct = player->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE);
    in.defenseBonus = float(player->GetDefenseSkillValue()) - float(player->GetSkillMaxForLevel());
    // Only gear and buffs' ranged attack power (agility's share is agility's).
    in.rangedPowerBonus = float(player->GetInt16Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 0));
    if (player->HasFusion())
        in.perks = player->GetFusionConst().perks;
    return in;
}

void FusionMgr::UpdateCombatStats(Player* player, uint32 now, uint32 diffMs)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.combatSent || int32(now - d.nextCombatMs) >= 0)
    {
        d.nextCombatMs = now + kCombatStatsEveryMs;
        Fusion::Stats::Derived fresh = Fusion::Stats::Compute(StatsInputOf(player));
        ApplyFieldStims(d, fresh, now);     // an alchemist's stims (FusionPrestige.cpp)
        sCraftsMgr.ApplyHandling(player, fresh);    // the kit's glyphs, the gun in hand's gems
        bool const changed = !d.combatSent || !Fusion::Stats::SameOnWire(fresh, d.combat);
        float const wasRelief = d.combat[Fusion::Stats::Out::HeavyGunRelief];
        d.combat = fresh;
        if (changed)
        {
            d.combatSent = true;
            SendChannel(player, Fusion::Stats::kChannelStats, Fusion::Stats::EncodeHandling(fresh));
            // Strength's relief changes a heavy gun's move speed.
            if (fresh[Fusion::Stats::Out::HeavyGunRelief] != wasRelief)
            {
                player->UpdateSpeed(MOVE_RUN, false);
                player->UpdateSpeed(MOVE_RUN_BACK, false);
            }
        }
    }

    // MW2's regeneration: once the delay since the last hit taken has passed. Not while held
    // down in Last Stand (its own rules), nor dead.
    if (!player->IsAlive() || d.downed.Active() || player->GetHealth() >= player->GetMaxHealth())
    {
        d.regenCarry = 0.f;
        return;
    }
    float const since = d.lastHurtMs ? float(now - d.lastHurtMs) / 1000.f : 1e6f;
    float const add = Fusion::Stats::RegenAmount(d.combat, float(player->GetMaxHealth()), since, diffMs / 1000.f) + d.regenCarry;
    uint32 const whole = add > 0.f ? uint32(add) : 0;
    d.regenCarry = add - float(whole);
    if (whole)
        player->SetHealth(std::min<uint32>(player->GetMaxHealth(), player->GetHealth() + whole));
}

void FusionMgr::NoteHurt(Unit* victim)
{
    if (Player* p = victim->ToPlayer(); p && p->HasFusion())
        p->GetFusion().lastHurtMs = std::max<uint32>(1, WorldTimer::getMSTime());
}

float FusionMgr::GunMultiplier(Player const* player) const
{
    return Fusion::Stats::GunDamageMultiplier(player->GetLevel());
}

float FusionMgr::BulletCutOn(Player const* target, Fusion::HitInput const& in, float armorIgnore) const
{
    float const cut = Fusion::Scale::PlayerArmorCut(float(target->GetArmor()), target->GetLevel());
    return Fusion::Stats::BulletCut(cut, in.weapon && in.weapon->fmj, armorIgnore);
}

bool FusionMgr::WeakSpotRoll(Player const* shooter, Unit const* target) const
{
    float const chance = shooter->HasFusion() ? shooter->GetFusionConst().combat.weakSpotChance : 0.f;
    float taken = 0.f;
    if (Player const* tp = target->ToPlayer(); tp && tp->HasFusion())
        taken = tp->GetFusionConst().combat[Fusion::Stats::Out::WeakSpotTaken];
    float const p = Fusion::Stats::WeakSpotChance(chance, taken);
    return p > 0.f && roll_chance_f(p * 100.f);
}

float FusionMgr::EquipmentStrength(Player const* player) const
{
    return player->HasFusion() ? player->GetFusionConst().combat[Fusion::Stats::Out::EquipmentStrength] : 0.f;
}

void FusionMgr::StrengthenTactical(Player* owner, Unit* u, uint32 spell) const
{
    float const strength = EquipmentStrength(owner);
    if (strength <= 0.f)
        return;
    if (SpellAuraHolder* h = u->GetSpellAuraHolder(spell, owner->GetObjectGuid()))
    {
        int32 const duration = int32(float(h->GetAuraMaxDuration()) * (1.f + strength));
        if (duration <= 0)
            return;
        h->SetAuraMaxDuration(duration);
        h->SetAuraDuration(duration);
        h->UpdateAuraDuration();
    }
}

void FusionMgr::GunProcs(Player* shooter, Unit* target, Fusion::WeaponProfile const& w, uint32 shotTimeMs)
{
    if (!target->IsAlive() || target == shooter || !shooter->IsAlive())
        return;
    FusionPlayerData& d = shooter->GetFusion();
    // One roll a pull: a shotgun's pellets and a round through two bodies share it.
    if (d.procShotRolled && d.procShotMs == shotTimeMs)
        return;
    d.procShotRolled = true;
    d.procShotMs = shotTimeMs;
    uint32 const now = WorldTimer::getMSTime();

    auto tryProc = [&](SpellEntry const* spell, float chancePct, float ppm, Item* item, bool selfIfPositive) {
        if (!spell || MeleeOnly(spell) || shooter->HasGCD(spell))
            return;
        auto it = d.procReadyMs.find(spell->Id);
        if (it != d.procReadyMs.end() && int32(now - it->second) < 0)
            return;
        float const p = Fusion::Stats::GunProcChance(chancePct, ppm, w.fireTime);
        if (!(p > 0.f) || !roll_chance_f(p * 100.f))
            return;
        d.procReadyMs[spell->Id] = now + Fusion::Stats::kGunProcCooldownMs;
        if (selfIfPositive && spell->IsPositiveSpell())
            shooter->CastSpell(shooter, spell->Id, true, item);
        else
            shooter->CastSpell(target, spell->Id, true, item);
    };

    // The weapons' own "chance on hit" and their enchantments (Crusader, poisons, Fiery...).
    for (WeaponAttackType att : {BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK})
    {
        Item* item = shooter->GetWeaponForAttack(att, true, true);
        if (!item || !item->GetProto())
            continue;
        for (auto const& sd : item->GetProto()->Spells)
        {
            if (!sd.SpellId || sd.SpellTrigger != ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
                continue;
            if (!target->IsAlive())
                return;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(sd.SpellId);
            float const pct = spell && spell->procChance <= 100 ? float(spell->procChance) : 0.f;
            float const ppm = sd.SpellPPMRate > 0.f ? sd.SpellPPMRate : (pct > 0.f ? 0.f : 1.f);
            tryProc(spell, pct, ppm, item, false);
        }
        for (int slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
        {
            SpellItemEnchantmentEntry const* e = sSpellItemEnchantmentStore.LookupEntry(item->GetEnchantmentId(EnchantmentSlot(slot)));
            if (!e)
                continue;
            for (int s = 0; s < 3; ++s)
            {
                if (e->type[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(e->spellid[s]);
                if (!spell)
                    continue;
                float const ppm = sSpellMgr.GetItemEnchantProcChance(spell->Id);
                float const pct = ppm > 0.f ? 0.f : (e->amount[s] ? float(e->amount[s]) : 0.f);
                tryProc(spell, pct, ppm > 0.f ? ppm : (pct > 0.f ? 0.f : 1.f), item, true);
            }
        }
    }

    // Equipped items' proc effects (trinkets, armor, a weapon's equip effect) that fire on a melee
    // swing or a ranged shot. Talents and buffs keep their own procs. Gathered first: a proc's
    // cast may change the aura list.
    struct AuraProc
    {
        SpellEntry const* spell;
        float pct, ppm;
        ObjectGuid item;
    };
    std::vector<AuraProc> auraProcs;
    for (Aura* aura : shooter->GetAurasByType(SPELL_AURA_PROC_TRIGGER_SPELL))
    {
        if (!aura->GetCastItemGuid())
            continue;
        SpellEntry const* source = aura->GetSpellProto();
        SpellProcEventEntry const* ev = sSpellMgr.GetSpellProcEvent(source->Id);
        uint32 const flags = ev && ev->procFlags ? ev->procFlags : source->procFlags;
        if (!(flags & (PROC_FLAG_DEAL_MELEE_SWING | PROC_FLAG_DEAL_RANGED_ATTACK)))
            continue;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(source->EffectTriggerSpell[aura->GetEffIndex()]);
        float const ppm = ev ? ev->ppmRate : 0.f;
        float const pct = ev && ev->customChance > 0.f ? ev->customChance : float(source->procChance);
        auraProcs.push_back({spell, std::min(pct, 100.f), ppm, aura->GetCastItemGuid()});
    }
    for (AuraProc const& a : auraProcs)
    {
        if (!target->IsAlive())
            break;
        tryProc(a.spell, a.pct, a.ppm, shooter->GetItemByGuid(a.item), true);
    }
}

std::string FusionMgr::CombatStatsReport(Player* player)
{
    using Fusion::Stats::Out;
    Fusion::Stats::Input const in = StatsInputOf(player);
    Fusion::Stats::Derived const d = Fusion::Stats::Compute(in);
    auto pct = [](float m) { return int(std::lround((1.f - m) * 100.f)); };
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(1);
    o << "Combat stats of " << player->GetName() << " (level " << in.level << ", stat budget "
      << Fusion::Stats::StatBudget(in.level) << "):\n";
    o << "  gear over base: str " << in.strengthBonus << ", agi " << in.agilityBonus << ", sta " << in.staminaBonus
      << ", int " << in.intellectBonus << ", spi " << in.spiritBonus << ", rap " << in.rangedPowerBonus
      << ", hit " << in.hitPct << "%, crit " << in.critPct << "%, defense " << in.defenseBonus << ", armor " << in.armor << "\n";
    o << "  aim " << pct(d[Out::AdsTime]) << "% quicker, swap " << pct(d[Out::SwapTime]) << "%, sprint-out "
      << pct(d[Out::SprintOutTime]) << "%, reload " << pct(d[Out::ReloadTime]) << "%\n";
    o << "  recoil " << pct(d[Out::Recoil]) << "% less, hip spread " << pct(d[Out::HipSpread]) << "%, bloom "
      << pct(d[Out::Bloom]) << "%, sway " << pct(d[Out::Sway]) << "%, flinch " << pct(d[Out::Flinch]) << "%\n";
    o << "  heavy gun weight " << int(std::lround(d[Out::HeavyGunRelief] * 100.f)) << "% lifted, equipment +"
      << int(std::lround(d[Out::EquipmentStrength] * 100.f)) << "%\n";
    o << "  bullets cut by armor " << int(std::lround(d.bulletCut * 100.f)) << "% (FMJ halves it), weak-spot chance "
      << d.weakSpotChance * 100.f << "%, weak spots taken " << int(std::lround(d[Out::WeakSpotTaken] * 100.f)) << "% fewer\n";
    o << "  regeneration after " << d.regenDelay << " s at " << d.regenRate * 100.f << "% a second; gun damage x"
      << GunMultiplier(player) << " (level only)\n";
    for (size_t i = 0; i < Fusion::Stats::kThresholds.size(); ++i)
        if (d.thresholds & (1u << i))
            o << "  * " << Fusion::Stats::kThresholds[i].text << "\n";
    return o.str();
}

bool ChatHandler::HandleFusionStatsCommand(char* /*args*/)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled())
    {
        SendSysMessage("Fusion: not enabled.");
        SetSentErrorMessage(true);
        return false;
    }
    // A GM may read their target's; anyone their own.
    Player* who = player;
    if (sFusionMgr.IsAdmin(player))
        if (Player* sel = GetSelectedPlayer())
            who = sel;
    std::istringstream lines(sFusionMgr.CombatStatsReport(who));
    for (std::string line; std::getline(lines, line);)
        SendSysMessage(line.c_str());
    return true;
}
