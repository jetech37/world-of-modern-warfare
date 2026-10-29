/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionIndex.h"
#include "fusion/CompanionKeeper.h"

#include <algorithm>
#include <set>

using namespace Fusion;
using namespace Fusion::Companions;
using namespace Fusion::Companions::Index;

namespace
{
    CompanionSpecies const& IndexSpecies(uint16_t id)
    {
        CompanionSpecies const* s = FindSpecies(StarterSpecies(), id);
        static CompanionSpecies none;
        return s ? *s : none;
    }

    std::string Named(uint32_t id) { return "#" + std::to_string(id); }
}

TEST(index_entries_show_only_what_was_seen_or_captured)
{
    SeenMap seen;
    CHECK(!Visible(seen, 6, false));
    CHECK(Visible(seen, 6, true));     // an admin sees the whole index
    CHECK(Record(seen, 6, Seen::Seen));
    CHECK(!Record(seen, 6, Seen::Seen));    // nothing new
    CHECK(Visible(seen, 6, false) && Of(seen, 6) == Seen::Seen);
    CHECK(Record(seen, 6, Seen::Captured));     // seen, then caught
    CHECK(!Record(seen, 6, Seen::Seen));        // never back down
    CHECK(Of(seen, 6) == Seen::Captured);
    CHECK(!Record(seen, 0, Seen::Captured) && !Record(seen, 7, Seen::Unknown));
    CHECK(!Visible(seen, 7, false) && Of(seen, 7) == Seen::Unknown);

    // An unseen entry goes out as its id and family alone: nothing to read off the wire.
    CompanionSpecies const& wolf = IndexSpecies(6);
    Entry hidden = MakeEntry(IndexSpecies(7), seen, false, Named, Named, Named);
    CHECK(hidden.id == 7 && hidden.family == Family::Cat && hidden.seen == Seen::Unknown);
    CHECK(hidden.name.empty() && hidden.stages.empty() && hidden.trait.empty());
    Entry shown = MakeEntry(wolf, seen, false, Named, Named, Named);
    CHECK(shown.seen == Seen::Captured && shown.name == "Wolf Pup" && shown.stages.size() == 3);
    CHECK(shown.stages[0].name == "#10259" && shown.stages[2].rideable && !shown.stages[0].rideable);
    CHECK(shown.stages[0].reach == "Starts here");
    CHECK(shown.stages[1].reach.rfind("Level 10", 0) == 0);
    CHECK(shown.stages[2].reach.find("1 #90102") != std::string::npos);
    CHECK(!shown.stages[1].abilities[0].empty() && !shown.stages[1].abilities[1].empty());
    CHECK(!shown.trait.empty() && !shown.mounted.empty());
    CHECK(HasTrait(shown.eligible, Trait::Surefooted) && !HasTrait(shown.eligible, Trait::Gunner));
    // The admin's view of a species never met is a full entry, marked seen.
    Entry gm = MakeEntry(IndexSpecies(7), SeenMap{}, true, Named, Named, Named);
    CHECK(gm.seen == Seen::Seen && gm.name == "Nightsaber Cub" && gm.stages.size() == 3);
}

TEST(index_knows_the_species_a_creature_belongs_to)
{
    CreatureMap const m = MapCreatures(StarterSpecies());
    // The wolf line's stages: a pup, a dire wolf, a war wolf.
    CHECK(m.count(10259) && m.at(10259) == std::vector<uint16_t>{6});
    CHECK(m.count(565) && m.at(565).front() == 6);
    CHECK(m.count(358) && std::find(m.at(358).begin(), m.at(358).end(), 6) != m.at(358).end());
    // A creature no stage shows is no species'.
    CHECK(m.count(1) == 0);
    for (auto const& [creature, list] : m)
    {
        CHECK(creature != 0 && !list.empty());
        std::set<uint16_t> unique(list.begin(), list.end());
        CHECK(unique.size() == list.size());
    }
}

TEST(index_gm_and_player_permissions)
{
    for (Access at : {Access::Hud, Access::Npc})
    {
        // Anyone, anywhere.
        for (Action a : {Action::Summon, Action::Evolve, Action::Pick, Action::Duel})
        {
            CHECK(Allowed(a, at, false));
            CHECK(Allowed(a, at, true));
        }
        // A game master's tools only, wherever the window was opened.
        CHECK(!Allowed(Action::GmGive, at, false) && Allowed(Action::GmGive, at, true));
        CHECK(!Allowed(Action::GmEdit, at, false) && Allowed(Action::GmEdit, at, true));
    }
    // A stable master's: breeding and changing an ability (an admin anywhere).
    CHECK(!Allowed(Action::Breed, Access::Hud, false) && Allowed(Action::Breed, Access::Npc, false));
    CHECK(!Allowed(Action::Repick, Access::Hud, false) && Allowed(Action::Repick, Access::Npc, false));
    CHECK(Allowed(Action::Breed, Access::Hud, true) && Allowed(Action::Repick, Access::Hud, true));
    // And players never buy: the keeper's rules refuse them too.
    Keeper::Customer rich;
    rich.level = 60;
    rich.money = 100000000;
    CHECK(Keeper::CanBuy(IndexSpecies(6), 0, 10, rich) == Keeper::Refusal::GmOnly);
}

TEST(index_mount_shows_the_companions_own_creature)
{
    // The bug: a Stranglethorn Kitten grown into an Elder Stranglethorn Tiger rides on the
    // Timber Wolf's spells (580 / 23250, for vanilla's speed and rules); the aura's own display is
    // a wolf. The ridden display is its own last form's: a riding tiger (the look table).
    CompanionSpecies const& kitten = IndexSpecies(106);
    CHECK(kitten.family == Family::Cat && kitten.stageCount == 3);
    CHECK(kitten.stages[2].rideable && kitten.stages[2].mountSpell == 580);
    CompanionState s;
    s.species = 106;
    s.stage = 2;
    CHECK(MountDisplay(kitten, s, 0, 12345) == 6443);       // the look table's riding tiger
    CHECK(MountDisplay(kitten, s, 4321, 12345) == 4321);    // the companion's own display wins
    s.stage = 1;
    CHECK(MountDisplay(kitten, s, 4321, 12345) == 0);       // a stage that is not ridden
    // A line with a vanilla mount of its own: no ride display, so its stage's own display.
    CompanionSpecies const& wolf = IndexSpecies(6);
    CompanionState w;
    w.species = 6;
    w.stage = 2;
    CHECK(wolf.stages[2].rideDisplay == 0);
    CHECK(MountDisplay(wolf, w, 0, 207) == 247);
    // Every ridden stage of every line shows some display.
    for (CompanionSpecies const& sp : StarterSpecies())
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            CompanionState x;
            x.species = sp.id;
            x.stage = st;
            CHECK((MountDisplay(sp, x, 0, 1) != 0) == sp.stages[st].rideable);
        }
}

TEST(index_messages_round_trip)
{
    SeenMap seen;
    Record(seen, 6, Seen::Captured);
    std::vector<Entry> entries;
    for (CompanionSpecies const& sp : StarterSpecies())
        entries.push_back(MakeEntry(sp, seen, false, Named, Named, Named));
    std::vector<std::vector<uint8_t>> const pages = IndexPages(entries, false, 12);
    CHECK(pages.size() == (entries.size() + 11) / 12);
    std::vector<Entry> back;
    for (size_t i = 0; i < pages.size(); ++i)
    {
        uint8_t flags = 0;
        std::vector<Entry> part;
        CHECK(DecodeIndex(pages[i], flags, part));
        CHECK(((flags & INDEX_FIRST) != 0) == (i == 0));
        CHECK(((flags & INDEX_LAST) != 0) == (i + 1 == pages.size()));
        CHECK((flags & INDEX_GM) == 0);
        back.insert(back.end(), part.begin(), part.end());
    }
    CHECK(back.size() == entries.size());
    size_t known = 0;
    for (size_t i = 0; i < back.size(); ++i)
    {
        CHECK(back[i].id == entries[i].id && back[i].family == entries[i].family && back[i].seen == entries[i].seen);
        CHECK(back[i].name == entries[i].name && back[i].stages.size() == entries[i].stages.size());
        if (back[i].seen != Seen::Unknown)
        {
            ++known;
            CHECK(back[i].stages[1].abilities[0] == entries[i].stages[1].abilities[0]);
            CHECK(back[i].eligible == entries[i].eligible && back[i].stages[1].reach == entries[i].stages[1].reach);
        }
    }
    CHECK(known == 1);
    // An empty index is still one page, first and last.
    std::vector<std::vector<uint8_t>> const none = IndexPages({}, true);
    CHECK(none.size() == 1 && none[0] == std::vector<uint8_t>({kOpIndex, INDEX_GM | INDEX_FIRST | INDEX_LAST, 0}));
    uint8_t flags = 0;
    std::vector<Entry> out;
    std::vector<uint8_t> cut = pages[0];
    cut.pop_back();
    CHECK(!DecodeIndex(cut, flags, out));

    CarriedEntry c;
    c.item = 0x0102030405060708ull;
    c.species = 106;
    c.stage = 2;
    c.level = 43;
    c.xp = 1234;
    c.xpToNext = 9999;
    c.traits = TraitBit(Trait::Swift);
    c.rarity = 1;
    c.picks = {1, 2, 0};
    c.pending = 2;
    c.flags = CARRIED_OUT | CARRIED_RIDEABLE;
    c.block = 1;
    c.breedCount = 1;
    c.breedRestSeconds = 3600;
    c.generation = 2;
    c.flags |= CARRIED_FAINTED;
    c.faintSeconds = 3120;
    c.healthPct = 0;
    c.seed = 0x8badf00du;
    CarriedCare care;
    care.draughts = 3;
    care.reviveCost = 430;
    care.needsCare = 2;
    std::vector<uint8_t> const body = EncodeCarried(CARRIED_GM | CARRIED_AT_STABLE, {c, CarriedEntry{}}, care);
    CHECK(body.size() == 3 + 2 * 48 + 6);
    uint8_t header = 0;
    std::vector<CarriedEntry> list;
    CarriedCare careBack;
    CHECK(DecodeCarried(body, header, list, careBack));
    CHECK(header == (CARRIED_GM | CARRIED_AT_STABLE) && list.size() == 2);
    CHECK(list[0].item == c.item && list[0].species == 106 && list[0].pending == 2 && list[0].picks[1] == 2);
    CHECK(list[0].breedRestSeconds == 3600 && list[0].generation == 2 && list[1].pending == -1);
    CHECK((list[0].flags & CARRIED_FAINTED) && list[0].faintSeconds == 3120 && list[0].healthPct == 0);
    CHECK(list[0].seed == 0x8badf00du && list[1].healthPct == 100 && list[1].faintSeconds == 0);
    CHECK(careBack.draughts == 3 && careBack.reviveCost == 430 && careBack.needsCare == 2);
    // The short form reads the same body; a cut one is refused.
    CHECK(DecodeCarried(body, header, list));
    std::vector<uint8_t> shortBody(body.begin(), body.end() - 6);
    CHECK(!DecodeCarried(shortBody, header, list));

    NoticeKind kind = NoticeKind::Info;
    std::string text;
    CHECK(DecodeNotice(EncodeNotice(NoticeKind::Good, "Your Wolf Pup reached level 12."), kind, text));
    CHECK(kind == NoticeKind::Good && text == "Your Wolf Pup reached level 12.");
    CHECK(EncodeOpen(Access::Npc) == std::vector<uint8_t>({kOpOpen, 1}));
}

TEST(index_requests_decode_strictly)
{
    auto rt = [](Index::Request const& r) {
        Index::Request out;
        bool const ok = Index::DecodeRequest(Index::EncodeRequest(r), out);
        return std::make_pair(ok, out);
    };
    Index::Request give;
    give.op = kOpGmGive;
    give.species = 164;
    give.stage = 2;
    give.level = 60;
    auto g = rt(give);
    CHECK(g.first && g.second.species == 164 && g.second.stage == 2 && g.second.level == 60);
    CHECK(Index::EncodeRequest(give) == std::vector<uint8_t>({kOpGmGive, 164, 0, 2, 60}));
    give.level = 61;
    CHECK(!rt(give).first);
    give.level = 1;
    give.stage = 3;
    CHECK(!rt(give).first);

    Index::Request edit;
    edit.op = kOpGmEdit;
    edit.item = 77;
    edit.what = Edit::Trait;
    edit.a = uint8_t(Trait::Alpha);
    auto e = rt(edit);
    CHECK(e.first && e.second.item == 77 && e.second.what == Edit::Trait && e.second.a == 28);
    std::vector<uint8_t> bad = Index::EncodeRequest(edit);
    bad[9] = 9;
    Index::Request out;
    CHECK(!Index::DecodeRequest(bad, out));

    Index::Request breed;
    breed.op = kOpBreed;
    breed.item = 1;
    breed.other = 2;
    auto b = rt(breed);
    CHECK(b.first && b.second.item == 1 && b.second.other == 2);

    Index::Request repick;
    repick.op = kOpRepick;
    repick.item = 5;
    repick.a = 1;
    repick.b = 2;
    CHECK(rt(repick).first);
    repick.b = 3;
    CHECK(!rt(repick).first);

    // Reviving: at the stable master, and a draught for one companion (0: the last to faint).
    CHECK(Index::EncodeRequest(Index::Request{kOpStableRevive}) == std::vector<uint8_t>({kOpStableRevive}));
    CHECK(Index::DecodeRequest({kOpStableRevive}, out) && out.op == kOpStableRevive);
    CHECK(Index::DecodeRequest({kOpBuyDraught}, out) && out.op == kOpBuyDraught);
    CHECK(!Index::DecodeRequest({kOpStableRevive, 1}, out));
    Index::Request draught;
    draught.op = kOpUseDraught;
    draught.item = 0x4000000000000102ull;
    auto d = rt(draught);
    CHECK(d.first && d.second.item == draught.item);
    CHECK(Index::EncodeRequest(draught).size() == 9);
    CHECK(!Index::DecodeRequest({kOpUseDraught, 1, 2}, out));
    CHECK(Allowed(Action::Draught, Access::Hud, false));
    CHECK(!Allowed(Action::StableCare, Access::Hud, false));
    CHECK(Allowed(Action::StableCare, Access::Npc, false) && Allowed(Action::StableCare, Access::Hud, true));

    CHECK(Index::DecodeRequest({kOpIndexRequest}, out) && out.op == kOpIndexRequest);
    CHECK(Index::DecodeRequest({kOpCarriedRequest}, out));
    CHECK(!Index::DecodeRequest({kOpIndexRequest, 0}, out));
    CHECK(!Index::DecodeRequest({kOpSummon, 0, 0, 0, 0, 0, 0, 0, 0}, out));   // the bar's ops are not the window's
    CHECK(!Index::DecodeRequest({}, out));
}

TEST(a_ridden_tiger_stays_a_tiger_while_its_mount_spell_is_cast)
{
    // The bug: riding casts the Timber Wolf's spell triggered (580, 3 s cast). A triggered spell
    // with a cast time is in no spell slot, so the half-second look saw "not casting, no aura"
    // after 1.5 s and put the tiger back on foot; the aura landed at 3 s on a rider who was no
    // longer riding, and showed the spell's own mount: a wolf.
    uint32_t const cast = 3000;
    for (uint32_t t = 0; t < cast + kRideCastGraceMs; t += 500)
        CHECK(LookAtRide(false, false, t, cast, false) == RideLook::Waiting);
    CHECK(LookAtRide(true, false, cast, cast, false) == RideLook::Mounted);
    CHECK(LookAtRide(true, true, 60000, cast, false) == RideLook::Mounted);
    // Off by choice or knocked off: the aura is gone after it was on.
    CHECK(LookAtRide(false, true, 4000, cast, false) == RideLook::Off);
    // No aura ever came (indoors, interrupted): only once the cast has had its time.
    CHECK(LookAtRide(false, false, cast + kRideCastGraceMs, cast, false) == RideLook::NeverTook);
    // Asked off while the cast is pending: it waits for the aura and takes it off, or gives up
    // quietly once the cast has had its time.
    CHECK(LookAtRide(false, false, 1000, cast, true) == RideLook::Waiting);
    CHECK(LookAtRide(true, false, cast, cast, true) == RideLook::Off);
    CHECK(LookAtRide(false, false, cast + kRideCastGraceMs, cast, true) == RideLook::Off);
    // An instant mount spell (a shorter cast) is judged on its own clock.
    CHECK(LookAtRide(false, false, kRideCastGraceMs, 0, false) == RideLook::NeverTook);

    // While riding, the tiger's own display answers the aura; the spell's is never shown.
    CompanionSpecies const& kitten = IndexSpecies(106);
    CompanionState s;
    s.species = 106;
    s.stage = 2;
    uint32_t const spell = RideSpell(kitten.stages[2], 60);
    CHECK(spell == 580);
    CHECK(MountDisplay(kitten, s, 0, 0) == 6443);
}

TEST(a_stray_companion_mount_aura_is_taken_off)
{
    std::vector<CompanionSpecies> const& all = StarterSpecies();
    CHECK(IsCompanionRideSpell(all, 580) && IsCompanionRideSpell(all, 23250));
    CHECK(IsCompanionRideSpell(all, 10793));    // the nightsaber's own
    CHECK(!IsCompanionRideSpell(all, 0) && !IsCompanionRideSpell(all, 133));
    // The ride in progress is not stray; the same spell with no ride (saved at a logout in the
    // saddle, or landing after the ride was given up) is.
    CHECK(!StrayRideAura(true, true, 580, 580, false, false));
    CHECK(StrayRideAura(true, false, 0, 580, false, false));
    CHECK(StrayRideAura(true, true, 23250, 580, false, false));
    // Vanilla's own reins (cast from the item) and a spell the rider knows are theirs.
    CHECK(!StrayRideAura(true, false, 0, 580, true, false));
    CHECK(!StrayRideAura(true, false, 0, 580, false, true));
    // Any other mount is none of the companions' business.
    CHECK(!StrayRideAura(false, false, 0, 458, false, false));
}

TEST(looks_carry_each_stage_model_its_moves_and_where_it_is_found)
{
    auto display = [](uint32_t entry) { return entry + 100000; };
    CompanionSpecies const& kitten = IndexSpecies(106);
    Looks const l = MakeLooks(kitten, display, Named);
    CHECK(l.id == 106 && l.stages.size() == 3);
    // Each stage's own display (the look table): a red kitten, a yellow tiger, a riding tiger.
    CHECK(l.stages[0].display == 15151 && l.stages[1].display == 1043 && l.stages[2].display == 6443);
    // Each stage's two options carry their spells (their icons); the mount form is the tiger's.
    MovesetInfo const& moves = MovesetData(MovesetOf(kitten));
    for (int st = 0; st < 3; ++st)
        for (int o = 0; o < 2; ++o)
            CHECK(l.stages[st].spells[o] == moves.stages[st][o].spell);
    CHECK(l.rideDisplay == 6443);
    CHECK(l.found.rfind("Drops from #681, #682 and #1085", 0) == 0);
    // A line ridden on its own spell shows its stage creature.
    Looks const wolf = MakeLooks(IndexSpecies(6), display, Named);
    CHECK(wolf.rideDisplay == wolf.stages[2].display && wolf.rideDisplay != 0);
    // Nothing ridden: no ride display.
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        Looks const x = MakeLooks(sp, display, Named);
        bool ridden = false;
        for (uint8_t st = 0; st < sp.stageCount; ++st)
            ridden = ridden || sp.stages[st].rideable;
        CHECK((x.rideDisplay != 0) == ridden);
        CHECK(x.stages.size() == sp.stageCount);
        CHECK(!x.found.empty());
    }

    // The wire, both ways, and strict about its length.
    std::vector<Looks> all;
    for (CompanionSpecies const& sp : StarterSpecies())
        all.push_back(MakeLooks(sp, display, Named));
    std::vector<std::vector<uint8_t>> const pages = LooksPages(all, 24);
    size_t count = 0;
    for (std::vector<uint8_t> const& p : pages)
    {
        CHECK(p.size() < 8192 && p[0] == kOpLooks);
        std::vector<Looks> back;
        CHECK(DecodeLooks(p, back));
        for (Looks const& b : back)
        {
            Looks const& a = all[count++];
            CHECK(a.id == b.id && a.stages.size() == b.stages.size() && a.found == b.found);
            CHECK(a.rideDisplay == b.rideDisplay && a.mountedSpell == b.mountedSpell && a.basicSpell == b.basicSpell);
            for (size_t st = 0; st < a.stages.size(); ++st)
                CHECK(a.stages[st].display == b.stages[st].display && a.stages[st].spells == b.stages[st].spells);
        }
    }
    CHECK(count == all.size());
    std::vector<uint8_t> cut = EncodeLooks({l});
    std::vector<Looks> back;
    CHECK(DecodeLooks(cut, back) && back.size() == 1);
    cut.pop_back();
    CHECK(!DecodeLooks(cut, back));
    // Its op is free on the companion channel.
    for (uint8_t op : {kOpBar, kOpEvolved, kOpDuel, kOpMoves, kOpCooldown, kOpLineage, kOpIndex, kOpCarried,
             kOpNotice, kOpOpen, kOpIndexRequest, kOpCarriedRequest, kOpGmGive, kOpGmEdit, kOpBreed, kOpRepick,
             kOpStableRevive, kOpUseDraught, kOpBuyDraught, kOpSummon, kOpDismiss, kOpEvolve, kOpRide, kOpPick})
        CHECK(op != kOpLooks);
}
