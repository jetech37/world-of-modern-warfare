/*
 * Azeroth Warfare: tests for door and portal locks (Locks.h) and SHA-256 (Sha256.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Locks.h"
#include "fusion/Sha256.h"

using namespace Fusion;
using namespace Fusion::Locks;
using Fusion::Build::PieceType;

TEST(sha256_matches_the_standards_vectors)
{
    CHECK(Sha256::Hex(Sha256::Hash(std::string())) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::Hex(Sha256::Hash(std::string("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::Hex(Sha256::Hash(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")))
        == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million a's: many blocks.
    CHECK(Sha256::Hex(Sha256::Hash(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // 55, 56 and 64 bytes: the padding's edges.
    CHECK(Sha256::Hex(Sha256::Hash(std::string(55, 'a'))) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(Sha256::Hex(Sha256::Hash(std::string(56, 'a'))) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(Sha256::Hex(Sha256::Hash(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    std::vector<uint8_t> bytes;
    CHECK(Sha256::FromHex("00ff10Ab", bytes) && bytes == std::vector<uint8_t>({0x00, 0xff, 0x10, 0xab}));
    CHECK(!Sha256::FromHex("abc", bytes) && !Sha256::FromHex("zz", bytes));
}

TEST(only_doors_and_portals_take_a_lock)
{
    CHECK(Lockable(PieceType::WoodDoor));
    CHECK(Lockable(Build::Styled(PieceType::WoodDoor, 3)));            // an orcish door
    CHECK(Lockable(PieceType(Build::kPortalProp)));
    CHECK(!Lockable(PieceType::WoodDoorway) && !Lockable(PieceType::WoodWall) && !Lockable(PieceType(124)));
    CHECK(uint8_t(kDefault) == Build::kDefaultLock && kDefault == Mode::Party);
    Build::Piece fresh;
    CHECK(fresh.lock == uint8_t(Mode::Party));
    for (uint8_t m = 0; m < kModeCount; ++m)
        CHECK(ValidMode(m) && std::string(ModeName(Mode(m))).size() > 0);
    CHECK(!ValidMode(kModeCount));
}

TEST(each_lock_mode_lets_in_whom_it_says)
{
    Visitor stranger;
    Visitor partyMember;
    partyMember.party = true;
    Visitor guildMate;
    guildMate.guild = true;
    Visitor owner;
    owner.owner = true;
    Visitor household;
    household.household = true;
    Visitor gm;
    gm.gm = true;
    Visitor knows;
    knows.knowsCode = true;

    CHECK(MayOpen(Mode::Unlocked, stranger) == Verdict::Open);
    CHECK(MayOpen(Mode::Owner, stranger) == Verdict::Locked);
    CHECK(MayOpen(Mode::Owner, partyMember) == Verdict::Locked);
    CHECK(MayOpen(Mode::Owner, guildMate) == Verdict::Locked);
    CHECK(MayOpen(Mode::Party, partyMember) == Verdict::Open);
    CHECK(MayOpen(Mode::Party, guildMate) == Verdict::Locked);
    CHECK(MayOpen(Mode::Party, stranger) == Verdict::Locked);
    CHECK(MayOpen(Mode::Guild, guildMate) == Verdict::Open);
    CHECK(MayOpen(Mode::Guild, partyMember) == Verdict::Locked);
    CHECK(MayOpen(Mode::Password, stranger) == Verdict::NeedsCode);
    CHECK(MayOpen(Mode::Password, partyMember) == Verdict::NeedsCode);
    CHECK(MayOpen(Mode::Password, knows) == Verdict::Open);
    // The owner, a plot's household and a game master pass every lock.
    for (uint8_t m = 0; m < kModeCount; ++m)
    {
        CHECK(MayOpen(Mode(m), owner) == Verdict::Open);
        CHECK(MayOpen(Mode(m), household) == Verdict::Open);
        CHECK(MayOpen(Mode(m), gm) == Verdict::Open);
    }
    // Who sets one.
    CHECK(MaySet(true, false, false) && MaySet(false, true, false) && MaySet(false, false, true));
    CHECK(!MaySet(false, false, false));
}

TEST(codes_are_kept_salted_and_hashed_never_as_themselves)
{
    CHECK(ValidCode("1234") && ValidCode("12345678"));
    CHECK(!ValidCode("123") && !ValidCode("123456789") && !ValidCode("12a4") && !ValidCode("") && !ValidCode("12 34"));
    std::vector<uint8_t> const salt = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Secret const s = MakeSecret("4711", salt);
    CHECK(s.Set());
    CHECK(s.salt == "0102030405060708090a0b0c0d0e0f10");
    CHECK(s.hash.size() == 64 && s.hash.find("4711") == std::string::npos);
    CHECK(CheckCode(s, "4711"));
    CHECK(!CheckCode(s, "4712") && !CheckCode(s, "47110") && !CheckCode(s, "") && !CheckCode(Secret(), "4711"));
    // The same code under another salt hashes otherwise; the same salt the same (stable).
    std::vector<uint8_t> other = salt;
    other[0] = 99;
    CHECK(MakeSecret("4711", other).hash != s.hash);
    CHECK(MakeSecret("4711", salt).hash == s.hash);
    // Stretched: not one plain SHA-256 of salt and code.
    std::vector<uint8_t> once(salt);
    for (char c : std::string("4711"))
        once.push_back(uint8_t(c));
    CHECK(Sha256::Hex(Sha256::Hash(once)) != s.hash);
    // No secret for a bad code or a short salt.
    CHECK(!MakeSecret("12", salt).Set() && !MakeSecret("1234", {1, 2}).Set());
}

TEST(wrong_codes_are_rate_limited)
{
    Limiter l;
    uint64_t const me = PlayerKey(42);
    int64_t t = 1000;
    CHECK(l.WaitFor(me, t) == 0);
    // The free tries.
    for (uint32_t i = 0; i < kFreeTries; ++i)
        CHECK(l.Miss(me, t) == 0);
    CHECK(l.WaitFor(me, t) == 0);
    // Then a wait, doubling, up to the most.
    CHECK(l.Miss(me, t) == kFirstLockout);
    CHECK(l.WaitFor(me, t) == kFirstLockout && l.WaitFor(me, t + 10) == kFirstLockout - 10);
    t += kFirstLockout;
    CHECK(l.WaitFor(me, t) == 0);
    CHECK(l.Miss(me, t) == 2 * kFirstLockout);
    for (int i = 0; i < 20; ++i)
        l.Miss(me, t);
    CHECK(l.WaitFor(me, t) == kMaxLockout);
    // Another player, and a lock, are counted apart.
    CHECK(l.WaitFor(PlayerKey(43), t) == 0 && l.WaitFor(LockKey(42), t) == 0);
    CHECK(PlayerKey(42) != LockKey(42));
    // A right code clears it.
    l.Clear(me);
    CHECK(l.WaitFor(me, t) == 0);
    // A lock left alone long enough starts over, and is forgotten.
    uint64_t const door = LockKey(7);
    for (uint32_t i = 0; i <= kFreeTries; ++i)
        l.Miss(door, t);
    CHECK(l.WaitFor(door, t) > 0);
    int64_t const later = t + kForgetSeconds + 1;
    CHECK(l.Miss(door, later) == 0);
    l.Forget(later + kForgetSeconds + kMaxLockout);
    CHECK(l.Size() == 0);
}

TEST(lock_messages_round_trip)
{
    SetLock s;
    s.piece = 0x01020304;
    s.mode = Mode::Password;
    s.code = "123456";
    std::vector<uint8_t> const b = EncodeSetLock(s);
    CHECK(b.size() == 7 + 6 && b[0] == kOpSetLock && b[1] == 0x04 && b[5] == uint8_t(Mode::Password) && b[6] == 6);
    SetLock back;
    CHECK(DecodeSetLock(b, back) && back.piece == s.piece && back.mode == Mode::Password && back.code == "123456");
    // A mode with no code (keeps the code a Password lock had, or isn't a code lock).
    SetLock guild;
    guild.piece = 9;
    guild.mode = Mode::Guild;
    CHECK(DecodeSetLock(EncodeSetLock(guild), back) && back.mode == Mode::Guild && back.code.empty());
    // Bad: a mode past the last, a length that lies, a letter in the code.
    std::vector<uint8_t> bad = b;
    bad[5] = kModeCount;
    CHECK(!DecodeSetLock(bad, back));
    bad = b;
    bad.pop_back();
    CHECK(!DecodeSetLock(bad, back));
    bad = b;
    bad[8] = 'x';
    CHECK(!DecodeSetLock(bad, back));

    uint32_t piece = 0;
    std::string code;
    std::vector<uint8_t> const t = EncodeTryCode(77, "4711");
    CHECK(t.size() == 10 && t[0] == kOpTryCode);
    CHECK(DecodeTryCode(t, piece, code) && piece == 77 && code == "4711");
    CHECK(!DecodeTryCode({kOpTryCode, 1, 0, 0, 0, 5, '1'}, piece, code));
    std::vector<uint8_t> const ask = EncodeAskCode(77, true);
    CHECK(ask == std::vector<uint8_t>({kOpAskCode, 77, 0, 0, 0, 1}));
    // The ops are free on the build channel: 4 and 5 after Toggle (3), 19 after the plot (18).
    CHECK(kOpSetLock == uint8_t(Build::ClientOp::Toggle) + 1 && kOpAskCode == 19);
}

TEST(a_lockable_pieces_mode_rides_in_its_flags)
{
    Build::Piece door;
    door.id = 5;
    door.type = PieceType::WoodDoor;
    door.health = Build::Def(door.type).health;
    door.lock = uint8_t(Mode::Password);
    door.owner = 3;
    std::vector<uint8_t> b = Build::EncodePieces({door}, 3);
    uint8_t const flags = b.back();
    CHECK((flags >> kPieceLockShift & 7) == uint8_t(Mode::Password));
    CHECK(flags & Build::kPieceMine);
    // A wall says nothing of locks.
    Build::Piece wall = door;
    wall.type = PieceType::WoodWall;
    b = Build::EncodePieces({wall}, 3);
    CHECK((b.back() >> kPieceLockShift & 7) == 0);
}
