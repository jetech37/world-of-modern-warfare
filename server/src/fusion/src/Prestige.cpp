/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Prestige.h"

#include <algorithm>

namespace Fusion::Prestige
{
    namespace
    {
        struct ProfessionInfo
        {
            uint16_t line;
            char const* name;           // the skill
            char const* noun;           // who has it, for titles
        };

        // Vanilla's skill lines (SkillLine.dbc): primary, then secondary.
        constexpr ProfessionInfo kProfessions[] = {
            {164, "Blacksmithing", "Blacksmith"},
            {165, "Leatherworking", "Leatherworker"},
            {171, "Alchemy", "Alchemist"},
            {182, "Herbalism", "Herbalist"},
            {186, "Mining", "Miner"},
            {197, "Tailoring", "Tailor"},
            {202, "Engineering", "Engineer"},
            {333, "Enchanting", "Enchanter"},
            {393, "Skinning", "Skinner"},
            {129, "First Aid", "Field Medic"},
            {185, "Cooking", "Cook"},
            {356, "Fishing", "Angler"},
        };

        int ProfessionIndex(uint16_t line)
        {
            for (size_t i = 0; i < sizeof(kProfessions) / sizeof(kProfessions[0]); ++i)
                if (kProfessions[i].line == line)
                    return int(i);
            return -1;
        }

        constexpr uint16_t kLevelTitleBase = 0;         // + rank (1-10)
        constexpr uint16_t kGunTitleBase = 10;          // + rank
        constexpr uint16_t kProfessionTitleBase = 100;  // + index * 10 + rank - 1
        constexpr uint16_t kMilestoneTitleBase = 200;   // + milestone id

        // MW2's ranks, worn before the name.
        constexpr char const* kLevelTitles[kMaxRank] = {"Private", "Corporal", "Sergeant", "Staff Sergeant",
            "Lieutenant", "Captain", "Major", "Lieutenant Colonel", "Colonel", "Commander"};
        // Worn after the name.
        constexpr char const* kGunTitles[kMaxRank] = {"the Rifleman", "the Marksman", "the Sharpshooter",
            "the Gunslinger", "the Armorer", "the Gunsmith", "the Arms Master", "the Weaponmaster",
            "the Warmonger", "the Arsenal"};
        // A profession's titles by rank: the adjective before its noun.
        constexpr char const* kProfessionGrades[kMaxRank] = {"Seasoned", "Seasoned", "Expert", "Expert",
            "Master", "Master", "Grand Master", "Grand Master", "Legendary", "Legendary"};

        void PutU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
        void PutU16(std::vector<uint8_t>& b, uint16_t v)
        {
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }
        void PutStr8(std::vector<uint8_t>& b, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            b.push_back(uint8_t(n));
            b.insert(b.end(), s.begin(), s.begin() + n);
        }
        void PutStr16(std::vector<uint8_t>& b, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 0xFFFF);
            PutU16(b, uint16_t(n));
            b.insert(b.end(), s.begin(), s.begin() + n);
        }

        struct In
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            uint8_t U8()
            {
                if (at + 1 > b.size()) { ok = false; return 0; }
                return b[at++];
            }
            uint16_t U16()
            {
                uint16_t const lo = U8();
                return uint16_t(lo | uint16_t(U8()) << 8);
            }
            uint32_t U32()
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(U8()) << (8 * i);
                return v;
            }
        };

        std::string Copper(uint32_t c)
        {
            std::string s;
            if (c >= 10000)
                s += std::to_string(c / 10000) + " gold";
            if (c % 10000 >= 100)
                s += (s.empty() ? "" : " ") + std::to_string((c % 10000) / 100) + " silver";
            return s.empty() ? std::to_string(c) + " copper" : s;
        }
    }

    bool IsProfession(uint16_t skillLine) { return ProfessionIndex(skillLine) >= 0; }

    char const* ProfessionName(uint16_t skillLine)
    {
        int const i = ProfessionIndex(skillLine);
        return i < 0 ? "a profession" : kProfessions[i].name;
    }

    std::vector<uint16_t> const& Professions()
    {
        static std::vector<uint16_t> const lines = [] {
            std::vector<uint16_t> v;
            for (ProfessionInfo const& p : kProfessions)
                v.push_back(p.line);
            return v;
        }();
        return lines;
    }

    char const* TracerName(Tracer t)
    {
        switch (t)
        {
            case Tracer::Default: return "Standard";
            case Tracer::Red: return "Red";
            case Tracer::Green: return "Green";
            case Tracer::Blue: return "Blue";
            case Tracer::Violet: return "Violet";
            case Tracer::Gold: return "Gold";
            case Tracer::White: return "White Phosphor";
            default: return "Standard";
        }
    }

    Rgb TracerColour(Tracer t)
    {
        switch (t)
        {
            case Tracer::Red: return {255, 60, 40};
            case Tracer::Green: return {70, 255, 90};
            case Tracer::Blue: return {70, 140, 255};
            case Tracer::Violet: return {190, 80, 255};
            case Tracer::Gold: return {255, 200, 60};
            case Tracer::White: return {255, 255, 255};
            default: return {199, 194, 179};   // the client's own grey glint
        }
    }

    uint8_t State::ProfessionRank(uint16_t line) const
    {
        auto it = professions.find(line);
        return it == professions.end() ? 0 : it->second;
    }

    uint8_t State::BestProfessionRank() const
    {
        uint8_t best = 0;
        for (auto const& kv : professions)
            best = std::max(best, kv.second);
        return best;
    }

    bool GunsMaxed(AchievementBook const& book, std::set<uint32_t> const& earned)
    {
        bool any = false;
        for (Achievement const& a : book.All())
        {
            if (a.category != AchievementCategory::Weapons && a.category != AchievementCategory::Attachments)
                continue;
            any = true;
            if (!earned.count(a.id))
                return false;
        }
        return any;
    }

    std::set<uint32_t> KeptAfterGunPrestige(AchievementBook const& book, std::set<uint32_t> const& earned)
    {
        std::set<uint32_t> kept;
        for (uint32_t id : earned)
        {
            Achievement const* a = book.Find(id);
            // An id the book no longer has is kept as it was: retuning is not a prestige.
            if (!a || a->category == AchievementCategory::Feats)
                kept.insert(id);
        }
        return kept;
    }

    char const* RefusalText(Refusal r)
    {
        switch (r)
        {
            case Refusal::None: return "Ready to prestige.";
            case Refusal::NotMaxed: return "Not maxed yet.";
            case Refusal::MaxRank: return "Already at the highest prestige.";
            case Refusal::NotAProfession: return "That is not a profession.";
            case Refusal::Busy: return "Not now: leave combat, battlegrounds and matches first.";
            case Refusal::NothingAsked: return "That question has expired; ask again.";
        }
        return "";
    }

    uint8_t RankOf(State const& s, Track track, uint16_t key)
    {
        switch (track)
        {
            case Track::Level: return s.level;
            case Track::Guns: return s.guns;
            case Track::Profession: return s.ProfessionRank(key);
        }
        return 0;
    }

    Refusal CanPrestige(State const& s, Standing const& now, Track track, uint16_t key)
    {
        if (track == Track::Profession && !IsProfession(key))
            return Refusal::NotAProfession;
        if (RankOf(s, track, key) >= kMaxRank)
            return Refusal::MaxRank;
        switch (track)
        {
            case Track::Level:
                return now.level >= kMaxLevel ? Refusal::None : Refusal::NotMaxed;
            case Track::Guns:
                return now.gunsMaxed ? Refusal::None : Refusal::NotMaxed;
            case Track::Profession:
            {
                auto it = now.skills.find(key);
                return it != now.skills.end() && it->second >= kMaxSkill ? Refusal::None : Refusal::NotMaxed;
            }
        }
        return Refusal::NotMaxed;
    }

    Reward RankReward(Track track, uint16_t key, uint8_t rank)
    {
        Reward r;
        if (rank < 1 || rank > kMaxRank)
            return r;
        switch (track)
        {
            case Track::Level:
                r.tokens = 5;
                r.title = uint16_t(kLevelTitleBase + rank);
                if (rank == 2) r.tracer = Tracer::Red;
                if (rank == 5) r.tracer = Tracer::Violet;
                if (rank == 10) r.tracer = Tracer::Gold;
                break;
            case Track::Guns:
                r.tokens = 5;
                r.title = uint16_t(kGunTitleBase + rank);
                // MW2's challenge camos, now on every gun: Red Tiger, Blue Tiger, Fall.
                if (rank == 1) r.camo = 6;
                if (rank == 3) r.camo = 7;
                if (rank == 6) r.camo = 8;
                if (rank == 4) r.tracer = Tracer::Green;
                if (rank == 8) r.tracer = Tracer::Blue;
                break;
            case Track::Profession:
            {
                int const i = ProfessionIndex(key);
                if (i < 0)
                    return Reward();
                r.tokens = 3;
                r.title = uint16_t(kProfessionTitleBase + i * 10 + rank - 1);
                break;
            }
        }
        return r;
    }

    Reward Apply(State& s, Track track, uint16_t key)
    {
        uint8_t rank = 0;
        switch (track)
        {
            case Track::Level: rank = s.level = uint8_t(std::min<int>(s.level + 1, kMaxRank)); break;
            case Track::Guns: rank = s.guns = uint8_t(std::min<int>(s.guns + 1, kMaxRank)); break;
            case Track::Profession:
                rank = s.professions[key] = uint8_t(std::min<int>(s.ProfessionRank(key) + 1, kMaxRank));
                break;
        }
        Reward const r = RankReward(track, key, rank);
        s.tokens += r.tokens;
        if (r.tracer != Tracer::Default)
            s.cosmetics |= 1u << uint8_t(r.tracer);
        return r;
    }

    std::string TitleText(uint16_t id)
    {
        if (id > kLevelTitleBase && id <= kLevelTitleBase + kMaxRank)
            return kLevelTitles[id - kLevelTitleBase - 1];
        if (id > kGunTitleBase && id <= kGunTitleBase + kMaxRank)
            return kGunTitles[id - kGunTitleBase - 1];
        if (id >= kProfessionTitleBase && id < kMilestoneTitleBase)
        {
            uint16_t const i = uint16_t((id - kProfessionTitleBase) / 10), rank = uint16_t((id - kProfessionTitleBase) % 10);
            if (i < sizeof(kProfessions) / sizeof(kProfessions[0]))
                return std::string(kProfessionGrades[rank]) + " " + kProfessions[i].noun;
            return "";
        }
        if (id >= kMilestoneTitleBase)
            if (Milestone const* m = FindMilestone(uint8_t(id - kMilestoneTitleBase)))
                return m->name;
        return "";
    }

    bool TitleIsPrefix(uint16_t id)
    {
        return id > kLevelTitleBase && id <= kLevelTitleBase + kMaxRank;
    }

    std::string Titled(std::string const& name, uint16_t id)
    {
        std::string const t = TitleText(id);
        if (t.empty())
            return name;
        if (TitleIsPrefix(id))
            return t + " " + name;
        // A profession title reads "Thrall, Master Engineer"; the others "Thrall the Gunsmith".
        if (id >= kProfessionTitleBase && id < kMilestoneTitleBase)
            return name + ", " + t;
        return name + " " + t;
    }

    std::vector<uint16_t> TitlesHeld(State const& s)
    {
        std::vector<uint16_t> v;
        for (uint8_t r = 1; r <= s.level && r <= kMaxRank; ++r)
            v.push_back(uint16_t(kLevelTitleBase + r));
        for (uint8_t r = 1; r <= s.guns && r <= kMaxRank; ++r)
            v.push_back(uint16_t(kGunTitleBase + r));
        for (auto const& kv : s.professions)
        {
            int const i = ProfessionIndex(kv.first);
            if (i < 0)
                continue;
            // One title a grade: the highest rank of each grade the profession reached.
            for (uint8_t r = 1; r <= kv.second && r <= kMaxRank; ++r)
                if (r == kv.second || kProfessionGrades[r - 1] != kProfessionGrades[r])
                    v.push_back(uint16_t(kProfessionTitleBase + i * 10 + r - 1));
        }
        for (Milestone const& m : Milestones())
            if ((s.milestones >> m.id) & 1u)
                v.push_back(uint16_t(kMilestoneTitleBase + m.id));
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    }

    bool HoldsTitle(State const& s, uint16_t id)
    {
        if (id == 0)
            return true;
        std::vector<uint16_t> const held = TitlesHeld(s);
        if (std::find(held.begin(), held.end(), id) != held.end())
            return true;
        // Any rank of a grade reached counts (TitlesHeld lists one a grade).
        if (id >= kProfessionTitleBase && id < kMilestoneTitleBase)
        {
            uint16_t const i = uint16_t((id - kProfessionTitleBase) / 10);
            if (i < sizeof(kProfessions) / sizeof(kProfessions[0]))
                return s.ProfessionRank(kProfessions[i].line) > (id - kProfessionTitleBase) % 10;
        }
        return false;
    }

    std::vector<uint8_t> PrestigeCamos(State const& s)
    {
        std::vector<uint8_t> camos;
        for (uint8_t r = 1; r <= s.guns && r <= kMaxRank; ++r)
            if (uint8_t const c = RankReward(Track::Guns, 0, r).camo)
                camos.push_back(c);
        for (Milestone const& m : Milestones())
            if (((s.milestones >> m.id) & 1u) && m.camo)
                camos.push_back(m.camo);
        for (uint8_t c = 1; c < 32 - kCamoCosmeticShift; ++c)
            if ((s.cosmetics >> (kCamoCosmeticShift + c)) & 1u)
                camos.push_back(c);
        std::sort(camos.begin(), camos.end());
        camos.erase(std::unique(camos.begin(), camos.end()), camos.end());
        return camos;
    }

    void GrantPrestigeUnlocks(Catalog const& catalog, State const& s, UnlockSet& unlocks)
    {
        std::vector<uint8_t> const camos = PrestigeCamos(s);
        if (camos.empty() || unlocks.IsEverything())
            return;
        for (WeaponProfile const& w : catalog.Weapons().All())
        {
            if (!w.camos)
                continue;
            for (uint8_t c : camos)
                if (catalog.FindCamo(c))
                    unlocks.Grant(Unlock::Camo(w.id, c));
        }
    }

    std::vector<Milestone> const& Milestones()
    {
        // Companion items from W5's range: a whelpling egg, a zephyr, a fel stone, a frostsaber.
        static std::vector<Milestone> const list = {
            {0, 0, "the Triple Threat", uint16_t(kMilestoneTitleBase + 0), 1000000, 10, 90001, Tracer::Default, 0},
            {1, 1, "the Unbroken", uint16_t(kMilestoneTitleBase + 1), 2500000, 15, 90022, Tracer::White, 0},
            {2, 3, "the Paragon", uint16_t(kMilestoneTitleBase + 2), 5000000, 25, 90024, Tracer::Default, 5},
            {3, 5, "Legend of Azeroth", uint16_t(kMilestoneTitleBase + 3), 10000000, 40, 90062, Tracer::Default, 4},
            {4, 10, "the Eternal", uint16_t(kMilestoneTitleBase + 4), 25000000, 100, 90060, Tracer::Default, 3},
        };
        return list;
    }

    Milestone const* FindMilestone(uint8_t id)
    {
        for (Milestone const& m : Milestones())
            if (m.id == id)
                return &m;
        return nullptr;
    }

    bool AllMaxed(Standing const& now)
    {
        if (now.level < kMaxLevel || !now.gunsMaxed)
            return false;
        for (auto const& kv : now.skills)
            if (IsProfession(kv.first) && kv.second >= kMaxSkill)
                return true;
        return false;
    }

    std::vector<uint8_t> MilestonesDue(State const& s, Standing const& now)
    {
        std::vector<uint8_t> due;
        if (!AllMaxed(now))
            return due;
        uint8_t const least = std::min({s.level, s.guns, s.BestProfessionRank()});
        for (Milestone const& m : Milestones())
            if (least >= m.minRank && !((s.milestones >> m.id) & 1u)
                && !((kAccountMilestones & s.accountMilestones) >> m.id & 1u))
                due.push_back(m.id);
        return due;
    }

    bool ClaimMilestone(State& s, uint8_t id)
    {
        Milestone const* m = FindMilestone(id);
        if (!m || ((s.milestones >> id) & 1u) || ((kAccountMilestones & s.accountMilestones) >> id & 1u))
            return false;
        s.milestones |= 1u << id;
        s.accountMilestones |= kAccountMilestones & (1u << id);
        s.tokens += m->tokens;
        if (m->tracer != Tracer::Default)
            s.cosmetics |= 1u << uint8_t(m->tracer);
        return true;
    }

    float KillXpMultiplier(uint8_t levelRank, uint32_t level)
    {
        if (levelRank == 0 || level >= kMaxLevel)
            return 1.f;
        return std::min(3.f, 1.f + 0.5f * float(levelRank));
    }

    std::vector<Good> const& Shop()
    {
        // Items: the field gear (FieldGear.h's 93200-93399), a W5 companion carrier.
        static std::vector<Good> const goods = {
            {1, GoodKind::Item, "Focus Stims (5)", 2, 93260, 5, Tracer::Default, 0},
            {2, GoodKind::Item, "Second Wind Stims (5)", 2, 93261, 5, Tracer::Default, 0},
            {3, GoodKind::Item, "Adrenaline Stims (5)", 2, 93262, 5, Tracer::Default, 0},
            {4, GoodKind::Item, "Spare Thorium Grenades (3)", 3, 93200, 3, Tracer::Default, 0},
            {5, GoodKind::Item, "Spare Arcane Flash Bombs (3)", 3, 93205, 3, Tracer::Default, 0},
            {6, GoodKind::Item, "Prestige Ghillie Mantle", 8, 93243, 1, Tracer::Default, 0},
            {7, GoodKind::Item, "Prestige Bandolier", 8, 93223, 1, Tracer::Default, 0},
            {8, GoodKind::Item, "Wolf Pup Carrier (companion)", 12, 90006, 1, Tracer::Default, 0},
            {20, GoodKind::TracerColour, "Red tracers", 4, 0, 1, Tracer::Red, 0},
            {21, GoodKind::TracerColour, "Green tracers", 4, 0, 1, Tracer::Green, 0},
            {22, GoodKind::TracerColour, "Blue tracers", 4, 0, 1, Tracer::Blue, 0},
            {23, GoodKind::TracerColour, "Violet tracers", 6, 0, 1, Tracer::Violet, 0},
            {30, GoodKind::Camo, "Arctic camo on every gun", 10, 0, 1, Tracer::Default, 3},
            {31, GoodKind::Camo, "Digital camo on every gun", 10, 0, 1, Tracer::Default, 4},
            {32, GoodKind::Camo, "Urban camo on every gun", 10, 0, 1, Tracer::Default, 5},
        };
        return goods;
    }

    Good const* FindGood(uint8_t id)
    {
        for (Good const& g : Shop())
            if (g.id == id)
                return &g;
        return nullptr;
    }

    bool Owns(State const& s, Good const& g)
    {
        switch (g.kind)
        {
            case GoodKind::TracerColour: return s.HasTracer(g.tracer);
            case GoodKind::Camo:
            {
                std::vector<uint8_t> const camos = PrestigeCamos(s);
                return std::find(camos.begin(), camos.end(), g.camo) != camos.end();
            }
            default: return false;
        }
    }

    BuyResult Buy(State& s, uint8_t goodId)
    {
        Good const* g = FindGood(goodId);
        if (!g)
            return BuyResult::Unknown;
        if (Owns(s, *g))
            return BuyResult::AlreadyOwned;
        if (s.tokens < g->price)
            return BuyResult::NotEnoughTokens;
        s.tokens -= g->price;
        if (g->kind == GoodKind::TracerColour)
            s.cosmetics |= 1u << uint8_t(g->tracer);
        else if (g->kind == GoodKind::Camo)
            s.cosmetics |= 1u << (kCamoCosmeticShift + g->camo);
        return BuyResult::Ok;
    }

    void Ask(Pending& p, Track track, uint16_t key, uint32_t nonce, uint32_t nowMs)
    {
        p.track = track;
        p.key = key;
        p.nonce = nonce ? nonce : 1;
        p.untilMs = nowMs + kConfirmMs;
        p.active = true;
    }

    bool Confirm(Pending& p, uint32_t nonce, uint32_t nowMs, Track& track, uint16_t& key)
    {
        if (!p.active || nonce != p.nonce || int32_t(nowMs - p.untilMs) > 0)
        {
            if (p.active && int32_t(nowMs - p.untilMs) > 0)
                p.active = false;
            return false;
        }
        p.active = false;
        track = p.track;
        key = p.key;
        return true;
    }

    std::string WhatItCosts(Track track, uint16_t key)
    {
        switch (track)
        {
            case Track::Level:
                return "Prestige your level: you go back to level 1 with no experience. You keep your items "
                       "(gear you cannot wear yet goes to your bags), gold, reputation, professions, mounts and "
                       "kits. Quests you have done stay done; kills give more experience on the way back up.";
            case Track::Guns:
                return "Prestige your guns: every gun, attachment, camo, perk, piece of equipment and killstreak "
                       "you earned goes back to the starter kit, on every character of this account, and the "
                       "achievement counters start again. You keep your Feats, their titles and every prestige "
                       "camo.";
            case Track::Profession:
                return std::string("Prestige ") + ProfessionName(key) + ": its skill goes back to 1. You keep its "
                       "recipes and its trained rank; recipes come back into use as the skill climbs again.";
        }
        return "";
    }

    std::vector<uint8_t> EncodeState(View const& v)
    {
        State const& s = v.state;
        std::vector<uint8_t> b{kVersion, uint8_t(Op::StateMsg)};
        PutU8(b, s.level);
        PutU8(b, s.guns);
        PutU32(b, s.tokens);
        PutU32(b, s.milestones);
        PutU32(b, s.cosmetics);
        PutU16(b, s.title);
        PutU8(b, uint8_t(s.tracer));
        PutU8(b, uint8_t(std::min<uint32_t>(v.standing.level, 255)));
        PutU8(b, v.standing.gunsMaxed ? 1 : 0);
        PutU8(b, v.busy ? 1 : 0);
        auto refusal = [&](Track t, uint16_t key) {
            Refusal const r = CanPrestige(s, v.standing, t, key);
            return uint8_t(v.busy && r == Refusal::None ? Refusal::Busy : r);
        };
        PutU8(b, refusal(Track::Level, 0));
        PutU8(b, refusal(Track::Guns, 0));
        // The professions known, and any with a rank.
        std::vector<uint16_t> lines;
        for (uint16_t line : Professions())
            if (v.standing.skills.count(line) || s.ProfessionRank(line))
                lines.push_back(line);
        PutU8(b, uint8_t(lines.size()));
        for (uint16_t line : lines)
        {
            auto it = v.standing.skills.find(line);
            PutU16(b, line);
            PutU16(b, it == v.standing.skills.end() ? 0 : it->second);
            PutU8(b, s.ProfessionRank(line));
            PutU8(b, refusal(Track::Profession, line));
            PutStr8(b, ProfessionName(line));
        }
        std::vector<uint16_t> const titles = TitlesHeld(s);
        PutU8(b, uint8_t(std::min<size_t>(titles.size(), 255)));
        for (size_t i = 0; i < titles.size() && i < 255; ++i)
        {
            PutU16(b, titles[i]);
            PutStr8(b, TitleText(titles[i]));
        }
        PutU8(b, uint8_t(Milestones().size()));
        for (Milestone const& m : Milestones())
        {
            PutU8(b, m.id);
            PutU8(b, m.minRank);
            PutU8(b, (s.milestones >> m.id) & 1u ? 1 : 0);
            PutStr8(b, m.name);
            std::string reward = "Title \"" + std::string(m.name) + "\", " + Copper(m.goldCopper) + ", "
                + std::to_string(m.tokens) + " tokens, a companion";
            if (m.tracer != Tracer::Default)
                reward += std::string(", ") + TracerName(m.tracer) + " tracers";
            if (m.camo)
                reward += ", a camo on every gun";
            std::string const when = m.minRank == 0
                ? "Level 60, every gun and attachment unlocked and a profession at 300, all at once."
                : "All three maxed together, every track prestiged " + std::to_string(m.minRank) + " times or more.";
            PutStr16(b, when + " " + reward + ".");
        }
        return b;
    }

    std::vector<uint8_t> EncodePrompt(uint32_t nonce, Track track, uint16_t key, std::string const& text)
    {
        std::vector<uint8_t> b{kVersion, uint8_t(Op::Prompt)};
        PutU32(b, nonce);
        PutU8(b, uint8_t(track));
        PutU16(b, key);
        PutStr16(b, text);
        return b;
    }

    std::vector<uint8_t> EncodeResult(bool ok, std::string const& text)
    {
        std::vector<uint8_t> b{kVersion, uint8_t(Op::Result), uint8_t(ok ? 1 : 0)};
        PutStr16(b, text);
        return b;
    }

    std::vector<uint8_t> EncodeEquipment(uint8_t slot, uint8_t count)
    {
        return {kVersion, uint8_t(Op::Equipment), slot, count};
    }

    std::vector<uint8_t> EncodeStim(uint8_t kind, uint32_t ms)
    {
        std::vector<uint8_t> b{kVersion, uint8_t(Op::Stim), kind};
        PutU32(b, ms);
        return b;
    }

    std::vector<uint8_t> EncodeLook(Tracer t)
    {
        Rgb const c = TracerColour(t);
        return {kVersion, uint8_t(Op::Look), uint8_t(t), c.r, c.g, c.b};
    }

    std::vector<uint8_t> EncodeShop(State const& s)
    {
        std::vector<uint8_t> b{kVersion, uint8_t(Op::ShopList)};
        std::vector<Good> const& goods = Shop();
        size_t const n = std::min<size_t>(goods.size(), 255);
        PutU8(b, uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            PutU8(b, goods[i].id);
            PutU32(b, goods[i].price);
            PutU8(b, Owns(s, goods[i]) ? 1 : 0);
            PutStr8(b, goods[i].name);
        }
        return b;
    }

    std::string BuyText(BuyResult r, Good const& g, uint32_t tokensLeft)
    {
        switch (r)
        {
            case BuyResult::Ok:
                return std::string("Bought ") + g.name + ". " + std::to_string(tokensLeft) + " tokens left.";
            case BuyResult::AlreadyOwned:
                return std::string("You already own ") + g.name + ".";
            case BuyResult::NotEnoughTokens:
                return std::string(g.name) + " costs " + std::to_string(g.price) + " prestige tokens; you have "
                    + std::to_string(tokensLeft) + ".";
            default:
                return "That is not for sale.";
        }
    }

    bool DecodeClient(std::vector<uint8_t> const& body, ClientMessage& out)
    {
        In in{body};
        if (in.U8() != kVersion || !in.ok)
            return false;
        out = ClientMessage();
        out.op = Op(in.U8());
        switch (out.op)
        {
            case Op::Request:
            case Op::Cancel:
                break;
            case Op::Ask:
            {
                uint8_t const t = in.U8();
                if (t < uint8_t(Track::Level) || t > uint8_t(Track::Profession))
                    return false;
                out.track = Track(t);
                out.key = in.U16();
                break;
            }
            case Op::Confirm:
                out.nonce = in.U32();
                break;
            case Op::SetTitle:
                out.title = in.U16();
                break;
            case Op::SetTracer:
            {
                uint8_t const t = in.U8();
                if (t >= uint8_t(Tracer::Count))
                    return false;
                out.tracer = Tracer(t);
                break;
            }
            case Op::Buy:
                out.good = in.U8();
                break;
            default:
                return false;
        }
        return in.ok;
    }

    std::vector<uint8_t> EncodeClient(ClientMessage const& m)
    {
        std::vector<uint8_t> b{kVersion, uint8_t(m.op)};
        switch (m.op)
        {
            case Op::Ask:
                PutU8(b, uint8_t(m.track));
                PutU16(b, m.key);
                break;
            case Op::Confirm: PutU32(b, m.nonce); break;
            case Op::SetTitle: PutU16(b, m.title); break;
            case Op::SetTracer: PutU8(b, uint8_t(m.tracer)); break;
            case Op::Buy: PutU8(b, m.good); break;
            default: break;
        }
        return b;
    }
}
