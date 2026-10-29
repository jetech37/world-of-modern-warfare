/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_GATHERING_H
#define FUSION_GATHERING_H

#include "fusion/ActionSystems.h"

#include <cstdint>

// Utility's tools (WOMW): the trades' tools held in the hands in first person, on Utility's bar
// alone (ActionSystems.h). With one readied, the fire button strikes with it at what the
// crosshair is on: a pick at an ore vein, a knife at a skinnable corpse, bare hands at a herb, an
// axe at a lumber tree, a fishing pole at the water, a smith's hammer at an anvil, a Repair Hammer
// at a built piece (Maintenance.h).
//
// The work is physical: a vein takes 2 to 4 swings of the pick, fewer the more the gatherer
// outskills it, and only the last one gathers. That last strike runs WoW's own gathering (the
// open-lock or skinning spell, cast for the player by the server), so the skill asked, the loot,
// the node's depletion and the skill-ups are vanilla's. The tools are vanilla's own requirements:
// the Mining Pick and the Skinning Knife the gathering spells ask for (their Totem), any fishing
// pole for Fishing, the Blacksmith Hammer for Blacksmithing; the Woodcutting Axe is Construction's
// (Construction.h), the Repair Hammer a builder's (Maintenance.h). A tool in the bags or in hand
// will do, as the totems in vanilla.
namespace Fusion { namespace Gathering
{
    using ActionSystems::Ability;

    // The vanilla skills (SkillLine.dbc) and tools (item entries).
    constexpr uint16_t kSkillMining = 186;
    constexpr uint16_t kSkillHerbalism = 182;
    constexpr uint16_t kSkillSkinning = 393;
    constexpr uint16_t kSkillFishing = 356;
    constexpr uint16_t kSkillBlacksmithing = 164;
    // Construction (Construction.h kSkillConstruction): the axe's and the Repair Hammer's trade.
    constexpr uint16_t kSkillConstruction = 9001;
    // Minstrelsy (Minstrelsy.h kSkillMinstrelsy): the instruments' trade.
    constexpr uint16_t kSkillMinstrelsy = 9002;
    constexpr uint32_t kMiningPick = 2901;
    constexpr uint32_t kSkinningKnife = 7005;
    constexpr uint32_t kBlacksmithHammer = 5956;
    constexpr uint32_t kWoodcuttingAxe = 91010;
    // The Repair Hammer (world DB, 25_utility_tools.sql): sold by the construction supplier and
    // made by smiths.
    constexpr uint32_t kRepairHammer = 91011;
    // Any fishing pole is an item of the weapon class, subclass 20.
    constexpr uint8_t kWeaponClass = 2;
    constexpr uint8_t kFishingPoleSubclass = 20;

    // What a strike lands on.
    enum class Work : uint8_t
    {
        None,
        Vein,       // a gameobject whose lock asks Mining
        Herb,       // a gameobject whose lock asks Herbalism
        Corpse,     // a skinnable creature
        Water,      // no target: the line goes out ahead
        Lumber,     // a lumber node (Construction.h Nodes)
        Anvil,      // a smith's anvil or forge
        Piece,      // a built piece (Maintenance.h)
        Song,       // an instrument: no target, the strike's target names the song (Minstrelsy.h)
    };

    struct ToolDef
    {
        Ability ability;
        char const* name;
        uint16_t skill;         // the trade it works (0: none asked)
        uint32_t item;          // the item it asks (0: none; fishing: any pole)
        Work work;
        uint32_t strikeMs;      // one swing, as the client paces it
    };
    // The tool of an ability id; null for anything else.
    ToolDef const* ToolInfo(uint32_t ability);

    // Whether a tool works what it struck.
    bool Fits(uint32_t ability, Work w);

    // How a strike went (the server's op 4).
    enum class Outcome : uint8_t
    {
        Struck = 0,         // a swing landed; more to go
        Gathered = 1,       // the last swing: WoW's gathering ran
        Cast = 2,           // the line went out
        Reeled = 3,         // the bobber pulled in
        Repaired = 4,       // a piece mended a step
        NoTool = 5,         // the tool's item isn't in the bags
        NotLearned = 6,     // the trade isn't known
        NoTarget = 7,       // nothing there to work
        TooFar = 8,
        WrongTarget = 9,    // the wrong tool for it (a pick at a herb)
        TooSoon = 10,       // faster than the tool swings
        Failed = 11,        // WoW refused (the skill asked, a node in use or gone, nothing to skin)
        NoMaterials = 12,   // a repair wants materials the bags lack
        NotDamaged = 13,    // the piece is whole
        RecentlyDamaged = 14,   // the piece was hit within Maintenance::kRepairAfterDamage
        NotYours = 15,      // a piece the player may not touch
        Opened = 16,        // the smith's hammer opened the trade's window
        // Minstrelsy (Minstrelsy.h Refusal): the song begins, or why not.
        Performing = 17,
        InCombat = 18,      // only the combat songs are played in combat
        SkillTooLow = 19,
        NotAtInn = 20,      // an inn song wants an inn or a city
        NotAtFire = 21,     // a fire song wants a campfire near
        Cooldown = 22,      // a combat song played moments ago
        NoSong = 23,        // no such song for that instrument
    };

    // What the player brings: the tool's item (in the bags or in hand), the trade learned. A game
    // master needs neither.
    struct Holder
    {
        bool hasItem = false;
        bool learned = false;
        bool gm = false;
    };
    // Struck when the tool may be used; NoTool or NotLearned when not.
    Outcome CheckTool(ToolDef const& t, Holder const& h);
    // Whether Utility's bar shows the tool (ActionSystems.h op 9): the player has it (its item in
    // the bags or equipped; Herbalism asks none) and is trained for it (its trade known; the
    // Repair Hammer asks none: owning it is enough). A game master's pass doesn't count: the bar
    // shows what the character really has.
    bool OnUtilityBar(ToolDef const& t, Holder const& h);
    // The tools mask of op 9 for a holder per tool.
    template <typename HolderOf>
    uint16_t UtilityBarTools(HolderOf&& holderOf)
    {
        uint16_t mask = 0;
        for (uint32_t id = ActionSystems::kFirstTool; id <= ActionSystems::kLastTool; ++id)
            if (ToolDef const* t = ToolInfo(id))
                if (OnUtilityBar(*t, holderOf(*t)))
                    mask |= ActionSystems::ToolBit(id);
        return mask;
    }

    // How many swings a gather takes: a vein or a tree 2 to 4 (4 while the node is orange to the
    // gatherer, 3 to yellow and green, 2 once it is far below them), a herb or a corpse 1 to 3, the
    // rest 1. `required` is the skill the node or corpse asks, `skill` the gatherer's.
    uint8_t StrikesFor(uint32_t ability, uint16_t required, uint16_t skill);
    // A node is "far below" a gatherer from this many points over its requirement (vanilla's gray).
    constexpr uint16_t kEasyMargin = 75;
    // Orange: under this many points over the requirement (vanilla's yellow).
    constexpr uint16_t kHardMargin = 25;

    // Vanilla's skinning requirement for a creature level (Spell::CheckCast): (level - 10) * 10
    // under 100 skill, level * 5 from 100.
    uint16_t SkinningRequired(uint32_t level, uint16_t skill);

    // The reach of a hand tool, to the target's nearest point (vanilla's interaction distance and a
    // little slack for the network).
    constexpr float kReach = 5.5f;

    // A gatherer's work on one target: its strikes so far. A strike on another target, with
    // another tool, or after a pause longer than kStrikeWindowMs starts over.
    constexpr uint32_t kStrikeWindowMs = 4000;
    struct Progress
    {
        uint32_t tool = 0;
        uint64_t target = 0;
        uint8_t done = 0;
        uint32_t lastMs = 0;
    };
    enum class Step : uint8_t { Counted, Complete, TooSoon };
    // A strike at `nowMs` toward `needed`: Complete on the last (and the progress clears), TooSoon
    // when faster than three quarters of the tool's swing (nothing counted).
    Step Strike(Progress& p, uint32_t tool, uint64_t target, uint8_t needed, uint32_t nowMs);
    // Another tool readied: the strikes counted so far are gone, but the last swing's time stays,
    // so changing tools between strikes never skips the wait.
    void ToolChanged(Progress& p);
    // The shortest gap between two strikes the server takes.
    uint32_t MinGapMs(uint32_t ability);
}}

#endif
