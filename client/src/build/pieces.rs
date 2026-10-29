//! The pieces and their geometry, the server's `fusion/Build.h` mirrored: the same boxes and snap
//! points, so a ghost lands where the server will put the piece. WoW axes and yards; a piece's
//! frame is x forward, y left, z up, turned by its yaw, its origin the middle of its bottom edge.

use bevy::math::Vec3;

/// A piece's kind as the wire numbers it: the built pieces 1 to 17, WoW's props from 100
/// ([`PROPS`]).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PieceType(pub u8);

#[allow(non_upper_case_globals)]
impl PieceType {
    pub const WoodFloor: PieceType = PieceType(1);
    pub const WoodWall: PieceType = PieceType(2);
    pub const WoodHalfWall: PieceType = PieceType(3);
    pub const WoodDoorway: PieceType = PieceType(4);
    pub const WoodWindow: PieceType = PieceType(5);
    pub const WoodRoof: PieceType = PieceType(6);
    pub const WoodStairs: PieceType = PieceType(7);
    pub const WoodBeam: PieceType = PieceType(8);
    pub const StoneFloor: PieceType = PieceType(9);
    pub const StoneWall: PieceType = PieceType(10);
    pub const StoneHalfWall: PieceType = PieceType(11);
    pub const Sandbags: PieceType = PieceType(12);
    pub const Campfire: PieceType = PieceType(13);
    pub const Torch: PieceType = PieceType(14);
    pub const SentryGun: PieceType = PieceType(15);
    pub const Bedroll: PieceType = PieceType(16);
    pub const WoodDoor: PieceType = PieceType(17);

    pub fn from_u8(v: u8) -> Option<Self> {
        // A styled piece (crate::build::look) is its own type on the wire.
        if PieceType(v).style() != 0 {
            return Some(PieceType(v));
        }
        all().iter().copied().find(|t| t.0 == v)
    }

    /// One of WoW's props (a barrel, a tent).
    pub fn is_prop(self) -> bool {
        self.0 >= PROP_BASE
    }

    /// The buildable portal (the server's `Build::kPortalProp`).
    pub fn is_portal(self) -> bool {
        self.0 == PORTAL
    }
}

/// The portal's piece type: the last prop, a mage's portal that sends its users to their
/// faction's capitals or their homestead (the server's `Portals.h`).
pub const PORTAL: u8 = 128;
/// The portal's model is a city's great ring (33 yards tall as it stands in the install); it is
/// drawn at this scale, a 5-yard arch, and its box in [`PROPS`] is the model's bounds
/// (`examples/prop_bounds.rs`) times it.
pub const PORTAL_SCALE: f32 = 0.15;

/// Whether a piece takes a lock (the server's `Locks::Lockable`): doors, in every style, and the
/// portal.
pub fn lockable(t: PieceType) -> bool {
    t.base() == PieceType::WoodDoor || t.is_portal()
}

/// The built pieces, in the build bar's order.
const BUILT: [PieceType; 17] = [
    PieceType::WoodFloor,
    PieceType::WoodWall,
    PieceType::WoodHalfWall,
    PieceType::WoodDoorway,
    PieceType::WoodDoor,
    PieceType::WoodWindow,
    PieceType::WoodRoof,
    PieceType::WoodStairs,
    PieceType::WoodBeam,
    PieceType::StoneFloor,
    PieceType::StoneWall,
    PieceType::StoneHalfWall,
    PieceType::Sandbags,
    PieceType::Campfire,
    PieceType::Torch,
    PieceType::SentryGun,
    PieceType::Bedroll,
];

/// Every piece and prop, in the build bar's order.
pub fn all() -> &'static [PieceType] {
    static ALL: std::sync::OnceLock<Vec<PieceType>> = std::sync::OnceLock::new();
    ALL.get_or_init(|| {
        BUILT
            .iter()
            .copied()
            .chain(PROPS.iter().map(|p| PieceType(p.id)))
            .chain(STRUCTURES.iter().map(|s| PieceType(s.id)))
            .chain(FURNITURE.iter().map(|f| PieceType(f.id)))
            .collect()
    })
}

/// What the build bar and menu offer: every piece but the sentry gun (a killstreak now, set down
/// by the server; it still comes over the wire as a piece), then WoW's props.
pub fn buildable() -> &'static [PieceType] {
    static BAR: std::sync::OnceLock<Vec<PieceType>> = std::sync::OnceLock::new();
    BAR.get_or_init(|| {
        all()
            .iter()
            .copied()
            .filter(|&t| t != PieceType::SentryGun)
            .collect()
    })
}

/// The first prop's id.
pub const PROP_BASE: u8 = 100;

/// One of WoW's own props, placed as it stands in the world: its model, and its bounds (the
/// model's box, from `examples/prop_bounds.rs`) for collision.
pub struct Prop {
    pub id: u8,
    pub name: &'static str,
    pub model: &'static str,
    pub center: [f32; 3],
    pub half: [f32; 3],
}

/// The props building offers (the server's `Build.cpp` holds the same table); the last is the
/// portal ([`PORTAL`]).
pub static PROPS: [Prop; 29] = [
    Prop {
        id: 100,
        name: "Campfire",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Campfire\\ElwynnCampfire.m2",
        center: [0.15, 0.06, 0.31],
        half: [1.01, 1.07, 0.41],
    },
    Prop {
        id: 101,
        name: "Bed",
        model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Furniture\\westfallbed01.m2",
        center: [0.00, 0.00, 0.58],
        half: [1.09, 1.54, 0.60],
    },
    Prop {
        id: 102,
        name: "Barrel",
        model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
        center: [-0.00, 0.03, 0.50],
        half: [0.43, 0.50, 0.50],
    },
    Prop {
        id: 103,
        name: "Crate",
        model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
        center: [0.00, 0.00, 0.25],
        half: [0.43, 0.76, 0.25],
    },
    Prop {
        id: 104,
        name: "Big crate",
        model: "World\\Azeroth\\Karazahn\\PassiveDoodads\\Crates\\KarazahnCrate01.m2",
        center: [0.01, 0.01, 0.47],
        half: [0.53, 0.58, 0.47],
    },
    Prop {
        id: 105,
        name: "Grain sack",
        model: "World\\Azeroth\\Karazahn\\PassiveDoodads\\GrainSacks\\KN_GrainSack01.m2",
        center: [-0.01, 0.04, 0.16],
        half: [0.56, 0.75, 0.16],
    },
    Prop {
        id: 106,
        name: "Haystack",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Haystacks\\haystack01.m2",
        center: [-0.01, -0.00, 0.35],
        half: [0.37, 0.72, 0.35],
    },
    Prop {
        id: 107,
        name: "Hay wagon",
        model: "World\\Azeroth\\Westfall\\PassiveDoodads\\HayWagon\\WestfallHayWagon.m2",
        center: [1.40, 0.23, 2.04],
        half: [3.87, 2.52, 2.17],
    },
    Prop {
        id: 108,
        name: "Mine cart",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\MineCart\\ElwynnMineCart.m2",
        center: [-0.77, -0.74, 1.16],
        half: [3.85, 2.93, 1.66],
    },
    Prop {
        id: 109,
        name: "Lamp post",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\LampPost\\LampPost.m2",
        center: [0.09, 0.30, 2.04],
        half: [0.20, 0.87, 2.05],
    },
    Prop {
        id: 110,
        name: "Anvil",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2",
        center: [0.05, 0.00, 0.48],
        half: [0.73, 0.36, 0.48],
    },
    Prop {
        id: 111,
        name: "Round table",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tables\\RoundTable\\RoundTable.m2",
        center: [0.01, 0.02, 0.44],
        half: [1.10, 1.15, 0.44],
    },
    Prop {
        id: 112,
        name: "Wood fence",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnWoodFence01.m2",
        center: [-2.01, -0.03, 0.67],
        half: [2.15, 0.16, 1.23],
    },
    Prop {
        id: 113,
        name: "Fence post",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnWoodPost01.m2",
        center: [0.00, -0.01, 0.67],
        half: [0.14, 0.14, 1.23],
    },
    Prop {
        id: 114,
        name: "Stone fence",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnStoneFence.m2",
        center: [-0.01, 2.66, 0.66],
        half: [0.28, 2.93, 0.66],
    },
    Prop {
        id: 115,
        name: "Stone fence post",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnStoneFencePost.m2",
        center: [-0.01, 0.01, 0.66],
        half: [0.28, 0.28, 0.66],
    },
    Prop {
        id: 116,
        name: "Spike wall",
        model: "World\\Azeroth\\RedRidge\\PassiveDoodads\\spikewall\\BattleGladeWoodSpikesLongRow.m2",
        center: [2.62, 0.11, 1.23],
        half: [11.68, 2.48, 1.47],
    },
    Prop {
        id: 117,
        name: "Banner",
        model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\BattleGladeBanner1\\BattleGladeBanner1.m2",
        center: [0.01, 0.16, 1.14],
        half: [0.18, 0.42, 1.14],
    },
    Prop {
        id: 118,
        name: "Large tent",
        model: "World\\Generic\\Buildings\\HumanTentLarge\\HumanTentLarge.m2",
        center: [0.10, 0.00, 2.10],
        half: [6.07, 6.63, 2.13],
    },
    Prop {
        id: 119,
        name: "Tent",
        model: "World\\Generic\\Buildings\\HumanTentMedium\\HumanTentMedium.m2",
        center: [-0.74, 0.09, 2.10],
        half: [4.40, 6.81, 2.13],
    },
    Prop {
        id: 120,
        name: "Orc tent",
        model: "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcTents\\OrcTent.m2",
        center: [-0.07, -0.20, 3.35],
        half: [3.48, 3.36, 3.45],
    },
    Prop {
        id: 121,
        name: "Goblin tent",
        model: "World\\Generic\\Goblin\\PassiveDoodads\\GoblinTents\\GoblinTent01.m2",
        center: [-0.22, -1.42, 1.91],
        half: [2.35, 3.78, 2.11],
    },
    Prop {
        id: 122,
        name: "Bonfire",
        model: "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\Bonfire\\OrcBonFire.m2",
        center: [-0.25, -0.15, 2.56],
        half: [1.88, 1.99, 2.86],
    },
    Prop {
        id: 123,
        name: "Rock wall",
        model: "World\\Azeroth\\Duskwood\\PassiveDoodads\\Walls\\DuskWoodRockWall.m2",
        center: [0.03, 0.61, 0.70],
        half: [0.96, 3.48, 0.70],
    },
    Prop {
        id: 124,
        name: "Gate",
        model: "World\\Azeroth\\Duskwood\\PassiveDoodads\\Gates\\DuskWoodGate01.m2",
        center: [-0.06, -0.95, 0.90],
        half: [0.20, 0.99, 0.77],
    },
    Prop {
        id: 125,
        name: "Water hut",
        model: "World\\Azeroth\\SwampOSorrow\\PassiveDoodads\\WaterHuts\\WaterHut01.m2",
        center: [-0.11, -0.47, 2.52],
        half: [1.77, 2.39, 2.75],
    },
    Prop {
        id: 126,
        name: "Well",
        model: "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\HolySpringWell\\HolySpringWell.m2",
        center: [1.40, 0.00, 1.37],
        half: [1.40, 2.64, 1.33],
    },
    Prop {
        id: 127,
        name: "Wall lantern",
        model: "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\DiamondMineWallLantern\\DiamondMineWallLantern.m2",
        center: [0.07, -0.01, 0.45],
        half: [0.48, 0.17, 0.41],
    },
    Prop {
        id: 128,
        name: "Portal",
        model: "World\\Generic\\ActiveDoodads\\MagePortals\\StormwindMagePortal01.m2",
        center: [0.00, 0.00, 1.98],
        half: [0.63, 2.77, 2.51],
    },
];

/// The first structure's id: whole buildings of WoW's models ([`STRUCTURES`]).
pub const STRUCTURE_BASE: u8 = 140;

/// One of a structure's models: its path in the install, where it stands in the piece's frame
/// (x forward, y left, z up, yards) and its turn about z.
pub struct StructurePart {
    pub model: &'static str,
    pub at: [f32; 3],
    pub yaw: f32,
}

/// A whole structure (a cottage, a watchtower, a barn...): drawn as its models, solid as the
/// boxes `examples/structure_boxes.rs` works out from their triangles (walls and posts to their
/// top, decks to walk on, roofs overhead). The server's `Build.cpp` holds the same boxes.
pub struct Structure {
    pub id: u8,
    pub name: &'static str,
    pub stone: bool,
    pub parts: &'static [StructurePart],
    /// Masonry and boards drawn in the install's textures (a city wall), beside its models.
    pub blocks: &'static [StructureBlock],
    pub boxes: &'static [BoxDef],
}

/// A block of a structure drawn in one of the install's textures: its centre and half sizes in
/// the piece's frame (WoW axes, yards).
pub struct StructureBlock {
    pub center: [f32; 3],
    pub half: [f32; 3],
    pub texture: &'static str,
}

/// A structure by its piece type.
pub fn structure(t: PieceType) -> Option<&'static Structure> {
    if is_furniture(t) {
        return None;
    }
    t.0.checked_sub(STRUCTURE_BASE)
        .and_then(|i| STRUCTURES.get(usize::from(i)))
}

/// How a piece is drawn when not as its boxes: a WoW model from the install, a structure's
/// models (by its id), or an MW2 model.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PieceModel {
    Wow(&'static str),
    Structure(u8),
    Mw2(&'static str),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Material {
    Wood,
    Stone,
    Sandbag,
    Fire,
    Metal,
}

/// A solid box of a piece: its centre in the piece's frame, half sizes along its own axes, and a
/// pitch (its +x tipped up toward +z).
#[derive(Clone, Copy, Debug)]
pub struct BoxDef {
    pub center: [f32; 3],
    pub half: [f32; 3],
    pub pitch: f32,
}

const fn b(center: [f32; 3], half: [f32; 3]) -> BoxDef {
    BoxDef {
        center,
        half,
        pitch: 0.0,
    }
}

pub struct PieceDef {
    pub name: &'static str,
    pub material: Material,
    pub health: f32,
    pub boxes: &'static [BoxDef],
    pub snaps: &'static [[f32; 3]],
    pub ground_only: bool,
    /// What it costs: a WoW item and how many.
    pub cost: (&'static str, u32),
    /// Drawn as this model rather than its boxes.
    pub model: Option<PieceModel>,
}

const ROOF_PITCH: f32 = std::f32::consts::FRAC_PI_4;

const WALL_SNAPS: [[f32; 3]; 6] = [
    [0.0, -1.5, 0.0],
    [0.0, 1.5, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, -1.5, 3.0],
    [0.0, 1.5, 3.0],
    [0.0, 0.0, 3.0],
];
const HALF_SNAPS: [[f32; 3]; 6] = [
    [0.0, -1.5, 0.0],
    [0.0, 1.5, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, -1.5, 1.5],
    [0.0, 1.5, 1.5],
    [0.0, 0.0, 1.5],
];
const fn floor_snaps(top: f32) -> [[f32; 3]; 8] {
    [
        [-1.5, -1.5, top],
        [-1.5, 1.5, top],
        [1.5, -1.5, top],
        [1.5, 1.5, top],
        [1.5, 0.0, top],
        [-1.5, 0.0, top],
        [0.0, 1.5, top],
        [0.0, -1.5, top],
    ]
}
const WOOD_FLOOR_SNAPS: [[f32; 3]; 8] = floor_snaps(0.3);
const STONE_FLOOR_SNAPS: [[f32; 3]; 8] = floor_snaps(0.4);

const WOOD: &str = "Simple Wood";
const STONE: &str = "Rough Stone";

static DEFS: [PieceDef; 17] = [
    PieceDef {
        name: "Wood floor",
        material: Material::Wood,
        health: 600.0,
        boxes: &[b([0.0, 0.0, 0.15], [1.5, 1.5, 0.15])],
        snaps: &WOOD_FLOOR_SNAPS,
        ground_only: false,
        cost: (WOOD, 2),
        model: None,
    },
    PieceDef {
        name: "Wood wall",
        material: Material::Wood,
        health: 600.0,
        boxes: &[b([0.0, 0.0, 1.5], [0.15, 1.5, 1.5])],
        snaps: &WALL_SNAPS,
        ground_only: false,
        cost: (WOOD, 2),
        model: None,
    },
    PieceDef {
        name: "Wood half wall",
        material: Material::Wood,
        health: 400.0,
        boxes: &[b([0.0, 0.0, 0.75], [0.15, 1.5, 0.75])],
        snaps: &HALF_SNAPS,
        ground_only: false,
        cost: (WOOD, 1),
        model: None,
    },
    PieceDef {
        name: "Wood doorway",
        material: Material::Wood,
        health: 500.0,
        boxes: &[
            b([0.0, -1.15, 1.5], [0.15, 0.35, 1.5]),
            b([0.0, 1.15, 1.5], [0.15, 0.35, 1.5]),
            b([0.0, 0.0, 2.75], [0.15, 0.8, 0.25]),
        ],
        snaps: &WALL_SNAPS,
        ground_only: false,
        cost: (WOOD, 2),
        model: None,
    },
    PieceDef {
        name: "Wood window",
        material: Material::Wood,
        health: 500.0,
        boxes: &[
            b([0.0, 0.0, 0.6], [0.15, 1.5, 0.6]),
            b([0.0, 0.0, 2.6], [0.15, 1.5, 0.4]),
            b([0.0, -1.1, 1.7], [0.15, 0.4, 0.5]),
            b([0.0, 1.1, 1.7], [0.15, 0.4, 0.5]),
        ],
        snaps: &WALL_SNAPS,
        ground_only: false,
        cost: (WOOD, 2),
        model: None,
    },
    PieceDef {
        name: "Wood roof (45°)",
        material: Material::Wood,
        health: 400.0,
        boxes: &[BoxDef {
            center: [1.5, 0.0, 1.5],
            half: [2.13, 1.5, 0.1],
            pitch: ROOF_PITCH,
        }],
        snaps: &[
            [0.0, -1.5, 0.0],
            [0.0, 1.5, 0.0],
            [0.0, 0.0, 0.0],
            [3.0, -1.5, 3.0],
            [3.0, 1.5, 3.0],
            [3.0, 0.0, 3.0],
        ],
        ground_only: false,
        cost: (WOOD, 2),
        model: None,
    },
    PieceDef {
        name: "Wood stairs",
        material: Material::Wood,
        health: 500.0,
        boxes: &[BoxDef {
            center: [1.5, 0.0, 1.5],
            half: [2.13, 1.0, 0.08],
            pitch: ROOF_PITCH,
        }],
        snaps: &[
            [0.0, -1.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0],
            [3.0, -1.0, 3.0],
            [3.0, 1.0, 3.0],
            [3.0, 0.0, 3.0],
        ],
        ground_only: false,
        cost: (WOOD, 3),
        model: None,
    },
    PieceDef {
        name: "Wood beam",
        material: Material::Wood,
        health: 300.0,
        boxes: &[b([0.0, 0.0, 1.5], [0.15, 0.15, 1.5])],
        snaps: &[[0.0, 0.0, 0.0], [0.0, 0.0, 3.0]],
        ground_only: false,
        cost: (WOOD, 1),
        model: None,
    },
    PieceDef {
        name: "Stone floor",
        material: Material::Stone,
        health: 2000.0,
        boxes: &[b([0.0, 0.0, 0.2], [1.5, 1.5, 0.2])],
        snaps: &STONE_FLOOR_SNAPS,
        ground_only: false,
        cost: (STONE, 4),
        model: None,
    },
    PieceDef {
        name: "Stone wall",
        material: Material::Stone,
        health: 2500.0,
        boxes: &[b([0.0, 0.0, 1.5], [0.25, 1.5, 1.5])],
        snaps: &WALL_SNAPS,
        ground_only: false,
        cost: (STONE, 4),
        model: None,
    },
    PieceDef {
        name: "Stone half wall",
        material: Material::Stone,
        health: 1500.0,
        boxes: &[b([0.0, 0.0, 0.75], [0.25, 1.5, 0.75])],
        snaps: &HALF_SNAPS,
        ground_only: false,
        cost: (STONE, 2),
        model: None,
    },
    PieceDef {
        name: "Sandbags",
        material: Material::Sandbag,
        health: 1500.0,
        boxes: &[b([0.0, 0.0, 0.45], [0.4, 1.5, 0.45])],
        snaps: &[[0.0, -1.5, 0.0], [0.0, 1.5, 0.0]],
        ground_only: true,
        cost: ("Linen Cloth", 3),
        model: None,
    },
    PieceDef {
        name: "Campfire",
        material: Material::Fire,
        health: 300.0,
        boxes: &[b([0.0, 0.0, 0.15], [0.5, 0.5, 0.15])],
        snaps: &[],
        ground_only: true,
        cost: (WOOD, 3),
        model: Some(PieceModel::Wow(
            "World\\Azeroth\\Elwynn\\PassiveDoodads\\Campfire\\ElwynnCampfire.m2",
        )),
    },
    PieceDef {
        name: "Torch",
        material: Material::Wood,
        health: 100.0,
        boxes: &[b([0.0, 0.0, 0.9], [0.06, 0.06, 0.9])],
        snaps: &[[0.0, 0.0, 0.0]],
        ground_only: false,
        cost: (WOOD, 1),
        model: None,
    },
    PieceDef {
        name: "Sentry gun",
        material: Material::Metal,
        health: 900.0,
        boxes: &[
            b([0.0, 0.0, 0.4], [0.35, 0.35, 0.4]),
            b([0.0, 0.0, 0.95], [0.3, 0.22, 0.15]),
        ],
        snaps: &[[0.0, 0.0, 0.0]],
        ground_only: false,
        cost: ("Copper Bar", 4),
        model: Some(PieceModel::Mw2("sentry_minigun")),
    },
    PieceDef {
        name: "Bedroll (respawn here)",
        material: Material::Sandbag,
        health: 200.0,
        boxes: &[b([0.0, 0.0, 0.06], [1.0, 0.45, 0.06])],
        snaps: &[],
        ground_only: true,
        cost: ("Linen Cloth", 4),
        model: Some(PieceModel::Wow(
            "World\\Azeroth\\Westfall\\PassiveDoodads\\Furniture\\westfallbed01.m2",
        )),
    },
    PieceDef {
        name: "Wood door (F opens)",
        material: Material::Wood,
        health: 500.0,
        boxes: &[
            b([0.0, -1.15, 1.5], [0.15, 0.35, 1.5]),
            b([0.0, 1.15, 1.5], [0.15, 0.35, 1.5]),
            b([0.0, 0.0, 2.75], [0.15, 0.8, 0.25]),
            b([0.0, 0.0, 1.25], [0.06, 0.8, 1.25]),
        ],
        snaps: &WALL_SNAPS,
        ground_only: false,
        cost: (WOOD, 3),
        model: None,
    },
];

/// A piece's boxes as it stands: an open door's panel swung a quarter turn on its hinge (the -y
/// jamb), into +x; the server's `BoxesOf`.
pub fn boxes_of(kind: PieceType, open: bool) -> Vec<BoxDef> {
    let mut boxes = def(kind).boxes.to_vec();
    if kind.base() == PieceType::WoodDoor && open && boxes.len() == 4 {
        boxes[3] = b([0.8, -0.74, 1.25], [0.8, 0.06, 1.25]);
    }
    boxes
}

pub fn def(t: PieceType) -> &'static PieceDef {
    // A styled piece is its plain one to the rules (crate::build::look).
    if t.style() != 0 {
        return def(t.base());
    }
    if is_furniture(t) {
        // Furniture (the server's `Furniture.h`): wood, or stone for the smith's anvil and forge;
        // a wall piece hangs rather than standing on the ground.
        static FURNITURE_DEFS: std::sync::OnceLock<Vec<PieceDef>> = std::sync::OnceLock::new();
        let defs = FURNITURE_DEFS.get_or_init(|| {
            FURNITURE
                .iter()
                .map(|f| {
                    let stone = f.name == "Smith's anvil" || f.name == "Forge";
                    PieceDef {
                        name: f.name,
                        material: if stone { Material::Stone } else { Material::Wood },
                        health: if stone { 1500.0 } else { 400.0 },
                        boxes: Box::leak(Box::new([b(f.center, f.half)])),
                        snaps: &[],
                        ground_only: f.mount != Mount::Wall,
                        cost: (WOOD, 2),
                        model: f.model.map(PieceModel::Wow),
                    }
                })
                .collect()
        });
        let i = usize::from(t.0 - FURNITURE_BASE).min(defs.len() - 1);
        return &defs[i];
    }
    if t.0 >= STRUCTURE_BASE {
        static STRUCTURE_DEFS: std::sync::OnceLock<Vec<PieceDef>> = std::sync::OnceLock::new();
        let defs = STRUCTURE_DEFS.get_or_init(|| {
            STRUCTURES
                .iter()
                .map(|s| PieceDef {
                    name: s.name,
                    material: if s.stone {
                        Material::Stone
                    } else {
                        Material::Wood
                    },
                    health: if s.stone { 6000.0 } else { 3000.0 },
                    boxes: s.boxes,
                    snaps: &[],
                    ground_only: true,
                    cost: (WOOD, 20),
                    model: Some(PieceModel::Structure(s.id)),
                })
                .collect()
        });
        let i = usize::from(t.0 - STRUCTURE_BASE).min(defs.len() - 1);
        return &defs[i];
    }
    if t.is_prop() {
        static PROP_DEFS: std::sync::OnceLock<Vec<PieceDef>> = std::sync::OnceLock::new();
        let defs = PROP_DEFS.get_or_init(|| {
            PROPS
                .iter()
                .map(|p| {
                    let stone = p.name.contains("Stone") || p.name.contains("Rock");
                    // The portal's arch is stone, and sturdier than a stone wall (as the server's).
                    let portal = p.id == PORTAL;
                    PieceDef {
                        name: p.name,
                        material: if stone || portal {
                            Material::Stone
                        } else {
                            Material::Wood
                        },
                        health: if portal {
                            3000.0
                        } else if stone {
                            2000.0
                        } else {
                            800.0
                        },
                        boxes: Box::leak(Box::new([b(p.center, p.half)])),
                        snaps: &[],
                        ground_only: true,
                        cost: (WOOD, 4),
                        model: Some(PieceModel::Wow(p.model)),
                    }
                })
                .collect()
        });
        let i = usize::from(t.0 - PROP_BASE).min(defs.len() - 1);
        return &defs[i];
    }
    &DEFS[usize::from(t.0).clamp(1, DEFS.len()) - 1]
}

/// A placed piece, as far as geometry goes.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Placed {
    pub kind: PieceType,
    /// WoW world position of its origin.
    pub pos: Vec3,
    pub yaw: f32,
}

/// A box in the world: its centre, its axes and half sizes along them (WoW axes).
#[derive(Clone, Copy, Debug)]
pub struct WorldBox {
    pub center: Vec3,
    pub axis: [Vec3; 3],
    pub half: Vec3,
}

fn turn(yaw: f32, v: Vec3) -> Vec3 {
    let (s, c) = yaw.sin_cos();
    Vec3::new(v.x * c - v.y * s, v.x * s + v.y * c, v.z)
}

impl Placed {
    pub fn to_world(&self, local: Vec3) -> Vec3 {
        self.pos + turn(self.yaw, local)
    }

    pub fn boxes(&self) -> impl Iterator<Item = WorldBox> + '_ {
        def(self.kind).boxes.iter().map(move |bx| {
            let (sp, cp) = bx.pitch.sin_cos();
            WorldBox {
                center: self.to_world(Vec3::from_array(bx.center)),
                axis: [
                    turn(self.yaw, Vec3::new(cp, 0.0, sp)),
                    turn(self.yaw, Vec3::Y),
                    turn(self.yaw, Vec3::new(-sp, 0.0, cp)),
                ],
                half: Vec3::from_array(bx.half),
            }
        })
    }

    /// The middle of its boxes.
    pub fn center(&self) -> Vec3 {
        let boxes = def(self.kind).boxes;
        let sum: Vec3 = self.boxes().map(|w| w.center).sum();
        sum / boxes.len().max(1) as f32
    }

    /// The nearest of its boxes a ray meets within `max`.
    pub fn ray(&self, origin: Vec3, dir: Vec3, max: f32) -> Option<f32> {
        self.boxes()
            .filter_map(|w| ray_box(&w, origin, dir))
            .filter(|t| *t <= max)
            .min_by(f32::total_cmp)
    }
}

fn project(b: &WorldBox, axis: Vec3) -> f32 {
    b.half.x * b.axis[0].dot(axis).abs()
        + b.half.y * b.axis[1].dot(axis).abs()
        + b.half.z * b.axis[2].dot(axis).abs()
}

/// Separating-axis test between two boxes.
pub fn boxes_overlap(a: &WorldBox, b: &WorldBox) -> bool {
    let d = b.center - a.center;
    let mut axes: Vec<Vec3> = Vec::with_capacity(15);
    for i in 0..3 {
        axes.push(a.axis[i]);
        axes.push(b.axis[i]);
        for j in 0..3 {
            let c = a.axis[i].cross(b.axis[j]);
            if c.length() > 1e-4 {
                axes.push(c.normalize());
            }
        }
    }
    axes.iter()
        .all(|ax| d.dot(*ax).abs() <= project(a, *ax) + project(b, *ax))
}

/// Shrunk to its core: a box only "inside" another when the cores meet.
fn core(mut w: WorldBox) -> WorldBox {
    let shrink = |h: f32| (h - (0.35f32).min(h * 0.8)).max(0.01);
    w.half = Vec3::new(shrink(w.half.x), shrink(w.half.y), shrink(w.half.z));
    w
}

pub fn ray_box(b: &WorldBox, origin: Vec3, dir: Vec3) -> Option<f32> {
    let rel = origin - b.center;
    let (mut t_min, mut t_max) = (0.0f32, f32::MAX);
    let h = b.half.to_array();
    for i in 0..3 {
        let o = rel.dot(b.axis[i]);
        let d = dir.dot(b.axis[i]);
        if d.abs() < 1e-8 {
            if o.abs() > h[i] {
                return None;
            }
            continue;
        }
        let (mut t1, mut t2) = ((-h[i] - o) / d, (h[i] - o) / d);
        if t1 > t2 {
            std::mem::swap(&mut t1, &mut t2);
        }
        t_min = t_min.max(t1);
        t_max = t_max.min(t2);
        if t_min > t_max {
            return None;
        }
    }
    Some(t_min)
}

/// A player's body as benilla's mover moves it: a capsule this wide and tall on its feet (the
/// server's `Build::kBodyRadius`, `kBodyHeight`).
pub const BODY_RADIUS: f32 = 0.3333;
pub const BODY_HEIGHT: f32 = 2.0278;
/// How deep a box may reach into a body and leave it free (a wall leant on, a floor stood on).
pub const BODY_SLACK: f32 = 0.08;

/// How far a point is from a box (0 inside it).
fn point_box_distance(b: &WorldBox, p: Vec3) -> f32 {
    let rel = p - b.center;
    let h = b.half.to_array();
    (0..3)
        .map(|i| (rel.dot(b.axis[i]).abs() - h[i]).max(0.0).powi(2))
        .sum::<f32>()
        .sqrt()
}

/// How near a segment comes to a box: convex along the segment, so a ternary search finds it.
fn segment_box_distance(b: &WorldBox, from: Vec3, to: Vec3) -> f32 {
    let d = to - from;
    let at = |t: f32| point_box_distance(b, from + d * t);
    let (mut lo, mut hi) = (0.0f32, 1.0f32);
    for _ in 0..40 {
        let (m1, m2) = (lo + (hi - lo) / 3.0, hi - (hi - lo) / 3.0);
        if at(m1) < at(m2) {
            hi = m2;
        } else {
            lo = m1;
        }
    }
    at(0.0).min(at(1.0)).min(at(0.5 * (lo + hi)))
}

/// Whether a placed piece's boxes cut into a body standing at `feet` (WoW position): it would be
/// shut inside. The server's `Build::Traps`, which refuses such a piece.
pub fn traps_body(p: &Placed, feet: Vec3) -> bool {
    let low = feet + Vec3::Z * BODY_RADIUS;
    let high = feet + Vec3::Z * (BODY_HEIGHT - BODY_RADIUS);
    p.boxes().any(|w| {
        w.center.distance(feet) <= w.half.length() + BODY_HEIGHT + BODY_RADIUS
            && segment_box_distance(&w, low, high) < BODY_RADIUS - BODY_SLACK
    })
}

/// Whether `p` sits inside one of `pieces`: deep overlap, or the same piece again.
pub fn overlaps<'a>(pieces: impl IntoIterator<Item = &'a Placed>, p: &Placed) -> bool {
    let c = p.center();
    for q in pieces {
        if q.center().distance(c) > 8.0 {
            continue;
        }
        // Furniture may stand closer (a row of candles: the server's `Furniture::kSameSpacing`).
        let spacing = if is_furniture(p.kind) { 0.1 } else { 0.5 };
        if q.kind == p.kind && q.pos.distance(p.pos) < spacing {
            return true;
        }
        for bp in p.boxes() {
            for bq in q.boxes() {
                if boxes_overlap(&core(bp), &core(bq)) {
                    return true;
                }
            }
        }
    }
    false
}

/// Where a piece of `kind` goes aimed at `aim` with `yaw`: one of its snap points on a snap point
/// of a piece within `radius` of the aim (turned square to it), the fit nearest the aim that
/// overlaps nothing. The server's `Build::Snap`.
pub fn snap(
    pieces: &[Placed],
    kind: PieceType,
    aim: Vec3,
    yaw: f32,
    radius: f32,
) -> Option<Placed> {
    const QUARTER: f32 = std::f32::consts::FRAC_PI_2;
    let mut best: Option<(f32, Placed)> = None;
    for q in pieces {
        if q.center().distance(aim) > radius + 6.0 {
            continue;
        }
        let y = q.yaw + ((yaw - q.yaw) / QUARTER).round() * QUARTER;
        for s_local in def(q.kind).snaps {
            let s = q.to_world(Vec3::from_array(*s_local));
            if s.distance(aim) > radius {
                continue;
            }
            for n in def(kind).snaps {
                let pos = s - turn(y, Vec3::from_array(*n));
                let c = Placed { kind, pos, yaw: y };
                let dist = c.center().distance(aim);
                if best.as_ref().is_some_and(|(d, _)| dist >= *d) || overlaps(pieces, &c) {
                    continue;
                }
                best = Some((dist, c));
            }
        }
    }
    best.map(|(_, p)| p)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn at(kind: PieceType, pos: [f32; 3], yaw: f32) -> Placed {
        Placed {
            kind,
            pos: Vec3::from_array(pos),
            yaw,
        }
    }

    #[test]
    fn the_wire_numbers_name_the_pieces() {
        assert_eq!(PieceType::from_u8(1), Some(PieceType::WoodFloor));
        assert_eq!(PieceType::from_u8(14), Some(PieceType::Torch));
        assert_eq!(PieceType::from_u8(16), Some(PieceType::Bedroll));
        assert_eq!(PieceType::from_u8(17), Some(PieceType::WoodDoor));
        assert_eq!(PieceType::from_u8(0), None);
        assert_eq!(PieceType::from_u8(18), None);
        assert_eq!(PieceType::from_u8(100), Some(PieceType(100))); // the first prop
        assert!(PieceType(100).is_prop() && def(PieceType(100)).model.is_some());
        for &t in all() {
            assert_eq!(PieceType::from_u8(t.0), Some(t));
            assert!(!def(t).boxes.is_empty());
        }
    }

    #[test]
    fn a_piece_through_the_body_traps_it_one_beside_it_does_not() {
        let feet = Vec3::new(0.0, 0.0, 100.0);
        // A wall through the feet; leant on; a yard off; a storey up.
        assert!(traps_body(
            &at(PieceType::WoodWall, [0.0, 0.0, 100.0], 0.0),
            feet
        ));
        assert!(!traps_body(
            &at(PieceType::WoodWall, [BODY_RADIUS + 0.15, 0.0, 100.0], 0.0),
            feet
        ));
        assert!(!traps_body(
            &at(PieceType::WoodWall, [1.0, 0.0, 100.0], 0.0),
            feet
        ));
        assert!(!traps_body(
            &at(PieceType::WoodWall, [0.0, 0.0, 103.0], 0.0),
            feet
        ));
        // A floor stood on is free, one through the knees is not.
        assert!(!traps_body(
            &at(PieceType::WoodFloor, [0.0, 0.0, 99.7], 0.0),
            feet
        ));
        assert!(traps_body(
            &at(PieceType::WoodFloor, [0.0, 0.0, 100.5], 0.0),
            feet
        ));
        // A doorway's opening is no trap; a shut door is.
        assert!(!traps_body(
            &at(PieceType::WoodDoorway, [0.0, 0.0, 100.0], 0.0),
            feet
        ));
        assert!(traps_body(
            &at(PieceType::WoodDoor, [0.0, 0.0, 100.0], 0.0),
            feet
        ));
    }

    #[test]
    fn a_ray_meets_a_wall_and_passes_a_doorway() {
        let wall = at(PieceType::WoodWall, [0.0, 0.0, 0.0], 0.0);
        let t = wall.ray(Vec3::new(-10.0, 0.0, 1.5), Vec3::X, 50.0).unwrap();
        assert!((t - 9.85).abs() < 1e-3);
        let door = at(PieceType::WoodDoorway, [0.0, 0.0, 0.0], 0.0);
        assert!(door
            .ray(Vec3::new(-10.0, 0.0, 1.0), Vec3::X, 50.0)
            .is_none());
        assert!(door
            .ray(Vec3::new(-10.0, 1.3, 1.0), Vec3::X, 50.0)
            .is_some());
    }

    #[test]
    fn snapping_matches_the_server() {
        // The server's snapping_lines_pieces_up.
        let pieces = [at(PieceType::WoodFloor, [0.0, 0.0, 0.0], 0.0)];
        let p = snap(
            &pieces,
            PieceType::WoodFloor,
            Vec3::new(3.4, 0.2, 0.3),
            0.0,
            3.0,
        )
        .unwrap();
        assert!(p.pos.distance(Vec3::new(3.0, 0.0, 0.0)) < 1e-3);
        let w = snap(
            &pieces,
            PieceType::WoodWall,
            Vec3::new(0.2, 1.6, 0.5),
            0.0,
            3.0,
        )
        .unwrap();
        assert!((w.pos.z - 0.3).abs() < 1e-3 && (w.pos.y - 1.5).abs() < 1e-3);
        assert!(snap(
            &pieces,
            PieceType::WoodFloor,
            Vec3::new(40.0, 0.0, 0.0),
            0.0,
            3.0
        )
        .is_none());
    }

    #[test]
    fn a_piece_may_not_sit_in_another() {
        let pieces = [at(PieceType::WoodWall, [0.0, 0.0, 0.0], 0.0)];
        assert!(overlaps(
            &pieces,
            &at(PieceType::WoodWall, [0.05, 0.0, 0.0], 0.0)
        ));
        assert!(!overlaps(
            &pieces,
            &at(PieceType::WoodWall, [0.0, 0.0, 3.0], 0.0)
        ));
        assert!(!overlaps(
            &pieces,
            &at(PieceType::WoodFloor, [1.5, 0.0, 0.0], 0.0)
        ));
    }
}

/// A blueprint: a whole structure put down piece by piece (in its own frame: x away from the
/// builder, y left; yaw added to the builder's).
pub struct Blueprint {
    pub name: &'static str,
    pub parts: &'static [(PieceType, [f32; 3], f32)],
}

const Q: f32 = std::f32::consts::FRAC_PI_2;

pub static BLUEPRINTS: [Blueprint; 3] = [
    Blueprint {
        name: "Blueprint: watchtower",
        parts: &[
            (PieceType::WoodFloor, [0.0, 0.0, 0.0], 0.0),
            (PieceType::WoodWall, [1.5, 0.0, 0.3], 0.0),
            (PieceType::WoodWall, [-1.5, 0.0, 0.3], 0.0),
            (PieceType::WoodWall, [0.0, 1.5, 0.3], Q),
            (PieceType::WoodDoorway, [0.0, -1.5, 0.3], Q),
            (PieceType::WoodFloor, [0.0, 0.0, 3.0], 0.0),
            (PieceType::WoodHalfWall, [1.5, 0.0, 3.3], 0.0),
            (PieceType::WoodHalfWall, [0.0, 1.5, 3.3], Q),
            (PieceType::WoodHalfWall, [0.0, -1.5, 3.3], Q),
            (PieceType::WoodStairs, [-4.5, 0.0, 0.0], 0.0),
            (PieceType::Torch, [1.2, 1.2, 3.3], 0.0),
        ],
    },
    Blueprint {
        name: "Blueprint: outpost",
        parts: &[
            (PieceType::WoodFloor, [0.0, -1.5, 0.0], 0.0),
            (PieceType::WoodFloor, [0.0, 1.5, 0.0], 0.0),
            (PieceType::WoodWall, [1.5, -1.5, 0.3], 0.0),
            (PieceType::WoodWall, [1.5, 1.5, 0.3], 0.0),
            (PieceType::WoodWindow, [-1.5, -1.5, 0.3], 0.0),
            (PieceType::WoodDoorway, [-1.5, 1.5, 0.3], 0.0),
            (PieceType::WoodWall, [0.0, -3.0, 0.3], Q),
            (PieceType::WoodWindow, [0.0, 3.0, 0.3], Q),
            (PieceType::WoodFloor, [0.0, -1.5, 3.0], 0.0),
            (PieceType::WoodFloor, [0.0, 1.5, 3.0], 0.0),
            (PieceType::Torch, [1.1, 2.6, 0.3], 0.0),
        ],
    },
    Blueprint {
        name: "Blueprint: sandbag nest",
        parts: &[
            (PieceType::Sandbags, [3.0, 0.0, 0.0], 0.0),
            (PieceType::Sandbags, [-3.0, 0.0, 0.0], 0.0),
            (PieceType::Sandbags, [0.0, 3.0, 0.0], Q),
            (PieceType::Sandbags, [0.0, -3.0, 0.0], Q),
        ],
    },
];

/// The structures building offers, ids from [`STRUCTURE_BASE`] (the server's `Build.cpp` holds
/// the same boxes). Printed by `examples/structure_boxes.rs`; run it again when a model changes.
#[rustfmt::skip]
// These decimal coordinates and angles mirror the server's collision geometry exactly.
#[allow(clippy::approx_constant)]
pub static STRUCTURES: &[Structure] = &[
// structure_boxes: begin
    Structure {
        id: 140,
        name: "Cottage",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse01.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-8.08, -7.95, 5.38], [1.22, 1.22, 0.62]),
            b([-1.98, -7.95, 3.38], [4.88, 1.22, 3.38]),
            b([4.12, -7.95, 3.25], [1.22, 1.22, 3.25]),
            b([6.56, -7.95, 3.38], [1.22, 1.22, 3.38]),
            b([9.01, -7.95, 6.12], [1.22, 1.22, 0.38]),
            b([-8.08, -5.51, 6.12], [1.22, 1.22, 0.62]),
            b([-5.64, -5.51, 4.12], [1.22, 1.22, 4.12]),
            b([-3.20, -5.51, 4.62], [1.22, 1.22, 4.62]),
            b([-0.76, -5.51, 6.12], [1.22, 1.22, 6.12]),
            b([1.68, -5.51, 5.38], [1.22, 1.22, 5.38]),
            b([4.12, -5.51, 4.00], [1.22, 1.22, 4.00]),
            b([6.56, -5.51, 4.12], [1.22, 1.22, 4.12]),
            b([9.01, -5.51, 6.38], [1.22, 1.22, 0.62]),
            b([-8.08, -3.07, 6.38], [1.22, 1.22, 0.62]),
            b([-5.64, -3.07, 4.88], [1.22, 1.22, 4.88]),
            b([-0.76, -3.07, 8.62], [3.66, 1.22, 0.62]),
            b([4.12, -3.07, 8.75], [1.22, 1.22, 0.50]),
            b([6.56, -3.07, 4.88], [1.22, 1.22, 4.88]),
            b([9.01, -3.07, 7.62], [1.22, 1.22, 0.62]),
            b([-8.08, -0.63, 6.25], [1.22, 1.22, 0.50]),
            b([-5.64, -0.63, 6.00], [1.22, 1.22, 6.00]),
            b([-1.98, -0.63, 10.25], [2.44, 1.22, 0.50]),
            b([1.68, -0.63, 10.12], [1.22, 1.22, 0.62]),
            b([4.12, -0.63, 10.38], [1.22, 1.22, 0.62]),
            b([6.56, -0.63, 6.00], [1.22, 1.22, 6.00]),
            b([9.01, -0.63, 8.62], [1.22, 1.22, 0.62]),
            b([-8.08, 1.81, 6.12], [1.22, 1.22, 0.38]),
            b([-5.64, 1.81, 5.12], [1.22, 1.22, 5.12]),
            b([-3.20, 1.81, 9.25], [1.22, 1.22, 0.50]),
            b([-0.76, 1.81, 9.12], [1.22, 1.22, 0.62]),
            b([2.90, 1.81, 9.38], [2.44, 1.22, 0.62]),
            b([6.56, 1.81, 5.12], [1.22, 1.22, 5.12]),
            b([9.01, 1.81, 8.12], [1.22, 1.22, 0.62]),
            b([-8.08, 4.26, 6.12], [1.22, 1.22, 0.62]),
            b([-5.64, 4.26, 4.38], [1.22, 1.22, 4.38]),
            b([0.46, 4.26, 7.88], [4.88, 1.22, 0.62]),
            b([6.56, 4.26, 4.38], [1.22, 1.22, 4.38]),
            b([9.01, 4.26, 6.88], [1.22, 1.22, 0.62]),
            b([-8.08, 6.70, 5.38], [1.22, 1.22, 0.62]),
            b([-4.42, 6.70, 3.62], [2.44, 1.22, 3.62]),
            b([0.46, 7.92, 6.00], [2.44, 2.44, 6.00]),
            b([4.12, 7.92, 5.62], [1.22, 2.44, 5.62]),
            b([6.56, 6.70, 3.75], [1.22, 1.22, 3.75]),
            b([9.01, 6.70, 6.38], [1.22, 1.22, 0.62]),
            b([-8.08, 9.14, 5.75], [1.22, 1.22, 0.25]),
            b([9.01, 9.14, 5.88], [1.22, 1.22, 0.12]),
        ],
    },
    Structure {
        id: 141,
        name: "Shack",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\Shack\\Shack.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-2.68, -0.89, 1.62], [0.50, 3.00, 1.62]),
            b([-1.68, -3.39, 1.75], [0.50, 0.50, 1.75]),
            b([-0.18, -3.39, 1.88], [1.00, 0.50, 1.88]),
            b([1.32, -1.39, 2.00], [0.50, 2.50, 2.00]),
            b([2.32, -3.39, 3.62], [0.50, 0.50, 0.38]),
            b([-1.68, -1.89, 3.25], [0.50, 1.00, 0.25]),
            b([-0.68, -2.39, 3.50], [0.50, 0.50, 0.25]),
            b([0.32, -2.39, 3.62], [0.50, 0.50, 0.12]),
            b([2.32, -2.39, 3.88], [0.50, 0.50, 0.12]),
            b([-0.68, -0.89, 3.38], [0.50, 1.00, 0.12]),
            b([0.32, 0.11, 3.50], [0.50, 2.00, 0.25]),
            b([2.32, -0.89, 3.62], [0.50, 1.00, 0.38]),
            b([-1.68, 0.61, 3.12], [0.50, 1.50, 0.12]),
            b([-0.68, 1.11, 3.25], [0.50, 1.00, 0.25]),
            b([2.32, 1.11, 3.50], [0.50, 1.00, 0.50]),
            b([1.32, 2.61, 1.88], [0.50, 1.50, 1.88]),
            b([-2.68, 3.11, 1.50], [0.50, 1.00, 1.50]),
            b([-1.68, 3.11, 1.62], [0.50, 1.00, 1.62]),
            b([-0.18, 3.11, 1.75], [1.00, 1.00, 1.75]),
            b([2.32, 3.11, 3.38], [0.50, 1.00, 0.38]),
        ],
    },
    Structure {
        id: 142,
        name: "Watchtower",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Elwynn\\Buildings\\HumanGuardTower\\HumanGuardTower.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-8.54, -7.96, 3.38], [0.98, 0.98, 3.38]),
            b([-2.68, -7.96, 3.62], [4.88, 0.98, 3.62]),
            b([4.16, -7.96, 3.50], [1.95, 0.98, 3.50]),
            b([7.09, -7.96, 3.62], [0.98, 0.98, 3.62]),
            b([9.04, -6.00, 0.38], [0.98, 2.93, 0.38]),
            b([-8.54, -5.03, 3.25], [0.98, 1.95, 3.25]),
            b([-6.58, -4.05, 7.25], [0.98, 2.93, 0.25]),
            b([0.25, -6.00, 7.62], [5.86, 0.98, 0.12]),
            b([7.09, 0.83, 3.75], [0.98, 7.81, 3.75]),
            b([-3.65, -4.05, 7.88], [1.95, 0.98, 0.62]),
            b([0.25, -3.07, 7.62], [1.95, 1.95, 0.38]),
            b([3.18, -3.07, 7.88], [0.98, 1.95, 0.62]),
            b([5.14, -4.05, 7.38], [0.98, 0.98, 0.12]),
            b([-8.54, 2.79, 3.38], [0.98, 5.86, 3.38]),
            b([-3.65, -2.10, 7.62], [1.95, 0.98, 0.38]),
            b([5.14, -2.10, 16.00], [0.98, 0.98, 16.00]),
            b([9.04, -2.10, 0.25], [0.98, 0.98, 0.25]),
            b([-6.58, 2.79, 7.38], [0.98, 3.91, 0.12]),
            b([-4.63, -0.14, 7.62], [0.98, 0.98, 0.12]),
            b([-2.68, 1.81, 7.62], [0.98, 2.93, 0.38]),
            b([-0.72, -0.14, 28.38], [0.98, 0.98, 0.12]),
            b([1.23, 0.83, 7.62], [0.98, 1.95, 0.12]),
            b([3.18, 0.83, 7.62], [0.98, 1.95, 0.38]),
            b([5.14, -0.14, 14.88], [0.98, 0.98, 14.88]),
            b([9.04, -0.14, 0.38], [0.98, 0.98, 0.38]),
            b([-4.63, 2.79, 7.88], [0.98, 1.95, 0.62]),
            b([-0.72, 1.81, 7.62], [0.98, 0.98, 0.12]),
            b([5.14, 1.81, 16.00], [0.98, 0.98, 16.00]),
            b([9.04, 5.72, 0.25], [0.98, 4.88, 0.25]),
            b([-0.72, 3.76, 7.62], [0.98, 0.98, 0.38]),
            b([2.21, 3.76, 7.88], [1.95, 0.98, 0.62]),
            b([5.14, 3.76, 7.38], [0.98, 0.98, 0.12]),
            b([0.25, 5.72, 7.62], [5.86, 0.98, 0.12]),
            b([-0.72, 7.67, 3.75], [6.84, 0.98, 3.75]),
            b([-6.58, 9.62, 0.75], [2.93, 0.98, 0.75]),
            b([0.25, 9.62, 1.00], [3.91, 0.98, 1.00]),
            b([6.11, 9.62, 0.75], [1.95, 0.98, 0.75]),
        ],
    },
    Structure {
        id: 143,
        name: "Lookout tower",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Stranglethorn\\Buildings\\TrollWatchTower\\TrollWatchTower.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-0.48, -5.23, 11.88], [0.98, 0.98, 0.38]),
            b([1.48, -5.23, 10.62], [0.98, 0.98, 0.62]),
            b([3.43, -5.23, 5.75], [0.98, 0.98, 5.75]),
            b([5.38, -5.23, 2.12], [0.98, 0.98, 2.12]),
            b([7.34, -5.23, 1.38], [0.98, 0.98, 1.38]),
            b([-4.38, -3.28, 12.88], [0.98, 0.98, 0.62]),
            b([-2.43, -3.28, 5.38], [0.98, 0.98, 0.38]),
            b([-0.48, -3.28, 8.88], [0.98, 0.98, 0.38]),
            b([1.48, -3.28, 6.75], [0.98, 0.98, 6.75]),
            b([4.41, -3.28, 6.50], [1.95, 0.98, 6.50]),
            b([7.34, -3.28, 13.50], [0.98, 0.98, 0.50]),
            b([-4.38, -1.32, 3.00], [0.98, 0.98, 0.25]),
            b([-2.43, -0.35, 6.62], [0.98, 1.95, 6.62]),
            b([-0.48, -1.32, 7.00], [0.98, 0.98, 7.00]),
            b([1.48, -1.32, 7.12], [0.98, 0.98, 7.12]),
            b([3.43, -1.32, 7.00], [0.98, 0.98, 7.00]),
            b([5.38, 0.63, 6.62], [0.98, 2.93, 6.62]),
            b([7.34, -1.32, 12.38], [0.98, 0.98, 0.12]),
            b([-4.38, 0.63, 1.38], [0.98, 0.98, 1.38]),
            b([-0.48, 0.63, 7.12], [0.98, 0.98, 7.12]),
            b([1.48, 0.63, 3.00], [0.98, 0.98, 0.25]),
            b([3.43, 0.63, 4.00], [0.98, 0.98, 0.50]),
            b([-4.38, 2.58, 1.12], [0.98, 0.98, 1.12]),
            b([-2.43, 2.58, 8.50], [0.98, 0.98, 0.50]),
            b([-0.48, 2.58, 3.12], [0.98, 0.98, 0.38]),
            b([1.48, 2.58, 3.00], [0.98, 0.98, 0.50]),
            b([3.43, 2.58, 6.88], [0.98, 0.98, 6.88]),
            b([7.34, 2.58, 12.88], [0.98, 0.98, 0.62]),
            b([-2.43, 4.54, 6.50], [0.98, 0.98, 0.50]),
            b([-0.48, 4.54, 8.88], [0.98, 0.98, 0.38]),
            b([1.48, 4.54, 8.50], [0.98, 0.98, 0.50]),
            b([3.43, 4.54, 6.38], [0.98, 0.98, 6.38]),
            b([5.38, 4.54, 6.12], [0.98, 0.98, 6.12]),
            b([7.34, 4.54, 3.12], [0.98, 0.98, 0.12]),
            b([1.48, 6.49, 12.50], [0.98, 0.98, 0.50]),
            b([3.43, 6.49, 4.00], [0.98, 0.98, 0.25]),
        ],
    },
    Structure {
        id: 144,
        name: "Barn",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Duskwood\\PassiveDoodads\\DuskwoodBarn\\BarnDuskwood.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-8.58, -4.76, 3.75], [0.98, 0.98, 3.75]),
            b([-6.63, -4.76, 3.62], [0.98, 0.98, 3.62]),
            b([-4.68, -4.76, 3.50], [0.98, 0.98, 3.50]),
            b([-1.75, -4.76, 3.62], [1.95, 0.98, 3.62]),
            b([1.18, -4.76, 3.50], [0.98, 0.98, 3.50]),
            b([4.11, -4.76, 3.62], [1.95, 0.98, 3.62]),
            b([7.04, -4.76, 3.75], [0.98, 0.98, 3.75]),
            b([8.99, -4.76, 4.88], [0.98, 0.98, 0.62]),
            b([-8.58, -2.81, 4.62], [0.98, 0.98, 4.62]),
            b([-6.63, -2.81, 4.50], [0.98, 0.98, 4.50]),
            b([0.20, -2.81, 8.00], [5.86, 0.98, 0.50]),
            b([7.04, -2.81, 4.62], [0.98, 0.98, 4.62]),
            b([8.99, -2.81, 6.12], [0.98, 0.98, 0.62]),
            b([-8.58, -0.85, 5.38], [0.98, 0.98, 5.38]),
            b([-6.63, 0.12, 5.25], [0.98, 1.95, 5.25]),
            b([-3.70, -0.85, 9.62], [1.95, 0.98, 0.62]),
            b([1.18, -0.85, 9.50], [2.93, 0.98, 0.50]),
            b([5.09, -0.85, 9.62], [0.98, 0.98, 0.62]),
            b([7.04, -0.85, 5.25], [0.98, 0.98, 5.25]),
            b([8.99, -0.85, 8.38], [0.98, 0.98, 0.62]),
            b([-8.58, 1.10, 5.25], [0.98, 0.98, 5.25]),
            b([-4.68, 1.10, 9.50], [0.98, 0.98, 0.50]),
            b([-2.72, 1.10, 9.12], [0.98, 0.98, 0.62]),
            b([-0.77, 1.10, 9.00], [0.98, 0.98, 0.50]),
            b([1.18, 1.10, 9.25], [0.98, 0.98, 0.50]),
            b([3.13, 1.10, 9.12], [0.98, 0.98, 0.62]),
            b([5.09, 1.10, 9.38], [0.98, 0.98, 0.62]),
            b([7.04, 1.10, 5.12], [0.98, 0.98, 5.12]),
            b([8.99, 1.10, 8.00], [0.98, 0.98, 0.50]),
            b([-7.61, 3.05, 4.38], [1.95, 0.98, 4.38]),
            b([-1.75, 3.05, 7.25], [3.91, 0.98, 0.50]),
            b([3.13, 3.05, 7.50], [0.98, 0.98, 0.50]),
            b([5.09, 3.05, 7.62], [0.98, 0.98, 0.62]),
            b([7.04, 3.05, 4.25], [0.98, 0.98, 4.25]),
            b([8.99, 3.05, 5.12], [0.98, 0.98, 0.62]),
            b([-6.63, 5.00, 3.38], [2.93, 0.98, 3.38]),
            b([-2.72, 5.00, 3.25], [0.98, 0.98, 3.25]),
            b([-0.77, 5.00, 3.12], [0.98, 0.98, 3.12]),
            b([2.16, 5.00, 3.25], [1.95, 0.98, 3.25]),
            b([6.06, 5.00, 3.38], [1.95, 0.98, 3.38]),
            b([8.99, 5.00, 4.88], [0.98, 0.98, 0.62]),
        ],
    },
    Structure {
        id: 145,
        name: "Stable",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Westfall Stable\\WestFallStable01.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-5.91, -3.87, 2.25], [1.88, 0.62, 2.25]),
            b([-2.79, -3.87, 2.12], [1.25, 0.62, 2.12]),
            b([-0.29, -3.87, 2.00], [1.25, 0.62, 2.00]),
            b([2.84, -3.87, 2.12], [1.88, 0.62, 2.12]),
            b([5.96, -3.87, 2.25], [1.25, 0.62, 2.25]),
            b([7.84, -3.87, 2.00], [0.62, 0.62, 2.00]),
            b([-5.91, -2.62, 2.38], [1.88, 0.62, 2.38]),
            b([-1.54, -2.62, 2.25], [1.25, 0.62, 2.25]),
            b([0.34, -2.62, 2.12], [0.62, 0.62, 2.12]),
            b([2.84, -2.62, 2.25], [1.88, 0.62, 2.25]),
            b([5.96, -2.62, 2.38], [1.25, 0.62, 2.38]),
            b([7.84, -2.62, 2.25], [0.62, 0.62, 2.25]),
            b([-6.54, -1.37, 2.50], [1.25, 0.62, 2.50]),
            b([-4.04, -1.37, 4.38], [1.25, 0.62, 0.38]),
            b([-2.16, -1.37, 2.38], [0.62, 0.62, 2.38]),
            b([-0.29, -1.37, 4.25], [1.25, 0.62, 0.25]),
            b([1.59, -1.37, 4.38], [0.62, 0.62, 0.38]),
            b([3.46, -1.37, 2.38], [1.25, 0.62, 2.38]),
            b([5.34, -1.37, 4.25], [0.62, 0.62, 0.50]),
            b([7.21, -1.37, 2.50], [1.25, 0.62, 2.50]),
            b([-7.16, -0.12, 2.88], [0.62, 0.62, 2.88]),
            b([-5.91, -0.12, 4.25], [0.62, 0.62, 0.50]),
            b([-4.04, -0.12, 4.62], [1.25, 0.62, 0.62]),
            b([-2.16, -0.12, 2.75], [0.62, 0.62, 2.75]),
            b([-0.91, -0.12, 4.38], [0.62, 0.62, 0.62]),
            b([0.96, -0.12, 4.62], [1.25, 0.62, 0.62]),
            b([3.46, -0.12, 2.75], [1.25, 0.62, 2.75]),
            b([5.34, -0.12, 4.62], [0.62, 0.62, 0.62]),
            b([7.21, -0.12, 2.75], [1.25, 0.62, 2.75]),
            b([-7.16, 1.75, 3.12], [0.62, 1.25, 3.12]),
            b([-5.91, 1.13, 3.25], [0.62, 0.62, 3.25]),
            b([-4.66, 1.13, 5.38], [0.62, 0.62, 0.62]),
            b([-3.41, 1.13, 4.62], [0.62, 0.62, 0.62]),
            b([-1.54, 1.13, 3.12], [1.25, 0.62, 3.12]),
            b([0.96, 1.13, 4.88], [1.25, 0.62, 0.62]),
            b([3.46, 1.75, 3.12], [1.25, 1.25, 3.12]),
            b([5.34, 1.13, 4.88], [0.62, 0.62, 0.62]),
            b([7.21, 1.75, 3.00], [1.25, 1.25, 3.00]),
            b([-5.91, 2.38, 3.12], [0.62, 0.62, 3.12]),
            b([-4.66, 2.38, 6.00], [0.62, 0.62, 0.50]),
            b([-3.41, 2.38, 5.88], [0.62, 0.62, 0.62]),
            b([-1.54, 2.38, 3.25], [1.25, 0.62, 3.25]),
            b([0.96, 2.38, 5.62], [1.25, 0.62, 0.62]),
            b([5.34, 2.38, 5.88], [0.62, 0.62, 0.38]),
        ],
    },
    Structure {
        id: 146,
        name: "Smithy",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Elwynn\\Buildings\\BlackSmith\\BlackSmith.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-5.39, -7.06, 3.50], [1.95, 0.98, 3.50]),
            b([-0.51, -7.06, 3.12], [2.93, 0.98, 3.12]),
            b([5.35, -7.06, 3.50], [2.93, 0.98, 3.50]),
            b([-5.39, -5.10, 3.62], [1.95, 0.98, 3.62]),
            b([-0.51, -5.10, 3.50], [2.93, 0.98, 3.50]),
            b([5.35, -5.10, 3.62], [2.93, 0.98, 3.62]),
            b([9.26, -5.10, 2.12], [0.98, 0.98, 2.12]),
            b([-6.36, -3.15, 4.00], [0.98, 0.98, 4.00]),
            b([-4.41, -2.17, 3.88], [0.98, 1.95, 3.88]),
            b([0.47, -2.17, 0.50], [3.91, 1.95, 0.50]),
            b([5.35, -3.15, 4.00], [0.98, 0.98, 4.00]),
            b([7.31, -3.15, 4.50], [0.98, 0.98, 0.50]),
            b([9.26, -3.15, 4.00], [0.98, 0.98, 0.50]),
            b([-6.36, -1.20, 3.88], [0.98, 0.98, 3.88]),
            b([5.35, -1.20, 3.88], [0.98, 0.98, 3.88]),
            b([7.31, -1.20, 3.50], [0.98, 0.98, 0.50]),
            b([9.26, -1.20, 2.25], [0.98, 0.98, 2.25]),
            b([-5.39, 0.76, 3.62], [1.95, 0.98, 3.62]),
            b([-0.51, 0.76, 3.38], [2.93, 0.98, 3.38]),
            b([5.35, 0.76, 3.62], [2.93, 0.98, 3.62]),
            b([9.26, 0.76, 2.12], [0.98, 0.98, 2.12]),
            b([-6.36, 2.71, 3.38], [0.98, 0.98, 3.38]),
            b([-4.41, 2.71, 3.50], [0.98, 0.98, 3.50]),
            b([-0.51, 2.71, 3.00], [2.93, 0.98, 3.00]),
            b([3.40, 2.71, 3.50], [0.98, 0.98, 3.50]),
            b([6.33, 2.71, 3.38], [1.95, 0.98, 3.38]),
            b([9.26, 2.71, 1.38], [0.98, 0.98, 1.38]),
            b([11.21, 2.71, 0.38], [0.98, 0.98, 0.38]),
            b([2.42, 5.64, 2.75], [1.95, 1.95, 2.75]),
            b([5.35, 4.66, 2.25], [0.98, 0.98, 2.25]),
            b([7.31, 4.66, 3.50], [0.98, 0.98, 0.50]),
            b([9.26, 4.66, 1.62], [0.98, 0.98, 1.62]),
            b([11.21, 4.66, 0.88], [0.98, 0.98, 0.88]),
            b([5.35, 6.62, 2.38], [0.98, 0.98, 2.38]),
            b([7.31, 6.62, 3.38], [0.98, 0.98, 0.62]),
            b([9.26, 6.62, 1.75], [0.98, 0.98, 1.75]),
            b([11.21, 6.62, 0.50], [0.98, 0.98, 0.50]),
            b([4.38, 8.57, 3.75], [1.95, 0.98, 3.75]),
            b([7.31, 8.57, 2.00], [0.98, 0.98, 2.00]),
            b([9.26, 8.57, 1.25], [0.98, 0.98, 1.25]),
            b([3.40, 10.52, 4.38], [0.98, 0.98, 0.38]),
            b([5.35, 10.52, 4.25], [0.98, 0.98, 0.50]),
        ],
    },
    Structure {
        id: 147,
        name: "Forge",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\SkillActivated\\TradeskillEnablers\\Tradeskill_Forge_03.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-1.18, -1.18, 1.12], [0.50, 0.50, 1.12]),
            b([-0.18, -1.18, 1.38], [0.50, 0.50, 1.38]),
            b([0.82, -1.18, 0.75], [0.50, 0.50, 0.75]),
            b([-2.18, -0.18, 0.75], [0.50, 0.50, 0.75]),
            b([-1.18, -0.18, 1.50], [0.50, 0.50, 1.50]),
            b([-0.18, -0.18, 2.62], [0.50, 0.50, 2.62]),
            b([0.82, -0.18, 2.75], [0.50, 0.50, 2.75]),
            b([1.82, -0.18, 0.62], [0.50, 0.50, 0.62]),
            b([-2.18, 0.82, 0.62], [0.50, 0.50, 0.62]),
            b([-1.18, 0.82, 1.38], [0.50, 0.50, 1.38]),
            b([-0.18, 0.82, 3.00], [0.50, 0.50, 3.00]),
            b([0.82, 0.82, 2.88], [0.50, 0.50, 2.88]),
            b([1.82, 0.82, 0.50], [0.50, 0.50, 0.50]),
            b([-1.18, 1.82, 0.62], [0.50, 0.50, 0.62]),
            b([-0.18, 1.82, 2.62], [0.50, 0.50, 2.62]),
            b([0.82, 1.82, 0.50], [0.50, 0.50, 0.50]),
            b([-1.68, 3.32, 1.88], [1.00, 1.00, 1.88]),
            b([-0.18, 3.32, 1.75], [0.50, 1.00, 1.75]),
            b([1.32, 3.32, 1.88], [1.00, 1.00, 1.88]),
            b([-1.18, 4.82, 1.00], [0.50, 0.50, 1.00]),
            b([-0.18, 4.82, 1.38], [0.50, 0.50, 1.38]),
            b([0.82, 4.82, 0.75], [0.50, 0.50, 0.75]),
            b([-0.18, 5.82, 1.12], [0.50, 0.50, 1.12]),
        ],
    },
    Structure {
        id: 148,
        name: "Market stall",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\VendorAwnings\\StormwindVendorAwning01.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
                at: [-0.90, 0.90, 0.00],
                yaw: 0.3000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                at: [-1.00, -1.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-1.27, -0.97, 1.75], [1.00, 0.50, 1.75]),
            b([0.23, -0.97, 3.38], [0.50, 0.50, 0.12]),
            b([1.23, -0.97, 3.12], [0.50, 0.50, 0.38]),
            b([-1.27, 0.03, 1.62], [1.00, 0.50, 1.62]),
            b([0.23, 0.03, 0.12], [0.50, 0.50, 0.12]),
            b([1.23, 0.03, 1.62], [0.50, 0.50, 1.62]),
            b([-1.77, 1.03, 1.75], [0.50, 0.50, 1.75]),
            b([-0.27, 1.03, 1.62], [1.00, 0.50, 1.62]),
            b([1.23, 1.03, 1.75], [0.50, 0.50, 1.75]),
            b([-0.27, 2.03, 0.50], [2.00, 0.50, 0.50]),
        ],
    },
    Structure {
        id: 149,
        name: "Palisade",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost02.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-0.56, -7.99, 3.62], [0.50, 0.50, 3.62]),
            b([0.44, -7.99, 3.12], [0.50, 0.50, 3.12]),
            b([-0.56, -6.99, 3.38], [0.50, 0.50, 3.38]),
            b([0.44, -6.99, 4.12], [0.50, 0.50, 4.12]),
            b([-0.56, -5.99, 3.62], [0.50, 0.50, 3.62]),
            b([0.44, -5.99, 3.12], [0.50, 0.50, 3.12]),
            b([-0.56, -4.99, 3.75], [0.50, 0.50, 3.75]),
            b([0.44, -4.99, 3.38], [0.50, 0.50, 3.38]),
            b([-0.56, -3.99, 3.50], [0.50, 0.50, 3.50]),
            b([0.44, -3.49, 3.12], [0.50, 1.00, 3.12]),
            b([-0.56, -2.99, 3.38], [0.50, 0.50, 3.38]),
            b([-0.56, -1.99, 4.00], [0.50, 0.50, 4.00]),
            b([0.44, -1.99, 3.62], [0.50, 0.50, 3.62]),
            b([-0.56, -0.99, 4.38], [0.50, 0.50, 4.38]),
            b([0.44, -0.49, 3.75], [0.50, 1.00, 3.75]),
            b([-0.56, 0.01, 4.12], [0.50, 0.50, 4.12]),
            b([-0.56, 1.01, 4.00], [0.50, 0.50, 4.00]),
            b([0.44, 1.01, 3.62], [0.50, 0.50, 3.62]),
            b([-0.56, 2.51, 3.75], [0.50, 1.00, 3.75]),
            b([0.44, 2.51, 3.50], [0.50, 1.00, 3.50]),
            b([-0.56, 4.01, 4.12], [0.50, 0.50, 4.12]),
            b([0.44, 4.01, 3.75], [0.50, 0.50, 3.75]),
            b([-0.56, 5.01, 3.88], [0.50, 0.50, 3.88]),
            b([0.44, 5.01, 3.50], [0.50, 0.50, 3.50]),
            b([-0.56, 6.01, 3.38], [0.50, 0.50, 3.38]),
            b([0.44, 6.01, 3.25], [0.50, 0.50, 3.25]),
            b([1.44, 6.01, 5.25], [0.50, 0.50, 0.50]),
            b([-0.56, 7.01, 3.50], [0.50, 0.50, 3.50]),
            b([0.44, 7.01, 3.38], [0.50, 0.50, 3.38]),
            b([1.44, 7.01, 5.50], [0.50, 0.50, 0.25]),
            b([-0.56, 8.01, 3.62], [0.50, 0.50, 3.62]),
            b([0.44, 8.01, 3.25], [0.50, 0.50, 3.25]),
        ],
    },
    Structure {
        id: 150,
        name: "Palisade gate",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost01.m2",
                at: [0.00, 7.10, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost01.m2",
                at: [0.00, -7.10, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost03.m2",
                at: [0.00, 3.20, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost03.m2",
                at: [0.00, -3.20, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-0.01, -9.79, 3.62], [0.50, 1.00, 3.62]),
            b([0.99, -10.29, 3.50], [0.50, 0.50, 3.50]),
            b([0.99, -9.29, 3.62], [0.50, 0.50, 3.62]),
            b([0.49, -8.29, 3.25], [1.00, 0.50, 3.25]),
            b([-0.01, -6.79, 3.25], [0.50, 1.00, 3.25]),
            b([0.99, -6.79, 3.12], [0.50, 1.00, 3.12]),
            b([-1.01, -5.29, 9.50], [0.50, 0.50, 0.50]),
            b([-0.01, -5.29, 4.88], [0.50, 0.50, 4.88]),
            b([0.99, -5.29, 4.50], [0.50, 0.50, 4.50]),
            b([-1.01, -4.29, 4.88], [0.50, 0.50, 4.88]),
            b([-0.01, -4.29, 5.00], [0.50, 0.50, 5.00]),
            b([0.99, -4.29, 5.12], [0.50, 0.50, 5.12]),
            b([1.99, -4.29, 5.50], [0.50, 0.50, 0.50]),
            b([-1.01, -3.29, 5.50], [0.50, 0.50, 5.50]),
            b([-0.01, -3.29, 5.12], [0.50, 0.50, 5.12]),
            b([0.99, -3.29, 5.00], [0.50, 0.50, 5.00]),
            b([1.99, -3.29, 6.75], [0.50, 0.50, 0.50]),
            b([-1.01, -2.29, 5.38], [0.50, 0.50, 5.38]),
            b([-0.01, -2.29, 5.00], [0.50, 0.50, 5.00]),
            b([0.99, -2.29, 5.12], [0.50, 0.50, 5.12]),
            b([-0.01, 1.71, 5.00], [1.50, 0.50, 5.00]),
            b([-1.01, 2.71, 5.25], [0.50, 0.50, 5.25]),
            b([-0.01, 2.71, 9.50], [0.50, 0.50, 0.50]),
            b([0.99, 3.21, 5.12], [0.50, 1.00, 5.12]),
            b([1.99, 2.71, 5.50], [0.50, 0.50, 0.50]),
            b([-1.01, 3.71, 5.50], [0.50, 0.50, 5.50]),
            b([-0.01, 3.71, 5.12], [0.50, 0.50, 5.12]),
            b([-1.01, 4.71, 7.25], [0.50, 0.50, 0.50]),
            b([-0.01, 4.71, 4.88], [0.50, 0.50, 4.88]),
            b([0.99, 4.71, 4.75], [0.50, 0.50, 4.75]),
            b([-0.01, 5.71, 3.50], [0.50, 0.50, 3.50]),
            b([0.99, 5.71, 3.38], [0.50, 0.50, 3.38]),
            b([0.49, 6.71, 3.25], [1.00, 0.50, 3.25]),
            b([-0.01, 7.71, 3.25], [0.50, 0.50, 3.25]),
            b([0.99, 7.71, 3.12], [0.50, 0.50, 3.12]),
            b([-0.01, 8.71, 3.50], [0.50, 0.50, 3.50]),
            b([0.99, 8.71, 3.38], [0.50, 0.50, 3.38]),
            b([-0.01, 9.71, 3.38], [0.50, 0.50, 3.38]),
            b([0.99, 9.71, 3.25], [0.50, 0.50, 3.25]),
            b([-0.01, 10.71, 3.25], [0.50, 0.50, 3.25]),
            b([0.99, 10.71, 3.12], [0.50, 0.50, 3.12]),
        ],
    },
    Structure {
        id: 151,
        name: "Footbridge",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Elwynn\\Buildings\\Bridges\\ElwynnFootbridge.m2",
                at: [0.00, 0.00, -0.42],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-12.70, -3.32, 1.88], [0.98, 0.98, 1.88]),
            b([-10.75, -3.32, 1.75], [0.98, 0.98, 1.75]),
            b([-8.79, -3.32, 1.50], [0.98, 0.98, 1.50]),
            b([-6.84, -3.32, 1.88], [0.98, 0.98, 1.88]),
            b([-4.89, -3.32, 1.75], [0.98, 0.98, 1.75]),
            b([-2.93, -3.32, 2.00], [0.98, 0.98, 2.00]),
            b([-0.98, -3.32, 2.12], [0.98, 0.98, 2.12]),
            b([0.97, -3.32, 1.88], [0.98, 0.98, 1.88]),
            b([2.93, -3.32, 2.00], [0.98, 0.98, 2.00]),
            b([4.88, -3.32, 1.75], [0.98, 0.98, 1.75]),
            b([6.83, -3.32, 1.88], [0.98, 0.98, 1.88]),
            b([9.76, -3.32, 1.75], [1.95, 0.98, 1.75]),
            b([12.69, -3.32, 1.88], [0.98, 0.98, 1.88]),
            b([-10.75, -0.39, 0.75], [2.93, 1.95, 0.75]),
            b([-5.86, -0.39, 1.00], [1.95, 1.95, 1.00]),
            b([-0.00, -0.39, 1.12], [3.91, 1.95, 1.12]),
            b([4.88, -0.39, 1.00], [0.98, 1.95, 1.00]),
            b([6.83, -0.39, 0.88], [0.98, 1.95, 0.88]),
            b([10.74, -0.39, 0.75], [2.93, 1.95, 0.75]),
            b([-12.70, 2.54, 1.88], [0.98, 0.98, 1.88]),
            b([-10.75, 2.54, 1.75], [0.98, 0.98, 1.75]),
            b([-8.79, 2.54, 1.50], [0.98, 0.98, 1.50]),
            b([-6.84, 2.54, 1.88], [0.98, 0.98, 1.88]),
            b([-4.89, 3.52, 1.75], [0.98, 1.95, 1.75]),
            b([-1.96, 2.54, 2.00], [1.95, 0.98, 2.00]),
            b([0.97, 3.52, 1.88], [0.98, 1.95, 1.88]),
            b([2.93, 3.52, 2.00], [0.98, 1.95, 2.00]),
            b([4.88, 3.52, 1.75], [0.98, 1.95, 1.75]),
            b([6.83, 2.54, 1.88], [0.98, 0.98, 1.88]),
            b([9.76, 2.54, 1.75], [1.95, 0.98, 1.75]),
            b([12.69, 2.54, 1.88], [0.98, 0.98, 1.88]),
            b([-11.72, 4.50, 1.62], [1.95, 0.98, 1.62]),
            b([-8.79, 4.50, 1.25], [0.98, 0.98, 1.25]),
            b([-6.84, 4.50, 1.75], [0.98, 0.98, 1.75]),
            b([-2.93, 4.50, 2.00], [0.98, 0.98, 2.00]),
            b([-0.98, 4.50, 2.12], [0.98, 0.98, 2.12]),
            b([6.83, 4.50, 1.75], [0.98, 0.98, 1.75]),
            b([10.74, 4.50, 1.62], [2.93, 0.98, 1.62]),
        ],
    },
    Structure {
        id: 152,
        name: "Stone bridge",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\Bridge\\StonebridgeShort.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-5.77, -4.91, 0.50], [1.88, 1.25, 0.50]),
            b([-2.65, -5.53, 1.12], [1.25, 0.62, 1.12]),
            b([-3.27, -4.28, 1.12], [0.62, 0.62, 1.12]),
            b([-2.02, -4.28, 1.38], [0.62, 0.62, 1.38]),
            b([-0.77, -4.28, 2.12], [0.62, 0.62, 2.12]),
            b([1.73, -4.28, 2.38], [1.88, 0.62, 2.38]),
            b([4.85, -4.28, 2.25], [1.25, 0.62, 2.25]),
            b([6.73, -4.28, 2.12], [0.62, 0.62, 2.12]),
            b([-5.15, 1.34, 0.50], [1.25, 5.00, 0.50]),
            b([-3.27, -3.03, 0.62], [0.62, 0.62, 0.62]),
            b([-2.02, -3.03, 1.12], [0.62, 0.62, 1.12]),
            b([-0.77, -3.03, 2.00], [0.62, 0.62, 2.00]),
            b([1.10, -3.03, 2.25], [1.25, 0.62, 2.25]),
            b([4.85, -3.03, 2.38], [2.50, 0.62, 2.38]),
            b([-7.02, 1.97, 0.50], [0.62, 4.38, 0.50]),
            b([-3.27, 0.09, 0.50], [0.62, 2.50, 0.50]),
            b([-0.77, 0.09, 0.75], [1.88, 2.50, 0.75]),
            b([1.73, -0.53, 1.25], [0.62, 1.88, 1.25]),
            b([4.85, -0.53, 1.50], [2.50, 1.88, 1.50]),
            b([2.35, 1.97, 2.00], [1.25, 0.62, 2.00]),
            b([5.48, 1.97, 2.12], [1.88, 0.62, 2.12]),
            b([-3.27, 3.22, 0.75], [0.62, 0.62, 0.75]),
            b([-2.02, 3.22, 1.25], [0.62, 0.62, 1.25]),
            b([-0.77, 3.22, 2.12], [0.62, 0.62, 2.12]),
            b([3.60, 3.22, 2.38], [3.75, 0.62, 2.38]),
            b([-3.27, 4.47, 1.12], [0.62, 0.62, 1.12]),
            b([-2.02, 4.47, 1.38], [0.62, 0.62, 1.38]),
            b([-0.77, 4.47, 2.00], [0.62, 0.62, 2.00]),
            b([1.10, 4.47, 2.25], [1.25, 0.62, 2.25]),
            b([2.98, 4.47, 2.12], [0.62, 0.62, 2.12]),
            b([4.23, 4.47, 4.00], [0.62, 0.62, 0.25]),
            b([5.48, 4.47, 4.12], [0.62, 0.62, 0.12]),
            b([-3.27, 5.72, 0.88], [0.62, 0.62, 0.88]),
            b([-2.02, 5.72, 1.00], [0.62, 0.62, 1.00]),
        ],
    },
    Structure {
        id: 153,
        name: "Village well",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Generic\\PassiveDoodads\\Well\\Well.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-0.73, -0.73, 1.62], [0.50, 0.50, 1.62]),
            b([0.27, -0.23, 1.50], [0.50, 1.00, 1.50]),
            b([1.27, -0.23, 0.75], [0.50, 1.00, 0.75]),
            b([-0.73, 0.27, 1.50], [0.50, 0.50, 1.50]),
            b([-0.73, 1.27, 0.75], [0.50, 0.50, 0.75]),
            b([0.27, 1.27, 1.62], [0.50, 0.50, 1.62]),
            b([1.27, 1.27, 1.25], [0.50, 0.50, 1.25]),
        ],
    },
    Structure {
        id: 154,
        name: "Guard post",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\DeadwindPass\\Buildings\\AbandonedGuardTower\\AbandonedHumanGuardTower.m2",
                at: [0.00, 0.00, -7.15],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-5.87, -3.70, 1.00], [0.62, 1.25, 1.00]),
            b([-3.37, -4.33, 7.25], [1.88, 0.62, 7.25]),
            b([1.63, -4.33, 7.25], [1.88, 0.62, 7.25]),
            b([4.13, -3.08, 1.88], [0.62, 1.88, 1.88]),
            b([-7.12, -3.08, 1.12], [0.62, 0.62, 1.12]),
            b([-4.62, -2.45, 7.50], [0.62, 1.25, 7.50]),
            b([-3.37, -3.08, 15.00], [0.62, 0.62, 0.10]),
            b([-0.87, -3.08, 7.50], [1.88, 0.62, 7.50]),
            b([1.63, -3.08, 15.00], [0.62, 0.62, 0.10]),
            b([2.88, -3.08, 4.62], [0.62, 0.62, 0.62]),
            b([-7.12, -1.83, 1.75], [0.62, 0.62, 1.75]),
            b([-5.87, -1.83, 2.50], [0.62, 0.62, 2.50]),
            b([-3.37, -0.58, 7.50], [0.62, 1.88, 7.50]),
            b([-2.12, -1.83, 10.00], [0.62, 0.62, 0.50]),
            b([-0.87, -1.83, 9.62], [0.62, 0.62, 0.12]),
            b([0.38, -1.83, 10.00], [0.62, 0.62, 0.50]),
            b([2.25, -1.83, 7.50], [1.25, 0.62, 7.50]),
            b([-7.12, -0.58, 2.00], [0.62, 0.62, 2.00]),
            b([-5.87, -0.58, 4.38], [0.62, 0.62, 0.62]),
            b([-4.62, -0.58, 4.75], [0.62, 0.62, 0.25]),
            b([1.63, 0.05, 7.50], [0.62, 1.25, 7.50]),
            b([-7.12, 0.67, 2.38], [0.62, 0.62, 2.38]),
            b([-5.87, 1.30, 2.75], [0.62, 1.25, 2.75]),
            b([-4.62, 1.92, 7.38], [0.62, 1.88, 7.38]),
            b([2.88, 0.67, 7.50], [0.62, 0.62, 7.50]),
            b([4.13, 2.55, 1.88], [0.62, 2.50, 1.88]),
            b([-7.12, 1.92, 2.62], [0.62, 0.62, 2.62]),
            b([-3.37, 1.92, 5.12], [0.62, 0.62, 0.12]),
            b([-1.50, 1.92, 4.62], [1.25, 0.62, 0.62]),
            b([0.38, 1.92, 4.50], [0.62, 0.62, 0.50]),
            b([2.88, 2.55, 4.62], [0.62, 1.25, 0.62]),
            b([-6.50, 3.17, 2.50], [1.25, 0.62, 2.50]),
            b([-3.37, 3.17, 6.38], [0.62, 0.62, 0.62]),
            b([-0.87, 3.17, 7.50], [1.88, 0.62, 7.50]),
            b([1.63, 3.17, 6.38], [0.62, 0.62, 0.62]),
            b([-5.87, 4.42, 0.88], [0.62, 0.62, 0.88]),
            b([-4.62, 4.42, 2.75], [0.62, 0.62, 2.75]),
            b([-2.75, 4.42, 2.62], [1.25, 0.62, 2.62]),
            b([1.63, 4.42, 2.75], [1.88, 0.62, 2.75]),
        ],
    },
    Structure {
        id: 155,
        name: "Woodshed",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Westfall\\Buildings\\Shed\\WestfallShed.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-1.55, 0.36, 2.25], [0.50, 4.50, 2.25]),
            b([-0.55, -3.64, 2.12], [0.50, 0.50, 2.12]),
            b([0.45, -3.64, 2.00], [0.50, 0.50, 2.00]),
            b([1.45, -2.14, 1.88], [0.50, 2.00, 1.88]),
            b([2.45, -3.14, 1.75], [0.50, 1.00, 1.75]),
            b([-0.55, -1.14, 4.12], [0.50, 2.00, 0.12]),
            b([0.45, -2.14, 3.88], [0.50, 1.00, 0.12]),
            b([2.45, -1.64, 1.88], [0.50, 0.50, 1.88]),
            b([0.45, -0.14, 4.00], [0.50, 1.00, 0.10]),
            b([2.45, -0.14, 3.50], [0.50, 1.00, 0.25]),
            b([1.45, 2.36, 2.00], [0.50, 2.50, 2.00]),
            b([-0.55, 1.86, 4.25], [0.50, 1.00, 0.10]),
            b([0.45, 1.86, 4.12], [0.50, 1.00, 0.12]),
            b([2.45, 2.86, 1.88], [0.50, 2.00, 1.88]),
            b([-0.55, 3.86, 2.12], [0.50, 1.00, 2.12]),
            b([0.45, 3.86, 2.00], [0.50, 1.00, 2.00]),
        ],
    },
    Structure {
        id: 156,
        name: "Inn",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse02.m2",
                at: [-7.40, -4.25, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Signs\\Shop\\HumanInnSignPost.m2",
                at: [-15.00, 4.50, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\Benches\\InnBench.m2",
                at: [-11.00, 5.00, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                at: [-15.50, -15.50, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-14.83, -17.15, 0.50], [1.53, 1.53, 0.50]),
            b([-2.63, -17.15, 10.50], [1.53, 1.53, 0.50]),
            b([0.42, -17.15, 11.00], [1.53, 1.53, 1.00]),
            b([3.48, -17.15, 12.00], [1.53, 1.53, 1.00]),
            b([6.53, -17.15, 12.50], [1.53, 1.53, 0.50]),
            b([11.11, -17.15, 11.50], [3.05, 1.53, 0.50]),
            b([15.68, -17.15, 12.00], [1.53, 1.53, 0.10]),
            b([-14.83, -12.57, 3.50], [1.53, 3.05, 3.50]),
            b([-11.78, -6.47, 4.00], [1.53, 9.16, 4.00]),
            b([-8.73, -12.57, 4.50], [1.53, 3.05, 4.50]),
            b([-5.68, -12.57, 5.00], [1.53, 3.05, 5.00]),
            b([-2.63, -12.57, 6.00], [1.53, 3.05, 6.00]),
            b([0.42, -12.57, 6.50], [1.53, 3.05, 6.50]),
            b([3.48, -14.10, 8.00], [1.53, 1.53, 8.00]),
            b([6.53, -12.57, 7.00], [1.53, 3.05, 7.00]),
            b([9.58, -4.94, 6.50], [1.53, 10.68, 6.50]),
            b([12.63, -1.89, 6.00], [1.53, 13.73, 6.00]),
            b([15.68, 1.16, 10.50], [1.53, 16.78, 0.50]),
            b([3.48, -11.05, 7.50], [1.53, 1.53, 7.50]),
            b([-14.83, -6.47, 3.00], [1.53, 3.05, 3.00]),
            b([-8.73, -4.94, 8.50], [1.53, 4.58, 0.50]),
            b([-5.68, -4.94, 9.50], [1.53, 4.58, 0.50]),
            b([-2.63, -4.94, 10.50], [1.53, 4.58, 0.50]),
            b([0.42, -3.42, 12.50], [1.53, 6.10, 0.50]),
            b([3.48, -8.00, 13.50], [1.53, 1.53, 0.50]),
            b([6.53, -8.00, 13.00], [1.53, 1.53, 0.10]),
            b([3.48, -0.37, 13.00], [1.53, 6.10, 0.10]),
            b([6.53, 1.16, 13.50], [1.53, 7.63, 0.50]),
            b([-14.83, 1.16, 3.50], [1.53, 4.58, 3.50]),
            b([-8.73, 1.16, 4.50], [1.53, 1.53, 4.50]),
            b([-5.68, 1.16, 5.00], [1.53, 1.53, 5.00]),
            b([-2.63, 1.16, 5.50], [1.53, 1.53, 0.50]),
            b([-10.26, 4.21, 0.50], [3.05, 1.53, 0.50]),
            b([-5.68, 4.21, 5.50], [1.53, 1.53, 5.50]),
            b([-2.63, 4.21, 7.50], [1.53, 1.53, 7.50]),
            b([0.42, 4.21, 12.00], [1.53, 1.53, 1.00]),
            b([-14.83, 7.26, 2.00], [1.53, 1.53, 2.00]),
            b([-5.68, 11.84, 10.50], [1.53, 6.10, 0.50]),
            b([-2.63, 10.31, 6.00], [1.53, 4.58, 6.00]),
            b([0.42, 7.26, 12.00], [1.53, 1.53, 0.10]),
            b([3.48, 7.26, 12.50], [1.53, 1.53, 0.50]),
            b([9.58, 7.26, 6.00], [1.53, 1.53, 6.00]),
            b([0.42, 10.31, 6.00], [1.53, 1.53, 6.00]),
            b([3.48, 11.84, 7.00], [1.53, 3.05, 7.00]),
            b([6.53, 10.31, 7.50], [1.53, 1.53, 7.50]),
            b([9.58, 10.31, 6.50], [1.53, 1.53, 6.50]),
            b([0.42, 13.37, 6.50], [1.53, 1.53, 6.50]),
            b([6.53, 13.37, 8.00], [1.53, 1.53, 8.00]),
            b([9.58, 13.37, 7.00], [1.53, 1.53, 7.00]),
            b([12.63, 13.37, 6.50], [1.53, 1.53, 6.50]),
            b([-2.63, 16.42, 10.50], [1.53, 1.53, 0.50]),
            b([0.42, 16.42, 11.50], [1.53, 1.53, 0.50]),
            b([5.00, 16.42, 12.50], [3.05, 1.53, 0.50]),
            b([9.58, 16.42, 11.50], [1.53, 1.53, 0.50]),
            b([12.63, 16.42, 11.00], [1.53, 1.53, 1.00]),
            b([3.48, 19.47, 12.50], [1.53, 1.53, 0.50]),
            b([6.53, 19.47, 12.00], [1.53, 1.53, 0.10]),
        ],
    },
    Structure {
        id: 157,
        name: "Chapel",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Westfall\\Buildings\\Church\\WestfallChurch.m2",
                at: [4.65, 1.05, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([0.15, -5.49, 6.00], [9.00, 0.50, 0.50]),
            b([-0.35, -4.49, 3.50], [8.50, 0.50, 3.50]),
            b([-9.35, -3.49, 6.75], [0.50, 0.50, 0.25]),
            b([-7.85, -3.49, 4.00], [1.00, 0.50, 4.00]),
            b([-5.85, -3.49, 6.50], [1.00, 0.50, 0.50]),
            b([-4.35, -3.49, 4.00], [0.50, 0.50, 4.00]),
            b([-0.35, -3.49, 3.75], [3.50, 0.50, 3.75]),
            b([4.15, -3.49, 4.00], [1.00, 0.50, 4.00]),
            b([6.15, -3.49, 6.50], [1.00, 0.50, 0.50]),
            b([7.65, -3.49, 4.00], [0.50, 0.50, 4.00]),
            b([-9.35, -2.49, 7.50], [0.50, 0.50, 0.50]),
            b([-7.85, -2.49, 4.50], [1.00, 0.50, 4.50]),
            b([0.15, -2.49, 7.50], [7.00, 0.50, 0.50]),
            b([7.65, -2.49, 4.50], [0.50, 0.50, 4.50]),
            b([9.65, -2.49, 8.00], [0.50, 0.50, 0.10]),
            b([-9.35, -1.49, 8.50], [0.50, 0.50, 0.50]),
            b([-7.85, -1.49, 5.00], [1.00, 0.50, 5.00]),
            b([-0.35, -1.49, 8.50], [6.50, 0.50, 0.50]),
            b([6.65, 0.01, 9.00], [0.50, 2.00, 9.00]),
            b([7.65, -1.49, 8.75], [0.50, 0.50, 8.75]),
            b([9.65, -1.49, 8.50], [0.50, 0.50, 0.50]),
            b([-9.35, -0.49, 9.50], [0.50, 0.50, 0.50]),
            b([-7.85, -0.49, 5.50], [1.00, 0.50, 5.50]),
            b([-0.35, -0.49, 9.50], [6.50, 0.50, 0.50]),
            b([7.65, 0.01, 5.00], [0.50, 1.00, 0.50]),
            b([8.65, 0.01, 4.75], [0.50, 1.00, 0.25]),
            b([9.65, -0.49, 9.50], [0.50, 0.50, 0.50]),
            b([-9.35, 0.51, 10.50], [0.50, 0.50, 0.50]),
            b([-7.85, 0.51, 5.75], [1.00, 0.50, 5.75]),
            b([-1.85, 0.51, 10.50], [5.00, 0.50, 0.50]),
            b([3.65, 0.51, 9.75], [0.50, 0.50, 0.75]),
            b([5.15, 0.51, 10.50], [1.00, 0.50, 0.50]),
            b([9.65, 0.51, 10.50], [0.50, 0.50, 0.50]),
            b([-9.35, 1.51, 9.25], [0.50, 0.50, 0.75]),
            b([-8.35, 1.51, 5.50], [0.50, 0.50, 5.50]),
            b([-7.35, 1.51, 5.25], [0.50, 0.50, 5.25]),
            b([-0.35, 1.51, 9.25], [6.50, 0.50, 0.75]),
            b([7.65, 2.01, 8.75], [0.50, 1.00, 8.75]),
            b([9.65, 1.51, 9.25], [0.50, 0.50, 0.75]),
            b([-9.35, 2.51, 8.00], [0.50, 0.50, 0.50]),
            b([-7.85, 2.51, 4.75], [1.00, 0.50, 4.75]),
            b([0.15, 2.51, 8.00], [7.00, 0.50, 0.50]),
            b([9.65, 2.51, 8.50], [0.50, 0.50, 0.50]),
            b([-9.35, 3.51, 7.00], [0.50, 0.50, 0.50]),
            b([-7.85, 3.51, 4.25], [1.00, 0.50, 4.25]),
            b([-5.85, 3.51, 6.75], [1.00, 0.50, 0.75]),
            b([-3.35, 3.51, 4.00], [1.50, 0.50, 4.00]),
            b([-1.35, 3.51, 0.88], [0.50, 0.50, 0.88]),
            b([1.65, 3.51, 4.00], [2.50, 0.50, 4.00]),
            b([4.65, 3.51, 4.25], [0.50, 0.50, 4.25]),
            b([6.15, 3.51, 6.75], [1.00, 0.50, 0.75]),
            b([7.65, 3.51, 4.25], [0.50, 0.50, 4.25]),
            b([-7.35, 4.51, 3.75], [1.50, 0.50, 3.75]),
            b([-0.85, 4.51, 3.50], [5.00, 0.50, 3.50]),
            b([6.15, 4.51, 3.75], [2.00, 0.50, 3.75]),
            b([0.15, 5.51, 6.00], [9.00, 0.50, 0.50]),
        ],
    },
    Structure {
        id: 158,
        name: "Windmill",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Westfall\\Buildings\\Windmill\\WestfallWindmill.m2",
                at: [1.20, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([2.72, -5.12, 0.50], [1.00, 0.50, 0.50]),
            b([4.22, -5.12, 8.75], [0.50, 0.50, 8.75]),
            b([5.22, -5.12, 17.75], [0.50, 0.50, 0.25]),
            b([2.22, -4.12, 2.25], [0.50, 0.50, 2.25]),
            b([3.22, -4.12, 2.38], [0.50, 0.50, 2.38]),
            b([4.22, -4.12, 10.12], [0.50, 0.50, 10.12]),
            b([5.22, -4.12, 10.38], [0.50, 0.50, 10.38]),
            b([1.22, -2.62, 8.38], [0.50, 1.00, 8.38]),
            b([2.22, -3.12, 5.00], [0.50, 0.50, 5.00]),
            b([3.22, -3.12, 4.62], [0.50, 0.50, 4.62]),
            b([4.22, -3.12, 10.88], [0.50, 0.50, 10.88]),
            b([5.22, -3.12, 11.12], [0.50, 0.50, 11.12]),
            b([-0.78, -2.12, 16.25], [0.50, 0.50, 0.50]),
            b([0.22, -2.12, 5.00], [0.50, 0.50, 0.50]),
            b([2.72, -2.12, 8.38], [1.00, 0.50, 8.38]),
            b([4.22, -2.12, 11.25], [0.50, 0.50, 11.25]),
            b([5.22, -2.12, 16.38], [0.50, 0.50, 0.62]),
            b([-4.78, -1.12, 0.50], [0.50, 0.50, 0.50]),
            b([-3.78, -1.12, 2.00], [0.50, 0.50, 2.00]),
            b([-2.78, -1.12, 3.88], [0.50, 0.50, 3.88]),
            b([-1.78, -1.12, 5.00], [0.50, 0.50, 5.00]),
            b([-0.78, -1.12, 4.50], [0.50, 0.50, 0.50]),
            b([0.22, -1.12, 4.88], [0.50, 0.50, 0.62]),
            b([1.22, -1.12, 5.25], [0.50, 0.50, 0.25]),
            b([3.22, 0.38, 9.38], [0.50, 2.00, 9.38]),
            b([4.22, -0.62, 13.62], [0.50, 1.00, 0.38]),
            b([5.22, -1.12, 13.75], [0.50, 0.50, 0.50]),
            b([-3.78, -0.12, 9.75], [1.50, 0.50, 9.75]),
            b([-1.78, -0.12, 9.88], [0.50, 0.50, 9.88]),
            b([-0.78, -0.12, 4.12], [0.50, 0.50, 0.38]),
            b([0.22, -0.12, 7.00], [0.50, 0.50, 0.25]),
            b([1.22, -0.12, 11.38], [0.50, 0.50, 0.12]),
            b([2.22, 0.88, 9.38], [0.50, 1.50, 9.38]),
            b([5.22, -0.12, 14.50], [0.50, 0.50, 0.50]),
            b([-3.78, 0.88, 8.75], [1.50, 0.50, 8.75]),
            b([-1.78, 0.88, 9.12], [0.50, 0.50, 9.12]),
            b([-0.78, 0.88, 4.12], [0.50, 0.50, 0.62]),
            b([0.22, 0.88, 4.88], [0.50, 0.50, 0.62]),
            b([1.22, 0.88, 5.50], [0.50, 0.50, 0.25]),
            b([5.22, 0.88, 13.50], [0.50, 0.50, 0.50]),
            b([-0.78, 1.88, 15.50], [0.50, 0.50, 0.50]),
            b([1.22, 2.38, 8.38], [0.50, 1.00, 8.38]),
            b([4.22, 1.88, 10.75], [0.50, 0.50, 10.75]),
            b([5.22, 1.88, 14.75], [0.50, 0.50, 0.50]),
            b([0.22, 2.88, 16.25], [0.50, 0.50, 0.50]),
            b([2.22, 2.88, 5.75], [0.50, 0.50, 5.75]),
            b([3.22, 2.88, 5.00], [0.50, 0.50, 5.00]),
            b([4.22, 2.88, 11.12], [0.50, 0.50, 11.12]),
            b([1.22, 3.88, 0.50], [0.50, 0.50, 0.50]),
            b([2.22, 3.88, 2.12], [0.50, 0.50, 2.12]),
            b([3.22, 3.88, 2.75], [0.50, 0.50, 2.75]),
            b([4.22, 3.88, 10.25], [0.50, 0.50, 10.25]),
            b([5.22, 3.88, 10.88], [0.50, 0.50, 10.88]),
            b([2.22, 4.88, 0.50], [0.50, 0.50, 0.50]),
            b([3.22, 4.88, 0.88], [0.50, 0.50, 0.88]),
            b([4.22, 4.88, 10.12], [0.50, 0.50, 10.12]),
            b([5.22, 4.88, 10.00], [0.50, 0.50, 10.00]),
            b([2.22, 5.88, 0.25], [0.50, 0.50, 0.25]),
            b([3.22, 5.88, 0.38], [0.50, 0.50, 0.38]),
        ],
    },
    Structure {
        id: 159,
        name: "Farmhouse",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse01.m2",
                at: [-6.00, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [8.20, -9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [12.50, -9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, -9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [8.20, 9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [12.50, 9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, 9.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, -4.80, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, -1.50, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, 5.70, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2",
                at: [16.80, 9.00, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\HayStack\\WestFallHayStack01.m2",
                at: [12.00, 5.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Scarecrow\\WestFallScarecrow.m2",
                at: [11.00, -4.50, 0.00],
                yaw: 1.5708,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-14.33, -8.21, 5.50], [0.98, 0.98, 0.50]),
            b([-5.54, -8.21, 3.00], [7.81, 0.98, 3.00]),
            b([4.23, -8.21, 0.50], [1.95, 0.98, 0.50]),
            b([12.04, -8.21, 1.00], [5.86, 0.98, 1.00]),
            b([-14.33, -6.25, 6.50], [0.98, 0.98, 0.50]),
            b([-11.40, -6.25, 4.00], [1.95, 0.98, 4.00]),
            b([-8.47, -6.25, 5.00], [0.98, 0.98, 5.00]),
            b([-6.52, -6.25, 6.00], [0.98, 0.98, 6.00]),
            b([-4.56, -6.25, 5.50], [0.98, 0.98, 5.50]),
            b([-2.61, -6.25, 4.50], [0.98, 0.98, 4.50]),
            b([0.32, -6.25, 4.00], [1.95, 0.98, 4.00]),
            b([3.25, -6.25, 7.50], [0.98, 0.98, 0.50]),
            b([16.92, -3.32, 1.00], [0.98, 3.91, 1.00]),
            b([-14.33, -4.30, 7.00], [0.98, 0.98, 1.00]),
            b([-12.37, -4.30, 4.50], [0.98, 0.98, 4.50]),
            b([-10.42, -4.30, 8.50], [0.98, 0.98, 0.50]),
            b([-5.54, -4.30, 8.00], [3.91, 0.98, 1.00]),
            b([0.32, -4.30, 4.50], [1.95, 0.98, 4.50]),
            b([3.25, -0.39, 8.50], [0.98, 4.88, 0.50]),
            b([9.11, -4.30, 1.50], [0.98, 0.98, 1.50]),
            b([11.06, -4.30, 2.00], [0.98, 0.98, 2.00]),
            b([13.02, -4.30, 1.50], [0.98, 0.98, 1.50]),
            b([-14.33, -0.39, 8.50], [0.98, 2.93, 0.50]),
            b([-12.37, -2.35, 5.00], [0.98, 0.98, 5.00]),
            b([-9.45, -2.35, 9.50], [1.95, 0.98, 0.50]),
            b([-6.52, -1.37, 10.50], [0.98, 1.95, 0.50]),
            b([-3.59, -2.35, 9.50], [1.95, 0.98, 0.50]),
            b([0.32, -2.35, 5.00], [1.95, 0.98, 5.00]),
            b([-12.37, -0.39, 6.00], [0.98, 0.98, 6.00]),
            b([-9.45, -0.39, 10.50], [1.95, 0.98, 0.50]),
            b([-3.59, -0.39, 10.50], [1.95, 0.98, 0.50]),
            b([0.32, -0.39, 6.00], [1.95, 0.98, 6.00]),
            b([-12.37, 1.56, 5.00], [0.98, 0.98, 5.00]),
            b([-6.52, 1.56, 9.50], [4.88, 0.98, 0.50]),
            b([0.32, 1.56, 5.00], [1.95, 0.98, 5.00]),
            b([16.92, 2.54, 0.50], [0.98, 1.95, 0.50]),
            b([-14.33, 3.51, 7.50], [0.98, 0.98, 0.50]),
            b([-12.37, 3.51, 4.50], [0.98, 0.98, 4.50]),
            b([-6.52, 3.51, 8.50], [4.88, 0.98, 0.50]),
            b([0.32, 3.51, 4.50], [1.95, 0.98, 4.50]),
            b([11.06, 4.49, 3.00], [0.98, 1.95, 3.00]),
            b([13.02, 4.49, 2.50], [0.98, 1.95, 2.50]),
            b([-14.33, 6.44, 6.50], [0.98, 1.95, 0.50]),
            b([-11.40, 5.47, 4.00], [1.95, 0.98, 4.00]),
            b([-8.47, 6.44, 5.50], [0.98, 1.95, 5.50]),
            b([-6.52, 6.44, 6.00], [0.98, 1.95, 6.00]),
            b([-4.56, 5.47, 4.00], [0.98, 0.98, 0.10]),
            b([-2.61, 6.44, 6.00], [0.98, 1.95, 6.00]),
            b([0.32, 5.47, 4.00], [1.95, 0.98, 4.00]),
            b([3.25, 5.47, 8.00], [0.98, 0.98, 0.10]),
            b([9.11, 5.47, 0.50], [0.98, 0.98, 0.50]),
            b([14.97, 5.47, 0.50], [0.98, 0.98, 0.50]),
            b([16.92, 7.42, 1.00], [0.98, 2.93, 1.00]),
            b([-11.40, 7.42, 3.50], [1.95, 0.98, 3.50]),
            b([-4.56, 7.42, 3.50], [0.98, 0.98, 0.50]),
            b([0.32, 7.42, 3.50], [1.95, 0.98, 3.50]),
            b([12.04, 8.39, 1.00], [1.95, 1.95, 1.00]),
            b([-4.56, 9.37, 5.50], [2.93, 0.98, 5.50]),
            b([4.23, 9.37, 0.50], [1.95, 0.98, 0.50]),
            b([8.13, 9.37, 1.00], [1.95, 0.98, 1.00]),
            b([14.97, 9.37, 1.00], [0.98, 0.98, 1.00]),
        ],
    },
    Structure {
        id: 160,
        name: "Blacksmith",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\SkillActivated\\TradeskillEnablers\\Tradeskill_Forge_03.m2",
                at: [0.00, -4.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\VendorAwnings\\StormwindVendorAwning01.m2",
                at: [0.30, 3.20, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2",
                at: [0.20, 3.40, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                at: [2.60, 3.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Tauren\\Passive Doodads\\WaterTroughs\\WaterTroughSmall01.m2",
                at: [-3.20, 3.60, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\firewood\\FirewoodPile01.m2",
                at: [-3.80, -1.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-1.44, -5.18, 1.12], [0.50, 0.50, 1.12]),
            b([-0.44, -5.18, 1.38], [0.50, 0.50, 1.38]),
            b([0.56, -5.18, 1.12], [0.50, 0.50, 1.12]),
            b([1.56, -5.18, 0.50], [0.50, 0.50, 0.50]),
            b([-1.44, -4.18, 1.38], [0.50, 0.50, 1.38]),
            b([-0.44, -4.18, 2.62], [0.50, 0.50, 2.62]),
            b([0.56, -4.18, 2.75], [0.50, 0.50, 2.75]),
            b([1.56, -3.68, 0.62], [0.50, 1.00, 0.62]),
            b([-1.44, -3.18, 1.12], [0.50, 0.50, 1.12]),
            b([-0.44, -3.18, 2.88], [0.50, 0.50, 2.88]),
            b([0.56, -3.18, 3.00], [0.50, 0.50, 3.00]),
            b([-3.94, -2.18, 0.25], [1.00, 0.50, 0.25]),
            b([-1.44, -2.18, 0.50], [0.50, 0.50, 0.50]),
            b([-0.44, -2.18, 2.62], [0.50, 0.50, 2.62]),
            b([0.56, -2.18, 2.75], [0.50, 0.50, 2.75]),
            b([-4.44, -1.18, 0.50], [0.50, 0.50, 0.50]),
            b([-3.44, -0.68, 0.38], [0.50, 1.00, 0.38]),
            b([-1.94, -0.68, 1.88], [1.00, 1.00, 1.88]),
            b([-0.44, -0.68, 1.75], [0.50, 1.00, 1.75]),
            b([1.06, -0.68, 1.88], [1.00, 1.00, 1.88]),
            b([2.56, -1.18, 3.38], [0.50, 0.50, 0.12]),
            b([-4.44, -0.18, 0.25], [0.50, 0.50, 0.25]),
            b([2.56, -0.18, 1.75], [0.50, 0.50, 1.75]),
            b([-1.44, 0.82, 0.75], [0.50, 0.50, 0.75]),
            b([-0.44, 0.82, 1.38], [0.50, 0.50, 1.38]),
            b([0.56, 0.82, 1.00], [0.50, 0.50, 1.00]),
            b([-4.44, 3.32, 0.50], [0.50, 2.00, 0.50]),
            b([-3.44, 1.82, 0.38], [0.50, 0.50, 0.38]),
            b([-2.44, 2.82, 0.50], [0.50, 1.50, 0.50]),
            b([-1.44, 1.82, 3.25], [0.50, 0.50, 0.25]),
            b([-0.44, 1.82, 1.75], [0.50, 0.50, 1.75]),
            b([0.56, 1.82, 3.38], [0.50, 0.50, 0.12]),
            b([1.56, 1.82, 3.25], [0.50, 0.50, 0.25]),
            b([-3.44, 3.82, 0.50], [0.50, 1.50, 0.50]),
            b([-1.44, 2.82, 3.12], [0.50, 0.50, 0.38]),
            b([0.06, 3.82, 1.62], [1.00, 1.50, 1.62]),
            b([1.56, 2.82, 3.00], [0.50, 0.50, 0.25]),
            b([2.56, 3.32, 0.50], [0.50, 1.00, 0.50]),
            b([-1.44, 3.82, 1.62], [0.50, 0.50, 1.62]),
            b([1.56, 4.32, 1.75], [0.50, 1.00, 1.75]),
            b([-1.94, 4.82, 1.75], [1.00, 0.50, 1.75]),
        ],
    },
    Structure {
        id: 161,
        name: "Lumber yard",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\LogMachines\\LogMachine01.m2",
                at: [2.30, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\LumberPiles\\DeadMineLumberPileLarge.m2",
                at: [11.00, -5.50, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\LumberPiles\\DeadMineLumberPileLarge.m2",
                at: [11.00, -2.80, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\PeasantLumber\\PeasantLumber01.m2",
                at: [10.50, 3.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\firewood\\FirewoodPile01.m2",
                at: [11.00, 6.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-12.42, -8.22, 4.50], [2.34, 0.78, 0.50]),
            b([-13.99, -5.09, 4.50], [0.78, 2.34, 0.50]),
            b([-12.42, 1.16, 3.50], [0.78, 8.59, 0.50]),
            b([-10.86, -5.09, 4.50], [0.78, 2.34, 0.50]),
            b([11.01, -6.66, 0.50], [2.34, 0.78, 0.50]),
            b([-15.55, -4.31, 6.50], [0.78, 1.56, 0.50]),
            b([-1.49, -5.09, 9.00], [0.78, 0.78, 9.00]),
            b([11.01, -5.09, 1.00], [2.34, 0.78, 1.00]),
            b([-3.05, -3.53, 0.50], [0.78, 0.78, 0.50]),
            b([-0.71, -2.75, 9.50], [1.56, 1.56, 9.50]),
            b([1.64, -3.53, 7.50], [0.78, 0.78, 7.50]),
            b([3.20, -3.53, 9.00], [0.78, 0.78, 0.10]),
            b([4.76, -3.53, 0.50], [0.78, 0.78, 0.50]),
            b([6.33, -3.53, 1.00], [0.78, 0.78, 1.00]),
            b([10.23, -3.53, 0.50], [3.12, 0.78, 0.50]),
            b([-15.55, -0.41, 4.50], [0.78, 2.34, 0.50]),
            b([-13.99, -0.41, 3.50], [0.78, 2.34, 0.50]),
            b([-10.86, -0.41, 3.50], [0.78, 2.34, 0.50]),
            b([-9.30, -1.19, 15.50], [0.78, 1.56, 0.50]),
            b([-7.74, 0.37, 14.50], [0.78, 3.12, 0.50]),
            b([-5.39, -1.19, 13.50], [1.56, 1.56, 0.50]),
            b([-3.05, -1.97, 8.50], [0.78, 0.78, 8.50]),
            b([1.64, -1.19, 8.00], [0.78, 1.56, 8.00]),
            b([4.76, -1.97, 6.00], [2.34, 0.78, 6.00]),
            b([7.89, -1.97, 5.50], [0.78, 0.78, 5.50]),
            b([11.01, -1.97, 1.00], [2.34, 0.78, 1.00]),
            b([-3.05, 1.16, 12.50], [0.78, 2.34, 0.50]),
            b([0.08, -0.41, 9.00], [0.78, 0.78, 9.00]),
            b([3.20, -0.41, 7.00], [0.78, 0.78, 7.00]),
            b([4.76, -0.41, 6.50], [0.78, 0.78, 6.50]),
            b([7.11, -0.41, 6.00], [1.56, 0.78, 6.00]),
            b([-9.30, 1.16, 5.00], [0.78, 0.78, 0.10]),
            b([-6.17, 1.94, 13.50], [0.78, 1.56, 0.50]),
            b([-4.61, 1.94, 13.00], [0.78, 1.56, 0.10]),
            b([-1.49, 1.16, 10.50], [0.78, 0.78, 0.50]),
            b([0.08, 1.94, 7.50], [0.78, 1.56, 0.50]),
            b([1.64, 2.72, 7.50], [0.78, 2.34, 7.50]),
            b([3.98, 1.16, 6.00], [1.56, 0.78, 6.00]),
            b([6.33, 1.16, 9.00], [0.78, 0.78, 0.10]),
            b([-15.55, 2.72, 5.00], [0.78, 0.78, 0.10]),
            b([-13.99, 2.72, 4.50], [0.78, 0.78, 0.50]),
            b([-10.86, 2.72, 4.00], [0.78, 0.78, 1.00]),
            b([-9.30, 2.72, 15.50], [0.78, 0.78, 0.50]),
            b([-1.49, 2.72, 12.50], [0.78, 0.78, 0.50]),
            b([3.20, 2.72, 5.50], [0.78, 0.78, 5.50]),
            b([11.01, 2.72, 0.50], [0.78, 0.78, 0.50]),
            b([-15.55, 5.84, 4.50], [0.78, 2.34, 0.50]),
            b([-13.99, 5.84, 3.50], [0.78, 2.34, 0.50]),
            b([-10.86, 6.62, 3.50], [0.78, 3.12, 0.50]),
            b([-1.49, 4.28, 15.00], [0.78, 0.78, 0.10]),
            b([3.20, 4.28, 1.00], [0.78, 0.78, 1.00]),
            b([3.20, 5.84, 0.50], [0.78, 0.78, 0.50]),
            b([11.01, 5.84, 0.50], [0.78, 0.78, 0.50]),
            b([12.58, 5.84, 0.00], [0.78, 0.78, 0.10]),
        ],
    },
    Structure {
        id: 162,
        name: "Lighthouse",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\Westfall\\Buildings\\Lighthouse\\WestfallLighthouse.m2",
                at: [-2.10, -1.35, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([0.56, -7.28, 2.62], [0.98, 0.98, 2.62]),
            b([2.51, -7.28, 3.12], [0.98, 0.98, 3.12]),
            b([4.47, -7.28, 3.00], [0.98, 0.98, 3.00]),
            b([6.42, -7.28, 2.62], [0.98, 0.98, 2.62]),
            b([-3.35, -5.32, 9.75], [2.93, 0.98, 9.75]),
            b([0.56, -5.32, 3.38], [0.98, 0.98, 3.38]),
            b([2.51, -5.32, 3.88], [0.98, 0.98, 3.88]),
            b([4.47, -5.32, 3.62], [0.98, 0.98, 3.62]),
            b([6.42, -5.32, 3.38], [0.98, 0.98, 3.38]),
            b([8.37, -5.32, 3.00], [0.98, 0.98, 3.00]),
            b([-8.23, -3.37, 9.75], [1.95, 0.98, 9.75]),
            b([-5.30, -3.37, 10.75], [0.98, 0.98, 10.75]),
            b([-3.35, -3.37, 5.62], [0.98, 0.98, 0.62]),
            b([-1.39, -3.37, 11.00], [0.98, 0.98, 11.00]),
            b([1.54, -3.37, 9.75], [1.95, 0.98, 9.75]),
            b([4.47, -3.37, 7.75], [0.98, 0.98, 0.50]),
            b([6.42, -3.37, 6.75], [0.98, 0.98, 0.50]),
            b([8.37, -3.37, 3.12], [0.98, 0.98, 3.12]),
            b([10.32, -3.37, 0.50], [0.98, 0.98, 0.50]),
            b([-9.21, 2.49, 9.75], [0.98, 4.88, 9.75]),
            b([-7.25, -1.42, 11.75], [0.98, 0.98, 11.75]),
            b([-5.30, -1.42, 15.38], [0.98, 0.98, 0.62]),
            b([-3.35, -1.42, 22.50], [0.98, 0.98, 0.50]),
            b([-1.39, -1.42, 16.00], [0.98, 0.98, 0.50]),
            b([0.56, -1.42, 3.75], [0.98, 0.98, 0.25]),
            b([2.51, -1.42, 9.75], [0.98, 0.98, 9.75]),
            b([4.47, -1.42, 8.50], [0.98, 0.98, 0.50]),
            b([6.42, -1.42, 7.25], [0.98, 0.98, 0.50]),
            b([8.37, -1.42, 3.38], [0.98, 0.98, 3.38]),
            b([10.32, -1.42, 2.62], [0.98, 0.98, 2.62]),
            b([-7.25, 0.54, 9.00], [0.98, 0.98, 0.50]),
            b([-5.30, 0.54, 24.25], [0.98, 0.98, 0.50]),
            b([-3.35, 0.54, 33.25], [0.98, 0.98, 0.50]),
            b([-1.39, 0.54, 21.25], [0.98, 0.98, 0.25]),
            b([0.56, 0.54, 9.88], [0.98, 0.98, 0.38]),
            b([2.51, 0.54, 10.88], [0.98, 0.98, 10.88]),
            b([4.47, 0.54, 4.62], [0.98, 0.98, 4.62]),
            b([6.42, 0.54, 6.75], [0.98, 0.98, 0.50]),
            b([8.37, 0.54, 3.12], [0.98, 0.98, 3.12]),
            b([10.32, 0.54, 0.50], [0.98, 0.98, 0.50]),
            b([-7.25, 2.49, 6.00], [0.98, 0.98, 0.50]),
            b([-5.30, 2.49, 24.12], [0.98, 0.98, 0.62]),
            b([-3.35, 2.49, 25.75], [0.98, 0.98, 0.50]),
            b([-1.39, 2.49, 24.25], [0.98, 0.98, 0.50]),
            b([0.56, 2.49, 8.12], [0.98, 0.98, 0.38]),
            b([2.51, 4.44, 9.75], [0.98, 2.93, 9.75]),
            b([4.47, 2.49, 5.00], [0.98, 0.98, 5.00]),
            b([6.42, 2.49, 3.50], [0.98, 0.98, 3.50]),
            b([8.37, 2.49, 2.88], [0.98, 0.98, 2.88]),
            b([-7.25, 4.44, 11.00], [0.98, 0.98, 11.00]),
            b([-5.30, 4.44, 10.12], [0.98, 0.98, 0.38]),
            b([-3.35, 4.44, 16.75], [0.98, 0.98, 0.50]),
            b([-1.39, 4.44, 11.25], [0.98, 0.98, 0.25]),
            b([0.56, 4.44, 12.25], [0.98, 0.98, 12.25]),
            b([6.42, 4.44, 0.50], [2.93, 0.98, 0.50]),
            b([-6.28, 6.40, 9.75], [1.95, 0.98, 9.75]),
            b([-3.35, 6.40, 10.50], [0.98, 0.98, 10.50]),
            b([-0.42, 6.40, 9.75], [1.95, 0.98, 9.75]),
            b([-3.35, 8.35, 9.75], [2.93, 0.98, 9.75]),
        ],
    },
    Structure {
        id: 163,
        name: "Gate arch",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Generic\\Centaur\\Passive Doodads\\CentaurRuins\\Centaur_Arch01.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 1.5708,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2",
                at: [3.20, -6.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2",
                at: [3.20, 6.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([0.12, -8.16, 7.25], [2.00, 0.50, 7.25]),
            b([-1.38, -7.16, 8.12], [0.50, 0.50, 8.12]),
            b([1.12, -7.16, 8.12], [1.00, 0.50, 8.12]),
            b([3.12, -7.16, 0.25], [1.00, 0.50, 0.25]),
            b([0.12, -6.16, 8.88], [2.00, 0.50, 8.88]),
            b([3.12, -6.16, 0.38], [1.00, 0.50, 0.38]),
            b([4.62, -5.66, 0.25], [0.50, 1.00, 0.25]),
            b([0.12, -5.16, 9.25], [2.00, 0.50, 9.25]),
            b([2.62, -5.16, 0.12], [0.50, 0.50, 0.12]),
            b([3.62, -5.16, 0.25], [0.50, 0.50, 0.25]),
            b([0.12, -4.16, 13.38], [2.00, 0.50, 0.62]),
            b([-1.38, -3.16, 15.12], [0.50, 0.50, 0.62]),
            b([0.12, -3.16, 14.88], [1.00, 0.50, 0.38]),
            b([1.62, -3.16, 15.12], [0.50, 0.50, 0.62]),
            b([-1.38, -2.16, 15.88], [0.50, 0.50, 0.62]),
            b([0.12, -2.16, 15.50], [1.00, 0.50, 0.25]),
            b([1.62, -2.16, 15.88], [0.50, 0.50, 0.62]),
            b([-1.38, -1.16, 16.50], [0.50, 0.50, 0.50]),
            b([0.12, -1.16, 16.00], [1.00, 0.50, 0.10]),
            b([1.62, -1.16, 16.50], [0.50, 0.50, 0.50]),
            b([-1.38, -0.16, 16.75], [0.50, 0.50, 0.50]),
            b([0.12, -0.16, 16.25], [1.00, 0.50, 0.10]),
            b([1.62, -0.16, 16.75], [0.50, 0.50, 0.50]),
            b([-1.38, 0.84, 16.62], [0.50, 0.50, 0.62]),
            b([0.12, 0.84, 16.12], [1.00, 0.50, 0.12]),
            b([1.62, 0.84, 16.50], [0.50, 0.50, 0.50]),
            b([-1.38, 1.84, 16.12], [0.50, 0.50, 0.62]),
            b([0.12, 1.84, 15.88], [1.00, 0.50, 0.12]),
            b([1.62, 1.84, 16.12], [0.50, 0.50, 0.62]),
            b([-1.38, 2.84, 15.38], [0.50, 0.50, 0.62]),
            b([0.12, 2.84, 15.12], [1.00, 0.50, 0.38]),
            b([1.62, 2.84, 15.38], [0.50, 0.50, 0.62]),
            b([0.12, 3.84, 13.88], [2.00, 0.50, 0.62]),
            b([0.12, 4.84, 9.38], [2.00, 0.50, 9.38]),
            b([3.12, 4.84, 0.25], [1.00, 0.50, 0.25]),
            b([0.12, 5.84, 9.00], [2.00, 0.50, 9.00]),
            b([3.12, 5.84, 0.38], [1.00, 0.50, 0.38]),
            b([4.62, 6.34, 0.25], [0.50, 1.00, 0.25]),
            b([-1.38, 6.84, 8.50], [0.50, 0.50, 8.50]),
            b([0.62, 6.84, 8.38], [0.50, 0.50, 8.38]),
            b([1.62, 6.84, 8.50], [0.50, 0.50, 8.50]),
            b([2.62, 6.84, 0.12], [0.50, 0.50, 0.12]),
            b([3.62, 6.84, 0.25], [0.50, 0.50, 0.25]),
            b([0.12, 7.84, 7.62], [2.00, 0.50, 7.62]),
            b([0.12, 8.84, 6.00], [2.00, 0.50, 6.00]),
        ],
    },
    Structure {
        id: 164,
        name: "City wall",
        stone: true,
        parts: &[
        ],
        blocks: &[
            StructureBlock {
                center: [0.00, 0.00, 2.25],
                half: [1.20, 8.00, 2.25],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, 0.00, 4.75],
                half: [0.20, 8.00, 0.25],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, -7.45, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, -5.00, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, -2.50, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, 0.00, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, 2.50, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, 5.00, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [1.00, 7.45, 5.40],
                half: [0.20, 0.55, 0.40],
                texture: "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp",
            },
            StructureBlock {
                center: [-0.15, 0.00, 4.55],
                half: [1.00, 8.00, 0.05],
                texture: "Dungeons\\Textures\\Floor\\MM_STRMWND_INT_FLOOR_01.blp",
            },
            StructureBlock {
                center: [-2.00, 3.50, 0.25],
                half: [0.80, 4.50, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 4.00, 0.75],
                half: [0.80, 4.00, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 4.50, 1.25],
                half: [0.80, 3.50, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 5.00, 1.75],
                half: [0.80, 3.00, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 5.50, 2.25],
                half: [0.80, 2.50, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 6.00, 2.75],
                half: [0.80, 2.00, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 6.50, 3.25],
                half: [0.80, 1.50, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 7.00, 3.75],
                half: [0.80, 1.00, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
            StructureBlock {
                center: [-2.00, 7.50, 4.25],
                half: [0.80, 0.50, 0.25],
                texture: "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp",
            },
        ],
        boxes: &[
            b([-0.80, 0.00, 2.25], [1.00, 8.00, 2.25]),
            b([0.70, 0.00, 2.88], [0.50, 8.00, 2.88]),
            b([-2.30, -0.50, 0.25], [0.50, 0.50, 0.25]),
            b([-2.30, 0.50, 0.50], [0.50, 0.50, 0.50]),
            b([-2.30, 1.50, 0.75], [0.50, 0.50, 0.75]),
            b([-2.30, 2.50, 1.00], [0.50, 0.50, 1.00]),
            b([-2.30, 3.50, 1.25], [0.50, 0.50, 1.25]),
            b([-2.30, 4.50, 1.50], [0.50, 0.50, 1.50]),
            b([-2.30, 5.50, 1.75], [0.50, 0.50, 1.75]),
            b([-2.30, 6.50, 2.00], [0.50, 0.50, 2.00]),
            b([-2.30, 7.50, 2.25], [0.50, 0.50, 2.25]),
        ],
    },
    Structure {
        id: 165,
        name: "Dock",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Azeroth\\RedRidge\\PassiveDoodads\\Docks\\RedRidgeDocks01.m2",
                at: [0.00, 0.00, -5.30],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                at: [-8.00, -7.00, 0.25],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
                at: [-8.50, -4.80, 0.25],
                yaw: 0.4000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-10.30, -9.29, 1.00], [1.50, 1.00, 1.00]),
            b([-6.30, -9.79, 0.25], [2.50, 0.50, 0.25]),
            b([6.20, -9.79, 0.25], [2.00, 0.50, 0.25]),
            b([9.70, -9.79, 1.25], [1.50, 0.50, 1.25]),
            b([-8.30, -8.79, 0.25], [0.50, 0.50, 0.25]),
            b([7.70, -8.79, 0.25], [0.50, 0.50, 0.25]),
            b([8.70, -8.79, 1.12], [0.50, 0.50, 1.12]),
            b([10.20, -8.79, 1.25], [1.00, 0.50, 1.25]),
            b([-11.30, -7.29, 0.25], [0.50, 1.00, 0.25]),
            b([-9.80, -7.79, 1.00], [1.00, 0.50, 1.00]),
            b([-8.30, -7.29, 0.62], [0.50, 1.00, 0.62]),
            b([-7.30, -7.79, 0.50], [0.50, 0.50, 0.50]),
            b([10.20, -7.79, 1.12], [1.00, 0.50, 1.12]),
            b([-9.80, -6.79, 0.25], [1.00, 0.50, 0.25]),
            b([-7.30, -6.79, 0.62], [0.50, 0.50, 0.62]),
            b([-6.30, -4.79, 0.25], [0.50, 2.50, 0.25]),
            b([8.70, -6.79, 0.25], [2.50, 0.50, 0.25]),
            b([-9.30, -5.79, 0.38], [2.50, 0.50, 0.38]),
            b([0.70, -4.29, 0.25], [6.50, 2.00, 0.25]),
            b([9.70, -4.29, 0.38], [2.50, 2.00, 0.38]),
            b([-9.80, -3.79, 0.38], [2.00, 1.50, 0.38]),
            b([-7.30, -4.29, 0.25], [0.50, 1.00, 0.25]),
            b([-7.30, -2.79, 0.38], [0.50, 0.50, 0.38]),
            b([-10.80, 2.21, 0.25], [1.00, 0.50, 0.25]),
            b([11.20, 2.21, 0.25], [1.00, 0.50, 0.25]),
            b([-9.80, 4.21, 0.38], [2.00, 1.50, 0.38]),
            b([-0.30, 4.21, 0.25], [7.50, 1.50, 0.25]),
            b([9.70, 3.21, 0.38], [2.50, 0.50, 0.38]),
            b([9.20, 4.71, 0.38], [2.00, 1.00, 0.38]),
            b([-9.80, 6.21, 0.25], [2.00, 0.50, 0.25]),
            b([9.70, 6.21, 0.25], [1.50, 0.50, 0.25]),
            b([-10.30, 8.21, 1.25], [0.50, 1.50, 1.25]),
            b([-9.30, 7.21, 0.38], [0.50, 0.50, 0.38]),
            b([9.70, 7.21, 0.88], [0.50, 0.50, 0.88]),
            b([-11.30, 8.71, 1.25], [0.50, 1.00, 1.25]),
            b([-9.30, 8.71, 1.25], [0.50, 1.00, 1.25]),
            b([8.70, 8.71, 0.88], [0.50, 1.00, 0.88]),
            b([10.20, 8.71, 1.00], [1.00, 1.00, 1.00]),
            b([-5.80, 9.21, 0.25], [3.00, 0.50, 0.25]),
            b([5.70, 9.21, 0.25], [2.50, 0.50, 0.25]),
            b([-9.80, 10.21, 1.12], [1.00, 0.50, 1.12]),
            b([9.70, 10.21, 0.88], [0.50, 0.50, 0.88]),
        ],
    },
    Structure {
        id: 166,
        name: "Gazebo",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Generic\\NightElf\\Passive Doodads\\Gazibos\\KalidarGazibo.m2",
                at: [-0.05, -0.30, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-5.05, -5.12, 12.50], [0.62, 0.62, 0.50]),
            b([-3.80, -4.49, 11.50], [0.62, 1.25, 0.50]),
            b([-2.55, -5.12, 12.00], [0.62, 0.62, 0.10]),
            b([0.58, -5.12, 6.00], [2.50, 0.62, 6.00]),
            b([4.33, -4.49, 11.50], [1.25, 1.25, 0.50]),
            b([-5.05, -3.87, 11.50], [0.62, 0.62, 0.50]),
            b([-2.55, -3.87, 10.50], [0.62, 0.62, 0.50]),
            b([-0.05, -3.87, 6.50], [1.88, 0.62, 6.50]),
            b([2.45, -3.87, 10.50], [0.62, 0.62, 0.50]),
            b([-5.05, -2.62, 12.00], [0.62, 0.62, 0.10]),
            b([-3.80, -2.62, 10.50], [0.62, 0.62, 0.50]),
            b([-2.55, -2.62, 11.50], [0.62, 0.62, 0.50]),
            b([-1.30, -2.62, 10.50], [0.62, 0.62, 0.50]),
            b([-0.05, -2.62, 13.50], [0.62, 0.62, 0.50]),
            b([1.83, -2.62, 11.00], [1.25, 0.62, 1.00]),
            b([4.33, -2.62, 10.50], [1.25, 0.62, 0.50]),
            b([-5.05, 0.51, 6.00], [0.62, 2.50, 6.00]),
            b([-3.80, -0.12, 6.50], [0.62, 1.88, 6.50]),
            b([-2.55, -1.37, 11.00], [0.62, 0.62, 1.00]),
            b([-0.05, -1.37, 14.50], [1.88, 0.62, 0.50]),
            b([2.45, -1.37, 11.00], [0.62, 0.62, 1.00]),
            b([3.70, -0.12, 6.50], [0.62, 1.88, 6.50]),
            b([4.95, 0.51, 6.00], [0.62, 2.50, 6.00]),
            b([6.20, -1.37, 4.00], [0.62, 0.62, 4.00]),
            b([-2.55, -0.12, 13.50], [0.62, 0.62, 0.50]),
            b([-1.30, 0.51, 14.50], [0.62, 1.25, 0.50]),
            b([-0.05, -0.12, 16.50], [0.62, 0.62, 0.50]),
            b([1.20, 0.51, 14.50], [0.62, 1.25, 0.50]),
            b([2.45, -0.12, 13.50], [0.62, 0.62, 0.50]),
            b([-2.55, 1.13, 11.00], [0.62, 0.62, 1.00]),
            b([-0.05, 1.13, 14.50], [0.62, 0.62, 0.50]),
            b([2.45, 1.13, 11.00], [0.62, 0.62, 1.00]),
            b([6.20, 1.13, 4.00], [0.62, 0.62, 4.00]),
            b([-3.80, 2.38, 10.50], [0.62, 0.62, 0.50]),
            b([-2.55, 2.38, 11.50], [0.62, 0.62, 0.50]),
            b([-1.30, 2.38, 11.00], [0.62, 0.62, 1.00]),
            b([-0.05, 2.38, 13.50], [0.62, 0.62, 0.50]),
            b([1.20, 2.38, 11.00], [0.62, 0.62, 1.00]),
            b([2.45, 2.38, 11.50], [0.62, 0.62, 0.50]),
            b([3.70, 2.38, 10.50], [0.62, 0.62, 0.50]),
            b([-4.42, 4.26, 11.50], [1.25, 1.25, 0.50]),
            b([-2.55, 4.26, 10.50], [0.62, 1.25, 0.50]),
            b([-0.05, 3.63, 6.50], [1.88, 0.62, 6.50]),
            b([2.45, 3.63, 10.50], [0.62, 0.62, 0.50]),
            b([3.70, 3.63, 11.50], [0.62, 0.62, 0.50]),
            b([4.95, 4.26, 12.00], [0.62, 1.25, 1.00]),
            b([0.58, 4.88, 6.00], [2.50, 0.62, 6.00]),
            b([3.70, 4.88, 12.00], [0.62, 0.62, 1.00]),
            b([6.20, 4.88, 13.00], [0.62, 0.62, 0.10]),
            b([-3.80, 6.13, 13.50], [0.62, 0.62, 0.50]),
            b([-1.30, 6.13, 4.00], [0.62, 0.62, 4.00]),
            b([1.20, 6.13, 4.00], [0.62, 0.62, 4.00]),
            b([4.95, 6.13, 13.00], [0.62, 0.62, 0.10]),
        ],
    },
    Structure {
        id: 167,
        name: "Orcish longhouse",
        stone: false,
        parts: &[
            StructurePart {
                model: "World\\Generic\\Orc\\Passive Doodads\\Tents\\orctent01.m2",
                at: [-6.50, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Orc\\Passive Doodads\\Tents\\orctent01.m2",
                at: [6.50, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Orc\\Passive Doodads\\FirePits\\SmallFirePit01.m2",
                at: [0.00, 0.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2",
                at: [14.80, -5.00, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2",
                at: [14.80, 5.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([-10.57, -5.50, 3.75], [1.22, 1.22, 0.75]),
            b([-8.13, -5.50, 3.00], [1.22, 1.22, 3.00]),
            b([-5.69, -5.50, 4.75], [1.22, 1.22, 0.75]),
            b([-3.25, -5.50, 3.75], [1.22, 1.22, 0.75]),
            b([1.64, -5.50, 3.75], [1.22, 1.22, 0.75]),
            b([4.08, -5.50, 3.00], [1.22, 1.22, 3.00]),
            b([6.52, -5.50, 4.50], [1.22, 1.22, 0.50]),
            b([8.96, -5.50, 4.00], [1.22, 1.22, 0.50]),
            b([11.40, -5.50, 3.50], [1.22, 1.22, 0.50]),
            b([13.84, -5.50, 2.75], [1.22, 1.22, 2.75]),
            b([16.29, -4.28, 5.00], [1.22, 2.44, 0.10]),
            b([-13.01, -3.05, 4.00], [1.22, 1.22, 0.10]),
            b([-10.57, -3.05, 3.00], [1.22, 1.22, 3.00]),
            b([-8.13, -3.05, 4.25], [1.22, 1.22, 4.25]),
            b([-5.69, -3.05, 5.75], [1.22, 1.22, 0.75]),
            b([-3.25, -3.05, 5.00], [1.22, 1.22, 0.50]),
            b([-0.80, -3.05, 4.25], [1.22, 1.22, 0.25]),
            b([1.64, -3.05, 4.50], [1.22, 1.22, 0.50]),
            b([4.08, -3.05, 3.50], [1.22, 1.22, 3.50]),
            b([6.52, -3.05, 5.50], [1.22, 1.22, 0.50]),
            b([8.96, -3.05, 5.00], [1.22, 1.22, 0.50]),
            b([11.40, -3.05, 4.50], [1.22, 1.22, 0.50]),
            b([13.84, -3.05, 2.50], [1.22, 1.22, 2.50]),
            b([-13.01, -0.61, 3.75], [1.22, 1.22, 0.75]),
            b([-10.57, -0.61, 5.00], [1.22, 1.22, 0.50]),
            b([-8.13, -0.61, 6.50], [1.22, 1.22, 0.50]),
            b([-5.69, -0.61, 7.50], [1.22, 1.22, 0.50]),
            b([-3.25, -0.61, 4.25], [1.22, 1.22, 4.25]),
            b([-0.80, -0.61, 3.25], [1.22, 1.22, 3.25]),
            b([1.64, -0.61, 3.00], [1.22, 1.22, 3.00]),
            b([4.08, -0.61, 6.00], [1.22, 1.22, 0.50]),
            b([6.52, -0.61, 7.50], [1.22, 1.22, 0.50]),
            b([8.96, -0.61, 4.25], [1.22, 1.22, 4.25]),
            b([11.40, -0.61, 3.25], [1.22, 1.22, 3.25]),
            b([13.84, -0.61, 3.50], [1.22, 1.22, 0.50]),
            b([-13.01, 1.83, 4.25], [1.22, 1.22, 0.25]),
            b([-10.57, 1.83, 4.75], [1.22, 1.22, 0.75]),
            b([-5.69, 1.83, 6.50], [1.22, 1.22, 0.50]),
            b([-3.25, 1.83, 5.50], [1.22, 1.22, 0.50]),
            b([0.42, 1.83, 2.75], [2.44, 1.22, 2.75]),
            b([6.52, 1.83, 6.50], [1.22, 1.22, 0.50]),
            b([8.96, 1.83, 5.75], [1.22, 1.22, 0.75]),
            b([11.40, 3.05, 4.50], [1.22, 2.44, 0.50]),
            b([-13.01, 4.27, 4.00], [1.22, 1.22, 0.10]),
            b([-10.57, 4.27, 3.75], [1.22, 1.22, 0.75]),
            b([-8.13, 4.27, 3.25], [1.22, 1.22, 3.25]),
            b([-5.69, 4.27, 5.00], [1.22, 1.22, 0.50]),
            b([-3.25, 4.27, 4.25], [1.22, 1.22, 0.75]),
            b([-0.80, 4.27, 4.25], [1.22, 1.22, 0.25]),
            b([1.64, 4.27, 4.50], [1.22, 1.22, 0.50]),
            b([4.08, 4.27, 3.25], [1.22, 1.22, 3.25]),
            b([6.52, 4.27, 5.00], [1.22, 1.22, 0.50]),
            b([8.96, 4.27, 4.25], [1.22, 1.22, 0.75]),
            b([13.84, 4.27, 2.75], [1.22, 1.22, 2.75]),
            b([16.29, 5.49, 5.00], [1.22, 2.44, 0.10]),
            b([-10.57, 6.71, 3.50], [1.22, 1.22, 0.50]),
            b([-6.91, 6.71, 4.25], [2.44, 1.22, 0.25]),
            b([-3.25, 6.71, 3.75], [1.22, 1.22, 0.75]),
            b([1.64, 6.71, 3.25], [1.22, 1.22, 0.25]),
            b([4.08, 6.71, 4.00], [1.22, 1.22, 0.50]),
            b([6.52, 6.71, 4.25], [1.22, 1.22, 0.25]),
            b([8.96, 6.71, 4.00], [1.22, 1.22, 0.50]),
            b([11.40, 6.71, 3.25], [1.22, 1.22, 0.25]),
            b([13.84, 6.71, 2.50], [1.22, 1.22, 2.50]),
        ],
    },
    Structure {
        id: 168,
        name: "Moonwell shrine",
        stone: true,
        parts: &[
            StructurePart {
                model: "World\\Generic\\NIGHTELF\\PASSIVE DOODADS\\MoonwellLight\\MoonWellLight.m2",
                at: [0.85, 0.15, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\NightElf\\Passive Doodads\\Lanterns\\NightElfLantern01.m2",
                at: [6.00, -4.50, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\NightElf\\Passive Doodads\\Lanterns\\NightElfLantern01.m2",
                at: [6.00, 4.50, 0.00],
                yaw: 0.0000,
            },
            StructurePart {
                model: "World\\Generic\\NightElf\\Passive Doodads\\StoneRunes\\KalidarStoneRune01.m2",
                at: [-7.00, 0.00, 0.00],
                yaw: 0.0000,
            },
        ],
        blocks: &[
        ],
        boxes: &[
            b([5.20, -4.19, 0.75], [0.50, 1.00, 0.75]),
            b([6.20, -4.69, 1.00], [0.50, 0.50, 1.00]),
            b([7.20, -4.19, 0.75], [0.50, 1.00, 0.75]),
            b([-2.80, -3.69, 0.25], [0.50, 0.50, 0.25]),
            b([-1.30, -3.19, 1.00], [1.00, 1.00, 1.00]),
            b([0.20, -3.69, 1.75], [0.50, 0.50, 1.75]),
            b([1.20, -3.69, 0.88], [0.50, 0.50, 0.88]),
            b([6.20, -3.69, 0.75], [0.50, 0.50, 0.75]),
            b([-3.30, -2.69, 1.00], [1.00, 0.50, 1.00]),
            b([0.70, -2.69, 2.00], [1.00, 0.50, 2.00]),
            b([2.20, -2.69, 1.00], [0.50, 0.50, 1.00]),
            b([3.20, -2.19, 0.75], [0.50, 1.00, 0.75]),
            b([-7.80, -1.69, 0.50], [0.50, 0.50, 0.50]),
            b([-6.80, -1.69, 1.00], [0.50, 0.50, 1.00]),
            b([-5.80, -1.69, 0.75], [0.50, 0.50, 0.75]),
            b([-3.80, -1.69, 1.75], [0.50, 0.50, 1.75]),
            b([-2.80, -1.69, 1.50], [0.50, 0.50, 1.50]),
            b([-1.80, -1.69, 0.75], [0.50, 0.50, 0.75]),
            b([-0.80, -1.69, 0.50], [0.50, 0.50, 0.50]),
            b([0.20, -1.69, 1.00], [0.50, 0.50, 1.00]),
            b([1.70, -1.69, 0.75], [1.00, 0.50, 0.75]),
            b([4.20, -1.69, 0.25], [0.50, 0.50, 0.25]),
            b([-7.80, -0.19, 0.75], [0.50, 1.00, 0.75]),
            b([-6.30, -0.19, 1.25], [1.00, 1.00, 1.25]),
            b([-4.80, -0.19, 0.75], [0.50, 1.00, 0.75]),
            b([-3.80, -0.19, 2.25], [0.50, 1.00, 2.25]),
            b([-2.80, -0.69, 1.75], [0.50, 0.50, 1.75]),
            b([-1.80, -0.19, 0.12], [0.50, 1.00, 0.12]),
            b([3.20, -0.69, 1.00], [1.50, 0.50, 1.00]),
            b([-2.80, 0.31, 2.00], [0.50, 0.50, 2.00]),
            b([2.70, 0.31, 1.00], [1.00, 0.50, 1.00]),
            b([4.20, 0.31, 0.75], [0.50, 0.50, 0.75]),
            b([-7.80, 1.31, 0.50], [0.50, 0.50, 0.50]),
            b([-6.80, 1.31, 1.00], [0.50, 0.50, 1.00]),
            b([-5.80, 1.31, 0.75], [0.50, 0.50, 0.75]),
            b([-4.80, 1.31, 0.50], [0.50, 0.50, 0.50]),
            b([-2.80, 1.31, 1.00], [1.50, 0.50, 1.00]),
            b([-0.30, 1.31, 0.25], [1.00, 0.50, 0.25]),
            b([1.20, 1.31, 0.50], [0.50, 0.50, 0.50]),
            b([2.20, 1.31, 1.25], [0.50, 0.50, 1.25]),
            b([3.20, 1.31, 1.00], [0.50, 0.50, 1.00]),
            b([4.20, 1.81, 0.50], [0.50, 1.00, 0.50]),
            b([-6.30, 2.31, 0.25], [1.00, 0.50, 0.25]),
            b([-3.80, 2.81, 1.00], [0.50, 1.00, 1.00]),
            b([-2.80, 2.31, 0.88], [0.50, 0.50, 0.88]),
            b([-1.80, 2.81, 1.00], [0.50, 1.00, 1.00]),
            b([-0.80, 2.81, 1.25], [0.50, 1.00, 1.25]),
            b([0.20, 2.81, 1.00], [0.50, 1.00, 1.00]),
            b([1.20, 2.31, 1.75], [0.50, 0.50, 1.75]),
            b([2.20, 2.31, 2.25], [0.50, 0.50, 2.25]),
            b([3.20, 2.31, 2.00], [0.50, 0.50, 2.00]),
            b([-2.80, 3.31, 1.00], [0.50, 0.50, 1.00]),
            b([1.20, 3.31, 1.00], [0.50, 0.50, 1.00]),
            b([2.20, 3.31, 2.00], [0.50, 0.50, 2.00]),
            b([3.20, 3.31, 0.75], [0.50, 0.50, 0.75]),
            b([-0.80, 4.31, 1.00], [0.50, 0.50, 1.00]),
            b([1.20, 4.31, 0.25], [0.50, 0.50, 0.25]),
            b([5.20, 4.81, 0.75], [0.50, 1.00, 0.75]),
            b([6.20, 4.31, 1.00], [0.50, 0.50, 1.00]),
            b([7.20, 4.81, 0.75], [0.50, 1.00, 0.75]),
            b([6.20, 5.31, 0.75], [0.50, 0.50, 0.75]),
        ],
    },
// structure_boxes: end
];

/// What the build bar offers: every piece, then the blueprints.
#[derive(Clone, Copy)]
pub enum Choice {
    Piece(PieceType),
    Blueprint(&'static Blueprint),
}

pub fn choices() -> usize {
    buildable().len() + BLUEPRINTS.len()
}

pub fn choice(i: usize) -> Choice {
    let i = i % choices();
    match buildable().get(i) {
        Some(t) => Choice::Piece(*t),
        None => Choice::Blueprint(&BLUEPRINTS[i - buildable().len()]),
    }
}

impl Choice {
    pub fn name(&self) -> &'static str {
        match self {
            Choice::Piece(t) => def(*t).name,
            Choice::Blueprint(b) => b.name,
        }
    }
}

/// A blueprint's pieces put down at `origin` facing `yaw`, in the order they hold each other up.
/// A built floor: things meant for the ground may stand on it.
pub fn is_floor(kind: PieceType) -> bool {
    matches!(kind.base(), PieceType::WoodFloor | PieceType::StoneFloor)
        || furniture(kind).is_some_and(|f| f.top > 0.0)
}

pub fn blueprint_parts(b: &Blueprint, origin: Vec3, yaw: f32) -> Vec<Placed> {
    b.parts
        .iter()
        .map(|(kind, at, t)| Placed {
            kind: *kind,
            pos: origin + turn(yaw, Vec3::from_array(*at)),
            yaw: yaw + t,
        })
        .collect()
}

#[cfg(test)]
mod blueprint_tests {
    use super::*;

    #[test]
    fn floors_take_ground_pieces() {
        assert!(is_floor(PieceType::WoodFloor) && is_floor(PieceType::StoneFloor));
        assert!(!is_floor(PieceType::WoodWall) && !is_floor(PieceType::WoodRoof));
    }

    #[test]
    fn blueprints_hold_together() {
        for b in &BLUEPRINTS {
            let parts = blueprint_parts(b, Vec3::new(100.0, 50.0, 10.0), 0.7);
            // No piece sits inside another.
            for (i, p) in parts.iter().enumerate() {
                let others: Vec<Placed> = parts
                    .iter()
                    .enumerate()
                    .filter(|(j, _)| *j != i)
                    .map(|(_, q)| *q)
                    .collect();
                assert!(!overlaps(&others, p), "{}: {:?} overlaps", b.name, p.kind);
            }
            // Every piece is within the server's reach of a builder 5.4 yards back.
            let builder = Vec3::new(100.0, 50.0, 11.0) - turn(0.7, Vec3::X * 5.4);
            for p in &parts {
                assert!(
                    p.center().distance(builder) < 13.0,
                    "{}: {:?} too far",
                    b.name,
                    p.kind
                );
            }
        }
        // 17 built pieces less the sentry gun (a killstreak now), the props, the structures,
        // the furniture, three blueprints.
        let pieces = 16 + PROPS.len() + STRUCTURES.len() + FURNITURE.len();
        assert_eq!(choices(), pieces + 3);
        assert!(matches!(
            choice(pieces),
            Choice::Blueprint(_)
        ));
        assert!(matches!(
            choice(choices()),
            Choice::Piece(PieceType::WoodFloor)
        ));
        assert_eq!(boxes_of(PieceType::WoodDoor, true)[3].center[0], 0.8);
        assert_eq!(boxes_of(PieceType::WoodDoor, false)[3].center[0], 0.0);
    }
}

// ── Furniture (the server's `Furniture.h`) ──

/// The first furniture piece's id (the server's `Build::kFurnitureBase`).
pub const FURNITURE_BASE: u8 = 200;

/// What a piece of furniture does (the server's `Furniture::Use`, in its order).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FurnitureUse {
    Decor,
    Seat,
    Bed,
    Surface,
    Stash,
    Book,
    Station,
    Mailbox,
    Dummy,
    Trophy,
    Stage,
}

/// What it stands on (the server's `Furniture::Mount`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mount {
    Floor,
    Surface,
    Wall,
}

/// A furniture piece: WoW 1.12's own doodad, its bounds (`examples/prop_bounds.rs`), its use,
/// what it stands on and, for a table, the height of its top. `model` None: the server stands a
/// WoW object in it (the mailbox, the dummy) and that object is what is seen.
pub struct Furniture {
    pub id: u8,
    pub name: &'static str,
    pub model: Option<&'static str>,
    pub center: [f32; 3],
    pub half: [f32; 3],
    pub use_: FurnitureUse,
    pub mount: Mount,
    pub top: f32,
}

// FURNITURE_TABLE
pub static FURNITURE: [Furniture; 55] = [
    Furniture {
        id: 200,
        name: "Wooden chair",
        model: Some("World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallChair\\WestfallChair.m2"),
        center: [-0.12, -0.03, 0.26],
        half: [0.33, 0.32, 0.27],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 201,
        name: "Cushioned chair",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Chairs\\GeneralChairHighEnd01.m2"),
        center: [-0.31, -0.00, 1.04],
        half: [0.46, 0.80, 1.04],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 202,
        name: "Inn bench",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Benches\\InnBench.m2"),
        center: [-0.01, 0.00, 0.39],
        half: [0.40, 2.10, 0.39],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 203,
        name: "Stormwind bench",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Benches\\StormWindBench01.m2"),
        center: [-0.47, 0.00, 0.22],
        half: [0.53, 1.50, 0.28],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 204,
        name: "Dwarven chair",
        model: Some("World\\Generic\\Dwarf\\Passive Doodads\\Chairs\\IronForgeChair_ornate01.m2"),
        center: [-0.14, 0.00, 1.53],
        half: [0.55, 1.04, 1.56],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 205,
        name: "Log bench",
        model: Some("World\\Generic\\Tauren\\Passive Doodads\\Chairs\\TaurenLogChair01.m2"),
        center: [-0.03, 0.03, 0.13],
        half: [0.70, 3.69, 0.37],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 206,
        name: "High-backed chair",
        model: Some("World\\Generic\\Dwarf\\Passive Doodads\\Chairs\\DwarvenHighEndChair.m2"),
        center: [-0.56, 0.00, 0.85],
        half: [0.69, 0.59, 0.86],
        use_: FurnitureUse::Seat,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 207,
        name: "Inn bed",
        model: Some("World\\Dungeon\\GoldshireInn\\Innbed\\InnBed.m2"),
        center: [0.05, 0.00, 0.67],
        half: [2.39, 1.69, 0.67],
        use_: FurnitureUse::Bed,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 208,
        name: "Canopy bed",
        model: Some("World\\Dungeon\\GoldshireInn\\InnBedCanopy\\InnBedCanopy.m2"),
        center: [0.00, 0.00, 2.15],
        half: [2.96, 3.10, 2.15],
        use_: FurnitureUse::Bed,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 209,
        name: "Bunk bed",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Beds\\Bunkbed01.m2"),
        center: [0.07, 0.03, 1.72],
        half: [2.63, 1.39, 1.72],
        use_: FurnitureUse::Bed,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 210,
        name: "Dwarven bed",
        model: Some("World\\Generic\\Dwarf\\Passive Doodads\\Beds\\DwarvenBed01.m2"),
        center: [0.01, 0.35, 1.13],
        half: [2.20, 3.51, 1.15],
        use_: FurnitureUse::Bed,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 211,
        name: "Inn table",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Tables\\InnTable.m2"),
        center: [-0.00, 0.00, 0.52],
        half: [1.43, 1.99, 0.52],
        use_: FurnitureUse::Surface,
        mount: Mount::Floor,
        top: 1.04,
    },
    Furniture {
        id: 212,
        name: "Small table",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Tables\\InnTableSmall.m2"),
        center: [-0.02, 0.01, 0.57],
        half: [1.06, 1.08, 0.57],
        use_: FurnitureUse::Surface,
        mount: Mount::Floor,
        top: 1.14,
    },
    Furniture {
        id: 213,
        name: "Long table",
        model: Some("World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallTable\\WestfallTable.m2"),
        center: [0.05, -0.00, 0.44],
        half: [1.35, 1.42, 0.54],
        use_: FurnitureUse::Surface,
        mount: Mount::Floor,
        top: 0.98,
    },
    Furniture {
        id: 214,
        name: "Writing desk",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Desks\\FancyDesk01.m2"),
        center: [-0.44, -0.00, 0.86],
        half: [0.98, 1.59, 0.88],
        use_: FurnitureUse::Surface,
        mount: Mount::Floor,
        top: 1.74,
    },
    Furniture {
        id: 215,
        name: "Chest",
        model: Some("World\\Generic\\ActiveDoodads\\Chest01\\Chest01.m2"),
        center: [-0.00, -0.00, 0.32],
        half: [0.39, 0.59, 0.31],
        use_: FurnitureUse::Stash,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 216,
        name: "Footlocker",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Footlockers\\DuskwoodFootLocker01.m2"),
        center: [-0.00, -0.00, 0.23],
        half: [0.30, 0.69, 0.24],
        use_: FurnitureUse::Stash,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 217,
        name: "Wardrobe",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Wardrobe\\DuskwoodWardrobe01.m2"),
        center: [0.34, 0.01, 1.71],
        half: [0.66, 1.05, 1.71],
        use_: FurnitureUse::Stash,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 218,
        name: "Bookshelf",
        model: Some("World\\Generic\\Human\\Passive Doodads\\BookShelves\\DuskwoodBookshelf01.m2"),
        center: [0.12, -0.00, 2.03],
        half: [0.44, 1.40, 2.04],
        use_: FurnitureUse::Book,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 219,
        name: "Abbey bookshelf",
        model: Some("World\\Generic\\Human\\Passive Doodads\\BookShelves\\AbbeyShelf01.m2"),
        center: [-0.09, -0.01, 2.54],
        half: [1.78, 0.77, 2.53],
        use_: FurnitureUse::Book,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 220,
        name: "Book stack",
        model: Some("World\\Generic\\Human\\Passive Doodads\\BookStacks\\GeneralBookStackShort01.m2"),
        center: [-0.14, 0.05, 0.18],
        half: [0.40, 0.29, 0.18],
        use_: FurnitureUse::Book,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 221,
        name: "Lectern",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Podiums\\DuskwoodPodium01.m2"),
        center: [0.04, 0.00, 0.74],
        half: [0.39, 0.41, 0.74],
        use_: FurnitureUse::Book,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 222,
        name: "Smith's anvil",
        model: Some("World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2"),
        center: [0.05, 0.00, 0.48],
        half: [0.73, 0.36, 0.48],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 223,
        name: "Forge",
        model: Some("World\\Generic\\Dwarf\\Passive Doodads\\Forgebonfire\\Forgebonfire.m2"),
        center: [0.18, 0.06, 0.33],
        half: [1.83, 1.70, 0.41],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 224,
        name: "Cooking fire",
        model: Some("World\\Generic\\Orc\\Passive Doodads\\TableCooker\\OrcTableCooker01Fire.m2"),
        center: [0.00, 0.00, 0.65],
        half: [1.49, 1.49, 0.64],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 225,
        name: "Loom",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Loom\\LoomHumanBlue.m2"),
        center: [-0.01, 0.01, 1.15],
        half: [0.48, 1.08, 1.15],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 226,
        name: "Alchemy table",
        model: Some("World\\Generic\\Undead\\Passive Doodads\\UndeadAlchemyTable\\undead_alchemy_table.m2"),
        center: [0.01, 0.06, 1.23],
        half: [0.97, 1.68, 1.23],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 227,
        name: "Mailbox",
        model: None,
        center: [0.00, 0.01, 1.54],
        half: [0.52, 0.71, 1.54],
        use_: FurnitureUse::Mailbox,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 228,
        name: "Training dummy",
        model: None,
        // Only its base: the server's dummy standing in it takes the shots.
        center: [0.12, -0.11, 0.05],
        half: [0.29, 0.89, 0.05],
        use_: FurnitureUse::Dummy,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 229,
        name: "Trophy plinth",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Altars\\Altar01.m2"),
        center: [-0.08, 0.01, 0.51],
        half: [1.11, 2.03, 0.68],
        use_: FurnitureUse::Trophy,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 230,
        name: "Bard's stage",
        model: Some("World\\Generic\\DarkIronDwarf\\Passive Doodads\\WoodPlanks\\DarkIronWoodPlanks01.m2"),
        center: [-1.00, 0.12, 0.24],
        half: [4.70, 3.85, 0.26],
        use_: FurnitureUse::Stage,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 231,
        name: "Stormwind banner",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Banners\\StormwindLionBanner.m2"),
        center: [-0.02, -0.03, 2.95],
        half: [0.19, 2.43, 3.50],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 232,
        name: "Horde banner",
        model: Some("World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2"),
        center: [-0.01, -0.00, 2.82],
        half: [0.33, 1.59, 2.77],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 233,
        name: "Clan banner",
        model: Some("World\\Generic\\Orc\\Passive Doodads\\Banners\\ClanBanner01.m2"),
        center: [-0.16, 0.00, 3.07],
        half: [0.21, 1.19, 3.26],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 234,
        name: "Stormwind rug",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Rugs\\StormwindRug01.m2"),
        center: [0.08, 0.02, 0.01],
        half: [1.55, 1.40, 0.05],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 235,
        name: "Red rug",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Rugs\\KarazahnRugRed.m2"),
        center: [0.11, -0.01, 0.01],
        half: [5.00, 2.39, 0.05],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 236,
        name: "Bearskin rug",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Rugs\\GeneralBearSkinRug01.m2"),
        center: [0.14, 0.00, 0.43],
        half: [2.79, 2.89, 0.45],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 237,
        name: "Tauren rug",
        model: Some("World\\Generic\\Tauren\\Passive Doodads\\TaurenRugs\\TaurenRug01.m2"),
        center: [-0.00, 0.02, 0.03],
        half: [1.34, 0.90, 0.05],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 238,
        name: "Candle",
        model: Some("World\\Generic\\PassiveDoodads\\Lights\\Candle01.m2"),
        center: [0.00, -0.00, 0.27],
        half: [0.12, 0.12, 0.27],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 239,
        name: "Candelabra",
        model: Some("World\\Generic\\PassiveDoodads\\Lights\\Candelabra01.m2"),
        center: [0.00, 0.00, 0.28],
        half: [0.18, 0.43, 0.28],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 240,
        name: "Brazier",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2"),
        center: [-0.02, -0.06, 0.37],
        half: [1.11, 1.16, 0.36],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 241,
        name: "Wall candelabra",
        model: Some("World\\Generic\\PassiveDoodads\\Lights\\CandelabraTallWall01.m2"),
        center: [0.00, 0.00, 1.59],
        half: [0.29, 0.84, 1.59],
        use_: FurnitureUse::Decor,
        mount: Mount::Wall,
        top: 0.00,
    },
    Furniture {
        id: 242,
        name: "Hanging lantern",
        model: Some("World\\Generic\\Human\\Passive Doodads\\HangingLanterns\\GeneralHangingLantern01.m2"),
        center: [0.00, 0.00, 0.92],
        half: [0.36, 0.36, 1.19],
        use_: FurnitureUse::Decor,
        mount: Mount::Wall,
        top: 0.00,
    },
    Furniture {
        id: 243,
        name: "Flowers",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Flowers\\FlowersBunch01.m2"),
        center: [0.02, -0.06, 0.13],
        half: [0.23, 0.41, 0.24],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 244,
        name: "Planter",
        model: Some("World\\Generic\\Human\\Passive Doodads\\PlanterBoxes\\StormwindPlanter.m2"),
        center: [0.00, 0.00, 0.56],
        half: [2.63, 2.66, 1.22],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 245,
        name: "Beer keg",
        model: Some("World\\Generic\\Human\\Passive Doodads\\BeerKegs\\BeerKeg01.m2"),
        center: [-0.15, 0.00, 0.65],
        half: [0.68, 0.74, 0.65],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 246,
        name: "Mug",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Mugs\\Mug01.m2"),
        center: [-0.05, -0.02, 0.13],
        half: [0.12, 0.16, 0.14],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 247,
        name: "Weapon rack",
        model: Some("World\\Generic\\Human\\Passive Doodads\\WeaponRacks\\GeneralWeaponrack01.m2"),
        center: [0.05, 0.18, 1.33],
        half: [0.65, 0.53, 1.45],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 248,
        name: "Wall shield",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Weapons&Armor\\WallShield03.m2"),
        center: [0.05, 0.00, -0.00],
        half: [0.05, 0.36, 0.53],
        use_: FurnitureUse::Decor,
        mount: Mount::Wall,
        top: 0.00,
    },
    Furniture {
        id: 249,
        name: "Mounted boar head",
        model: Some("World\\Generic\\Human\\Passive Doodads\\AnimalHeads\\DuskwoodBoarHead01.m2"),
        center: [0.42, 0.00, 0.32],
        half: [0.48, 0.48, 0.62],
        use_: FurnitureUse::Decor,
        mount: Mount::Wall,
        top: 0.00,
    },
    Furniture {
        id: 250,
        name: "Stuffed bear",
        model: Some("World\\Generic\\Human\\Passive Doodads\\AnimalHeads\\StuffedBear.m2"),
        center: [0.12, -0.07, 1.90],
        half: [1.09, 1.12, 1.91],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 251,
        name: "Painting",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Artwork\\StormwindPainting01.m2"),
        center: [-0.01, -0.00, -0.05],
        half: [0.11, 2.36, 1.22],
        use_: FurnitureUse::Decor,
        mount: Mount::Wall,
        top: 0.00,
    },
    Furniture {
        id: 252,
        name: "Globe",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Globes\\Globe01.m2"),
        center: [0.01, -0.05, 0.83],
        half: [0.39, 0.46, 0.84],
        use_: FurnitureUse::Decor,
        mount: Mount::Surface,
        top: 0.00,
    },
    Furniture {
        id: 253,
        name: "Armor stand",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Armor\\ArmorStand.m2"),
        center: [0.00, -0.00, 0.89],
        half: [0.45, 0.43, 0.89],
        use_: FurnitureUse::Decor,
        mount: Mount::Floor,
        top: 0.00,
    },
    Furniture {
        id: 254,
        name: "Potbelly stove",
        model: Some("World\\Generic\\Human\\Passive Doodads\\Stoves\\potbellystove.m2"),
        center: [-0.32, 0.04, 1.85],
        half: [0.72, 0.54, 1.89],
        use_: FurnitureUse::Station,
        mount: Mount::Floor,
        top: 0.00,
    },
];

/// Whether a piece is furniture.
pub fn is_furniture(t: PieceType) -> bool {
    t.0 >= FURNITURE_BASE
}

/// A furniture piece's entry.
pub fn furniture(t: PieceType) -> Option<&'static Furniture> {
    t.0.checked_sub(FURNITURE_BASE)
        .and_then(|i| FURNITURE.get(usize::from(i)))
}

/// Furniture's finer placement (the server's `Furniture::kGrid` and `kTurnStep`): a quarter-yard
/// grid and 15-degree turns; free placement (Sprint held) takes neither.
pub const FURNITURE_GRID: f32 = 0.25;
pub const FURNITURE_TURN: f32 = std::f32::consts::PI / 12.0;

/// `v` on the furniture grid.
pub fn grid(v: f32) -> f32 {
    (v / FURNITURE_GRID).round() * FURNITURE_GRID
}

/// `yaw` to the nearest 15-degree step, in [0, 2pi).
pub fn snap_turn(yaw: f32) -> f32 {
    ((yaw / FURNITURE_TURN).round() * FURNITURE_TURN).rem_euclid(std::f32::consts::TAU)
}

/// Where a piece of furniture goes, aimed at `aim` on a surface facing `normal` (WoW axes), the
/// builder looking along `facing` (a WoW yaw), turned `turns` 15-degree steps. A wall piece aimed
/// at a wall hangs flat on it, its back (its box's -x face) against the wall and facing out of it;
/// anything else stands where it is aimed, facing the builder. Off `free`, positions go on the
/// grid (a wall piece's height too) and turns on the 15-degree steps.
pub fn place_furniture(
    kind: PieceType,
    aim: Vec3,
    normal: Vec3,
    facing: f32,
    turns: i32,
    free: bool,
) -> Placed {
    let Some(f) = furniture(kind) else {
        return Placed {
            kind,
            pos: aim,
            yaw: facing,
        };
    };
    let flat = Vec3::new(normal.x, normal.y, 0.0);
    if f.mount == Mount::Wall && normal.z.abs() < 0.5 && flat.length() > 1e-3 {
        let n = flat.normalize();
        let yaw = n.y.atan2(n.x).rem_euclid(std::f32::consts::TAU);
        let out = f.half[0] - f.center[0];
        let z = if free { aim.z } else { grid(aim.z) } - f.center[2];
        let mut pos = aim + n * out;
        pos.z = z;
        return Placed { kind, pos, yaw };
    }
    // Facing the builder: its front (+x) toward them.
    let yaw = facing + std::f32::consts::PI + turns as f32 * FURNITURE_TURN;
    let (pos, yaw) = if free {
        (aim, yaw.rem_euclid(std::f32::consts::TAU))
    } else {
        (Vec3::new(grid(aim.x), grid(aim.y), aim.z), snap_turn(yaw))
    };
    Placed { kind, pos, yaw }
}
