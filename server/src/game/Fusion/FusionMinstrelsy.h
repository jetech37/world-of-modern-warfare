/*
 * World of Modern Warfare: Minstrelsy in the game (fusion/Minstrelsy.h; design/GAME_DESIGN.md
 * section 12, "Minstrelsy"). GPL-2.0-or-later.
 *
 * A secondary profession every class may learn from a Minstrel Trainer (creature 94100, script
 * npc_fusion_minstrel_trainer; sql/custom/azeroth_warfare/30_minstrelsy.sql), whose window
 * (channel 10 ops 60-62) trains the tiers and sells the Hand Drum and the Lute. The tier and
 * skill are kept in `character_fusion_skill` (key 9002, beside Construction's), the day's tips
 * in `character_fusion_minstrel` (made here at startup).
 *
 * The instruments are Utility tools (FusionTools.cpp): readied in the hands, a strike (op 3)
 * whose target is a song starts a performance (Perform); its notes come in on op 50 and its end
 * on op 52, and the server judges them as the Bard's songs were judged. A landed song puts its
 * buff on its listeners (kept here, on FusionPlayerData::minstrel), told to each (op 55):
 * FusionMgr's speed and damage hooks and this file's regeneration read them. Listeners at an
 * inn may tip (op 58), and the crowd may too, both capped a day.
 *
 * Threads: everything runs on the player's map thread; a song touches only players on that map.
 */

#ifndef AW_FUSION_MINSTRELSY_H
#define AW_FUSION_MINSTRELSY_H

#include "Common.h"
#include "ObjectGuid.h"
#include "fusion/Minstrelsy.h"

#include <map>
#include <string>
#include <vector>

class Creature;
class Player;

// A song being played.
struct FusionMinstrelPerformance
{
    uint32 perf = 0;                // its id on the wire; 0: nothing playing
    uint16 song = 0;
    uint32 startMs = 0;             // the song's clock's zero, on the server's clock
    Fusion::Minstrelsy::Chart chart;
    Fusion::Minstrelsy::Stream stream;
};

// A song this player heard (for a tip).
struct FusionMinstrelHeard
{
    uint64 performer = 0;
    uint32 account = 0;
    uint32 perf = 0;
    uint32 atMs = 0;
    bool tipped = false;
};

// What Minstrelsy keeps for a player (FusionPlayerData::minstrel).
struct FusionMinstrelData
{
    bool loaded = false;
    Fusion::Minstrelsy::Tier tier = Fusion::Minstrelsy::Tier::None;
    uint16 skill = 0;
    bool sent = false;              // the skill told the client
    FusionMinstrelPerformance playing;
    uint32 lastPerf = 0;
    std::map<uint16, uint32> combatReadyMs;         // a combat song again then
    std::vector<uint16> recipes;                     // the instrument recipes learned
    std::vector<Fusion::Minstrelsy::Buff> buffs;    // the songs on this player
    bool buffsDirty = true;
    float speed = 1.f;              // the speed factor last applied
    uint32 nextTickMs = 0;
    std::vector<FusionMinstrelHeard> heard;
    // The day's tips (character_fusion_minstrel).
    uint32 day = 0;
    uint32 given = 0;
    uint32 got = 0;
    uint32 crowd = 0;
    ObjectGuid trainer;             // the trainer the window was opened at
};

class MinstrelMgr
{
public:
    static MinstrelMgr& Instance();

    // Startup: the characters table.
    void Load();
    // Player::Update.
    void UpdatePlayer(Player* player, uint32 diffMs);
    // Channel 10's ops 50, 52, 58, 60 and 61 (FusionMgr::HandleChannel); true when it was one.
    bool HandleChannel(Player* player, std::vector<uint8> const& body);
    // A strike of a readied instrument (FusionTools.cpp ToolStrike): the song begins, or why not
    // (a Gathering::Outcome).
    uint8 Perform(Player* player, uint32 tool, uint16 song);
    // Whether the instrument goes on Utility's bar: Minstrelsy known, and the skill it asks.
    bool Knows(Player const* player, uint32 tool) const;
    // The trainer's gossip (its script): the window.
    bool GossipHello(Player* player, Creature* creature);
    // FusionMgr's hooks: the run speed out of (and in) combat, the damage taken.
    float SpeedFactor(Player const* player) const;
    float DamageTakenFactor(Player const* player) const;
    // A wanderer minstrel's song at an inn (FusionWandererLives.cpp): the players resting near it
    // hear Hearthside Rhythm as if a player had played it well (the same rest and cooldown).
    void WandererSong(Creature* performer);
    // The game masters' .profession (FusionProfessions.cpp): the skill (0: not learned), set to
    // `skill` with the tier that holds it, and every instrument recipe learned (how many new).
    uint16 GmSkillOf(Player* player);
    void GmSetSkill(Player* player, uint16 skill);
    uint32 GmLearnAllRecipes(Player* player);

private:
    struct Said
    {
        bool ok = true;
        std::string text;
    };
    void LoadPlayer(Player* player);
    void SetSkill(Player* player, Fusion::Minstrelsy::Tier tier, uint16 skill, bool announce);
    void SaveDay(Player* player);
    void RollDay(Player* player);
    void TickPerformance(Player* player, uint32 now);
    void Finish(Player* player, uint8 flags);
    void Land(Player* player, Fusion::Minstrelsy::SongDef const& song, float potency, float quality);
    void TickBuffs(Player* player, uint32 now);
    void Tip(Player* player, uint64 performer, uint32 copper);
    void SendWindow(Player* player, bool open, Said const& said = {});
    Said Act(Player* player, Fusion::Minstrelsy::Action a, uint32 arg);
    Said Make(Player* player, uint16 recipe);
    Creature* TrainerNear(Player* player) const;
};

#define sMinstrelMgr MinstrelMgr::Instance()

#endif
