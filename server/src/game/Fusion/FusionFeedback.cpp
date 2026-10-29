/*
 * World of Modern Warfare: bug reports and suggestions from the game. GPL-2.0-or-later.
 * See FusionFeedback.h.
 */

#include "FusionFeedback.h"
#include "FusionMgr.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"
#include "revision.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

using namespace Fusion::Feedback;

namespace
{
    constexpr char const* kStatuses[] = {"new", "reviewing", "accepted", "declined", "done"};

    uint64 Now()
    {
        return uint64(time(nullptr));
    }

    // What the player has targeted, for the report: "Hogger (creature 448, level 11, 12 yd)".
    std::string TargetOf(Player* player)
    {
        ObjectGuid const guid = player->GetSelectionGuid();
        if (guid.IsEmpty())
            return std::string();
        Unit* target = ObjectAccessor::GetUnit(*player, guid);
        if (!target)
            return std::string();
        std::ostringstream s;
        s << target->GetName() << " (";
        if (target->GetTypeId() == TYPEID_PLAYER)
            s << "player";
        else
            s << "creature " << target->GetEntry();
        s << ", level " << target->GetLevel() << ", " << uint32(std::lround(player->GetDistance(target))) << " yd)";
        return CleanLine(s.str(), 120);
    }

    std::string AreaName(uint32 id)
    {
        AreaEntry const* a = id ? AreaEntry::GetById(id) : nullptr;
        return a && a->Name ? CleanLine(a->Name, 64) : std::string();
    }
}

FeedbackMgr& FeedbackMgr::Instance()
{
    static FeedbackMgr instance;
    return instance;
}

bool FeedbackMgr::KnownStatus(std::string const& s)
{
    for (char const* k : kStatuses)
        if (s == k)
            return true;
    return false;
}

void FeedbackMgr::Load()
{
    m_enabled = sConfig.GetBoolDefault("Feedback.Enable", true);
    Limits limits;
    limits.cooldownSeconds = uint32(std::max(0, sConfig.GetIntDefault("Feedback.CooldownSeconds", int32(kDefaultCooldownSeconds))));
    limits.dailyLimit = uint32(std::clamp(sConfig.GetIntDefault("Feedback.DailyLimit", int32(kDefaultDailyLimit)), 0, 254));

    // The table, made here as fusion_build_piece is (no migration file: one once kept mangosd
    // from starting). Statuses and types are the ENUMs aw-admin filters on.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_feedback` ("
        "`id` int(10) unsigned NOT NULL,"
        "`created_at` datetime NOT NULL,"
        "`type` enum('bug','suggestion','other') NOT NULL DEFAULT 'bug',"
        "`status` enum('new','reviewing','accepted','declined','done') NOT NULL DEFAULT 'new',"
        "`title` varchar(80) NOT NULL DEFAULT '',"
        "`body` text NOT NULL,"
        "`account_id` int(10) unsigned NOT NULL DEFAULT '0',"
        "`account_name` varchar(32) NOT NULL DEFAULT '',"
        "`gm_level` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`character_guid` int(10) unsigned NOT NULL DEFAULT '0',"
        "`character_name` varchar(32) NOT NULL DEFAULT '',"
        "`level` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`race` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`class` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`map` smallint(5) unsigned NOT NULL DEFAULT '0',"
        "`zone` int(10) unsigned NOT NULL DEFAULT '0',"
        "`area` int(10) unsigned NOT NULL DEFAULT '0',"
        "`zone_name` varchar(64) NOT NULL DEFAULT '',"
        "`area_name` varchar(64) NOT NULL DEFAULT '',"
        "`x` float NOT NULL DEFAULT '0', `y` float NOT NULL DEFAULT '0', `z` float NOT NULL DEFAULT '0',"
        "`o` float NOT NULL DEFAULT '0',"
        "`target` varchar(120) NOT NULL DEFAULT '',"
        "`client_version` varchar(64) NOT NULL DEFAULT '',"
        "`server_build` varchar(64) NOT NULL DEFAULT '',"
        "`admin_note` varchar(1000) NOT NULL DEFAULT '',"
        "`updated_at` datetime DEFAULT NULL,"
        "PRIMARY KEY (`id`), KEY `status` (`status`), KEY `account_time` (`account_id`, `created_at`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8");

    std::lock_guard<std::mutex> lock(m_lock);
    m_limiter.Clear();
    m_limiter.SetLimits(limits);
    m_nextId = 1;
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.Query("SELECT MAX(`id`) FROM `aw_feedback`"))
        m_nextId = result->Fetch()[0].GetUInt32() + 1;
    uint64 const now = Now();
    uint32 recent = 0;
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.Query(
            "SELECT `account_id`, UNIX_TIMESTAMP(`created_at`) FROM `aw_feedback` WHERE `created_at` > NOW() - INTERVAL 1 DAY"))
    {
        do
        {
            Field* f = result->Fetch();
            m_limiter.Record(f[0].GetUInt32(), f[1].GetUInt64(), now);
            ++recent;
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Feedback: %s, one report per %u s, %u a day (0: no limit); next report #%u, %u in the last day.",
        m_enabled ? "on" : "off", limits.cooldownSeconds, limits.dailyLimit, m_nextId, recent);
}

void FeedbackMgr::Reply(Player* player, Fusion::Feedback::Reply const& r) const
{
    sFusionMgr.SendChannel(player, kChannelFeedback, EncodeReply(r));
}

void FeedbackMgr::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    if (!player || !player->IsInWorld() || body.empty())
        return;
    if (!m_enabled)
    {
        Fusion::Feedback::Reply r;
        r.result = Result::Disabled;
        Reply(player, r);
        return;
    }
    if (body[0] == kOpAsk)
    {
        if (body.size() != 1)
            return;
        Verdict v;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            v = m_limiter.Check(player->GetSession()->GetAccountId(), Now());
        }
        Fusion::Feedback::Reply r;
        r.result = v.result;
        r.waitSeconds = v.waitSeconds;
        r.leftToday = v.leftToday;
        Reply(player, r);
        return;
    }
    if (body[0] == kOpSubmit)
        Submit(player, body);
}

void FeedbackMgr::Submit(Player* player, std::vector<uint8> const& body)
{
    Fusion::Feedback::Reply r;
    Submission s;
    if (!DecodeSubmit(body, s))
    {
        r.result = Result::Invalid;
        Reply(player, r);
        return;
    }
    std::string const title = CleanLine(s.title, kMaxTitleChars);
    std::string const text = CleanText(s.body, kMaxBodyChars);
    std::string const version = CleanLine(s.version, kMaxVersionChars);
    if (CountChars(title) < kMinTitleChars)
    {
        r.result = Result::Invalid;
        Reply(player, r);
        return;
    }

    WorldSession* session = player->GetSession();
    uint32 const account = session->GetAccountId();
    uint64 const now = Now();
    uint32 id = 0;
    {
        // Checked and counted under one lock, so two packets at once can't both pass.
        std::lock_guard<std::mutex> lock(m_lock);
        Verdict const v = m_limiter.Check(account, now);
        if (v.result != Result::Ready)
        {
            r.result = v.result;
            r.waitSeconds = v.waitSeconds;
            r.leftToday = v.leftToday;
            Reply(player, r);
            return;
        }
        m_limiter.Record(account, now, now);
        id = m_nextId++;
        r.leftToday = m_limiter.Check(account, now).leftToday;
    }

    uint32 const zone = player->GetZoneId();
    uint32 const area = player->GetAreaId();
    static SqlStatementID insertFeedback;
    SqlStatement stmt = CharacterDatabase.CreateStatement(insertFeedback,
        "INSERT INTO `aw_feedback` (`id`, `created_at`, `type`, `title`, `body`, `account_id`, `account_name`, `gm_level`, "
        "`character_guid`, `character_name`, `level`, `race`, `class`, `map`, `zone`, `area`, `zone_name`, `area_name`, "
        "`x`, `y`, `z`, `o`, `target`, `client_version`, `server_build`) "
        "VALUES (?, FROM_UNIXTIME(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.addUInt32(id);
    stmt.addUInt64(now);
    stmt.addString(KindName(s.kind));
    stmt.addString(title);
    stmt.addString(text);
    stmt.addUInt32(account);
    stmt.addString(CleanLine(session->GetUsername(), 32));
    stmt.addUInt8(uint8(session->GetSecurity()));
    stmt.addUInt32(player->GetGUIDLow());
    stmt.addString(CleanLine(player->GetName(), 32));
    stmt.addUInt8(uint8(player->GetLevel()));
    stmt.addUInt8(player->GetRace());
    stmt.addUInt8(player->GetClass());
    stmt.addUInt16(uint16(player->GetMapId()));
    stmt.addUInt32(zone);
    stmt.addUInt32(area);
    stmt.addString(AreaName(zone));
    stmt.addString(AreaName(area));
    stmt.addFloat(player->GetPositionX());
    stmt.addFloat(player->GetPositionY());
    stmt.addFloat(player->GetPositionZ());
    stmt.addFloat(player->GetOrientation());
    stmt.addString(TargetOf(player));
    stmt.addString(version);
    stmt.addString(CleanLine(REVISION_HASH " " REVISION_DATE, 64));
    stmt.Execute();

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Feedback: #%u (%s) from %s (account %u): %s", id, KindName(s.kind),
        player->GetName(), account, title.c_str());
    r.result = Result::Sent;
    r.id = id;
    Reply(player, r);
}

// ── `.feedback` (GM level 3 and up, and the console) ────────────────────────────────────────────

namespace
{
    char const* kFeedbackHelp =
        ".feedback list [new|reviewing|accepted|declined|done|all] [bug|suggestion|other] [count]\n"
        ".feedback show <id>\n"
        ".feedback set <id> <new|reviewing|accepted|declined|done> [note]";

    bool KnownType(std::string const& s)
    {
        return s == "bug" || s == "suggestion" || s == "other";
    }
}

bool ChatHandler::HandleFeedbackCommand(char* args)
{
    std::istringstream in(args ? std::string(args) : std::string());
    std::string what;
    in >> what;
    if (what.empty() || what == "help")
    {
        SendSysMessage(kFeedbackHelp);
        return true;
    }
    if (what == "list")
    {
        // Every word is checked against a fixed list or read as a number: nothing typed reaches
        // the SQL as text.
        std::string status = "new", type;
        uint32 count = 20;
        std::string word;
        while (in >> word)
        {
            if (word == "all" || FeedbackMgr::KnownStatus(word))
                status = word;
            else if (KnownType(word))
                type = word;
            else if (word.find_first_not_of("0123456789") == std::string::npos && word.size() < 5)
                count = std::clamp<uint32>(uint32(std::stoul(word)), 1, 100);
            else
            {
                SendSysMessage(kFeedbackHelp);
                SetSentErrorMessage(true);
                return false;
            }
        }
        std::string where = "WHERE 1";
        if (status != "all")
            where += " AND `status` = '" + status + "'";
        if (!type.empty())
            where += " AND `type` = '" + type + "'";
        std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery(
            "SELECT `id`, DATE_FORMAT(`created_at`, '%%Y-%%m-%%d %%H:%%i'), `type`, `status`, `character_name`, `title` "
            "FROM `aw_feedback` %s ORDER BY `id` DESC LIMIT %u", where.c_str(), count);
        if (!result)
        {
            PSendSysMessage("No reports (%s%s%s).", status.c_str(), type.empty() ? "" : ", ", type.c_str());
            return true;
        }
        PSendSysMessage("Reports (%s%s%s), newest first:", status.c_str(), type.empty() ? "" : ", ", type.c_str());
        do
        {
            Field* f = result->Fetch();
            PSendSysMessage("#%u %s [%s, %s] %s: %s", f[0].GetUInt32(), f[1].GetString(), f[2].GetString(), f[3].GetString(),
                EscapeChat(f[4].GetCppString()).c_str(), EscapeChat(f[5].GetCppString()).c_str());
        } while (result->NextRow());
        return true;
    }
    if (what == "show" || what == "set")
    {
        std::string idWord;
        in >> idWord;
        if (idWord.empty() || idWord.find_first_not_of("0123456789") != std::string::npos || idWord.size() > 9)
        {
            SendSysMessage(kFeedbackHelp);
            SetSentErrorMessage(true);
            return false;
        }
        uint32 const id = uint32(std::stoul(idWord));
        if (what == "set")
        {
            std::string status;
            in >> status;
            if (!FeedbackMgr::KnownStatus(status))
            {
                SendSysMessage(kFeedbackHelp);
                SetSentErrorMessage(true);
                return false;
            }
            std::string note;
            std::getline(in, note);
            note = CleanLine(note, 1000);
            std::unique_ptr<QueryResult> exists = CharacterDatabase.PQuery("SELECT 1 FROM `aw_feedback` WHERE `id` = %u", id);
            if (!exists)
            {
                PSendSysMessage("No report #%u.", id);
                SetSentErrorMessage(true);
                return false;
            }
            static SqlStatementID setStatus, setStatusNote;
            if (note.empty())
            {
                SqlStatement stmt = CharacterDatabase.CreateStatement(setStatus,
                    "UPDATE `aw_feedback` SET `status` = ?, `updated_at` = NOW() WHERE `id` = ?");
                stmt.addString(status);
                stmt.addUInt32(id);
                stmt.Execute();
            }
            else
            {
                SqlStatement stmt = CharacterDatabase.CreateStatement(setStatusNote,
                    "UPDATE `aw_feedback` SET `status` = ?, `admin_note` = ?, `updated_at` = NOW() WHERE `id` = ?");
                stmt.addString(status);
                stmt.addString(note);
                stmt.addUInt32(id);
                stmt.Execute();
            }
            PSendSysMessage("Report #%u is now %s.", id, status.c_str());
            return true;
        }
        std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery(
            "SELECT `id`, DATE_FORMAT(`created_at`, '%%Y-%%m-%%d %%H:%%i'), `type`, `status`, `title`, `body`, `account_name`, "
            "`character_name`, `level`, `map`, `zone_name`, `area_name`, `x`, `y`, `z`, `target`, `client_version`, `admin_note` "
            "FROM `aw_feedback` WHERE `id` = %u", id);
        if (!result)
        {
            PSendSysMessage("No report #%u.", id);
            SetSentErrorMessage(true);
            return false;
        }
        Field* f = result->Fetch();
        PSendSysMessage("#%u %s [%s, %s] %s", f[0].GetUInt32(), f[1].GetString(), f[2].GetString(), f[3].GetString(),
            EscapeChat(f[4].GetCppString()).c_str());
        PSendSysMessage("From %s (account %s), level %u, map %u %s / %s (%.1f, %.1f, %.1f)",
            EscapeChat(f[7].GetCppString()).c_str(), EscapeChat(f[6].GetCppString()).c_str(), f[8].GetUInt32(), f[9].GetUInt32(),
            EscapeChat(f[10].GetCppString()).c_str(), EscapeChat(f[11].GetCppString()).c_str(), f[12].GetFloat(), f[13].GetFloat(),
            f[14].GetFloat());
        if (!f[15].GetCppString().empty())
            PSendSysMessage("Target: %s", EscapeChat(f[15].GetCppString()).c_str());
        if (!f[16].GetCppString().empty())
            PSendSysMessage("Client: %s", EscapeChat(f[16].GetCppString()).c_str());
        // The body line by line: a chat line is one line.
        std::istringstream lines(f[5].GetCppString());
        std::string line;
        while (std::getline(lines, line))
            PSendSysMessage("  %s", EscapeChat(line).c_str());
        if (!f[17].GetCppString().empty())
            PSendSysMessage("Note: %s", EscapeChat(f[17].GetCppString()).c_str());
        return true;
    }
    SendSysMessage(kFeedbackHelp);
    SetSentErrorMessage(true);
    return false;
}
