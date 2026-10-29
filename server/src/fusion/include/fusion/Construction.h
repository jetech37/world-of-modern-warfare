/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CONSTRUCTION_H
#define FUSION_CONSTRUCTION_H

#include "fusion/Build.h"
#include "fusion/Common.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

// Construction: building as a profession. A secondary profession like Cooking or First Aid (it
// takes no primary slot), skill 1 to 300 in vanilla's four tiers, learned from a trainer. Every
// piece costs materials: lumber cut from lumber nodes, Mining's stones and bars, and parts the
// smiths and engineers make; a piece unlocks at a skill and teaches by vanilla's colours. The
// client can't learn a new SkillLine.dbc row, so the skill lives in the fusion's own table and
// the fusion UI shows it; vanilla's craft spells can't be added either, so the smiths' parts are
// crafted by the fusion from the world table `fusion_recipe` (docs/CONSTRUCTION.md).
//
// Woodcutting is not a profession of its own: cutting lumber uses (and raises) the Construction
// skill, as a builder's own trade. One skill to train keeps a secondary profession small, and a
// builder levels by either gathering or building.
namespace Fusion { namespace Construction
{
    // The key of Construction in `character_fusion_skill` and `fusion_recipe.skill_line`: not a
    // SkillLine.dbc id (vanilla's run below 800), so it can't be mistaken for one.
    constexpr uint16_t kSkillConstruction = 9001;
    // The vanilla skills whose crafters make construction parts.
    constexpr uint16_t kSkillBlacksmithing = 164;
    constexpr uint16_t kSkillEngineering = 202;

    // ── Items (world DB, 03_construction.sql) ──
    // Lumber, by node.
    constexpr uint32_t kRoughLumber = 91001;
    constexpr uint32_t kSturdyLumber = 91002;
    constexpr uint32_t kHardwoodPlank = 91003;
    constexpr uint32_t kIronwoodPlank = 91004;
    // The tool every lumber node asks for, bought from a construction supplier.
    constexpr uint32_t kWoodcuttingAxe = 91010;
    // Parts: Blacksmithing (and Copper Nails from the supplier too), Engineering for the sentry's.
    constexpr uint32_t kCopperNails = 91020;
    constexpr uint32_t kBronzeHinge = 91021;
    constexpr uint32_t kIronBracket = 91022;
    constexpr uint32_t kReinforcedPlate = 91023;
    constexpr uint32_t kFortifiedPlate = 91024;
    constexpr uint32_t kSentryBarrel = 91025;
    constexpr uint32_t kSentryTargeting = 91026;
    constexpr uint32_t kSentryTripod = 91027;
    // Plot deeds, one a size (Plots.h): the steward hands one over with the plot.
    constexpr uint32_t kDeedSmall = 91030;
    constexpr uint32_t kDeedStandard = 91031;
    constexpr uint32_t kDeedLarge = 91032;
    // Each site's deeds name it (sql 12): Azshara Crater's, then the Thalassian plateau's.
    constexpr uint32_t kDeedCraterSmall = 93400;
    constexpr uint32_t kDeedCraterStandard = 93401;
    constexpr uint32_t kDeedCraterLarge = 93402;
    constexpr uint32_t kDeedPlateauSmall = 93403;
    constexpr uint32_t kDeedPlateauStandard = 93404;
    constexpr uint32_t kDeedPlateauLarge = 93405;
    // Vanilla's own trade goods the recipes use.
    constexpr uint32_t kRoughStone = 2835;
    constexpr uint32_t kCoarseStone = 2836;
    constexpr uint32_t kHeavyStone = 2838;
    constexpr uint32_t kSolidStone = 7912;
    constexpr uint32_t kDenseStone = 12365;
    constexpr uint32_t kCopperBar = 2840;
    constexpr uint32_t kBronzeBar = 2841;
    constexpr uint32_t kIronBar = 3575;
    constexpr uint32_t kMithrilBar = 3860;
    constexpr uint32_t kThoriumBar = 12359;
    constexpr uint32_t kLinenCloth = 2589;
    constexpr uint32_t kWoolCloth = 2592;
    constexpr uint32_t kBoltOfSilk = 4305;
    constexpr uint32_t kBoltOfMageweave = 4339;
    constexpr uint32_t kLightLeather = 2318;
    // The rare goods a portal asks (Portals.h): an alchemist's transmute and an enchanter's shard.
    constexpr uint32_t kArcaneCrystal = 12363;
    constexpr uint32_t kLargeBrilliantShard = 14344;

    // ── Tiers ──
    enum class Tier : uint8_t { None, Apprentice, Journeyman, Expert, Artisan };

    struct TierDef
    {
        Tier tier;
        char const* name;
        uint16_t cap;               // the most skill this tier holds
        uint16_t skillToLearn;      // the skill the trainer asks before teaching it
        uint8_t levelToLearn;
        uint32_t costCopper;
    };
    TierDef const& TierInfo(Tier t);
    uint16_t Cap(Tier t);
    // The tier a trainer offers next (None past Artisan).
    Tier Next(Tier t);

    enum class TrainResult : uint8_t { Ok, AlreadyKnown, NotNext, SkillTooLow, LevelTooLow, NotEnoughMoney };
    // Whether a trainer teaches `want` to one who knows `have`. One tier at a time, in order. A
    // game master skips the skill, level and money asks (not the order).
    TrainResult CanTrain(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, bool gm = false);
    // The skill a newly learned Apprentice starts at.
    constexpr uint16_t kStartSkill = 1;

    // ── Colours and skill-ups ──
    // A recipe or node's colours: orange from `required` (none below), yellow from `yellow`,
    // green from `green`, gray from `gray`.
    struct Thresholds
    {
        uint16_t required = 1;
        uint16_t yellow = 25;
        uint16_t green = 50;
        uint16_t gray = 75;
    };
    // Vanilla's usual spacing: yellow 25 past the requirement, green 50, gray 75.
    Thresholds Spaced(uint16_t required);

    enum class Color : uint8_t { Red, Orange, Yellow, Green, Gray };
    Color ColorOf(uint16_t skill, Thresholds const& t);

    // The odds of a point from making (or gathering) at `skill`, vanilla's: orange always, gray
    // never, and between them falling straight from yellow to gray, (gray - skill) / (gray -
    // yellow), so a yellow recipe is nearly sure and a late green rarely pays.
    float SkillUpChance(uint16_t skill, Thresholds const& t);
    // The skill after one use: up a point on `roll` (in [0, 1)) under the chance, never past `cap`.
    uint16_t ApplySkillUp(uint16_t skill, uint16_t cap, Thresholds const& t, float roll);

    // ── Pieces ──
    struct Material
    {
        uint32_t item = 0;
        uint32_t count = 0;
    };

    struct Recipe
    {
        Build::PieceType piece = Build::PieceType::None;
        Thresholds skill;
        std::vector<Material> materials;
    };
    // Every known piece's recipe, built pieces then WoW's props, in type order.
    std::vector<Recipe> const& Recipes();
    // The recipe of a known piece; null for an unknown one.
    Recipe const* RecipeFor(Build::PieceType t);

    // A player's bag counts by item entry.
    using Inventory = std::unordered_map<uint32_t, uint32_t>;
    bool CanAfford(Inventory const& bags, std::vector<Material> const& materials);
    // What is short: each material with the count still wanted.
    std::vector<Material> Missing(Inventory const& bags, std::vector<Material> const& materials);

    // Whether a piece may be placed, before the build rules (support, overlap, plots) run.
    enum class PlaceCheck : uint8_t { Ok, Unknown, NotLearned, SkillTooLow, NoMaterials };
    // `free`: a game master, or building set free on the realm: no skill, no materials.
    PlaceCheck CheckPlace(Build::PieceType t, Tier tier, uint16_t skill, Inventory const& bags, bool free);

    // The skill after placing a piece: only a piece paid for with fresh materials teaches (a free
    // build would make skill free too, and salvage would let a builder level by taking a piece
    // down and putting it up again). `paid`: not free, and no salvaged material went into it.
    uint16_t PlacementSkillUp(Build::PieceType t, uint16_t skill, Tier tier, bool paid, float roll);
    // Whether placing a piece could teach a point now (not gray, not at the tier's cap).
    bool CouldTeach(Build::PieceType t, uint16_t skill, Tier tier);

    // What comes back when the owner takes a piece down: this share of each material, scaled by
    // what is left of the piece, rounded down, so building and taking down never makes anything.
    // Releasing a plot, or a lapsed plot packed up, gives back the same (Plots::Pack). All of it
    // comes back salvaged (below).
    constexpr float kRefundShare = 0.5f;
    std::vector<Material> Refund(Build::PieceType t, float healthFraction);
    // The same for a reinforced piece: its reinforcement's share comes back too.
    std::vector<Material> Refund(Build::PieceType t, float healthFraction, uint8_t reinforcement);
    // Only of what was paid for (Build::Piece::paid): a piece built free gives none of its
    // recipe back, a tier reinforced free none of that tier's.
    std::vector<Material> Refund(Build::PieceType t, float healthFraction, uint8_t reinforcement, uint8_t paid);
    // A built piece's refund: by its health, of what was paid for it.
    std::vector<Material> Refund(Build::Piece const& p);

    // ── Salvage: what comes back out of a built piece ──
    // Whatever comes back out of a piece (taken down, packed off a released or lapsed plot, or
    // broken by someone: Refund, Plots::Pack, Salvage) comes back as "Salvaged <material>", an
    // item of its own (world DB 93600-93649) that builds pieces and pays for reinforcement as the
    // fresh one does, but never teaches Construction: skill-ups need fresh materials (cut, mined,
    // bought or made). So taking down and putting up again can't level the skill. Salvaged goods
    // are of no use to the other trades (a smith's recipe wants a fresh bar).
    constexpr uint32_t kSalvagedFirst = 93600;
    constexpr uint32_t kSalvagedLast = 93649;
    struct SalvagePair
    {
        uint32_t fresh;
        uint32_t salvaged;
    };
    // Every material a recipe or reinforcement takes, with its salvaged item.
    std::vector<SalvagePair> const& SalvageTable();
    uint32_t SalvagedOf(uint32_t fresh);        // 0: it has none
    uint32_t FreshOf(uint32_t salvaged);        // 0: not a salvaged item
    // A list with every material that has a salvaged item turned into it (summed by item).
    std::vector<Material> AsSalvaged(std::vector<Material> const& fresh);

    // Paying for a piece (or a reinforcement) from bags that hold fresh and salvaged goods: each
    // material from either, salvaged first unless `preferFresh` (a piece that could teach: fresh
    // first, so it does). `take` lists the items to destroy; `usedSalvage` says a salvaged one
    // went in (and the piece then teaches nothing). Not ok: not enough of something.
    struct Payment
    {
        bool ok = false;
        bool usedSalvage = false;
        std::vector<Material> take;
    };
    Payment PlanPayment(std::vector<Material> const& cost, Inventory const& bags, bool preferFresh);
    // What is short when salvaged goods count as their fresh ones.
    std::vector<Material> MissingForBuild(Inventory const& bags, std::vector<Material> const& materials);
    bool CanAffordBuild(Inventory const& bags, std::vector<Material> const& materials);
    // The items to count in the bags to pay `materials`: each, and its salvaged item.
    std::vector<uint32_t> ItemsToCount(std::vector<Material> const& materials);

    // ── The open world (design doc, section 12) ──
    // Whether a piece is built at all: it has a recipe. The sentry gun is no longer one (a
    // killstreak now, Killstreak.h); it stays a known piece for the killstreak's sentry.
    bool Buildable(Build::PieceType t);

    // Destroying a piece rewards whoever broke it with this share of what went into it (its
    // reinforcement included), each material rounded down, and at least one of its first (main)
    // material when it cost any. A group shares it out (SplitSalvage). It is given salvaged
    // (AsSalvaged), as a refund is: breaking pieces is no way around the fresh-materials rule.
    constexpr float kSalvageShare = 0.4f;
    std::vector<Material> Salvage(Build::PieceType t, uint8_t reinforcement);
    // Only of what was paid for (Build::Piece::paid), as Refund; the one main material only when
    // the piece itself was paid for.
    std::vector<Material> Salvage(Build::PieceType t, uint8_t reinforcement, uint8_t paid);
    // `salvage` dealt out one item at a time among `members`, the first to member `start`; one
    // list a member.
    std::vector<std::vector<Material>> SplitSalvage(std::vector<Material> const& salvage, size_t members, size_t start);

    // Reinforcement: very expensive upgrades the owner pays to put a piece up a tier (to
    // Build::kMaxReinforcement), each asking more Construction and the smiths' heaviest parts.
    // The tier's health and its cut of bullets are Build.h's.
    struct ReinforceTier
    {
        uint8_t tier;
        char const* name;
        uint16_t skill;
        std::vector<Material> materials;
    };
    // Tiers 1 to 3; null otherwise.
    ReinforceTier const* ReinforceInfo(uint8_t tier);
    // Walls, floors, doors, stairs, roofs, beams and sandbags; not the camp's things nor WoW's
    // props.
    bool Reinforceable(Build::PieceType t);
    enum class ReinforceCheck : uint8_t { Ok, NotReinforceable, MaxTier, NotLearned, SkillTooLow, NoMaterials };
    // Whether a piece at `current` may go up a tier. `free`: a game master or a free realm.
    ReinforceCheck CheckReinforce(Build::PieceType t, uint8_t current, Tier tier, uint16_t skill, Inventory const& bags, bool free);
    // Everything spent to reinforce a piece to `tier` (every tier up to it), summed by item.
    std::vector<Material> ReinforcementSpent(uint8_t tier);
    // Only the tiers paid for (Build::Piece::paid's tier bits).
    std::vector<Material> ReinforcementSpent(uint8_t tier, uint8_t paid);

    // ── The wire: the world channel (Packets kChannelWorld), ops 50-59 ──
    // Client: [50, piece id u32]: reinforce the piece a tier.
    // Server: [51, count u8, each: tier u8, skill u16, health x100 u16, bullet cut percent u8,
    //          materials u8, each: item u32, count u16]: the tiers, once with the recipes.
    constexpr uint8_t kOpReinforce = 50;
    constexpr uint8_t kOpReinforcements = 51;
    bool DecodeReinforce(std::vector<uint8_t> const& body, uint32_t& id);
    std::vector<uint8_t> EncodeReinforcements();

    // ── Woodcutting ──
    enum class Node : uint8_t { None, Softwood, SturdyTimber, Hardwood, Ironwood };

    struct NodeDef
    {
        Node node;
        uint32_t gameobject;        // gameobject_template entry
        uint32_t lootId;            // gameobject_loot_template entry (data1)
        char const* name;
        Thresholds skill;
        uint32_t lumber;            // the item it gives
        uint8_t minYield;
        uint8_t maxYield;
        uint32_t respawnMinSec;
        uint32_t respawnMaxSec;
    };
    std::vector<NodeDef> const& Nodes();
    NodeDef const* NodeFor(uint32_t gameobjectEntry);
    NodeDef const* NodeDefOf(Node n);

    enum class GatherCheck : uint8_t { Ok, NotANode, NotLearned, NoAxe, SkillTooLow };
    // Whether a player may cut a node: Construction learned, an axe in the bags, the skill. A game
    // master needs none of them.
    GatherCheck CanGather(uint32_t gameobjectEntry, Tier tier, uint16_t skill, bool hasAxe, bool gm = false);
    // How much lumber a cut gives, `roll` in [0, 1): the node's range, even odds each count. The
    // loot tables in the SQL give the same; this is for the client's tooltip and the tests.
    uint32_t NodeYield(NodeDef const& n, float roll);
    // The skill after a cut.
    uint16_t GatherSkillUp(NodeDef const& n, uint16_t skill, Tier tier, float roll);

    // ── Parts the fusion crafts (world table `fusion_recipe`) ──
    struct CraftRecipe
    {
        uint32_t id = 0;
        uint16_t skillLine = 0;     // 164 Blacksmithing, 202 Engineering, 9001 Construction
        Thresholds skill;
        uint32_t product = 0;
        uint32_t productCount = 1;
        std::vector<Material> reagents;
    };
    enum class CraftCheck : uint8_t { Ok, SkillTooLow, NoReagents };
    // `skill` the crafter's value in the recipe's skill line (0 if unlearned).
    CraftCheck CanCraft(CraftRecipe const& r, uint16_t skill, Inventory const& bags, bool gm = false);

    // ── The wire: on the build channel (Packets kChannelBuild), after Build.h's ops ──
    enum class ServerOp : uint8_t
    {
        Recipes = 16,       // count u16, each: type u8, required yellow green gray u16, materials u8, each: item u32, count u16
        Skill = 17,         // tier u8, skill u16, cap u16
    };
    std::vector<uint8_t> EncodeRecipes(std::vector<Recipe> const& recipes);
    bool DecodeRecipes(std::vector<uint8_t> const& body, std::vector<Recipe>& out);
    std::vector<uint8_t> EncodeSkill(Tier tier, uint16_t skill);

    // ── Kits: pieces crafted ahead, placed later (the profession's "recipes") ──
    // Construction works as WoW's crafting trades do: a piece is made at the profession's trade
    // window (its materials taken, the skill-up rolled by its colour there), which gives the
    // builder one kit of it; the Build Structure ability (Utility) then places a kit in the
    // world. Placing takes the kit and never teaches. A game master (or a free realm) places
    // without kits, as before. Kits are kept per character (`character_fusion_construction_kit`).
    using Kits = std::map<uint8_t, uint32_t>;          // piece type -> kits held
    // The most kits of one piece a character holds, and the most made by one Create All.
    constexpr uint32_t kMaxKits = 999;
    constexpr uint16_t kMaxCraftBatch = 50;
    uint32_t KitsOf(Kits const& k, Build::PieceType t);
    // One kit of `t` taken; false (and nothing taken) when none is held.
    bool TakeKit(Kits& k, Build::PieceType t);
    // `n` kits of `t` added, never past kMaxKits; how many went in.
    uint32_t AddKits(Kits& k, Build::PieceType t, uint32_t n);
    // Whether a kit of a piece may be made: Construction learned, the skill, the materials
    // (salvage counts as fresh, as building did). `free`: a game master or a free realm.
    enum class KitCheck : uint8_t { Ok, Unknown, NotLearned, SkillTooLow, NoMaterials, Full };
    KitCheck CheckKit(Build::PieceType t, Tier tier, uint16_t skill, Inventory const& bags, uint32_t held, bool free);
    // The count a Create All asks, held to 1..kMaxCraftBatch.
    uint16_t CraftBatch(uint16_t asked);

    // The professions channel (shared by the trade windows the client mirrors):
    //   client [1, piece u8, count u16]: make `count` kits of the piece (Create / Create All).
    //   server [2, n u16, n x (piece u8, kits u16)]: the kits held, at login and on a change.
    constexpr uint8_t kChannelProfessions = 12;
    constexpr uint8_t kOpCraftKit = 1;
    constexpr uint8_t kOpKits = 2;
    bool DecodeCraftKit(std::vector<uint8_t> const& body, Build::PieceType& t, uint16_t& count);
    std::vector<uint8_t> EncodeCraftKit(Build::PieceType t, uint16_t count);
    std::vector<uint8_t> EncodeKits(Kits const& k);
    bool DecodeKits(std::vector<uint8_t> const& body, Kits& out);
}}

#endif
