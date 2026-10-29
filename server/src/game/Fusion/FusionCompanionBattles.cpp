/*
 * World of Modern Warfare: companion duels in the game. GPL-2.0-or-later.
 *
 * Turn-based card duels of companion teams after Slay the Spire (design/COMPANION_BATTLES.md):
 * the rules are fusion/CompanionBattles.h; this file puts them in the world. Each companion plays
 * its own deck (character_fusion_companion_deck, its starter deck until its owner saves one in the
 * deck window, channel 13 ops 4/14/15); every play is the rules' to check.
 *
 * - Tamers and Companion Battle Trainers are gossip NPCs (creatures 94201-94235, script
 *   npc_fusion_companion_tamer; sql/custom/azeroth_warfare/33_companion_battles.sql). Their teams
 *   come from the world table fusion_companion_tamer (CompanionBattles::Tamers() without it).
 * - Some wanderers battle too (WandererMgr's gossip, StartWandererBattle); their team grows with them.
 * - Players challenge each other (channel 13 op 10, `.fusion petbattle`).
 *
 * A duel holds its players where they stand (rooted, pacified, their companion put away) and its
 * NPC facing them, and it is driven from its first player's update (both owners are on one map, so
 * one map thread). Plays come in on channel 13 (op 12); an NPC plays the turn its intent showed
 * after a short pause; a side that does not end its turn in CompanionBattles::kTurnMs has it ended.
 * The state goes to each player after every change (op 1). A duel never wounds or faints a
 * companion in the world: a beaten one stays at 1 health in the duel and is whole after it.
 *
 * At the end: experience to the winners' companions that fought (CompanionBattles::XpEarned, PvP
 * at most Companions::kDuelWinsPerDay paid wins a day per account), a tamer's daily reward on the
 * first win of the day against it (`characters.character_fusion_tamer`, made here at startup), and
 * the achievements' counters (Stat::CompanionBattlesWon, Stat::TamersDefeated).
 */

#include "FusionMgr.h"
#include "FusionWanderers.h"
#include "FusionShips.h"
#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <mutex>

using namespace Fusion;
namespace CB = Fusion::CompanionBattles;

struct FusionCompanionBattle
{
    uint32 id = 0;
    CB::Battle battle;
    std::array<uint64, 2> players{};        // side 1 may be an NPC (0)
    std::array<std::string, 2> names;
    std::array<size_t, 2> sent{};           // log lines each player has
    std::array<std::string, 2> rewards;
    ObjectGuid npc;
    bool wanderer = false;
    uint16 tamer = 0;
    uint8 xpFactor = CB::kTamerXpFactor;
    bool daily = false;
    bool pvp = false;
    uint32 deadlineMs = 0;
    uint32 npcAtMs = 0;
    uint16 lastTurn = 0;
    CB::Phase lastPhase = CB::Phase::Playing;
    bool ended = false;
    std::recursive_mutex lock;
};

namespace
{
    constexpr uint32 kTamerSender = 0x5442;
    enum TamerAction : uint32
    {
        kFight = 1,
        kExplain = 10,
        kPractice = 11,
        kWhere = 12,
        kLessons = 13,
        kLesson = 100,          // + the lesson's index in CB::TrainerCards()
    };
    constexpr float kChallengeRange = 10.f;
    constexpr uint32 kAskMs = 60000;
    constexpr uint32 kNpcThinkMs = 1800;

    uint32 Today() { return uint32(time(nullptr) / 86400); }

    uint8 SideOf(Player const* p) { return p->GetTeam() == ALLIANCE ? 1 : 2; }

    template <class F>
    void ForEachBag(Player* player, F f)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                f(item);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* b = dynamic_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
                for (uint32 slot = 0; slot < b->GetBagSize(); ++slot)
                    if (Item* item = b->GetItemByPos(uint8(slot)))
                        f(item);
    }

    std::string Money(uint32 copper)
    {
        std::string s;
        if (copper >= 10000)
            s += std::to_string(copper / 10000) + "g ";
        if (copper % 10000 >= 100)
            s += std::to_string(copper % 10000 / 100) + "s ";
        if (copper % 100 || s.empty())
            s += std::to_string(copper % 100) + "c";
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    }

    // An item given to a player (the bags, else nothing): true when it went in.
    bool GiveItem(Player* player, uint32 entry, uint32 count)
    {
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, count) != EQUIP_ERR_OK)
            return false;
        if (Item* item = player->StoreNewItem(dest, entry, true))
        {
            player->SendNewItem(item, count, true, false);
            return true;
        }
        return false;
    }
}

// ── Loading ─────────────────────────────────────────────────────────────────────────────────

void FusionMgr::LoadCompanionBattles()
{
    m_tamers = CB::Tamers();
    CharacterDatabase.Execute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_tamer` ("
        "`guid` int(10) unsigned NOT NULL, `tamer` smallint(5) unsigned NOT NULL, "
        "`day` int(10) unsigned NOT NULL DEFAULT '0', `wins` int(10) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`guid`, `tamer`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 "
        "COMMENT='WOMW companion battles: each character''s wins over each tamer and its last daily reward'");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_companion_deck` ("
        "`item_guid` int(10) unsigned NOT NULL, `deck` varchar(255) NOT NULL DEFAULT '', "
        "`learned` varchar(255) NOT NULL DEFAULT '', PRIMARY KEY (`item_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 "
        "COMMENT='WOMW companion duels: each companion item''s saved deck and the cards it learned (empty: its starter deck)'");
    CharacterDatabase.DirectExecute(
        "DELETE d FROM `character_fusion_companion_deck` d LEFT JOIN `item_instance` i ON i.`guid` = d.`item_guid` "
        "WHERE i.`guid` IS NULL");
    {
        std::unordered_map<uint32, CompanionDeckRow> decks;
        if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
                "SELECT `item_guid`, `deck`, `learned` FROM `character_fusion_companion_deck`"))
        {
            do
            {
                Field* f = r->Fetch();
                CompanionDeckRow row;
                row.deck = CB::SplitCards(f[1].GetCppString());
                row.learned = CB::SplitCards(f[2].GetCppString());
                decks[f[0].GetUInt32()] = std::move(row);
            } while (r->NextRow());
        }
        std::lock_guard<std::mutex> lock(m_companionLock);
        m_companionDecks = std::move(decks);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u saved companion decks.", uint32(m_companionDecks.size()));
    }
    std::unique_ptr<QueryResult> table = WorldDatabase.Query(
        "SELECT 1 FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'fusion_companion_tamer'");
    if (!table)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: companion battles use the built-in %u tamers (no fusion_companion_tamer table).",
            uint32(m_tamers.size()));
        return;
    }
    std::vector<CB::Tamer> loaded;
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `id`, `entry`, `name`, `zone`, `side`, `level`, `grand`, `place`, `species1`, `species2`, `species3` "
            "FROM `fusion_companion_tamer` ORDER BY `id`"))
    {
        do
        {
            Field* f = r->Fetch();
            CB::Tamer t;
            t.id = f[0].GetUInt16();
            t.entry = f[1].GetUInt32();
            t.name = f[2].GetCppString();
            t.zone = f[3].GetUInt32();
            t.side = f[4].GetUInt8();
            t.level = std::max<uint8>(1, std::min<uint8>(f[5].GetUInt8(), Companions::kMaxLevel));
            t.grand = f[6].GetUInt8() != 0;
            t.place = f[7].GetCppString();
            CB::Tamer const* built = CB::FindTamer(CB::Tamers(), t.id);
            for (uint8 k = 0; k < 3; ++k)
            {
                uint16 const species = f[8 + k].GetUInt16();
                if (!species)
                    continue;
                if (!Companions::FindSpecies(m_companionSpecies, species))
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: tamer %u names unknown companion species %u; left out.", t.id, species);
                    continue;
                }
                CB::TamerPet p;
                p.species = species;
                p.level = t.level;
                p.stage = CB::StageForLevel(t.level);
                if (built && k < built->team.size())
                    p.picks = built->team[k].picks;
                t.team.push_back(p);
            }
            if (!t.team.empty())
                loaded.push_back(std::move(t));
        } while (r->NextRow());
    }
    if (!loaded.empty())
        m_tamers = std::move(loaded);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u companion tamers.", uint32(m_tamers.size()));
}

// ── Lookups ─────────────────────────────────────────────────────────────────────────────────

std::shared_ptr<FusionCompanionBattle> FusionMgr::BattleOf(Player const* player) const
{
    std::lock_guard<std::mutex> lock(m_battleLock);
    auto it = m_battleOf.find(player->GetObjectGuid().GetRawValue());
    if (it == m_battleOf.end())
        return nullptr;
    auto b = m_battles.find(it->second);
    return b == m_battles.end() ? nullptr : b->second;
}

bool FusionMgr::InCompanionBattle(Player const* player) const
{
    return player && BattleOf(player) != nullptr;
}

void FusionMgr::TellBattle(Player* player, std::string const& text)
{
    if (player && player->GetSession())
        SendChannel(player, CB::kChannel, CB::EncodeTell(text));
    FusionCompanionTell(player).SendSysMessage(text.c_str());
}

bool FusionMgr::CanBattle(Player* player, std::string& why)
{
    if (!m_enabled || !player->HasFusion() || !player->IsInWorld())
        why = "Companion duels are not available.";
    else if (!player->IsAlive())
        why = "You can't battle while dead.";
    else if (player->IsInCombat())
        why = "Not while you are in combat.";
    else if (player->IsMounted() || player->GetFusion().companion.riding)
        why = "Get off your mount first.";
    else if (player->IsTaxiFlying())
        why = "Not while flying.";
    else if (IsDowned(player))
        why = "Not while you are downed.";
    else if (PlayerShip const* ship = sShipMgr.IsEnabled() ? sShipMgr.ShipOf(player) : nullptr;
             ship && ship->Pilot() == player->GetObjectGuid())
        why = "Not while you are at the helm.";
    else if (InCompanionBattle(player))
        why = "You are already in a companion duel.";
    else if (CompanionDuelOf(player->GetObjectGuid().GetRawValue()))
        why = "Not during a companion duel.";
    else
        return true;
    return false;
}

std::vector<CB::BattlePet> FusionMgr::BattleTeamOf(Player* player, std::string& why, uint8 cap)
{
    struct Pick
    {
        Item* item;
        Companions::CompanionSpecies const* sp;
        Companions::CompanionState s;
        bool out;
    };
    std::vector<Pick> picks;
    ObjectGuid const out = player->GetFusion().companion.item;
    uint32 fainted = 0;
    ForEachBag(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()))
            return;
        Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
        if (!sp)
            return;
        if (CompanionFaintLeft(player, item) > 0)
        {
            ++fainted;
            return;
        }
        Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        s.species = sp->id;
        picks.push_back({item, sp, s, item->GetObjectGuid() == out});
    });
    // The companion out first, then the strongest.
    std::stable_sort(picks.begin(), picks.end(), [](Pick const& a, Pick const& b) {
        if (a.out != b.out)
            return a.out;
        return a.s.level > b.s.level;
    });
    std::vector<CB::BattlePet> team;
    uint8 const ownerLevel = uint8(player->GetLevel());
    for (Pick const& p : picks)
    {
        if (team.size() >= CB::kTeamSize)
            break;
        uint8 level = Companions::EffectiveLevel(p.s, ownerLevel);
        if (cap && level > cap)
            level = cap;
        // Its saved deck when it still holds, else its starter deck (every companion has one).
        std::vector<uint16> const deck = CB::DuelDeck(CompanionLibrary(p.item, *p.sp, p.s),
            CompanionDeckOf(p.item->GetGUIDLow()).deck, *p.sp, p.s);
        team.push_back(CB::MakePet(*p.sp, p.s, p.item->GetObjectGuid().GetRawValue(), deck, level));
    }
    if (team.empty())
        why = fainted ? "Your companions have all fainted. Revive them at a stable master first."
                      : "You need a companion in your bags to duel.";
    return team;
}

std::vector<CB::BattlePet> FusionMgr::NpcTeam(std::vector<CB::TamerPet> const& pets) const
{
    std::vector<CB::BattlePet> team;
    for (CB::TamerPet const& p : pets)
    {
        Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, p.species);
        if (!sp)
            sp = Companions::FindSpecies(Companions::StarterSpecies(), p.species);
        if (!sp)
            continue;
        Companions::CompanionState s;
        s.species = sp->id;
        s.level = p.level;
        s.stage = std::min<uint8>(p.stage, uint8(std::max<uint8>(sp->stageCount, 1) - 1));
        s.picks = p.picks;
        team.push_back(CB::MakePet(*sp, s, 0, CB::BuiltDeck(*sp, s), p.level));
    }
    return team;
}

// ── Starting ────────────────────────────────────────────────────────────────────────────────

void FusionMgr::HoldForBattle(Player* player, bool hold)
{
    if (!player)
        return;
    if (hold)
    {
        // The companion out goes back in its item for the battle (its owner may call it again
        // after); the player stands where they are, hands down.
        if (!player->GetFusion().companion.pet.IsEmpty())
            DismissCompanion(player, false, true, false);
        player->CombatStop(true);
        player->SetRooted(true);
        player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    }
    else
    {
        player->SetRooted(false);
        player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    }
}

bool FusionMgr::StartCompanionBattle(Player* a, Player* b, Creature* npc, std::vector<CB::BattlePet> npcTeam,
    std::string const& npcName, uint16 tamer, uint8 xpFactor, bool daily)
{
    std::string why;
    if (!CanBattle(a, why) || (b && !CanBattle(b, why)))
    {
        TellBattle(a, why);
        return false;
    }
    uint8 cap = 0;
    std::vector<CB::BattlePet> teamA, teamB;
    if (b)
    {
        // PvP: both teams at the lower of the two teams' highest levels.
        std::string w2;
        std::vector<CB::BattlePet> const ta = BattleTeamOf(a, why), tb = BattleTeamOf(b, w2);
        if (ta.empty() || tb.empty())
        {
            TellBattle(a, ta.empty() ? why : std::string(b->GetName()) + " has no companion fit to duel.");
            if (tb.empty())
                TellBattle(b, w2);
            return false;
        }
        std::vector<uint8> la, lb;
        for (CB::BattlePet const& p : ta)
            la.push_back(p.level);
        for (CB::BattlePet const& p : tb)
            lb.push_back(p.level);
        cap = CB::PvpLevelCap(la, lb);
        teamA = BattleTeamOf(a, why, cap);
        teamB = BattleTeamOf(b, w2, cap);
    }
    else
    {
        teamA = BattleTeamOf(a, why);
        teamB = std::move(npcTeam);
        if (teamA.empty() || teamB.empty())
        {
            TellBattle(a, teamA.empty() ? why : "They have no companions to duel with.");
            return false;
        }
    }
    auto battle = std::make_shared<FusionCompanionBattle>();
    // Against an NPC the player goes first and the NPC shows its intent; two players: the seed
    // picks who starts, and the one who starts draws a card fewer (CompanionBattles.h).
    battle->battle = CB::Battle(std::move(teamA), std::move(teamB), urand(1, 0x7FFFFFFF), {false, b == nullptr}, b ? -1 : 0);
    battle->players = {a->GetObjectGuid().GetRawValue(), b ? b->GetObjectGuid().GetRawValue() : 0};
    battle->names = {a->GetName(), b ? std::string(b->GetName()) : npcName};
    battle->npc = npc ? npc->GetObjectGuid() : ObjectGuid();
    battle->wanderer = npc && npc->m_wanderer;
    battle->tamer = tamer;
    battle->xpFactor = xpFactor;
    battle->daily = daily;
    battle->pvp = b != nullptr;
    uint32 const now = WorldTimer::getMSTime();
    battle->deadlineMs = now + CB::kTurnMs;
    battle->npcAtMs = now + kNpcThinkMs;
    {
        std::lock_guard<std::mutex> lock(m_battleLock);
        battle->id = m_nextBattle++;
        m_battles[battle->id] = battle;
        m_battleOf[battle->players[0]] = battle->id;
        if (b)
            m_battleOf[battle->players[1]] = battle->id;
    }
    HoldForBattle(a, true);
    HoldForBattle(b, true);
    if (npc)
    {
        npc->StopMoving();
        npc->SetFacingToObject(a);
        a->SetFacingToObject(npc);
        if (npc->m_wanderer)
            sWandererMgr.HoldForBattle(npc, a->GetObjectGuid().GetRawValue(), CB::kTurnMs * 2);
    }
    else if (b)
    {
        a->SetFacingToObject(b);
        b->SetFacingToObject(a);
    }
    SendBattle(battle, now);
    return true;
}

bool FusionMgr::StartWandererBattle(Player* player, Creature* c, std::string const& name, uint8 level, uint32 seed)
{
    if (!m_enabled || !player || !c)
        return false;
    return StartCompanionBattle(player, nullptr, c, NpcTeam(CB::WandererTeam(seed, level)), name, 0,
        uint8(CB::kTamerXpFactor - 1), false);
}

// ── Gossip ──────────────────────────────────────────────────────────────────────────────────

bool FusionMgr::CompanionTamerHello(Player* player, Creature* creature)
{
    if (!m_enabled || !player || !creature)
        return false;
    uint32 const entry = creature->GetEntry();
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    GossipMenu& g = menu->GetGossipMenu();
    if (CB::Tamer const* t = CB::TamerByEntry(m_tamers, entry))
    {
        if (t->side && t->side != SideOf(player))
        {
            ChatHandler(player).PSendSysMessage("%s won't battle your side's tamers.", t->name.c_str());
            menu->CloseGossip();
            return true;
        }
        bool claimed = false;
        if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
                "SELECT `day` FROM `character_fusion_tamer` WHERE `guid` = %u AND `tamer` = %u", player->GetGUIDLow(), t->id))
            claimed = r->Fetch()[0].GetUInt32() == Today();
        std::string label = "Let's duel! (level " + std::to_string(t->level) + ", " + std::to_string(t->team.size())
            + " companions" + (claimed ? ")" : "; today's reward is still yours to win)");
        g.AddMenuItem(GOSSIP_ICON_BATTLE, label, kTamerSender, kFight, "", false);
        g.AddMenuItem(GOSSIP_ICON_CHAT, "How do companion duels work?", kTamerSender, kExplain, "", false);
        g.AddMenuItem(GOSSIP_ICON_CHAT, "Where should I battle next?", kTamerSender, kWhere, "", false);
    }
    else if (entry >= CB::kTrainerEntryFirst && entry <= CB::kTrainerEntryLast)
    {
        g.AddMenuItem(GOSSIP_ICON_TRAINER, "How do companion duels work?", kTamerSender, kExplain, "", false);
        g.AddMenuItem(GOSSIP_ICON_BATTLE, "A practice duel, please.", kTamerSender, kPractice, "", false);
        g.AddMenuItem(GOSSIP_ICON_VENDOR, "Teach my companion a card.", kTamerSender, kLessons, "", false);
        g.AddMenuItem(GOSSIP_ICON_CHAT, "Where should I battle next?", kTamerSender, kWhere, "", false);
    }
    else
        return false;
    menu->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, creature->GetObjectGuid());
    return true;
}

bool FusionMgr::CompanionTamerSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (sender != kTamerSender || !m_enabled || !player || !creature)
        return false;
    player->PlayerTalkClass->CloseGossip();
    ChatHandler chat(player);
    switch (action)
    {
        case kFight:
        {
            CB::Tamer const* t = CB::TamerByEntry(m_tamers, creature->GetEntry());
            if (!t || (t->side && t->side != SideOf(player)))
                return true;
            StartCompanionBattle(player, nullptr, creature, NpcTeam(t->team), t->name, t->id, uint8(CB::kTamerXpFactor), true);
            return true;
        }
        case kPractice:
        {
            std::string why;
            std::vector<CB::BattlePet> const mine = BattleTeamOf(player, why);
            if (mine.empty())
            {
                TellBattle(player, why);
                return true;
            }
            uint8 top = 1;
            for (CB::BattlePet const& p : mine)
                top = std::max(top, p.level);
            StartCompanionBattle(player, nullptr, creature, NpcTeam(CB::PracticeTeam(top, urand(1, 0x7FFFFFFF))),
                creature->GetName(), 0, 1, false);
            return true;
        }
        case kExplain:
            chat.SendSysMessage("|cffffd200Companion duels|r: each companion fights with a deck of cards. Every turn you get 3 energy "
                "and draw 5 cards: play attacks, block and skills, then end your turn. Block falls away at the start of your next turn.");
            chat.SendSysMessage("Your opponent's intent shows what it will do next. Bring up to three companions (the one out goes "
                "first); when one is beaten the next steps in. Each family plays its own way: cats bleed, bears block, birds dodge.");
            chat.SendSysMessage("Your companion learns cards as it levels, evolves, breeds and wins; edit its deck with "
                "|cffffd200.fusion petbattle deck|r. Duels never hurt your companions; winners that fought gain experience.");
            return true;
        case kLessons:
        {
            if (!DeckItem(player, 0))
            {
                chat.SendSysMessage("Summon the companion you want taught first.");
                return true;
            }
            PlayerMenu* menu = player->PlayerTalkClass;
            menu->ClearMenus();
            GossipMenu& g = menu->GetGossipMenu();
            std::vector<uint16> const& lessons = CB::TrainerCards();
            for (size_t i = 0; i < lessons.size(); ++i)
            {
                CB::Card const& c = CB::CardById(lessons[i]);
                uint32 const price = CB::TrainerPrice(c.id, uint8(player->GetLevel()));
                g.AddMenuItem(GOSSIP_ICON_VENDOR, c.name + " (" + Money(price) + "): " + CB::CardText(c), kTamerSender,
                    uint32(kLesson + i), "", false);
            }
            menu->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, creature->GetObjectGuid());
            return true;
        }
        case kWhere:
        {
            std::string why;
            uint8 top = 1;
            for (CB::BattlePet const& p : BattleTeamOf(player, why))
                top = std::max(top, p.level);
            std::vector<CB::Tamer const*> const next = CB::NextTamers(m_tamers, SideOf(player), top, 3);
            if (next.empty())
                chat.SendSysMessage("You have beaten the road's end. Try the grand masters again, or another player.");
            for (CB::Tamer const* t : next)
                chat.PSendSysMessage("|cffffd200%s|r waits at %s (level %u%s).", t->name.c_str(), t->place.c_str(),
                    uint32(t->level), t->grand ? ", a grand master" : "");
            return true;
        }
        default:
        {
            std::vector<uint16> const& lessons = CB::TrainerCards();
            if (action < kLesson || action >= kLesson + lessons.size())
                return true;
            if (creature->GetEntry() < CB::kTrainerEntryFirst || creature->GetEntry() > CB::kTrainerEntryLast)
                return true;
            uint16 const card = lessons[action - kLesson];
            Item* item = DeckItem(player, 0);
            if (!item)
            {
                chat.SendSysMessage("Summon the companion you want taught first.");
                return true;
            }
            CompanionDeckRow row = CompanionDeckOf(item->GetGUIDLow());
            if (std::find(row.learned.begin(), row.learned.end(), card) != row.learned.end())
            {
                chat.SendSysMessage("Your companion already knows that card.");
                return true;
            }
            uint32 const price = CB::TrainerPrice(card, uint8(player->GetLevel()));
            if (player->GetMoney() < price)
            {
                chat.SendSysMessage("You can't afford that lesson.");
                return true;
            }
            player->ModifyMoney(-int32(price));
            row.learned.push_back(card);
            SaveCompanionDeckRow(item->GetGUIDLow(), row);
            chat.PSendSysMessage("Your companion learns |cffffd200%s|r. Add it to its deck with .fusion petbattle deck.",
                CB::CardById(card).name.c_str());
            return true;
        }
    }
}

// ── PvP ─────────────────────────────────────────────────────────────────────────────────────

void FusionMgr::ChallengeCompanionBattle(Player* player, ObjectGuid target)
{
    Player* other = target.IsPlayer() && player->IsInWorld() ? player->GetMap()->GetPlayer(target) : nullptr;
    if (!other || other == player || !other->HasFusion())
    {
        TellBattle(player, "Target a player to challenge them to a companion duel.");
        return;
    }
    std::string why;
    if (!CanBattle(player, why))
        return TellBattle(player, why);
    if (!CanBattle(other, why))
        return TellBattle(player, std::string(other->GetName()) + " can't duel now.");
    if (!player->IsWithinDistInMap(other, kChallengeRange))
        return TellBattle(player, std::string(other->GetName()) + " is too far away.");
    if (BattleTeamOf(player, why).empty())
        return TellBattle(player, why);
    uint32 const now = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(m_battleLock);
        auto it = m_battleAsks.find(other->GetObjectGuid().GetRawValue());
        if (it != m_battleAsks.end() && it->second.first == player->GetObjectGuid().GetRawValue()
            && now - it->second.second < kAskMs / 6)
            return;     // asked just now
        m_battleAsks[other->GetObjectGuid().GetRawValue()] = {player->GetObjectGuid().GetRawValue(), now};
    }
    SendChannel(other, CB::kChannel, CB::EncodeChallenged(player->GetObjectGuid().GetRawValue(), player->GetName()));
    ChatHandler(other).PSendSysMessage("%s challenges you to a companion duel. (.fusion petbattle accept | decline)",
        player->GetName());
    TellBattle(player, "You challenge " + std::string(other->GetName()) + " to a companion duel.");
}

void FusionMgr::AnswerCompanionBattle(Player* player, bool accept)
{
    uint64 challenger = 0;
    uint32 const now = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(m_battleLock);
        auto it = m_battleAsks.find(player->GetObjectGuid().GetRawValue());
        if (it != m_battleAsks.end() && now - it->second.second < kAskMs)
            challenger = it->second.first;
        if (it != m_battleAsks.end())
            m_battleAsks.erase(it);
    }
    Player* other = challenger && player->IsInWorld() ? player->GetMap()->GetPlayer(ObjectGuid(challenger)) : nullptr;
    if (!other)
        return TellBattle(player, "Nobody is waiting on your answer.");
    if (!accept)
    {
        TellBattle(other, std::string(player->GetName()) + " declines your companion duel.");
        return;
    }
    if (!player->IsWithinDistInMap(other, kChallengeRange * 2))
        return TellBattle(player, std::string(other->GetName()) + " has gone too far away.");
    StartCompanionBattle(other, player, nullptr, {}, "", 0, uint8(CB::kPvpXpFactor), false);
}

// ── The channel ─────────────────────────────────────────────────────────────────────────────

void FusionMgr::HandleCompanionBattleChannel(Player* player, std::vector<uint8> const& body)
{
    if (!m_enabled || !player->IsInWorld() || !player->HasFusion())
        return;
    std::optional<CB::Request> const r = CB::DecodeRequest(body);
    if (!r)
        return;
    switch (r->op)
    {
        case CB::kOpChallenge:
            ChallengeCompanionBattle(player, ObjectGuid(r->target));
            return;
        case CB::kOpAnswer:
            AnswerCompanionBattle(player, r->accept);
            return;
        case CB::kOpAskDeck:
            SendCompanionDeck(player, r->target);
            return;
        case CB::kOpSaveDeck:
            SaveCompanionDeck(player, r->target, r->cards);
            return;
        case CB::kOpAsk:
        {
            if (std::shared_ptr<FusionCompanionBattle> b = BattleOf(player))
            {
                std::lock_guard<std::recursive_mutex> lock(b->lock);
                int const side = b->players[0] == player->GetObjectGuid().GetRawValue() ? 0 : 1;
                b->sent[side] = 0;      // the whole log again
                SendBattle(b, WorldTimer::getMSTime());
            }
            return;
        }
        case CB::kOpAction:
        {
            std::shared_ptr<FusionCompanionBattle> b = BattleOf(player);
            if (!b)
                return TellBattle(player, "You are not in a companion duel.");
            std::lock_guard<std::recursive_mutex> lock(b->lock);
            int const side = b->players[0] == player->GetObjectGuid().GetRawValue() ? 0 : 1;
            CB::Refusal const why = b->battle.Choose(side, r->action);
            if (why != CB::Refusal::None)
                TellBattle(player, CB::RefusalText(why));
            uint32 const now = WorldTimer::getMSTime();
            SendBattle(b, now);
            if (b->battle.GetPhase() == CB::Phase::Over)
                EndCompanionBattle(b);
            return;
        }
        default:
            return;
    }
}

void FusionMgr::SendBattle(std::shared_ptr<FusionCompanionBattle> const& b, uint32 now)
{
    std::lock_guard<std::recursive_mutex> lock(b->lock);
    // A new turn or phase: its own clock, and the NPC thinks a moment first.
    if (b->battle.Turn() != b->lastTurn || b->battle.GetPhase() != b->lastPhase)
    {
        b->lastTurn = b->battle.Turn();
        b->lastPhase = b->battle.GetPhase();
        b->deadlineMs = now + CB::kTurnMs;
        b->npcAtMs = now + kNpcThinkMs;
    }
    std::vector<std::string> const& log = b->battle.Log();
    for (int side = 0; side < 2; ++side)
    {
        if (!b->players[side])
            continue;
        Player* p = ObjectAccessor::FindPlayer(ObjectGuid(b->players[side]));
        if (!p || !p->GetSession())
            continue;
        CB::View v;
        v.battle = b->id;
        v.side = side;
        v.owners = {b->names[side], b->names[side ^ 1]};
        v.secondsLeft = int32(b->deadlineMs - now) > 0 ? (b->deadlineMs - now + 999) / 1000 : 0;
        if (b->sent[side] < log.size())
            v.newLines.assign(log.begin() + b->sent[side], log.end());
        b->sent[side] = log.size();
        v.rewards = b->rewards[side];
        SendChannel(p, CB::kChannel, CB::EncodeState(b->battle, v));
    }
}

// ── The clock ───────────────────────────────────────────────────────────────────────────────

void FusionMgr::UpdateCompanionBattle(Player* player, uint32 now)
{
    std::shared_ptr<FusionCompanionBattle> b = BattleOf(player);
    if (!b)
        return;
    std::lock_guard<std::recursive_mutex> lock(b->lock);
    // Driven from its first player's update (both owners are on one map).
    if (b->players[0] != player->GetObjectGuid().GetRawValue() || b->ended)
        return;
    CB::Battle& battle = b->battle;
    size_t const lines = battle.Log().size();
    CB::Phase const phase = battle.GetPhase();
    // Its people still here, alive and out of any other fight.
    for (int side = 0; side < 2 && battle.GetPhase() != CB::Phase::Over; ++side)
    {
        if (!b->players[side])
            continue;
        Player* p = side == 0 ? player : player->GetMap()->GetPlayer(ObjectGuid(b->players[side]));
        if (!p || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat())
        {
            if (p && p->IsInCombat())
                TellBattle(p, "Pulled into a fight: you forfeit the companion duel.");
            battle.Choose(side, {CB::ActionKind::Forfeit, 0});
        }
    }
    if (battle.GetPhase() != CB::Phase::Over && !b->players[1])
    {
        Creature* npc = player->GetMap()->GetCreature(b->npc);
        if (!npc || !npc->IsAlive() || !player->IsWithinDistInMap(npc, 40.f))
            battle.Abandon();
        else
        {
            if (b->wanderer)
                sWandererMgr.HoldForBattle(npc, player->GetObjectGuid().GetRawValue(), 10000);
            // The NPC's turn: it plays what its intent showed.
            if (battle.ToPlay() == 1 && battle.IsAi(1) && int32(now - b->npcAtMs) >= 0)
                battle.PlayAiTurn();
        }
    }
    if (battle.GetPhase() != CB::Phase::Over && int32(now - b->deadlineMs) >= 0)
        battle.Timeout();
    if (battle.Log().size() != lines || battle.GetPhase() != phase || battle.GetPhase() == CB::Phase::Over)
        SendBattle(b, now);
    if (battle.GetPhase() == CB::Phase::Over)
        EndCompanionBattle(b);
}

void FusionMgr::CompanionBattleLogout(Player* player)
{
    std::shared_ptr<FusionCompanionBattle> b = BattleOf(player);
    if (!b)
        return;
    std::lock_guard<std::recursive_mutex> lock(b->lock);
    int const side = b->players[0] == player->GetObjectGuid().GetRawValue() ? 0 : 1;
    b->battle.Choose(side, {CB::ActionKind::Forfeit, 0});
    SendBattle(b, WorldTimer::getMSTime());
    EndCompanionBattle(b);
}

// ── The end ─────────────────────────────────────────────────────────────────────────────────

void FusionMgr::EndCompanionBattle(std::shared_ptr<FusionCompanionBattle> const& b)
{
    std::lock_guard<std::recursive_mutex> lock(b->lock);
    if (b->ended)
        return;
    b->ended = true;
    CB::Battle const& battle = b->battle;
    uint32 const today = Today();
    for (int side = 0; side < 2; ++side)
    {
        if (!b->players[side])
            continue;
        Player* p = ObjectAccessor::FindPlayer(ObjectGuid(b->players[side]));
        if (!p)
            continue;
        HoldForBattle(p, false);
        bool const won = battle.Winner() == side;
        std::string text;
        if (battle.GetEnd() == CB::End::Abandoned)
            text = "The duel was called off.";
        else if (battle.Winner() < 0)
            text = "The duel ends in a draw.";
        else
            text = won ? "You win the duel!" : "You lose the duel.";
        std::vector<uint32> xp = CB::XpEarned(battle, side, b->xpFactor);
        if (won && b->pvp)
        {
            // At most kDuelWinsPerDay paid PvP wins a day per account.
            std::lock_guard<std::mutex> l2(m_battleLock);
            if (!Companions::PayDuelWin(m_battleXp[p->GetSession()->GetAccountId()], today))
            {
                std::fill(xp.begin(), xp.end(), 0);
                text += " (No more experience from player duels today.)";
            }
        }
        // A tamer's first win of the day.
        bool dailyPaid = false;
        CB::Tamer const* tamer = b->tamer ? CB::FindTamer(m_tamers, b->tamer) : nullptr;
        if (won && tamer && b->daily)
        {
            uint32 lastDay = 0;
            if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
                    "SELECT `day` FROM `character_fusion_tamer` WHERE `guid` = %u AND `tamer` = %u", p->GetGUIDLow(), tamer->id))
                lastDay = r->Fetch()[0].GetUInt32();
            dailyPaid = lastDay != today;
            CharacterDatabase.PExecute(
                "INSERT INTO `character_fusion_tamer` (`guid`, `tamer`, `day`, `wins`) VALUES (%u, %u, %u, 1) "
                "ON DUPLICATE KEY UPDATE `wins` = `wins` + 1%s",
                p->GetGUIDLow(), uint32(tamer->id), dailyPaid ? today : lastDay, dailyPaid ? ", `day` = VALUES(`day`)" : "");
        }
        // Experience to the companions.
        uint8 const ownerLevel = uint8(p->GetLevel());
        auto const& team = battle.Team(side);
        for (size_t i = 0; i < team.size(); ++i)
        {
            uint32 amount = i < xp.size() ? xp[i] : 0;
            if (dailyPaid && team[i].fought)
                amount += CB::DailyBonusXp(team[i].level);
            if (!amount || !team[i].item)
                continue;
            Item* item = p->GetItemByGuid(ObjectGuid(team[i].item));
            if (!item)
                continue;
            Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
            Companions::XpGain const gain = Companions::GainXp(s, amount, ownerLevel);
            SaveCompanionState(item->GetGUIDLow(), s);
            StampCompanionItem(item);
            text += "\n" + team[i].name + ": +" + std::to_string(amount) + " experience";
            if (gain.levels)
                text += ", now level " + std::to_string(s.level);
            if (gain.capped)
                text += " (held at your level)";
        }
        if (dailyPaid)
        {
            uint32 const money = CB::DailyMoney(tamer->level, uint8(tamer->team.size()));
            p->ModifyMoney(int32(money));
            text += "\nDaily reward: " + Money(money);
            if (GiveItem(p, Companions::kReviveDraughtItem, CB::kTamerRewardDraughts))
                text += ", a Reviving Draught";
            if (tamer->grand)
            {
                uint32 const catalyst = Companions::Catalyst::DraconicEssence + urand(0, 6);
                if (GiveItem(p, catalyst, 1))
                    text += ", an evolution catalyst";
            }
            // A card drop: the companion that fought first learns a reward card it lacks.
            if (!team.empty() && team[0].item)
                if (Item* item = p->GetItemByGuid(ObjectGuid(team[0].item)))
                    if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
                    {
                        Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
                        uint16 const card = CB::RollDropCard(CompanionLibrary(item, *sp, s), urand(0, 0x7FFFFFFF));
                        if (card)
                        {
                            CompanionDeckRow row = CompanionDeckOf(item->GetGUIDLow());
                            row.learned.push_back(card);
                            SaveCompanionDeckRow(item->GetGUIDLow(), row);
                            text += "\n" + team[0].name + " learns a card: " + CB::CardById(card).name;
                        }
                    }
        }
        else if (won && tamer && b->daily)
            text += "\n(Today's reward from " + tamer->name + " is already claimed.)";
        if (won && battle.GetEnd() != CB::End::Abandoned)
        {
            CountStat(p, {Fusion::Stat::CompanionBattlesWon, Fusion::kAnyKey});
            if (tamer)
            {
                CountStat(p, {Fusion::Stat::TamersDefeated, Fusion::kAnyKey});
                CountStat(p, {Fusion::Stat::TamersDefeated, tamer->id});
            }
        }
        b->rewards[side] = text;
        FusionCompanionTell(p).SendSysMessage(text.substr(0, text.find('\n')).c_str());
    }
    if (!b->players[1])
        if (Player* p = ObjectAccessor::FindPlayer(ObjectGuid(b->players[0])))
            if (Creature* npc = p->IsInWorld() ? p->GetMap()->GetCreature(b->npc) : nullptr)
                if (b->wanderer)
                    sWandererMgr.HoldForBattle(npc, p->GetObjectGuid().GetRawValue(), 4000);
    SendBattle(b, WorldTimer::getMSTime());
    std::lock_guard<std::mutex> l3(m_battleLock);
    for (uint64 g : b->players)
        if (g)
            m_battleOf.erase(g);
    m_battles.erase(b->id);
}

// ── Decks ───────────────────────────────────────────────────────────────────────────────────

FusionMgr::CompanionDeckRow FusionMgr::CompanionDeckOf(uint32 itemGuidLow) const
{
    std::lock_guard<std::mutex> lock(m_companionLock);
    auto it = m_companionDecks.find(itemGuidLow);
    return it == m_companionDecks.end() ? CompanionDeckRow{} : it->second;
}

void FusionMgr::SaveCompanionDeckRow(uint32 itemGuidLow, CompanionDeckRow const& row)
{
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        m_companionDecks[itemGuidLow] = row;
    }
    std::string deck = CB::JoinCards(row.deck), learned = CB::JoinCards(row.learned);
    CharacterDatabase.escape_string(deck);
    CharacterDatabase.escape_string(learned);
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_companion_deck` (`item_guid`, `deck`, `learned`) VALUES (%u, '%s', '%s')",
        itemGuidLow, deck.c_str(), learned.c_str());
}

std::vector<CB::LibraryCard> FusionMgr::CompanionLibrary(Item* item, Companions::CompanionSpecies const& sp,
    Companions::CompanionState const& s) const
{
    return CB::Library(sp, s, CompanionDeckOf(item->GetGUIDLow()).learned, m_companionSpecies);
}

Item* FusionMgr::DeckItem(Player* player, uint64 itemGuid) const
{
    // Only a companion item in the player's own bags (or the one out).
    ObjectGuid const guid = itemGuid ? ObjectGuid(itemGuid) : player->GetFusion().companion.item;
    if (guid.IsEmpty())
        return nullptr;
    Item* item = player->GetItemByGuid(guid);
    return item && Companions::IsCompanionItem(item->GetEntry()) ? item : nullptr;
}

void FusionMgr::SendCompanionDeck(Player* player, uint64 itemGuid, std::string const& note)
{
    if (!m_enabled || !player || !player->GetSession())
        return;
    Item* item = DeckItem(player, itemGuid);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!item || !sp)
        return TellBattle(player, "Summon a companion to see its deck.");
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    CB::DeckView v;
    v.item = item->GetObjectGuid().GetRawValue();
    v.name = sp->name;
    v.library = CompanionLibrary(item, *sp, s);
    std::vector<uint16> const saved = CompanionDeckOf(item->GetGUIDLow()).deck;
    v.deck = CB::DuelDeck(v.library, saved, *sp, s);
    v.note = note;
    if (note.empty() && !saved.empty() && v.deck != saved)
        v.note = "Its saved deck no longer holds (a changed ability?); it duels with its starter deck.";
    SendChannel(player, CB::kChannel, CB::EncodeDeck(v));
}

void FusionMgr::SaveCompanionDeck(Player* player, uint64 itemGuid, std::vector<uint16> const& cards)
{
    if (!m_enabled || !player)
        return;
    if (InCompanionBattle(player))
        return TellBattle(player, "Not during a duel.");
    Item* item = DeckItem(player, itemGuid);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!item || !sp)
        return TellBattle(player, "That companion isn't in your bags.");
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    CB::DeckProblem const why = CB::CheckDeck(CompanionLibrary(item, *sp, s), cards);
    if (why != CB::DeckProblem::None)
        return SendCompanionDeck(player, itemGuid, CB::DeckProblemText(why));
    CompanionDeckRow row = CompanionDeckOf(item->GetGUIDLow());
    row.deck = cards;
    SaveCompanionDeckRow(item->GetGUIDLow(), row);
    SendCompanionDeck(player, itemGuid, "Deck saved.");
}

// ── The command ─────────────────────────────────────────────────────────────────────────────

// .fusion petbattle [accept | decline | forfeit | deck]: challenge your target to a companion
// duel, answer a challenge, give up the duel you are in, or open your companion's deck.
bool ChatHandler::HandleFusionPetBattleCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !player->HasFusion())
        return false;
    std::string const arg = args ? std::string(args) : std::string();
    if (arg == "accept")
        sFusionMgr.AnswerCompanionBattle(player, true);
    else if (arg == "decline")
        sFusionMgr.AnswerCompanionBattle(player, false);
    else if (arg == "forfeit")
        sFusionMgr.HandleCompanionBattleChannel(player, CB::EncodeAction({CB::ActionKind::Forfeit, 0}));
    else if (arg == "deck")
        sFusionMgr.SendCompanionDeck(player, 0);
    else
        sFusionMgr.ChallengeCompanionBattle(player, player->GetSelectionGuid());
    return true;
}
