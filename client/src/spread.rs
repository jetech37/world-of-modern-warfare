//! MW2's bullet spread (`PM_UpdateAimSpread`): an aim-spread scale from 0 to 1 that each shot
//! raises by the gun's fire add, moving and turning raise, and that decays at the stance's rate.
//! The hip cone runs from the stance's least to most spread by that scale; aiming blends it to the
//! gun's aimed spread. Steady Aim narrows the hip cone (0.65) and the air doubles it. The cone is
//! a half-angle in degrees; each pull's pellets scatter in it (`benilla_app::fusion::pellet_direction`).

use benilla_app::fusion::Stance;

/// A gun's spread numbers from its WeaponDef (degrees, and scale units per second).
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SpreadStats {
    pub stand_min: f32,
    pub stand_max: f32,
    pub ducked_min: f32,
    pub ducked_max: f32,
    pub prone_min: f32,
    pub prone_max: f32,
    pub decay_rate: f32,
    pub fire_add: f32,
    pub move_add: f32,
    pub turn_add: f32,
    pub ducked_decay: f32,
    pub prone_decay: f32,
    pub ads: f32,
}

/// The hip cone as a share of MW2's own (the server's `kGunfight.hipSpreadScale`,
/// server/src/fusion/include/fusion/Gunfights.h; keep the two the same): MW2 scatters its rounds
/// toward the middle of the cone, ours spread evenly over it, so the same degrees felt wilder.
pub const HIP_SCALE: f32 = 0.7;
/// A rifle, sniper or LMG's aimed cone at most, degrees (the server's `kGunfight.adsCapDeg`):
/// down the sights is on.
pub const ADS_CAP_DEG: f32 = 0.1;
/// MW2's `weapClass` values (IW4 `WEAPCLASS_*`) whose aimed cone is held to [`ADS_CAP_DEG`]:
/// rifle, sniper, machine gun.
const CAPPED_CLASSES: [i32; 3] = [0, 1, 2];

/// Steady Aim's hip-spread scale, and the air's.
pub const STEADY_AIM_SCALE: f32 = 0.65;
pub const AIRBORNE_SCALE: f32 = 2.0;

/// The turn rate (degrees a second) that adds the gun's whole turn add.
const FULL_TURN_DPS: f32 = 90.0;

impl SpreadStats {
    fn range(&self, stance: Stance) -> (f32, f32) {
        match stance {
            Stance::Crouch | Stance::Slide => (self.ducked_min, self.ducked_max),
            Stance::Prone => (self.prone_min, self.prone_max),
            Stance::Stand => (self.stand_min, self.stand_max),
        }
    }

    /// The definition's numbers with a rifle, sniper or LMG's aimed cone held to
    /// [`ADS_CAP_DEG`] (`weap_class` is MW2's `weapClass`).
    pub fn aimed_capped(mut self, weap_class: i32) -> Self {
        if CAPPED_CLASSES.contains(&weap_class) {
            self.ads = self.ads.min(ADS_CAP_DEG);
        }
        self
    }

    fn decay(&self, stance: Stance) -> f32 {
        let k = match stance {
            Stance::Crouch | Stance::Slide => self.ducked_decay,
            Stance::Prone => self.prone_decay,
            Stance::Stand => 1.0,
        };
        // A WeaponDef that leaves the stance factor at 0 decays at the standing rate.
        self.decay_rate * if k > 0.0 { k } else { 1.0 }
    }
}

/// The aim-spread scale, 0 (settled) to 1 (wide open).
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Spread {
    pub scale: f32,
}

impl Spread {
    /// One frame: moving (`move_frac`, 0 still to 1 a full run) and turning raise the scale; with
    /// neither, the stance's decay lowers it (as MW2: the two never cancel).
    pub fn step(
        &mut self,
        s: &SpreadStats,
        stance: Stance,
        dt: f32,
        move_frac: f32,
        turn_dps: f32,
    ) {
        let add = s.move_add * move_frac.clamp(0.0, 1.0)
            + s.turn_add * (turn_dps.abs() / FULL_TURN_DPS).min(1.0);
        let rate = if add > 0.0 { add } else { -s.decay(stance) };
        self.scale = (self.scale + rate * dt).clamp(0.0, 1.0);
    }

    /// A round fired: the gun's fire add.
    pub fn fired(&mut self, s: &SpreadStats) {
        self.scale = (self.scale + s.fire_add).min(1.0);
    }

    /// The cone now (half-angle, degrees): `ads` 0 at the hip to 1 fully aimed.
    pub fn angle(
        &self,
        s: &SpreadStats,
        stance: Stance,
        ads: f32,
        steady_aim: bool,
        airborne: bool,
    ) -> f32 {
        let (lo, hi) = s.range(stance);
        let mut hip = (lo + (hi - lo) * self.scale) * HIP_SCALE;
        if steady_aim {
            hip *= STEADY_AIM_SCALE;
        }
        if airborne {
            hip *= AIRBORNE_SCALE;
        }
        let ads = ads.clamp(0.0, 1.0);
        hip + (s.ads - hip) * ads
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn m4() -> SpreadStats {
        SpreadStats {
            stand_min: 3.0,
            stand_max: 8.0,
            ducked_min: 2.5,
            ducked_max: 7.0,
            prone_min: 2.0,
            prone_max: 6.0,
            decay_rate: 2.0,
            fire_add: 0.4,
            move_add: 1.5,
            turn_add: 0.5,
            ducked_decay: 1.05,
            prone_decay: 1.1,
            ads: 0.0,
        }
    }

    #[test]
    fn firing_opens_the_cone_and_rest_closes_it() {
        let s = m4();
        let mut sp = Spread::default();
        assert!((sp.angle(&s, Stance::Stand, 0.0, false, false) - 3.0 * HIP_SCALE).abs() < 1e-5);
        sp.fired(&s);
        sp.fired(&s);
        assert!((sp.angle(&s, Stance::Stand, 0.0, false, false) - 7.0 * HIP_SCALE).abs() < 1e-4);
        sp.fired(&s);
        assert_eq!(sp.scale, 1.0); // capped
        for _ in 0..60 {
            sp.step(&s, Stance::Stand, 1.0 / 60.0, 0.0, 0.0);
        }
        assert!(sp.scale < 1e-4, "{}", sp.scale);
    }

    #[test]
    fn moving_widens_and_aiming_narrows() {
        let s = m4();
        let mut sp = Spread::default();
        for _ in 0..30 {
            sp.step(&s, Stance::Stand, 1.0 / 60.0, 1.0, 0.0);
        }
        assert!(sp.scale > 0.0);
        let hip = sp.angle(&s, Stance::Stand, 0.0, false, false);
        assert!(sp.angle(&s, Stance::Stand, 1.0, false, false) < hip);
        assert_eq!(sp.angle(&s, Stance::Stand, 1.0, false, false), s.ads);
    }

    #[test]
    fn stance_perk_and_air_shape_the_hip_cone() {
        let s = m4();
        let sp = Spread::default();
        let near = |a: f32, b: f32| (a - b).abs() < 1e-5;
        assert!(near(sp.angle(&s, Stance::Prone, 0.0, false, false), 2.0 * HIP_SCALE));
        assert!(near(sp.angle(&s, Stance::Crouch, 0.0, false, false), 2.5 * HIP_SCALE));
        assert!(near(
            sp.angle(&s, Stance::Stand, 0.0, true, false),
            3.0 * HIP_SCALE * 0.65
        ));
        assert!(near(sp.angle(&s, Stance::Stand, 0.0, false, true), 6.0 * HIP_SCALE));
    }

    #[test]
    fn the_hip_is_tighter_than_mw2s_and_rifles_aim_true() {
        // The server's floor (Ballistics.cpp LeastSpread) allows the narrowed hip cone.
        assert!(HIP_SCALE > 0.5 && HIP_SCALE < 1.0);
        let mut s = m4();
        s.ads = 1.5;
        // A rifle (0), sniper (1) or machine gun (2) aims within the cap; a shotgun (4) keeps its own.
        assert_eq!(s.aimed_capped(0).ads, ADS_CAP_DEG);
        assert_eq!(s.aimed_capped(1).ads, ADS_CAP_DEG);
        assert_eq!(s.aimed_capped(2).ads, ADS_CAP_DEG);
        assert_eq!(s.aimed_capped(4).ads, 1.5);
        s.ads = 0.0;
        assert_eq!(s.aimed_capped(0).ads, 0.0);
    }
}
