//! A hand's shape apart from where the hand is: its middle finger's three joints and its thumb's,
//! in the hand's own frame. The frame is the one the arms already agree on
//! (`crate::mw2::wow_arms::hand_frame`): x from the wrist to the middle knuckle, y toward the
//! thumb's root, z = x × y; the wrist-to-knuckle length is 1. The tracks' clips carry one per hand,
//! frame by frame (a fist round the hilt, a hand opened for a spell); the arms put it on MW2's
//! hand in place of the gun grip's fingers. It also carries where a spell held in the hand sits
//! (The tracks' `MagicNode`), so the glow stays in the hand as it moves.

use bevy::math::{Mat3, Vec3};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct HandShape {
    pub mid: [Vec3; 3],
    pub thumb: [Vec3; 3],
    /// Where a spell held in this hand sits.
    pub magic: Vec3,
}

/// The frame from the wrist toward `knuckle`, its side toward `thumb` (made square).
fn frame(wrist: Vec3, knuckle: Vec3, thumb: Vec3) -> Mat3 {
    let x = (knuckle - wrist).normalize_or(Vec3::X);
    let side = thumb - wrist;
    let y = (side - x * side.dot(x)).normalize_or(x.any_orthonormal_vector());
    Mat3::from_cols(x, y, x.cross(y))
}

impl HandShape {
    /// The tracks' fist round a one-hander's hilt (`1hm_idle`, the right hand).
    pub const FIST: HandShape = HandShape {
        mid: [
            Vec3::new(1.0, 0.0, 0.0),
            Vec3::new(0.859, 0.14, -0.361),
            Vec3::new(0.657, 0.215, -0.444),
        ],
        thumb: [
            Vec3::new(0.345, 0.324, 0.0),
            Vec3::new(0.459, 0.624, -0.166),
            Vec3::new(0.647, 0.533, -0.406),
        ],
        magic: FIST_MAGIC,
    };
    /// The tracks' hand held ready with a spell, a loose claw (`mrh_readyloop`, the right hand).
    pub const OPEN: HandShape = HandShape {
        mid: [
            Vec3::new(1.0, 0.0, 0.0),
            Vec3::new(1.286, 0.112, -0.275),
            Vec3::new(1.198, 0.206, -0.466),
        ],
        thumb: [
            Vec3::new(0.345, 0.324, 0.0),
            Vec3::new(0.498, 0.609, -0.16),
            Vec3::new(0.681, 0.73, -0.391),
        ],
        magic: OPEN_MAGIC,
    };

    /// A hand's shape from its joints and its spell's place (anywhere, any size).
    pub fn of(wrist: Vec3, mid: [Vec3; 3], thumb: [Vec3; 3], magic: Vec3) -> HandShape {
        let f = frame(wrist, mid[0], thumb[0]);
        let inv = f.transpose();
        let len = (mid[0] - wrist).length().max(1e-6);
        let local = |p: Vec3| inv * (p - wrist) / len;
        HandShape {
            mid: mid.map(local),
            thumb: thumb.map(local),
            magic: local(magic),
        }
    }

    /// The joints of this shape on a hand whose wrist, middle knuckle and thumb root are where
    /// they are: (middle finger, thumb). The knuckle and thumb root keep their ways from the wrist.
    pub fn place(&self, wrist: Vec3, knuckle: Vec3, thumb: Vec3) -> ([Vec3; 3], [Vec3; 3]) {
        let f = frame(wrist, knuckle, thumb);
        let len = (knuckle - wrist).length();
        let world = |p: Vec3| wrist + f * p * len;
        (self.mid.map(world), self.thumb.map(world))
    }

    /// Where a spell held in this hand sits, on a hand placed as for [`Self::place`].
    pub fn magic_at(&self, wrist: Vec3, knuckle: Vec3, thumb: Vec3) -> Vec3 {
        let f = frame(wrist, knuckle, thumb);
        wrist + f * self.magic * (knuckle - wrist).length()
    }

    /// Part way to `other`.
    pub fn mix(&self, other: &HandShape, k: f32) -> HandShape {
        let k = k.clamp(0.0, 1.0);
        let lerp = |a: [Vec3; 3], b: [Vec3; 3]| [0, 1, 2].map(|i| a[i].lerp(b[i], k));
        HandShape {
            mid: lerp(self.mid, other.mid),
            thumb: lerp(self.thumb, other.thumb),
            magic: self.magic.lerp(other.magic, k),
        }
    }

    /// How closed the hand is, 0 (The tracks' spell hand) to 1 (its fist round a hilt): how far
    /// the middle finger's last joint has curled back toward the wrist.
    pub fn closed(&self) -> f32 {
        let (open, fist) = (HandShape::OPEN.mid[2].x, HandShape::FIST.mid[2].x);
        ((open - self.mid[2].x) / (open - fist)).clamp(0.0, 1.0)
    }

    /// The other hand's twin (a mirror turns the frame's z about: x × y flips).
    pub fn mirrored(&self) -> HandShape {
        let m = |p: Vec3| Vec3::new(p.x, p.y, -p.z);
        HandShape {
            mid: self.mid.map(m),
            thumb: self.thumb.map(m),
            magic: m(self.magic),
        }
    }
}

/// The tracks' `MagicNode` rides the hand, the same in every clip: in front of the palm by the
/// knuckles, a little toward the thumb (the right hand; the left's mirrors it).
const MAGIC: Vec3 = Vec3::new(0.956, 0.198, -0.317);
const FIST_MAGIC: Vec3 = MAGIC;
const OPEN_MAGIC: Vec3 = MAGIC;

impl Default for HandShape {
    fn default() -> Self {
        HandShape::FIST
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn near(a: Vec3, b: Vec3) -> bool {
        (a - b).length() < 1e-4
    }

    #[test]
    fn a_shape_taken_from_a_hand_goes_back_onto_it() {
        let wrist = Vec3::new(3.0, -2.0, -10.0);
        let mid = [
            Vec3::new(3.0, -2.0, -14.0),
            Vec3::new(3.0, -3.0, -15.0),
            Vec3::new(3.0, -4.0, -15.0),
        ];
        let thumb = [
            Vec3::new(3.5, -1.0, -11.0),
            Vec3::new(3.8, -0.5, -12.5),
            Vec3::new(3.9, -0.5, -14.0),
        ];
        let magic = Vec3::new(2.0, -2.5, -13.0);
        let s = HandShape::of(wrist, mid, thumb, magic);
        assert!(near(s.magic_at(wrist, mid[0], thumb[0]), magic));
        assert!(near(s.mid[0], Vec3::X));
        assert!(s.thumb[0].z.abs() < 1e-5); // the thumb root lies in the frame's xy plane
        let (m, t) = s.place(wrist, mid[0], thumb[0]);
        for i in 0..3 {
            assert!(near(m[i], mid[i]), "{i}: {} vs {}", m[i], mid[i]);
            assert!(near(t[i], thumb[i]), "{i}: {} vs {}", t[i], thumb[i]);
        }
    }

    #[test]
    fn a_shape_goes_onto_a_bigger_hand_turned_any_way() {
        let s = HandShape::FIST;
        // A hand twice the size pointing up (+y), its thumb to -x.
        let (m, t) = s.place(
            Vec3::ZERO,
            Vec3::new(0.0, 2.0, 0.0),
            Vec3::new(-1.0, 0.5, 0.0),
        );
        assert!(near(m[0], Vec3::new(0.0, 2.0, 0.0)));
        // The knuckle is twice as far; the other joints with it.
        assert!((m[1].length() - 2.0 * s.mid[1].length()).abs() < 1e-4);
        assert!(t[0].x < 0.0);
    }

    #[test]
    fn a_fist_curls_further_than_an_open_hand() {
        let reach = |s: &HandShape| s.mid[2].x;
        assert!(reach(&HandShape::FIST) < reach(&HandShape::OPEN));
    }

    #[test]
    fn the_left_hands_twin_curls_the_mirror_way() {
        let wrist = Vec3::ZERO;
        let right = HandShape::FIST;
        // The right hand pointing ahead (-z), thumb up; its mirror across x.
        let (rm, rt) = right.place(wrist, Vec3::NEG_Z, Vec3::new(0.2, 1.0, -0.2));
        let mirror = |v: Vec3| Vec3::new(-v.x, v.y, v.z);
        let left = right.mirrored();
        let (lm, lt) = left.place(wrist, Vec3::NEG_Z, mirror(Vec3::new(0.2, 1.0, -0.2)));
        for i in 0..3 {
            assert!(near(lm[i], mirror(rm[i])), "{i}");
            assert!(near(lt[i], mirror(rt[i])), "{i}");
        }
    }

    #[test]
    fn a_fist_is_closed_and_a_spell_hand_open() {
        assert!((HandShape::FIST.closed() - 1.0).abs() < 1e-5);
        assert!(HandShape::OPEN.closed().abs() < 1e-5);
        let half = HandShape::FIST.mix(&HandShape::OPEN, 0.5).closed();
        assert!((half - 0.5).abs() < 0.05, "{half}");
        // Past either end stays in 0..1.
        let straight = HandShape {
            mid: [Vec3::X, Vec3::X * 1.5, Vec3::X * 2.0],
            ..HandShape::OPEN
        };
        assert_eq!(straight.closed(), 0.0);
    }

    #[test]
    fn halfway_is_between() {
        let h = HandShape::FIST.mix(&HandShape::OPEN, 0.5);
        assert!(near(
            h.mid[2],
            HandShape::FIST.mid[2].lerp(HandShape::OPEN.mid[2], 0.5)
        ));
    }
}
