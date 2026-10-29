//! How the built pieces look: five builders' styles drawn in WoW's own world textures (read from
//! the player's install, never copied), each piece a few textured boxes with its trim — corner
//! posts and a top plate on a timber wall, a stone sill under it, a frame round a door or a
//! window, a fascia and a ridge on a roof, treads on stairs, a plinth and coping on masonry, and
//! the orcs' spikes along a wall's top. The collision stays the piece's own boxes
//! (crate::build::pieces); what is drawn here is only drawn.
//!
//! A style is a type of its own on the wire (the server's `Build.h` Styles): `STYLE_BASE +
//! (style - 1) * STYLED_SLOTS + slot`, the same piece to the rules as its plain one.

use super::pieces::{BoxDef, PieceModel, PieceType, def};

/// The first styled type, the styles (the plain one first) and the pieces each restyles: the
/// server's `kStyleBase`, `kStyleCount`, `kStyledSlots`.
pub const STYLE_BASE: u8 = 20;
pub const STYLE_COUNT: u8 = 5;
pub const STYLED_SLOTS: u8 = 16;

/// The pieces a style redraws, in slot order (the server's `StyledPieces`).
pub const STYLED: [PieceType; STYLED_SLOTS as usize] = [
    PieceType::WoodFloor,
    PieceType::WoodWall,
    PieceType::WoodHalfWall,
    PieceType::WoodDoorway,
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
    PieceType::Bedroll,
    PieceType::WoodDoor,
];

impl PieceType {
    /// Its style: 0 for a plain piece, a prop or a structure.
    pub fn style(self) -> u8 {
        if self.0 < STYLE_BASE || self.0 >= STYLE_BASE + (STYLE_COUNT - 1) * STYLED_SLOTS {
            return 0;
        }
        1 + (self.0 - STYLE_BASE) / STYLED_SLOTS
    }

    /// The plain piece it is to the rules (itself when plain).
    pub fn base(self) -> PieceType {
        match self.style() {
            0 => self,
            _ => STYLED[usize::from((self.0 - STYLE_BASE) % STYLED_SLOTS)],
        }
    }

    /// This plain piece in `style` (itself for 0); None for a piece no style redraws, a style
    /// past the last, or a piece already styled.
    pub fn styled(self, style: u8) -> Option<PieceType> {
        if style >= STYLE_COUNT || self.style() != 0 {
            return None;
        }
        let slot = STYLED.iter().position(|t| *t == self)? as u8;
        Some(if style == 0 {
            self
        } else {
            PieceType(STYLE_BASE + (style - 1) * STYLED_SLOTS + slot)
        })
    }
}

/// A texture of the install (a path in the MPQs) and how many yards it spans before repeating.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Tex {
    pub path: &'static str,
    pub yards: f32,
}

const fn tex(path: &'static str, yards: f32) -> Tex {
    Tex { path, yards }
}

/// Where a texture loads from.
pub fn mpq_url(path: &str) -> String {
    format!("mpq://{}", path.replace('\\', "/"))
}

/// A builder's style: the textures of each part and the models of its camp.
pub struct Style {
    /// The catalogue's button.
    pub name: &'static str,
    /// What it is, under the buttons.
    pub blurb: &'static str,
    /// A timber wall's face, a wood floor's boards, a roof's top and its underside.
    pub wall: Tex,
    pub floor: Tex,
    pub roof: Tex,
    pub under: Tex,
    /// Posts, plates, frames and stringers.
    pub beam: Tex,
    /// The stone a timber wall stands on, a window's sill.
    pub sill: Tex,
    /// A door's leaf, drawn whole.
    pub door: Tex,
    pub stairs: Tex,
    /// Masonry: a stone wall's face, a stone floor's top, plinths and coping.
    pub stone_wall: Tex,
    pub stone_floor: Tex,
    pub stone_trim: Tex,
    /// Sandbags.
    pub cloth: Tex,
    pub campfire: &'static str,
    pub bedroll: &'static str,
    /// A torch's and a campfire's flame.
    pub flame: [f32; 3],
    /// The orcs' sharpened stakes along a timber wall's top.
    pub spikes: bool,
}

macro_rules! dt {
    ($p:literal, $y:expr) => {
        tex(concat!("Dungeons\\Textures\\", $p), $y)
    };
}

/// The styles, the plain one first (the server's `StyleName` order).
pub static STYLES: [Style; STYLE_COUNT as usize] = [
    Style {
        name: "Elwynn",
        blurb: "Elwynn timber and plaster, a thatched roof, fieldstone",
        wall: dt!("Walls\\MM_STRMWND_WALL_02.blp", 3.0),
        floor: dt!("Floor\\MM_STRMWND_INT_FLOOR_01.blp", 3.0),
        roof: dt!("Roof\\MM_STRAWROOF_01.blp", 2.0),
        under: dt!("Wood\\ELWYNNPLANKBOTTOM02.blp", 3.0),
        beam: dt!("Wood\\BM_TMBRMW_PLAINWOOD01.blp", 1.5),
        sill: dt!("Trim\\MM_WESTFALL_STONEBASE_01.blp", 1.5),
        door: dt!("Door\\MM_BROWNDOOR_01.blp", 1.0),
        stairs: dt!("Wood\\ELWYNNPLANKBOTTOM02.blp", 2.0),
        stone_wall: dt!("Brick\\MM_WESTFALL_BRICK_01.blp", 2.0),
        stone_floor: dt!("Floor\\BM_LDRN_COBBLESTONE01.blp", 3.0),
        stone_trim: dt!("Trim\\MM_WESTFALL_STONEBASE_01.blp", 1.5),
        cloth: tex(
            "World\\Azeroth\\Karazahn\\PassiveDoodads\\GrainSacks\\GrainSack.blp",
            3.0,
        ),
        campfire: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Campfire\\ElwynnCampfire.m2",
        bedroll: "World\\Azeroth\\Westfall\\PassiveDoodads\\Furniture\\westfallbed01.m2",
        flame: [1.0, 0.75, 0.4],
        spikes: false,
    },
    Style {
        name: "Stormwind",
        blurb: "Stormwind's keep: dressed white stone, dark timber, blue slate",
        wall: dt!("Walls\\MM_STRMWND_WWALL_01.blp", 3.0),
        floor: dt!("Wood\\BM_PVP_WOODPLANKS01.blp", 3.0),
        roof: dt!("Roof\\MM_STRMWND_ROOF_01.blp", 2.0),
        under: dt!("Wood\\LAKEBRIDGEWOOD01.blp", 3.0),
        beam: dt!("Wood\\LAKEBRIDGEWOOD01.blp", 1.5),
        sill: dt!("Brick\\MM_GREYBRICK_01.blp", 1.5),
        door: dt!("Door\\BM_LDRN_DOOR01.blp", 1.0),
        stairs: dt!("Wood\\BM_PVP_WOODPLANKS01.blp", 2.0),
        stone_wall: dt!("Walls\\MM_STRMWND_WALL_05.blp", 3.0),
        stone_floor: dt!("Floor\\BM_LDRN_STONEFLOOR01.blp", 3.0),
        stone_trim: dt!("Brick\\MM_GREYBRICK_01.blp", 1.5),
        cloth: tex(
            "World\\Azeroth\\Karazahn\\PassiveDoodads\\GrainSacks\\GrainSack.blp",
            3.0,
        ),
        campfire: "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2",
        bedroll: "World\\Dungeon\\GoldshireInn\\InnBed\\InnBed.m2",
        flame: [1.0, 0.85, 0.55],
        spikes: false,
    },
    Style {
        name: "Ironforge",
        blurb: "Ironforge's red brick, iron-banded timber, a sod roof",
        wall: dt!("Walls\\MM_WESTFALL_WALL_02.blp", 3.0),
        floor: dt!("Wood\\ELWYNNPILLARWOOD05.blp", 3.0),
        roof: dt!("Roof\\MM_DWRF_ROOF_01.blp", 3.0),
        under: dt!("Wood\\ELWYNNPILLARWOOD05.blp", 3.0),
        beam: dt!("Trim\\MM_IRNFG_TRIM_02.blp", 1.5),
        sill: dt!("Trim\\MM_IRNFG_TRIM_01.blp", 1.5),
        door: dt!("Door\\MM_IRNFG_DOOR_01.blp", 1.0),
        stairs: dt!("Floor\\MM_DWRF_FLOOR_02.blp", 2.0),
        stone_wall: dt!("Brick\\MM_REDBRICK_03X.blp", 1.5),
        stone_floor: dt!("Floor\\MM_IRNFG_FLOOR_01.blp", 3.0),
        stone_trim: dt!("Trim\\MM_IRNFG_TRIM_01.blp", 1.5),
        cloth: tex(
            "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcSleepMats\\ClothMatt04.blp",
            2.0,
        ),
        campfire: "World\\Generic\\Dwarf\\Passive Doodads\\Braziers\\DwarvenBrazier01.m2",
        bedroll: "World\\Azeroth\\Westfall\\PassiveDoodads\\Furniture\\westfallbed01.m2",
        flame: [1.0, 0.7, 0.35],
        spikes: false,
    },
    Style {
        name: "Orcish",
        blurb: "Orgrimmar's lashed logs and sharpened stakes, hide roofs, red rock",
        wall: dt!("Floor\\MM_OGRMR_FLOOR_02.blp", 3.0),
        floor: dt!("Floor\\BM_THUNDER_WDFLOOR_01.blp", 3.0),
        roof: dt!("Roof\\MM_OGRMR_ROOF_03.blp", 3.0),
        under: dt!("Wood\\BM_THUNDER_WOOD_01.blp", 3.0),
        beam: dt!("Wood\\BM_THUNDER_LOGS_01.blp", 1.5),
        sill: dt!("Floor\\MM_ORC_AFLOOR_01.blp", 2.0),
        door: dt!("Walls\\MM_ORC_WALL_01.blp", 1.0),
        stairs: dt!("Floor\\BM_THUNDER_WDFLOOR_01.blp", 2.0),
        stone_wall: dt!("Walls\\MM_OGRMR_WALL_04.blp", 3.0),
        stone_floor: dt!("Floor\\MM_ORC_AFLOOR_01.blp", 3.0),
        stone_trim: dt!("Wood\\BM_THUNDER_LOGS_01.blp", 1.5),
        cloth: tex(
            "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcSleepMats\\ClothMatt04.blp",
            2.0,
        ),
        campfire: "World\\Generic\\Orc\\Passive Doodads\\FirePits\\SmallFirePit01.m2",
        bedroll: "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcSleepMats\\OrcSleepMat01.m2",
        flame: [1.0, 0.6, 0.25],
        spikes: true,
    },
    Style {
        name: "Night elf",
        blurb: "Darnassus's pale moonwood, leaf roofs, silvered moonstone",
        wall: dt!("Walls\\MM_NE_WALL_01.blp", 3.0),
        floor: dt!("Floor\\MM_NE_FLOOR_02.blp", 3.0),
        roof: dt!("Roof\\MM_NE_ROOF_01.blp", 2.0),
        under: dt!("Walls\\MM_NE_INTWALL_01.blp", 3.0),
        beam: dt!("Walls\\MM_NE_INTWALL_01.blp", 1.5),
        sill: dt!("Trim\\MM_NE_TRIM_02.blp", 1.5),
        door: dt!("Walls\\MM_DRNSS_WALL_01.blp", 1.0),
        stairs: dt!("Floor\\MM_NE_FLOOR_02.blp", 2.0),
        stone_wall: dt!("Floor\\MM_NE_DS_FLOOR_01.blp", 2.0),
        stone_floor: dt!("Floor\\MM_DRNSS_FLOOR_04.blp", 3.0),
        stone_trim: dt!("Trim\\MM_NE_DS_TRIM_01.blp", 1.5),
        cloth: tex(
            "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcSleepMats\\ClothMatt04.blp",
            2.0,
        ),
        campfire: "World\\Azeroth\\Elwynn\\PassiveDoodads\\Campfire\\ElwynnCampfire_blue.m2",
        bedroll: "World\\Generic\\NightElf\\Passive Doodads\\Beds\\elfbed02.m2",
        flame: [0.55, 0.7, 1.0],
        spikes: false,
    },
];

/// A piece's style (the plain one for a prop or a structure).
pub fn style_of(kind: PieceType) -> &'static Style {
    &STYLES[usize::from(kind.style()).min(STYLES.len() - 1)]
}

/// Every texture the styles use, each once.
pub fn all_textures() -> Vec<Tex> {
    let mut out: Vec<Tex> = Vec::new();
    for s in &STYLES {
        for t in [
            s.wall,
            s.floor,
            s.roof,
            s.under,
            s.beam,
            s.sill,
            s.door,
            s.stairs,
            s.stone_wall,
            s.stone_floor,
            s.stone_trim,
            s.cloth,
        ] {
            if !out.iter().any(|o| o.path == t.path) {
                out.push(t);
            }
        }
    }
    out
}

/// How a box's faces take their texture's coordinates.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Uv {
    /// A wall's panel: across the wall from its left end, down from `top` (yards over the
    /// piece's origin), so the pieces of a doorway or a window show one wall's face and a half
    /// wall the lower half of it.
    Panel(f32),
    /// From each face's top edge down, across from its left.
    Fit,
    /// Along the grain: the texture's height runs the long way of each face (a post up, a plate
    /// or a stringer along).
    Grain,
    /// The whole texture once across each face (a door's leaf).
    Whole,
}

/// A drawn box: where it stands in the piece's frame, its sides', top's and bottom's textures,
/// and how they map.
#[derive(Clone, Copy, Debug)]
pub struct DrawBox {
    pub b: BoxDef,
    pub side: Tex,
    pub top: Tex,
    pub bottom: Tex,
    pub uv: Uv,
}

fn bx(center: [f32; 3], half: [f32; 3]) -> BoxDef {
    BoxDef {
        center,
        half,
        pitch: 0.0,
    }
}

fn all(b: BoxDef, t: Tex, uv: Uv) -> DrawBox {
    DrawBox {
        b,
        side: t,
        top: t,
        bottom: t,
        uv,
    }
}

/// A box tipped by `pitch` about the piece's y through `pivot`, `local` from it along its own
/// axes (a roof's fascia, a stair's stringer).
fn pitched(pivot: [f32; 3], local: [f32; 3], half: [f32; 3], pitch: f32) -> BoxDef {
    let (s, c) = pitch.sin_cos();
    BoxDef {
        center: [
            pivot[0] + local[0] * c - local[2] * s,
            pivot[1] + local[1],
            pivot[2] + local[0] * s + local[2] * c,
        ],
        half,
        pitch,
    }
}

/// A timber wall's frame: corner posts, a top plate at `height`, a stone sill along its foot
/// between the posts (`sill`), and the orcs' stakes along its top.
fn frame(out: &mut Vec<DrawBox>, s: &Style, height: f32, sill: bool) {
    for y in [-1.42, 1.42] {
        out.push(all(
            bx(
                [0.0, y, height / 2.0 + 0.01],
                [0.19, 0.08, height / 2.0 + 0.01],
            ),
            s.beam,
            Uv::Grain,
        ));
    }
    out.push(all(
        bx([0.0, 0.0, height - 0.06], [0.19, 1.34, 0.07]),
        s.beam,
        Uv::Grain,
    ));
    if sill {
        out.push(all(
            bx([0.0, 0.0, 0.12], [0.2, 1.34, 0.12]),
            s.sill,
            Uv::Fit,
        ));
    }
    spikes(out, s, height);
}

fn spikes(out: &mut Vec<DrawBox>, s: &Style, height: f32) {
    if !s.spikes {
        return;
    }
    for (i, y) in [-1.2f32, -0.6, 0.0, 0.6, 1.2].into_iter().enumerate() {
        let lean = if i % 2 == 0 { 0.22 } else { -0.22 };
        out.push(all(
            BoxDef {
                center: [0.0, y, height + 0.3],
                half: [0.05, 0.05, 0.36],
                pitch: lean,
            },
            s.beam,
            Uv::Grain,
        ));
    }
}

/// The face of a wall-like box in the style's timber (its top and bottom in the posts' wood).
fn wall_face(b: BoxDef, s: &Style) -> DrawBox {
    DrawBox {
        b,
        side: s.wall,
        top: s.beam,
        bottom: s.beam,
        uv: Uv::Panel(3.0),
    }
}

/// The doorway's jambs and lintel with a frame round the opening.
fn doorway(out: &mut Vec<DrawBox>, s: &Style) {
    for b in &def(PieceType::WoodDoorway).boxes[..3] {
        out.push(wall_face(*b, s));
    }
    for y in [-0.82, 0.82] {
        out.push(all(
            bx([0.0, y, 1.25], [0.2, 0.05, 1.25]),
            s.beam,
            Uv::Grain,
        ));
    }
    out.push(all(
        bx([0.0, 0.0, 2.53], [0.2, 0.87, 0.05]),
        s.beam,
        Uv::Grain,
    ));
    for y in [-1.07, 1.07] {
        out.push(all(bx([0.0, y, 0.12], [0.2, 0.27, 0.12]), s.sill, Uv::Fit));
    }
    frame(out, s, 3.0, false);
}

/// What is drawn of a piece (its own boxes as it stands, `open` for a door), in its style.
pub fn draw_boxes(kind: PieceType, open: bool) -> Vec<DrawBox> {
    let s = style_of(kind);
    let base = kind.base();
    let mut out = Vec::new();
    match base {
        PieceType::WoodWall => {
            out.push(wall_face(def(base).boxes[0], s));
            frame(&mut out, s, 3.0, true);
        }
        PieceType::WoodHalfWall => {
            out.push(wall_face(def(base).boxes[0], s));
            for y in [-1.42, 1.42] {
                out.push(all(
                    bx([0.0, y, 0.76], [0.19, 0.08, 0.76]),
                    s.beam,
                    Uv::Grain,
                ));
            }
            out.push(all(
                bx([0.0, 0.0, 1.5], [0.21, 1.5, 0.06]),
                s.beam,
                Uv::Grain,
            ));
            out.push(all(
                bx([0.0, 0.0, 0.12], [0.2, 1.34, 0.12]),
                s.sill,
                Uv::Fit,
            ));
            spikes(&mut out, s, 1.56);
        }
        PieceType::WoodDoorway => doorway(&mut out, s),
        PieceType::WoodDoor => {
            doorway(&mut out, s);
            let leaf = super::pieces::boxes_of(base, open)[3];
            out.push(DrawBox {
                b: leaf,
                side: s.door,
                top: s.beam,
                bottom: s.beam,
                uv: Uv::Whole,
            });
        }
        PieceType::WoodWindow => {
            for b in def(base).boxes {
                out.push(wall_face(*b, s));
            }
            // The sill, the frame round the light, and its cross.
            out.push(all(
                bx([0.0, 0.0, 1.21], [0.27, 0.8, 0.05]),
                s.sill,
                Uv::Fit,
            ));
            out.push(all(
                bx([0.0, 0.0, 2.17], [0.19, 0.72, 0.04]),
                s.beam,
                Uv::Grain,
            ));
            for y in [-0.68, 0.68] {
                out.push(all(bx([0.0, y, 1.7], [0.19, 0.04, 0.5]), s.beam, Uv::Grain));
            }
            out.push(all(
                bx([0.0, 0.0, 1.7], [0.04, 0.03, 0.46]),
                s.beam,
                Uv::Grain,
            ));
            out.push(all(
                bx([0.0, 0.0, 1.7], [0.04, 0.64, 0.03]),
                s.beam,
                Uv::Grain,
            ));
            frame(&mut out, s, 3.0, true);
        }
        PieceType::WoodRoof => {
            let slab = def(base).boxes[0];
            let pivot = slab.center;
            // A little thicker drawn than solid, its top the roof, its underside boards.
            out.push(DrawBox {
                b: BoxDef {
                    half: [slab.half[0], slab.half[1], 0.13],
                    ..slab
                },
                side: s.beam,
                top: s.roof,
                bottom: s.under,
                uv: Uv::Fit,
            });
            // The fascia at the eave and the ridge board at the top.
            for x in [-2.08, 2.08] {
                out.push(all(
                    pitched(pivot, [x, 0.0, 0.02], [0.06, 1.5, 0.19], slab.pitch),
                    s.beam,
                    Uv::Grain,
                ));
            }
        }
        PieceType::WoodStairs => {
            let slab = def(base).boxes[0];
            let pivot = slab.center;
            // The underside, two stringers, and ten treads a third of a yard apart.
            out.push(DrawBox {
                b: pitched(pivot, [0.0, 0.0, -0.06], [2.13, 0.9, 0.05], slab.pitch),
                side: s.beam,
                top: s.under,
                bottom: s.under,
                uv: Uv::Fit,
            });
            for y in [-0.95, 0.95] {
                out.push(all(
                    pitched(pivot, [0.0, y, 0.0], [2.13, 0.06, 0.2], slab.pitch),
                    s.beam,
                    Uv::Grain,
                ));
            }
            for i in 0..10 {
                let x = 0.3 * i as f32;
                out.push(all(
                    bx(
                        [x + 0.15, 0.0, 0.3 * (i + 1) as f32 - 0.05],
                        [0.16, 0.9, 0.05],
                    ),
                    s.stairs,
                    Uv::Grain,
                ));
            }
        }
        PieceType::WoodBeam => {
            out.push(all(def(base).boxes[0], s.beam, Uv::Grain));
            out.push(all(bx([0.0, 0.0, 0.1], [0.2, 0.2, 0.1]), s.sill, Uv::Fit));
            out.push(all(
                bx([0.0, 0.0, 2.93], [0.2, 0.2, 0.07]),
                s.beam,
                Uv::Grain,
            ));
        }
        PieceType::WoodFloor => out.push(DrawBox {
            b: def(base).boxes[0],
            side: s.beam,
            top: s.floor,
            bottom: s.beam,
            uv: Uv::Fit,
        }),
        PieceType::StoneFloor => out.push(DrawBox {
            b: def(base).boxes[0],
            side: s.stone_trim,
            top: s.stone_floor,
            bottom: s.stone_trim,
            uv: Uv::Fit,
        }),
        PieceType::StoneWall | PieceType::StoneHalfWall => {
            let b = def(base).boxes[0];
            let height = b.half[2] * 2.0;
            out.push(DrawBox {
                b,
                side: s.stone_wall,
                top: s.stone_trim,
                bottom: s.stone_trim,
                uv: Uv::Panel(3.0),
            });
            // A plinth along its foot and coping along its top.
            out.push(all(
                bx([0.0, 0.0, 0.18], [0.31, 1.5, 0.18]),
                s.stone_trim,
                Uv::Fit,
            ));
            out.push(all(
                bx([0.0, 0.0, height], [0.31, 1.5, 0.06]),
                s.stone_trim,
                Uv::Fit,
            ));
        }
        PieceType::Torch => {
            out.push(all(def(base).boxes[0], s.beam, Uv::Grain));
            out.push(all(
                bx([0.0, 0.0, 1.72], [0.1, 0.1, 0.08]),
                s.stone_trim,
                Uv::Fit,
            ));
        }
        // Anything else draws as its own boxes in the style's timber.
        _ => {
            for b in super::pieces::boxes_of(base, open) {
                out.push(all(b, s.beam, Uv::Fit));
            }
        }
    }
    out
}

/// The model a piece is drawn as, its style's own for a campfire or a bedroll.
pub fn model_of(kind: PieceType) -> Option<PieceModel> {
    match kind.base() {
        PieceType::Campfire => Some(PieceModel::Wow(style_of(kind).campfire)),
        PieceType::Bedroll => Some(PieceModel::Wow(style_of(kind).bedroll)),
        _ => def(kind).model,
    }
}

/// A preview of a piece for the catalogue: its style's face for a box piece (the wall's for a
/// wall, the roof's for a roof...); None for a model.
pub fn swatch(kind: PieceType) -> Option<Tex> {
    let s = style_of(kind);
    Some(match kind.base() {
        PieceType::WoodFloor => s.floor,
        PieceType::WoodWall
        | PieceType::WoodHalfWall
        | PieceType::WoodDoorway
        | PieceType::WoodWindow => s.wall,
        PieceType::WoodDoor => s.door,
        PieceType::WoodRoof => s.roof,
        PieceType::WoodStairs => s.stairs,
        PieceType::WoodBeam | PieceType::Torch => s.beam,
        PieceType::StoneFloor => s.stone_floor,
        PieceType::StoneWall | PieceType::StoneHalfWall => s.stone_wall,
        PieceType::Sandbags => s.cloth,
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn styles_number_as_the_server_numbers_them() {
        // Build.h: kStyleBase 20, five styles, sixteen slots; the wood wall is slot 1.
        assert_eq!(PieceType::WoodWall.styled(0), Some(PieceType::WoodWall));
        assert_eq!(PieceType::WoodWall.styled(1), Some(PieceType(21)));
        assert_eq!(
            PieceType::WoodDoor.styled(4),
            Some(PieceType(20 + 3 * 16 + 15))
        );
        for style in 1..STYLE_COUNT {
            for base in STYLED {
                let t = base.styled(style).unwrap();
                assert_eq!((t.base(), t.style()), (base, style));
                assert!(t.0 < super::super::pieces::PROP_BASE);
                assert_eq!(t.styled(1), None);
                assert_eq!(PieceType::from_u8(t.0), Some(t));
            }
        }
        assert_eq!(PieceType::SentryGun.styled(1), None);
        assert_eq!(PieceType(140).style(), 0);
        assert_eq!(PieceType::WoodWall.styled(STYLE_COUNT), None);
        assert_eq!(STYLES[3].name, "Orcish");
    }

    #[test]
    fn a_styled_piece_is_its_plain_one_to_the_rules() {
        let wall = PieceType::WoodWall.styled(3).unwrap();
        assert_eq!(def(wall).name, def(PieceType::WoodWall).name);
        assert_eq!(def(wall).boxes.len(), 1);
        let door = PieceType::WoodDoor.styled(2).unwrap();
        assert_eq!(super::super::pieces::boxes_of(door, true)[3].center[0], 0.8);
    }

    #[test]
    fn every_piece_draws_its_trim_and_the_orcs_their_stakes() {
        for style in 0..STYLE_COUNT {
            for base in STYLED {
                let t = base.styled(style).unwrap();
                let drawn = draw_boxes(t, false);
                if model_of(t).is_none() && base != PieceType::Sandbags {
                    assert!(!drawn.is_empty(), "{t:?}");
                }
                for d in &drawn {
                    assert!(d.b.half.iter().all(|h| *h > 0.0), "{t:?}");
                    assert!(!d.side.path.is_empty() && d.side.yards > 0.0);
                }
            }
        }
        let plain = draw_boxes(PieceType::WoodWall, false).len();
        let orc = draw_boxes(PieceType::WoodWall.styled(3).unwrap(), false).len();
        assert!(plain >= 5 && orc == plain + 5);
        // A wall's face is its style's, its posts the style's timber.
        let ne = draw_boxes(PieceType::WoodWall.styled(4).unwrap(), false);
        assert_eq!(ne[0].side, STYLES[4].wall);
        assert!(ne[1..].iter().any(|d| d.side == STYLES[4].beam));
        // A door's leaf swings with it.
        let shut = draw_boxes(PieceType::WoodDoor, false);
        let open = draw_boxes(PieceType::WoodDoor, true);
        assert!(open.last().unwrap().b.center[0] > shut.last().unwrap().b.center[0]);
        assert!(model_of(PieceType::Campfire.styled(3).unwrap()).is_some());
        assert_ne!(
            model_of(PieceType::Bedroll.styled(4).unwrap()),
            model_of(PieceType::Bedroll)
        );
        assert!(swatch(PieceType::WoodRoof.styled(1).unwrap()) == Some(STYLES[1].roof));
        assert_eq!(swatch(PieceType::Campfire), None);
        assert!(all_textures().len() >= 30);
        assert_eq!(
            mpq_url("Dungeons\\Textures\\Roof\\MM_STRAWROOF_01.blp"),
            "mpq://Dungeons/Textures/Roof/MM_STRAWROOF_01.blp"
        );
    }
}
