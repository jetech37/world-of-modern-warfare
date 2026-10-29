/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_ACTION_SYSTEMS_H
#define FUSION_ACTION_SYSTEMS_H

#include <cstdint>
#include <optional>
#include <vector>

// The three action systems (WOMW): Modern (the MW2 gun and gear), Classic (WoW's combat: melee,
// spells, class abilities) and Utility (the weapons put away: bare hands and the trades' tools,
// Gathering.h). The client switches between them (X, Z) and tells the server, which swings
// Utility's fists unarmed and works the tool the hands hold.
//
// Each system has its own action bar, saved as WoW's action buttons are (character_action): the
// layer's own abilities are buttons of their own type, kFusionButtonType, which only their own
// systems' bars take. The bars' buttons (0-based, as the wire numbers them):
//   Classic  0..59    WoW's first five pages, where the kit is laid out (FusionClasses).
//   Utility  60..71   the sixth page, and 210..215 (the last of bonus bar 12) for its third page
//                     (the instruments of Minstrelsy made two pages too few).
//   Modern   192..209 bonus bar 11 and the first half of 12, which no form uses (DruidForms.h).
// The druid forms' own pages (bonus bars 1-10, buttons 72..191) are WoW's.
namespace Fusion::ActionSystems
{
    // The channel and its ops (little-endian):
    //   1 client: the active system, one byte.
    //   2 client: the tool readied in Utility's hands: its ability id u8 (0 bare hands).
    //   3 client: a strike of the readied tool: tool ability id u8, target u64 (a gameobject's or
    //     a creature's guid, a built piece's id; 0 for none: fishing's cast and reel).
    //   4 server: how a strike went: tool ability id u8, Gathering::Outcome u8, strikes done u8,
    //     strikes needed u8.
    //   5-8: what others see of the hands (client pose and act, their relays): see below.
    //   9 server: the tools Utility's bar shows, a u16 mask (bit n: tool ability kFirstTool + n):
    //     those the player carries (bags or equipped) and has the trade for (Gathering.h
    //     OnUtilityBar). Sent at login and whenever it changes; the client adds and removes the
    //     tools' buttons to match, bare hands always there.
    constexpr uint8_t kChannel = 10;
    constexpr uint8_t kOpSystem = 1;
    constexpr uint8_t kOpTool = 2;
    constexpr uint8_t kOpStrike = 3;
    constexpr uint8_t kOpStruck = 4;
    constexpr uint8_t kOpTools = 9;

    enum class System : uint8_t
    {
        Modern = 0,
        Classic = 1,
        Utility = 2,
    };

    // The layer's abilities, by id (the button's action).
    enum class Ability : uint32_t
    {
        Primary = 1,
        Secondary = 2,
        Underbarrel = 3,
        Killstreak = 4,
        Lethal = 5,
        Tactical = 6,
        SpecialAmmo = 7,
        Rearm = 8,
        BareHands = 9,
        // Utility's tools (Gathering.h): each held in the hands, each on Utility's bar only.
        MiningPick = 10,
        SkinningKnife = 11,
        Herbalism = 12,
        FishingPole = 13,
        WoodcuttingAxe = 14,
        BlacksmithHammer = 15,
        RepairHammer = 16,
        // Minstrelsy's instruments (Minstrelsy.h): tools too, played rather than swung.
        HandDrum = 17,
        Lute = 18,
        Flute = 19,
        Harp = 20,
        Bagpipes = 21,
        ThunderDrums = 22,
        // Construction's Build Structure (Construction.h Kits): build mode, a crafted kit placed in
        // the world. Utility's alone, and no tool (nothing is held: the hands build).
        BuildStructure = 23,
        // The Companion system's bar (the client's fourth bar, a view over the hands' system):
        // one entry per companion pet bar slot 1-10 (Companions.h PetBarSlot). Pressed, the client
        // presses that pet bar slot over the companion channel; on the Companion bar alone.
        CompanionSlot1 = 24,
        CompanionSlot10 = 33,
    };
    constexpr uint32_t kLastAbility = uint32_t(Ability::CompanionSlot10);
    inline bool IsCompanionSlot(uint32_t id)
    {
        return id >= uint32_t(Ability::CompanionSlot1) && id <= uint32_t(Ability::CompanionSlot10);
    }
    constexpr uint32_t kFirstTool = uint32_t(Ability::MiningPick);
    constexpr uint32_t kLastTool = uint32_t(Ability::ThunderDrums);
    inline bool IsInstrument(uint32_t id) { return id >= uint32_t(Ability::HandDrum) && id <= kLastTool; }
    inline bool IsTool(uint32_t id) { return id >= kFirstTool && id <= kLastTool; }
    // A tool's bit in the tools mask (op 9); 0 for anything else.
    inline uint16_t ToolBit(uint32_t id) { return IsTool(id) ? uint16_t(1u << (id - kFirstTool)) : uint16_t(0); }
    // Every tool's bit.
    constexpr uint16_t kAllTools = uint16_t((1u << (kLastTool - kFirstTool + 1)) - 1);

    // The action button type of a layer ability (vmangos ActionButtonType has 0x00, 0x01, 0x40
    // and 0x80).
    constexpr uint8_t kFusionButtonType = 0x20;
    // Every button there is (Player.h MAX_ACTION_BUTTONS): DruidForms.h kActionButtons (216) and
    // the Companion bar's two pages after them.
    constexpr uint16_t kButtons = 228;
    constexpr uint8_t kSlotsPerPage = 6;

    struct Range
    {
        uint8_t first;
        uint8_t last;
        bool Holds(uint16_t button) const { return button >= first && button <= last; }
    };
    Range RangeOf(System s);
    // Utility's third page, apart from its first two.
    constexpr Range kUtilityExtra{210, 215};
    // The Companion bar's two pages (Lua actions 217-228), past every form page: its slots only.
    constexpr Range kCompanionBar{216, 227};
    // The system whose bar holds a button, none for a form page.
    std::optional<System> SystemOfButton(uint16_t button);

    bool KnownAbility(uint32_t id);
    // Whether an ability goes on a system's bar: bare hands on Classic's and Utility's, the tools
    // on Utility's alone, the rest on Modern's.
    bool AbilityOn(uint32_t id, System s);

    // Whether a button may hold an action of a type: a layer ability only on its own systems'
    // bars, WoW's own (spells, items, macros) only on Classic's bar and the form pages.
    bool ButtonAllows(uint16_t button, uint32_t action, uint8_t type);

    // The client's op: the system it names, none for a malformed one.
    std::optional<System> DecodeSystem(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodeSystem(System s);

    // The client's tool op: the tool readied (0 bare hands), none for a malformed one or an id
    // that is no tool.
    std::optional<uint32_t> DecodeTool(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodeTool(uint32_t tool);
    struct StrikeRequest
    {
        uint32_t tool = 0;
        uint64_t target = 0;
    };
    std::optional<StrikeRequest> DecodeStrike(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodeStrike(StrikeRequest const& r);
    std::vector<uint8_t> EncodeStruck(uint32_t tool, uint8_t outcome, uint8_t done, uint8_t needed);
    // The server's tools op: [9, mask u16 LE]; none for a malformed one (a bit past the tools).
    std::vector<uint8_t> EncodeTools(uint16_t mask);
    std::optional<uint16_t> DecodeTools(std::vector<uint8_t> const& body);

    // Utility swings bare fists: the weapons are put away.
    inline bool Unarmed(System s) { return s == System::Utility; }

    // ── What others see of a player's hands (the third-person body, client/src/body_state.rs) ──
    // The client tells its pose when it changes (at most ~10 a second, and once a second
    // regardless) and each hand action as it happens; the server relays both, with the player's
    // guid in front, to every fusion player within kTellRadius but the player.
    // client op 5, 10 bytes: [5, system u8, flags u8 (kPose*), stance u8 (Packets Stance),
    //                         pitch i8 (degrees, -90..90), gun u16 (catalog id, 0 none),
    //                         main subclass u8, off subclass u8 (255 none), tool u8 (ToolKind)]
    // client op 6, 3 bytes:  [6, kind u8 (Act), arg u8]
    // server op 7, 18 bytes: [7, guid u64, the pose's 9 bytes after its op]
    // server op 8, 11 bytes: [8, guid u64, kind u8, arg u8]
    constexpr uint8_t kOpPose = 5;
    constexpr uint8_t kOpAct = 6;
    constexpr uint8_t kOpPoseOf = 7;
    constexpr uint8_t kOpActOf = 8;
    constexpr size_t kPoseBytes = 9;
    constexpr float kTellRadius = 100.f;
    // Pose flags.
    constexpr uint8_t kPoseGunUp = 0x01;      // Modern: the gun is up (not a hand of melee)
    constexpr uint8_t kPoseAds = 0x02;
    constexpr uint8_t kPoseReloading = 0x04;
    constexpr uint8_t kPoseSprint = 0x08;
    constexpr uint8_t kPoseCastRight = 0x10;  // a spell held, charging or pouring in that hand
    constexpr uint8_t kPoseCastLeft = 0x20;
    constexpr uint8_t kPoseBlock = 0x40;      // the shield raised
    constexpr uint8_t kPoseTwoHanded = 0x80;
    // The last valid act kind.
    constexpr uint8_t kActLast = 7;           // 1 swing, 2 cast, 3 throw, 4 reload, 5 punch, 6 tool, 7 fire

    struct Pose
    {
        uint8_t system = 0;
        uint8_t flags = 0;
        uint8_t stance = 0;
        int8_t pitch = 0;
        uint16_t gun = 0;
        uint8_t mainSubclass = 255;
        uint8_t offSubclass = 255;
        uint8_t tool = 0;
    };
    // The client's pose op; none for a malformed one (a bad system or stance, a pitch past 90).
    std::optional<Pose> DecodePose(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodePose(Pose const& p);
    // The relay of a pose the client sent: its bytes after the op, behind the guid.
    std::vector<uint8_t> EncodePoseOf(uint64_t guid, Pose const& p);
    struct Act
    {
        uint8_t kind = 0;
        uint8_t arg = 0;
    };
    std::optional<Act> DecodeAct(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodeActOf(uint64_t guid, Act a);
}

#endif
