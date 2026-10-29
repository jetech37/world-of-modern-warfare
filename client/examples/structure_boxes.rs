//! `cargo run --release --example structure_boxes -- <WoW Data dir> [out dir]`: the collision of
//! the build catalogue's structures (crate::build::pieces::STRUCTURES), printed as the Rust and
//! C++ tables both sides use. Each structure is one or more of WoW's own models from the
//! player's install, placed in the piece's frame; its collision is worked out from the models'
//! triangles on a grid of cells, so walls and posts block, a floor or a bridge deck carries you,
//! and a roof or an awning stands over your head without filling the room under it:
//!
//! - a cell with something at body height above its lowest surface is solid from the ground to
//!   its top (a wall, a post, a railing on the ground);
//! - a cell whose lowest surface is under head height and nothing above it at body height is a
//!   step up to that surface (a porch, a bridge's ramp, a deck);
//! - a cell whose lowest surface is over head height is a slab there (a roof, an awning, a high
//!   deck), with the room under it clear.
//!
//! Like cells are merged into boxes. With `out dir` it also draws each structure from above and
//! from the front, with its boxes, as PNGs to check the fit. Nothing of WoW's is copied, only
//! each model's shape as boxes.

use std::path::{Path, PathBuf};

use benilla_formats::{Chain, ModelBlend, parse_m2_render_submeshes};

/// A model placed in a structure's frame: its path, its offset (x forward, y left, z up, yards)
/// and its turn about z.
struct Part {
    model: &'static str,
    at: [f32; 3],
    yaw: f32,
}

const fn part(model: &'static str, at: [f32; 3], yaw: f32) -> Part {
    Part { model, at, yaw }
}

/// A block of a structure built of the install's textures rather than a model (a city wall's
/// masonry, its walkway and steps): its centre and half sizes in the piece's frame, and the
/// texture the client draws it with. Solid as it stands, like a model's triangles.
struct Block {
    center: [f32; 3],
    half: [f32; 3],
    texture: &'static str,
}

const fn block(center: [f32; 3], half: [f32; 3], texture: &'static str) -> Block {
    Block {
        center,
        half,
        texture,
    }
}

/// (id, name, material "Wood"/"Stone", models, blocks), in id order from 140.
type Entry = (
    u8,
    &'static str,
    &'static str,
    &'static [Part],
    &'static [Block],
);
const STRUCTURES: &[Entry] = &[
    (
        140,
        "Cottage",
        "Wood",
        &[part(
            "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse01.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        141,
        "Shack",
        "Wood",
        &[part(
            "World\\Generic\\Human\\Passive Doodads\\Shack\\Shack.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        142,
        "Watchtower",
        "Stone",
        &[part(
            "World\\Azeroth\\Elwynn\\Buildings\\HumanGuardTower\\HumanGuardTower.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        143,
        "Lookout tower",
        "Wood",
        &[part(
            "World\\Azeroth\\Stranglethorn\\Buildings\\TrollWatchTower\\TrollWatchTower.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        144,
        "Barn",
        "Wood",
        &[part(
            "World\\Azeroth\\Duskwood\\PassiveDoodads\\DuskwoodBarn\\BarnDuskwood.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        145,
        "Stable",
        "Wood",
        &[part(
            "World\\Azeroth\\Westfall\\PassiveDoodads\\Westfall Stable\\WestFallStable01.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        146,
        "Smithy",
        "Stone",
        &[part(
            "World\\Azeroth\\Elwynn\\Buildings\\BlackSmith\\BlackSmith.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        147,
        "Forge",
        "Stone",
        &[part(
            "World\\SkillActivated\\TradeskillEnablers\\Tradeskill_Forge_03.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        148,
        "Market stall",
        "Wood",
        &[
            part(
                "World\\Generic\\Human\\Passive Doodads\\VendorAwnings\\StormwindVendorAwning01.m2",
                [0.0, 0.0, 0.0],
                0.0,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
                [-0.9, 0.9, 0.0],
                0.3,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                [-1.0, -1.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        149,
        "Palisade",
        "Wood",
        &[part(
            "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost02.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        150,
        "Palisade gate",
        "Wood",
        &[
            part(
                "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost01.m2",
                [0.0, 7.1, 0.0],
                0.0,
            ),
            part(
                "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost01.m2",
                [0.0, -7.1, 0.0],
                0.0,
            ),
            part(
                "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost03.m2",
                [0.0, 3.2, 0.0],
                0.0,
            ),
            part(
                "World\\Kalimdor\\Dustwallow\\PassiveDoodads\\Outposts\\DustwallowOutpost03.m2",
                [0.0, -3.2, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        151,
        "Footbridge",
        "Wood",
        &[part(
            "World\\Azeroth\\Elwynn\\Buildings\\Bridges\\ElwynnFootbridge.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        152,
        "Stone bridge",
        "Stone",
        &[part(
            "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\Bridge\\StonebridgeShort.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        153,
        "Village well",
        "Stone",
        &[part(
            "World\\Generic\\PassiveDoodads\\Well\\Well.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        154,
        "Guard post",
        "Wood",
        &[part(
            "World\\Azeroth\\DeadwindPass\\Buildings\\AbandonedGuardTower\\AbandonedHumanGuardTower.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        155,
        "Woodshed",
        "Wood",
        &[part(
            "World\\Azeroth\\Westfall\\Buildings\\Shed\\WestfallShed.m2",
            [0.0, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    // The second catalogue: real buildings of a town, a farm and a camp.
    (
        156,
        "Inn",
        "Wood",
        &[
            part(
                "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse02.m2",
                [-7.4, -4.25, 0.0],
                0.0,
            ),
            part(
                "World\\Azeroth\\Elwynn\\PassiveDoodads\\Signs\\Shop\\HumanInnSignPost.m2",
                [-15.0, 4.5, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\Benches\\InnBench.m2",
                [-11.0, 5.0, 0.0],
                Q,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                [-15.5, -15.5, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        157,
        "Chapel",
        "Wood",
        &[part(
            "World\\Azeroth\\Westfall\\Buildings\\Church\\WestfallChurch.m2",
            [4.65, 1.05, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        158,
        "Windmill",
        "Wood",
        &[part(
            "World\\Azeroth\\Westfall\\Buildings\\Windmill\\WestfallWindmill.m2",
            [1.2, 0.0, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        159,
        "Farmhouse",
        "Wood",
        &[
            part(
                "World\\Lordaeron\\Arathi\\PassiveDoodads\\FarmHouses\\ArathiFarmHouse01.m2",
                [-6.0, 0.0, 0.0],
                0.0,
            ),
            // The yard before its door, fenced on three sides with a gap for the gate.
            part(WESTFALL_FENCE, [8.2, -9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [12.5, -9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [16.8, -9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [8.2, 9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [12.5, 9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [16.8, 9.0, 0.0], 0.0),
            part(WESTFALL_FENCE, [16.8, -4.8, 0.0], Q),
            part(WESTFALL_FENCE, [16.8, -1.5, 0.0], Q),
            part(WESTFALL_FENCE, [16.8, 5.7, 0.0], Q),
            part(WESTFALL_FENCE, [16.8, 9.0, 0.0], Q),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\HayStack\\WestFallHayStack01.m2",
                [12.0, 5.0, 0.0],
                0.0,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Scarecrow\\WestFallScarecrow.m2",
                [11.0, -4.5, 0.0],
                Q,
            ),
        ],
        &[],
    ),
    (
        160,
        "Blacksmith",
        "Stone",
        &[
            part(
                "World\\SkillActivated\\TradeskillEnablers\\Tradeskill_Forge_03.m2",
                [0.0, -4.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\VendorAwnings\\StormwindVendorAwning01.m2",
                [0.3, 3.2, 0.0],
                0.0,
            ),
            part(
                "World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2",
                [0.2, 3.4, 0.0],
                Q,
            ),
            // The quench barrel by the anvil, and a trough.
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                [2.6, 3.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Tauren\\Passive Doodads\\WaterTroughs\\WaterTroughSmall01.m2",
                [-3.2, 3.6, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\firewood\\FirewoodPile01.m2",
                [-3.8, -1.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        161,
        "Lumber yard",
        "Wood",
        &[
            part(
                "World\\Generic\\Human\\Passive Doodads\\LogMachines\\LogMachine01.m2",
                [2.3, 0.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\LumberPiles\\DeadMineLumberPileLarge.m2",
                [11.0, -5.5, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\LumberPiles\\DeadMineLumberPileLarge.m2",
                [11.0, -2.8, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\PeasantLumber\\PeasantLumber01.m2",
                [10.5, 3.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\firewood\\FirewoodPile01.m2",
                [11.0, 6.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        162,
        "Lighthouse",
        "Stone",
        &[part(
            "World\\Azeroth\\Westfall\\Buildings\\Lighthouse\\WestfallLighthouse.m2",
            [-2.1, -1.35, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        163,
        "Gate arch",
        "Stone",
        &[
            // Turned so the way through runs forward.
            part(
                "World\\Generic\\Centaur\\Passive Doodads\\CentaurRuins\\Centaur_Arch01.m2",
                [0.0, 0.0, 0.0],
                Q,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2",
                [3.2, -6.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2",
                [3.2, 6.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        164,
        "City wall",
        "Stone",
        &[],
        &[
            // The curtain: Stormwind's dressed stone, a walk along its top.
            block([0.0, 0.0, 2.25], [1.2, 8.0, 2.25], SW_STONE),
            // The outer parapet and its merlons.
            block([1.0, 0.0, 4.75], [0.2, 8.0, 0.25], SW_STONE),
            block([1.0, -7.45, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, -5.0, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, -2.5, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, 0.0, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, 2.5, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, 5.0, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            block([1.0, 7.45, 5.4], [0.2, 0.55, 0.4], SW_STONE),
            // The walk's boards.
            block([-0.15, 0.0, 4.55], [1.0, 8.0, 0.05], SW_BOARDS),
            // Steps up the inside, half a yard a yard.
            block([-2.0, 3.5, 0.25], [0.8, 4.5, 0.25], SW_STEPS),
            block([-2.0, 4.0, 0.75], [0.8, 4.0, 0.25], SW_STEPS),
            block([-2.0, 4.5, 1.25], [0.8, 3.5, 0.25], SW_STEPS),
            block([-2.0, 5.0, 1.75], [0.8, 3.0, 0.25], SW_STEPS),
            block([-2.0, 5.5, 2.25], [0.8, 2.5, 0.25], SW_STEPS),
            block([-2.0, 6.0, 2.75], [0.8, 2.0, 0.25], SW_STEPS),
            block([-2.0, 6.5, 3.25], [0.8, 1.5, 0.25], SW_STEPS),
            block([-2.0, 7.0, 3.75], [0.8, 1.0, 0.25], SW_STEPS),
            block([-2.0, 7.5, 4.25], [0.8, 0.5, 0.25], SW_STEPS),
        ],
    ),
    (
        165,
        "Dock",
        "Wood",
        &[
            // Sunk so its deck stands a step over the ground (or the water), its pylons below.
            part(
                "World\\Azeroth\\RedRidge\\PassiveDoodads\\Docks\\RedRidgeDocks01.m2",
                [0.0, 0.0, DOCK_SINK],
                0.0,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
                [-8.0, -7.0, DOCK_DECK],
                0.0,
            ),
            part(
                "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
                [-8.5, -4.8, DOCK_DECK],
                0.4,
            ),
        ],
        &[],
    ),
    (
        166,
        "Gazebo",
        "Wood",
        &[part(
            "World\\Generic\\NightElf\\Passive Doodads\\Gazibos\\KalidarGazibo.m2",
            [-0.05, -0.3, 0.0],
            0.0,
        )],
        &[],
    ),
    (
        167,
        "Orcish longhouse",
        "Wood",
        &[
            part(
                "World\\Generic\\Orc\\Passive Doodads\\Tents\\orctent01.m2",
                [-6.5, 0.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Orc\\Passive Doodads\\Tents\\orctent01.m2",
                [6.5, 0.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Orc\\Passive Doodads\\FirePits\\SmallFirePit01.m2",
                [0.0, 0.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2",
                [14.8, -5.0, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2",
                [14.8, 5.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
    (
        168,
        "Moonwell shrine",
        "Stone",
        &[
            part(
                "World\\Generic\\NIGHTELF\\PASSIVE DOODADS\\MoonwellLight\\MoonWellLight.m2",
                [0.85, 0.15, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\NightElf\\Passive Doodads\\Lanterns\\NightElfLantern01.m2",
                [6.0, -4.5, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\NightElf\\Passive Doodads\\Lanterns\\NightElfLantern01.m2",
                [6.0, 4.5, 0.0],
                0.0,
            ),
            part(
                "World\\Generic\\NightElf\\Passive Doodads\\StoneRunes\\KalidarStoneRune01.m2",
                [-7.0, 0.0, 0.0],
                0.0,
            ),
        ],
        &[],
    ),
];

const Q: f32 = std::f32::consts::FRAC_PI_2;
const WESTFALL_FENCE: &str =
    "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallFence\\WestfallFence.m2";
/// The city wall's masonry, its walk's boards and its steps (the client draws them in these).
const SW_STONE: &str = "Dungeons\\Textures\\Walls\\MM_STRMWND_WALL_05.blp";
const SW_BOARDS: &str = "Dungeons\\Textures\\Floor\\MM_STRMWND_INT_FLOOR_01.blp";
const SW_STEPS: &str = "Dungeons\\Textures\\Brick\\MM_GREYBRICK_01.blp";
/// How far the dock is sunk, and so where its deck stands.
const DOCK_SINK: f32 = -5.3;
const DOCK_DECK: f32 = 0.25;

/// Head height: a surface over this is overhead; under it, something is a step or in the way.
const HEAD: f32 = 2.6;
/// Body height over a cell's lowest surface: anything between these blocks the cell.
const BODY_LO: f32 = 0.45;
const BODY_HI: f32 = 2.2;
/// A surface this near the ground is the ground.
const GROUND: f32 = 0.3;
/// At most this many boxes a structure: past it, the cells grow.
const MAX_BOXES: usize = 48;
/// The second catalogue's bound: its inn and farm are big, and the server allows 64.
const MAX_BOXES_LARGE: usize = 64;

type Tri = [[f32; 3]; 3];

fn load(chain: &Chain, parts: &[Part]) -> Vec<Tri> {
    let mut tris = Vec::new();
    for p in parts {
        let bytes = chain
            .read(p.model)
            .unwrap_or_else(|e| panic!("{}: {e}", p.model));
        let dir = p.model.rsplit_once('\\').map_or("", |(d, _)| d);
        let subs = parse_m2_render_submeshes(&bytes, dir, &[]).expect("parse");
        let (s, c) = p.yaw.sin_cos();
        let place = |v: [f32; 3]| {
            [
                v[0] * c - v[1] * s + p.at[0],
                v[0] * s + v[1] * c + p.at[1],
                v[2] + p.at[2],
            ]
        };
        for sub in &subs {
            if sub.billboard.is_some()
                || !matches!(sub.blend, ModelBlend::Opaque | ModelBlend::AlphaTest)
            {
                continue;
            }
            for t in sub.indices.chunks_exact(3) {
                let v = |i: u32| place(sub.positions[i as usize]);
                tris.push([v(t[0]), v(t[1]), v(t[2])]);
            }
        }
    }
    tris
}

/// A block's twelve triangles.
fn block_tris(b: &Block) -> Vec<Tri> {
    let corner = |i: usize| {
        let s = |bit: usize| if i & bit != 0 { 1.0 } else { -1.0 };
        [
            b.center[0] + b.half[0] * s(1),
            b.center[1] + b.half[1] * s(2),
            b.center[2] + b.half[2] * s(4),
        ]
    };
    let faces = [
        [0, 1, 3, 2],
        [4, 6, 7, 5],
        [0, 4, 5, 1],
        [2, 3, 7, 6],
        [0, 2, 6, 4],
        [1, 5, 7, 3],
    ];
    let mut tris = Vec::new();
    for f in faces {
        tris.push([corner(f[0]), corner(f[1]), corner(f[2])]);
        tris.push([corner(f[0]), corner(f[2]), corner(f[3])]);
    }
    // A block is solid, not a shell: planes through it every half yard, so every cell under it
    // finds it at body height.
    let (c, h) = (b.center, b.half);
    let mut x = c[0] - h[0];
    while x <= c[0] + h[0] {
        let p = |y: f32, z: f32| [x, y, z];
        let (y0, y1, z0, z1) = (c[1] - h[1], c[1] + h[1], c[2] - h[2], c[2] + h[2]);
        tris.push([p(y0, z0), p(y1, z0), p(y1, z1)]);
        tris.push([p(y0, z0), p(y1, z1), p(y0, z1)]);
        x += 0.5;
    }
    let mut y = c[1] - h[1];
    while y <= c[1] + h[1] {
        let p = |x: f32, z: f32| [x, y, z];
        let (x0, x1, z0, z1) = (c[0] - h[0], c[0] + h[0], c[2] - h[2], c[2] + h[2]);
        tris.push([p(x0, z0), p(x1, z0), p(x1, z1)]);
        tris.push([p(x0, z0), p(x1, z1), p(x0, z1)]);
        y += 0.5;
    }
    tris
}

fn load_all(chain: &Chain, parts: &[Part], blocks: &[Block]) -> Vec<Tri> {
    let mut tris = load(chain, parts);
    for b in blocks {
        tris.extend(block_tris(b));
    }
    tris
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Cell {
    Empty,
    /// Solid from the ground to the top.
    Column(f32),
    /// A step up to a surface under head height.
    Step(f32),
    /// Overhead: a slab from and to.
    Slab(f32, f32),
}

struct Grid {
    x0: f32,
    y0: f32,
    size: f32,
    nx: usize,
    ny: usize,
    cells: Vec<Cell>,
}

fn grid(tris: &[Tri], size: f32) -> Grid {
    let (mut lo, mut hi) = ([f32::MAX; 2], [f32::MIN; 2]);
    for t in tris {
        for v in t {
            for k in 0..2 {
                lo[k] = lo[k].min(v[k]);
                hi[k] = hi[k].max(v[k]);
            }
        }
    }
    let nx = (((hi[0] - lo[0]) / size).ceil() as usize).max(1);
    let ny = (((hi[1] - lo[1]) / size).ceil() as usize).max(1);
    let mut zs: Vec<Vec<f32>> = vec![Vec::new(); nx * ny];
    // Sample every triangle finely enough that each cell it crosses gets samples.
    let step = size / 3.0;
    for t in tris {
        let e1 = (0..3).map(|k| t[1][k] - t[0][k]).collect::<Vec<_>>();
        let e2 = (0..3).map(|k| t[2][k] - t[0][k]).collect::<Vec<_>>();
        let len = |e: &[f32]| (e[0] * e[0] + e[1] * e[1] + e[2] * e[2]).sqrt();
        let n = ((len(&e1).max(len(&e2)) / step).ceil() as usize).clamp(1, 400);
        for i in 0..=n {
            for j in 0..=(n - i) {
                let (a, b) = (i as f32 / n as f32, j as f32 / n as f32);
                let p: Vec<f32> = (0..3).map(|k| t[0][k] + e1[k] * a + e2[k] * b).collect();
                let cx = (((p[0] - lo[0]) / size) as usize).min(nx - 1);
                let cy = (((p[1] - lo[1]) / size) as usize).min(ny - 1);
                zs[cy * nx + cx].push(p[2]);
            }
        }
    }
    let cells = zs
        .into_iter()
        .map(|mut z| {
            if z.is_empty() {
                return Cell::Empty;
            }
            z.sort_by(f32::total_cmp);
            let low = z[0].max(0.0);
            let body = z.iter().any(|&h| h > low + BODY_LO && h < low + BODY_HI);
            if low >= HEAD {
                // Overhead: the slab of the lowest sheet (a roof's, an awning's).
                let sheet = z
                    .iter()
                    .copied()
                    .take_while(|&h| h < z[0] + 1.2)
                    .fold(low, f32::max);
                Cell::Slab(low, sheet.max(low + 0.2))
            } else if body {
                // Solid to its top: a wall runs up into its roof, a tower's shaft up its height.
                Cell::Column(z[z.len() - 1].max(low + 0.3))
            } else if low > GROUND {
                Cell::Step(low)
            } else {
                Cell::Empty
            }
        })
        .collect();
    Grid {
        x0: lo[0],
        y0: lo[1],
        size,
        nx,
        ny,
        cells,
    }
}

/// A box: centre and half sizes (piece frame).
#[derive(Clone, Copy, Debug)]
struct Bx {
    c: [f32; 3],
    h: [f32; 3],
}

fn q(z: f32) -> i32 {
    (z * 4.0).round() as i32
}

/// Merge like cells into boxes, greedily: rows along x, then down y.
fn boxes(g: &Grid, step: f32) -> Vec<Bx> {
    // Walls' tops and slabs to `step` (a big building merges into fewer boxes); a deck or a stair
    // keeps the quarter yard a foot can tell.
    let coarse = |z: f32| q((z / step).round() * step);
    let key = |c: Cell| -> Option<(i32, i32)> {
        match c {
            Cell::Empty => None,
            Cell::Column(top) => Some((0, coarse(top))),
            Cell::Step(top) => Some((0, q(top))),
            Cell::Slab(a, b) => Some((coarse(a), coarse(b))),
        }
    };
    let mut used = vec![false; g.cells.len()];
    let mut out = Vec::new();
    for y in 0..g.ny {
        for x in 0..g.nx {
            let i = y * g.nx + x;
            let Some(k) = (!used[i]).then(|| key(g.cells[i])).flatten() else {
                continue;
            };
            let mut w = 1;
            while x + w < g.nx && !used[i + w] && key(g.cells[i + w]) == Some(k) {
                w += 1;
            }
            let mut h = 1;
            'grow: while y + h < g.ny {
                for dx in 0..w {
                    let j = (y + h) * g.nx + x + dx;
                    if used[j] || key(g.cells[j]) != Some(k) {
                        break 'grow;
                    }
                }
                h += 1;
            }
            for dy in 0..h {
                for dx in 0..w {
                    used[(y + dy) * g.nx + x + dx] = true;
                }
            }
            let (z0, z1) = (k.0 as f32 / 4.0, k.1 as f32 / 4.0);
            let (xa, ya) = (g.x0 + x as f32 * g.size, g.y0 + y as f32 * g.size);
            let (xb, yb) = (xa + w as f32 * g.size, ya + h as f32 * g.size);
            out.push(Bx {
                c: [(xa + xb) / 2.0, (ya + yb) / 2.0, (z0 + z1) / 2.0],
                h: [(xb - xa) / 2.0, (yb - ya) / 2.0, ((z1 - z0) / 2.0).max(0.1)],
            });
        }
    }
    out
}

fn ascii(g: &Grid) -> String {
    let mut s = String::new();
    for y in (0..g.ny).rev() {
        for x in 0..g.nx {
            s.push(match g.cells[y * g.nx + x] {
                Cell::Empty => '.',
                Cell::Column(_) => '#',
                Cell::Step(_) => '=',
                Cell::Slab(..) => '^',
            });
        }
        s.push('\n');
    }
    s
}

// â”€â”€ PNG, uncompressed (stored deflate blocks), for checking the fit by eye â”€â”€

fn crc32(data: &[u8]) -> u32 {
    let mut c = 0xFFFF_FFFFu32;
    for &b in data {
        c ^= u32::from(b);
        for _ in 0..8 {
            c = if c & 1 != 0 {
                0xEDB8_8320 ^ (c >> 1)
            } else {
                c >> 1
            };
        }
    }
    !c
}

fn png(path: &Path, w: usize, h: usize, rgb: &[u8]) {
    let mut raw = Vec::with_capacity((w * 3 + 1) * h);
    for y in 0..h {
        raw.push(0);
        raw.extend_from_slice(&rgb[y * w * 3..(y + 1) * w * 3]);
    }
    let mut z = vec![0x78, 0x01];
    for (i, chunk) in raw.chunks(65_535).enumerate() {
        let last = (i + 1) * 65_535 >= raw.len();
        z.push(u8::from(last));
        let n = chunk.len() as u16;
        z.extend_from_slice(&n.to_le_bytes());
        z.extend_from_slice(&(!n).to_le_bytes());
        z.extend_from_slice(chunk);
    }
    let (mut a, mut b) = (1u32, 0u32);
    for &x in &raw {
        a = (a + u32::from(x)) % 65_521;
        b = (b + a) % 65_521;
    }
    z.extend_from_slice(&((b << 16) | a).to_be_bytes());
    let mut out = vec![0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
    let mut chunk = |kind: &[u8], data: &[u8]| {
        out.extend_from_slice(&(data.len() as u32).to_be_bytes());
        let mut c = kind.to_vec();
        c.extend_from_slice(data);
        out.extend_from_slice(&c);
        out.extend_from_slice(&crc32(&c).to_be_bytes());
    };
    let mut ihdr = Vec::new();
    ihdr.extend_from_slice(&(w as u32).to_be_bytes());
    ihdr.extend_from_slice(&(h as u32).to_be_bytes());
    ihdr.extend_from_slice(&[8, 2, 0, 0, 0]);
    chunk(b"IHDR", &ihdr);
    chunk(b"IDAT", &z);
    chunk(b"IEND", &[]);
    std::fs::write(path, out).expect("write png");
}

/// The structure from above (left: its height, light high) and from the front (right: looking
/// along +x), each with its boxes' outlines in red.
fn draw(path: &Path, tris: &[Tri], bx: &[Bx]) {
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    for t in tris {
        for v in t {
            for k in 0..3 {
                lo[k] = lo[k].min(v[k]);
                hi[k] = hi[k].max(v[k]);
            }
        }
    }
    let span = (0..3).map(|k| hi[k] - lo[k]).fold(1.0f32, f32::max);
    let s = 300.0 / span;
    let (w, h) = (640usize, 320usize);
    let mut img = vec![30u8; w * h * 3];
    let mut depth = vec![f32::MIN; w * h];
    let mut put = |x: f32, y: f32, d: f32, c: [u8; 3], img: &mut Vec<u8>| {
        let (xi, yi) = (x as isize, y as isize);
        if xi < 0 || yi < 0 || xi as usize >= w || yi as usize >= h {
            return;
        }
        let i = yi as usize * w + xi as usize;
        if d >= depth[i] {
            depth[i] = d;
            img[i * 3..i * 3 + 3].copy_from_slice(&c);
        }
    };
    for view in 0..2 {
        // Top: screen x from -y (left of the frame on the right), screen y from x. Front: screen
        // x from -y, screen y from z.
        let to = |v: [f32; 3]| -> (f32, f32, f32) {
            let sx = 10.0 + (hi[1] - v[1]) * s + view as f32 * 320.0;
            if view == 0 {
                (sx, 10.0 + (hi[0] - v[0]) * s, v[2])
            } else {
                (sx, 10.0 + (hi[2] - v[2]) * s, -v[0])
            }
        };
        for t in tris {
            let p: Vec<(f32, f32, f32)> = t.iter().map(|v| to(*v)).collect();
            let n = 2
                + ((p[0].0 - p[1].0).abs() + (p[0].1 - p[1].1).abs())
                    .max((p[0].0 - p[2].0).abs() + (p[0].1 - p[2].1).abs())
                    as usize;
            for i in 0..=n {
                for j in 0..=(n - i) {
                    let (a, b) = (i as f32 / n as f32, j as f32 / n as f32);
                    let x = p[0].0 + (p[1].0 - p[0].0) * a + (p[2].0 - p[0].0) * b;
                    let y = p[0].1 + (p[1].1 - p[0].1) * a + (p[2].1 - p[0].1) * b;
                    let d = p[0].2 + (p[1].2 - p[0].2) * a + (p[2].2 - p[0].2) * b;
                    let shade = if view == 0 {
                        ((d - lo[2]) / (hi[2] - lo[2]).max(0.1) * 180.0 + 60.0) as u8
                    } else {
                        ((d + hi[0]) / (hi[0] - lo[0]).max(0.1) * 180.0 + 60.0) as u8
                    };
                    put(x, y, d, [shade, shade, shade], &mut img);
                }
            }
        }
        for b in bx {
            let corners = [
                [b.c[0] - b.h[0], b.c[1] - b.h[1], b.c[2] - b.h[2]],
                [b.c[0] + b.h[0], b.c[1] + b.h[1], b.c[2] + b.h[2]],
            ];
            let (x0, y0, _) = to(corners[0]);
            let (x1, y1, _) = to(corners[1]);
            let (xa, xb) = (x0.min(x1), x0.max(x1));
            let (ya, yb) = (y0.min(y1), y0.max(y1));
            let mut x = xa;
            while x <= xb {
                put(x, ya, f32::MAX, [220, 40, 40], &mut img);
                put(x, yb, f32::MAX, [220, 40, 40], &mut img);
                x += 1.0;
            }
            let mut y = ya;
            while y <= yb {
                put(xa, y, f32::MAX, [220, 40, 40], &mut img);
                put(xb, y, f32::MAX, [220, 40, 40], &mut img);
                y += 1.0;
            }
        }
    }
    png(path, w, h, &img);
}

fn main() {
    let data = std::env::args().nth(1).expect("WoW Data dir");
    let out_dir = std::env::args().nth(2).map(PathBuf::from);
    let chain = Chain::open(Path::new(&data)).expect("open the patch chain");
    let mut rust = String::new();
    let mut cpp = String::new();
    for (id, name, material, parts, blocks) in STRUCTURES {
        let mut tris = load_all(&chain, parts, blocks);
        // A model that floats over its origin (some doodads are placed with their origin under
        // their foot) comes down onto it; one sunk into the ground stays as it was set.
        let min_z = tris.iter().flatten().map(|v| v[2]).fold(f32::MAX, f32::min);
        let lift = if min_z > 0.02 { -min_z } else { 0.0 };
        for v in tris.iter_mut().flatten() {
            v[2] += lift;
        }
        // The finest cells that fit the bound: the first catalogue's at quarter-yard heights (as
        // their tables were made), the second's trying half and whole yards before cells grow.
        let (steps, most): (&[f32], usize) = if *id < 156 {
            (&[0.25], MAX_BOXES)
        } else {
            (&[0.25, 0.5, 1.0], MAX_BOXES_LARGE)
        };
        let mut size = 1.0;
        let (g, bx) = 'fit: loop {
            let g = grid(&tris, size);
            for &step in steps {
                let bx = boxes(&g, step);
                if bx.len() <= most || size > 4.0 {
                    break 'fit (g, bx);
                }
            }
            size *= 1.25;
        };
        eprintln!(
            "{id} {name}: {} boxes at {size:.2} yd cells, lowest z {min_z:.2}\n{}",
            bx.len(),
            ascii(&g)
        );
        if let Some(dir) = &out_dir {
            draw(&dir.join(format!("structure_{id}.png")), &tris, &bx);
        }
        let fmt = |b: &Bx, f: &dyn Fn(f32, f32, f32, f32, f32, f32) -> String| {
            f(b.c[0], b.c[1], b.c[2], b.h[0], b.h[1], b.h[2])
        };
        rust.push_str(&format!(
            "    Structure {{\n        id: {id},\n        name: \"{name}\",\n        stone: {},\n        parts: &[\n",
            *material == "Stone"
        ));
        for p in parts.iter() {
            rust.push_str(&format!(
                "            StructurePart {{\n                model: \"{}\",\n                at: [{:.2}, {:.2}, {:.2}],\n                yaw: {:.4},\n            }},\n",
                p.model.replace('\\', "\\\\"),
                p.at[0],
                p.at[1],
                p.at[2] + lift,
                p.yaw
            ));
        }
        rust.push_str("        ],\n        blocks: &[\n");
        for k in blocks.iter() {
            rust.push_str(&format!(
                "            StructureBlock {{\n                center: [{:.2}, {:.2}, {:.2}],\n                half: [{:.2}, {:.2}, {:.2}],\n                texture: \"{}\",\n            }},\n",
                k.center[0],
                k.center[1],
                k.center[2] + lift,
                k.half[0],
                k.half[1],
                k.half[2],
                k.texture.replace('\\', "\\\\"),
            ));
        }
        rust.push_str("        ],\n        boxes: &[\n");
        cpp.push_str(&format!(
            "        {{{id}, \"{name}\", {}, {{",
            *material == "Stone"
        ));
        for b in &bx {
            rust.push_str(&fmt(b, &|a, b2, c, d, e, f| {
                format!("            b([{a:.2}, {b2:.2}, {c:.2}], [{d:.2}, {e:.2}, {f:.2}]),\n")
            }));
            cpp.push_str(&fmt(b, &|a, b2, c, d, e, f| {
                format!("{{{{{a:.2}f, {b2:.2}f, {c:.2}f}}, {{{d:.2}f, {e:.2}f, {f:.2}f}}}}, ")
            }));
        }
        rust.push_str("        ],\n    },\n");
        cpp.push_str("}},\n");
    }
    println!("// Rust\n{rust}\n// C++\n{cpp}");
}
