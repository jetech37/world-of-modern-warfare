/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_LOCKS_H
#define FUSION_LOCKS_H

#include "fusion/Build.h"
#include "fusion/Common.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Locks: who may open a door (and use a portal, Portals.h). Each lockable piece carries a lock its
// owner chooses: Unlocked (anyone), Owner only, Owner and party, Owner and guild, or a numeric
// code of 4 to 8 digits. The owner sets it from a small dialog (in build mode, F on the door);
// anyone else at a code lock is asked for the code on a keypad. Wrong codes are rate-limited per
// player and per lock. A code is never kept or sent as itself: the server stores a salted,
// stretched SHA-256 of it, and no client ever learns a code (the wire says only the mode).
//
// New doors start at Owner and party: a builder's friends get in without fuss, strangers don't,
// and an open-world door is no longer a hole in a wall anyone may walk through. Doors from before
// locks existed take the same default (the column's default).
//
// On a plot (Plots.h) the plot's owner, those they permit and game masters open every door on it
// whatever its lock (the plot is their household); everyone else meets the door's lock, so an
// owner who wants a shop door open to all sets it Unlocked.
namespace Fusion { namespace Locks
{
    enum class Mode : uint8_t
    {
        Unlocked = 0,
        Owner = 1,
        Party = 2,      // the owner and the owner's party (or raid)
        Guild = 3,      // the owner and the owner's guild
        Password = 4,   // anyone who knows the code
    };
    constexpr uint8_t kModeCount = 5;
    constexpr Mode kDefault = Mode::Party;
    bool ValidMode(uint8_t m);
    char const* ModeName(Mode m);

    // Doors (every style) and portals.
    bool Lockable(Build::PieceType t);

    // A code: 4 to 8 ASCII digits.
    constexpr size_t kMinCode = 4;
    constexpr size_t kMaxCode = 8;
    bool ValidCode(std::string const& code);

    // What the server keeps of a code lock: a random salt and the stretched hash, both hex.
    struct Secret
    {
        std::string salt;   // 32 hex (16 bytes)
        std::string hash;   // 64 hex
        bool Set() const { return !salt.empty() && hash.size() == 64; }
    };
    // How many times the salted hash is run again: slows a guess against a stolen table, costs a
    // door's use a fraction of a millisecond.
    constexpr uint32_t kStretch = 2048;
    // SHA-256 of salt bytes and the code, then of the last digest and the salt, kStretch times.
    std::string HashCode(std::string const& code, std::string const& saltHex);
    // A secret for `code` with `saltBytes` (16 random bytes from the caller); empty if the code or
    // salt is no good.
    Secret MakeSecret(std::string const& code, std::vector<uint8_t> const& saltBytes);
    // Whether `code` opens the lock, compared in time that doesn't depend on where they differ.
    bool CheckCode(Secret const& s, std::string const& code);

    // Who is at the lock, as the game sees them.
    struct Visitor
    {
        bool gm = false;
        bool owner = false;         // the piece's builder, or a character of the same account
        bool household = false;     // on a plot: its owner or one they permit (Plots::CanUseDoor)
        bool party = false;         // in a group with the owner
        bool guild = false;         // in the owner's guild (a guild, not none)
        bool knowsCode = false;     // gave the right code (since it was last changed)
    };
    enum class Verdict : uint8_t { Open, Locked, NeedsCode };
    Verdict MayOpen(Mode mode, Visitor const& v);
    // Who may set a lock: its owner, the owner of the plot it stands on, or a game master.
    bool MaySet(bool owner, bool plotOwner, bool gm);

    // ── Wrong codes ──
    // A player (or anyone at one lock) may miss kFreeTries times in a row; then each further miss
    // shuts them out for a while, doubling from kFirstLockout to kMaxLockout. A right code clears
    // the player's count; a lock's count runs down after kForgetSeconds without a miss.
    constexpr uint32_t kFreeTries = 3;
    constexpr int64_t kFirstLockout = 30;
    constexpr int64_t kMaxLockout = 15 * 60;
    constexpr int64_t kForgetSeconds = 30 * 60;
    class Limiter
    {
    public:
        // Seconds `key` must still wait before trying again (0: may try now).
        int64_t WaitFor(uint64_t key, int64_t now) const;
        // A miss: the wait it now has (0 while it has free tries left).
        int64_t Miss(uint64_t key, int64_t now);
        void Clear(uint64_t key);
        // Drop what has long been quiet.
        void Forget(int64_t now);
        size_t Size() const { return m_entries.size(); }

    private:
        struct Entry
        {
            uint32_t misses = 0;
            int64_t until = 0;
            int64_t last = 0;
        };
        std::unordered_map<uint64_t, Entry> m_entries;
    };
    // The limiter keys: a player's, and a lock's (a piece).
    inline uint64_t PlayerKey(uint32_t guid) { return uint64_t(1) << 32 | guid; }
    inline uint64_t LockKey(uint32_t pieceId) { return uint64_t(2) << 32 | pieceId; }

    // ── The wire: the build channel (Packets kChannelBuild) ──
    // Client: [4, piece u32, mode u8, code length u8, code digits]: set a lock (the code only for
    //          Password; an empty one keeps the code a Password lock already has).
    //         [5, piece u32, code length u8, code digits]: a code tried at a lock.
    // Server: [19, piece u32, what u8 (0 a door, 1 a portal)]: the lock wants a code: show the
    //          keypad.
    constexpr uint8_t kOpSetLock = 4;
    constexpr uint8_t kOpTryCode = 5;
    constexpr uint8_t kOpAskCode = 19;
    struct SetLock
    {
        uint32_t piece = 0;
        Mode mode = kDefault;
        std::string code;
    };
    bool DecodeSetLock(std::vector<uint8_t> const& body, SetLock& out);
    std::vector<uint8_t> EncodeSetLock(SetLock const& s);
    bool DecodeTryCode(std::vector<uint8_t> const& body, uint32_t& piece, std::string& code);
    std::vector<uint8_t> EncodeTryCode(uint32_t piece, std::string const& code);
    std::vector<uint8_t> EncodeAskCode(uint32_t piece, bool portal);

    // A lockable piece's mode rides in bits 4-6 of its wire flags (Build.h EncodePieces).
    constexpr uint8_t kPieceLockShift = 4;
}}

#endif
