//! Terrain holes that show the void from the air, closed around the warded cities
//! (`fusion/Airspace.h`). A 1.12 `MCNK` cuts its holes with the low 16 bits of the header word at
//! `+0x3C`: a 4 x 4 grid of 8⅓-yard cells (bit `row · 4 + col`, the row along world -X, the
//! column along world -Y from the chunk's corner at `+0x68`).
//!
//! A hole under a WMO is the WMO's way in (Ironforge's gate, the Undercity's lifts, a cave): it
//! stays. A hole no WMO's box covers looks into nothing, and is filled by clearing its bit (the
//! chunk's heights run on under a hole). Only cells inside the city circles are looked at.
//!
//! Std only, and in place (a hole bit is a fixed header word, nothing moves): `wotlk_import` runs
//! it on the stock tiles it puts in `patch-9`, and `tools/community-merge` includes this same file
//! (`#[path]`) for the city tiles `patch-A` carries, which win over `patch-9`.

/// The ADT frame's origin offset (32 tiles of 533⅓ yd).
const ORIGIN: f32 = 17066.666;
const TILE: f32 = 533.333_3;
const CHUNK: f32 = TILE / 16.0;
const CELL: f32 = CHUNK / 4.0;
/// A WMO's box counts this far beyond itself (its entrance's lip).
const WMO_MARGIN: f32 = 2.0;

/// The warded cities' circles, `(map, world x, world y, radius)`: `Airspace.cpp`'s cities.
pub const CITIES: [(u32, f32, f32, f32); 7] = [
    (0, -8676.0, 803.0, 720.0),   // Stormwind and its harbour
    (0, -4836.0, -1124.0, 550.0), // Ironforge and its mountain
    (1, 1813.0, -4410.0, 700.0),  // Orgrimmar
    (0, 1558.0, 393.0, 420.0),    // the Undercity / Lordaeron
    (1, -1187.0, -8.0, 420.0),    // Thunder Bluff
    (1, 9885.0, 2408.0, 450.0),   // Darnassus
    (0, 277.0, 343.0, 380.0),     // Dalaran
];

fn rd32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}
fn rdf(b: &[u8], o: usize) -> f32 {
    f32::from_bits(rd32(b, o))
}

/// The tile `(x, y)` (`<Map>_<x>_<y>.adt`) holding world `(wx, wy)`.
pub fn tile_of(wx: f32, wy: f32) -> (u32, u32) {
    (((ORIGIN - wy) / TILE) as u32, ((ORIGIN - wx) / TILE) as u32)
}

/// Every tile a city circle touches, `(map, x, y)`, without repeats.
pub fn city_tiles() -> Vec<(u32, u32, u32)> {
    let mut out = Vec::new();
    for (map, cx, cy, r) in CITIES {
        let (x0, y0) = tile_of(cx + r, cy + r);
        let (x1, y1) = tile_of(cx - r, cy - r);
        for x in x0..=x1 {
            for y in y0..=y1 {
                if !out.contains(&(map, x, y)) {
                    out.push((map, x, y));
                }
            }
        }
    }
    out
}

/// The archive path of a tile.
pub fn tile_path(map: u32, x: u32, y: u32) -> String {
    let dir = if map == 0 { "Azeroth" } else { "Kalimdor" };
    format!("World\\Maps\\{dir}\\{dir}_{x}_{y}.adt")
}

fn in_city(map: u32, x: f32, y: f32) -> bool {
    CITIES
        .iter()
        .any(|&(m, cx, cy, r)| m == map && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
}

/// The top-level chunks of an ADT: `(tag as read forward, payload start, payload length)`.
fn chunks(b: &[u8]) -> Vec<([u8; 4], usize, usize)> {
    let mut out = Vec::new();
    let mut o = 0;
    while o + 8 <= b.len() {
        let tag = [b[o + 3], b[o + 2], b[o + 1], b[o]];
        let n = rd32(b, o + 4) as usize;
        if o + 8 + n > b.len() {
            break;
        }
        out.push((tag, o + 8, n));
        o += 8 + n;
    }
    out
}

/// A tile's WMO placement: its file, world position, heading (MODF `+0x18`, degrees) and the world
/// XY box of its MODF extents `[min x, min y, max x, max y]` (placement frame: world X = ORIGIN -
/// z', world Y = ORIGIN - x').
#[derive(Clone, Debug)]
pub struct Wmo {
    pub name: String,
    pub pos: [f32; 3],
    pub heading: f32,
    pub bbox: [f32; 4],
    pub zs: [f32; 2],
}

pub fn wmos(b: &[u8]) -> Vec<Wmo> {
    let cs = chunks(b);
    let get = |t: &[u8; 4]| cs.iter().find(|c| &c.0 == t).map(|c| &b[c.1..c.1 + c.2]).unwrap_or(&[]);
    let (names, ids, modf) = (get(b"MWMO"), get(b"MWID"), get(b"MODF"));
    modf.chunks_exact(64)
        .map(|r| {
            let id = rd32(r, 0) as usize;
            let name = (id * 4 + 4 <= ids.len())
                .then(|| rd32(ids, id * 4) as usize)
                .and_then(|o| names.get(o..))
                .map(|s| String::from_utf8_lossy(&s[..s.iter().position(|&c| c == 0).unwrap_or(s.len())]).into_owned())
                .unwrap_or_default();
            let (ax, ay, az, bx, by, bz) = (rdf(r, 0x20), rdf(r, 0x24), rdf(r, 0x28), rdf(r, 0x2C), rdf(r, 0x30), rdf(r, 0x34));
            Wmo {
                name,
                pos: [ORIGIN - rdf(r, 16), ORIGIN - rdf(r, 8), rdf(r, 12)],
                heading: rdf(r, 0x18),
                bbox: [ORIGIN - az.max(bz), ORIGIN - ax.max(bx), ORIGIN - az.min(bz), ORIGIN - ax.min(bx)],
                zs: [ay.min(by), ay.max(by)],
            }
        })
        .collect()
}

/// A world box `[min x, min y, min z, max x, max y, max z]` some WMO geometry fills.
pub type Volume = [f32; 6];

/// The world boxes of a WMO root's groups (MOGI, 32 bytes each: flags, local min, local max,
/// name), turned by the placement's heading as `dalaran::to_world` does (about Z by heading -
/// 180; pitch and roll are 0 in every city placement).
pub fn group_volumes(w: &Wmo, root: &[u8]) -> Vec<Volume> {
    let cs = chunks(root);
    let Some(&(_, at, n)) = cs.iter().find(|c| &c.0 == b"MOGI") else {
        return Vec::new();
    };
    let a = (w.heading - 180.0).to_radians();
    let (c, s) = (a.cos(), a.sin());
    root[at..at + n]
        .chunks_exact(32)
        .map(|g| {
            let lo = [rdf(g, 4), rdf(g, 8), rdf(g, 12)];
            let hi = [rdf(g, 16), rdf(g, 20), rdf(g, 24)];
            let mut v = [f32::MAX, f32::MAX, f32::MAX, f32::MIN, f32::MIN, f32::MIN];
            for k in 0..8 {
                let l = [
                    if k & 1 == 0 { lo[0] } else { hi[0] },
                    if k & 2 == 0 { lo[1] } else { hi[1] },
                    if k & 4 == 0 { lo[2] } else { hi[2] },
                ];
                let p = [w.pos[0] + c * l[0] - s * l[1], w.pos[1] + s * l[0] + c * l[1], w.pos[2] + l[2]];
                for d in 0..3 {
                    v[d] = v[d].min(p[d]);
                    v[d + 3] = v[d + 3].max(p[d]);
                }
            }
            v
        })
        .collect()
}

/// What a tile's WMOs fill: each placement's group boxes when its root reads, else its whole
/// MODF box. `read` gives an archive file's bytes.
pub fn volumes(b: &[u8], read: &dyn Fn(&str) -> Option<Vec<u8>>) -> Vec<Volume> {
    let mut out = Vec::new();
    for w in wmos(b) {
        let groups = read(&w.name).map(|root| group_volumes(&w, &root)).unwrap_or_default();
        if groups.is_empty() {
            out.push([w.bbox[0], w.bbox[1], w.zs[0], w.bbox[2], w.bbox[3], w.zs[1]]);
        } else {
            out.extend(groups);
        }
    }
    out
}

/// The WMO volumes of a tile and the eight round it: a big WMO (a whole city) is not always
/// listed in every tile it reaches.
pub fn volumes_around(map: u32, x: u32, y: u32, read: &dyn Fn(&str) -> Option<Vec<u8>>) -> Vec<Volume> {
    let mut out = Vec::new();
    for dx in -1i32..=1 {
        for dy in -1i32..=1 {
            let (nx, ny) = (x as i32 + dx, y as i32 + dy);
            if !(0..64).contains(&nx) || !(0..64).contains(&ny) {
                continue;
            }
            if let Some(b) = read(&tile_path(map, nx as u32, ny as u32)) {
                out.extend(volumes(&b, read));
            }
        }
    }
    out
}

/// Which hole bits of a chunk with corner `(px, py)` and ground heights `zs` to close: open, in a
/// city, and no WMO volume (grown by [`WMO_MARGIN`]) at that cell near the ground. Also the
/// in-city open bits a WMO keeps.
pub fn void_bits(map: u32, px: f32, py: f32, zs: [f32; 2], holes: u16, vols: &[Volume]) -> (u16, u16) {
    let (mut close, mut keep) = (0u16, 0u16);
    let m = WMO_MARGIN;
    for bit in 0..16u16 {
        if holes & (1 << bit) == 0 {
            continue;
        }
        let (row, col) = ((bit / 4) as f32, (bit % 4) as f32);
        let (x1, y1) = (px - row * CELL, py - col * CELL);
        let (x0, y0) = (x1 - CELL, y1 - CELL);
        if !in_city(map, (x0 + x1) / 2.0, (y0 + y1) / 2.0) {
            continue;
        }
        let under = vols.iter().any(|v| {
            x0 < v[3] + m && x1 > v[0] - m && y0 < v[4] + m && y1 > v[1] - m && zs[0] - GROUND_REACH < v[5] && zs[1] + GROUND_REACH > v[2]
        });
        if under {
            keep |= 1 << bit;
        } else {
            close |= 1 << bit;
        }
    }
    (close, keep)
}

/// A WMO counts at a hole when its geometry comes within this of the ground's height there.
const GROUND_REACH: f32 = 15.0;

/// What the pass does to a tile: open in-city hole cells, the ones it closes (their world
/// centre and the chunk's base z) and the ones a WMO keeps.
#[derive(Debug, Default)]
pub struct Survey {
    pub open: usize,
    pub kept: usize,
    pub closed: Vec<[f32; 3]>,
}

/// An MCNK's lowest and highest ground (base z plus its MCVT), from its payload start `at`.
fn ground_range(b: &[u8], at: usize, n: usize) -> [f32; 2] {
    let base = rdf(b, at + 0x70);
    let ofs = rd32(b, at + 0x14) as usize;
    // The offset counts from the MCNK magic; the sub-chunk's data is 8 past its own header.
    let sub = (at + ofs).checked_sub(8).filter(|&h| h + 8 + 145 * 4 <= at + n && &b[h..h + 4] == b"TVCM");
    let Some(h) = sub else {
        return [base - 50.0, base + 50.0];
    };
    let (mut lo, mut hi) = (f32::MAX, f32::MIN);
    for k in 0..145 {
        let v = base + rdf(b, h + 8 + k * 4);
        lo = lo.min(v);
        hi = hi.max(v);
    }
    [lo, hi]
}

fn pass(map: u32, b: &mut [u8], vols: &[Volume], write: bool) -> Survey {
    let mut s = Survey::default();
    for (tag, at, n) in chunks(b) {
        if &tag != b"MCNK" || n < 0x74 {
            continue;
        }
        let word = rd32(b, at + 0x3C);
        if word as u16 == 0 {
            continue;
        }
        let (px, py, pz) = (rdf(b, at + 0x68), rdf(b, at + 0x6C), rdf(b, at + 0x70));
        let (close, keep) = void_bits(map, px, py, ground_range(b, at, n), word as u16, vols);
        s.open += (close | keep).count_ones() as usize;
        s.kept += keep.count_ones() as usize;
        for bit in 0..16 {
            if close & (1 << bit) != 0 {
                let (row, col) = ((bit / 4) as f32, (bit % 4) as f32);
                s.closed.push([px - (row + 0.5) * CELL, py - (col + 0.5) * CELL, pz]);
            }
        }
        if write && close != 0 {
            b[at + 0x3C..at + 0x40].copy_from_slice(&(word & !(close as u32)).to_le_bytes());
        }
    }
    s
}

/// What the pass would do to a tile, unchanged; `vols` from [`volumes_around`].
pub fn survey(map: u32, raw: &[u8], vols: &[Volume]) -> Survey {
    let mut b = raw.to_vec();
    pass(map, &mut b, vols, false)
}

/// Close a tile's void holes: the patched bytes and the cells closed, `None` when none.
pub fn close_tile(map: u32, raw: &[u8], vols: &[Volume]) -> Option<(Vec<u8>, usize)> {
    let mut b = raw.to_vec();
    let s = pass(map, &mut b, vols, true);
    (!s.closed.is_empty()).then(|| (b, s.closed.len()))
}
#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// A one-chunk ADT: MVER, one WMO placement (`a.wmo`, world XY box `wmo`, z 0..40, heading
    /// 180 at the box's corner) unless `None`, and one MCNK with `holes` at ground z 10.
    pub(crate) fn tile(corner: [f32; 2], holes: u16, wmo: Option<[f32; 4]>) -> Vec<u8> {
        let mut b = Vec::new();
        let mut put = |tag: &[u8; 4], d: &[u8]| {
            b.extend([tag[3], tag[2], tag[1], tag[0]]);
            b.extend((d.len() as u32).to_le_bytes());
            b.extend_from_slice(d);
        };
        put(b"MVER", &18u32.to_le_bytes());
        put(b"MWMO", b"a.wmo\0");
        put(b"MWID", &0u32.to_le_bytes());
        let mut modf = Vec::new();
        if let Some(w) = wmo {
            let mut r = vec![0u8; 64];
            let f = |r: &mut Vec<u8>, o: usize, v: f32| r[o..o + 4].copy_from_slice(&v.to_le_bytes());
            // Back to the placement frame: x' = ORIGIN - Y, y' = Z, z' = ORIGIN - X.
            f(&mut r, 8, ORIGIN - w[1]);
            f(&mut r, 16, ORIGIN - w[0]);
            f(&mut r, 0x18, 180.0);
            for (k, v) in [ORIGIN - w[3], 0.0, ORIGIN - w[2], ORIGIN - w[1], 40.0, ORIGIN - w[0]].iter().enumerate() {
                f(&mut r, 0x20 + k * 4, *v);
            }
            modf.extend(r);
        }
        put(b"MODF", &modf);
        let mut c = vec![0u8; 0x80];
        c[0x3C..0x40].copy_from_slice(&(holes as u32 | 0xABCD_0000).to_le_bytes());
        c[0x68..0x6C].copy_from_slice(&corner[0].to_le_bytes());
        c[0x6C..0x70].copy_from_slice(&corner[1].to_le_bytes());
        c[0x70..0x74].copy_from_slice(&10.0f32.to_le_bytes());
        put(b"MCNK", &c);
        b
    }

    /// A WMO root with one group box (local min, max).
    fn root(lo: [f32; 3], hi: [f32; 3]) -> Vec<u8> {
        let mut g = vec![0u8; 32];
        for k in 0..3 {
            g[4 + k * 4..8 + k * 4].copy_from_slice(&lo[k].to_le_bytes());
            g[16 + k * 4..20 + k * 4].copy_from_slice(&hi[k].to_le_bytes());
        }
        let mut b = Vec::new();
        b.extend(*b"IGOM");
        b.extend(32u32.to_le_bytes());
        b.extend(g);
        b
    }

    #[test]
    fn the_city_tiles_are_where_the_cities_are() {
        // Stormwind's trade district is Azeroth_30_48; Dalaran's tiles are dalaran.rs's.
        assert_eq!(tile_of(-8830.0, 640.0), (30, 48));
        assert!(city_tiles().contains(&(0, 30, 48)));
        assert_eq!(tile_of(273.7, 369.2), (31, 31));
    }

    #[test]
    fn only_open_holes_in_a_city_with_no_wmo_at_the_ground_close() {
        // A chunk in Stormwind with its corner at (-8800, 600): cells 0 and 5 open.
        let holes = 0b0010_0001;
        let g = [10.0, 12.0];
        assert_eq!(void_bits(0, -8800.0, 600.0, g, holes, &[]).0, holes);
        // A WMO at the ground over cell 0 keeps it (a way in); cell 5 still closes.
        let at_ground = [-8803.0, 597.0, 0.0, -8802.0, 598.0, 20.0];
        assert_eq!(void_bits(0, -8800.0, 600.0, g, holes, &[at_ground]), (0b0010_0000, 1));
        // The same WMO high over the ground (a roof, a bridge) keeps nothing.
        let high = [-8803.0, 597.0, 60.0, -8802.0, 598.0, 90.0];
        assert_eq!(void_bits(0, -8800.0, 600.0, g, holes, &[high]).0, holes);
        // Outside every city, or on the other map: nothing.
        assert_eq!(void_bits(0, 0.0, -3000.0, g, holes, &[]), (0, 0));
        assert_eq!(void_bits(1, -8800.0, 600.0, g, holes, &[]), (0, 0));
    }

    #[test]
    fn a_tile_closes_in_place_from_its_wmo_groups() {
        // The placement's MODF box covers both cells, but its one group only cell 0.
        let raw = tile([-8800.0, 600.0], 0b0010_0001, Some([-8820.0, 580.0, -8790.0, 610.0]));
        let w = wmos(&raw);
        assert_eq!((w.len(), w[0].name.as_str()), (1, "a.wmo"));
        // Heading 180: local axes are world axes, from the placement at the box's min corner.
        let r = root([17.0, 17.0, 0.0], [19.0, 18.0, 20.0]);
        let vols = volumes(&raw, &|p| (p == "a.wmo").then(|| r.clone()));
        assert_eq!(vols.len(), 1);
        assert!((vols[0][0] - (-8803.0)).abs() < 0.01 && (vols[0][4] - 598.0).abs() < 0.01);
        let (out, n) = close_tile(0, &raw, &vols).unwrap();
        assert_eq!((out.len(), n), (raw.len(), 1));
        // Only bit 5 went; the word's upper half is untouched.
        let s = survey(0, &out, &vols);
        assert_eq!((s.open, s.kept, s.closed.len()), (1, 1, 0));
        assert_eq!(rd32(&out, raw.len() - 0x80 + 0x3C), 0xABCD_0001);
        assert!(close_tile(0, &out, &vols).is_none());
        // With no root to read, the whole MODF box keeps both.
        assert!(close_tile(0, &raw, &volumes(&raw, &|_| None)).is_none());
    }
}
