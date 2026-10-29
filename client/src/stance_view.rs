//! MW2's stances in the first-person view: the eye drops to MW2's crouch and prone heights
//! (60, 40 and 11 inches, `weapon_iw4`'s view-height targets) over MW2's transition times, and
//! the stance keys behave as MW2's: a tap of Left Ctrl (or the Crouch binding) toggles crouch,
//! holding it goes prone, and Space rises one stance instead of jumping. The downed crawl is prone
//! too (crate::downed).

use benilla_app::Objects;
use benilla_app::fusion::Stance;
use benilla_world::view::WorldCamera;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::Fusion;
use crate::mode::FpsState;

/// MW2's eye heights over the standing one (`VIEWHEIGHT_TARGET_*`: 60, 40, 11 inches).
pub fn eye_scale(stance: Stance) -> f32 {
    match stance {
        Stance::Stand => 1.0,
        Stance::Crouch | Stance::Slide => 40.0 / 60.0,
        Stance::Prone => 11.0 / 60.0,
    }
}

/// MW2's stance change times: 0.2 s between standing and crouching, 0.5 s into or out of prone.
pub fn transition_secs(from: Stance, to: Stance) -> f32 {
    if from == Stance::Prone || to == Stance::Prone {
        0.5
    } else {
        0.2
    }
}

/// A held Crouch binding (besides Ctrl, crate::input) goes prone after this long, as MW2's held
/// crouch does.
pub const HOLD_FOR_PRONE: f32 = 0.3;

/// The stance the keys leave this frame. A prone press (a held Left Ctrl, crate::input) toggles
/// prone; the Crouch binding held [`HOLD_FOR_PRONE`] goes prone; a crouch press toggles crouch
/// (from prone, it rises to crouch); Space rises one stance.
pub fn stance_input(
    current: Stance,
    crouch_pressed: bool,
    crouch_held_secs: f32,
    prone_pressed: bool,
    space_pressed: bool,
) -> Stance {
    if prone_pressed {
        return if current == Stance::Prone {
            Stance::Stand
        } else {
            Stance::Prone
        };
    }
    if crouch_held_secs >= HOLD_FOR_PRONE {
        return Stance::Prone;
    }
    if crouch_pressed {
        return if current == Stance::Crouch {
            Stance::Stand
        } else {
            Stance::Crouch
        };
    }
    if space_pressed {
        return match current {
            Stance::Prone => Stance::Crouch,
            Stance::Crouch | Stance::Slide => Stance::Stand,
            Stance::Stand => Stance::Stand,
        };
    }
    current
}

/// A stance holds only on foot with the layer up: dead, mounted or swimming stands you up.
pub fn must_stand(active: bool, dead: bool, mounted: bool, swimming: bool) -> bool {
    !active || dead || mounted || swimming
}

/// The eye's share of its standing height, eased between stances.
#[derive(Resource, Debug)]
pub struct StanceView {
    pub scale: f32,
    from: f32,
    to: f32,
    t: f32,
    dur: f32,
    stance: Stance,
}

impl Default for StanceView {
    fn default() -> Self {
        Self {
            scale: 1.0,
            from: 1.0,
            to: 1.0,
            t: 0.0,
            dur: 0.0,
            stance: Stance::Stand,
        }
    }
}

impl StanceView {
    /// Head for `to` over `dur` seconds, starting from wherever the view is now.
    pub fn retarget(&mut self, to: f32, dur: f32) {
        if to == self.to {
            return;
        }
        self.from = self.scale;
        self.to = to;
        self.t = 0.0;
        self.dur = dur.max(1e-3);
    }

    /// Advance by `dt` and return the eased scale (smoothstep).
    pub fn step(&mut self, dt: f32) -> f32 {
        self.t = (self.t + dt).min(self.dur);
        let x = if self.dur > 0.0 {
            self.t / self.dur
        } else {
            1.0
        };
        let s = x * x * (3.0 - 2.0 * x);
        self.scale = self.from + (self.to - self.from) * s;
        self.scale
    }

    /// Jump straight to standing (the layer is off, or the body can't hold a stance).
    fn reset(&mut self) {
        *self = Self::default();
    }
}

pub struct StanceViewPlugin;

impl Plugin for StanceViewPlugin {
    fn build(&self, app: &mut App) {
        // After benilla seats the camera and this frame's stance is known, before the crosshair
        // ray is cast (`Aim`): the eye height follows the stance the same frame.
        app.init_resource::<StanceView>().add_systems(
            Update,
            apply_stance_view
                .after(Mw2Set::Mode)
                .after(crate::fusion::update_stance)
                .before(Mw2Set::Aim),
        );
    }
}

/// Lower the seated camera to the stance's eye height.
pub(crate) fn apply_stance_view(
    time: Res<Time>,
    state: Res<FpsState>,
    fusion: Res<Fusion>,
    mut view: ResMut<StanceView>,
    objects: Objects,
    // The body's own Transform: it is a root, seated this frame from the same position as the
    // camera. Its GlobalTransform is last frame's, which moved the eye with every rise and fall.
    bodies: Query<&Transform, Without<WorldCamera>>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    if !state.active || !fusion.enabled {
        if view.scale != 1.0 || view.stance != Stance::Stand {
            view.reset();
        }
        return;
    }
    if fusion.stance != view.stance {
        let dur = transition_secs(view.stance, fusion.stance);
        view.stance = fusion.stance;
        view.retarget(eye_scale(fusion.stance), dur);
    }
    let scale = view.step(time.delta_secs());
    if scale >= 0.9999 {
        return;
    }
    let Some(feet) = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(|b| b.translation.y)
    else {
        return;
    };
    let Ok(mut cam) = cameras.single_mut() else {
        return;
    };
    let stand_eye = cam.translation.y - feet;
    if stand_eye > 0.0 {
        cam.translation.y -= (1.0 - scale) * stand_eye;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use benilla_app::fusion::Stance;

    #[test]
    fn eye_scale_matches_mw2() {
        assert_eq!(eye_scale(Stance::Stand), 1.0);
        assert!((eye_scale(Stance::Crouch) - 40.0 / 60.0).abs() < 1e-4);
        assert!((eye_scale(Stance::Prone) - 11.0 / 60.0).abs() < 1e-4);
    }

    #[test]
    fn transition_times() {
        assert_eq!(transition_secs(Stance::Stand, Stance::Crouch), 0.2);
        assert_eq!(transition_secs(Stance::Crouch, Stance::Prone), 0.5);
        assert_eq!(transition_secs(Stance::Prone, Stance::Stand), 0.5);
    }

    #[test]
    fn view_reaches_target() {
        let mut v = StanceView::default();
        v.retarget(0.5, 0.2);
        let mid = v.step(0.1);
        assert!(mid < 1.0 && mid > 0.5, "mid-transition {mid}");
        assert_eq!(v.step(0.2), 0.5);
    }

    #[test]
    fn retarget_mid_transition_starts_from_where_it_is() {
        let mut v = StanceView::default();
        v.retarget(0.2, 0.5);
        let mid = v.step(0.25);
        v.retarget(1.0, 0.2);
        assert!((v.step(0.0) - mid).abs() < 1e-5);
    }

    #[test]
    fn stance_only_holds_on_foot_with_the_layer_up() {
        assert!(!must_stand(true, false, false, false));
        assert!(must_stand(false, false, false, false)); // out of the world
        assert!(must_stand(true, true, false, false)); // dead or a ghost
        assert!(must_stand(true, false, true, false)); // mounted
        assert!(must_stand(true, false, false, true)); // swimming
    }

    #[test]
    fn stance_keys_follow_mw2() {
        use Stance::*;
        let k = |s, c, held, prone, space| stance_input(s, c, held, prone, space);
        assert_eq!(k(Stand, true, 0.0, false, false), Crouch);
        assert_eq!(k(Crouch, true, 0.0, false, false), Stand);
        assert_eq!(k(Crouch, false, 0.0, false, false), Crouch);
        assert_eq!(k(Crouch, false, 0.3, false, false), Prone, "a held binding goes prone");
        assert_eq!(k(Stand, false, 0.0, true, false), Prone, "a held Ctrl goes prone");
        assert_eq!(k(Crouch, false, 0.0, true, false), Prone);
        assert_eq!(k(Prone, false, 0.0, true, false), Stand, "held again: up");
        assert_eq!(k(Prone, true, 0.0, false, false), Crouch, "a tap from prone rises to crouch");
        assert_eq!(k(Prone, false, 0.0, false, true), Crouch);
        assert_eq!(k(Crouch, false, 0.0, false, true), Stand);
        assert_eq!(k(Stand, false, 0.0, false, true), Stand);
    }
}
