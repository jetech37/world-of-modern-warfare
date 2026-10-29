/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_BUILD_H
#define FUSION_BUILD_H

#include "fusion/Common.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Building, after Valheim and Enshrouded: players put down floors, walls, roofs and stairs that
// snap to each other; each piece is held up by what it stands on, less with every storey and
// more with every reach outward (wood holds farther than stone sideways, stone taller), and a
// piece nothing holds falls. Pieces are boxes in the world, so bullets stop at them, blasts break
// them and NPCs see through only their openings.
//
// A piece's frame: x forward, y left, z up, turned by its yaw about z; its origin is the middle
// of its bottom edge (a wall's foot, a floor's underside). Yards, WoW axes.
namespace Fusion { namespace Build
{
    enum class PieceType : uint8_t
    {
        None,
        WoodFloor,
        WoodWall,
        WoodHalfWall,
        WoodDoorway,
        WoodWindow,
        WoodRoof,
        WoodStairs,
        WoodBeam,
        StoneFloor,
        StoneWall,
        StoneHalfWall,
        Sandbags,
        Campfire,
        Torch,
        SentryGun,      // shoots hostile creatures near its builder (Sentry.h)
        Bedroll,        // its builder comes back to life here (MW2's Tactical Insertion)
        WoodDoor,       // a doorway with a door that opens and shuts (F)
        Count,
    };

    enum class Material : uint8_t { Wood, Stone, Sandbag, Fire, Metal };

    struct MaterialRules
    {
        float verticalLoss;     // support lost a piece up
        float horizontalLoss;   // support lost a piece out
        float minSupport;       // below this a piece falls
        float blastScale;       // how much more a blast hurts it than a bullet
    };
    MaterialRules const& Rules(Material m);

    // A solid box of a piece: its centre in the piece's frame, half sizes along its own axes and
    // a pitch (radians, its +x tipped up toward +z: roofs and ramps).
    struct Box
    {
        Vec3 center;
        Vec3 half;
        float pitch = 0.f;
    };

    // What a piece costs: an item (WoW's own trade goods) and how many.
    struct Cost
    {
        uint32_t item = 0;
        uint32_t count = 0;
    };

    constexpr uint32_t kSimpleWood = 4470;
    constexpr uint32_t kRoughStone = 2835;
    constexpr uint32_t kLinenCloth = 2589;
    constexpr uint32_t kCopperBar = 2840;

    struct PieceDef
    {
        std::string name;
        Material material = Material::Wood;
        float health = 0.f;             // CoD damage it takes to break (an M4 round is 30)
        std::vector<Box> boxes;
        std::vector<Vec3> snaps;        // points others snap to, in the piece's frame
        float top = 0.f;                // a floor's walking height above its origin
        bool groundOnly = false;        // must stand on the ground (sandbags, a campfire)
        Cost cost;
    };

    PieceDef const& Def(PieceType t);
    bool Known(uint8_t type);

    // ── Styles ──
    // A built piece in another culture's look: Elwynn timber and plaster is the plain one (the
    // types above), then Stormwind's keep stone, Ironforge's brick, the orcs' spiked timber and
    // the night elves' moonwood. To the rules a styled piece is its plain one (the same boxes,
    // health, snaps and support, BaseOf); the client draws it in that culture's textures and
    // models, and its recipe asks a little of that culture's trade goods (Construction.cpp). A
    // styled piece is a type of its own on the wire and in the pieces table, so nothing else
    // changes: kStyleBase + (style - 1) * kStyledSlots + its slot in StyledPieces().
    constexpr uint8_t kStyleBase = 20;
    constexpr uint8_t kStyleCount = 5;      // 0 Elwynn (plain), 1 Stormwind, 2 Ironforge, 3 Orcish, 4 Night elf
    constexpr uint8_t kStyledSlots = 16;
    // The plain pieces a style redraws, in slot order (every built piece but the sentry gun).
    std::vector<PieceType> const& StyledPieces();
    // `base` in `style`: `base` itself for style 0; None for a piece no style redraws, an unknown
    // style, or a type that is already styled.
    PieceType Styled(PieceType base, uint8_t style);
    // The plain piece a type draws (itself when it isn't styled), and its style (0 when plain).
    PieceType BaseOf(PieceType t);
    uint8_t StyleOf(PieceType t);
    char const* StyleName(uint8_t style);

    // WoW's own props, placed as they stand in the world (tents, barrels, fences...): ids from
    // kPropBase, each boxed by its model's bounds (the client's examples/prop_bounds.rs prints the
    // table). Ground pieces, wood or stone by name.
    constexpr uint8_t kPropBase = 100;
    struct Prop
    {
        uint8_t id;
        char const* name;
        Vec3 center;
        Vec3 half;
    };
    std::vector<Prop> const& Props();
    inline bool IsProp(PieceType t) { return uint8_t(t) >= kPropBase; }
    // The last prop is no prop of the world but a working thing: a mage's portal (the Stormwind
    // mage portal's model) that sends its users to their faction's capitals or their homestead
    // (Portals.h). It is a ground piece like the props; only its use is its own.
    constexpr uint8_t kPortalProp = 128;
    inline bool IsPortal(PieceType t) { return uint8_t(t) == kPortalProp; }

    // Whole structures of WoW's own models (a cottage, a watchtower, a barn, a smithy, a market
    // stall, a palisade and its gate, bridges, a well, a guard post...): ids from kStructureBase,
    // ground pieces like the props (IsProp holds for them too). The client draws each as one or
    // more models; its boxes are worked out from their triangles on a grid (the client's
    // examples/structure_boxes.rs prints the table): walls and posts are solid to their top, a
    // deck or a bridge carries, a roof or an awning is a slab overhead with the room under it
    // clear.
    constexpr uint8_t kStructureBase = 140;
    struct Structure
    {
        uint8_t id;
        char const* name;
        bool stone;
        std::vector<Box> boxes;
    };
    std::vector<Structure> const& Structures();
    // Furniture (Furniture.h) follows from 200: props too (ground pieces of WoW's models), not
    // structures.
    constexpr uint8_t kFurnitureBase = 200;
    inline bool IsStructure(PieceType t) { return uint8_t(t) >= kStructureBase && uint8_t(t) < kFurnitureBase; }
    inline bool IsFurniturePiece(PieceType t) { return uint8_t(t) >= kFurnitureBase; }

    // A new door's or portal's lock: Locks::Mode::Party, the owner and their party.
    constexpr uint8_t kDefaultLock = 2;

    // Piece::paid: bit 0 the piece itself, bit t its reinforcement tier t (1 to kMaxReinforcement).
    constexpr uint8_t kPaidPiece = 0x01;
    constexpr uint8_t kPaidAll = 0x0F;
    constexpr uint8_t PaidTier(uint8_t tier) { return uint8_t(1u << tier); }

    struct Piece
    {
        uint32_t id = 0;
        PieceType type = PieceType::None;
        Vec3 pos;
        float yaw = 0.f;
        float health = 0.f;
        float support = 1.f;
        uint32_t owner = 0;             // the builder's character guid (low)
        uint32_t map = 0;
        bool open = false;              // a door stands open
        uint8_t reinforcement = 0;      // 0 to kMaxReinforcement (Construction.h's tiers)
        uint8_t lock = kDefaultLock;    // a door's or a portal's lock (Locks::Mode)
        // Upkeep (Maintenance.h): the time (unix seconds) `health` was last brought up to date by
        // decay (0 for a piece from before decay), the last time it was hit (0 never; not kept
        // across restarts), and whether a paid plot keeps it from decaying (the wire's flag).
        int64_t decayedAt = 0;
        int64_t damagedAt = 0;
        bool kept = false;
        // What was paid for it (kPaid bits): the piece's own recipe, and each reinforcement tier.
        // A piece built or reinforced free (a free realm, a game master's help) pays nothing of
        // that part back: no refund, salvage or packing (security review 2026-10, #14). Pieces
        // from before the flag count as paid in full.
        uint8_t paid = kPaidAll;
    };

    // ── Open-world rules (design doc, section 12) ──
    // Out in the world nothing protects a piece (a bought plot does, Plots.h): guns break a wood
    // wall in under a magazine and a stone one in two, spells in two casts or four, and the owner
    // can pay to reinforce it, each tier multiplying its health and turning part of each bullet
    // (and each blow); blasts and spells are not turned, so explosives answer a fortified wall.
    constexpr uint8_t kMaxReinforcement = 3;
    static_assert(kPaidAll == uint8_t((PaidTier(kMaxReinforcement) << 1) - 1), "a paid bit for the piece and each tier");
    float ReinforcedHealthMultiplier(uint8_t tier);     // 1, 2, 3.5, 6
    float ReinforcedBulletReduction(uint8_t tier);      // 0, 25%, 45%, 65%
    float MaxHealth(PieceType t, uint8_t reinforcement);
    float MaxHealth(Piece const& p);

    enum class DamageKind : uint8_t { Bullet, Blast, Spell, Melee };
    // What `damage` (CoD) of a kind takes off a piece: a blast scaled by its material, a bullet
    // or a blow less its reinforcement's cut.
    float DamageTaken(Piece const& p, float damage, DamageKind kind);

    // A spell's damage to a piece: kSpellPieceDamage for a spell as strong as a creature of the
    // caster's level is expected to cast (Scale::ExpectedWowSpell), a third of it at the least and
    // three times at the most; a periodic tick a fifth of that. A spell hits the pieces within
    // its area, or within kSpellSingleRadius of a single target, and the piece between its caster
    // and its target.
    constexpr float kSpellPieceDamage = 450.f;
    constexpr float kSpellSingleRadius = 2.f;
    float SpellPieceDamage(float wowDamage, uint32_t casterLevel, bool tick);

    // A piece's boxes as it stands now (an open door's panel swung back).
    std::vector<Box> BoxesOf(Piece const& p);

    // Where a piece's box sits in the world: centre and its three axes.
    struct WorldBox
    {
        Vec3 center;
        Vec3 axis[3];
        Vec3 half;
    };
    WorldBox Place(Piece const& p, Box const& b);
    Vec3 ToWorld(Piece const& p, Vec3 local);
    Vec3 Center(Piece const& p);        // the middle of its boxes

    // The first box of the piece a ray meets within `maxDistance` (`t` its distance).
    bool RayHits(Piece const& p, Vec3 origin, Vec3 dir, float maxDistance, float& t);
    // Whether a segment passes through any box of the piece.
    bool SegmentBlocked(Piece const& p, Vec3 from, Vec3 to);

    // The top of a piece's boxes under (x, y): the highest surface a body coming down from `z`
    // lands on, or none. A box whose top is over `z` (the point inside it, or under a wall too
    // tall for the search) is no surface under it.
    std::optional<float> TopUnder(Piece const& p, float x, float y, float z);
    std::optional<float> TopUnder(std::vector<Piece> const& pieces, float x, float y, float z);

    // How far a point is from a piece: to the nearest point of its boxes (0 inside one).
    float Distance(Piece const& p, Vec3 point);

    // Whether two pieces' boxes touch (within `slack` yards).
    bool Touch(Piece const& a, Piece const& b, float slack = 0.06f);
    // Whether `p` sits inside a piece of `pieces` (deep overlap, or the same piece again).
    bool Overlaps(std::vector<Piece> const& pieces, Piece const& p);

    // Ground height under a point, searched from `z` down (WoW's terrain and WMO floors on the
    // server).
    using Ground = std::function<float(float x, float y, float z)>;
    // A piece touches the ground when its lowest point is this close above it (or under it).
    constexpr float kGroundedSlack = 0.25f;
    bool Grounded(Piece const& p, Ground const& ground);
    // Ground pieces (sandbags, a campfire, WoW's props) may also stand on a built floor: the top of
    // a floor (a piece whose def has a walking top) under the piece's middle, within kFloorSlack of
    // its base. A wall's or a beam's top is no floor.
    constexpr float kFloorSlack = 0.15f;
    bool OnFloor(Piece const& p, std::vector<Piece> const& pieces);
    bool Footed(Piece const& p, std::vector<Piece> const& pieces, Ground const& ground);

    // ── Bodies: nobody is shut inside a piece ──
    // A player's body as the client moves it (benilla's mover): a capsule this wide and tall
    // standing on its feet. A piece whose boxes cut into one is never put up (Refusal::Traps),
    // and a body found inside one anyway (a door shut on it, a plot's pieces coming back) is
    // moved to the nearest spot clear of them.
    constexpr float kBodyRadius = 0.3333f;
    constexpr float kBodyHeight = 2.0278f;
    // How deep a box may reach into a body and leave it free: a wall leant on, a floor stood on,
    // the mover's skin.
    constexpr float kBodySlack = 0.08f;
    // Whether a piece's boxes cut into a body standing at `feet` (WoW position: its feet).
    bool Traps(Piece const& p, Vec3 feet);
    bool Traps(std::vector<Piece> const& pieces, Vec3 feet);
    // The nearest spot to `feet` where a body stands clear of `pieces`: rings outward every half
    // yard to kFreeSearch, each spot on the higher of the ground and a piece's top under it (no
    // more than kFreeClimb above the feet, no step a body couldn't make out of a doorway); the
    // nearest of the first ring that has one. `feet` itself when it is clear; none when nothing
    // within the search is. `ground` may be empty (the feet's height everywhere).
    constexpr float kFreeSearch = 30.f;
    constexpr float kFreeClimb = 1.5f;
    std::optional<Vec3> FreeSpot(std::vector<Piece> const& pieces, Vec3 feet, Ground const& ground);

    // Support of every piece by id: grounded ones hold 1, the rest what their best neighbour
    // passes on less their material's loss up or out.
    using Supports = std::unordered_map<uint32_t, float>;
    Supports Solve(std::vector<Piece> const& pieces, Ground const& ground);
    bool Stands(PieceType t, float support);
    // Remove piece `id`, then every piece left without support, until all stand; returns the
    // ids that fell (not `id`).
    std::vector<uint32_t> Collapse(std::vector<Piece>& pieces, uint32_t id, Ground const& ground);
    // Collapse over a window of the world: `pieces` are those gathered within a wide radius of
    // `at` (kCollapseGather), but only pieces within `fallRadius` of it may fall or have their
    // support changed. A piece near the window's edge may hold up on something just outside it;
    // judged from inside the window alone it would seem to float and be wrongly taken.
    constexpr float kCollapseGather = 120.f;
    constexpr float kCollapseFall = 50.f;
    std::vector<uint32_t> CollapseNear(std::vector<Piece>& pieces, uint32_t id, Ground const& ground, Vec3 at,
        float fallRadius = kCollapseFall);

    // Snapping: where a piece of `type` goes when aimed at `aim` with yaw `yaw` (turned to the
    // nearest quarter of the piece it snaps to): one of its snap points on a snap point of a
    // piece within `radius` of the aim, the fit closest to the aim that overlaps nothing.
    struct Placement
    {
        Vec3 pos;
        float yaw = 0.f;
    };
    bool Snap(std::vector<Piece> const& pieces, PieceType type, Vec3 aim, float yaw, float radius, Placement& out);

    // How far from a player a piece may be worked (taken down, reinforced: the reach of a hammer).
    constexpr float kReach = 10.f;
    // How far out a piece may be put down: the client's wheel sets it from 2 to 25 yards, to its
    // nearest point (a structure's middle is farther).
    constexpr float kPlaceReach = 25.f;
    // There is no cap on how many pieces a player owns: their cost and their upkeep (decay and
    // repair, Maintenance.h) are the limit. The status op's max is 0 for "no cap".
    // Pieces stream to players this near, and leave them past the far distance.
    constexpr float kStreamNear = 160.f;
    constexpr float kStreamFar = 220.f;

    // ── The wire: CHANNEL_BUILD (fusion channel 1) bodies. Little-endian. ──
    enum class ClientOp : uint8_t
    {
        Place = 1,          // type u8, x y z yaw f32
        Remove = 2,         // id u32 (the owner takes it down; the materials come back)
        Toggle = 3,         // id u32: open or shut a door
        // 4 and 5 are Locks.h's (set a lock, try a code), 6 and 7 Portals.h's (ask, travel).
    };
    enum class ServerOp : uint8_t
    {
        Pieces = 10,        // count u16, each: id u32, type u8, x y z yaw f32, health per mille u16, support u8, flags u8
        Removed = 11,       // count u16, each: id u32, cause u8
        Health = 12,        // count u16, each: id u32, health per mille u16, support u8
        Refused = 13,       // reason u8
        Status = 14,        // owned u16, max u16 (0: no cap), free u8 (builds without materials)
        SentryFire = 15,    // piece id u32, target guid u64, hit u8: a sentry's burst at a target
        // 16 and 17 are Construction.h's (recipes, skill), 18 Plots.h's (the plot here), 19
        // Locks.h's (a code asked for), 20 Portals.h's (a portal's destinations).
    };
    constexpr uint8_t kPieceMine = 0x01;
    constexpr uint8_t kPieceOpen = 0x02;
    // Bits 2 and 3 of a piece's flags: its reinforcement tier.
    constexpr uint8_t kPieceReinforceShift = 2;
    // Bits 4-6: a door's or portal's lock (Locks.h kPieceLockShift); 0 on any other piece.
    // Bit 7: a paid plot keeps it, so it doesn't decay (Maintenance.h).
    constexpr uint8_t kPieceKept = 0x80;
    // How near a player must be to work a door.
    constexpr float kDoorReach = 4.5f;

    enum class RemoveCause : uint8_t { TakenDown, Destroyed, Collapsed };
    enum class Refusal : uint8_t
    {
        Unsupported = 1,
        Overlaps,
        TooMany,            // no longer sent: there is no cap on pieces
        TooFar,
        NoMaterials,
        NotYours,
        Unknown,
        InCombat,
        SkillTooLow,        // Construction (Construction.h): the piece wants more skill
        NotLearned,         // no Construction yet
        Protected,          // someone's plot (Plots.h), or a district's road
        NotReinforceable,   // the camp's things and WoW's props take no reinforcement
        MaxReinforcement,   // already at the top tier
        Traps,              // it would shut a player inside it (the builder or anyone near)
        OutOfSight,         // the builder can't see where it would go (through a wall or a hill)
        CrowdsNpc,          // too near a vendor, trainer, flight master, steward or the like
        Locked,             // a door or portal whose lock keeps this player out (Locks.h)
        WrongCode,          // the code given doesn't open it
        TooManyTries,       // too many wrong codes: wait a little
        CoolingDown,        // the hearthstone's cooldown, which portals and the courier share
        PortalLimit,        // as many portals as a player (or a plot) may have
        NoKit,              // Construction: no kit of the piece made yet (Construction.h Kits)
    };
    // Nothing is built this near (yards, to the piece's nearest point) a creature with any NPC
    // service (npc flags): nobody walls in a flight master, a bank or a Plot Steward.
    constexpr float kNpcClearance = 6.f;
    bool CrowdsNpc(Piece const& p, Vec3 npcPos, uint32_t npcFlags);
    // How far short of a new piece's middle the builder's line of sight is traced, so the pieces
    // it snaps against (a floor under it, a wall beside it) never block their own neighbour.
    constexpr float kSightSlack = 0.75f;

    struct PlaceRequest
    {
        PieceType type = PieceType::None;
        Vec3 pos;
        float yaw = 0.f;
    };
    bool DecodePlace(std::vector<uint8_t> const& body, PlaceRequest& out);
    bool DecodeRemove(std::vector<uint8_t> const& body, uint32_t& id);
    bool DecodeToggle(std::vector<uint8_t> const& body, uint32_t& id);
    std::vector<uint8_t> EncodePieces(std::vector<Piece> const& pieces, uint32_t viewer);
    std::vector<uint8_t> EncodeRemoved(std::vector<uint32_t> const& ids, RemoveCause cause);
    std::vector<uint8_t> EncodeHealth(std::vector<Piece> const& pieces);
    std::vector<uint8_t> EncodeRefused(Refusal r);
    std::vector<uint8_t> EncodeStatus(uint32_t owned, uint32_t max, bool free);
    std::vector<uint8_t> EncodeSentryFire(uint32_t piece, uint64_t target, bool hit);
    // A player keeps one bedroll: a new one takes the old one's place.
    constexpr bool kOneBedrollEach = true;
}}

#endif
