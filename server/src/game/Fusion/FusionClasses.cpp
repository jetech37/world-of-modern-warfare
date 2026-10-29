/*
 * World of Modern Warfare: the class framework in the game (see FusionClasses.h). This file holds
 * the framework: the spellbook, the trainer, the kit picks, the client channel and the hooks'
 * plumbing; FusionClassAbilities.cpp holds the options' custom parts.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionClasses.h"
#include "fusion/ClassTuning.h"
#include "FusionMgr.h"
#include "fusion/DruidForms.h"
#include "Chat.h"
#include "Chat/MasterPlayer.h"
#include "Config/Config.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <unordered_set>

using namespace Fusion::Classes;

namespace
{
    // The vanilla class spell tabs (SkillLine ids): their spells and talents are the old spellbook.
    std::unordered_set<uint32> const kClassSkillLines = {
        26, 256, 257,       // warrior: Arms, Fury, Protection
        594, 267, 184,      // paladin: Holy, Protection, Retribution
        50, 163, 51,        // hunter: Beast Mastery, Marksmanship, Survival
        253, 38, 39,        // rogue: Assassination, Combat, Subtlety
        56, 613, 78,        // priest: Holy, Discipline, Shadow Magic
        375, 373, 374,      // shaman: Elemental Combat, Enhancement, Restoration
        237, 8, 6,          // mage: Arcane, Fire, Frost
        355, 354, 593,      // warlock: Affliction, Demonology, Destruction
        574, 134, 573,      // druid: Balance, Feral Combat, Restoration
    };

    // Spells a character keeps whatever its kit: the plain attacks the gun and the hands lean on.
    std::unordered_set<uint32> const kAlwaysKept = {
        75,     // Auto Shot
        3018,   // Shoot (bow, gun)
        5019,   // Shoot (wand)
        2764,   // Throw
        2567,   // Throw (rogue)
        6603,   // Attack
        7266,   // Duel
        7267,   // Grovel
    };

    constexpr uint32 kTickMs = 250;             // the class update's step
    constexpr uint32 kMeterSendMs = 250;        // the meters go to the client at most this often

    // Transformations an option ends by its own timer (FusionClassAbilities.cpp kFxRemoveAura):
    // Avatar of Stone, Demon Form, and Tree Form as an earlier build cast it.
    constexpr uint32 kTimedTransforms[] = {4934, 8872, 775};

    // Attribute bits the kit system lifts off the class spells.
    constexpr uint32 kAttrNotInCombat = 0x10000000;    // usable only out of combat
    constexpr uint32 kAttrExFinisher = 0x00500000;     // the combo-point finishers

    uint32 Now() { return WorldTimer::getMSTime(); }
}

FusionClasses& FusionClasses::Instance()
{
    static FusionClasses instance;
    return instance;
}

void FusionClasses::Load()
{
    m_enabled = sFusionMgr.IsEnabled() && sConfig.GetBoolDefault("Fusion.Classes.Enable", true);
    m_rangedSpellPower = sConfig.GetFloatDefault("Fusion.Classes.RangedSpellPower", Fusion::Classes::kRangedSpellPower);
    if (!m_enabled)
        return;
    // The options a character bought (the kits' picks are the kits' own: character_fusion_class).
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_class_option` ("
        "`guid` int(11) unsigned NOT NULL, `option_id` smallint(5) unsigned NOT NULL, "
        "`bought_at` bigint(20) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`guid`, `option_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");

    TweakSpells();

    // Every rank of every option's spells, by class, for the hooks.
    m_spellOption.clear();
    m_triggerOption.clear();
    uint32 spells = 0;
    for (Option const& o : Catalog())
        for (uint32 s : o.spells)
            if (s)
                for (Rank const& r : Chain(s))
                {
                    // A form's ability shared by two forms (the charge) stays the first form's.
                    m_spellOption[o.cls].emplace(r.spell, o.id);
                    ++spells;
                    // The spells it sets off (Holy Shock's smite, Hurricane's pulses, Arcane
                    // Missiles' missiles) deal its damage: the identity pass tunes them as it.
                    if (SpellEntry const* e = sSpellMgr.GetSpellEntry(r.spell))
                        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                            if (uint32 const t = e->EffectTriggerSpell[i])
                                m_triggerOption[o.cls].emplace(t, o.id);
                }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: class framework on, %u options, %u class spell ranks.",
        uint32(Catalog().size()), spells);
    BuildSingleEntries();
    // The spells a hand holds (FusionSpellcasting.cpp), from the ranks just listed.
    FitHandSpells();
}

void FusionClasses::BuildSingleEntries()
{
    // One entry per ability: every ranked chain a class casts (its options and the abilities that
    // come with them, its Utility spells, its basic spell) has each rank superseded by the next in
    // the spellbook (Player::AddSpell): the book holds the highest rank known, the lower ones stay
    // learned beneath it and are sent to no client, and the client moves its bars onto it.
    m_chains.clear();
    m_chainOfRank.clear();
    m_classChains.clear();
    std::unordered_map<uint32, uint32> successors;
    auto add = [&](uint8 cls, uint32 spell)
    {
        if (!spell)
            return;
        std::vector<Rank> chain = Chain(spell);
        if (chain.size() < 2)
            return;
        uint32 index;
        auto known = m_chainOfRank.find(chain.front().spell);
        if (known != m_chainOfRank.end())
            index = known->second;
        else
        {
            index = uint32(m_chains.size());
            for (Rank const& r : chain)
                m_chainOfRank.emplace(r.spell, index);
            for (auto const& step : Successors(chain))
                successors.emplace(step.first, step.second);
            m_chains.push_back(chain);
        }
        std::vector<uint32>& mine = m_classChains[cls];
        if (std::find(mine.begin(), mine.end(), index) == mine.end())
            mine.push_back(index);
    };
    for (Option const& o : Catalog())
        for (uint32 sp : o.spells)
            add(o.cls, sp);
    for (uint8 cls : {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock, kDruid})
    {
        for (uint32 u : UtilityOf(cls))
            add(cls, u);
        add(cls, BasicSpellOf(cls));
    }
    for (Fusion::DruidForms::Form const& f : Fusion::DruidForms::Forms())
    {
        add(kDruid, f.spell);
        for (uint32 a : Fusion::DruidForms::Abilities(f))
            add(kDruid, a);
    }
    uint32 const ranks = uint32(successors.size());
    sSpellMgr.SetFusionSuccessors(std::move(successors));
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u ranked class spells are one entry each (%u ranks beneath them).",
        uint32(m_chains.size()), ranks);
}

std::vector<std::vector<Rank>> FusionClasses::ChainsOf(uint8 cls) const
{
    std::vector<std::vector<Rank>> out;
    auto it = m_classChains.find(cls);
    if (it != m_classChains.end())
        for (uint32 index : it->second)
            out.push_back(m_chains[index]);
    return out;
}

uint32 FusionClasses::SingleEntryOf(Player const* player, uint32 spellId) const
{
    if (!m_enabled || !player)
        return 0;
    auto it = m_chainOfRank.find(spellId);
    if (it == m_chainOfRank.end())
        return 0;
    return SingleEntry(m_chains[it->second], spellId, [&](uint32 s) { return player->HasActiveSpell(s); });
}

std::vector<Rank> FusionClasses::Chain(uint32 spellId) const
{
    std::vector<Rank> out;
    uint32 id = spellId;
    for (int guard = 0; id && guard < 32; ++guard)
    {
        SpellEntry const* e = sSpellMgr.GetSpellEntry(id);
        if (!e)
            break;
        out.push_back({id, uint8(std::min<uint32>(e->spellLevel, 255))});
        id = sSpellMgr.GetPrevSpellInChain(id);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

uint32 FusionClasses::RankFor(uint32 spellId, uint8 level) const
{
    return RankForLevel(Chain(spellId), level);
}

Option const* FusionClasses::DamageOptionOf(uint8 cls, uint32 spellId) const
{
    if (Option const* o = OptionOfSpell(cls, spellId))
        return o;
    auto c = m_triggerOption.find(cls);
    if (c == m_triggerOption.end())
        return nullptr;
    auto it = c->second.find(spellId);
    return it == c->second.end() ? nullptr : FindOption(it->second);
}

Option const* FusionClasses::OptionOfSpell(uint8 cls, uint32 spellId) const
{
    auto c = m_spellOption.find(cls);
    if (c == m_spellOption.end())
        return nullptr;
    auto it = c->second.find(spellId);
    return it == c->second.end() ? nullptr : FindOption(it->second);
}

uint32 FusionClasses::CatalogSpellOf(uint8 cls, uint32 spellId) const
{
    uint32 const first = sSpellMgr.GetFirstSpellInChain(spellId);
    auto same = [&](uint32 s) { return s == spellId || (first && sSpellMgr.GetFirstSpellInChain(s) == first); };
    if (Option const* o = OptionOfSpell(cls, spellId))
        for (uint32 s : o->spells)
            if (s && same(s))
                return s;
    for (uint32 u : UtilityOf(cls))
        if (same(u))
            return u;
    if (uint32 const basic = BasicSpellOf(cls); basic && same(basic))
        return basic;
    return 0;
}

bool FusionClasses::SkipsDefaultSpell(uint8 cls, uint32 spellId) const
{
    if (!m_enabled || kAlwaysKept.count(spellId) || CatalogSpellOf(cls, spellId))
        return false;
    SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
    for (auto it = bounds.first; it != bounds.second; ++it)
        if (kClassSkillLines.count(it->second->skillId))
            return true;
    return false;
}

void FusionClasses::TweakSpells()
{
    // The class spells are fitted to the kit system in memory, once, before anyone casts
    // (Fusion::Classes::Refits): every rank of a chain loses its totem, reagent and spell focus
    // needs, the stance it asks for (a form it may also be cast in stays: Shadowform's spells) and
    // its mana or energy cost (abilities are cooldown-limited; rage stays the warrior's), and takes
    // the catalog's cooldown and cast time. A donor for the look is turned onto its caster and its
    // vanilla effects replaced, for players casting it as an option only (ReplacesEffect).
    m_replaced.clear();
    m_tweaked.clear();
    uint32 changed = 0;
    for (Refit const& t : Refits())
    {
        for (Rank const& r : Chain(t.spell))
        {
            SpellEntry* e = const_cast<SpellEntry*>(sSpellMgr.GetSpellEntry(r.spell));
            if (!e)
                continue;
            // A stance it needs goes; the forms it is also allowed in stay (clearing those would
            // keep Shadow Word: Pain out of Shadowform).
            // A druid form's own ability keeps its form (Prowl only in Cat Form).
            if (!(e->AttributesEx2 & SPELL_ATTR_EX2_ALLOW_WHILE_NOT_SHAPESHIFTED) && !(t.flags & REFIT_KEEP_STANCE))
                e->Stances = 0;
            e->StancesNot = 0;
            if (t.flags & REFIT_NOT_NEXT_SWING)     // the custom part strikes at once (Maul)
                e->Attributes &= ~uint32(SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE | SPELL_ATTR_ON_NEXT_SWING);
            e->RequiresSpellFocus = 0;
            for (uint32& totem : e->Totem)
                totem = 0;
            if (!(t.flags & REFIT_KEEP_REAGENT))
                for (int i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    e->Reagent[i] = 0;
                    e->ReagentCount[i] = 0;
                }
            if (!(t.flags & REFIT_KEEP_COST) && (e->powerType != POWER_RAGE || (t.flags & REFIT_FREE)))
            {
                e->manaCost = 0;
                e->manaCostPerlevel = 0;
                e->manaPerSecond = 0;
                e->manaPerSecondPerLevel = 0;
                e->ManaCostPercentage = 0;
            }
            if (t.cooldownMs >= 0)
            {
                e->RecoveryTime = uint32(t.cooldownMs);
                e->CategoryRecoveryTime = 0;
            }
            if (t.castIndex > 0)
                e->CastingTimeIndex = uint32(t.castIndex);
            if (t.rangeIndex > 0)
                e->rangeIndex = uint32(t.rangeIndex);
            if (t.flags & REFIT_IN_COMBAT)
                e->Attributes &= ~kAttrNotInCombat;
            if (t.flags & REFIT_NO_COMBO)
                e->AttributesEx &= ~kAttrExFinisher;
            if (t.flags & REFIT_NOT_CHANNEL)    // a replaced donor's channel would only hold its caster
                e->AttributesEx &= ~uint32(SPELL_ATTR_EX_IS_CHANNELED | SPELL_ATTR_EX_IS_SELF_CHANNELED);
            if (t.flags & REFIT_NO_DRAIN)
                e->AttributesEx &= ~uint32(SPELL_ATTR_EX_USE_ALL_MANA);
            if (t.flags & REFIT_ANY_TARGET)
                e->TargetCreatureType = 0;
            if (t.flags & REFIT_NO_AURA_STATE)
            {
                e->CasterAuraState = 0;
                e->TargetAuraState = 0;
            }
            if (t.flags & REFIT_NO_WEAPON)
            {
                e->EquippedItemClass = -1;
                e->EquippedItemSubClassMask = 0;
                e->EquippedItemInventoryTypeMask = 0;
            }
            if (t.flags & REFIT_SELF)
            {
                e->Targets = 0;
                e->rangeIndex = 1;
                e->TargetCreatureType = 0;
                for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (e->Effect[i])
                    {
                        e->EffectImplicitTargetA[i] = TARGET_UNIT_CASTER;
                        e->EffectImplicitTargetB[i] = 0;
                    }
            }
            if (t.flags & REFIT_AT_FEET)
            {
                // A ground-targeted spell (Flare, Hurricane) lands where its caster stands: no
                // cursor to wait on, which the hands' crosshair could never click.
                e->Targets &= ~uint32(TARGET_FLAG_DEST_LOCATION);
                e->rangeIndex = 1;
                for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (e->Effect[i] && e->EffectImplicitTargetA[i] != TARGET_LOCATION_CASTER_DEST)
                    {
                        e->EffectImplicitTargetB[i] = e->EffectImplicitTargetA[i];
                        e->EffectImplicitTargetA[i] = TARGET_LOCATION_CASTER_DEST;
                    }
            }
            m_tweaked[r.spell] = t.flags;
            if (uint8 const mask = ReplaceMask(t.flags))
                m_replaced[r.spell] = mask;
            ++changed;
        }
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u class spell ranks fitted to the kit system.", changed);
    FitDruidForms();
}

void FusionClasses::FitDruidForms()
{
    // The new forms' donors were transforms (Reindeer, Owl Form, Mar'li Transform, Arugal's
    // Curse): each becomes one MOD_SHAPESHIFT aura into its form, positive and cancelable, so it
    // enters and leaves as a stock form does.
    namespace DF = Fusion::DruidForms;
    uint32 fitted = 0;
    for (DF::Form const& f : DF::Forms())
    {
        if (!f.fit)
            continue;
        SpellEntry* e = const_cast<SpellEntry*>(sSpellMgr.GetSpellEntry(f.spell));
        if (!e)
            continue;
        DF::FittedSpell const s = DF::FitFormSpell(f);
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            DF::FittedEffect const& x = s.effects[i];
            e->Effect[i] = x.effect;
            e->EffectApplyAuraName[i] = x.aura;
            e->EffectMiscValue[i] = x.misc;
            e->EffectBasePoints[i] = x.basePoints;
            e->EffectBaseDice[i] = x.effect ? 1 : 0;
            e->EffectDieSides[i] = x.effect ? 1 : 0;
            e->EffectDicePerLevel[i] = 0.f;
            e->EffectRealPointsPerLevel[i] = 0.f;
            e->EffectImplicitTargetA[i] = x.target;
            e->EffectImplicitTargetB[i] = 0;
            e->EffectRadiusIndex[i] = 0;
            e->EffectAmplitude[i] = 0;
            e->EffectMechanic[i] = 0;
            e->EffectTriggerSpell[i] = 0;
            e->EffectChainTarget[i] = 0;
            e->EffectItemType[i] = 0;
        }
        e->Attributes = s.attributes;
        e->AttributesEx = s.attributesEx;
        e->AttributesEx2 = 0;
        e->AttributesEx3 = 0;
        e->AttributesEx4 = 0;
        e->Stances = 0;
        e->StancesNot = 0;
        e->Targets = 0;
        e->rangeIndex = 1;
        e->CastingTimeIndex = 1;
        e->DurationIndex = s.durationIndex;
        e->AuraInterruptFlags = s.auraInterruptFlags;
        e->InterruptFlags = 0;
        e->ChannelInterruptFlags = 0;
        e->activeIconID = s.activeIconId;
        e->Dispel = s.dispel;
        e->Mechanic = s.mechanic;
        e->procFlags = 0;
        e->procChance = 0;
        e->StartRecoveryCategory = 133;     // the global cooldown, as the stock forms
        e->StartRecoveryTime = 1500;
        e->Custom |= SPELL_CUSTOM_POSITIVE;
        e->Custom &= ~SPELL_CUSTOM_NEGATIVE;
        e->Internal |= SPELL_INTERNAL_POSITIVE | SPELL_INTERNAL_APPLIES_AURA;
        e->Internal &= ~SPELL_INTERNAL_APPLIES_PERIODIC_AURA;
        // The kit system's own pass may have marked it replaced (an old flag): never now.
        m_replaced.erase(f.spell);
        ++fitted;
        // A new form has no rage: its abilities borrowed from the bear (Feral Charge, the stag's
        // charge and the worgen's leap) cost nothing, or they could never be used.
        for (uint32 ability : DF::Abilities(f))
            for (Rank const& r : Chain(ability))
                if (SpellEntry* a = const_cast<SpellEntry*>(sSpellMgr.GetSpellEntry(r.spell));
                    a && a->powerType == POWER_RAGE)
                {
                    a->powerType = POWER_MANA;
                    a->manaCost = 0;
                    a->manaCostPerlevel = 0;
                    a->ManaCostPercentage = 0;
                }
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u druid form spells fitted as shapeshifts.", fitted);
}

FusionClassState& FusionClasses::StateOf(Player* player)
{
    std::shared_ptr<FusionClassState>& p = player->GetFusion().classFramework;
    if (!p)
        p = std::make_shared<FusionClassState>();
    return *p;
}

FusionClassState const* FusionClasses::StateOf(Player const* player) const
{
    return player->HasFusion() ? player->GetFusionConst().classFramework.get() : nullptr;
}

void FusionClasses::LoadState(Player* player)
{
    FusionClassState& s = StateOf(player);
    s.learned.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `option_id` FROM `character_fusion_class_option` WHERE `guid` = %u", player->GetGUIDLow()))
    {
        do
        {
            uint16 const id = r->Fetch()[0].GetUInt16();
            if (Option const* o = FindOption(id); o && o->cls == player->GetClass())
                s.learned.insert(id);
        } while (r->NextRow());
    }
    s.loaded = true;
}

std::set<uint16> const& FusionClasses::Learned(Player* player)
{
    FusionClassState& s = StateOf(player);
    if (!s.loaded)
        LoadState(player);
    return s.learned;
}

PickError FusionClasses::ValidatePicks(Player* player, Picks const& picks, uint8* badTier)
{
    if (!m_enabled)
    {
        // No class framework: a kit holds no picks.
        for (uint8 t = 0; t < kTiers; ++t)
            if (picks[t])
            {
                if (badTier)
                    *badTier = uint8(t + 1);
                return PickError::UnknownOption;
            }
        return PickError::None;
    }
    return Fusion::Classes::ValidatePicks(player->GetClass(), uint8(std::min<uint32>(player->GetLevel(), 255)),
        Learned(player), picks, badTier, sFusionMgr.IsAdmin(player));
}

bool FusionClasses::SetKitPicks(Player* player, Picks const& picks)
{
    FusionPlayerData& d = player->GetFusion();
    uint8 const slot = d.equippedSlot;
    if (slot >= Fusion::kClassSlots || !d.classes[slot])
        return false;
    d.classes[slot]->talentPicks = picks;
    if (d.applied)
        d.applied->talentPicks = picks;
    CharacterDatabase.PExecute(
        "UPDATE `character_fusion_class` SET `talent1` = %u, `talent2` = %u, `talent3` = %u, `talent4` = %u, "
        "`talent5` = %u, `talent6` = %u WHERE `guid` = %u AND `slot` = %u",
        picks[0], picks[1], picks[2], picks[3], picks[4], picks[5], player->GetGUIDLow(), uint32(slot));
    // The kit editor has the kit as the server keeps it.
    sFusionMgr.SendLoadout(player, Fusion::Packets::LoadoutStatus::Applied);
    return true;
}

void FusionClasses::Apply(Player* player, bool layOutBar)
{
    FusionClassState& s = StateOf(player);
    uint8 const cls = player->GetClass();
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    if (!IsPlayableClass(cls))
        return;

    // What the kit wants known: every rank up to the level's of each chain.
    std::unordered_set<uint32> keep;
    std::vector<uint32> learn;
    auto want = [&](uint32 spell, bool gateByLevel)
    {
        std::vector<Rank> const chain = Chain(spell);
        if (chain.empty() || (gateByLevel && chain.front().level > level))
            return;
        uint32 const rank = RankForLevel(chain, level);
        for (Rank const& r : chain)
        {
            keep.insert(r.spell);
            if (r.spell == rank)
                break;
        }
        learn.push_back(rank);
    };
    for (Option const* o : KitOptions(cls, s.picks))
        if (LearnsSpells(*o))
            for (uint32 sp : o->spells)
                if (sp)
                    want(sp, false);
    for (uint32 u : UtilityOf(cls))
        if (player->IsSpellFitByClassAndRace(u))
            // A spray deals class damage (the same at every rank): from level 1, at its first rank.
            want(u, !Fusion::Spellcasting::SprayOf(sSpellMgr.GetFirstSpellInChain(u) ? sSpellMgr.GetFirstSpellInChain(u) : u));
    // The basic damaging spell: from level 1, its first rank even before that rank's level.
    if (uint32 const basic = BasicSpellOf(cls))
        want(basic, false);

    // The old spellbook goes: class lines and talents outside the kit. A stock talent spent
    // before the kits (or by any other way in) is one of these: removing it frees its points
    // (Player::RemoveSpell), and with no talent tree there is nothing to spend them on.
    std::vector<uint32> remove;
    uint32 talentRanks = 0;
    for (auto const& [id, ps] : player->GetSpellMap())
    {
        if (ps.state == PLAYERSPELL_REMOVED || keep.count(id) || kAlwaysKept.count(id))
            continue;
        SpellFacts f;
        f.talent = GetTalentSpellPos(id) != nullptr;
        SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(id);
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (kClassSkillLines.count(it->second->skillId))
                f.classSkillLine = true;
        // An option's spell of another rank or out of the kit (a donor from outside the lines), or
        // a donor an earlier build used.
        f.kit = false;
        std::vector<uint32> const& retired = RetiredSpells(cls);
        if (!f.classSkillLine
            && (OptionOfSpell(cls, id) || std::find(retired.begin(), retired.end(), id) != retired.end()))
            f.classSkillLine = true;
        if (!KeepSpell(f))
        {
            remove.push_back(id);
            if (f.talent)
                ++talentRanks;
        }
    }
    for (uint32 id : remove)
        player->RemoveSpell(id, false, false);
    if (talentRanks)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %s had %u stock talent rank(s) outside the kit; removed.",
            player->GetName(), talentRanks);
    for (uint32 id : learn)
        if (!player->HasSpell(id))
            player->LearnSpell(id, false);

    // Homing is a feature of a kit pick, never the default (fusion/ClassTuning.h): the picks
    // that grant it make their spell's bolts home for as long as they are in the kit.
    for (Option const* o : OptionsOf(cls))
        if (Fusion::ClassTuning::GrantsHoming(o->id) && o->spells[0])
        {
            uint32 const chain = sSpellMgr.GetFirstSpellInChain(o->spells[0]) ? sSpellMgr.GetFirstSpellInChain(o->spells[0]) : o->spells[0];
            if (InKit(cls, s.picks, o->id))
                GrantHoming(player, chain, Fusion::Spellcasting::kGrantedHoming, 0);
            else
                RevokeHoming(player, chain);
        }

    uint8 const wasLevel = s.appliedLevel;
    bool const first = !s.applied;
    // A timed transformation outlives its timer when its player logs out in it (the aura is
    // saved, the timer is not): a fresh login takes it off rather than leave the look for good.
    if (first && s.effects.empty())
        for (uint32 aura : kTimedTransforms)
            if (player->HasAura(aura))
                player->RemoveAurasDueToSpell(aura);
    s.appliedLevel = level;
    s.applied = true;
    bool const migrated = MigrateRetiredBar(player);
    if (layOutBar || migrated || wasLevel != level || BarNeedsLayout(player))
        LayOutBar(player);
    RebuildMeters(player, s);
    if (first)
    {
        SendSpellTweaks(player);
        SendForms(player);
        SendHandSpells(player);
    }
    if (first || wasLevel != level)
        SendPage(player);
    SendState(player);
}

void FusionClasses::SendSpellTweaks(Player* player)
{
    auto c = m_spellOption.find(player->GetClass());
    if (c == m_spellOption.end())
        return;
    // The options' spells, the basic spell's ranks and every ranked chain of the class (its
    // Utility's too: their ranks are one entry, so their "Rank N" goes).
    std::set<uint32> spells;
    for (auto const& [spell, option] : c->second)
        spells.insert(spell);
    if (uint32 const basic = BasicSpellOf(player->GetClass()))
        for (Rank const& r : Chain(basic))
            spells.insert(r.spell);
    for (std::vector<Rank> const& chain : ChainsOf(player->GetClass()))
        for (Rank const& r : chain)
            spells.insert(r.spell);
    std::vector<SpellTweak> tweaks;
    for (uint32 spell : spells)
    {
        auto t = m_tweaked.find(spell);
        SpellEntry const* e = sSpellMgr.GetSpellEntry(spell);
        bool const ranked = m_chainOfRank.count(spell) != 0;
        if (!e || (t == m_tweaked.end() && !ranked))
            continue;
        if (t == m_tweaked.end())
        {
            // A ranked spell the kit system leaves as it is: only its rank's name goes.
            SpellTweak w;
            w.spell = spell;
            w.attributes = e->Attributes;
            w.attributesEx = e->AttributesEx;
            w.stances = e->Stances;
            w.recoveryMs = e->RecoveryTime;
            w.categoryRecoveryMs = e->CategoryRecoveryTime;
            w.castIndex = uint16(e->CastingTimeIndex);
            w.rangeIndex = uint16(e->rangeIndex);
            w.flags = TWEAK_NO_RANK;
            tweaks.push_back(w);
            continue;
        }
        SpellTweak w;
        w.spell = spell;
        w.attributes = e->Attributes;
        w.attributesEx = e->AttributesEx;
        w.stances = e->Stances;
        w.recoveryMs = e->RecoveryTime;
        w.categoryRecoveryMs = e->CategoryRecoveryTime;
        w.castIndex = uint16(e->CastingTimeIndex);
        w.rangeIndex = uint16(e->rangeIndex);
        uint32 const f = t->second;
        // What the client's copy must drop to gate the spell as the server does.
        w.flags = uint8((f & REFIT_KEEP_REAGENT ? 0 : TWEAK_NO_REAGENTS)
            | (e->manaCost || e->ManaCostPercentage ? 0 : TWEAK_NO_COST)
            | (f & (REFIT_SELF | REFIT_AT_FEET) ? TWEAK_ON_SELF : 0)
            // A new form's donor wanted a target (Arugal's Curse): its form is cast on oneself.
            | (Fusion::DruidForms::IsFitted(spell) ? TWEAK_ON_SELF : 0)
            | (f & REFIT_NO_WEAPON ? TWEAK_NO_ITEM : 0)
            | (e->TargetCreatureType ? 0 : TWEAK_ANY_CREATURE)
            | (e->CasterAuraState || e->TargetAuraState ? 0 : TWEAK_NO_AURA_STATE)
            | TWEAK_NO_FOCUS
            | (ranked ? TWEAK_NO_RANK : 0));
        tweaks.push_back(w);
    }
    for (std::vector<uint8> const& body : EncodeSpellTweaks(tweaks, Fusion::Packets::kMaxChannelBody))
        sFusionMgr.SendChannel(player, kChannelClasses, body);
}

void FusionClasses::SendPage(Player* player)
{
    // The class's catalog for the talent page, each option shown by the rank it has (or will
    // have at its tier).
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    Page page = MakePage(player->GetClass());
    for (PageEntry& e : page.entries)
        if (Option const* o = FindOption(e.id); o && LearnsSpells(*o))
            e.spell = RankFor(o->spells[0], std::max(level, TierLevel(o->tier)));
    sFusionMgr.SendChannel(player, kChannelClasses, EncodePage(page));
}

bool FusionClasses::BarNeedsLayout(Player* player) const
{
    // A character never laid out has its old first button (Attack, Heroic Strike...), not the
    // first core ability.
    MasterPlayer* mp = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
    std::array<Option const*, 3> const core = CoreOf(player->GetClass());
    if (!mp || !core[0])
        return false;
    ActionButtonList& buttons = mp->GetActionButtons();
    // The button holds the catalog spell `want` (any rank).
    auto holds = [&](uint8 slot, uint32 want)
    {
        auto it = buttons.find(slot);
        return it != buttons.end() && it->second.uState != ACTIONBUTTON_DELETED
            && it->second.GetType() == ACTION_BUTTON_SPELL
            && CatalogSpellOf(player->GetClass(), it->second.GetAction()) == want;
    };
    if (!holds(0, core[0]->spells[0]))
        return true;
    // A caster laid out before the basic spell came has a pick in its slot.
    uint32 const basic = BasicSpellOf(player->GetClass());
    if (basic && !holds(3, basic))
        return true;
    // A druid form known whose own page was never laid out (a character from before the pages).
    if (player->GetClass() == CLASS_DRUID)
    {
        uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
        for (Fusion::DruidForms::Form const& f : Fusion::DruidForms::Forms())
        {
            uint32 const first = Fusion::DruidForms::BarLayout(f)[0];
            if (!player->HasSpell(RankFor(f.spell, level)) || !first || !player->HasSpell(RankFor(first, level)))
                continue;
            auto page = buttons.find(uint8(Fusion::DruidForms::FirstSlotOfBar(f.bonusBar)));
            if (page == buttons.end() || page->second.uState == ACTIONBUTTON_DELETED)
                return true;
        }
    }
    return false;
}

bool FusionClasses::MigrateRetiredBar(Player* player)
{
    // A character saved before a donor retired (a Bard's instruments, the old Backstab...): its
    // buttons get the option that took the tier's place when known, else they are cleared. The
    // kit's picks need nothing: the Duelist kept the Bard's ids (Classes.h MigrateRetiredButtons).
    MasterPlayer* mp = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
    if (!mp || RetiredSpells(player->GetClass()).empty())
        return false;
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    ActionButtonList& buttons = mp->GetActionButtons();
    std::vector<std::pair<uint8, uint32>> spellButtons;
    for (auto const& [button, ab] : buttons)
        if (ab.uState != ACTIONBUTTON_DELETED && ab.GetType() == ACTION_BUTTON_SPELL)
            spellButtons.push_back({button, ab.GetAction()});
    auto topOf = [&](uint32 spell)
    {
        std::vector<Rank> const chain = Chain(spell);
        return chain.empty() ? spell : chain.back().spell;
    };
    auto known = [&](uint32 spell) { return player->HasSpell(RankFor(spell, level)); };
    std::vector<std::pair<uint8, uint32>> const moves = MigrateRetiredButtons(player->GetClass(), spellButtons, topOf, known);
    for (auto const& [button, spell] : moves)
    {
        if (spell)
            mp->addActionButton(button, RankFor(spell, level), ACTION_BUTTON_SPELL);
        else
            mp->removeActionButton(button);
    }
    if (!moves.empty())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %s had %u bar button(s) on retired class spells; moved on.",
            player->GetName(), uint32(moves.size()));
    return !moves.empty();
}

void FusionClasses::LayOutBar(Player* player)
{
    MasterPlayer* mp = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
    if (!mp)
        return;
    FusionClassState& s = StateOf(player);
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    std::array<uint32, 12> const bar = BarLayout(player->GetClass(), s.picks);
    ActionButtonList& buttons = mp->GetActionButtons();
    // The first page is the kit's (the hands mode shows it): its buttons, empty where it has none.
    for (uint8 i = 0; i < bar.size(); ++i)
    {
        uint32 const spell = bar[i] ? RankFor(bar[i], level) : 0;
        if (spell && player->HasSpell(spell))
            mp->addActionButton(i, spell, ACTION_BUTTON_SPELL);
        else
        {
            auto it = buttons.find(i);
            if (it != buttons.end() && it->second.uState != ACTIONBUTTON_DELETED
                && it->second.GetType() == ACTION_BUTTON_SPELL)
                mp->removeActionButton(i);
        }
    }
    // Elsewhere: a class spell's button holds its single entry (any older rank is moved onto it:
    // a bar saved before the ranks were one entry, or before a level-up); an old class spell no
    // longer known leaves its button.
    std::vector<std::pair<uint8, uint32>> spellButtons;
    for (auto const& [button, ab] : buttons)
        if (button >= bar.size() && ab.uState != ACTIONBUTTON_DELETED && ab.GetType() == ACTION_BUTTON_SPELL)
            spellButtons.push_back({button, ab.GetAction()});
    std::vector<std::pair<uint8, uint32>> const upgrade = CollapseBar(spellButtons, ChainsOf(player->GetClass()),
        level, [&](uint32 s) { return player->HasActiveSpell(s); });
    std::vector<uint8> drop;
    for (auto const& [button, spell] : spellButtons)
    {
        bool const moved = std::any_of(upgrade.begin(), upgrade.end(),
            [&](std::pair<uint8, uint32> const& m) { return m.first == button; });
        if (!moved && !player->HasActiveSpell(spell) && !SingleEntryOf(player, spell))
            drop.push_back(button);
    }
    for (auto const& [button, rank] : upgrade)
        mp->addActionButton(button, rank, ACTION_BUTTON_SPELL);
    for (uint8 button : drop)
        mp->removeActionButton(button);
    LayOutFormBars(player, true);
    mp->SendInitialActionButtons();
}

void FusionClasses::LayOutFormBars(Player* player, bool force)
{
    // Each form the character knows has its own page (its bonus bar, as cat and bear in 1.12):
    // its abilities first, then Barkskin. `force` lays the page out; otherwise only empty slots.
    namespace DF = Fusion::DruidForms;
    MasterPlayer* mp = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
    if (!mp || player->GetClass() != CLASS_DRUID)
        return;
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    ActionButtonList& buttons = mp->GetActionButtons();
    for (DF::Form const& f : DF::Forms())
    {
        if (!player->HasSpell(RankFor(f.spell, level)) && !player->HasSpell(f.spell))
            continue;
        std::array<uint32, DF::kSlotsPerPage> const bar = DF::BarLayout(f);
        for (uint8 i = 0; i < bar.size(); ++i)
        {
            uint32 const spell = bar[i] ? RankFor(bar[i], level) : 0;
            if (!spell || !player->HasSpell(spell))
                continue;
            uint8 const button = uint8(DF::FirstSlotOfBar(f.bonusBar) + i);
            auto it = buttons.find(button);
            bool const empty = it == buttons.end() || it->second.uState == ACTIONBUTTON_DELETED;
            if (force || empty)
                mp->addActionButton(button, spell, ACTION_BUTTON_SPELL);
        }
    }
}

void FusionClasses::SendForms(Player* player)
{
    if (player->GetClass() != CLASS_DRUID)
        return;
    namespace DF = Fusion::DruidForms;
    sFusionMgr.SendChannel(player, kChannelClasses, DF::EncodeForms(DF::WireForms()));
}

void FusionClasses::RebuildMeters(Player* player, FusionClassState& s)
{
    std::vector<Meter> next;
    for (Resource r : KitResources(player->GetClass(), s.picks))
    {
        Meter m;
        m.resource = r;
        m.max = ResourceMax(r, player->GetClass(), s.picks);
        bool had = false;
        for (Meter const& old : s.meters)
            if (old.resource == r)
            {
                m.value = std::min(old.value, m.max);
                had = true;
            }
        (void)had;
        next.push_back(m);
    }
    s.meters = std::move(next);
    s.metersDirty = true;
}

void FusionClasses::Update(Player* player, uint32 diffMs)
{
    (void)diffMs;
    if (!m_enabled || !player->IsInWorld() || !IsPlayableClass(player->GetClass()))
        return;
    FusionClassState& s = StateOf(player);
    uint32 const now = Now();
    if (!s.loaded)
        LoadState(player);
    // The picks in play are the kit in hand's (a kit switch waits out combat in FusionMgr).
    FusionPlayerData const& d = player->GetFusionConst();
    Fusion::ClassLoadout const* kit = d.Equipped();
    Picks const wanted = kit ? Sanitized(player->GetClass(), uint8(std::min<uint32>(player->GetLevel(), 255)),
        s.learned, kit->talentPicks, sFusionMgr.IsAdmin(player)) : Picks{};
    uint8 const slot = kit ? d.equippedSlot : 255;
    if (!s.applied || wanted != s.picks || slot != s.kit)
    {
        bool const changed = s.applied && wanted != s.picks;
        s.picks = wanted;
        s.kit = slot;
        Apply(player, changed);
    }
    else if (s.appliedLevel != player->GetLevel())
        Apply(player, false);
    if (!s.pendingMelee.empty())
        RunMeleeProcs(player, s);
    // Hand spells' bolts fly every update, not at the effects' pace.
    if (!s.handBolts.empty())
        TickHandBolts(player, s, now);
    // A spray ticks at its own pace (a quarter second), every update.
    if (s.handSprays[0].spell || s.handSprays[1].spell)
        TickHandSprays(player, s, now);
    if (!s.cooldownSync.empty())
        SyncCooldowns(player, s);
    if (int32(now - s.nextTickMs) >= 0)
    {
        s.nextTickMs = now + kTickMs;
        TickEffects(player, s, now);
        TickZones(player, s, now);
        TickMinions(player, s, now);
        TickHandZones(player, s, now);
    }
    if (s.metersDirty && int32(now - s.nextMeterMs) >= 0)
    {
        s.nextMeterMs = now + kMeterSendMs;
        s.metersDirty = false;
        SendMeters(player);
    }
    float const speed = SpeedMultiplier(player);
    if (speed != s.speed)
    {
        s.speed = speed;
        player->UpdateSpeed(MOVE_RUN, false);
        player->UpdateSpeed(MOVE_SWIM, false);
    }
}

void FusionClasses::SyncCooldowns(Player* player, FusionClassState& s)
{
    // The client times a cast's cooldown from its own Spell.dbc at SMSG_SPELL_GO; the server's
    // differs for the kit's spells. Sent an update later, after the cast's SMSG_SPELL_GO: clear
    // the client's and give it the server's.
    WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + s.cooldownSync.size() * 8);
    data << player->GetObjectGuid();
    uint32 count = 0;
    for (auto const& [spell, forced] : s.cooldownSync)
    {
        SpellEntry const* e = sSpellMgr.GetSpellEntry(spell);
        if (!e)
            continue;
        player->SendClearCooldown(spell, player);
        uint32 const ms = forced ? forced : std::max(e->RecoveryTime, e->CategoryRecoveryTime);
        if (!ms || player->IsSpellReady(e))
            continue;
        data << uint32(spell) << uint32(ms);
        ++count;
    }
    s.cooldownSync.clear();
    if (count)
        player->GetSession()->SendPacket(&data);
}

void FusionClasses::SendState(Player* player)
{
    FusionClassState& s = StateOf(player);
    State st;
    st.cls = player->GetClass();
    st.level = uint8(std::min<uint32>(player->GetLevel(), 255));
    st.kit = s.kit;
    bool const admin = sFusionMgr.IsAdmin(player);
    st.flags = (Fusion::CanChangeKits(sFusionMgr.KitAccessNow(player)) ? STATE_CAN_EDIT : 0) | (admin ? STATE_ADMIN : 0);
    st.picks = s.picks;
    st.learned.assign(s.learned.begin(), s.learned.end());
    sFusionMgr.SendChannel(player, kChannelClasses, EncodeState(st));
}

void FusionClasses::SendMeters(Player* player)
{
    sFusionMgr.SendChannel(player, kChannelClasses, EncodeMeters(StateOf(player).meters));
}

void FusionClasses::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    if (!m_enabled || body.empty() || !IsPlayableClass(player->GetClass()))
        return;
    if (!StateOf(player).loaded)
        LoadState(player);
    if (body[0] == kOpAskState)
    {
        SendPage(player);
        SendState(player);
        SendMeters(player);
    }
    else if (body[0] == Fusion::Spellcasting::kOpHandCast || body[0] == Fusion::Spellcasting::kOpHandAim)
        HandCast(player, body);
    else if (body[0] == Fusion::Spellcasting::kOpSprayAim)
        HandSpray(player, body);
}

bool FusionClasses::SendTrainerList(Player* player, Creature* trainer)
{
    if (!m_enabled || !trainer || !IsPlayableClass(player->GetClass()))
        return false;
    CreatureInfo const* ci = trainer->GetCreatureInfo();
    if (!ci || ci->trainer_type != TRAINER_TYPE_CLASS || ci->trainer_class != player->GetClass())
        return false;
    FusionClassState& s = StateOf(player);
    if (!s.loaded)
        LoadState(player);
    uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
    std::vector<Option const*> const options = OptionsOf(player->GetClass());
    std::string const title = "Each option is bought once and grows with you. Pick your six under Edit your kits.";
    WorldPacket data(SMSG_TRAINER_LIST, 8 + 4 + 4 + options.size() * 38 + title.size() + 1);
    data << trainer->GetObjectGuid();
    data << uint32(TRAINER_TYPE_CLASS);
    data << uint32(options.size());
    for (Option const* o : options)
    {
        uint8 const tierLevel = TierLevel(o->tier);
        // The rank the character will have (at least the tier's): the tooltip of what is bought.
        uint32 const shown = LearnsSpells(*o) ? RankFor(o->spells[0], std::max(level, tierLevel)) : o->spells[0];
        TrainerSpellState const state = s.learned.count(o->id) ? TRAINER_SPELL_GRAY
            : level < tierLevel || !Available(o->id) ? TRAINER_SPELL_RED : TRAINER_SPELL_GREEN;
        data << uint32(shown);
        data << uint8(state);
        data << uint32(TierPrice(o->tier));
        data << uint32(0) << uint32(0);
        data << uint8(tierLevel);
        data << uint32(0) << uint32(0);     // no skill
        data << uint32(0) << uint32(0);     // no ability before it
        data << uint32(0);
    }
    data << title;
    player->GetSession()->SendPacket(&data);
    return true;
}

bool FusionClasses::BuyFromTrainer(Player* player, Creature* trainer, uint32 spellId)
{
    if (!m_enabled || !trainer || !IsPlayableClass(player->GetClass()))
        return false;
    CreatureInfo const* ci = trainer->GetCreatureInfo();
    if (!ci || ci->trainer_type != TRAINER_TYPE_CLASS || ci->trainer_class != player->GetClass())
        return false;
    FusionClassState& s = StateOf(player);
    if (!s.loaded)
        LoadState(player);
    WorldSession* session = player->GetSession();
    Option const* o = OptionOfSpell(player->GetClass(), spellId);
    if (!o)
        for (Option const* x : OptionsOf(player->GetClass()))
            if (x->spells[0] == spellId)
                o = x;
    if (!o || o->tier == kCoreTier)
    {
        session->SendTrainingFailure(trainer->GetObjectGuid(), spellId, TRAIN_FAIL_UNAVAILABLE);
        return true;
    }
    BuyError const e = CanBuy(player->GetClass(), uint8(std::min<uint32>(player->GetLevel(), 255)),
        player->GetMoney(), s.learned, o->id);
    if (e != BuyError::None)
    {
        session->SendTrainingFailure(trainer->GetObjectGuid(), spellId,
            e == BuyError::NoMoney ? TRAIN_FAIL_NOT_ENOUGH_MONEY : TRAIN_FAIL_UNAVAILABLE);
        return true;
    }
    player->ModifyMoney(-int32(TierPrice(o->tier)));
    GrantOption(player, o->id);
    session->SendTrainingSuccess(trainer->GetObjectGuid(), spellId);
    SendTrainerList(player, trainer);
    return true;
}

bool FusionClasses::GrantOption(Player* player, uint16 id)
{
    Option const* o = FindOption(id);
    if (!o || o->cls != player->GetClass() || o->tier == kCoreTier)
        return false;
    FusionClassState& s = StateOf(player);
    if (!s.loaded)
        LoadState(player);
    if (!s.learned.insert(id).second)
        return false;
    CharacterDatabase.PExecute(
        "REPLACE INTO `character_fusion_class_option` (`guid`, `option_id`, `bought_at`) VALUES (%u, %u, %u)",
        player->GetGUIDLow(), uint32(id), uint32(time(nullptr)));
    // An empty tier of the kit in hand takes it at once (a trainer is a kit station); otherwise it
    // waits for a kit edit.
    FusionPlayerData const& d = player->GetFusionConst();
    Picks picks = d.Equipped() ? d.Equipped()->talentPicks : Picks{};
    bool const slotted = d.Equipped() && player->GetLevel() >= TierLevel(o->tier) && AutoSlot(picks, *o)
        && SetKitPicks(player, picks);
    ChatHandler(player).PSendSysMessage("|cffffd200%s|r learned (%s, tier %u).%s", o->name,
        ArchetypeName(o->cls, o->archetype), uint32(o->tier),
        slotted ? " It is in your kit." : " Pick it under Edit your kits.");
    SendState(player);
    return true;
}

void FusionClasses::ForgetOptions(Player* player)
{
    FusionClassState& s = StateOf(player);
    s.learned.clear();
    CharacterDatabase.PExecute("DELETE FROM `character_fusion_class_option` WHERE `guid` = %u", player->GetGUIDLow());
    s.applied = false;      // the next update drops the picks no longer bought
}

SpellCastResult FusionClasses::CheckCast(Spell* spell)
{
    Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
    if (!m_enabled || !player || spell->IsTriggered())
        return SPELL_CAST_OK;
    Option const* o = OptionOfSpell(player->GetClass(), spell->m_spellInfo->Id);
    if (!o)
        return SPELL_CAST_OK;
    FusionClassState& s = StateOf(player);
    // Paladin: Rekindle is Redemption cast in combat in 2 s; without it, Redemption is the
    // out-of-combat Utility spell.
    if (o->id == OptionId(kPaladin, 4, 2) && !InKit(kPaladin, s.picks, o->id) && player->IsInCombat())
        return SPELL_FAILED_AFFECTING_COMBAT;
    // Assassin: an opener needs stealth or the opening (Ambush also the target's back, unless a
    // Distract has it looking away). Cast from stealth, the opening starts with it.
    if (IsOpener(o->id))
    {
        uint32 const now = WorldTimer::getMSTime();
        Unit* target = spell->m_targets.getUnitTarget();
        bool const stealthed = player->HasAuraType(SPELL_AURA_MOD_STEALTH);
        bool const behind = target && target != player && player->IsBehindTarget(target, false);
        switch (CanOpen(o->id, stealthed, HasOpening(s, now), behind, HasFlank(s, now)))
        {
            case OpenerVerdict::NeedsStealth:
                ChatHandler(player).PSendSysMessage("|cffff6060%s opens a fight: from stealth or the opening.|r", o->name);
                return SPELL_FAILED_ONLY_STEALTHED;
            case OpenerVerdict::NeedsBehind:
                return SPELL_FAILED_NOT_BEHIND;
            default:
                break;
        }
        if (stealthed)
            OpenNow(player, s);
    }
    // What it needs of a kept resource (its audit row's cost: the Necromancer's Life Force).
    std::pair<Resource, uint16> const cost = CastCost(o->id);
    if (cost.first != Resource::None)
    {
        Meter* m = MeterOf(s, cost.first);
        if (!m || m->value < cost.second)
        {
            ChatHandler(player).PSendSysMessage("|cffff6060%s needs %u %s.|r", o->name, uint32(cost.second),
                ResourceName(cost.first));
            return SPELL_FAILED_NOT_READY;
        }
    }
    return SPELL_CAST_OK;
}

int32 FusionClasses::CastTime(Spell* spell, int32 castTime)
{
    // A hand's bolt landing: it flew already, it lands now.
    if (spell->m_fusionArrived)
        return 0;
    Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
    if (!m_enabled || !player || !player->HasFusion() || !player->GetFusion().classFramework)
        return castTime;
    // A hand casts what it charged at once (FusionSpellcasting.cpp): the charge was its cast time.
    if (StateOf(player).handCasting == spell->m_spellInfo->Id)
        return 0;
    // Light Fingers: Pick Lock (the rogue's Utility) is instant.
    if (player->GetClass() == CLASS_ROGUE && sSpellMgr.GetFirstSpellInChain(spell->m_spellInfo->Id) == 1804)
        return int32(PickLockCastMs(uint32(std::max(0, castTime)),
            InKit(kRogue, StateOf(player).picks, OptionId(kRogue, 4, 1))));
    Option const* o = OptionOfSpell(player->GetClass(), spell->m_spellInfo->Id);
    if (!o)
        return castTime;
    FusionClassState& s = StateOf(player);
    if (o->id == OptionId(kPaladin, 4, 2) && InKit(kPaladin, s.picks, o->id))
        return std::min(castTime, 2000);
    // Arcane Edge: the next Wizard damage spell is instant (GainOnCast spends it).
    if (o->cls == kMage && o->archetype == 0 && o->tier != kCoreTier && HasArcaneEdge(s))
        return 0;
    return castTime;
}

bool FusionClasses::CastsWhileDowned(Spell const* spell) const
{
    Player const* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
    if (!m_enabled || !player)
        return false;
    Option const* o = OptionOfSpell(player->GetClass(), spell->m_spellInfo->Id);
    return o && o->id == OptionId(kPriest, 0, 1);       // Desperate Prayer
}

bool FusionClasses::ReplacesEffect(Spell* spell, uint8 effIndex)
{
    if (!m_enabled || m_replaced.empty())
        return false;
    auto it = m_replaced.find(spell->m_spellInfo->Id);
    if (it == m_replaced.end() || !(it->second & (1 << effIndex)))
        return false;
    // Only a player casting it as its option (an NPC's own Smoke Bomb keeps its effect).
    Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
    return player && OptionOfSpell(player->GetClass(), spell->m_spellInfo->Id);
}

void FusionClasses::OnCast(Spell* spell)
{
    Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
    // A hand's bolt lands as a triggered cast (FusionSpellcasting.cpp): its option runs then.
    // A splashed enemy's share is no second cast of it: the option ran on the hit (or the impact).
    if (!m_enabled || !player || (spell->IsTriggered() && (!spell->m_fusionArrived || spell->m_fusionSplash > 0.f)))
        return;
    Option const* o = OptionOfSpell(player->GetClass(), spell->m_spellInfo->Id);
    if (!o)
        return;
    FusionClassState& s = StateOf(player);
    s.cooldownSync.push_back({spell->m_spellInfo->Id, 0});
    GainOnCast(player, s, *o, spell);
    CastOption(player, s, *o, spell);
}

std::string FusionClasses::Report(Player* player)
{
    FusionClassState& s = StateOf(player);
    if (!s.loaded)
        LoadState(player);
    std::ostringstream out;
    out << ClassName(player->GetClass()) << ", kit " << (s.kit < Fusion::kClassSlots ? std::to_string(s.kit + 1) : "none")
        << ": ";
    for (uint8 t = 0; t < kTiers; ++t)
    {
        Option const* o = FindOption(s.picks[t]);
        out << (t ? " | " : "") << uint32(t + 1) << ". " << (o ? o->name : "-");
    }
    out << ". Bought " << s.learned.size() << " of 18:";
    for (uint16 id : s.learned)
        out << ' ' << id;
    for (Meter const& m : s.meters)
        out << ". " << ResourceName(m.resource) << ' ' << m.value << '/' << m.max;
    return out.str();
}

// .fusion talents [t1 t2 t3 t4 t5 t6]: set the picks of the kit in hand directly (testing; a GM
// may pick any option of the class, bought or not); with no argument, the kit and the bought
// options. Each pick is an option id (class*100 + tier*10 + archetype 1..3), or 1..3 for that
// tier's archetype, 0 none. A selected player is changed instead of oneself.
bool ChatHandler::HandleFusionTalentsCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* self = session ? session->GetPlayer() : nullptr;
    if (!self || !sFusionClasses.IsEnabled() || !self->HasFusion())
    {
        SendSysMessage("Fusion: the class framework is off.");
        SetSentErrorMessage(true);
        return false;
    }
    Player* player = self;
    if (Player* target = GetSelectedPlayer(); target && sFusionMgr.IsAdmin(self))
        player = target;
    if (!IsPlayableClass(player->GetClass()))
        return false;
    std::istringstream in(args ? args : "");
    std::vector<uint32> values;
    uint32 v = 0;
    while (in >> v)
        values.push_back(v);
    if (values.empty())
    {
        PSendSysMessage("Fusion: %s", sFusionClasses.Report(player).c_str());
        return true;
    }
    if (!sFusionMgr.IsAdmin(self))
    {
        SendSysMessage("Fusion: choose talents in the kit window at a trainer or innkeeper.");
        SetSentErrorMessage(true);
        return false;
    }
    Picks picks{};
    for (uint8 t = 0; t < kTiers && t < values.size(); ++t)
        picks[t] = values[t] >= 1 && values[t] <= 3 ? OptionId(player->GetClass(), uint8(t + 1), uint8(values[t] - 1))
            : uint16(values[t]);
    uint8 bad = 0;
    PickError const e = sFusionClasses.ValidatePicks(player, picks, &bad);
    if (e != PickError::None)
    {
        PSendSysMessage("Fusion: tier %u's pick is refused (error %u).", uint32(bad), uint32(e));
        SetSentErrorMessage(true);
        return false;
    }
    if (!sFusionClasses.SetKitPicks(player, picks))
    {
        SendSysMessage("Fusion: no kit in hand; equip one in the kit editor (K) first.");
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("Fusion: kit picks set; they apply at once (or out of combat).");
    return true;
}

// .fusion option [learn <id | all> | forget | list]: the class options without a trainer (GMs).
bool ChatHandler::HandleFusionOptionCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* self = session ? session->GetPlayer() : nullptr;
    if (!self || !sFusionClasses.IsEnabled() || !self->HasFusion() || !sFusionMgr.IsAdmin(self))
    {
        SendSysMessage("Fusion: only an admin may grant class options.");
        SetSentErrorMessage(true);
        return false;
    }
    Player* player = self;
    if (Player* target = GetSelectedPlayer())
        player = target;
    if (!IsPlayableClass(player->GetClass()))
        return false;
    std::istringstream in(args ? args : "");
    std::string verb, what;
    in >> verb >> what;
    if (verb == "learn" && what == "all")
    {
        uint32 n = 0;
        for (Option const* o : OptionsOf(player->GetClass()))
            n += sFusionClasses.GrantOption(player, o->id) ? 1 : 0;
        PSendSysMessage("Fusion: %u options learned.", n);
        return true;
    }
    if (verb == "learn" && !what.empty())
    {
        uint32 const id = uint32(std::strtoul(what.c_str(), nullptr, 10));
        if (!sFusionClasses.GrantOption(player, uint16(id)))
        {
            PSendSysMessage("Fusion: option %u is not one of the class's, or is known.", id);
            SetSentErrorMessage(true);
            return false;
        }
        return true;
    }
    if (verb == "forget")
    {
        sFusionClasses.ForgetOptions(player);
        SendSysMessage("Fusion: every option forgotten.");
        return true;
    }
    for (Option const* o : OptionsOf(player->GetClass()))
    {
        Spec const* spec = SpecOf(o->id);
        PSendSysMessage("%u  %s (%s, tier %u, route %s%s, %s)%s", uint32(o->id), o->name, ArchetypeName(o->cls, o->archetype),
            uint32(o->tier), o->route == Route::A ? "A" : o->route == Route::B ? "B" : "C",
            o->kind == Kind::Passive ? ", passive" : o->kind == Kind::Form ? ", form" : "",
            spec ? StatusName(spec->status) : "?", sFusionClasses.Learned(player).count(o->id) ? "  [bought]" : "");
    }
    SendSysMessage("Fusion: .fusion option learn <id | all> | forget; .fusion talents <t1..t6> (ids, or 1-3 per tier).");
    return true;
}
