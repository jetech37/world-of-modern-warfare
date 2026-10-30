/*
 * World of Modern Warfare: Inscription and Jewelcrafting in the game (fusion/Crafts.h;
 * design/GAME_DESIGN.md section 12, "Inscription and Jewelcrafting"). GPL-2.0-or-later.
 *
 * Two primary professions every class may learn from their trainers (creatures 94300 and 94450,
 * script npc_fusion_craft_trainer; sql/custom/azeroth_warfare/35_inscription_jewelcrafting.sql),
 * whose windows (professions channel 12, ops 20-24) train the tiers, teach the recipes, sell the
 * basic goods and unlearn. Learning one takes a primary profession slot (vanilla's free
 * profession points, given back on unlearning). The tier and skill are kept in
 * `character_fusion_skill` (keys 9003 and 9004), the recipes learned in
 * `character_fusion_craft_recipe`. Made at the trade window (the stock TradeSkillFrame the client
 * mirrors; Create sends op 21 Make).
 *
 * Glyphs and gems go into the kits (ops 30-32): a kit's glyph slots and its guns' sockets, kept in
 * `character_fusion_kit_glyph` and `character_fusion_kit_gem` by kit slot, changed where kits
 * change (a kit station, the field editor, a GM). The kit carried's glyphs feed the perks' hooks
 * (FusionPlayerData::augments) and, with the gun in hand's gems, the handling (UpdateCombatStats).
 *
 * Threads: everything runs on the player's map thread.
 */

#ifndef AW_FUSION_CRAFTS_H
#define AW_FUSION_CRAFTS_H

#include "Common.h"
#include "ObjectGuid.h"
#include "fusion/Crafts.h"
#include "fusion/Loadout.h"

#include <array>
#include <string>
#include <vector>

class Creature;
class Player;

// One trade's state for a player.
struct FusionCraftTrade
{
    Fusion::Crafts::Tier tier = Fusion::Crafts::Tier::None;
    uint16 skill = 0;
    std::vector<uint16> recipes;        // learned (starters are known without a row)
};

// What the two trades and the kits' augments keep for a player (FusionPlayerData::crafts).
struct FusionCraftsData
{
    bool loaded = false;
    bool sent = false;                  // the skills and books told the client
    bool slotsTaken = false;            // the primary profession slots taken from vanilla's count
    std::array<FusionCraftTrade, Fusion::Crafts::kTrades> trades;
    std::array<Fusion::Crafts::KitGlyphs, Fusion::kClassSlots> glyphs{};
    std::array<Fusion::Crafts::KitGems, Fusion::kClassSlots> gems{};
    uint16 lastHeld = 0;                // the gun in hand when the handling was last worked out
    uint8 lastKit = 255;                // the kit carried when the client was last told its augments
    ObjectGuid trainer;                 // the trainer the window was opened at
    Fusion::Crafts::Trade trainerTrade = Fusion::Crafts::Trade::None;

    FusionCraftTrade& Of(Fusion::Crafts::Trade t) { return trades[size_t(t) - 1]; }
    FusionCraftTrade const& Of(Fusion::Crafts::Trade t) const { return trades[size_t(t) - 1]; }
};

class CraftsMgr
{
public:
    static CraftsMgr& Instance();

    // Startup: the characters tables.
    void Load();
    // Player::Update: loads at the first, takes the profession slots, tells the client.
    void UpdatePlayer(Player* player, uint32 diffMs);
    // The professions channel's ops 20, 21, 30 and 31 (FusionMgr::HandleChannel); true when one.
    bool HandleChannel(Player* player, std::vector<uint8> const& body);
    // A trainer's gossip (its script): the window.
    bool GossipHello(Player* player, Creature* creature);
    // FusionMgr::UpdateCombatStats: the kit's glyphs and the gun in hand's gems on the handling;
    // the kit's other glyph effects into FusionPlayerData::augments.
    void ApplyHandling(Player* player, Fusion::Stats::Derived& combat);
    // A trade's skill (0: not learned), for Jewelcrafting's own socket and the GM tools.
    uint16 SkillOf(Player const* player, Fusion::Crafts::Trade t) const;
    // The game masters' .profession (FusionProfessions.cpp).
    uint16 GmSkillOf(Player* player, Fusion::Crafts::Trade t);
    void GmSetSkill(Player* player, Fusion::Crafts::Trade t, uint16 skill);
    uint32 GmLearnAllRecipes(Player* player, Fusion::Crafts::Trade t);

private:
    struct Said
    {
        bool ok = true;
        std::string text;
    };
    void LoadPlayer(Player* player);
    void TakeSlots(Player* player);
    void SetSkill(Player* player, Fusion::Crafts::Trade t, Fusion::Crafts::Tier tier, uint16 skill, bool announce);
    void SendSkill(Player* player, Fusion::Crafts::Trade t);
    void SendBook(Player* player, Fusion::Crafts::Trade t);
    void SendWindow(Player* player, Fusion::Crafts::Trade t, bool open, Said const& said);
    // GCC rejects `= {}` for a nested struct with member initializers, so the default is an overload.
    void SendWindow(Player* player, Fusion::Crafts::Trade t, bool open) { SendWindow(player, t, open, Said()); }
    Said Act(Player* player, Fusion::Crafts::Trade t, Fusion::Crafts::Action a, uint32 arg);
    Said Make(Player* player, Fusion::Crafts::Trade t, uint16 recipe);
    Creature* TrainerNear(Player* player, Fusion::Crafts::Trade t) const;
    // The kits' augments.
    Fusion::Crafts::GunInfo GunOf(Player const* player, Fusion::Crafts::GunSlot g) const;
    void SendAugments(Player* player, bool open, Said const& said);
    void SendAugments(Player* player, bool open) { SendAugments(player, open, Said()); }
    Said Augment(Player* player, Fusion::Crafts::AugmentAsk const& ask);
};

#define sCraftsMgr CraftsMgr::Instance()

#endif
