//! MW2's sprint on the client: a mirror of the server's stamina (`Fusion::SprintStamina`: 4 s, 1/s
//! back, Marathon unlimited) for the HUD bar and the sprint animations, and MW2's sprint-out: no
//! fire or aim while sprinting nor for 0.25 s after. The server enforces both; this keeps the
//! client from asking for what it would refuse.

use bevy::prelude::*;

/// Seconds of sprint at full stamina.
pub const SPRINT_MAX: f32 = 4.0;
/// Stamina back per second while not sprinting.
pub const SPRINT_REGEN: f32 = 1.0;
/// MW2's sprint-out: seconds after a sprint before the gun can fire or aim.
pub const SPRINT_OUT: f32 = 0.25;

/// The sprint's stamina and state, mirrored from the server's rules.
#[derive(Resource, Debug, Clone, Copy)]
pub struct Sprint {
    pub stamina: f32,
    pub sprinting: bool,
    /// Elapsed seconds when the last sprint ended.
    pub ended_at: f32,
    /// Ran out with Shift still held: MW2 wants it let go before sprinting again.
    spent: bool,
    /// The sprint-out in seconds: MW2's, quicker with Agility (crate::handling).
    pub sprint_out: f32,
}

impl Default for Sprint {
    fn default() -> Self {
        Self {
            stamina: SPRINT_MAX,
            sprinting: false,
            ended_at: -10.0,
            spent: false,
            sprint_out: SPRINT_OUT,
        }
    }
}

impl Sprint {
    /// Advance by `dt` at elapsed time `t`, `wants` being Shift held while moving.
    pub fn update(&mut self, dt: f32, wants: bool, marathon: bool, t: f32) {
        if !wants {
            self.spent = false;
        }
        let was = self.sprinting;
        if marathon {
            self.stamina = SPRINT_MAX;
            self.sprinting = wants;
        } else if wants && !self.spent && self.stamina > 0.0 {
            self.stamina = (self.stamina - dt).max(0.0);
            self.sprinting = self.stamina > 0.0;
            self.spent = !self.sprinting;
        } else {
            self.sprinting = false;
            self.stamina = (self.stamina + SPRINT_REGEN * dt).min(SPRINT_MAX);
        }
        if was && !self.sprinting {
            self.ended_at = t;
        }
    }

    /// Whether the gun may fire or aim: not sprinting, and the sprint-out has passed.
    pub fn fire_ready(&self, t: f32) -> bool {
        !self.sprinting && t - self.ended_at >= self.sprint_out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stamina_runs_out_in_4s() {
        let mut s = Sprint::default();
        let mut t = 0.0;
        for _ in 0..41 {
            t += 0.1;
            s.update(0.1, true, false, t);
        }
        assert!(!s.sprinting);
        assert!(s.stamina <= 0.0);
    }

    #[test]
    fn regen_1_per_s() {
        let mut s = Sprint {
            stamina: 0.0,
            ..Sprint::default()
        };
        s.update(2.0, false, false, 2.0);
        assert!((s.stamina - 2.0).abs() < 1e-5);
    }

    #[test]
    fn marathon_never_runs_out() {
        let mut s = Sprint::default();
        let mut t = 0.0;
        for _ in 0..200 {
            t += 0.1;
            s.update(0.1, true, true, t);
        }
        assert!(s.sprinting);
        assert_eq!(s.stamina, SPRINT_MAX);
    }

    #[test]
    fn fire_waits_after_sprint() {
        let mut s = Sprint::default();
        s.update(0.5, true, false, 1.0);
        assert!(!s.fire_ready(1.0));
        s.update(0.016, false, false, 1.016);
        assert!(!s.fire_ready(1.1));
        assert!(s.fire_ready(1.3));
    }

    #[test]
    fn exhausted_sprint_needs_shift_released() {
        // Out of stamina while Shift stays held: no sprint even after a little regen.
        let mut s = Sprint {
            stamina: 0.05,
            ..Sprint::default()
        };
        s.update(0.1, true, false, 0.1);
        assert!(!s.sprinting);
        s.update(0.5, true, false, 0.6);
        assert!(!s.sprinting, "held Shift must not restart a spent sprint");
        s.update(0.1, false, false, 0.7);
        s.update(0.1, true, false, 0.8);
        assert!(s.sprinting);
    }
}
