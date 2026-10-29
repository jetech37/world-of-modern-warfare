/*
 * World of Modern Warfare: the game masters' profession tools (fusion/ProfessionAdmin.h,
 * vps/ADMIN.md "Professions"). GPL-2.0-or-later.
 *
 * .profession list | learn <name|all> [skill] | set <name> <skill> | recipes <name|all>
 *
 * On the selected player (yourself with none selected). Vanilla's professions are learned by
 * their trainer spells, rank by rank up to the one the skill needs, and the skill set; their
 * recipes are every SkillLineAbility row of the line (as .learn all_recipes). Construction and
 * Minstrelsy, Inscription and Jewelcrafting live in the fusion's own skill table: their managers
 * set the tier and skill, and the recipes are learned (Construction's pieces come with its tiers).
 */

#include "FusionMgr.h"
#include "FusionMinstrelsy.h"

#include "Chat.h"
#include "Language.h"
#include "Player.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "fusion/Construction.h"
#include "fusion/ProfessionAdmin.h"

#include <sstream>

namespace
{
    namespace PA = Fusion::ProfessionAdmin;
    using Fusion::Construction::Tier;

    // The tier that holds `skill` (Construction's caps, which Minstrelsy shares).
    Tier TierHolding(uint16 skill)
    {
        Tier t = Tier::Apprentice;
        while (Fusion::Construction::Cap(t) < skill && t != Tier::Artisan)
            t = Fusion::Construction::Next(t);
        return t;
    }

    // A vanilla profession to `skill`: its rank spells up to the one that holds it, then the skill.
    void LearnStock(Player* p, PA::Profession const& prof, uint16 skill)
    {
        uint8 const ranks = PA::RanksFor(skill);
        for (uint8 r = 0; r < ranks; ++r)
            if (uint32 const spell = prof.rankSpells[r]; spell && sSpellMgr.GetSpellEntry(spell) && !p->HasSpell(spell))
                p->LearnSpell(spell, false);
        uint16 const cap = std::max<uint16>(PA::RankCap(ranks), p->GetSkillMaxPure(prof.skillLine));
        p->SetSkill(prof.skillLine, skill, cap);
    }

    Fusion::Crafts::Trade TradeOf(PA::Kind k)
    {
        return k == PA::Kind::Inscription ? Fusion::Crafts::Trade::Inscription : Fusion::Crafts::Trade::Jewelcrafting;
    }

    std::string Line(Player* p, PA::Profession const& prof)
    {
        std::ostringstream o;
        o << prof.name << ": ";
        FusionPlayerData const& d = p->GetFusionConst();
        switch (prof.kind)
        {
            case PA::Kind::Stock:
                if (p->HasSkill(prof.skillLine))
                    o << p->GetSkillValueBase(prof.skillLine) << "/" << p->GetSkillMaxPure(prof.skillLine);
                else
                    o << "not learned";
                break;
            case PA::Kind::Construction:
                if (d.constructionTier != Tier::None)
                    o << d.constructionSkill << "/" << Fusion::Construction::Cap(d.constructionTier);
                else
                    o << "not learned";
                break;
            case PA::Kind::Minstrelsy:
            {
                uint16 const skill = sMinstrelMgr.GmSkillOf(p);
                if (skill)
                    o << skill;
                else
                    o << "not learned";
                break;
            }
            case PA::Kind::Inscription:
            case PA::Kind::Jewelcrafting:
            {
                uint16 const skill = sCraftsMgr.GmSkillOf(p, TradeOf(prof.kind));
                if (skill)
                    o << skill;
                else
                    o << "not learned";
                break;
            }
        }
        return o.str();
    }
}

bool ChatHandler::HandleProfessionCommand(char* args)
{
    Player* target = GetSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }
    PA::Command const c = PA::Parse(args ? args : "");
    if (!c.ok)
    {
        SendSysMessage(c.error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    bool const fusion = sFusionMgr.IsEnabled() && target->HasFusion();
    if (c.verb == PA::Verb::List)
    {
        PSendSysMessage("%s's professions:", target->GetName());
        for (PA::Profession const& prof : PA::All())
            if (prof.kind == PA::Kind::Stock || fusion)
                SendSysMessage(Line(target, prof).c_str());
        return true;
    }
    std::vector<PA::Profession const*> profs;
    if (c.all)
    {
        for (PA::Profession const& prof : PA::All())
            profs.push_back(&prof);
    }
    else
        profs.push_back(c.profession);

    for (PA::Profession const* prof : profs)
    {
        if (prof->kind != PA::Kind::Stock && !fusion)
        {
            if (!c.all)
                PSendSysMessage("%s needs the fusion (Fusion.Enable).", prof->name);
            continue;
        }
        switch (prof->kind)
        {
            case PA::Kind::Stock:
                if (c.verb == PA::Verb::Recipes)
                {
                    if (!target->HasSkill(prof->skillLine))
                        LearnStock(target, *prof, PA::kMaxSkill);
                    HandleLearnSkillRecipesHelper(target, prof->skillLine);
                    // Every recipe wants its skill: the line at its max.
                    uint16 const max = target->GetSkillMaxPure(prof->skillLine);
                    target->SetSkill(prof->skillLine, max, max);
                }
                else
                    LearnStock(target, *prof, c.skill);
                break;
            case PA::Kind::Construction:
            {
                // Its recipes are its tiers: every piece at Artisan, with the skill to build it.
                uint16 const skill = c.verb == PA::Verb::Recipes ? PA::kMaxSkill : c.skill;
                sFusionMgr.SetConstruction(target, TierHolding(skill), skill, false);
                break;
            }
            case PA::Kind::Minstrelsy:
                if (c.verb == PA::Verb::Recipes)
                {
                    if (!sMinstrelMgr.GmSkillOf(target))
                        sMinstrelMgr.GmSetSkill(target, PA::kMaxSkill);
                    PSendSysMessage("Minstrelsy: %u instrument recipes learned.", sMinstrelMgr.GmLearnAllRecipes(target));
                }
                else
                    sMinstrelMgr.GmSetSkill(target, c.skill);
                break;
            case PA::Kind::Inscription:
            case PA::Kind::Jewelcrafting:
            {
                Fusion::Crafts::Trade const t = TradeOf(prof->kind);
                if (c.verb == PA::Verb::Recipes)
                {
                    if (!sCraftsMgr.GmSkillOf(target, t))
                        sCraftsMgr.GmSetSkill(target, t, PA::kMaxSkill);
                    PSendSysMessage("%s: %u recipes learned.", prof->name, sCraftsMgr.GmLearnAllRecipes(target, t));
                }
                else
                    sCraftsMgr.GmSetSkill(target, t, c.skill);
                break;
            }
        }
        SendSysMessage(Line(target, *prof).c_str());
    }
    if (c.verb == PA::Verb::Recipes)
        PSendSysMessage("%s knows every recipe of %s.", target->GetName(), c.all ? "every profession" : c.profession->name);
    return true;
}
