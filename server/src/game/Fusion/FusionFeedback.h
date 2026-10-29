/*
 * World of Modern Warfare: bug reports and suggestions from the game. GPL-2.0-or-later.
 *
 * The Escape menu's "Report Bug / Suggest" dialog sends a report on fusion channel 9
 * (fusion/Feedback.h: op 1 a report, op 2 a question, op 1 back the answer). The server cleans
 * the text, holds each account to Feedback.CooldownSeconds and Feedback.DailyLimit
 * (mangosd.conf), adds the player's context (account, character, level, race, class, map, zone, position, target, client version) and keeps
 * it in the characters database's `aw_feedback` table, made here at startup as
 * `fusion_build_piece` is (FusionMgr::LoadPieces). The owner reads them with `aw-admin feedback`
 * on the VPS, and a GM in the game with `.feedback`.
 */

#ifndef AW_FUSION_FEEDBACK_H
#define AW_FUSION_FEEDBACK_H

#include "Common.h"
#include "fusion/Feedback.h"

#include <mutex>
#include <vector>

class Player;

class FeedbackMgr
{
public:
    static FeedbackMgr& Instance();

    // Startup: the config, the table, the next report number and each account's reports of the
    // last day (so a restart does not reset the limits).
    void Load();
    bool IsEnabled() const { return m_enabled; }

    // Channel 9's ops 1 (submit) and 2 (ask), from FusionMgr::HandleChannel. Any map thread.
    void HandleChannel(Player* player, std::vector<uint8> const& body);

    // The statuses a report can have, the table's ENUM: new, reviewing, accepted, declined, done.
    static bool KnownStatus(std::string const& s);

private:
    void Reply(Player* player, Fusion::Feedback::Reply const& r) const;
    void Submit(Player* player, std::vector<uint8> const& body);

    bool m_enabled = true;
    std::mutex m_lock;      // m_limiter and m_nextId: packets come from every map thread
    Fusion::Feedback::Limiter m_limiter;
    uint32 m_nextId = 1;
};

#define sFeedbackMgr FeedbackMgr::Instance()

#endif
