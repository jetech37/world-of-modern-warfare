/*
 * World of Modern Warfare: the class identity pass (design/BALANCE_GUNS_VS_CLASSES.md, part 5). One
 * place for every number that tunes a class's own way to fight against guns: each option's damage
 * scale, the basic spell's scale by the kit's lead archetype, which options make their bolts home,
 * and the identity rules (Shatter, Charging through fire, Atonement, Ambush's sundering, Drain's
 * pressure). The game reads them in FusionClasses::AdjustDamage / AdjustDealt / CastOption / Apply;
 * the duel model (fusion/Balance.h Duel) reads the same rows, so what the sim says is what plays.
 * GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CLASS_TUNING_H
#define FUSION_CLASS_TUNING_H

#include "fusion/Classes.h"

#include <cstdint>
#include <vector>

namespace Fusion::ClassTuning
{
    // ── Per option ──
    // `damage`: a multiplier on the damage of the option's own spells (direct hits and their
    // ticks), on top of the ranged-spell power (ClassEffects.h RangedSpellScale). Custom strikes
    // whose %EH is a constant in ClassEffects.h (Lunge, Blade Blink, Ground Smash...) are tuned
    // there, not here. `homing`: the option's bolts curve toward their target
    // (Spellcasting.h kGrantedHoming) while it is in the kit: homing is a feature of a kit pick,
    // never the default.
    struct Row
    {
        uint16_t option = 0;
        float damage = 1.f;
        bool homing = false;
        char const* why = "";
    };
    std::vector<Row> const& Rows();
    Row const* RowOf(uint16_t option);
    float DamageScale(uint16_t option);         // 1 for an unlisted option
    bool GrantsHoming(uint16_t option);

    // ── The basic spell (Smite, Wrath, Shadow Bolt, Fireball, Lightning Bolt, Exorcism) ──
    // The archetype the kit leans on: the one with the most picks (a tie goes to the archetype of
    // the highest tier among the tied), -1 for none.
    int LeadArchetype(uint8_t cls, Classes::Picks const& picks);
    // The basic spell's damage multiplier for a kit leaning on `archetype` (-1: none, 1).
    float BasicSpellScale(uint8_t cls, int archetype);

    // Against creatures, a class spell from range (the same spells RangedSpellScale doubles) hits
    // this much harder again: a caster clears a normal creature in about 3 s instead of 5-9, so
    // questing without a gun is a real way to play (a rifle still does it in a third of a second
    // while its ammunition lasts).
    constexpr float kCreatureSpellPower = 1.75f;

    // ── Identity rules ──
    // Healers (Lightwarden, Cleric, Oracle, Totemist, Grovekeeper): they win attrition, not trades.
    bool IsHealerArchetype(uint8_t cls, int archetype);
    // Atonement: a healer-led kit's basic spell heals the caster for this share of what it dealt.
    constexpr float kAtonementShare = 0.6f;
    float AtonementHeal(float dealt, bool healerLead);

    // Wizard Shatter: a Wizard-led kit's spells hit a frozen or rooted target (Frost Nova,
    // Frostbolt's chill does not count; a root does) this much harder. The burst combo: Nova or a
    // root, then Fireball / Arcane Missiles into it.
    constexpr float kShatterBonus = 1.5f;
    float ShatterMultiplier(bool wizardLead, bool targetHeld);

    // Charging through fire: the warrior's gap-closers (Charge, Intercept, Shield Charge) and the
    // Duelist's Lunge, Blade Blink and Fel Leap give this cut of bullet damage for kGapGuardMs from
    // the cast, so closing the distance on a rifle is a skill play, not a death march.
    constexpr float kGapGuardBulletCut = 0.5f;
    constexpr uint32_t kGapGuardMs = 2500;
    bool IsGapCloser(uint16_t option);

    // A spirit wolf is hard to hit: Ghost Wolf (every shaman's core) takes this share of bullets,
    // the Earthbreaker's way through a rifle's field of fire (firing breaks the form).
    constexpr float kGhostWolfBulletCut = 0.65f;

    // The hunter's bond: a hunter's battle companion (fusion/Companions.h) fights as the hunter
    // pet it stands in for: these multipliers on its stats (the cap under a hunter pet is lifted
    // for hunters only). A companion attacking the hunter's Mark Prey takes kBondPreyBonus more.
    constexpr float kHunterBondHealth = 1.3f;
    constexpr float kHunterBondDamage = 1.5f;
    constexpr float kBondPreyBonus = 1.3f;
    bool HunterBond(uint8_t cls);

    // The trap's snap: any hunter trap that goes off on a player stuns it this long (once per
    // trap set off, not again for the traps it chains), so a freeze is not wasted on the first hit.
    constexpr uint32_t kTrapSnapMs = 1500;

    // Ambush's sundering: an Ambush that lands from stealth or the opening disarms a player's gun
    // for this long (the gun and the weapon knocked aside, as a warrior's Disarm), the Assassin's
    // "delete a gunner" window.
    constexpr uint32_t kSunderDisarmMs = 2000;

    // Drain pressure (Afflicter): while the warlock's afflictions tick on a target, each
    // different one cuts that target's healing taken by this much (stacks to kDrainHealCutMax).
    constexpr float kDrainHealCutPerAffliction = 0.15f;
    constexpr float kDrainHealCutMax = 0.45f;
    float DrainHealMultiplier(uint32_t afflictions);
}

#endif
