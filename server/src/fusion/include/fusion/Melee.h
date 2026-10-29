/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_MELEE_H
#define FUSION_MELEE_H

#include "fusion/Common.h"

#include <cstdint>
#include <vector>

// The knife is the weapon in the main hand: its kind sets how fast it swings and how hard it hits,
// by weight: maces slowest and hardest, axes between, swords quickest and lightest of the
// three; daggers and fists quick and weak; two-handers slow and heavy. The weapon's own DPS against
// its level's moves that, and the wielder's damage multiplier M (Scale.h) carries the rest.
namespace Fusion { namespace Melee
{
    // 1.12's ItemSubclassWeapon.
    enum Subclass : uint8_t
    {
        Axe = 0, Axe2 = 1, Bow = 2, Gun = 3, Mace = 4, Mace2 = 5, Polearm = 6, Sword = 7,
        Sword2 = 8, Obsolete = 9, Staff = 10, Exotic = 11, Exotic2 = 12, Fist = 13, Misc = 14,
        Dagger = 15, Thrown = 16, Spear = 17, Crossbow = 18, Wand = 19, FishingPole = 20,
    };
    constexpr uint8_t kNoWeapon = 255;

    struct Profile
    {
        uint8_t subclass = kNoWeapon;   // the item's subclass, kNoWeapon for an empty hand
        bool twoHanded = false;
        uint16_t swingMs = 500;
        uint16_t lungeMs = 700;         // the lunge's recovery: 1.4 swings
        float baseDamage = 25.f;        // CoD damage a hit
    };

    // The main hand's profile by its subclass (kNoWeapon: fists). A ranged or odd kind in the main
    // hand swings as the nearest melee kind (a spear as a polearm, a fishing pole as a staff).
    Profile ProfileFor(uint8_t subclass);

    // The weapon against its level: clamp(dps / (3 + 0.9 level), 0.6, 2); 1 for an empty hand.
    float WeaponFactor(float weaponDps, uint32_t level, bool hasWeapon);
    // A hit's CoD damage: base x weapon factor x M.
    float Damage(Profile const& p, float weaponDps, uint32_t level, bool hasWeapon, float multiplier);

    // Whether a swing (or lunge) `sinceLastMs` after the last may land: never quicker than the
    // weapon swings (10% slack for the network's jitter).
    bool Ready(uint32_t sinceLastMs, uint16_t recoveryMs);

    // ── Combos: timing the swings (design/BALANCE_GUNS_VS_CLASSES.md) ──
    // A blow that lands within kComboWindowMs after the weapon has recovered from the last one
    // carries the chain on: the second hits kComboSecond as hard, the third (the finisher)
    // kComboFinisher, and the chain starts over. Waiting too long, or a swing that meets nothing,
    // breaks it. A rhythm, not mashing: a swing is never quicker than the weapon (Ready).
    constexpr uint32_t kComboWindowMs = 450;
    constexpr uint8_t kComboLength = 3;
    constexpr float kComboSecond = 1.1f;
    constexpr float kComboFinisher = 1.5f;
    struct Combo
    {
        uint8_t step = 0;           // the last blow's place in the chain (1..kComboLength), 0 none
        uint32_t lastHitMs = 0;
    };
    // The multiplier of a blow at `step` (1-based).
    float ComboMultiplier(uint8_t step);
    // A blow landed at `nowMs` by a weapon swinging every `swingMs`: the chain moves on (or starts
    // over) and the blow's multiplier comes back.
    float ComboHit(Combo& c, uint32_t nowMs, uint16_t swingMs);
    // A swing that met nothing: the chain is broken.
    inline void ComboBreak(Combo& c) { c.step = 0; }

    // ── Where the blow lands ──
    // From behind (more than 120 degrees off the target's facing) a blow hits kBehindMultiplier as
    // hard; from the flank (more than 60) kFlankMultiplier: moving round a gunner pays.
    constexpr float kBehindMultiplier = 1.25f;
    constexpr float kFlankMultiplier = 1.1f;
    // `offFacing`: the angle (radians, 0..pi) between where the target faces and where the
    // attacker stands.
    float PositionalMultiplier(float offFacing);

    // ── The shield raised (the Classic layer's right button with a shield in the off hand) ──
    // Raised, it takes kGuardBulletCut off bullets and kGuardBlowCut off blows from within
    // kGuardHalfAngle of the bearer's facing. Raised in time, it parries: for kPerfectBlockMs after
    // it comes up, whatever it meets from in front does nothing. A perfect block is armed only by a
    // raise kPerfectBlockRearmMs after the last one, so flicking the shield up and down is no wall.
    // (The Vanguard's Bulwark still stops every frontal round, and is the tank's.)
    constexpr float kGuardHalfAngle = 1.0472f;      // 60 degrees either side
    constexpr float kGuardBulletCut = 0.5f;
    constexpr float kGuardBlowCut = 0.75f;
    constexpr uint32_t kPerfectBlockMs = 300;
    constexpr uint32_t kPerfectBlockRearmMs = 1200;
    struct Guard
    {
        bool up = false;
        uint32_t upSinceMs = 0;
        uint32_t lastRaiseMs = 0;
        bool raisedBefore = false;
        bool perfectArmed = false;  // this raise may parry
    };
    // The shield up or down at `nowMs`.
    void GuardUpdate(Guard& g, bool up, uint32_t nowMs);
    enum class Block : uint8_t { None, Partial, Perfect };
    Block GuardBlocks(Guard const& g, bool fromFront, uint32_t nowMs);
    // What a hit keeps after the block: 1 none, 0 a perfect one.
    float BlockMultiplier(Block b, bool bullet);

    // A plain swing rides CMSG_FUSION_SHOTS with this weapon id (origin and direction the eye's).
    constexpr uint16_t kSwingWeaponId = 4999;
    // How far a swing reaches past the bodies' edges, and its cone either side of the aim.
    constexpr float kReach = 2.5f;
    constexpr float kConeHalf = 0.8f;   // radians

    // What the off hand holds, for the client's hands (WOMW: a weapon or spell in either hand, a
    // shield in the left, a two-hander in both).
    enum class OffHandKind : uint8_t
    {
        Empty = 0,
        Weapon = 1,     // dual wield
        Shield = 2,
        Held = 3,       // a held-in-off-hand item: a book, an orb, a lantern
    };
    struct OffHand
    {
        OffHandKind kind = OffHandKind::Empty;
        uint8_t subclass = kNoWeapon;   // a weapon's ItemSubclassWeapon
        uint32_t displayId = 0;
    };

    // SMSG_FUSION_CHANNEL (kChannelWorld) op 20, 21 bytes: [20, subclass u8, two_handed u8,
    // swing_ms u16, lunge_ms u16, item_entry u32, display_id u32, off_kind u8, off_subclass u8,
    // off_display u32], little-endian. (The first 15 bytes are the message before the off hand.)
    constexpr uint8_t kOpProfile = 20;
    std::vector<uint8_t> EncodeProfile(Profile const& p, uint32_t itemEntry, uint32_t displayId,
        OffHand const& off = {});
}}

#endif
