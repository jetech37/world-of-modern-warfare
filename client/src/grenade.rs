//! Bouncing frags as the server flies them (world op 35, the server's `Fusion::Bounce`): every
//! frag thrown near us, ours, other players' and NPC gunners', comes as its path's corners (the
//! throw, each bounce, where it comes to rest or is when its fuse runs out) and the gravity it
//! falls under. Between two corners it flies the one arc under that gravity that joins them in
//! their time, so the path rebuilt here passes through exactly the server's corners
//! (`Bounce::PathAt`). crate::equipment draws it.

use bevy::prelude::*;

use crate::tracer::from_wow;

/// World op 35 (the server's `Fusion::Bounce::kOpPath`).
pub const OP_PATH: u8 = 35;
/// A path has at most this many corners (`Bounce::kMaxCorners`).
pub const MAX_CORNERS: usize = 40;
/// How long a frag's fuse is from the throw (seconds), uncooked: the server's `FuseSeconds`.
pub const FRAG_FUSE: f32 = 3.5;
/// The pin's pull before a cook starts counting (seconds).
pub const PULL_BACK: f32 = 0.2;

/// A thrown frag's path as the server sent it (raw WoW yards, z up).
#[derive(Clone, Debug, PartialEq)]
pub struct FragPath {
    pub thrower: u64,
    pub weapon: u16,
    /// Yards a second squared, down.
    pub gravity: f32,
    /// When it goes off, from the throw.
    pub fuse_ms: u16,
    /// Its corners: ms from the throw and where.
    pub corners: Vec<(u16, [f32; 3])>,
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

/// Op 35, 18 + 14 n bytes: `[35, thrower u64, weapon u16, gravity f32, fuse_ms u16, n u8,
/// then n × (ms u16, x, y, z f32)]`.
pub fn decode_path(b: &[u8]) -> Option<FragPath> {
    if b.len() < 18 || b[0] != OP_PATH {
        return None;
    }
    let n = usize::from(b[17]);
    if n == 0 || n > MAX_CORNERS || b.len() != 18 + 14 * n {
        return None;
    }
    let corners = b[18..]
        .chunks_exact(14)
        .map(|c| {
            (
                u16::from_le_bytes([c[0], c[1]]),
                [f32_at(c, 2), f32_at(c, 6), f32_at(c, 10)],
            )
        })
        .collect();
    Some(FragPath {
        thrower: u64::from_le_bytes(b[1..9].try_into().ok()?),
        weapon: u16::from_le_bytes([b[9], b[10]]),
        gravity: f32_at(b, 11),
        fuse_ms: u16::from_le_bytes([b[15], b[16]]),
        corners,
    })
}

/// Where a path is `ms` after the throw (Bevy space, gravity down -Y): between two corners, the
/// arc under `gravity` joining them in their time; before the first, the first; after the last,
/// the last. As the server's `Bounce::PathAt`.
pub fn path_at(corners: &[(u16, Vec3)], gravity: f32, ms: f32) -> Vec3 {
    let Some(&(first_ms, first)) = corners.first() else {
        return Vec3::ZERO;
    };
    if ms <= f32::from(first_ms) {
        return first;
    }
    for pair in corners.windows(2) {
        let ((a_ms, a), (b_ms, b)) = (pair[0], pair[1]);
        if ms >= f32::from(b_ms) {
            continue;
        }
        let span = (f32::from(b_ms) - f32::from(a_ms)) / 1000.0;
        if span <= 0.0 {
            return b;
        }
        let s = (ms - f32::from(a_ms)) / 1000.0;
        // p(T) = a + v T - g T² / 2 Y, solved for v.
        let v = (b - a + Vec3::Y * (0.5 * gravity * span * span)) / span;
        return a + v * s - Vec3::Y * (0.5 * gravity * s * s);
    }
    corners.last().map_or(first, |c| c.1)
}

impl FragPath {
    /// The corners in Bevy space.
    pub fn corners_bevy(&self) -> Vec<(u16, Vec3)> {
        self.corners
            .iter()
            .map(|&(ms, p)| (ms, from_wow(p)))
            .collect()
    }

    /// The path sampled every `step_ms` from the throw to the fuse (Bevy space).
    pub fn points(&self, step_ms: f32) -> Vec<Vec3> {
        let corners = self.corners_bevy();
        let steps = (f32::from(self.fuse_ms) / step_ms).ceil() as usize;
        (0..=steps)
            .map(|k| path_at(&corners, self.gravity, k as f32 * step_ms))
            .collect()
    }
}

/// How long a frag held `held` seconds has cooked (ms, as the throw's seed carries it): the
/// pin's pull first, then up to the whole fuse.
pub fn cook_ms(held: f32) -> u32 {
    ((held - PULL_BACK).clamp(0.0, FRAG_FUSE) * 1000.0).round() as u32
}

/// A frag held this long is cooked through: it leaves the hand on its own.
pub fn cooked_through(held: f32) -> bool {
    held >= PULL_BACK + FRAG_FUSE
}

#[cfg(test)]
mod tests {
    use super::*;

    fn encode(path: &FragPath) -> Vec<u8> {
        let mut b = vec![OP_PATH];
        b.extend_from_slice(&path.thrower.to_le_bytes());
        b.extend_from_slice(&path.weapon.to_le_bytes());
        b.extend_from_slice(&path.gravity.to_le_bytes());
        b.extend_from_slice(&path.fuse_ms.to_le_bytes());
        b.push(path.corners.len() as u8);
        for (ms, p) in &path.corners {
            b.extend_from_slice(&ms.to_le_bytes());
            for v in p {
                b.extend_from_slice(&v.to_le_bytes());
            }
        }
        b
    }

    fn sample() -> FragPath {
        FragPath {
            thrower: 0x1122_3344_5566_7788,
            weapon: 0x0203,
            gravity: 19.2911,
            fuse_ms: 3500,
            corners: vec![
                (0, [1.0, 2.0, 3.0]),
                (420, [4.0, -5.0, 0.05]),
                (900, [6.0, -6.0, 0.05]),
            ],
        }
    }

    #[test]
    fn a_path_decodes_as_the_server_writes_it() {
        let p = sample();
        let b = encode(&p);
        assert_eq!(b.len(), 18 + 14 * 3);
        // The server's own bytes for its header (BounceTests' the_path_message_carries_every_corner).
        assert_eq!(
            &b[..11],
            &[
                35, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x03, 0x02
            ]
        );
        assert_eq!(decode_path(&b), Some(p));
        assert_eq!(decode_path(&b[..b.len() - 1]), None);
        let mut empty = b[..18].to_vec();
        empty[17] = 0;
        assert_eq!(decode_path(&empty), None);
        let mut other = b.clone();
        other[0] = 31;
        assert_eq!(decode_path(&other), None);
    }

    #[test]
    fn the_path_passes_through_its_corners_and_falls_between() {
        let g = 19.2911;
        let origin = Vec3::new(0.0, 1.5, 0.0);
        let v = Vec3::new(10.0, 4.0, 0.0);
        let at = |s: f32| origin + v * s - Vec3::Y * (0.5 * g * s * s);
        let corners = vec![
            (0u16, origin),
            (1000u16, at(1.0)),
            (1500, Vec3::new(12.0, 0.05, 0.0)),
        ];
        for &(ms, p) in &corners {
            assert!(path_at(&corners, g, f32::from(ms)).distance(p) < 1e-4);
        }
        // Between the first two corners: the free flight itself.
        assert!(path_at(&corners, g, 500.0).distance(at(0.5)) < 1e-4);
        // Before the throw, in the hand; after the last corner, still.
        assert_eq!(path_at(&corners, g, -10.0), origin);
        assert_eq!(path_at(&corners, g, 3000.0), Vec3::new(12.0, 0.05, 0.0));
        assert_eq!(path_at(&[], g, 10.0), Vec3::ZERO);
    }

    #[test]
    fn the_rebuilt_path_is_the_servers_in_wow_space() {
        // The server's arc falls along WoW's -z; rebuilt in Bevy space it falls along -Y.
        let p = sample();
        let pts = p.points(20.0);
        assert_eq!(pts.len(), 3500 / 20 + 1);
        assert!(pts[0].distance(from_wow([1.0, 2.0, 3.0])) < 1e-5);
        assert!(pts[21].distance(from_wow([4.0, -5.0, 0.05])) < 1e-4); // 420 ms
        assert_eq!(*pts.last().unwrap(), from_wow([6.0, -6.0, 0.05]));
        // Halfway along the first arc it is above the straight line (it was thrown up and fell).
        let mid = path_at(&p.corners_bevy(), p.gravity, 210.0);
        let line = from_wow([1.0, 2.0, 3.0]).lerp(from_wow([4.0, -5.0, 0.05]), 0.5);
        assert!(mid.y > line.y);
    }

    #[test]
    fn a_held_frag_cooks_after_the_pull() {
        assert_eq!(cook_ms(0.1), 0);
        assert_eq!(cook_ms(PULL_BACK), 0);
        assert_eq!(cook_ms(1.2), 1000);
        assert_eq!(cook_ms(60.0), 3500);
        assert!(!cooked_through(3.0));
        assert!(cooked_through(PULL_BACK + FRAG_FUSE));
    }
}
