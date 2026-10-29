//! Wrath's Dalaran on the 1.12 Eastern Kingdoms map, where classic Dalaran stands in the Alterac
//! Mountains: the classic violet dome is removed and the WotLK city (`ND_Dalaran.wmo`, version 17
//! like 1.12's, root and 91 groups, its doodads down-ported by `super::m2conv`) is placed over
//! the crater, level with the land: the ground is laid as a plateau just under the city's rim
//! ledge and streets (its floating-island rock fully buried) and eased back into Alterac and the
//! Lordamere shore over a 60-yard ring, so you walk up onto the rim and into the streets.
//!
//! What 1.12 places there: one M2, `dalarandome.mdx`, listed in tiles 30_31 and 31_31 (the crater
//! is plain terrain; the ruins around it are farmhouse WMOs and doodads). Every doodad and WMO
//! placement standing inside the city's footprint goes too, or it would poke through the streets.
//! The city keeps its WotLK heading (-219 degrees); its outer rim ledge (the lowest up-facing
//! surface round the island, local Z about -10) sits on the crater floor (41.9), so the streets
//! (local 0 to 8) stand clear over the ground and the arrival floor, in the citadel, at 71.
//!
//! The groups ship as Wrath wrote them except for their lighting: [`group_for_112`] drops the
//! baked MOCV of the exterior groups, which the 1.12 path would multiply into the sky light (the
//! city drew black). The reshaped ground keeps the stock normals where it is not moved and tilts
//! them by the change where it is, read across chunk and tile borders ([`Ground`]), and loses
//! the stock baked shadow where it moved, so the ring meets the mountains without a seam.
//!
//! The patched tiles are written into the patch layer: benilla, the reference client and the
//! server's extractors read them from there. Server side: `34_dalaran.sql` (the portals), `37_dalaran_city.sql` (the gate and the people) and the
//! map data re-extraction `aw.ps1` runs when the patch layer changes.

use anyhow::{Context, Result};
use benilla_formats::Chain;

use super::adt::Adt;
use super::import::{convert_model, m2_path};
use super::m2conv;
use super::source::Source;

/// The city's WMO (in the 3.3.5 chain; the same path in the patch layer).
pub const ROOT: &str = "World\\wmo\\Northrend\\Dalaran\\ND_Dalaran.wmo";
/// The classic dome the city replaces.
pub const DOME: &str = "dalarandome";
/// The 1.12 tiles the city and its terrain blend can reach (`Azeroth_<x>_<y>`).
pub const TILES: [(u32, u32); 9] = [
    (30, 30),
    (30, 31),
    (30, 32),
    (31, 30),
    (31, 31),
    (31, 32),
    (32, 30),
    (32, 31),
    (32, 32),
];
/// The ADT frame's origin offset (32 tiles of 533⅓ yd).
const ORIGIN: f32 = 17066.666;
/// The classic dome's centre, world X and Y (its MDDF position).
pub const CENTRE: [f32; 2] = [ORIGIN - 16793.0, ORIGIN - 16697.5];
/// The city's heading, as WotLK places it (MODF degrees).
pub const HEADING: f32 = -219.0;
/// Where the city's Northrend arrival floor (local Z 18.98, in the citadel) stands, world Z. The
/// rim ledge round the island (local about -10, the lowest walkable surface) then stands on the
/// crater floor (41.9) and the streets (local 0 to 25) at or a little over the land round the
/// city; the ground is laid up to the rim as a plateau ([`Footprint::terrain`]). (The first splice
/// put the arrival itself at the crater floor, which sank the streets into Alterac and dug a pit.)
pub const STREET_Z: f32 = 71.0;
/// The arrival floor of the 3.3.5 `Teleport: Dalaran` spell in the WMO's own frame (map 571
/// `5807.75, 588.347, 660.939, 1.663` through its Northrend placement), `(x, y, z, facing)`.
pub const ARRIVAL_LOCAL: [f32; 4] = [36.15, -71.87, 18.98, 2.344];
/// The terrain blend ring around the footprint, yards: the slope from the rim back to the land.
pub const BLEND: f32 = 60.0;
/// How far past the ring the farm ruins are cleared, yards.
const CLEAR_PAST: f32 = 15.0;

/// Whether a 1.12 doodad is nature (trees, bushes, grass, rocks), which may stay on the ring.
pub fn is_nature(path: &str) -> bool {
    let p = path.to_ascii_lowercase();
    ["tree", "bush", "shrub", "grass", "fern", "plant", "flower", "rock", "boulder", "stump", "log", "mushroom", "weed"]
        .iter()
        .any(|k| p.rsplit('\\').next().unwrap_or(&p).contains(k))
}

/// How far under the city's lowest walkable surface the plateau stands, yards: under the rim
/// ledge a step a unit takes without noticing.
const GROUND_UNDER: f32 = 0.5;
/// The highest surface still counted as the city's ground floor, world Z: over it are roofs,
/// spires and the landing that juts out over the edge, which must not pull the ground up.
const FLOOR_MAX: f32 = STREET_Z + 15.0;
/// The lowest city surface the ground is laid under, world Z: the rim ledge, the streets and
/// everything above them (up-facing surfaces only, [`up_facing`]); the rock underside, its flanks
/// and the Underbelly below are buried.
pub const FLOOR_MIN: f32 = STREET_Z - 33.0;
/// The winding the city's floors are written in: +1 if an up-facing floor's `(b-a)x(c-a)` points
/// up.
const UP_SIGN: f32 = 1.0;
/// The footprint grid's cell, yards.
const CELL: f32 = 2.0;
/// The new placement's unique id, far above 1.12's.
const UNIQUE_ID: u32 = 0x00D4_1A00;

/// The WMO-local to world rotation: about Z by `heading - 180` (benilla_assets'
/// `placement_rotation` with no pitch or roll).
fn rot() -> (f32, f32) {
    let a = (HEADING - 180.0).to_radians();
    (a.cos(), a.sin())
}

/// The world translation of the city's origin: the local box centre over the dome's centre, the
/// arrival floor at [`STREET_Z`]. `bbox` is MOHD's.
pub fn translation(bbox: [f32; 6]) -> [f32; 3] {
    let (c, s) = rot();
    let (lx, ly) = ((bbox[0] + bbox[3]) / 2.0, (bbox[1] + bbox[4]) / 2.0);
    [
        CENTRE[0] - (c * lx - s * ly),
        CENTRE[1] - (s * lx + c * ly),
        STREET_Z - ARRIVAL_LOCAL[2],
    ]
}

/// A WMO-local point in the world.
pub fn to_world(t: [f32; 3], l: [f32; 3]) -> [f32; 3] {
    let (c, s) = rot();
    [t[0] + c * l[0] - s * l[1], t[1] + s * l[0] + c * l[1], t[2] + l[2]]
}

/// Where a portal lands, world `(x, y, z, facing)` on map 0.
pub fn arrival(bbox: [f32; 6]) -> [f32; 4] {
    let w = to_world(translation(bbox), [ARRIVAL_LOCAL[0], ARRIVAL_LOCAL[1], ARRIVAL_LOCAL[2]]);
    let f = (ARRIVAL_LOCAL[3] + (HEADING - 180.0).to_radians()).rem_euclid(std::f32::consts::TAU);
    [w[0], w[1], w[2], f]
}

/// The chunks of a WMO: `(tag as stored, data offset, size)`.
fn chunks(b: &[u8]) -> Vec<([u8; 4], usize, usize)> {
    let mut out = Vec::new();
    let mut o = 0;
    while o + 8 <= b.len() {
        let tag = [b[o], b[o + 1], b[o + 2], b[o + 3]];
        let n = u32::from_le_bytes([b[o + 4], b[o + 5], b[o + 6], b[o + 7]]) as usize;
        out.push((tag, o + 8, n));
        o += 8 + n;
    }
    out
}

pub fn chunk<'b>(b: &'b [u8], tag: &[u8; 4]) -> Option<&'b [u8]> {
    let rev = [tag[3], tag[2], tag[1], tag[0]];
    chunks(b)
        .into_iter()
        .find(|(t, _, _)| *t == rev)
        .and_then(|(_, o, n)| b.get(o..o + n))
}

fn strings(block: &[u8]) -> Vec<String> {
    block
        .split(|&c| c == 0)
        .filter(|s| !s.is_empty())
        .map(|s| String::from_utf8_lossy(s).into_owned())
        .collect()
}

pub fn mohd_bbox(root: &[u8]) -> Result<[f32; 6]> {
    let mohd = chunk(root, b"MOHD").context("no MOHD")?;
    let mut bbox = [0f32; 6];
    for (k, v) in bbox.iter_mut().enumerate() {
        *v = f32::from_le_bytes(mohd[0x24 + k * 4..0x28 + k * 4].try_into()?);
    }
    Ok(bbox)
}

/// The city's triangles in the world, from its groups' MOVT/MOVI.
pub fn world_triangles(groups: &[Vec<u8>], t: [f32; 3]) -> Vec<[[f32; 3]; 3]> {
    let mut tris = Vec::new();
    for g in groups {
        let Some(mogp) = chunk(g, b"MOGP") else { continue };
        let sub = &mogp[0x44.min(mogp.len())..];
        let (Some(v), Some(i)) = (chunk(sub, b"MOVT"), chunk(sub, b"MOVI")) else {
            continue;
        };
        let vert = |k: usize| -> [f32; 3] {
            let l = std::array::from_fn(|c| f32::from_le_bytes(v[k * 12 + c * 4..k * 12 + c * 4 + 4].try_into().unwrap()));
            to_world(t, l)
        };
        let nv = v.len() / 12;
        for tri in i.chunks_exact(6) {
            let idx = [0, 2, 4].map(|o| u16::from_le_bytes([tri[o], tri[o + 1]]) as usize);
            if idx.iter().all(|&k| k < nv) {
                tris.push(idx.map(vert));
            }
        }
    }
    tris
}

/// MOGP flag: the group is outdoors (lit by the sky).
const MOGP_EXTERIOR: u32 = 0x8;
/// MOGP flag: the group carries MOCV vertex colours.
const MOGP_HAS_MOCV: u32 = 0x4;
/// MOGP flag (Wrath): the group carries a second MOCV.
const MOGP_HAS_MOCV2: u32 = 0x0100_0000;

/// One Wrath group file made fit for the 1.12 lighting path: an exterior group's MOCV dropped.
///
/// No 1.12 exterior group carries MOCV (all 1279 stock exterior groups have none), and the 1.12
/// path multiplies an exterior batch's sky light by whatever MOCV it finds. Wrath's Dalaran is a
/// unified-render-path WMO (MOHD flags `0x2`) and bakes a dark MOCV into every one of its 23
/// lit exterior groups (mean luminance about 35 of 255, two thirds of the vertices under 20):
/// drawn the 1.12 way, the streets, walls and roofs come out nearly black. Without the MOCV they
/// are lit as every stock exterior is. Interior groups keep theirs: 1.12 interiors are lit from
/// it. Anything that is not a group (no MOGP) comes back unchanged.
pub fn group_for_112(group: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(group.len());
    for (tag, o, n) in chunks(group) {
        let body = &group[o..(o + n).min(group.len())];
        if &tag != b"PGOM" || body.len() < 0x44 {
            out.extend_from_slice(&group[o - 8..(o + n).min(group.len())]);
            continue;
        }
        let flags = u32::from_le_bytes(body[8..12].try_into().unwrap());
        let mut mogp = body[..0x44].to_vec();
        if flags & MOGP_EXTERIOR != 0 {
            let kept = flags & !(MOGP_HAS_MOCV | MOGP_HAS_MOCV2);
            mogp[8..12].copy_from_slice(&kept.to_le_bytes());
            let sub = &body[0x44..];
            for (st, so, sn) in chunks(sub) {
                if &st != b"VCOM" {
                    mogp.extend_from_slice(&sub[so - 8..(so + sn).min(sub.len())]);
                }
            }
        } else {
            mogp.extend_from_slice(&body[0x44..]);
        }
        out.extend_from_slice(&tag);
        out.extend((mogp.len() as u32).to_le_bytes());
        out.extend_from_slice(&mogp);
    }
    out
}

/// The city's footprint on a world grid: per cell its lowest surface at or over [`FLOOR_MIN`]
/// (the ground is kept under it), and the distance to the footprint with that height there for
/// the blend ring.
pub struct Footprint {
    x0: f32,
    y0: f32,
    w: usize,
    h: usize,
    /// The base Z where the city covers the cell.
    base: Vec<Option<f32>>,
    /// `(distance yd, nearest base Z)` for cells within [`BLEND`] outside it.
    ring: Vec<Option<(f32, f32)>>,
}

impl Footprint {
    pub fn new(tris: &[[[f32; 3]; 3]]) -> Self {
        let (mut x0, mut y0, mut x1, mut y1) = (f32::MAX, f32::MAX, f32::MIN, f32::MIN);
        for t in tris {
            for p in t {
                x0 = x0.min(p[0]);
                y0 = y0.min(p[1]);
                x1 = x1.max(p[0]);
                y1 = y1.max(p[1]);
            }
        }
        let pad = BLEND + 2.0 * CELL;
        let (x0, y0) = (x0 - pad, y0 - pad);
        let w = ((x1 + pad - x0) / CELL) as usize + 1;
        let h = ((y1 + pad - y0) / CELL) as usize + 1;
        let mut base: Vec<Option<f32>> = vec![None; w * h];
        for t in tris.iter().filter(|t| up_facing(t)) {
            let (tx0, tx1) = (t.iter().map(|p| p[0]).fold(f32::MAX, f32::min), t.iter().map(|p| p[0]).fold(f32::MIN, f32::max));
            let (ty0, ty1) = (t.iter().map(|p| p[1]).fold(f32::MAX, f32::min), t.iter().map(|p| p[1]).fold(f32::MIN, f32::max));
            let (cx0, cx1) = (((tx0 - x0) / CELL) as usize, ((tx1 - x0) / CELL) as usize);
            let (cy0, cy1) = (((ty0 - y0) / CELL) as usize, ((ty1 - y0) / CELL) as usize);
            for cy in cy0..=cy1.min(h - 1) {
                for cx in cx0..=cx1.min(w - 1) {
                    // The cell's centre in the triangle, or a sliver's vertex in the cell.
                    let (px, py) = (x0 + (cx as f32 + 0.5) * CELL, y0 + (cy as f32 + 0.5) * CELL);
                    let z = bary_z(t, px, py).or_else(|| {
                        (cx1 - cx0 < 1 || cy1 - cy0 < 1).then(|| t.iter().map(|p| p[2]).fold(f32::MAX, f32::min))
                    });
                    if let Some(z) = z.filter(|&z| (FLOOR_MIN..=FLOOR_MAX).contains(&z)) {
                        let c = &mut base[cy * w + cx];
                        *c = Some(c.map_or(z, |o| o.min(z)));
                    }
                }
            }
        }
        // Close single-cell gaps between triangles (a grid-sized crack is no hole in the rock).
        let src = base.clone();
        for cy in 1..h - 1 {
            for cx in 1..w - 1 {
                if src[cy * w + cx].is_none() {
                    let n: Vec<f32> = [(cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)]
                        .iter()
                        .filter_map(|&(a, b)| src[b * w + a])
                        .collect();
                    if n.len() >= 3 {
                        base[cy * w + cx] = n.iter().copied().reduce(f32::min);
                    }
                }
            }
        }
        // The plateau under each cell: the lowest base within four cells (8 yards, a wall's thickness), so the ground (a vertex
        // every 4 yards, the triangles between them sloping) never leans out of a raised court
        // over a lower floor beside it, the rim ledge under the city wall least of all.
        let src = base.clone();
        for cy in 0..h {
            for cx in 0..w {
                if src[cy * w + cx].is_some() {
                    let mut m = f32::MAX;
                    for ny in cy.saturating_sub(4)..(cy + 5).min(h) {
                        for nx in cx.saturating_sub(4)..(cx + 5).min(w) {
                            if let Some(z) = src[ny * w + nx] {
                                m = m.min(z);
                            }
                        }
                    }
                    base[cy * w + cx] = Some(m);
                }
            }
        }
        // The blend ring: a chamfer distance outward, carrying the nearest base Z.
        let mut ring: Vec<Option<(f32, f32)>> = base.iter().map(|b| b.map(|z| (0.0, z))).collect();
        for _ in 0..2 {
            for pass in 0..2 {
                let order: Box<dyn Iterator<Item = usize>> =
                    if pass == 0 { Box::new(0..w * h) } else { Box::new((0..w * h).rev()) };
                for i in order {
                    let (cx, cy) = (i % w, i / w);
                    for (dx, dy, dd) in [(-1i32, 0i32, 1.0f32), (1, 0, 1.0), (0, -1, 1.0), (0, 1, 1.0), (-1, -1, 1.414), (1, 1, 1.414), (-1, 1, 1.414), (1, -1, 1.414)] {
                        let (nx, ny) = (cx as i32 + dx, cy as i32 + dy);
                        if nx < 0 || ny < 0 || nx >= w as i32 || ny >= h as i32 {
                            continue;
                        }
                        if let Some((d, z)) = ring[ny as usize * w + nx as usize] {
                            let nd = d + dd * CELL;
                            if nd <= BLEND && ring[i].is_none_or(|(od, _)| nd < od) {
                                ring[i] = Some((nd, z));
                            }
                        }
                    }
                }
            }
        }
        Self { x0, y0, w, h, base, ring }
    }

    fn cell(&self, x: f32, y: f32) -> Option<usize> {
        let (cx, cy) = ((x - self.x0) / CELL, (y - self.y0) / CELL);
        (cx >= 0.0 && cy >= 0.0 && (cx as usize) < self.w && (cy as usize) < self.h)
            .then(|| cy as usize * self.w + cx as usize)
    }

    /// Whether a world point lies in the city, its ring, or within [`CLEAR_PAST`] beyond the ring.
    pub fn near(&self, x: f32, y: f32) -> bool {
        [(0.0, 0.0), (CLEAR_PAST, 0.0), (-CLEAR_PAST, 0.0), (0.0, CLEAR_PAST), (0.0, -CLEAR_PAST)]
            .iter()
            .any(|(dx, dy)| self.cell(x + dx, y + dy).is_some_and(|i| self.base[i].is_some() || self.ring[i].is_some()))
    }

    /// The city's base Z over a world point, if it covers it.
    pub fn base(&self, x: f32, y: f32) -> Option<f32> {
        self.cell(x, y).and_then(|i| self.base[i])
    }

    /// The terrain height at a world point: a plateau [`GROUND_UNDER`] under the city's lowest
    /// walkable surface where the city covers it (raised or lowered: the rock is buried, nothing
    /// pokes through), eased back to `orig` across the ring (a smoothstep over [`BLEND`] yards
    /// from the rim's height, so the land meets the rim ledge and slopes gently away), `orig`
    /// beyond it.
    pub fn terrain(&self, x: f32, y: f32, orig: f32) -> f32 {
        let Some(i) = self.cell(x, y) else { return orig };
        match (self.base[i], self.ring[i]) {
            (Some(z), _) => z - GROUND_UNDER,
            (None, Some((d, z))) => {
                let rim = z - GROUND_UNDER;
                let t = (d / BLEND).clamp(0.0, 1.0);
                let t = t * t * (3.0 - 2.0 * t);
                rim + (orig - rim) * t
            }
            _ => orig,
        }
    }

    /// The world box `(x0, y0, x1, y1)` the city and its ring cover.
    pub fn bounds(&self) -> [f32; 4] {
        [self.x0, self.y0, self.x0 + self.w as f32 * CELL, self.y0 + self.h as f32 * CELL]
    }
}

/// A triangle that faces up (a floor, a roof, a step): its normal within 60 degrees of +Z.
/// Walls and the rock's underside and flanks do not cap the ground; a city sitting on the crater
/// floor buries them.
pub fn up_facing(t: &[[f32; 3]; 3]) -> bool {
    let (u, v) = ([t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]], [t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]]);
    let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
    l > 1e-9 && UP_SIGN * n[2] / l > 0.5
}

fn bary_z(t: &[[f32; 3]; 3], x: f32, y: f32) -> Option<f32> {
    let [a, b, c] = t;
    let d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    if d.abs() < 1e-9 {
        return None;
    }
    let u = ((x - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (y - a[1])) / d;
    let v = ((b[0] - a[0]) * (y - a[1]) - (x - a[0]) * (b[1] - a[1])) / d;
    (u >= -1e-4 && v >= -1e-4 && u + v <= 1.0 + 1e-4).then(|| a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]))
}

/// The floors (surface Z) over a world point among `tris`.
pub fn floors(tris: &[[[f32; 3]; 3]], x: f32, y: f32) -> Vec<f32> {
    tris.iter().filter_map(|t| bary_z(t, x, y)).collect()
}

/// Whether a unit can stand at `(x, y)` on a city floor within 6 yards of `z`: the floor Z, if
/// it is there, nothing hangs within 4 yards over it (no wall, beam or ceiling through the unit)
/// and the floor goes on, as flat, a yard round it. The check the city's spawns are held to.
pub fn standing_spot(tris: &[[[f32; 3]; 3]], x: f32, y: f32, z: f32) -> Option<f32> {
    let fl = floors(tris, x, y);
    let f = fl.iter().copied().filter(|f| (f - z).abs() < 6.0).reduce(f32::max)?;
    let clear = |fl: &[f32]| !fl.iter().any(|g| *g > f + 0.3 && *g < f + 4.0);
    if !clear(&fl) {
        return None;
    }
    for (dx, dy) in [(1.2, 0.0), (-1.2, 0.0), (0.0, 1.2), (0.0, -1.2), (0.9, 0.9), (-0.9, -0.9), (0.9, -0.9), (-0.9, 0.9)] {
        let f2 = floors(tris, x + dx, y + dy);
        if !f2.iter().any(|g| (g - f).abs() < 0.6) || !clear(&f2) {
            return None;
        }
    }
    Some(f)
}

/// An MDDF/MODF position (ADT frame: `ORIGIN - Y`, `Z`, `ORIGIN - X`) as world X, Y, Z.
fn placement_world(rec: &[u8]) -> [f32; 3] {
    let f = |o: usize| f32::from_le_bytes(rec[o..o + 4].try_into().unwrap());
    [ORIGIN - f(16), ORIGIN - f(8), f(12)]
}

/// A terrain vertex's spacing, yards (a chunk is 8 of them).
const STEP: f32 = 33.333_332 / 8.0;

/// Vertex `k` (stride-17 MCVT order) of a chunk at `pos`, world X and Y.
fn vertex_xy(pos: [f32; 3], k: usize) -> (f32, f32) {
    let (r, c) = (k / 17, k % 17);
    if c < 9 {
        (pos[0] - r as f32 * STEP, pos[1] - c as f32 * STEP)
    } else {
        (pos[0] - (r as f32 + 0.5) * STEP, pos[1] - ((c - 9) as f32 + 0.5) * STEP)
    }
}

/// The stock terrain around the city across every tile it reaches: each vertex's height, by its
/// place on the half-step lattice (outer and inner vertices share it), so a vertex's neighbours
/// a step away are found across chunk and tile borders.
#[derive(Default)]
pub struct Ground {
    z: std::collections::HashMap<(i32, i32), f32>,
    /// The stock MCNR bytes there (the first chunk's, where two share the vertex).
    n: std::collections::HashMap<(i32, i32), [i8; 3]>,
}

impl Ground {
    fn key(x: f32, y: f32) -> (i32, i32) {
        ((x / (STEP / 2.0)).round() as i32, (y / (STEP / 2.0)).round() as i32)
    }

    /// Add a stock tile's vertices.
    pub fn add(&mut self, adt: &Adt) {
        for i in adt.mcnk() {
            let (pos, h) = adt.heights(i);
            let n = adt.normals(i);
            for (k, v) in h.iter().enumerate() {
                let (x, y) = vertex_xy(pos, k);
                self.z.insert(Self::key(x, y), pos[2] + v);
                if let Some(n) = &n {
                    self.n.entry(Self::key(x, y)).or_insert(n[k]);
                }
            }
        }
    }

    /// The stock height of the vertex at a world point, if one is there.
    pub fn at(&self, x: f32, y: f32) -> Option<f32> {
        self.z.get(&Self::key(x, y)).copied()
    }

    /// The stock ground height at any world point, bilinear over the outer vertices around it.
    pub fn height(&self, x: f32, y: f32) -> Option<f32> {
        let (fi, fj) = ((ORIGIN - x) / STEP, (ORIGIN - y) / STEP);
        let (i, j) = (fi.floor(), fj.floor());
        let (u, v) = (fi - i, fj - j);
        let at = |a: f32, b: f32| self.at(ORIGIN - a * STEP, ORIGIN - b * STEP);
        let (h00, h10, h01, h11) = (at(i, j)?, at(i + 1.0, j)?, at(i, j + 1.0)?, at(i + 1.0, j + 1.0)?);
        Some(h00 * (1.0 - u) * (1.0 - v) + h10 * u * (1.0 - v) + h01 * (1.0 - u) * v + h11 * u * v)
    }

    /// The stock normal bytes of the vertex at a world point.
    pub fn normal(&self, x: f32, y: f32) -> Option<[i8; 3]> {
        self.n.get(&Self::key(x, y)).copied()
    }
}

/// A stored MCNR normal as world `[X, Y, Z]` (disk bytes are WoW `[X, Y, Z]`, 127 = 1.0).
fn unpack_normal(n: [i8; 3]) -> [f32; 3] {
    n.map(|c| f32::from(c) / 127.0)
}

/// A normal as stored, scaled to `len` (1.0 = 127).
fn pack_normal(n: [f32; 3], len: f32) -> [i8; 3] {
    let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt().max(1e-6);
    n.map(|c| (c / l * len * 127.0).round().clamp(-127.0, 127.0) as i8)
}

/// One chunk after the city: its 145 heights (relative to `pos` Z) and normals as stored, or
/// `None` if untouched.
///
/// The normals keep the stock ones and tilt them only by the change the reshape made: the
/// gradient of `new - stock` is added to the stock normal's slope, its neighbours read across
/// chunk and tile borders from `ground`. Where the reshape changes nothing nearby the stored
/// bytes are kept as they are, so an edited chunk meets its stock neighbours with the normals
/// Blizzard baked (no lighting seam at the blend ring's edge or along any chunk border).
fn reshape(
    fp: &Footprint,
    ground: &Ground,
    pos: [f32; 3],
    h: &[f32],
    stock: Option<&[[i8; 3]]>,
) -> Option<(Vec<f32>, Vec<[i8; 3]>)> {
    let new: Vec<f32> = (0..145)
        .map(|k| {
            let (x, y) = vertex_xy(pos, k);
            fp.terrain(x, y, pos[2] + h[k]) - pos[2]
        })
        .collect();
    if new.iter().zip(h).all(|(a, b)| (a - b).abs() < 1e-3) {
        return None;
    }
    // The reshape's change at a vertex: from `ground`, else (off the loaded tiles) none.
    let delta = |x: f32, y: f32| -> f32 { ground.at(x, y).map_or(0.0, |o| fp.terrain(x, y, o) - o) };
    let normals = (0..145)
        .map(|k| {
            let (x, y) = vertex_xy(pos, k);
            let dgx = (delta(x + STEP, y) - delta(x - STEP, y)) / (2.0 * STEP);
            let dgy = (delta(x, y + STEP) - delta(x, y - STEP)) / (2.0 * STEP);
            let base = stock.map(|s| s[k]);
            // A change flatter than a thousandth of a slope keeps the stock bytes.
            if dgx.abs() < 1e-3 && dgy.abs() < 1e-3 {
                if let Some(b) = base {
                    return b;
                }
            }
            // The stock slope plus the change's. The stock normal is read from `ground`, so the
            // chunks (and tiles) sharing this vertex all start from the same bytes.
            let from = ground.normal(x, y).or(base).map(unpack_normal).filter(|n| n[2] > 0.05);
            let (gx, gy) = from.map_or((0.0, 0.0), |n| (-n[0] / n[2], -n[1] / n[2]));
            // Blizzard's normals are a little short of unit length (about 0.97); the new one keeps
            // the stock length so it shades as its stock neighbours do.
            let len = from.map_or(1.0, |n| (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt());
            pack_normal([-(gx + dgx), -(gy + dgy), 1.0], len)
        })
        .collect();
    Some((new, normals))
}

/// Clear the baked shadow where the reshape moved the ground: the stock MCSH was cast on the old
/// crater (and by the dome), and it would print dark patches on the new slope. Texel `(x, y)`
/// covers 1/64 of the chunk along Y (column) and X (row), as the vertices do.
fn clear_moved_shadow(adt: &mut Adt, idx: usize, h: &[f32], new: &[f32]) -> usize {
    let Some(sh) = adt.shadow_mut(idx) else { return 0 };
    let mut cleared = 0;
    for ty in 0..64usize {
        for tx in 0..64usize {
            let (r, c) = (((ty as f32 + 0.5) / 8.0).round() as usize, ((tx as f32 + 0.5) / 8.0).round() as usize);
            let k = r.min(8) * 17 + c.min(8);
            let (byte, bit) = (ty * 8 + tx / 8, tx % 8);
            if byte < sh.len() && (new[k] - h[k]).abs() > 0.5 && sh[byte] >> bit & 1 != 0 {
                sh[byte] &= !(1 << bit);
                cleared += 1;
            }
        }
    }
    cleared
}

/// The stock ground over [`TILES`] and the ring of tiles around them (a vertex on a tile's outer
/// edge reads its neighbours from the next tile).
pub fn stock_ground(vanilla: &Chain) -> Result<Ground> {
    let mut g = Ground::default();
    for x in 29..=33 {
        for y in 29..=33 {
            if let Ok(raw) = vanilla.read(&format!("World\\Maps\\Azeroth\\Azeroth_{x}_{y}.adt")) {
                g.add(&Adt::parse(&raw)?);
            }
        }
    }
    Ok(g)
}

/// What the Dalaran build makes.
pub struct Built {
    pub files: Vec<(String, Vec<u8>)>,
    pub report: Vec<String>,
}

/// Patch tile `adt` (raw 1.12 bytes): the dome and everything in the footprint removed, the city
/// placed, the terrain reshaped. `None` when the city does not touch it.
pub fn patch_tile(raw: &[u8], fp: &Footprint, ground: &Ground, modf: &[u8]) -> Result<Option<(Vec<u8>, usize, usize)>> {
    let mut adt = Adt::parse(raw)?;
    let [bx0, by0, bx1, by1] = fp.bounds();
    // The tile's own box, from its chunks' corners.
    let mcnk = adt.mcnk();
    let corners: Vec<[f32; 3]> = mcnk.iter().map(|&i| adt.heights(i).0).collect();
    let (tx1, ty1) = (corners.iter().map(|p| p[0]).fold(f32::MIN, f32::max), corners.iter().map(|p| p[1]).fold(f32::MIN, f32::max));
    let (tx0, ty0) = (tx1 - 533.333_3, ty1 - 533.333_3);
    if tx1 < bx0 || tx0 > bx1 || ty1 < by0 || ty0 > by1 {
        return Ok(None);
    }
    let inside = |p: [f32; 3]| -> bool {
        [(0.0, 0.0), (3.0, 0.0), (-3.0, 0.0), (0.0, 3.0), (0.0, -3.0)]
            .iter()
            .any(|(dx, dy)| fp.base(p[0] + dx, p[1] + dy).is_some())
    };
    // Within the ring (and a little past it) only nature stays: the old farm ruins, scaffolds,
    // fences and carts of the crater crowd the city wall and sit oddly on the new slope.
    let crowds = |p: [f32; 3], name: &str| -> bool { fp.near(p[0], p[1]) && !is_nature(name) };
    let doodad_names = adt.names(b"MMDX", b"MMID");
    let doodads = adt.records(b"MDDF", 36);
    let keep_d: Vec<bool> = doodads
        .iter()
        .map(|r| {
            let name = &doodad_names[u32::from_le_bytes(r[..4].try_into().unwrap()) as usize];
            !name.to_ascii_lowercase().contains(DOME) && !inside(placement_world(r)) && !crowds(placement_world(r), name)
        })
        .collect();
    let wmos = adt.records(b"MODF", 64);
    let keep_w: Vec<bool> = wmos.iter().map(|r| !inside(placement_world(r)) && !crowds(placement_world(r), "wmo")).collect();
    let removed = keep_d.iter().filter(|k| !**k).count() + keep_w.iter().filter(|k| !**k).count();
    // What stays rides the ground it stood on: a placement where the reshape lowered the ground
    // (the blend ring) moves down with it, or it would float there.
    let drop = |p: [f32; 3]| -> f32 { ground.height(p[0], p[1]).map_or(0.0, |o| fp.terrain(p[0], p[1], o) - o) };
    let mut mddf = adt.get(b"MDDF").to_vec();
    for (k, r) in mddf.chunks_exact_mut(36).enumerate() {
        let dz = drop(placement_world(r));
        if keep_d[k] && dz.abs() > 0.05 {
            let z = f32::from_le_bytes(r[12..16].try_into().unwrap()) + dz;
            r[12..16].copy_from_slice(&z.to_le_bytes());
        }
    }
    if !mddf.is_empty() {
        *adt.get_mut(b"MDDF") = mddf;
    }
    let mut modf_all = adt.get(b"MODF").to_vec();
    for (k, r) in modf_all.chunks_exact_mut(64).enumerate() {
        let dz = drop(placement_world(r));
        if keep_w[k] && dz.abs() > 0.05 {
            // Position Z and the extents' Z (ADT frame Y: bytes 12, 36, 48).
            for o in [12usize, 36, 48] {
                let z = f32::from_le_bytes(r[o..o + 4].try_into().unwrap()) + dz;
                r[o..o + 4].copy_from_slice(&z.to_le_bytes());
            }
        }
    }
    if !modf_all.is_empty() {
        *adt.get_mut(b"MODF") = modf_all;
    }
    adt.set_records(false, &keep_d, &[], &|_, _| false);
    // The city, referenced from every chunk under its box.
    let slot = adt.add_name(b"MWMO", b"MWID", ROOT);
    let mut rec = modf.to_vec();
    rec[..4].copy_from_slice(&slot.to_le_bytes());
    let ext = placement_world(modf); // unused placement origin; the box decides the chunks
    let _ = ext;
    let chunk_hits: Vec<bool> = corners
        .iter()
        .map(|p| {
            let (x1, y1) = (p[0], p[1]);
            let (x0, y0) = (x1 - 33.333_33, y1 - 33.333_33);
            !(x1 < bx0 + BLEND || x0 > bx1 - BLEND || y1 < by0 + BLEND || y0 > by1 - BLEND)
        })
        .collect();
    adt.set_records(true, &keep_w, &[rec], &|ci, _| chunk_hits[ci]);
    let mut reshaped = 0;
    for &i in &mcnk {
        let (pos, h) = adt.heights(i);
        let stock = adt.normals(i);
        if let Some((nh, nn)) = reshape(fp, ground, pos, &h, stock.as_deref()) {
            adt.set_heights(i, &nh, &[]);
            adt.set_normals_raw(i, &nn);
            clear_moved_shadow(&mut adt, i, &h, &nh);
            reshaped += 1;
        }
    }
    Ok(Some((adt.to_bytes(), removed, reshaped)))
}

/// The city's MODF record (name slot 0, filled in per tile).
pub fn modf_record(t: [f32; 3], tris: &[[[f32; 3]; 3]]) -> Vec<u8> {
    let mut lo = [f32::MAX; 3];
    let mut hi = [f32::MIN; 3];
    for p in tris.iter().flatten() {
        for k in 0..3 {
            lo[k] = lo[k].min(p[k]);
            hi[k] = hi[k].max(p[k]);
        }
    }
    let adt = |w: [f32; 3]| [ORIGIN - w[1], w[2], ORIGIN - w[0]];
    let (a, b) = (adt(lo), adt(hi));
    let mut r = Vec::with_capacity(64);
    r.extend(0u32.to_le_bytes());
    r.extend(UNIQUE_ID.to_le_bytes());
    for v in adt(t) {
        r.extend(v.to_le_bytes());
    }
    for v in [0.0f32, HEADING, 0.0] {
        r.extend(v.to_le_bytes());
    }
    for k in 0..3 {
        r.extend(a[k].min(b[k]).to_le_bytes());
    }
    for k in 0..3 {
        r.extend(a[k].max(b[k]).to_le_bytes());
    }
    r.extend([0u8; 8]); // flags, doodad set 0, name set 0, padding
    r
}

pub fn build(src: &Source, vanilla: &mut Chain) -> Result<Built> {
    let anims = benilla_formats::load_anim_data_catalog(vanilla)?;
    let mut files: Vec<(String, Vec<u8>)> = Vec::new();
    let mut have = std::collections::HashSet::new();
    let mut add = |files: &mut Vec<(String, Vec<u8>)>, name: String, bytes: Vec<u8>| {
        if have.insert(name.to_ascii_lowercase()) {
            files.push((name, bytes));
        }
    };

    let root = src.read(ROOT)?;
    let groups_n = u32::from_le_bytes(chunk(&root, b"MOHD").context("no MOHD")?[4..8].try_into()?) as usize;
    let bbox = mohd_bbox(&root)?;
    let stem = ROOT.strip_suffix(".wmo").unwrap_or(ROOT);
    let mut groups = Vec::new();
    for g in 0..groups_n {
        let name = format!("{stem}_{g:03}.wmo");
        let bytes = group_for_112(&src.read(&name)?);
        groups.push(bytes.clone());
        add(&mut files, name, bytes);
    }
    add(&mut files, ROOT.to_string(), root.clone());

    let mut missing = 0;
    for t in strings(chunk(&root, b"MOTX").unwrap_or(&[])) {
        if vanilla.contains(&t) {
            continue;
        }
        match src.read(&t) {
            Ok(b) => add(&mut files, t, b),
            Err(_) => missing += 1,
        }
    }
    let (mut converted, mut stock, mut failed) = (0, 0, Vec::new());
    for d in strings(chunk(&root, b"MODN").unwrap_or(&[])) {
        let path = m2_path(&d);
        if vanilla.contains(&path) {
            stock += 1;
            continue;
        }
        match convert_model(src, &path, &anims) {
            Ok(m2) => {
                converted += 1;
                let raw = src.read(&path)?;
                for f in m2conv::referenced_files(&raw).unwrap_or_default() {
                    if !vanilla.contains(&f)
                        && let Ok(b) = src.read(&f)
                    {
                        add(&mut files, f, b);
                    }
                }
                add(&mut files, path, m2);
            }
            Err(e) => failed.push(format!("  doodad {path}: {e:#}")),
        }
    }

    let t = translation(bbox);
    let tris = world_triangles(&groups, t);
    let fp = Footprint::new(&tris);
    let modf = modf_record(t, &tris);
    let ground = stock_ground(vanilla)?;
    let a = arrival(bbox);
    let mut report = vec![
        String::new(),
        format!("Dalaran: Wrath's city over classic Dalaran (map 0), origin {t:?}, heading {HEADING}"),
        format!("  arrival {:.2} {:.2} {:.2} facing {:.3}", a[0], a[1], a[2], a[3]),
        format!(
            "  {groups_n} groups, {converted} doodads converted, {stock} already in 1.12, {} failed, {missing} textures missing",
            failed.len()
        ),
    ];
    report.extend(failed);
    for (x, y) in TILES {
        let name = format!("World\\Maps\\Azeroth\\Azeroth_{x}_{y}.adt");
        let Ok(raw) = vanilla.read(&name) else { continue };
        if let Some((bytes, removed, reshaped)) = patch_tile(&raw, &fp, &ground, &modf)? {
            report.push(format!("  {name}: {removed} placements removed, {reshaped} chunks reshaped"));
            add(&mut files, name, bytes);
        }
    }
    Ok(Built { files, report })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_placement_maps_the_arrival_into_the_crater() {
        // MOHD's box as 3.3.5 ships it.
        let bbox = [-336.4, -379.5, -325.6, 277.0, 323.6, 550.4];
        let a = arrival(bbox);
        // Over classic Dalaran's crater, on the street level the city is lowered to.
        let d = ((a[0] - CENTRE[0]).powi(2) + (a[1] - CENTRE[1]).powi(2)).sqrt();
        assert!(d < 120.0, "{a:?} is {d} yd from the dome");
        assert!((a[2] - STREET_Z).abs() < 1e-3);
        // The WotLK facing survives the heading: the arrival faces as in Northrend (1.663).
        assert!((a[3] - 1.663).abs() < 0.01, "{a:?}");
        // The local box centre lands on the dome's centre.
        let c = to_world(translation(bbox), [(bbox[0] + bbox[3]) / 2.0, (bbox[1] + bbox[4]) / 2.0, 0.0]);
        assert!((c[0] - CENTRE[0]).abs() < 1e-2 && (c[1] - CENTRE[1]).abs() < 1e-2);
    }

    /// The whole job against the installs: the city loads through benilla's WMO reader, a floor
    /// stands under the arrival point, the patched tiles parse, no dome is left, the city is
    /// placed in the crater's tile, and the terrain under the arrival sits under the street.
    /// Skipped without the 3.3.5 or the 1.12 install.
    #[test]
    fn the_city_replaces_the_dome_in_alterac() {
        let (Some(src), Some(data)) = (Source::open_default(), super::super::vanilla_install()) else {
            eprintln!("no 3.3.5 or 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        let root = src.read(ROOT).unwrap();
        let n = u32::from_le_bytes(chunk(&root, b"MOHD").unwrap()[4..8].try_into().unwrap()) as usize;
        let stem = ROOT.strip_suffix(".wmo").unwrap();
        let groups: Vec<Vec<u8>> = (0..n).map(|g| src.read(&format!("{stem}_{g:03}.wmo")).unwrap()).collect();
        let bbox = mohd_bbox(&root).unwrap();
        let t = translation(bbox);
        let tris = world_triangles(&groups, t);
        let a = arrival(bbox);
        assert!(
            floors(&tris, a[0], a[1]).iter().any(|z| (z - a[2]).abs() < 0.5),
            "no floor under the arrival {a:?}"
        );
        let fp = Footprint::new(&tris);
        let modf = modf_record(t, &tris);
        let ground = stock_ground(&chain).unwrap();
        let mut patched = 0;
        for (x, y) in TILES {
            let name = format!("World\\Maps\\Azeroth\\Azeroth_{x}_{y}.adt");
            let Ok(raw) = chain.read(&name) else { continue };
            let Some((bytes, _, _)) = patch_tile(&raw, &fp, &ground, &modf).unwrap() else { continue };
            patched += 1;
            let benilla_adt::ParsedAdt::Root(r) =
                benilla_adt::parse_adt(&mut std::io::Cursor::new(bytes.as_slice())).unwrap();
            assert_eq!(r.mcnk_chunks.len(), 256, "{name}");
            let adt = Adt::parse(&bytes).unwrap();
            let names = adt.names(b"MMDX", b"MMID");
            for d in adt.records(b"MDDF", 36) {
                let nm = &names[u32::from_le_bytes(d[..4].try_into().unwrap()) as usize];
                assert!(!nm.to_ascii_lowercase().contains(DOME), "{name} still places {nm}");
            }
            assert!(r.wmos.iter().any(|w| w.eq_ignore_ascii_case(ROOT)), "{name}: no city");
            // The terrain under the arrival point (in its tile) is under the street.
            for (ci, c) in r.mcnk_chunks.iter().enumerate() {
                let p = c.header.position;
                if a[0] <= p[0] && a[0] > p[0] - 33.34 && a[1] <= p[1] && a[1] > p[1] - 33.34 {
                    let (_, h) = adt.heights(adt.mcnk()[ci]);
                    let top = h.iter().map(|v| p[2] + v).fold(f32::MIN, f32::max);
                    assert!(top < a[2], "{name}: terrain {top} over the street {}", a[2]);
                }
            }
        }
        assert!(patched >= 2, "{patched} tiles patched");
    }

    /// A minimal group file: MVER, then MOGP (`flags`) holding MOVT and MOCV.
    fn group(flags: u32) -> Vec<u8> {
        let sub = |tag: &[u8; 4], body: &[u8]| -> Vec<u8> {
            let mut v = vec![tag[3], tag[2], tag[1], tag[0]];
            v.extend((body.len() as u32).to_le_bytes());
            v.extend_from_slice(body);
            v
        };
        let mut mogp = vec![0u8; 0x44];
        mogp[8..12].copy_from_slice(&flags.to_le_bytes());
        mogp.extend(sub(b"MOVT", &[0u8; 12]));
        mogp.extend(sub(b"MOCV", &[10, 20, 30, 255]));
        let mut g = sub(b"MVER", &17u32.to_le_bytes());
        g.extend(sub(b"MOGP", &mogp));
        g
    }

    #[test]
    fn an_exterior_group_loses_its_baked_mocv_and_an_interior_keeps_it() {
        let ext = group_for_112(&group(0x8 | 0x4 | 0x0100_0000 | 0x8000_0000));
        let mogp = chunk(&ext, b"MOGP").unwrap();
        assert_eq!(u32::from_le_bytes(mogp[8..12].try_into().unwrap()), 0x8 | 0x8000_0000);
        assert!(chunk(&mogp[0x44..], b"MOCV").is_none());
        assert_eq!(chunk(&mogp[0x44..], b"MOVT").map(<[u8]>::len), Some(12));
        let int = group(0x2000 | 0x4);
        assert_eq!(group_for_112(&int), int, "an interior group is left as is");
    }

    /// The city's lighting as benilla reads it: no exterior group draws with vertex colours, every
    /// interior group still does, and the geometry is untouched.
    #[test]
    fn the_converted_city_lights_its_exteriors_from_the_sky() {
        let Some(src) = Source::open_default() else {
            eprintln!("no 3.3.5 install: skipped");
            return;
        };
        let root = src.read(ROOT).unwrap();
        let n = u32::from_le_bytes(chunk(&root, b"MOHD").unwrap()[4..8].try_into().unwrap()) as usize;
        let stem = ROOT.strip_suffix(".wmo").unwrap();
        let wr = benilla_formats::parse_wmo_root(&root).unwrap();
        let (mut ext, mut int) = (0, 0);
        for g in 0..n {
            let raw = src.read(&format!("{stem}_{g:03}.wmo")).unwrap();
            let conv = group_for_112(&raw);
            let flags = u32::from_le_bytes(chunk(&conv, b"MOGP").unwrap()[8..12].try_into().unwrap());
            let subs = benilla_formats::wmo_group_submeshes(&conv, &wr).unwrap();
            let before = benilla_formats::wmo_group_submeshes(&raw, &wr).unwrap();
            assert_eq!(
                subs.iter().map(|s| s.positions.len()).sum::<usize>(),
                before.iter().map(|s| s.positions.len()).sum::<usize>(),
                "group {g}: geometry changed"
            );
            if flags & 0x48 != 0 {
                ext += 1;
                assert!(subs.iter().all(|s| s.vertex_colors.is_empty()), "exterior group {g} still tinted");
            } else {
                int += 1;
                assert!(subs.iter().all(|s| !s.vertex_colors.is_empty()), "interior group {g} lost its bake");
            }
        }
        assert!(ext >= 20 && int >= 60, "{ext} exterior, {int} interior groups");
    }

    /// The slope a set of heights implies at `(x, y)`, as a unit world normal.
    fn slope_normal(z: &dyn Fn(f32, f32) -> Option<f32>, x: f32, y: f32) -> Option<[f32; 3]> {
        let dx = (z(x + STEP, y)? - z(x - STEP, y)?) / (2.0 * STEP);
        let dy = (z(x, y + STEP)? - z(x, y - STEP)?) / (2.0 * STEP);
        let l = (dx * dx + dy * dy + 1.0).sqrt();
        Some([-dx / l, -dy / l, 1.0 / l])
    }

    fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
        a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    }

    /// The MCNR reading the reshape relies on: a stock normal is the slope of the stock heights
    /// in world `[X, Y, Z]` (disk byte order), over Alterac's real slopes.
    #[test]
    fn stock_normals_are_the_slope_of_the_stock_heights() {
        let Some(data) = super::super::vanilla_install() else {
            eprintln!("no 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        let ground = stock_ground(&chain).unwrap();
        let adt = Adt::parse(&chain.read("World\\Maps\\Azeroth\\Azeroth_31_30.adt").unwrap()).unwrap();
        let mut dots = Vec::new();
        for i in adt.mcnk() {
            let (pos, _) = adt.heights(i);
            let stock = adt.normals(i).unwrap();
            for k in 0..145 {
                let (x, y) = vertex_xy(pos, k);
                if let Some(n) = slope_normal(&|x, y| ground.at(x, y), x, y)
                    && n[2] < 0.97
                {
                    dots.push(dot(n, unpack_normal(stock[k])));
                }
            }
        }
        dots.sort_by(f32::total_cmp);
        assert!(dots.len() > 1000, "{} sloped vertices", dots.len());
        let median = dots[dots.len() / 2];
        assert!(median > 0.97, "stock normals vs slope: median dot {median}");
    }

    /// The seam at the blend ring and along every chunk and tile border: patched heights meet,
    /// a vertex two chunks (or two tiles) share gets one normal, normals outside the reshape are
    /// the stock bytes, inside they follow the new slope, and no baked shadow is left on ground
    /// the reshape moved.
    #[test]
    fn the_reshaped_ground_meets_its_neighbours_without_a_seam() {
        let (Some(src), Some(data)) = (Source::open_default(), super::super::vanilla_install()) else {
            eprintln!("no 3.3.5 or 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        let root = src.read(ROOT).unwrap();
        let n = u32::from_le_bytes(chunk(&root, b"MOHD").unwrap()[4..8].try_into().unwrap()) as usize;
        let stem = ROOT.strip_suffix(".wmo").unwrap();
        let groups: Vec<Vec<u8>> = (0..n).map(|g| src.read(&format!("{stem}_{g:03}.wmo")).unwrap()).collect();
        let t = translation(mohd_bbox(&root).unwrap());
        let tris = world_triangles(&groups, t);
        let fp = Footprint::new(&tris);
        let modf = modf_record(t, &tris);
        let ground = stock_ground(&chain).unwrap();
        // Every vertex of every patched (or stock) tile around the city: (height, normal bytes).
        let mut seen: std::collections::HashMap<(i32, i32), (f32, [i8; 3])> = Default::default();
        let mut stock_n: std::collections::HashMap<(i32, i32), [i8; 3]> = Default::default();
        let (mut shared, mut moved, mut follow) = (0, 0, Vec::new());
        for x in 29..=33u32 {
            for y in 29..=33u32 {
                let name = format!("World\\Maps\\Azeroth\\Azeroth_{x}_{y}.adt");
                let Ok(raw) = chain.read(&name) else { continue };
                let stock = Adt::parse(&raw).unwrap();
                for i in stock.mcnk() {
                    let (pos, _) = stock.heights(i);
                    for (k, nb) in stock.normals(i).unwrap().into_iter().enumerate() {
                        let (vx, vy) = vertex_xy(pos, k);
                        stock_n.insert(Ground::key(vx, vy), nb);
                    }
                }
                let bytes = if TILES.contains(&(x, y)) {
                    patch_tile(&raw, &fp, &ground, &modf).unwrap().map_or(raw.clone(), |p| p.0)
                } else {
                    raw.clone()
                };
                let adt = Adt::parse(&bytes).unwrap();
                for i in adt.mcnk() {
                    let (pos, h) = adt.heights(i);
                    let nr = adt.normals(i).unwrap();
                    for k in 0..145 {
                        let (vx, vy) = vertex_xy(pos, k);
                        let key = Ground::key(vx, vy);
                        let v = (pos[2] + h[k], nr[k]);
                        if let Some(o) = seen.insert(key, v) {
                            shared += 1;
                            assert!((o.0 - v.0).abs() < 1e-3, "{name}: height step {} vs {} at {vx},{vy}", o.0, v.0);
                            if (v.0 - ground.at(vx, vy).unwrap()).abs() > 1e-3 {
                                assert_eq!(o.1, v.1, "{name}: two normals at the shared vertex {vx},{vy}");
                            }
                        }
                    }
                }
                // No shadow left where the ground moved.
                for i in adt.mcnk() {
                    let (pos, h) = adt.heights(i);
                    let o = ground.at(pos[0], pos[1]).unwrap();
                    if (pos[2] + h[0] - o).abs() > 0.5 {
                        moved += 1;
                        let mut a = adt.clone();
                        if let Some(sh) = a.shadow_mut(i) {
                            assert!(sh[0] & 1 == 0, "{name}: a shadow over moved ground at {pos:?}");
                        }
                    }
                }
            }
        }
        assert!(shared > 1000, "{shared} shared vertices");
        assert!(moved > 10, "{moved} moved chunk corners");
        let new_z = |x: f32, y: f32| seen.get(&Ground::key(x, y)).map(|v| v.0);
        for (&key, &(_, nb)) in &seen {
            let (x, y) = (key.0 as f32 * STEP / 2.0, key.1 as f32 * STEP / 2.0);
            let changed = |x: f32, y: f32| match (new_z(x, y), ground.at(x, y)) {
                (Some(a), Some(b)) => (a - b).abs() > 1e-3,
                _ => false,
            };
            let near = [(0.0, 0.0), (STEP, 0.0), (-STEP, 0.0), (0.0, STEP), (0.0, -STEP)]
                .iter()
                .any(|(dx, dy)| changed(x + dx, y + dy));
            if !near {
                assert_eq!(Some(&nb), stock_n.get(&key), "a normal changed where the ground did not, {x},{y}");
            } else if fp.base(x, y).is_none()
                && let (Some(a), Some(b)) = (slope_normal(&new_z, x, y), slope_normal(&|x, y| ground.at(x, y), x, y))
            {
                // Outside the city (where the ring shows): the stored normal tracks the new
                // slope as closely as the stock one tracked the old.
                let s = stock_n[&key];
                follow.push(dot(unpack_normal(nb), a) - dot(unpack_normal(s), b));
            }
        }
        follow.sort_by(f32::total_cmp);
        assert!(follow.len() > 100, "{} reshaped ring vertices", follow.len());
        let median = follow[follow.len() / 2];
        assert!(median > -0.02, "reshaped normals lag the new slope: median {median}");
    }

    /// `37_dalaran_city.sql`'s spawns: every one inside the city stands on a city floor (within
    /// a few hand-widths of the floor, nothing through it, room round it), the gate up lands on
    /// the arrival floor, and the gatekeeper outside and the gate down stand on the open ground
    /// beyond the city. Skipped without the 3.3.5 or the 1.12 install.
    #[test]
    fn the_city_spawns_stand_on_its_floors() {
        let sql = std::fs::read_to_string(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare/37_dalaran_city.sql"),
        )
        .unwrap();
        let nums = |l: &str| -> Vec<f32> {
            l.trim_start_matches('(').split(", ").filter_map(|v| v.trim_end_matches([')', ',', ';']).parse().ok()).collect()
        };
        let spawns: Vec<Vec<f32>> = sql.lines().filter(|l| l.starts_with("(25946")).map(nums).collect();
        assert!(spawns.len() >= 20, "{} spawns", spawns.len());
        let gates: Vec<Vec<f32>> = sql.lines().filter(|l| l.starts_with("(94201")).map(nums).collect();
        assert_eq!(gates.len(), 2);
        let (Some(src), Some(data)) = (Source::open_default(), super::super::vanilla_install()) else {
            eprintln!("no 3.3.5 or 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        let root = src.read(ROOT).unwrap();
        let n = u32::from_le_bytes(chunk(&root, b"MOHD").unwrap()[4..8].try_into().unwrap()) as usize;
        let stem = ROOT.strip_suffix(".wmo").unwrap();
        let groups: Vec<Vec<u8>> = (0..n).map(|g| src.read(&format!("{stem}_{g:03}.wmo")).unwrap()).collect();
        let bbox = mohd_bbox(&root).unwrap();
        let tris = world_triangles(&groups, translation(bbox));
        let fp = Footprint::new(&tris);
        let ground = stock_ground(&chain).unwrap();
        let on_ground = |x: f32, y: f32, z: f32| {
            let g = ground.height(x, y).unwrap();
            fp.base(x, y).is_none() && (fp.terrain(x, y, g) - z).abs() < 1.0
        };
        // guid, entry, map, x, y, z, o, ...
        for s in &spawns {
            assert_eq!(s[2], 0.0, "{s:?}: map 0");
            let (x, y, z) = (s[3], s[4], s[5]);
            if fp.base(x, y).is_some() {
                let f = standing_spot(&tris, x, y, z).unwrap_or_else(|| panic!("{s:?}: no room on a floor"));
                assert!((f - z).abs() < 0.3, "{s:?}: floor at {f}");
            } else {
                assert!(on_ground(x, y, z), "{s:?}: off the ground");
            }
        }
        // id, delay, priority, command, map, 11 zeros, then x, y, z, o.
        let a = arrival(bbox);
        let up = &gates[0];
        assert!((up[16] - a[0]).abs() < 0.02 && (up[17] - a[1]).abs() < 0.02 && (up[18] - a[2]).abs() < 0.02, "{up:?}");
        let down = &gates[1];
        assert!(on_ground(down[16], down[17], down[18]), "{down:?}: the road");
    }

    /// Every Dalaran teleport the gossip menus run moves the player, not the NPC: command 6 moves
    /// the script's source, a gossip script's source is the creature, and data_flags 1 swaps
    /// them (an archmage without it teleported itself away when asked for a portal).
    #[test]
    fn the_gossip_teleports_move_the_player() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare");
        let mut n = 0;
        for f in ["34_dalaran.sql", "37_dalaran_city.sql"] {
            let sql = std::fs::read_to_string(dir.join(f)).unwrap();
            for l in sql.lines().filter(|l| l.starts_with("(9420")) {
                let v: Vec<&str> = l[1..].split(", ").collect();
                // id, delay, priority, command, datalong x4, target_param1, target_param2, target_type, data_flags
                if v[3] == "6" {
                    assert_eq!(v[11], "1", "{f}: {l}");
                    n += 1;
                }
            }
        }
        assert_eq!(n, 5, "the three portals and the two gates");
    }

    #[test]
    fn the_ring_keeps_nature_and_loses_the_farm() {
        for keep in [
            "world\\lordaeron\\alteracmountains\\passivedoodads\\trees\\alteractree01.mdx",
            "world\\lordaeron\\alteracmountains\\passivedoodads\\bushes\\alteracshrub02.mdx",
            "World\\Generic\\PassiveDoodads\\Rocks\\GenericRock01.mdx",
        ] {
            assert!(is_nature(keep), "{keep}");
        }
        for go in [
            "World\\Generic\\Human\\Passive Doodads\\Fences\\HumanFence01.mdx",
            "World\\Generic\\Human\\Passive Doodads\\Carts\\Cart01.mdx",
            "world\\lordaeron\\alteracmountains\\passivedoodads\\scaffolding\\scaffold01.mdx",
        ] {
            assert!(!is_nature(go), "{go}");
        }
    }

    /// The server's portal lands where the client places the arrival.
    #[test]
    fn the_portal_script_lands_on_the_arrival() {
        let sql = std::fs::read_to_string(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare/34_dalaran.sql"),
        )
        .unwrap();
        let line = sql.lines().find(|l| l.starts_with("(942001,")).expect("the portal script");
        let f: Vec<f32> = line[1..].split(", ").filter_map(|v| v.parse().ok()).collect();
        // id, delay, priority, command, map, 11 zeros, then x, y, z, o.
        assert_eq!(f[4], 0.0, "map 0");
        let a = arrival([-336.41965, -379.13211, -325.4588, 277.26865, 323.17432, 549.87866]);
        for k in 0..4 {
            assert!((f[16 + k] - a[k]).abs() < 0.02, "{line} vs {a:?}");
        }
        assert!(!sql.contains("INSERT INTO `map_template`"), "no map of its own");
    }
}
