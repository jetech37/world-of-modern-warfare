/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Companions.h"

#include <fstream>
#include <map>
#include <set>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    CompanionSpecies const& Species(uint16_t id)
    {
        CompanionSpecies const* s = FindSpecies(StarterSpecies(), id);
        static CompanionSpecies none;
        return s ? *s : none;
    }

    // A summoned companion standing in the right place for the black whelpling's first rule.
    EvolveContext Ready(uint8_t ownerLevel)
    {
        EvolveContext c;
        c.ownerLevel = ownerLevel;
        c.summoned = true;
        c.zoneId = 46;      // Burning Steppes
        c.areaId = 0;
        c.hour = 12;
        return c;
    }

    // The comma-separated fields of one SQL row "(a, 'b, c', d)", quotes stripped.
    std::vector<std::string> Fields(std::string const& row)
    {
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false;
        size_t start = row.find('('), end = row.rfind(')');
        for (size_t i = start + 1; i < end; ++i)
        {
            char c = row[i];
            if (c == '\'' && quoted && i + 1 < end && row[i + 1] == '\'')
                cur += row[++i];    // '' is a quote inside a string
            else if (c == '\'')
                quoted = !quoted;
            else if (c == ',' && !quoted)
            {
                out.push_back(cur);
                cur.clear();
            }
            else if (!(c == ' ' && cur.empty() && !quoted))
                cur += c;
        }
        out.push_back(cur);
        return out;
    }

    std::string Join(std::vector<uint32_t> const& v)
    {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i)
            s += (i ? "," : "") + std::to_string(v[i]);
        return s;
    }
}

// ── Levels and experience ───────────────────────────────────────────────────────────────────

TEST(companion_xp_curve_rises_to_60_and_is_faster_than_a_player)
{
    CHECK(PlayerXpToNext(1) == 400);        // 1.12's own numbers
    CHECK(PlayerXpToNext(2) == 900);
    CHECK(PlayerXpToNext(59) == 209800);
    CHECK(XpToNext(60) == 0);
    CHECK(XpToNext(1) == 240);
    for (uint8_t l = 1; l < kMaxLevel; ++l)
    {
        CHECK(XpToNext(l) > 0);
        CHECK(XpToNext(l) < PlayerXpToNext(l));
        if (l > 1)
            CHECK(XpToNext(l) > XpToNext(l - 1));
    }
    // Fed enough, a companion goes all the way to 60 and stops there.
    CompanionState s;
    uint32_t total = 0;
    for (uint8_t l = 1; l < kMaxLevel; ++l)
        total += XpToNext(l);
    XpGain g = GainXp(s, total + 12345, 60);
    CHECK(s.level == 60);
    CHECK(s.xp == 0);
    CHECK(g.levels == 59);
    CHECK(g.wasted == 12345);
    CHECK(GainXp(s, 500, 60).wasted == 500);
}

TEST(companion_never_outlevels_its_owner)
{
    CompanionState s;
    XpGain g = GainXp(s, 1000000, 10);
    CHECK(s.level == 10);
    CHECK(g.levels == 9);
    CHECK(g.capped);
    CHECK(s.xp == XpToNext(10));        // a full bar banked...
    CHECK(g.wasted > 0);                // ...and the rest dropped
    CHECK(EffectiveLevel(s, 10) == 10);

    // The owner dings: the banked bar pays out at once, and no further.
    g = GainXp(s, 0, 11);
    CHECK(s.level == 11);
    CHECK(s.xp == 0);
    CHECK(g.levels == 1);
    CHECK(!g.capped);

    // Traded to a lower character: it keeps its level on the item, fights at the owner's.
    CompanionState bought;
    bought.level = 40;
    CHECK(EffectiveLevel(bought, 20) == 20);
    CHECK(EffectiveLevel(bought, 60) == 40);
    g = GainXp(bought, 100, 20);
    CHECK(bought.level == 40);          // never loses a level...
    CHECK(g.levels == 0);               // ...and never gains one past the owner
    CHECK(bought.xp == 100);
    g = GainXp(bought, 10000000, 20);
    CHECK(bought.level == 40);
    CHECK(bought.xp == XpToNext(40));
    CHECK(g.capped);
}

TEST(companion_kill_xp_follows_gray_and_elite_rules)
{
    CHECK(KillXp(10, 10, false) == 95);         // 5 L + 45 at the same level
    CHECK(KillXp(10, 10, true) == 190);         // elites double
    CHECK(KillXp(10, 12, false) == 104);        // +5% a level above
    CHECK(KillXp(10, 20, false) == KillXp(10, 14, false));  // capped at +4
    CHECK(KillXp(10, 8, false) < KillXp(10, 10, false));    // green: less
    CHECK(GrayLevel(10) == 4);
    CHECK(KillXp(10, 4, false) == 0);           // gray: nothing
    CHECK(KillXp(5, 1, false) > 0);             // nothing is gray to a level 5
    CHECK(GrayLevel(60) == 51);
    CHECK(KillXp(60, 51, true) == 0);
    for (uint8_t l = 1; l <= 60; ++l)
        CHECK(KillXp(l, l, false) == 5u * l + 45u);
}

// ── Stats ───────────────────────────────────────────────────────────────────────────────────

TEST(companion_stats_scale_and_stay_below_a_hunter_pet)
{
    CompanionStats pet60 = HunterPetBaseline(60);
    CHECK(pet60.health == 3340);
    CHECK_NEAR(pet60.Dps(), 49.5, 0.01);
    CHECK(pet60.armor == 3000);
    for (CompanionSpecies const& sp : StarterSpecies())
        for (uint8_t stage = 0; stage < sp.stageCount; ++stage)
        {
            CompanionStats prev;
            for (uint8_t l = 1; l <= 60; ++l)
            {
                CompanionStats c = Stats(sp.role, stage, sp.stageCount, l);
                CompanionStats pet = HunterPetBaseline(l);
                CHECK(c.health < pet.health);
                CHECK(c.Dps() < pet.Dps());
                CHECK(c.armor < pet.armor);
                CHECK(c.damageMin < c.damageMax);
                if (l > 1)
                {
                    CHECK(c.health > prev.health);
                    CHECK(c.Dps() > prev.Dps());
                }
                prev = c;
            }
        }
    // Each evolution is a real step up.
    CompanionStats baby = Stats(Role::Melee, 0, 3, 40), adult = Stats(Role::Melee, 1, 3, 40),
        elder = Stats(Role::Melee, 2, 3, 40);
    CHECK(baby.health < adult.health && adult.health < elder.health);
    CHECK(baby.Dps() < adult.Dps() && adult.Dps() < elder.Dps());
    // Roles differ the way they say: a tank outlasts, a ranged one hits from afar.
    CHECK(Stats(Role::Tank, 1, 3, 40).health > Stats(Role::Ranged, 1, 3, 40).health);
    CHECK(Stats(Role::Ranged, 1, 3, 40).Dps() > Stats(Role::Tank, 1, 3, 40).Dps());
    CHECK(Stats(Role::Ranged, 1, 3, 40).reach == kRangedReach);
    CHECK(Stats(Role::Melee, 1, 3, 40).reach == 0.f);
    // Stats follow the effective level: a level 50 companion under a level 20 owner hits as a 20.
    CompanionState s;
    s.level = 50;
    s.stage = 1;
    CHECK(Stats(Species(1), s, 20).health == Stats(Role::Ranged, 1, 3, 20).health);
}

// ── Evolution ───────────────────────────────────────────────────────────────────────────────

TEST(companion_evolution_wants_level_place_and_catalyst)
{
    CompanionSpecies const& whelp = Species(1);    // Black Whelpling: 20 in Blackrock lands, then
    CHECK(whelp.stageCount == 3);                 // 45 with a Draconic Essence at Blackrock or the Wyrmbog
    CompanionState s;
    s.level = 19;
    EvolveContext ctx = Ready(60);
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::LevelTooLow);
    s.level = 20;
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::None);
    // The owner's level counts too: a traded level 20 whelp waits for a level 15 owner.
    CHECK(CanEvolve(whelp, s, Ready(15)) == EvolveBlock::LevelTooLow);
    EvolveContext away = ctx;
    away.zoneId = 12;   // Elwynn Forest
    CHECK(CanEvolve(whelp, s, away) == EvolveBlock::WrongPlace);
    EvolveContext sub = away;
    sub.areaId = 511;   // the Wyrmbog, a sub-area of Dustwallow Marsh
    sub.zoneId = 15;
    CHECK(CanEvolve(whelp, s, sub) == EvolveBlock::None);
    EvolveContext busy = ctx;
    busy.inCombat = true;
    CHECK(CanEvolve(whelp, s, busy) == EvolveBlock::InCombat);
    EvolveContext stabled = ctx;
    stabled.summoned = false;
    CHECK(CanEvolve(whelp, s, stabled) == EvolveBlock::NotSummoned);

    Evolve(s);
    CHECK(s.stage == 1);
    CHECK(s.level == 20);
    s.level = 45;
    ctx.zoneId = 25;    // Blackrock Mountain
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::MissingItem);
    ctx.itemsHeld = 1;
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::None);
    ctx.zoneId = 46;    // the Burning Steppes do not count for the second step
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::WrongPlace);
    ctx.zoneId = 25;
    Evolve(s);
    CHECK(s.stage == 2);
    CHECK(CanEvolve(whelp, s, ctx) == EvolveBlock::FinalStage);
    CHECK(NextRule(whelp, s) == nullptr);

    // A critter grows too (20 and 40, anywhere, no catalyst) and stops at its third form.
    CompanionState cat;
    cat.level = 60;
    CHECK(CanEvolve(Species(26), cat, Ready(60)) == EvolveBlock::None);
    cat.stage = 2;
    CHECK(CanEvolve(Species(26), cat, Ready(60)) == EvolveBlock::FinalStage);
}

TEST(companion_evolution_conditions_time_and_kills)
{
    EvolutionRule night;
    night.level = 10;
    night.condition = Condition::Night;
    CompanionState s;
    s.level = 10;
    EvolveContext c = Ready(60);
    c.hour = 14;
    CHECK(CanEvolve(night, s, c) == EvolveBlock::WrongTime);
    c.hour = 23;
    CHECK(CanEvolve(night, s, c) == EvolveBlock::None);
    c.hour = 3;
    CHECK(CanEvolve(night, s, c) == EvolveBlock::None);
    EvolutionRule day = night;
    day.condition = Condition::Day;
    CHECK(CanEvolve(day, s, c) == EvolveBlock::WrongTime);

    // The murloc hatchling's second step: 30 humanoids slain while it was out.
    CompanionSpecies const& murloc = Species(23);
    CompanionState m;
    m.level = 30;
    m.stage = 1;
    EvolveContext anywhere = Ready(60);
    CHECK(CanEvolve(murloc, m, anywhere) == EvolveBlock::NeedKills);
    CHECK(!RecordKill(murloc, m, CreatureType::Beast));     // the wrong kind doesn't count
    for (int i = 0; i < 30; ++i)
        CHECK(RecordKill(murloc, m, CreatureType::Humanoid));
    CHECK(!RecordKill(murloc, m, CreatureType::Humanoid));  // full; no more counting
    CHECK(m.evolveKills == 30);
    CHECK(CanEvolve(murloc, m, anywhere) == EvolveBlock::None);
    Evolve(m);
    CHECK(m.evolveKills == 0);
    CHECK(m.stage == 2);
}

// ── Drops ───────────────────────────────────────────────────────────────────────────────────

TEST(companion_drop_chances_are_rare_and_ranked)
{
    CHECK_NEAR(DropChance(CreatureType::Critter, CreatureRank::Normal, true), 0.005, 1e-6);
    CHECK_NEAR(DropChance(CreatureType::Beast, CreatureRank::Normal, false), 0.002, 1e-6);
    CHECK_NEAR(DropChance(CreatureType::Beast, CreatureRank::Elite, false), 0.01, 1e-6);
    CHECK_NEAR(DropChance(CreatureType::Dragonkin, CreatureRank::RareElite, false), 0.05, 1e-6);
    CHECK_NEAR(DropChance(CreatureType::Dragonkin, CreatureRank::WorldBoss, false), 0.03, 1e-6);
    CHECK_NEAR(DropChance(CreatureType::Humanoid, CreatureRank::DungeonBoss, false), 0.03, 1e-6);
    CHECK(DropChance(CreatureType::Totem, CreatureRank::Normal, false) == 0.f);
    CHECK(DropChance(CreatureType::None, CreatureRank::Elite, false) == 0.f);

    auto const& all = StarterSpecies();
    // A listed entry wins: a black broodling drops the black whelpling.
    DropVictim broodling{7047, CreatureType::Dragonkin, 0, CreatureRank::Normal};
    std::vector<uint16_t> d = DroppableSpecies(all, broodling);
    CHECK(d.size() == 1 && d[0] == 1);
    // An unlisted wolf falls back to its pet family.
    DropVictim wolf{999999, CreatureType::Beast, 1, CreatureRank::Normal};
    d = DroppableSpecies(all, wolf);
    CHECK(d.size() == 1 && d[0] == 6);
    // An unlisted cat could be any of the cat cubs.
    DropVictim cat{999998, CreatureType::Beast, 2, CreatureRank::Normal};
    CHECK(DroppableSpecies(all, cat).size() == 3);
    // Any critter: the wild rabbit; a rat: the alley cat.
    DropVictim chicken{620, CreatureType::Critter, 0, CreatureRank::Normal};
    d = DroppableSpecies(all, chicken);
    CHECK(d.size() == 1 && d[0] == 27);
    DropVictim rat{2110, CreatureType::Critter, 0, CreatureRank::Normal};
    d = DroppableSpecies(all, rat);
    CHECK(d.size() == 1 && d[0] == 26);
    // A beast with no matching family drops nothing; a humanoid that isn't a murloc neither.
    CHECK(DroppableSpecies(all, {999997, CreatureType::Beast, 0, CreatureRank::Normal}).empty());
    CHECK(DroppableSpecies(all, {999996, CreatureType::Humanoid, 0, CreatureRank::Normal}).empty());

    CHECK(RollDrop(all, broodling, 0.0019f, 0.5f) == 1);
    CHECK(RollDrop(all, broodling, 0.0021f, 0.5f) == 0);
    CHECK(RollDrop(all, cat, 0.f, 0.f) != RollDrop(all, cat, 0.f, 0.99f));
    CHECK(RollDrop(all, {999996, CreatureType::Humanoid, 0, CreatureRank::Normal}, 0.f, 0.f) == 0);
    // Rolled many times, the rate is what it says.
    int drops = 0;
    for (int i = 0; i < 100000; ++i)
        drops += RollDrop(all, chicken, float(i) / 100000.f, 0.5f) ? 1 : 0;
    CHECK(drops == 500);
}

TEST(companion_catalysts_drop_from_their_creature_type)
{
    CHECK(CatalystFor(CreatureType::Dragonkin) == Catalyst::DraconicEssence);
    CHECK(CatalystFor(CreatureType::Beast) == Catalyst::PrimalBeastEssence);
    CHECK(CatalystFor(CreatureType::Elemental) == Catalyst::ElementalCore);
    CHECK(CatalystFor(CreatureType::Demon) == Catalyst::FelEmber);
    CHECK(CatalystFor(CreatureType::Undead) == Catalyst::NecroticResidue);
    CHECK(CatalystFor(CreatureType::Mechanical) == Catalyst::ArcaneGearwork);
    CHECK(CatalystFor(CreatureType::Humanoid) == Catalyst::TribalFetish);
    CHECK(CatalystFor(CreatureType::Critter) == 0);
    CHECK(CatalystDropChance(CreatureType::Beast, CreatureRank::Normal, 24) == 0.f);
    CHECK(CatalystDropChance(CreatureType::Beast, CreatureRank::Normal, 25) > 0.f);
    CHECK(CatalystDropChance(CreatureType::Dragonkin, CreatureRank::RareElite, 50) >
        CatalystDropChance(CreatureType::Dragonkin, CreatureRank::Elite, 50));
    CHECK(CatalystDropChance(CreatureType::Critter, CreatureRank::WorldBoss, 60) == 0.f);
    // Every catalyst a rule asks for is one that drops somewhere.
    std::set<uint32_t> dropped = {Catalyst::DraconicEssence, Catalyst::PrimalBeastEssence,
        Catalyst::ElementalCore, Catalyst::FelEmber, Catalyst::NecroticResidue, Catalyst::ArcaneGearwork,
        Catalyst::TribalFetish};
    for (CompanionSpecies const& sp : StarterSpecies())
        for (EvolutionRule const& r : sp.evolutions)
            if (r.item)
            {
                CHECK(IsCatalystItem(r.item));
                CHECK(dropped.count(r.item) == 1);
            }
}

// ── The catalog, and its SQL twin ───────────────────────────────────────────────────────────

TEST(companion_starter_catalog_is_consistent)
{
    auto const& all = StarterSpecies();
    CHECK(all.size() == 33 + 64 + 50 + 9);    // the starter set, the catalog's lines, the vanilla pets, the flying mounts
    std::set<uint16_t> ids;
    std::set<uint32_t> items;
    std::set<Family> families;
    for (CompanionSpecies const& sp : all)
    {
        CHECK(ids.insert(sp.id).second);
        CHECK(items.insert(sp.item).second);
        families.insert(sp.family);
        CHECK(sp.item == SpeciesItem(sp.id));
        CHECK(sp.id <= 255);    // the item seed's eight bits
        CHECK(IsCompanionItem(sp.item));
        CHECK(FindByItem(all, sp.item) == &sp);
        CHECK(sp.stageCount >= 1 && sp.stageCount <= kMaxStages);
        for (uint8_t i = 0; i < sp.stageCount; ++i)
        {
            CHECK(sp.stages[i].creature != 0);
            CHECK(sp.stages[i].scale >= 0.15f && sp.stages[i].scale <= 2.5f);
        }
        // A rule for every step there is, none for steps there aren't, levels climbing.
        uint8_t lastLevel = 0;
        for (uint8_t i = 0; i + 1 < kMaxStages; ++i)
        {
            bool step = i + 1 < sp.stageCount;
            CHECK(sp.evolutions[i].Exists() == step);
            if (step)
            {
                CHECK(sp.evolutions[i].level > lastLevel && sp.evolutions[i].level <= kMaxLevel);
                lastLevel = sp.evolutions[i].level;
            }
        }
        CHECK(!sp.drops.entries.empty() || sp.drops.type != CreatureType::None);
    }
    CHECK(families.size() >= 15);
    CHECK(!IsCompanionItem(90101) && IsCatalystItem(90101));
    CHECK(IsCompanionItem(91164) && !IsCompanionItem(90100) && !IsCompanionItem(92000));
    for (uint32_t c : {90105u, 90106u, 90107u})
        CHECK(IsCatalystItem(c) && !IsCompanionItem(c));
}

TEST(companion_sql_matches_the_catalog)
{
    // 02 has the starter rows, 08 the new lines (one more column, ride_display) and the
    // starter set's rescale as UPDATEs, 28 every stage's display and scale (the look table) and
    // the ride displays of the lines that ride on the wolf's spells, as UPDATEs applied in order.
    std::string line;
    bool inSpecies = false, inStarts = false;
    std::vector<std::vector<std::string>> rows, starts;
    std::set<uint32_t> itemRows;
    std::map<uint32_t, std::map<int, float>> rescale;   // id -> stage (0-based) -> scale
    std::map<uint32_t, std::map<int, uint32_t>> displays;   // id -> stage (0-based) -> display
    std::map<uint32_t, uint32_t> rideDisplays;
    for (char const* file : {"02_companions.sql", "08_companion_lines.sql", "29_companion_looks.sql", "31_vanilla_companions.sql", "33_wotlk_looks.sql",
             "35_flying_companions.sql"})
    {
        std::ifstream in(std::string(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/") + file);
        CHECK(in.good());
        if (!in.good())
            return;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.rfind("INSERT INTO `fusion_companion_species`", 0) == 0)
                inSpecies = true;
            else if (line.rfind("INSERT INTO `fusion_companion_item`", 0) == 0)
                inStarts = true;
            else if ((inSpecies || inStarts) && line.rfind("(", 0) == 0)
            {
                (inSpecies ? rows : starts).push_back(Fields(line));
                if (line.find(");") != std::string::npos)
                    inSpecies = inStarts = false;
            }
            else if (line.rfind("(90", 0) == 0 || line.rfind("(91", 0) == 0)
                itemRows.insert(uint32_t(std::stoul(Fields(line)[0])));
            else if (line.rfind("UPDATE `fusion_companion_species` SET ", 0) == 0)
            {
                // UPDATE ... SET `stage1_scale` = a, `stage2_scale` = b WHERE `id` = n;
                size_t const id = line.find("`id` = ");
                CHECK(id != std::string::npos);
                if (id == std::string::npos)
                    continue;
                uint32_t const sid = uint32_t(std::stoul(line.substr(id + 7)));
                for (int st = 0; st < 3; ++st)
                {
                    std::string const sk = "`stage" + std::to_string(st + 1) + "_scale` = ";
                    if (size_t const s = line.find(sk); s != std::string::npos)
                        rescale[sid][st] = std::stof(line.substr(s + sk.size()));
                    std::string const key = "`stage" + std::to_string(st + 1) + "_display` = ";
                    if (size_t const d = line.find(key); d != std::string::npos)
                        displays[sid][st] = uint32_t(std::stoul(line.substr(d + key.size())));
                }
                if (size_t const r = line.find("`ride_display` = "); r != std::string::npos)
                    rideDisplays[sid] = uint32_t(std::stoul(line.substr(r + 17)));
            }
        }
    }
    // The rescale lands on the starter rows.
    for (auto& f : rows)
        if (f.size() >= 11 && rescale.count(uint32_t(std::stoul(f[0]))))
        {
            for (auto const& [st, sc] : rescale[uint32_t(std::stoul(f[0]))])
                f[6 + 2 * st] = std::to_string(sc);
        }
    // The mounts' companion items, row for row.
    auto const& items = StarterItems();
    CHECK(starts.size() == items.size());
    for (size_t i = 0; i < starts.size() && i < items.size(); ++i)
    {
        CHECK(starts[i].size() == 4);
        if (starts[i].size() != 4)
            continue;
        CHECK(std::stoul(starts[i][0]) == items[i].item);
        CHECK(std::stoul(starts[i][1]) == items[i].species);
        CHECK(std::stoul(starts[i][2]) == items[i].stage);
        CHECK(std::stoul(starts[i][3]) == items[i].level);
        CHECK(itemRows.count(items[i].item) == 1);
    }
    auto const& all = StarterSpecies();
    CHECK(rows.size() == all.size());
    for (size_t i = 0; i < rows.size() && i < all.size(); ++i)
    {
        auto const& f = rows[i];
        CompanionSpecies const& sp = all[i];
        CHECK(f.size() == (sp.id < 100 ? 31u : 32u));
        if (f.size() < 31)
            continue;
        uint32_t rideDisplay = 0;
        // The ride columns: the 1-based rideable stage and its two spells.
        uint32_t rideStage = 0;
        for (uint8_t st = 0; st < sp.stageCount; ++st)
            if (sp.stages[st].rideable)
                rideStage = st + 1u;
        CHECK(std::stoul(f[28]) == rideStage);
        CHECK(std::stoul(f[29]) == (rideStage ? sp.stages[rideStage - 1].mountSpell : 0u));
        CHECK(std::stoul(f[30]) == (rideStage ? sp.stages[rideStage - 1].swiftMountSpell : 0u));
        if (rideStage)
            rideDisplay = sp.stages[rideStage - 1].rideDisplay;
        uint32_t const sqlRide = rideDisplays.count(sp.id) ? rideDisplays[sp.id]
            : f.size() > 31 ? uint32_t(std::stoul(f[31])) : 0u;
        CHECK(sqlRide == rideDisplay);
        // Every stage's display, as the look table has it.
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            uint32_t const sqlDisplay = displays.count(sp.id) && displays[sp.id].count(st) ? displays[sp.id][st] : 0u;
            CHECK(sqlDisplay == sp.stages[st].display);
        }
        CHECK(std::stoul(f[0]) == sp.id);
        CHECK(f[1] == sp.name);
        CHECK(std::stoul(f[2]) == sp.item);
        CHECK(std::stoul(f[3]) == uint32_t(sp.family));
        CHECK(std::stoul(f[4]) == uint32_t(sp.role));
        for (uint8_t st = 0; st < kMaxStages; ++st)
        {
            uint32_t creature = st < sp.stageCount ? sp.stages[st].creature : 0;
            CHECK(std::stoul(f[5 + 2 * st]) == creature);
            if (st < sp.stageCount)
                CHECK_NEAR(std::stof(f[6 + 2 * st]), sp.stages[st].scale, 1e-4);
        }
        for (int e = 0; e < 2; ++e)
        {
            EvolutionRule const& r = sp.evolutions[e];
            size_t b = 11 + 7 * e;
            CHECK(std::stoul(f[b]) == r.level);
            CHECK(std::stoul(f[b + 1]) == r.item);
            CHECK(std::stoul(f[b + 2]) == r.itemCount);
            CHECK(f[b + 3] == Join(r.areas));
            CHECK(std::stoul(f[b + 4]) == uint32_t(r.condition));
            CHECK(std::stoul(f[b + 5]) == uint32_t(r.conditionType));
            CHECK(std::stoul(f[b + 6]) == r.conditionCount);
        }
        CHECK(f[25] == Join(sp.drops.entries));
        CHECK(std::stoul(f[26]) == uint32_t(sp.drops.type));
        CHECK(std::stoul(f[27]) == sp.drops.petFamily);
        CHECK(itemRows.count(sp.item) == 1);    // its item exists
    }
    for (uint32_t c : {90101u, 90102u, 90103u, 90104u, 90105u, 90106u, 90107u})
        CHECK(itemRows.count(c) == 1);
}

// ── Duels ───────────────────────────────────────────────────────────────────────────────────

TEST(companion_duel_runs_challenge_to_result)
{
    CompanionDuel d;
    CHECK(!d.Challenge(1, 1, 0));               // not yourself
    CHECK(d.Challenge(1, 2, 1000));
    CHECK(d.Phase() == DuelPhase::Challenged);
    CHECK(d.Involves(2) && !d.Involves(3));
    CHECK(!d.Challenge(1, 3, 1000));            // one at a time
    CHECK(!d.Accept(1, 2000));                  // only the challenged accepts
    CHECK(d.Accept(2, 2000));
    CHECK(d.Phase() == DuelPhase::Countdown);
    CHECK(d.CountdownLeftMs(3000) == 2000);
    d.Beaten(1);                                // no one is beaten before the fight
    CHECK(d.Phase() == DuelPhase::Countdown);
    d.Update(4999);
    CHECK(d.Phase() == DuelPhase::Countdown);
    d.Update(5000);
    CHECK(d.Phase() == DuelPhase::Fighting);
    d.Beaten(99);                               // a stranger changes nothing
    CHECK(d.Phase() == DuelPhase::Fighting);
    d.Beaten(2);
    CHECK(d.Phase() == DuelPhase::Finished);
    CHECK(d.Result().end == DuelEnd::Beaten);
    CHECK(d.Result().winner == 0);
    d.Forfeit(1);                               // finished stays finished
    CHECK(d.Result().winner == 0);
    CHECK(d.Challenge(2, 1, 9000));             // and a new one can begin
    CHECK(d.Result().end == DuelEnd::None);
}

TEST(companion_duel_ends_on_decline_expiry_flight_meddling_and_time)
{
    CompanionDuel decline;
    decline.Challenge(1, 2, 0);
    CHECK(!decline.Decline(3));
    CHECK(decline.Decline(2));
    CHECK(decline.Result().end == DuelEnd::Declined && decline.Result().winner == -1);

    CompanionDuel expire;
    expire.Challenge(1, 2, 0);
    expire.Update(CompanionDuel::kChallengeTimeoutMs - 1);
    CHECK(expire.Phase() == DuelPhase::Challenged);
    expire.Update(CompanionDuel::kChallengeTimeoutMs);
    CHECK(expire.Result().end == DuelEnd::Expired);

    CompanionDuel early;                        // running off before the fight just cancels
    early.Challenge(1, 2, 0);
    early.Accept(2, 0);
    early.Fled(1);
    CHECK(early.Result().end == DuelEnd::Cancelled && early.Result().winner == -1);

    auto fighting = [](CompanionDuel& d) {
        d.Challenge(1, 2, 0);
        d.Accept(2, 0);
        d.Update(CompanionDuel::kCountdownMs);
    };
    CompanionDuel fled;
    fighting(fled);
    fled.Fled(1);
    CHECK(fled.Result().end == DuelEnd::Fled && fled.Result().winner == 1);

    CompanionDuel meddle;
    fighting(meddle);
    meddle.Interfered(2);
    CHECK(meddle.Result().end == DuelEnd::Interfered && meddle.Result().winner == 0);

    CompanionDuel quit;
    fighting(quit);
    quit.Forfeit(2);
    CHECK(quit.Result().end == DuelEnd::Forfeit && quit.Result().winner == 0);

    CompanionDuel slow;
    fighting(slow);
    uint32_t limit = CompanionDuel::kCountdownMs + CompanionDuel::kMaxFightMs;
    slow.Update(limit - 1, 0.3f, 0.6f);
    CHECK(slow.Phase() == DuelPhase::Fighting);
    slow.Update(limit, 0.3f, 0.6f);
    CHECK(slow.Result().end == DuelEnd::Timeout && slow.Result().winner == 1);

    CompanionDuel even;
    fighting(even);
    even.Update(limit, 0.5f, 0.505f);
    CHECK(even.Result().end == DuelEnd::Timeout && even.Result().winner == -1);

    CHECK(CompanionDuel::InArena({0, 0, 0}, {30, 0, 0}));
    CHECK(!CompanionDuel::InArena({0, 0, 0}, {30, 30, 0}));
}

TEST(companion_duel_levels_and_reward)
{
    CHECK(DuelLevels(40, 25) == std::make_pair(uint8_t(25), uint8_t(25)));
    CHECK(DuelLevels(10, 10) == std::make_pair(uint8_t(10), uint8_t(10)));
    CHECK(DuelWinXp(30, 30) == XpToNext(30) / 20);
    CHECK(DuelWinXp(30, GrayLevel(30)) == 0);   // no farming a weakling
    CHECK(DuelWinXp(60, 60) == 0);
    // Only a fight won pays: no farming by forfeiting at once.
    CHECK(DuelPaysXp(DuelEnd::Beaten) && DuelPaysXp(DuelEnd::Timeout));
    for (DuelEnd e : {DuelEnd::None, DuelEnd::Forfeit, DuelEnd::Fled, DuelEnd::Interfered, DuelEnd::Declined,
             DuelEnd::Expired, DuelEnd::Cancelled})
        CHECK(!DuelPaysXp(e));
    // A challenge waiting for its answer holds nobody's companion; the countdown and fight do.
    CHECK(!DuelHolds(DuelPhase::Idle) && !DuelHolds(DuelPhase::Challenged) && !DuelHolds(DuelPhase::Finished));
    CHECK(DuelHolds(DuelPhase::Countdown) && DuelHolds(DuelPhase::Fighting));
}

// ── Packets ─────────────────────────────────────────────────────────────────────────────────

TEST(companion_bar_packet_round_trips)
{
    CompanionSpecies const& whelp = Species(1);
    CompanionState s;
    s.level = 20;
    s.xp = 1234;
    EvolveContext ctx = Ready(60);
    Bar b = MakeBar(0x0102030405060708ull, whelp, s, ctx, 87);
    CHECK(b.flags == (BAR_SUMMONED | BAR_CAN_EVOLVE));
    CHECK(b.block == EvolveBlock::None);
    CHECK(b.nextEvolveLevel == 20);
    CHECK(b.catalystCount == 0);
    CHECK(b.xpToNext == XpToNext(20));
    std::vector<uint8_t> bytes = EncodeBar(b);
    CHECK(bytes.size() == 27);
    CHECK(bytes[0] == kOpBar);
    CHECK(bytes[1] == 0x08 && bytes[8] == 0x01);    // little-endian guid
    CHECK(bytes[9] == 1 && bytes[10] == 0);         // species
    CHECK(bytes[14] == (1234 & 0xFF) && bytes[15] == (1234 >> 8));
    Bar back;
    CHECK(DecodeBar(bytes, back));
    CHECK(back.item == b.item && back.species == 1 && back.level == 20 && back.effectiveLevel == 20);
    CHECK(back.xp == 1234 && back.xpToNext == b.xpToNext && back.flags == b.flags);
    CHECK(back.block == EvolveBlock::None && back.nextEvolveLevel == 20 && back.healthPct == 87);
    bytes.pop_back();
    CHECK(!DecodeBar(bytes, back));

    // Held at its owner's level with a full bar: flagged, and the next step wants its essence.
    CompanionState full;
    full.stage = 1;
    full.level = 30;
    GainXp(full, 10000000, 30);
    Bar capped = MakeBar(1, whelp, full, Ready(30), 100);
    CHECK(capped.flags & BAR_CAPPED);
    CHECK(capped.block == EvolveBlock::LevelTooLow);
    CHECK(capped.nextEvolveLevel == 45 && capped.catalystCount == 1);
    full.stage = 2;
    CHECK(MakeBar(1, whelp, full, Ready(30), 100).flags & BAR_FINAL_STAGE);

    std::vector<uint8_t> ev = EncodeEvolved(7, 3, 2);
    CHECK(ev == std::vector<uint8_t>({kOpEvolved, 7, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2}));

    DuelUpdate du;
    du.phase = DuelPhase::Finished;
    du.end = DuelEnd::Beaten;
    du.winner = 0;
    du.opponent = 42;
    std::vector<uint8_t> db = EncodeDuel(du);
    CHECK(db.size() == 12);
    DuelUpdate dback;
    CHECK(DecodeDuel(db, dback));
    CHECK(dback.phase == DuelPhase::Finished && dback.end == DuelEnd::Beaten && dback.winner == 0 &&
        dback.opponent == 42);
    du.winner = -1;
    CHECK(DecodeDuel(EncodeDuel(du), dback) && dback.winner == -1);
}

TEST(companion_requests_decode_strictly)
{
    Request r;
    CHECK(DecodeRequest(EncodeRequest({kOpSummon, 0xABCDull, false}), r));
    CHECK(r.op == kOpSummon && r.guid == 0xABCDull);
    CHECK(DecodeRequest(EncodeRequest({kOpEvolve, 5, false}), r) && r.guid == 5);
    CHECK(DecodeRequest(EncodeRequest({kOpDuelChallenge, 77, false}), r) && r.guid == 77);
    CHECK(DecodeRequest(EncodeRequest({kOpDuelAnswer, 0, true}), r) && r.accept);
    CHECK(DecodeRequest(EncodeRequest({kOpDismiss, 0, false}), r) && r.op == kOpDismiss);
    CHECK(DecodeRequest(EncodeRequest({kOpDuelForfeit, 0, false}), r));
    CHECK(EncodeRequest({kOpDismiss, 0, false}).size() == 1);
    CHECK(!DecodeRequest({}, r));
    CHECK(!DecodeRequest({kOpSummon, 1, 2}, r));            // short
    CHECK(!DecodeRequest({kOpDismiss, 0}, r));              // long
    CHECK(!DecodeRequest({kOpBar}, r));                     // a server op
    CHECK(!DecodeRequest({200}, r));
}

// ── Riding ──────────────────────────────────────────────────────────────────────────────────

TEST(companion_mount_families_grow_into_a_rideable_last_stage)
{
    // Every mount family's last stage, and only it, is a mount with both vanilla spells.
    std::set<Family> mountFamilies = {Family::Wolf, Family::Cat, Family::Raptor, Family::Kodo, Family::Horse,
        Family::Mechanostrider, Family::SkeletalHorse, Family::Ram, Family::Spider, Family::Bear, Family::Scorpid,
        Family::Crocolisk, Family::Turtle, Family::Boar, Family::Tallstrider, Family::Basilisk, Family::Dragon,
        Family::Felhound, Family::CryptFiend, Family::Mechanical, Family::Gryphon, Family::WindRider,
        Family::Hippogryph, Family::Dragonhawk, Family::FireElemental};
    std::set<Family> ridden;
    for (CompanionSpecies const& sp : StarterSpecies())
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            StageInfo const& stage = sp.stages[st];
            if (!stage.rideable)
            {
                CHECK(stage.mountSpell == 0 && stage.swiftMountSpell == 0);
                continue;
            }
            ridden.insert(sp.family);
            CHECK(st + 1 == sp.stageCount);
            CHECK(stage.mountSpell != 0 && stage.swiftMountSpell != 0 && stage.mountSpell != stage.swiftMountSpell);
            CHECK(mountFamilies.count(sp.family) == 1);
            CHECK(!IsHumanoidFamily(sp.family));
            // A new line rides on the wolf's spells and shows its own display.
            if (sp.id > 100)
                CHECK(stage.mountSpell == 580 && stage.rideDisplay != 0);
        }
    CHECK(ridden == mountFamilies);
    // The wolf grows pup -> dire wolf -> war wolf, ridden with the Timber Wolf's spells.
    CompanionSpecies const& wolf = Species(6);
    CHECK(wolf.stageCount == 3 && wolf.stages[1].creature == 565 && wolf.stages[2].creature == 358);
    CHECK(RideSpell(wolf.stages[2], 60) == 580 && RideSpell(wolf.stages[2], 100) == 23250);
    CHECK(RideSpell(wolf.stages[2], 0) == 0 && RideSpell(wolf.stages[0], 60) == 0);
}

TEST(companion_riding_follows_vanillas_level_and_skill_rules)
{
    CHECK(MountSpeed(39, 300) == 0);
    CHECK(MountSpeed(40, 74) == 0);
    CHECK(MountSpeed(40, 75) == 60);
    CHECK(MountSpeed(59, 150) == 60);
    CHECK(MountSpeed(60, 149) == 60);
    CHECK(MountSpeed(60, 150) == 100);

    CompanionSpecies const& wolf = Species(6);
    CompanionState s;
    s.species = 6;
    s.level = 45;
    RideContext ctx;
    ctx.ownerLevel = 45;
    ctx.ridingSkill = 75;
    ctx.summoned = true;
    CHECK(!IsRideable(wolf, s));
    CHECK(CanRide(wolf, s, ctx) == RideBlock::NotRideable);
    s.stage = 2;
    CHECK(IsRideable(wolf, s));
    CHECK(CanRide(wolf, s, ctx) == RideBlock::None);
    RideContext c = ctx;
    c.summoned = false;
    CHECK(CanRide(wolf, s, c) == RideBlock::NotSummoned);
    c = ctx;
    c.inCombat = true;
    CHECK(CanRide(wolf, s, c) == RideBlock::InCombat);
    c = ctx;
    c.inDuel = true;
    CHECK(CanRide(wolf, s, c) == RideBlock::InDuel);
    c = ctx;
    c.ownerLevel = 39;
    CHECK(CanRide(wolf, s, c) == RideBlock::LevelTooLow);
    c = ctx;
    c.ridingSkill = 0;
    CHECK(CanRide(wolf, s, c) == RideBlock::NoSkill);
    // Worn down, it carries nobody until it has healed.
    c = ctx;
    c.healthShare = kRideMinHealth - 0.01f;
    CHECK(CanRide(wolf, s, c) == RideBlock::Worn);
    c.healthShare = kRideMinHealth;
    CHECK(CanRide(wolf, s, c) == RideBlock::None);
    // No mounts in a dungeon or indoors, even a companion one.
    c = ctx;
    c.mountAllowed = false;
    CHECK(CanRide(wolf, s, c) == RideBlock::NotHere);
    CHECK(RideFlags(wolf, s, c, false) == BAR_RIDEABLE);

    // The bar's riding bits.
    CHECK(RideFlags(wolf, s, ctx, false) == (BAR_RIDEABLE | BAR_CAN_RIDE));
    CHECK(RideFlags(wolf, s, RideContext{}, false) == BAR_RIDEABLE);
    CHECK(RideFlags(wolf, s, RideContext{}, true) == (BAR_RIDEABLE | BAR_CAN_RIDE | BAR_RIDING));
    s.stage = 1;
    CHECK(RideFlags(wolf, s, ctx, false) == 0);

    Request r;
    CHECK(DecodeRequest(EncodeRequest({kOpRide, 0, false}), r) && r.op == kOpRide);
    CHECK(EncodeRequest({kOpRide, 0, false}) == std::vector<uint8_t>({kOpRide}));
    CHECK(!DecodeRequest({kOpRide, 1}, r));
}

TEST(companion_mount_items_start_at_the_rideable_stage)
{
    auto const& all = StarterSpecies();
    auto const& items = StarterItems();
    CHECK(items.size() >= 8);
    std::set<uint32_t> seen;
    for (ItemStart const& i : items)
    {
        CHECK(seen.insert(i.item).second);
        CHECK(IsCompanionItem(i.item));
        CHECK(!FindByItem(all, i.item));        // not any species' own item
        CompanionState s;
        CompanionSpecies const* sp = FreshState(all, items, i.item, s);
        CHECK(sp && sp->id == i.species);
        if (!sp)
            continue;
        CHECK(s.level == 40 && s.stage == i.stage && s.xp == 0);
        CHECK(IsRideable(*sp, s));
        CHECK(NextRule(*sp, s) == nullptr);
    }
    // A species' own item starts at the beginning; anything else is no companion.
    CompanionState s;
    CompanionSpecies const* pup = FreshState(all, items, 90006, s);
    CHECK(pup && pup->id == 6 && s.level == 1 && s.stage == 0);
    CHECK(FreshState(all, items, 90099, s) == nullptr);
    CHECK(FreshState(all, items, 12345, s) == nullptr);
}

TEST(companion_item_seed_carries_species_stage_and_level)
{
    // The item's suffix factor (ITEM_FIELD_PROPERTY_SEED) shows a buyer what the companion is:
    // level 6 bits, stage 2, species 8, three trait ids 5 bits each, the bred bit.
    CompanionState s;
    s.species = 6;
    s.stage = 2;
    s.level = 43;
    uint32_t const seed = ItemSeed(s);
    CHECK(seed == (43u | 2u << 6 | 6u << 8));
    CompanionState back;
    CHECK(FromItemSeed(seed, back));
    CHECK(back.species == 6 && back.stage == 2 && back.level == 43 && back.traits == 0 && back.generation == 0);
    // Traits, lowest id first, and the bred bit.
    s.species = 164;
    s.traits = 1u << 1 | 1u << 5 | 1u << 29;    // Swift (2), Venomous (6), Primal (30)
    s.generation = 3;
    uint32_t const full = ItemSeed(s);
    CHECK(full == (43u | 2u << 6 | 164u << 8 | 2u << 16 | 6u << 21 | 30u << 26 | 1u << 31));
    CHECK(FromItemSeed(full, back));
    CHECK(back.species == 164 && back.traits == s.traits && back.generation == 1 && back.level == 43);
    // A plain item's 0 (and a level 0, species 0, stage 3 or a trait past 30) is no companion.
    CHECK(!FromItemSeed(0, back));
    CHECK(!FromItemSeed(6u << 8 | 1u << 6, back));
    CHECK(!FromItemSeed(1u << 6 | 5u, back));
    CHECK(!FromItemSeed(6u << 8 | 3u << 6 | 5u, back));
    CHECK(!FromItemSeed(6u << 8 | 5u | 31u << 16, back));
    // Levels past 60 are not a companion's.
    CHECK(!FromItemSeed(6u << 8 | 61u, back));
}

TEST(companion_item_range_leaves_the_construction_items_alone)
{
    // 91001-91099 are Construction's materials, parts and Hyjal deeds (03_construction.sql).
    CHECK(!IsCompanionItem(91001));
    CHECK(!IsCompanionItem(91030));
    CHECK(!IsCompanionItem(91099));
    CHECK(IsCompanionItem(91101));
    CHECK(IsCompanionItem(90006));
}

TEST(a_companion_follows_like_a_pet_and_catches_up)
{
    // Never on its owner: a step and a half of its own size, past a pet's two yards.
    CHECK(FollowDistance(0.f) > 2.f);
    CHECK(FollowDistance(2.f) > FollowDistance(0.5f) + 2.f);
    // Behind and to the side, not in front nor exactly behind.
    CHECK(kFollowAngle > 1.6f && kFollowAngle < 3.1f);
    // Far behind out of a fight: catches up. In a fight it is left to its fight.
    CHECK(!ShouldCatchUp(10.f, false));
    CHECK(ShouldCatchUp(kCatchUpYards + 1.f, false));
    CHECK(!ShouldCatchUp(kCatchUpYards + 1.f, true));
}

TEST(companion_follows_at_an_offset_behind_beside_never_on_its_owner)
{
    using namespace Fusion::Companions;
    // Never on its owner, even with no navmesh to trace the walk (mmaps off).
    CHECK(FollowFallbackOf(true) == FollowFallback::FollowSpot);
    CHECK(FollowFallbackOf(false) == FollowFallback::OwnerPosition);
    for (float r : { 0.f, 0.3f, 1.f, 3.f })
    {
        float const d = FollowDistance(r);
        CHECK(d >= 2.f);               // at least PET_FOLLOW_DIST
        CHECK(d > r + 1.f);            // clear of its own bulk
        // Owner at the origin facing +x: the spot is behind (x < 0) and to one side (y != 0).
        float x, y;
        FollowSpot(0.f, 0.f, 0.f, d, kFollowAngle, x, y);
        CHECK(x < 0.f);
        CHECK(std::fabs(y) > 0.25f * d);
        CHECK(std::fabs(std::sqrt(x * x + y * y) - d) < 0.001f);
    }
    CHECK(kFollowAngle > 1.5707963f && kFollowAngle < 3.1415926f);
}

TEST(mounting_is_a_cast_the_companion_runs_in_front_for)
{
    // Near: the shortest cast; farther: as long as the run takes and its settle, up to the most.
    CHECK(MountCastMs(0.f, 7.f) == kMountCastMinMs);
    CHECK(MountCastMs(14.f, 7.f) == 2000 + kMountCastSettleMs);
    CHECK(MountCastMs(200.f, 7.f) == kMountCastMaxMs);
    CHECK(MountCastMs(14.f, 0.f) == 2000 + kMountCastSettleMs);    // no speed known: a run's
    // Far off, it runs faster to arrive as the bar ends; never slower than its own run.
    CHECK_NEAR(MountRunSpeed(5.f, 7.f, 1500), 7.f, 1e-5f);
    float const fast = MountRunSpeed(64.f, 7.f, kMountCastMaxMs);
    CHECK_NEAR(fast, 64.f / (float(kMountCastMaxMs - kMountCastSettleMs) / 1000.f), 1e-3f);
    // The spot: straight ahead of its owner, past its own size.
    Vec3 const s = MountSpot({10.f, 20.f, 5.f}, 0.f, 1.f);
    CHECK_NEAR(s.x, 10.f + kMountSpotYards + 1.f, 1e-5f);
    CHECK_NEAR(s.y, 20.f, 1e-5f);
    CHECK_NEAR(s.z, 5.f, 1e-5f);
    Vec3 const n = MountSpot({0.f, 0.f, 0.f}, 3.14159265f / 2.f, 0.f);
    CHECK_NEAR(n.x, 0.f, 1e-4f);
    CHECK_NEAR(n.y, kMountSpotYards, 1e-4f);
    // Standing still holds it; a step breaks it.
    CHECK(MountCastHolds({0.f, 0.f, 0.f}, {0.3f, 0.f, 0.f}));
    CHECK(!MountCastHolds({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}));
    // Its wire: [7, phase, ms].
    std::vector<uint8_t> const w = EncodeMountCast(MountCastPhase::Start, 2300);
    CHECK(w.size() == 6);
    CHECK(w[0] == kOpMountCast && w[1] == 0);
    CHECK(w[2] == (2300 & 0xFF) && w[3] == (2300 >> 8));
}
