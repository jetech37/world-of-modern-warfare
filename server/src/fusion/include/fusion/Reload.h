/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_RELOAD_H
#define FUSION_RELOAD_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

namespace Fusion
{
    // The client says when its reload finished (STATE_RELOADING, or STATE_RELOAD_ONE for each
    // shell of a shotgun loading one at a time); the server fills the magazine then. A modified
    // client could say so right after its last shot and never pause to reload. The server holds
    // a reload to at least kReloadSlack of the quickest time the client could honestly take,
    // counted from the gun's last round (security review 2026-10, #13).
    //
    // The slack (0.7) leaves room for lag: a shot held up on the way and the reload's word
    // arriving on time make the gap look shorter than it was. A reload asked for too early is not
    // thrown away but waits for its time, so an honest client on a bad line only loses a moment.
    constexpr float kReloadSlack = 0.7f;
    // How long a quicker reload factor (Sleight of Hand, ranged attack power) keeps counting after
    // it is gone: a reload begun with it may still be running when it ends (a class change, a buff
    // running out).
    constexpr uint32_t kReloadFactorMemoryMs = 10000;

    // The share of a gun's own reload time the character takes: MW2's Sleight of Hand halves it
    // (the client's `reload_scale`), and ranged attack power's handling shortens it (Stats.h
    // Out::ReloadTime, held to its limit as the client receives it).
    float ReloadFactor(bool sleightOfHand, float handlingReload);

    // The quickest a whole-magazine reload of `w` honestly takes with `factor`, in seconds: the
    // shorter of its reload with rounds left and from empty (the server's and the client's
    // magazine counts can differ by a round). 0 when the gun gives no time: no limit.
    float QuickestReloadSeconds(WeaponProfile const& w, float factor);
    // The quickest one shell of a shell-by-shell reload takes: its per-shell time (a segmented
    // definition's reload time is one segment's). 0 for none.
    float QuickestShellSeconds(WeaponProfile const& w, float factor);

    // One gun's reload pace: when its last round left, and the reloads asked for and not yet due.
    class ReloadPace
    {
    public:
        // A round left the gun at `nowMs`: a reload counts from here (one asked for and waiting
        // waits again, so firing on through a reload never refills the magazine mid-burst).
        void Fired(uint32_t nowMs);
        // The client says a whole-magazine reload, or one shell, finished.
        void AskFull() { m_pendingFull = true; }
        void AskShell() { if (m_pendingShells < 255) ++m_pendingShells; }

        struct Due
        {
            bool full = false;          // fill the magazine
            uint32_t shells = 0;        // load this many single rounds
            bool Any() const { return full || shells > 0; }
        };
        // What of the asked-for reloads may land at `nowMs`: a whole reload once kReloadSlack of
        // `fullSecs` has passed since the last round, the k-th shell since it once k times
        // kReloadSlack of `shellSecs` has (seconds as QuickestReloadSeconds and
        // QuickestShellSeconds give them). With no round fired yet, everything is due. Taken ones
        // are forgotten.
        Due Take(uint32_t nowMs, float fullSecs, float shellSecs);
        bool Pending() const { return m_pendingFull || m_pendingShells > 0; }

    private:
        bool m_fired = false;
        uint32_t m_lastRoundMs = 0;
        bool m_pendingFull = false;
        uint8_t m_pendingShells = 0;
        uint32_t m_shellsSinceRound = 0;
    };

    // The least of a value over the last few seconds: the reload factor a reload still running
    // may have begun with.
    class RecentLeast
    {
    public:
        float Update(float value, uint32_t nowMs, uint32_t memoryMs = kReloadFactorMemoryMs);

    private:
        bool m_set = false;
        float m_least = 1.f;
        uint32_t m_untilMs = 0;
    };
}

#endif
