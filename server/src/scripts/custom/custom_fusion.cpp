/*
 * World of Modern Warfare: the Construction NPCs' and lumber nodes' scripts. They only hand over
 * to the fusion (game/Fusion/FusionConstruction.cpp), which keeps the profession, the plots and
 * their rules; see src/fusion/docs/CONSTRUCTION.md. The script names are the ones
 * sql/custom/azeroth_warfare/03_construction.sql gives the creatures and gameobjects.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "scriptPCH.h"
#include "custom.h"
#include "Fusion/FusionMgr.h"
#include "Fusion/FusionBounties.h"
#include "Fusion/FusionShips.h"
#include "Chat.h"
#include "fusion/Furniture.h"

// The Construction Trainer, the Plot Steward and the Homestead Courier share one gossip: the
// fusion tells them apart by entry.
bool GossipHello_npc_fusion_construction(Player* player, Creature* creature)
{
    return sFusionMgr.ConstructionGossipHello(player, creature);
}

bool GossipSelect_npc_fusion_construction(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    return sFusionMgr.ConstructionGossipSelect(player, creature, sender, action);
}

// A lumber node used: the Construction skill, the axe and the node's skill gate it (true: handled,
// the loot opened or the use refused).
bool GOHello_fusion_lumber(Player* player, GameObject* go)
{
    return sFusionMgr.UseLumberNode(player, go);
}

// Opened by a spell instead (should a node ever be given a lock): the skill-up only.
bool GOOpen_fusion_lumber(Player* player, GameObject* go)
{
    sFusionMgr.OpenedLumberNode(player, go);
    return false;
}

// A Minstrel Trainer (sql/custom/azeroth_warfare/30_minstrelsy.sql): the Minstrelsy window, on
// fusion channel 10 (game/Fusion/FusionMinstrelsy.cpp).
bool GossipHello_npc_fusion_minstrel_trainer(Player* player, Creature* creature)
{
    return sMinstrelMgr.GossipHello(player, creature);
}

// An Inscription or Jewelcrafting Trainer (sql/custom/azeroth_warfare/35_inscription_jewelcrafting.sql):
// its trade's window, on the professions channel 12 (game/Fusion/FusionCrafts.cpp).
bool GossipHello_npc_fusion_craft_trainer(Player* player, Creature* creature)
{
    return sCraftsMgr.GossipHello(player, creature);
}

// A bounty board used (sql/custom/azeroth_warfare/27_bounty_boards.sql): its window, on fusion
// channel 11 (game/Fusion/FusionBounties.cpp).
bool GossipHello_npc_fusion_shipwright(Player* player, Creature* creature)
{
    return sShipMgr.GossipHello(player, creature);
}

bool GossipSelect_npc_fusion_shipwright(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    return sShipMgr.GossipSelect(player, creature, sender, action);
}

bool GOHello_fusion_bounty_board(Player* player, GameObject* go)
{
    return sBountyMgr.UseBoard(player, go);
}

// A companion tamer or a Companion Battle Trainer (sql/custom/azeroth_warfare/33_companion_battles.sql):
// its gossip, then a battle on fusion channel 13 (game/Fusion/FusionCompanionBattles.cpp).
bool GossipHello_npc_fusion_companion_tamer(Player* player, Creature* creature)
{
    return sFusionMgr.CompanionTamerHello(player, creature);
}

bool GossipSelect_npc_fusion_companion_tamer(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    return sFusionMgr.CompanionTamerSelect(player, creature, sender, action);
}

// A training dummy (furniture, fusion/Furniture.h): the server stands one in each dummy piece
// (game/Fusion/FusionFurniture.cpp). It never fights back, never moves and never dies; each
// player striking it has a meter, and when they stop it tells them their damage per second.
struct npc_fusion_training_dummyAI : ScriptedAI
{
    explicit npc_fusion_training_dummyAI(Creature* c) : ScriptedAI(c) { npc_fusion_training_dummyAI::Reset(); }

    std::unordered_map<ObjectGuid, Fusion::Furniture::DpsMeter> meters;
    uint32 checkTimer = 1000;

    void Reset() override {}
    void AttackStart(Unit*) override {}
    void MoveInLineOfSight(Unit*) override {}
    void Aggro(Unit*) override { SetCombatMovement(false); }

    void DamageTaken(Unit* who, uint32& damage) override
    {
        if (who)
            if (Player* p = who->GetCharmerOrOwnerPlayerOrPlayerItself())
                meters[p->GetObjectGuid()].Hit(WorldTimer::getMSTime(), damage);
        // Never dies: a hit that would leave it under a tenth heals it whole.
        if (m_creature->GetHealth() <= damage + m_creature->GetMaxHealth() / 10)
        {
            damage = 0;
            m_creature->SetHealth(m_creature->GetMaxHealth());
        }
    }

    void UpdateAI(uint32 const diff) override
    {
        if (checkTimer > diff)
        {
            checkTimer -= diff;
            return;
        }
        checkTimer = 1000;
        uint32 const now = WorldTimer::getMSTime();
        for (auto it = meters.begin(); it != meters.end();)
        {
            if (!it->second.Idle(now))
            {
                ++it;
                continue;
            }
            if (Player* p = m_creature->GetMap()->GetPlayer(it->first))
            {
                ChatHandler(p).PSendSysMessage("Training dummy: %s.", Fusion::Furniture::DpsReport(it->second).c_str());
                m_creature->GetThreatManager().modifyThreatPercent(p, -101.0f);
                m_creature->_removeAttacker(p);
            }
            it = meters.erase(it);
        }
        if (meters.empty() && m_creature->IsInCombat())
        {
            m_creature->SetHealth(m_creature->GetMaxHealth());
            EnterEvadeMode();
        }
    }
};

CreatureAI* GetAI_npc_fusion_training_dummy(Creature* c)
{
    return new npc_fusion_training_dummyAI(c);
}

void AddSC_custom_fusion()
{
    Script* newscript;

    newscript = new Script;
    newscript->Name = "npc_fusion_training_dummy";
    newscript->GetAI = &GetAI_npc_fusion_training_dummy;
    newscript->RegisterSelf(false);

    for (char const* name : {"npc_fusion_construction_trainer", "npc_fusion_plot_steward", "npc_fusion_homestead_courier"})
    {
        newscript = new Script;
        newscript->Name = name;
        newscript->pGossipHello = &GossipHello_npc_fusion_construction;
        newscript->pGossipSelect = &GossipSelect_npc_fusion_construction;
        newscript->RegisterSelf(false);
    }

    newscript = new Script;
    newscript->Name = "npc_fusion_shipwright";
    newscript->pGossipHello = &GossipHello_npc_fusion_shipwright;
    newscript->pGossipSelect = &GossipSelect_npc_fusion_shipwright;
    newscript->RegisterSelf(false);

    newscript = new Script;
    newscript->Name = "npc_fusion_minstrel_trainer";
    newscript->pGossipHello = &GossipHello_npc_fusion_minstrel_trainer;
    newscript->RegisterSelf(false);

    newscript = new Script;
    newscript->Name = "npc_fusion_craft_trainer";
    newscript->pGossipHello = &GossipHello_npc_fusion_craft_trainer;
    newscript->RegisterSelf(false);

    newscript = new Script;
    newscript->Name = "npc_fusion_companion_tamer";
    newscript->pGossipHello = &GossipHello_npc_fusion_companion_tamer;
    newscript->pGossipSelect = &GossipSelect_npc_fusion_companion_tamer;
    newscript->RegisterSelf(false);

    newscript = new Script;
    newscript->Name = "go_fusion_lumber";
    newscript->pGOHello = &GOHello_fusion_lumber;
    newscript->pGOOpen = &GOOpen_fusion_lumber;
    newscript->RegisterSelf(false);

    newscript = new Script;
    newscript->Name = "go_fusion_bounty_board";
    newscript->pGOHello = &GOHello_fusion_bounty_board;
    newscript->RegisterSelf(false);
}
