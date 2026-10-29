/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_KITS_H
#define FUSION_KITS_H

#include "fusion/Achievements.h"
#include "fusion/Common.h"
#include "fusion/Killstreak.h"
#include "fusion/Loadout.h"

#include <vector>

// Kits (WOMW design, section 3). A kit is a Create-a-Class class (Loadout.h) plus its talent picks
// and killstreaks; a character has up to five. They are viewed anywhere (the HUD's Kits button) but
// edited and switched only near a kit station (any trainer or innkeeper: "Edit your kits" in their
// gossip), through the field editor (once an hour, from the kit window), or by a GM.
namespace Fusion
{
    // ── Kit stations ──
    // vmangos UNIT_NPC_FLAG_TRAINER (class, profession and weapon trainers) and _INNKEEPER.
    constexpr uint32_t kNpcFlagGossip = 0x01;
    constexpr uint32_t kNpcFlagTrainer = 0x10;
    constexpr uint32_t kNpcFlagInnkeeper = 0x80;
    // How near a station kits change: its room, not the street outside.
    constexpr float kKitStationReach = 15.f;

    // Whether a creature with these npc flags is a kit station (a trainer or an innkeeper).
    bool IsKitStation(uint32_t npcFlags);
    // Whether a creature lets the player change kits where they stand: a kit station, alive, not
    // hostile to them, within reach.
    bool KitStationAllows(uint32_t npcFlags, bool alive, bool hostile, float distance);

    // What a player may do with their kits where they stand.
    enum class KitAccess : uint8_t
    {
        View,           // look only
        Station,        // near a trainer or innkeeper
        Field,          // the field editor's window is open
        Gm,
    };

    KitAccess KitAccessOf(bool isGm, bool atStation, bool fieldOpen);
    bool CanChangeKits(KitAccess a);

    // Whether a kit request changes anything: it saves a kit, or puts another kit in hand than
    // the one there. (Asking for the kit already in hand changes nothing.)
    bool KitRequestChanges(bool saves, uint8_t equipSlot, uint8_t equippedSlot);

    // ── Opening the kit window ──
    // Who opened it. The HUD's button views only, even beside a trainer (the client's
    // Access::Hud); a station's "Edit your kits" and the field editor open it to edit (its
    // Access::Npc). The server still checks every request where the player stands.
    enum class KitOpener : uint8_t
    {
        Hud = 0,
        Station = 1,
        Field = 2,
    };
    bool OpenerMayEdit(KitOpener o);

    // The world channel message that opens the kit window: [kOpOpenClasses, opener], and for the
    // field editor the seconds its window stays open, u16 little-endian.
    std::vector<uint8_t> OpenKitEditor(KitOpener opener, uint16_t fieldSeconds = 0);
    bool DecodeOpenKitEditor(std::vector<uint8_t> const& body, KitOpener& opener, uint16_t& fieldSeconds);

    // ── The field editor ──
    // A kit station anywhere, for a short window, once an hour: opened from the kit window
    // (build channel op 31) or by the old Field Quartermaster's Ledger item (op 30, its guid). The
    // first change made through a window starts the hour, which is saved with the character. Times
    // are unix seconds, so the cooldown runs on across logouts and restarts.
    constexpr uint32_t kFieldEditorCooldownSeconds = 3600;
    constexpr uint32_t kFieldEditorWindowSeconds = 120;
    // The Field Quartermaster's Ledger: no longer sold (its Quartermasters are gone); a copy in the
    // bags still opens the field editor, on the same cooldown.
    constexpr uint32_t kLedgerItem = 93001;
    // Build/loadout channel ops (client): [30, item guid u64] the ledger used; [31] the field
    // editor asked for from the kit window; [32] the kit window opened: send the kit status.
    constexpr uint8_t kOpUseLedger = 30;
    constexpr uint8_t kOpFieldEdit = 31;
    constexpr uint8_t kOpAskKitStatus = 32;

    struct FieldEditor
    {
        uint64_t readyAt = 0;       // when it can be used again (saved)
        uint64_t openUntil = 0;     // the end of the window it opened
        bool spent = false;         // a change was made through this window
    };

    enum class FieldEditorUse : uint8_t
    {
        Opened,
        OnCooldown,
    };

    FieldEditorUse UseFieldEditor(FieldEditor& f, uint64_t now);
    bool FieldEditorOpen(FieldEditor const& f, uint64_t now);
    // A change made through the field editor: true when it started the cooldown (save `readyAt`).
    // `cooldown`: seconds until it is ready again (One Man Army Pro halves it, Perks.h).
    bool SpendFieldEditor(FieldEditor& f, uint64_t now, uint32_t cooldown = kFieldEditorCooldownSeconds);
    // Seconds until it can be used again (0: ready), and left in its open window.
    uint32_t FieldEditorSecondsLeft(FieldEditor const& f, uint64_t now);
    uint32_t FieldEditorWindowLeft(FieldEditor const& f, uint64_t now);

    bool DecodeUseLedger(std::vector<uint8_t> const& body, uint64_t& guid);

    // ── Rearming at a kit station ──
    // Every kit station's gossip (any trainer or innkeeper) offers "Rearm" beside "Edit your
    // kits" (WOMW design, section 9): every gun's magazine and reserve full and the equipment
    // back, free as talking to one already is, and the special rounds the character has
    // unlocked topped up for the quartermaster's fee (Ammo.h QuartermasterFee) when it can pay.
    enum class StationRearm : uint8_t
    {
        Rounds,     // refilled, and the special rounds bought
        NoMoney,    // refilled; the rounds' fee is more than the character has
    };
    StationRearm PlanStationRearm(uint32_t money, uint32_t fee);
    // The gossip line, with the rounds' fee ("Rearm: ammunition, equipment and special rounds (48s)").
    std::string RearmGossipText(uint32_t fee);
    // What the player is told after.
    std::string RearmResultText(StationRearm r, uint32_t fee);

    // ── The kit status: what the window shows of where kits can change ──
    // World channel op 42: [42, flags u8, field ready in u32 s, field open for u16 s]. Flags: 1
    // kits can change here now, 2 the field editor's window is open, 4 a GM.
    constexpr uint8_t kOpKitStatus = 42;
    constexpr uint8_t kKitStatusCanEdit = 0x01;
    constexpr uint8_t kKitStatusFieldOpen = 0x02;
    constexpr uint8_t kKitStatusGm = 0x04;
    struct KitStatus
    {
        uint8_t flags = 0;
        uint32_t fieldReadyIn = 0;
        uint16_t fieldOpenFor = 0;
        bool operator==(KitStatus const& o) const
        {
            return flags == o.flags && fieldReadyIn == o.fieldReadyIn && fieldOpenFor == o.fieldOpenFor;
        }
    };
    KitStatus KitStatusOf(KitAccess access, FieldEditor const& f, uint64_t now);
    std::vector<uint8_t> EncodeKitStatus(KitStatus const& s);
    bool DecodeKitStatus(std::vector<uint8_t> const& body, KitStatus& out);

    // ── A kit's killstreaks ──
    // The rewards a kit plays with: the ones it chose (streak1-3, in kill order), else the whole
    // ladder; only those the account has unlocked either way.
    std::vector<StreakReward> KitStreaks(ClassLoadout const* kit, std::vector<StreakReward> const& ladder,
        UnlockSet const& unlocks);
}

#endif
