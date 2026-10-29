/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionIndex.h"

#include <algorithm>
#include <cstring>

namespace Fusion::Companions::Index
{
    namespace
    {
        void Put8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
        void Put16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }
        void Put32(std::vector<uint8_t>& o, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void Put64(std::vector<uint8_t>& o, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void PutStr8(std::vector<uint8_t>& o, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            Put8(o, uint8_t(n));
            o.insert(o.end(), s.begin(), s.begin() + n);
        }

        // Reads past the end fail the whole message (ok goes false, reads give 0).
        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at;
            bool ok = true;

            bool Need(size_t n)
            {
                if (!ok || at + n > b.size())
                    ok = false;
                return ok;
            }
            uint8_t U8() { return Need(1) ? b[at++] : 0; }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t const v = uint16_t(b[at] | b[at + 1] << 8);
                at += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(b[at + i]) << (8 * i);
                at += 4;
                return v;
            }
            uint64_t U64()
            {
                if (!Need(8))
                    return 0;
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(b[at + i]) << (8 * i);
                at += 8;
                return v;
            }
            std::string Str(size_t n)
            {
                if (!Need(n))
                    return {};
                std::string s(b.begin() + long(at), b.begin() + long(at + n));
                at += n;
                return s;
            }
            std::string Str8() { return Str(U8()); }
            bool Done() const { return ok && at == b.size(); }
        };

        char const* TypePlural(CreatureType t)
        {
            switch (t)
            {
                case CreatureType::Beast: return "beasts";
                case CreatureType::Dragonkin: return "dragonkin";
                case CreatureType::Demon: return "demons";
                case CreatureType::Elemental: return "elementals";
                case CreatureType::Giant: return "giants";
                case CreatureType::Undead: return "undead";
                case CreatureType::Humanoid: return "humanoids";
                case CreatureType::Critter: return "critters";
                case CreatureType::Mechanical: return "mechanicals";
                default: return "creatures";
            }
        }
    }

    bool Record(SeenMap& seen, uint16_t species, Seen how)
    {
        if (!species || how == Seen::Unknown)
            return false;
        Seen& at = seen[species];
        if (uint8_t(how) <= uint8_t(at))
            return false;
        at = how;
        return true;
    }

    Seen Of(SeenMap const& seen, uint16_t species)
    {
        auto it = seen.find(species);
        return it == seen.end() ? Seen::Unknown : it->second;
    }

    bool Visible(SeenMap const& seen, uint16_t species, bool gm)
    {
        return gm || Of(seen, species) != Seen::Unknown;
    }

    CreatureMap MapCreatures(std::vector<CompanionSpecies> const& all)
    {
        CreatureMap m;
        for (CompanionSpecies const& sp : all)
            for (uint8_t st = 0; st < sp.stageCount && st < kMaxStages; ++st)
                if (uint32_t const c = sp.stages[st].creature)
                {
                    std::vector<uint16_t>& v = m[c];
                    if (std::find(v.begin(), v.end(), sp.id) == v.end())
                        v.push_back(sp.id);
                }
        return m;
    }

    bool Allowed(Action a, Access at, bool gm)
    {
        switch (a)
        {
            case Action::Summon:
            case Action::Evolve:
            case Action::Pick:
            case Action::Duel:
                return true;
            case Action::Draught:
                return true;
            case Action::Repick:
            case Action::Breed:
            case Action::StableCare:
                return gm || at == Access::Npc;
            case Action::GmGive:
            case Action::GmEdit:
                return gm;
        }
        return false;
    }

    uint32_t MountDisplay(CompanionSpecies const& sp, CompanionState const& s, uint32_t petDisplay,
        uint32_t creatureDisplay)
    {
        if (s.stage >= sp.stageCount || !sp.stages[s.stage].rideable)
            return 0;
        if (petDisplay)
            return petDisplay;
        // Its stage's own display (the look table): a mount line's last form is a mount model.
        if (sp.stages[s.stage].display)
            return sp.stages[s.stage].display;
        if (sp.stages[s.stage].rideDisplay)
            return sp.stages[s.stage].rideDisplay;
        return creatureDisplay;
    }

    // ── Requests ────────────────────────────────────────────────────────────────────────────

    std::vector<uint8_t> EncodeRequest(Request const& r)
    {
        std::vector<uint8_t> o{r.op};
        switch (r.op)
        {
            case kOpGmGive:
                Put16(o, r.species);
                Put8(o, r.stage);
                Put8(o, r.level);
                break;
            case kOpGmEdit:
                Put64(o, r.item);
                Put8(o, uint8_t(r.what));
                Put8(o, r.a);
                Put8(o, r.b);
                break;
            case kOpBreed:
                Put64(o, r.item);
                Put64(o, r.other);
                break;
            case kOpRepick:
                Put64(o, r.item);
                Put8(o, r.a);
                Put8(o, r.b);
                break;
            case kOpUseDraught:
                Put64(o, r.item);
                break;
            default:
                break;
        }
        return o;
    }

    bool DecodeRequest(std::vector<uint8_t> const& body, Request& out)
    {
        if (body.empty())
            return false;
        out = Request{};
        out.op = body[0];
        Reader r{body, 1};
        switch (out.op)
        {
            case kOpIndexRequest:
            case kOpCarriedRequest:
            case kOpStableRevive:
            case kOpBuyDraught:
                return body.size() == 1;
            case kOpUseDraught:
                if (body.size() != 9)
                    return false;
                out.item = r.U64();
                return true;
            case kOpGmGive:
                if (body.size() != 5)
                    return false;
                out.species = r.U16();
                out.stage = r.U8();
                out.level = r.U8();
                return out.stage < kMaxStages && out.level >= 1 && out.level <= kMaxLevel;
            case kOpGmEdit:
            {
                if (body.size() != 12)
                    return false;
                out.item = r.U64();
                uint8_t const what = r.U8();
                out.a = r.U8();
                out.b = r.U8();
                if (what < uint8_t(Edit::Level) || what > uint8_t(Edit::ClearTraits))
                    return false;
                out.what = Edit(what);
                return true;
            }
            case kOpBreed:
                if (body.size() != 17)
                    return false;
                out.item = r.U64();
                out.other = r.U64();
                return true;
            case kOpRepick:
                if (body.size() != 11)
                    return false;
                out.item = r.U64();
                out.a = r.U8();
                out.b = r.U8();
                return out.a < kMaxStages && out.b >= 1 && out.b <= 2;
            default:
                return false;
        }
    }

    // ── Entries ─────────────────────────────────────────────────────────────────────────────

    std::string ReachText(CompanionSpecies const& sp, uint8_t stage, std::function<std::string(uint32_t)> const& itemName,
        std::function<std::string(uint32_t)> const& placeName)
    {
        if (stage == 0 || stage >= sp.stageCount)
            return stage == 0 ? "Starts here" : "";
        EvolutionRule const& rule = sp.evolutions[stage - 1];
        if (!rule.Exists())
            return "";
        std::string o = "Level " + std::to_string(rule.level);
        if (rule.item)
        {
            std::string const name = itemName ? itemName(rule.item) : std::string();
            o += ", " + std::to_string(std::max<uint8_t>(rule.itemCount, 1)) + " " + (name.empty() ? "catalyst" : name);
        }
        if (!rule.areas.empty())
        {
            std::string where;
            for (uint32_t id : rule.areas)
            {
                std::string const n = placeName ? placeName(id) : std::string();
                if (!n.empty() && where.find(n) == std::string::npos)
                    where += (where.empty() ? "" : " or ") + n;
            }
            o += where.empty() ? ", in the right place" : ", in " + where;
        }
        switch (rule.condition)
        {
            case Condition::Night: o += ", at night"; break;
            case Condition::Day: o += ", by day"; break;
            case Condition::KillType:
                o += ", " + std::to_string(rule.conditionCount) + " kills of " + TypePlural(rule.conditionType);
                break;
            default: break;
        }
        return o;
    }

    Entry MakeEntry(CompanionSpecies const& sp, SeenMap const& seen, bool gm,
        std::function<std::string(uint32_t)> const& creatureName, std::function<std::string(uint32_t)> const& itemName,
        std::function<std::string(uint32_t)> const& placeName)
    {
        Entry e;
        e.id = sp.id;
        e.family = sp.family;
        if (!Visible(seen, sp.id, gm))
            return e;
        e.seen = Of(seen, sp.id);
        if (e.seen == Seen::Unknown)
            e.seen = Seen::Seen;     // an admin's view of one never met
        e.role = sp.role;
        e.item = sp.item;
        e.name = sp.name;
        MovesetInfo const& moves = MovesetData(MovesetOf(sp));
        e.trait = moves.trait;
        if (moves.mounted.Exists() && sp.stageCount && sp.stages[sp.stageCount - 1].rideable)
            e.mounted = moves.mounted.name;
        for (uint8_t t = 1; t <= kTraitCount; ++t)
            if (TraitEligible(Trait(t), sp))
                e.eligible |= TraitBit(Trait(t));
        for (uint8_t st = 0; st < sp.stageCount && st < kMaxStages; ++st)
        {
            StageEntry s;
            s.creature = sp.stages[st].creature;
            s.name = creatureName ? creatureName(s.creature) : std::string();
            if (s.name.empty())
                s.name = sp.name;
            s.reach = ReachText(sp, st, itemName, placeName);
            s.rideable = sp.stages[st].rideable;
            for (int o = 0; o < 2; ++o)
            {
                Ability const& a = moves.stages[st][o];
                if (!a.Exists())
                    continue;
                s.abilities[o] = a.name;
                s.kinds[o] = uint8_t(a.kind);
                s.cooldowns[o] = a.cooldown;
            }
            e.stages.push_back(std::move(s));
        }
        return e;
    }

    std::vector<uint8_t> EncodeIndex(uint8_t flags, std::vector<Entry> const& entries)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpIndex);
        Put8(o, flags);
        Put8(o, uint8_t(std::min<size_t>(entries.size(), 255)));
        for (size_t i = 0; i < entries.size() && i < 255; ++i)
        {
            Entry const& e = entries[i];
            Put16(o, e.id);
            Put8(o, uint8_t(e.family));
            Put8(o, uint8_t(e.seen));
            if (e.seen == Seen::Unknown)
                continue;
            Put8(o, uint8_t(e.role));
            Put32(o, e.item);
            PutStr8(o, e.name);
            PutStr8(o, e.trait);
            PutStr8(o, e.mounted);
            Put32(o, e.eligible);
            Put8(o, uint8_t(std::min<size_t>(e.stages.size(), kMaxStages)));
            for (size_t st = 0; st < e.stages.size() && st < kMaxStages; ++st)
            {
                StageEntry const& s = e.stages[st];
                Put32(o, s.creature);
                PutStr8(o, s.name);
                PutStr8(o, s.reach);
                Put8(o, s.rideable ? 1 : 0);
                for (int a = 0; a < 2; ++a)
                {
                    PutStr8(o, s.abilities[a]);
                    Put8(o, s.kinds[a]);
                    Put8(o, s.cooldowns[a]);
                }
            }
        }
        return o;
    }

    bool DecodeIndex(std::vector<uint8_t> const& body, uint8_t& flags, std::vector<Entry>& out)
    {
        Reader r{body, 0};
        out.clear();
        if (r.U8() != kOpIndex)
            return false;
        flags = r.U8();
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Entry e;
            e.id = r.U16();
            e.family = Family(r.U8());
            uint8_t const seen = r.U8();
            if (seen > uint8_t(Seen::Captured))
                return false;
            e.seen = Seen(seen);
            if (e.seen != Seen::Unknown)
            {
                e.role = Role(r.U8());
                e.item = r.U32();
                e.name = r.Str8();
                e.trait = r.Str8();
                e.mounted = r.Str8();
                e.eligible = r.U32();
                uint8_t const stages = r.U8();
                if (stages > kMaxStages)
                    return false;
                for (uint8_t st = 0; st < stages && r.ok; ++st)
                {
                    StageEntry s;
                    s.creature = r.U32();
                    s.name = r.Str8();
                    s.reach = r.Str8();
                    s.rideable = r.U8() != 0;
                    for (int a = 0; a < 2; ++a)
                    {
                        s.abilities[a] = r.Str8();
                        s.kinds[a] = r.U8();
                        s.cooldowns[a] = r.U8();
                    }
                    e.stages.push_back(std::move(s));
                }
            }
            out.push_back(std::move(e));
        }
        return r.Done();
    }

    std::vector<std::vector<uint8_t>> IndexPages(std::vector<Entry> const& entries, bool gm, size_t perPage)
    {
        std::vector<std::vector<uint8_t>> pages;
        perPage = std::max<size_t>(1, std::min<size_t>(perPage, 255));
        size_t at = 0;
        do
        {
            size_t const end = std::min(entries.size(), at + perPage);
            uint8_t flags = gm ? INDEX_GM : 0;
            if (at == 0)
                flags |= INDEX_FIRST;
            if (end == entries.size())
                flags |= INDEX_LAST;
            pages.push_back(EncodeIndex(flags, std::vector<Entry>(entries.begin() + long(at), entries.begin() + long(end))));
            at = end;
        } while (at < entries.size());
        return pages;
    }

    // ── Carried ─────────────────────────────────────────────────────────────────────────────

    std::vector<uint8_t> EncodeCarried(uint8_t header, std::vector<CarriedEntry> const& list, CarriedCare const& care)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpCarried);
        Put8(o, header);
        Put8(o, uint8_t(std::min<size_t>(list.size(), 255)));
        for (size_t i = 0; i < list.size() && i < 255; ++i)
        {
            CarriedEntry const& c = list[i];
            Put64(o, c.item);
            Put16(o, c.species);
            Put8(o, c.stage);
            Put8(o, c.level);
            Put32(o, c.xp);
            Put32(o, c.xpToNext);
            Put32(o, c.traits);
            Put8(o, c.rarity);
            Put8(o, c.twinStage);
            for (uint8_t p : c.picks)
                Put8(o, p);
            Put8(o, uint8_t(c.pending));
            Put8(o, c.flags);
            Put8(o, c.block);
            Put8(o, c.nextEvolveLevel);
            Put8(o, c.breedCount);
            Put32(o, c.breedRestSeconds);
            Put8(o, c.generation);
            Put32(o, c.faintSeconds);
            Put8(o, c.healthPct);
            Put32(o, c.seed);
        }
        Put8(o, care.draughts);
        Put32(o, care.reviveCost);
        Put8(o, care.needsCare);
        return o;
    }

    bool DecodeCarried(std::vector<uint8_t> const& body, uint8_t& header, std::vector<CarriedEntry>& out)
    {
        CarriedCare care;
        return DecodeCarried(body, header, out, care);
    }

    bool DecodeCarried(std::vector<uint8_t> const& body, uint8_t& header, std::vector<CarriedEntry>& out,
        CarriedCare& care)
    {
        Reader r{body, 0};
        out.clear();
        if (r.U8() != kOpCarried)
            return false;
        header = r.U8();
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            CarriedEntry c;
            c.item = r.U64();
            c.species = r.U16();
            c.stage = r.U8();
            c.level = r.U8();
            c.xp = r.U32();
            c.xpToNext = r.U32();
            c.traits = r.U32();
            c.rarity = r.U8();
            c.twinStage = r.U8();
            for (uint8_t& p : c.picks)
                p = r.U8();
            c.pending = int8_t(r.U8());
            c.flags = r.U8();
            c.block = r.U8();
            c.nextEvolveLevel = r.U8();
            c.breedCount = r.U8();
            c.breedRestSeconds = r.U32();
            c.generation = r.U8();
            c.faintSeconds = r.U32();
            c.healthPct = r.U8();
            c.seed = r.U32();
            out.push_back(c);
        }
        care.draughts = r.U8();
        care.reviveCost = r.U32();
        care.needsCare = r.U8();
        return r.Done();
    }

    std::vector<uint8_t> EncodeNotice(NoticeKind kind, std::string const& text)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpNotice);
        Put8(o, uint8_t(kind));
        size_t const n = std::min<size_t>(text.size(), 1000);
        Put16(o, uint16_t(n));
        o.insert(o.end(), text.begin(), text.begin() + long(n));
        return o;
    }

    bool DecodeNotice(std::vector<uint8_t> const& body, NoticeKind& kind, std::string& text)
    {
        Reader r{body, 0};
        if (r.U8() != kOpNotice)
            return false;
        uint8_t const k = r.U8();
        if (k > uint8_t(NoticeKind::Good))
            return false;
        kind = NoticeKind(k);
        text = r.Str(r.U16());
        return r.Done();
    }

    std::vector<uint8_t> EncodeOpen(Access at)
    {
        return {kOpOpen, uint8_t(at)};
    }

    // ── Looks ───────────────────────────────────────────────────────────────────────────────

    namespace
    {
        // creature_template.pet_family's names (CreatureFamily.dbc), for "of the wolf family".
        char const* PetFamilyName(uint8_t f)
        {
            switch (f)
            {
                case 1: return "wolf";
                case 2: return "cat";
                case 3: return "spider";
                case 4: return "bear";
                case 5: return "boar";
                case 6: return "crocolisk";
                case 7: return "carrion bird";
                case 8: return "crab";
                case 9: return "gorilla";
                case 11: return "raptor";
                case 12: return "tallstrider";
                case 15: return "felhunter";
                case 16: return "voidwalker";
                case 17: return "succubus";
                case 19: return "doomguard";
                case 20: return "scorpid";
                case 21: return "turtle";
                case 23: return "imp";
                case 24: return "bat";
                case 25: return "hyena";
                case 26: return "owl";
                case 27: return "wind serpent";
                default: return "";
            }
        }
    }

    std::string FoundText(CompanionSpecies const& sp, std::function<std::string(uint32_t)> const& creatureName)
    {
        std::vector<std::string> names;
        for (uint32_t entry : sp.drops.entries)
        {
            std::string const n = creatureName ? creatureName(entry) : std::string();
            if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end())
                names.push_back(n);
        }
        std::string out;
        if (!names.empty())
        {
            size_t const shown = std::min<size_t>(names.size(), 3);
            out = "Drops from ";
            for (size_t i = 0; i < shown; ++i)
            {
                if (i)
                    out += (i + 1 == shown && names.size() == shown) ? " and " : ", ";
                out += names[i];
            }
            if (names.size() > shown)
                out += " and " + std::to_string(names.size() - shown) + " more";
        }
        if (sp.drops.type != CreatureType::None)
        {
            std::string kind;
            if (char const* f = PetFamilyName(sp.drops.petFamily); *f)
                kind = std::string("any creature of the ") + f + " family";
            else
                kind = std::string("any ") + TypePlural(sp.drops.type);
            out += out.empty() ? "Drops from " + kind : "; also " + kind;
        }
        if (out.empty())
            return "Not found in the wild: bred at a stable master, or traded";
        return out + ". Rare and elite ones drop it more often.";
    }

    Looks MakeLooks(CompanionSpecies const& sp, std::function<uint32_t(uint32_t)> const& creatureDisplay,
        std::function<std::string(uint32_t)> const& creatureName)
    {
        Looks l;
        l.id = sp.id;
        MovesetInfo const& moves = MovesetData(MovesetOf(sp));
        l.basicSpell = moves.basicSpell;
        for (uint8_t st = 0; st < sp.stageCount && st < kMaxStages; ++st)
        {
            StageLook s;
            // The stage's own display (the look table), else its creature's.
            s.display = sp.stages[st].display ? sp.stages[st].display
                : creatureDisplay ? creatureDisplay(sp.stages[st].creature) : 0;
            for (int o = 0; o < 2; ++o)
                s.spells[o] = moves.stages[st][o].spell;
            if (sp.stages[st].rideable)
            {
                CompanionState ridden;
                ridden.species = sp.id;
                ridden.stage = st;
                l.rideDisplay = MountDisplay(sp, ridden, 0, s.display);
            }
            l.stages.push_back(s);
        }
        if (moves.mounted.Exists() && sp.stageCount && sp.stages[sp.stageCount - 1].rideable)
            l.mountedSpell = moves.mounted.spell;
        l.found = FoundText(sp, creatureName);
        return l;
    }

    std::vector<uint8_t> EncodeLooks(std::vector<Looks> const& looks)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpLooks);
        Put8(o, uint8_t(std::min<size_t>(looks.size(), 255)));
        for (size_t i = 0; i < looks.size() && i < 255; ++i)
        {
            Looks const& l = looks[i];
            Put16(o, l.id);
            uint8_t const n = uint8_t(std::min<size_t>(l.stages.size(), kMaxStages));
            Put8(o, n);
            for (uint8_t st = 0; st < n; ++st)
            {
                Put32(o, l.stages[st].display);
                Put32(o, l.stages[st].spells[0]);
                Put32(o, l.stages[st].spells[1]);
            }
            Put32(o, l.basicSpell);
            Put32(o, l.mountedSpell);
            Put32(o, l.rideDisplay);
            PutStr8(o, l.found);
        }
        return o;
    }

    bool DecodeLooks(std::vector<uint8_t> const& body, std::vector<Looks>& out)
    {
        Reader r{body, 0};
        if (r.U8() != kOpLooks)
            return false;
        uint8_t const n = r.U8();
        out.clear();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Looks l;
            l.id = r.U16();
            uint8_t const stages = r.U8();
            if (stages > kMaxStages)
                return false;
            for (uint8_t st = 0; st < stages; ++st)
            {
                StageLook s;
                s.display = r.U32();
                s.spells[0] = r.U32();
                s.spells[1] = r.U32();
                l.stages.push_back(s);
            }
            l.basicSpell = r.U32();
            l.mountedSpell = r.U32();
            l.rideDisplay = r.U32();
            l.found = r.Str8();
            out.push_back(std::move(l));
        }
        return r.Done();
    }

    std::vector<std::vector<uint8_t>> LooksPages(std::vector<Looks> const& looks, size_t perPage)
    {
        std::vector<std::vector<uint8_t>> pages;
        perPage = std::max<size_t>(1, std::min<size_t>(perPage, 255));
        for (size_t at = 0; at < looks.size(); at += perPage)
        {
            size_t const end = std::min(looks.size(), at + perPage);
            pages.push_back(EncodeLooks(std::vector<Looks>(looks.begin() + long(at), looks.begin() + long(end))));
        }
        return pages;
    }
}
