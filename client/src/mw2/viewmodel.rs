//! MW2's first-person weapon in the WoW camera: the gun and arms the [`load`](super::load)
//! module read from the install, posed every frame with IW4L's DObj (the same bone blend and
//! skinning MW2's renderer runs), placed in eye space as MW2 places it (`tag_view` at the eye)
//! and parented to benilla's world camera in yards.
//!
//! It draws with Bevy's unlit standard material, the way benilla draws its own markers in the
//! world pass, with a baked key light in the vertex colours instead of MW2's shaders.

use std::sync::Arc;

use asset_anim::{AnimClip, AnimInstance, Attach, DObj};
use asset_material::MaterialDrawMode;
use benilla_app::fusion::{Gait, Stance};
use benilla_world::view::WorldCamera;
use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::NoFrustumCulling;
use bevy::image::{ImageAddressMode, ImageSampler, ImageSamplerDescriptor};
use bevy::math::{Mat3, Mat4, Vec3, Vec4};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};
use weapon_iw4::{WeaponBobState, WeaponBobWaveformInputs};

use super::load::{LoadedWeapon, anim};
use super::sound::{Sfx, gear_alias, step_crossed};
use crate::mode::FpsState;
use crate::weapon::{ReloadTiming, Weapon};
use crate::{hand_anim, melee_pose};

/// MW2 units (inches) to WoW yards.
pub(crate) const INCHES_TO_YARDS: f32 = 1.0 / 36.0;

/// The eye-space key light (Bevy camera space: +X right, +Y up, toward -Z), over the shoulder.
pub(crate) const KEY_LIGHT: Vec3 = Vec3::new(0.35, 0.8, 0.45);

/// Seconds to blend between hip and aimed.
const ADS_BLEND: f32 = 0.15;

/// The idle's least weight: enough to hold the bones the ADS clip leaves unkeyed, too little to
/// move the ones it keys.
const IDLE_FLOOR: f32 = 1e-3;
/// A shot's clip against the base pose it plays over (weights normalise per bone).
const FIRE_OVER_BASE: f32 = 20.0;

/// MW2's `tag_view` axes (X forward, Y left, Z up) folded into Bevy's camera axes, as IW4L's
/// `fpv_pose::tag_view_to_bevy_camera` does.
fn tag_view_to_camera() -> Mat4 {
    let f = Mat3::from_cols(
        Vec3::new(0.0, 0.0, -1.0),
        Vec3::new(-1.0, 0.0, 0.0),
        Vec3::new(0.0, 1.0, 0.0),
    );
    Mat4::from_cols(
        f.x_axis.extend(0.0),
        f.y_axis.extend(0.0),
        f.z_axis.extend(0.0),
        Vec4::W,
    )
}

/// One playing clip: the slot it came from, when it started, and how fast it plays (MW2 plays a
/// reload clip over the WeaponDef's reload time).
#[derive(Clone, Copy, Debug, PartialEq)]
struct Playing {
    slot: usize,
    started: f32,
    rate: f32,
}

impl Playing {
    fn at(slot: usize, started: f32) -> Self {
        Self {
            slot,
            started,
            rate: 1.0,
        }
    }

    /// The clip time at elapsed seconds `t`.
    fn time(&self, t: f32) -> f32 {
        (t - self.started).max(0.0) * self.rate
    }
}

/// The assembled viewmodel: its DObj, the clips with their bone bindings, and the drawn
/// surfaces' mesh handles.
#[derive(Component)]
pub struct Viewmodel {
    weapon: Arc<LoadedWeapon>,
    dobj: DObj,
    view_bone: usize,
    /// Per animation slot, the clip and its track-to-bone map.
    clips: Vec<Option<(Arc<AnimClip>, Vec<Option<usize>>)>>,
    /// Per part, its first bone in the DObj.
    bone_base: Vec<usize>,
    /// Per part, per surface, the mesh to rewrite.
    meshes: Vec<Vec<Handle<Mesh>>>,
    one_shot: Option<Playing>,
    ads: f32,
    last_shots: Option<u64>,
    was_reloading: bool,
    /// Elapsed seconds the current sprint started, while sprinting.
    sprint_started: Option<f32>,
    /// MW2's bob cycle, 0..256, advanced by distance moved.
    bob_phase: f32,
    last_knife: Option<u64>,
    /// The melee weapon's swing in progress (crate::melee_pose): what, when, how long.
    melee_swing: Option<(melee_pose::Motion, f32, f32)>,
    /// The next light swing comes from the right.
    melee_from_right: bool,
    /// How far the block is up, 0..1.
    melee_block: f32,
    /// How far the hands are up (0 just raised with V, 1 ready): the weapon draws up from below.
    melee_drawn: f32,
    /// How far into the sprint's carry the weapon is (0 at the ready, 1 at the hip).
    melee_sprint: f32,
    /// Where the melee weapon is this frame, in the eye's frame (inches), while it is up
    /// (crate::mw2::held draws it there).
    pub(crate) melee_grip: Option<melee_pose::Grip>,
    /// The left hand's: a second weapon, a shield, a spell or a two-hander's hilt; `None` when
    /// it is down out of sight (crate::hands).
    pub(crate) left_grip: Option<melee_pose::Grip>,
    /// Each hand's spell glow (0 right, 1 left): where, in the eye's frame, and its school.
    pub(crate) glows: [Option<(Vec3, u32)>; 2],
    /// The left weapon's swing in progress (a dual wielder's off hand): when, how long.
    left_swing: Option<(bool, f32, f32)>,
    left_from_right: bool,
    /// Eases over jumps in the melee hands' pose (a weapon swapped in hand, a spell readied).
    melee_settle: hand_anim::Settle,
    /// How far Utility's hands are at rest (crate::hand_anim::relax_step).
    relax: f32,
    /// The melee weapon was up last frame (the gun comes back up with its raise when it goes).
    was_melee: bool,
    last_dry: Option<u64>,
    /// Elapsed seconds the reload in progress started.
    reload_started: Option<f32>,
    /// A swap began: play the drop from then (crate::mw2::keep_viewmodel).
    pub drop_from: Option<f32>,
    /// This frame's bone poses and the eye transform, and MW2's arm joints per side (right,
    /// left: shoulder, elbow, wrist, the middle finger's three, the thumb's three), for the
    /// player's own arms
    /// (crate::mw2::wow_arms).
    pub(crate) pose: Vec<Mat4>,
    pub(crate) eye_from_world: Mat4,
    pub(crate) arm_joints: [Option<[usize; 9]>; 2],
    /// The muzzle (MW2's `tag_flash`), where rounds leave the gun.
    pub(crate) flash_bone: Option<usize>,
    last_landed: f32,
    /// When the landing rattle last played: a stance change can read as a short hop.
    land_sound_at: f32,
    /// The one-shot whose notetracks have been played up to this clip time.
    notes: Option<(Playing, f32)>,
    /// MW2's eased stance placement (the crouch and prone offsets, the crawl's tilt).
    placement_state: weapon_iw4::WeaponPlacementState,
}

impl Viewmodel {
    fn clip(&self, slot: usize) -> Option<&(Arc<AnimClip>, Vec<Option<usize>>)> {
        self.clips.get(slot).and_then(Option::as_ref)
    }

    /// How long a slot's clip runs, in seconds.
    pub fn duration(&self, slot: usize) -> Option<f32> {
        self.clip(slot).map(|(c, _)| c.duration())
    }
}

/// Build the viewmodel's DObj and meshes, and spawn it under the world camera.
pub fn spawn(
    commands: &mut Commands,
    camera: Entity,
    weapon: Arc<LoadedWeapon>,
    meshes: &mut Assets<Mesh>,
    images: &mut Assets<Image>,
    materials: &mut Assets<StandardMaterial>,
    now: f32,
    raise: usize,
) -> Result<Entity, String> {
    let poses: Vec<_> = weapon
        .parts
        .iter()
        .map(|p| {
            p.skel
                .pose
                .as_ref()
                .ok_or_else(|| format!("`{}` has no skeleton", p.skel.name))
        })
        .collect::<Result<_, _>>()?;
    // The arms come first; the gun hangs its root (`j_gun`) off their `tag_weapon`, as MW2
    // attaches the view weapon to the view hands.
    let dobj = DObj::build(
        &poses
            .iter()
            .enumerate()
            .map(|(i, p)| {
                // The gun in the hand (akimbo's second in the left); a launcher's round on the
                // gun's `tag_clip`.
                let attach = match i {
                    0 => None,
                    1 => Some(Attach {
                        parent_model: 0,
                        tag: if weapon.hand == 1 {
                            "tag_weapon_left"
                        } else {
                            "tag_weapon"
                        }
                        .to_owned(),
                    }),
                    _ => Some(Attach {
                        parent_model: 1,
                        tag: "tag_clip".to_owned(),
                    }),
                };
                (*p, attach)
            })
            .collect::<Vec<_>>(),
    )
    .map_err(|e| e.to_string())?;
    let view_bone = dobj
        .bones
        .iter()
        .position(|b| b.name.eq_ignore_ascii_case("tag_view"))
        .ok_or("the arms have no tag_view")?;
    let clips = weapon
        .clips
        .iter()
        .map(|c| c.as_ref().map(|c| (c.clone(), dobj.tracks_for(c))))
        .collect();
    let bone_base = dobj.models.iter().map(|m| m.base).collect();

    let root = commands
        .spawn((
            Name::new("mw2 viewmodel"),
            Transform::from_scale(Vec3::splat(INCHES_TO_YARDS)),
            Visibility::Inherited,
            ChildOf(camera),
        ))
        .id();

    // The weapon's hide tags (attachment mounts and the like) and every bone under them: a
    // surface skinned to any is not drawn, MW2's `partBits` test against the DObj's hidden bones.
    // Parents precede children in the DObj, so one pass carries a hidden parent down.
    let mut hidden = [0u32; 6];
    for (bone, b) in dobj.bones.iter().enumerate() {
        let tagged = weapon
            .hide_tags
            .iter()
            .any(|t| t.eq_ignore_ascii_case(&b.name));
        let under_hidden = b
            .parent
            .is_some_and(|p| anim_iw4::hide_part_bit(&hidden, p));
        if tagged || under_hidden {
            anim_iw4::set_hide_part_bit(&mut hidden, bone);
        }
    }

    let mut part_meshes = Vec::new();
    for (part_index, part) in weapon.parts.iter().enumerate() {
        let skel = &part.skel;
        let base = dobj.models[part_index].base as u32;
        let mut handles = Vec::new();
        for surface in &part.surfaces {
            let (v0, vn) = surface.vertices;
            let (i0, in_) = surface.indices;
            if vn == 0
                || in_ == 0
                || v0 + vn > skel.positions.len()
                || anim_iw4::surface_hidden(&surface.part_bits, &hidden, base)
            {
                handles.push(Handle::default());
                continue;
            }
            let mut mesh = Mesh::new(
                PrimitiveTopology::TriangleList,
                RenderAssetUsages::default(),
            );
            mesh.insert_attribute(
                Mesh::ATTRIBUTE_POSITION,
                skel.positions[v0..v0 + vn].to_vec(),
            );
            mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, skel.normals[v0..v0 + vn].to_vec());
            mesh.insert_attribute(
                Mesh::ATTRIBUTE_UV_0,
                skel.uvs
                    .get(v0..v0 + vn)
                    .map(<[_]>::to_vec)
                    .unwrap_or_else(|| vec![[0.0; 2]; vn]),
            );
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; vn]);
            let indices: Vec<u32> = skel.indices[i0..i0 + in_]
                .iter()
                .map(|&i| i.saturating_sub(v0 as u32))
                .collect();
            mesh.insert_indices(Indices::U32(indices));
            let handle = meshes.add(mesh);

            let texture = surface.color.as_ref().map(|img| {
                let mut image = Image::new(
                    Extent3d {
                        width: img.width,
                        height: img.height,
                        depth_or_array_layers: 1,
                    },
                    TextureDimension::D2,
                    img.pixels.clone(),
                    // Unorm, not sRGB: benilla shades in gamma space and decodes at the end.
                    TextureFormat::Rgba8Unorm,
                    RenderAssetUsages::RENDER_WORLD,
                );
                image.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor {
                    address_mode_u: ImageAddressMode::Repeat,
                    address_mode_v: ImageAddressMode::Repeat,
                    ..ImageSamplerDescriptor::linear()
                });
                images.add(image)
            });
            let material = materials.add(StandardMaterial {
                base_color: if texture.is_some() {
                    let [r, g, b] = surface.tint;
                    Color::srgba(r, g, b, surface.opacity)
                } else {
                    Color::srgb(0.25, 0.25, 0.25).with_alpha(surface.opacity)
                },
                base_color_texture: texture,
                unlit: true,
                alpha_mode: match surface.draw {
                    MaterialDrawMode::Opaque => AlphaMode::Opaque,
                    MaterialDrawMode::AlphaTest { .. } => {
                        AlphaMode::Mask(surface.draw.alpha_cutoff().unwrap_or(0.5))
                    }
                    MaterialDrawMode::Blend => AlphaMode::Blend,
                    MaterialDrawMode::Multiply => AlphaMode::Multiply,
                    MaterialDrawMode::Additive | MaterialDrawMode::Screen => AlphaMode::Add,
                },
                cull_mode: None,
                ..default()
            });
            let mut e = commands.spawn((
                Mesh3d(handle.clone()),
                MeshMaterial3d(material),
                Transform::IDENTITY,
                NoFrustumCulling,
                ChildOf(root),
                ViewmodelPart(part_index),
            ));
            if surface.motion_tracker {
                e.insert(MotionTrackerScreen);
            }
            handles.push(handle);
        }
        part_meshes.push(handles);
    }

    // MW2's arm joints, for the player's own arms (crate::mw2::wow_arms).
    let arm_joints = ["ri", "le"].map(|side| {
        let at = |name: String| {
            dobj.bones
                .iter()
                .position(|b| b.name.eq_ignore_ascii_case(&name))
        };
        Some([
            at(format!("j_shoulder_{side}"))?,
            at(format!("j_elbow_{side}"))?,
            at(format!("j_wrist_{side}"))?,
            at(format!("j_mid_{side}_0"))?,
            at(format!("j_mid_{side}_1"))?,
            at(format!("j_mid_{side}_2"))?,
            at(format!("j_thumb_{side}_0"))?,
            at(format!("j_thumb_{side}_1"))?,
            at(format!("j_thumb_{side}_2"))?,
        ])
    });
    let flash_bone = dobj
        .bones
        .iter()
        .position(|b| b.name.eq_ignore_ascii_case("tag_flash"));
    commands.entity(root).insert(Viewmodel {
        weapon,
        flash_bone,
        dobj,
        view_bone,
        clips,
        bone_base,
        meshes: part_meshes,
        one_shot: Some(Playing::at(raise, now)),
        drop_from: None,
        pose: Vec::new(),
        eye_from_world: Mat4::IDENTITY,
        arm_joints,
        ads: 0.0,
        last_shots: None,
        was_reloading: false,
        sprint_started: None,
        bob_phase: 0.0,
        last_knife: None,
        melee_swing: None,
        melee_from_right: true,
        melee_block: 0.0,
        melee_drawn: 0.0,
        melee_sprint: 0.0,
        melee_grip: None,
        left_grip: None,
        glows: [None; 2],
        left_swing: None,
        left_from_right: false,
        melee_settle: hand_anim::Settle::default(),
        relax: 0.0,
        was_melee: false,
        last_dry: None,
        reload_started: None,
        last_landed: -10.0,
        land_sound_at: -10.0,
        notes: None,
        placement_state: weapon_iw4::WeaponPlacementState::default(),
    });
    Ok(root)
}

/// Pick this frame's clips from the weapon's state, pose the DObj and skin every surface.
/// One frame of MW2's stance placement (`BaseStanceMovementAngles`): the gun eases to the
/// weapon's crouch or prone offset, and while moving toward its walk, crouch-walk or crawl offsets
/// (the prone crawl's tilt), at the weapon's own rates. Returns the origin (inches, MW2 axes:
/// forward, left, up) and angles (degrees: pitch, yaw, roll) to add. `max_ips` is the stance's
/// top speed.
fn stance_placement(
    state: &mut weapon_iw4::WeaponPlacementState,
    placement: &super::load::Placement,
    stance: Stance,
    ads: f32,
    speed_ips: f32,
    max_ips: f32,
    dt: f32,
) -> ([f32; 3], [f32; 3]) {
    use playerstate_iw4::eflags;
    let e_flags = match stance {
        Stance::Prone => eflags::PRONE,
        Stance::Crouch | Stance::Slide => eflags::DUCK,
        Stance::Stand => 0,
    };
    let m = placement.movement;
    let movement = weapon_iw4::WeaponMovementOfsInputs {
        stand_move: m.stand_move,
        stand_rot: m.stand_rot,
        strafe_move: m.strafe_move,
        strafe_rot: m.strafe_rot,
        ducked_move: m.ducked_move,
        ducked_rot: m.ducked_rot,
        prone_move: m.prone_move,
        prone_rot: m.prone_rot,
        pos_move_rate: m.pos_move_rate,
        pos_prone_move_rate: m.pos_prone_move_rate,
        stand_move_min_speed: m.stand_move_min_speed,
        ducked_move_min_speed: m.ducked_move_min_speed,
        prone_move_min_speed: m.prone_move_min_speed,
        pos_rot_rate: m.pos_rot_rate,
        pos_prone_rot_rate: m.pos_prone_rot_rate,
    };
    let ps = weapon_iw4::WeaponPlacementPsInputs {
        e_flags,
        weapon_pos_frac: ads,
        ..Default::default()
    };
    let stance_ofs = weapon_iw4::WeaponStanceStaticOfsInputs {
        ducked_ofs: placement.ducked_ofs,
        prone_ofs: placement.prone_ofs,
        ..Default::default()
    };
    let kinematics = weapon_iw4::WeaponMovementKinematics {
        xyspeed: speed_ips,
        speed: max_ips.max(speed_ips).max(1.0),
        frametime: dt,
        ..Default::default()
    };
    weapon_iw4::base_stance_movement_angles(
        state,
        ps,
        stance_ofs,
        weapon_iw4::StanceTransitionFadeGlobals::default(),
        movement,
        kinematics,
    );
    (state.movement_origin, state.movement_angles)
}

/// The stance's share of the run speed, as the server grants it (`Fusion::SpeedMultiplier`).
fn stance_speed_scale(stance: Stance) -> f32 {
    match stance {
        Stance::Stand => 1.0,
        Stance::Crouch => 0.6,
        Stance::Prone => 0.2,
        Stance::Slide => 1.5,
    }
}

/// The knife clip: the lunge stabs (`MELEE_CHARGE`), a plain knife slices (`MELEE`).
fn melee_slot(lunge: bool, has_charge: bool) -> usize {
    if lunge && has_charge {
        anim::MELEE_CHARGE
    } else {
        anim::MELEE
    }
}

/// WoW yards per second as MW2 inches per second, run for run: WoW's 7 yd/s run is MW2's 190.
pub fn mw2_speed(wow_yards_per_sec: f32) -> f32 {
    wow_yards_per_sec * 190.0 / 7.0
}

/// MW2's weapon bob (the gun's own sway against the view, in degrees) for this stance and
/// motion, before ADS attenuation.
fn weapon_bob(
    stance: Stance,
    sprinting: bool,
    ads: f32,
    speed_ips: f32,
    cycle: u8,
) -> WeaponBobState {
    weapon_iw4::calculate_weapon_movement_bob_waveform(bob_inputs(
        stance, sprinting, ads, speed_ips, cycle,
    ))
}

/// MW2's bob at a fractional point of its 256-step cycle: the waveform at the steps either side,
/// blended (the cycle advances 1-2 steps a frame; whole steps make the bob stutter).
fn smooth_bob(
    stance: Stance,
    sprinting: bool,
    ads: f32,
    speed_ips: f32,
    cycle: f32,
) -> WeaponBobState {
    let c = cycle.rem_euclid(256.0);
    let c0 = c.floor() as u32 % 256;
    let f = c - c.floor();
    let a = weapon_bob(stance, sprinting, ads, speed_ips, c0 as u8);
    let b = weapon_bob(stance, sprinting, ads, speed_ips, ((c0 + 1) % 256) as u8);
    WeaponBobState {
        pitch: a.pitch + (b.pitch - a.pitch) * f,
        yaw: a.yaw + (b.yaw - a.yaw) * f,
        roll: a.roll + (b.roll - a.roll) * f,
    }
}

/// MW2 calms the bob as the gun comes up to aim (`weapon_bob_apply_ads_attenuation`, with the
/// rifles' bob factor of 0: none at full aim).
fn aimed_bob(mut bob: WeaponBobState, ads: f32) -> WeaponBobState {
    weapon_iw4::weapon_bob_apply_ads_attenuation(&mut bob, ads, ADS_BOB_FACTOR);
    bob
}

const ADS_BOB_FACTOR: f32 = 0.0;

/// The stance and movement offsets as MW2 applies them: faded out as the gun aims
/// (`placement_movement_channel_scale`, gone by half way), so the sights sit on the aim in any
/// stance.
fn aimed_placement(origin: [f32; 3], angles: [f32; 3], ads: f32) -> ([f32; 3], [f32; 3]) {
    let k = weapon_iw4::placement_movement_channel_scale(ads);
    (origin.map(|v| v * k), angles.map(|v| v * k))
}

/// MW2's bob inputs for this stance and motion (`speed_ips` in inches per second): the
/// stance's view height (60, 40, 11), the sprint flag, and how far the gun is aimed.
fn bob_inputs(
    stance: Stance,
    sprinting: bool,
    ads: f32,
    speed_ips: f32,
    cycle: u8,
) -> WeaponBobWaveformInputs {
    WeaponBobWaveformInputs {
        bob_cycle: cycle,
        xyspeed: speed_ips,
        view_height_target: match stance {
            Stance::Prone => weapon_iw4::VIEWHEIGHT_TARGET_PRONE,
            Stance::Crouch | Stance::Slide => weapon_iw4::VIEWHEIGHT_TARGET_CROUCH,
            Stance::Stand => 60,
        },
        pm_flags: if sprinting {
            playerstate_iw4::pm_flags::SPRINTING
        } else {
            0
        },
        weapon_pos_frac: ads,
    }
}

/// Bob cycle units per inch moved: one cycle (256) over about two strides.
const BOB_CYCLE_PER_INCH: f32 = 1.35;
/// How far the gun dips on landing, in inches (MW2's `viewweapon_land_origin_z` scales it).
const LAND_DIP_INCHES: f32 = 4.0;

/// How fast to play a clip of `clip_secs` so that it lasts `secs`: as recorded without a time.
fn clip_rate(clip_secs: Option<f32>, secs: f32) -> f32 {
    match clip_secs {
        Some(d) if d > 0.0 && secs > 0.05 => d / secs,
        _ => 1.0,
    }
}

/// Where a segmented reload stands `since` seconds in: the clip slot, when its segment began
/// (seconds after the reload's start) and how long the segment lasts.
fn reload_segment(since: f32, timing: &ReloadTiming, segments: u32) -> (usize, f32, f32) {
    if since < timing.start_secs {
        return (anim::RELOAD_START, 0.0, timing.start_secs);
    }
    let into = since - timing.start_secs;
    let loops = segments as f32 * timing.secs;
    if timing.secs > 0.0 && into < loops {
        let shell = (into / timing.secs).floor();
        return (
            anim::RELOAD,
            timing.start_secs + shell * timing.secs,
            timing.secs,
        );
    }
    (anim::RELOAD_END, timing.start_secs + loops, timing.end_secs)
}

/// The sprint clip and its time, `since` seconds into a sprint: MW2's sprint-in, then the loop.
/// Missing a loop, the end of the sprint-in holds; missing both, nothing plays.
fn sprint_clip(since: f32, in_dur: Option<f32>, loop_dur: Option<f32>) -> Option<(usize, f32)> {
    let into = in_dur.unwrap_or(0.0);
    match (in_dur, loop_dur) {
        (Some(d), _) if since < d => Some((anim::SPRINT_IN, since)),
        (_, Some(l)) => Some((anim::SPRINT_LOOP, (since - into) % l.max(0.001))),
        (Some(d), None) => Some((anim::SPRINT_IN, d)),
        (None, None) => None,
    }
}

/// The lowered gun while down: inches down and degrees tipped (crate::downed).
const LOWERED_DOWN: f32 = 7.0;
const LOWERED_TIP: f32 = 35.0;

pub fn animate(
    time: Res<Time>,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    fusion: Res<crate::fusion::Fusion>,
    input: Res<crate::input::Mw2Input>,
    hint: Res<crate::movement::MantleHint>,
    mut sfx: MessageWriter<Sfx>,
    mut models: Query<(&mut Viewmodel, &mut Visibility)>,
    mut meshes: ResMut<Assets<Mesh>>,
    view_light: Res<ViewLight>,
    held: Res<crate::melee::MeleeHeld>,
    (vault, downed, form, systems, equip): (
        Res<crate::movement::MantleVault>,
        Res<crate::downed::Downed>,
        Res<crate::forms::PlayerForm>,
        Res<crate::action_systems::ActionSystems>,
        Res<crate::equipment::EquipState>,
    ),
    melee_weapon: Res<crate::melee::MeleeWeapon>,
    hands: Res<crate::hands::Hands>,
    third: Res<crate::thirdperson::ThirdPerson>,
    mut clips: ResMut<crate::hand_anim::HandAnimClips>,
) {
    let t = time.elapsed_secs();
    // Over a ledge (crate::movement): the gun drops and tips while the hands plant.
    let (vault_down, vault_tip) = vault
        .at
        .map_or((0.0, 0.0), |at| crate::movement::vault_dip(t - at));
    // Down without Last Stand (crate::downed): the gun hangs low and tipped, out of the fight.
    let (vault_down, vault_tip) = if downed.lowered() {
        (vault_down + LOWERED_DOWN, vault_tip + LOWERED_TIP)
    } else {
        (vault_down, vault_tip)
    };
    let dt = time.delta_secs();
    for (mut vm, mut vis) in &mut models {
        // A scope up hides the gun (crate::scope); the melee weapon up, akimbo's left gun.
        let scoped = crate::scope::scoped(weapon.scope.is_some(), weapon.ads);
        let left_put_away = held.0 && vm.weapon.hand == 1;
        // Third person shows the body instead (crate::thirdperson); a form (crate::forms) has
        // neither the gun nor the arms that hold it.
        let want = if state.active
            && !scoped
            && !state.building
            && !state.view_away()
            && !left_put_away
            && !third.on
            && !form.gunless()
        {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
        if !state.active {
            continue;
        }
        let vm = &mut *vm;
        // Akimbo's left gun is silent: the right hand's viewmodel sounds for both.
        let lead = vm.weapon.hand == 0;
        let mut emit = |e: Sfx| {
            if lead {
                sfx.write(e);
            }
        };

        // Events from the gun: a shot, a reload.
        if counted(&mut vm.last_shots, weapon.shots) {
            // Akimbo: the hands fire in turn; the right hand's viewmodel sounds every shot.
            let mine = !vm.weapon.akimbo() || weapon.shot_hand == vm.weapon.hand;
            if mine
                && let Some(slot) = fire_slot(weapon.ads, weapon.mag == 0, |s| vm.clip(s).is_some())
            {
                vm.one_shot = Some(Playing::at(slot, t));
            }
            let sounds = &vm.weapon.sounds;
            let last = sounds.fire_last.as_ref().filter(|_| weapon.mag == 0);
            if let Some(alias) = last.or(sounds.fire.as_ref()) {
                emit(Sfx::new(alias.clone()));
            }
        }
        if counted(&mut vm.last_dry, weapon.dry_clicks)
            && let Some(alias) = &vm.weapon.sounds.empty
        {
            emit(Sfx::new(alias.clone()));
        }
        // The knife: a slice, or the stab when lunging. With the melee weapon up, the swing is
        // the weapon's own (crate::melee_pose): side to side in turn, overhead on a lunge.
        if counted(&mut vm.last_knife, weapon.knife_swings) {
            if held.0 {
                // Side to side in turn; a lunge swings the same (no power attacks). A dual
                // wielder's off hand swings its own (crate::hands). Each swing is over before
                // the weapon may swing again (crate::hand_anim::swing_secs).
                let now = hand_anim::HandsNow {
                    two: melee_weapon.two_handed,
                    fists: melee_weapon.subclass == 255,
                    axe: hand_anim::is_axe_or_hammer(melee_weapon.subclass),
                    handmade: std::env::var_os("MW2_HANDS_HANDMADE").is_some(),
                    tool: systems.tool_out().map_or(0, |a| a.id() as u8),
                    ..Default::default()
                };
                let gate = weapon
                    .knife_gate
                    .slash_secs
                    .unwrap_or(crate::weapon::SLASH_SECS);
                let left = weapon.knife_left;
                let turn = if left {
                    &mut vm.left_from_right
                } else {
                    &mut vm.melee_from_right
                };
                let from_right = *turn;
                *turn = !from_right;
                let secs = hand_anim::swing_secs(&now, left, gate);
                if left {
                    vm.left_swing = Some((from_right, t, secs));
                } else {
                    vm.melee_swing = Some((melee_pose::Motion::Swing { from_right }, t, secs));
                }
            } else {
                let slot = melee_slot(
                    weapon.last_knife_lunged,
                    vm.clip(anim::MELEE_CHARGE).is_some(),
                );
                if vm.clip(slot).is_some() {
                    vm.one_shot = Some(Playing::at(slot, t));
                }
            }
            let swing = vm
                .weapon
                .sounds
                .melee
                .as_deref()
                .unwrap_or("melee_knife_swing");
            emit(Sfx::new(swing));
        }
        // The melee weapon raised: the arms pose from the gun's idle (crate::mw2::viewmodel::
        // hold_melee), not a raise or reload it was part way through. Put away: the gun comes
        // back up with its raise rather than appearing at the ready.
        if held.0 != vm.was_melee {
            vm.was_melee = held.0;
            if held.0 {
                vm.one_shot = None;
            } else if vm.clip(anim::RAISE).is_some() {
                vm.one_shot = Some(Playing::at(anim::RAISE, t));
            }
        }
        // A swap: the drop clip, then the next gun replaces this one.
        if let Some(at) = vm.drop_from.take()
            && vm.clip(anim::DROP).is_some()
        {
            vm.one_shot = Some(Playing::at(anim::DROP, at));
        }

        // The reload, played over the WeaponDef's time: one clip, or for a segmented reload the
        // start, a loop per shell and the end.
        let reloading = weapon.reloading();
        let timing = vm.weapon.reload.scaled(weapon.reload_factor());
        let segmented = timing.segmented
            && vm.clip(anim::RELOAD_START).is_some()
            && vm.clip(anim::RELOAD_END).is_some();
        if reloading && !vm.was_reloading {
            vm.reload_started = Some(t);
            if !segmented {
                let slot = if weapon.mag == 0 && vm.clip(anim::RELOAD_EMPTY).is_some() {
                    anim::RELOAD_EMPTY
                } else {
                    anim::RELOAD
                };
                let left = weapon.reload_until.map_or(0.0, |end| end - t);
                vm.one_shot = Some(Playing {
                    rate: clip_rate(vm.duration(slot), left),
                    ..Playing::at(slot, t)
                });
            }
        }
        if !reloading {
            vm.reload_started = None;
        } else if segmented && let Some(t0) = vm.reload_started {
            let (slot, offset, secs) = reload_segment(t - t0, &timing, weapon.reload_segments);
            let started = t0 + offset;
            if vm.one_shot.map(|p| (p.slot, p.started)) != Some((slot, started)) {
                vm.one_shot = Some(Playing {
                    rate: clip_rate(vm.duration(slot), secs),
                    ..Playing::at(slot, started)
                });
            }
        }
        vm.was_reloading = reloading;

        // The sprint: in, then the loop while it lasts; out as a one-shot when it ends.
        let sprinting = fusion.gait == Gait::Sprint;
        if sprinting && vm.sprint_started.is_none() {
            vm.sprint_started = Some(t);
        } else if !sprinting
            && vm.sprint_started.take().is_some()
            && vm.clip(anim::SPRINT_OUT).is_some()
        {
            vm.one_shot = Some(Playing::at(anim::SPRINT_OUT, t));
        }

        // The one-shot's notetracks: MW2's reload and raise foley, named by sound alias.
        if let Some(p) = vm.one_shot
            && let Some(clip) = vm.clip(p.slot).map(|(c, _)| c.clone())
        {
            let now = p.time(t).clamp(0.0, clip.duration());
            let from = match vm.notes {
                Some((q, at)) if q == p => at,
                _ => {
                    if matches!(p.slot, anim::RAISE | anim::FIRST_RAISE)
                        && let Some(alias) = &vm.weapon.sounds.raise
                    {
                        emit(Sfx::new(alias.clone()));
                    }
                    0.0
                }
            };
            if now > from {
                for name in clip.crossed_notifies(from, now) {
                    emit(Sfx::new(name));
                }
            }
            vm.notes = Some((p, now));
        }

        // A finished one-shot falls back to the idle.
        if let Some(p) = vm.one_shot {
            let done = vm.duration(p.slot).is_none_or(|d| p.time(t) >= d);
            if done {
                vm.one_shot = None;
            }
        }

        let goal = weapon.ads;
        vm.ads += (goal - vm.ads).clamp(-dt / ADS_BLEND, dt / ADS_BLEND);

        let mut instances: Vec<AnimInstance<'_>> = Vec::with_capacity(2);
        let one_shot = vm.one_shot;
        // A shot's clip plays over the idle or aimed pose (below); any other one-shot (raise,
        // reload, knife, drop) is the whole pose.
        let fire = one_shot.filter(|p| is_fire_slot(p.slot));
        // A grenade in hand: the pin pulled and held while it cooks, the throw on release
        // (MW2's own pullback and throw clips), each the whole pose.
        let grenade = is_thrown_weapon(&vm.weapon.name)
            .then(|| {
                grenade_clip(
                    t,
                    equip.cooking,
                    equip.thrown_at,
                    vm.duration(anim::HOLD_FIRE),
                    vm.duration(anim::FIRE),
                )
            })
            .flatten();
        if let Some((slot, time)) = grenade
            && let Some((clip, tracks)) = vm.clip(slot)
        {
            instances.push(AnimInstance {
                clip,
                tracks,
                time,
                weight: 1.0,
                parts: None,
            });
        } else if let Some(p) = one_shot
            .filter(|p| !is_fire_slot(p.slot))
            .and_then(|p| vm.clip(p.slot).map(|c| (p, c)))
        {
            let (playing, (clip, tracks)) = p;
            instances.push(AnimInstance {
                clip,
                tracks,
                time: playing.time(t),
                weight: 1.0,
                parts: None,
            });
        } else if held.0 {
            // The melee weapon up: the arms from the idle, then reached for the weapon (below).
            if let Some((clip, tracks)) = vm.clip(anim::IDLE) {
                instances.push(AnimInstance {
                    clip,
                    tracks,
                    time: 0.0,
                    weight: 1.0,
                    parts: None,
                });
            }
        } else if let Some((slot, time)) = vm.sprint_started.and_then(|s| {
            sprint_clip(
                t - s,
                vm.duration(anim::SPRINT_IN),
                vm.duration(anim::SPRINT_LOOP),
            )
        }) && let Some((clip, tracks)) = vm.clip(slot)
        {
            instances.push(AnimInstance {
                clip,
                tracks,
                time,
                weight: 1.0,
                parts: None,
            });
            // The sprint clips key only part of the rig too; the idle holds the rest.
            if let Some((clip, tracks)) = vm.clip(anim::IDLE) {
                instances.push(AnimInstance {
                    clip,
                    tracks,
                    time: t % clip.duration().max(0.001),
                    weight: IDLE_FLOOR,
                    parts: None,
                });
            }
        } else {
            if let Some((clip, tracks)) = vm.clip(anim::IDLE) {
                let d = clip.duration().max(0.001);
                instances.push(AnimInstance {
                    clip,
                    tracks,
                    time: t % d,
                    // The blend is per bone, normalised over the clips that key it, and a bone
                    // no clip keys falls to its bind rotation at zero offset. The ADS clip keys
                    // only part of the rig, so the idle stays as the base layer for the rest.
                    weight: (1.0 - vm.ads).max(IDLE_FLOOR),
                    parts: None,
                });
            }
            // The aimed pose is the end of the ADS-up clip.
            if vm.ads > 0.0
                && let Some((clip, tracks)) = vm.clip(anim::ADS_UP)
            {
                instances.push(AnimInstance {
                    clip,
                    tracks,
                    time: clip.duration(),
                    weight: vm.ads,
                    parts: None,
                });
            }
            // The shot: its clip outweighs the base on the bones it keys, the base holds the
            // rest, so an aimed gun fires from the aim.
            if let Some(p) = fire
                && let Some((clip, tracks)) = vm.clip(p.slot)
            {
                instances.push(AnimInstance {
                    clip,
                    tracks,
                    time: p.time(t),
                    weight: FIRE_OVER_BASE,
                    parts: None,
                });
            }
        }

        let mut world = vm
            .dobj
            .pose(&instances, &vm.dobj.all_parts(), Mat4::IDENTITY);
        let Some(view) = world.get(vm.view_bone).copied() else {
            continue;
        };
        if std::env::var_os("MW2_PROBE_ADS").is_some()
            && vm.ads > 0.0
            && (t * 2.0).fract() < dt * 2.0
        {
            let slots: Vec<_> = instances
                .iter()
                .map(|i| (i.clip.name.clone(), i.weight))
                .collect();
            info!(
                "probe: vm `{}` hand {} ads {:.2} one_shot {:?} sprint {:?} view {:?} clips {slots:?}",
                vm.weapon.name,
                vm.weapon.hand,
                vm.ads,
                vm.one_shot.map(|p| p.slot),
                vm.sprint_started,
                view.w_axis.truncate()
            );
        }
        // MW2's weapon bob for the stance and motion (degrees, turning the gun about the eye),
        // and the dip on landing (inches).
        let speed_ips = mw2_speed(fusion.speed);
        let old_phase = vm.bob_phase;
        vm.bob_phase = (vm.bob_phase + speed_ips * dt * BOB_CYCLE_PER_INCH) % 256.0;
        // The gear rattles with each step on the ground, and on landing and mantling.
        if !fusion.airborne && speed_ips > 20.0 && step_crossed(old_phase, vm.bob_phase) {
            let sprinting = fusion.gait == Gait::Sprint;
            emit(Sfx::at(
                gear_alias(fusion.stance, sprinting, speed_ips),
                0.7,
            ));
        }
        if fusion.landed_at != vm.last_landed {
            vm.last_landed = fusion.landed_at;
            if t - fusion.landed_at < 0.2 && t - vm.land_sound_at > 1.0 {
                vm.land_sound_at = t;
                emit(Sfx::new("gear_rattle_plr_land"));
            }
        }
        if input.jump.pressed && hint.show && fusion.stance == Stance::Stand {
            emit(Sfx::new("gear_rattle_plr_mantle"));
        }
        let bob = aimed_bob(
            smooth_bob(
                fusion.stance,
                fusion.gait == Gait::Sprint,
                vm.ads,
                speed_ips,
                vm.bob_phase,
            ),
            vm.ads,
        );
        let land_ms = ((t - fusion.landed_at) * 1000.0) as i32;
        let dip = if land_ms >= 0 {
            weapon_iw4::viewweapon_land_origin_z(land_ms, -LAND_DIP_INCHES)
        } else {
            0.0
        } - vault_down;
        // MW2's stance placement: the crouch and prone offsets, and the crawl's tilt.
        let stance_top = mw2_speed(7.0 * stance_speed_scale(fusion.stance));
        let (place_origin, place_angles) = stance_placement(
            &mut vm.placement_state,
            &vm.weapon.placement,
            fusion.stance,
            vm.ads,
            speed_ips,
            stance_top,
            dt,
        );
        // MW2's axes are forward, left, up; Bevy's camera is right, up, back (looks down -Z).
        // MW2's pitch is nose-down positive and its yaw turns left.
        let (place_origin, place_angles) = aimed_placement(place_origin, place_angles, vm.ads);
        let spread_apart = dual_offset(vm.weapon.hand, vm.weapon.dual_offset, vm.weapon.akimbo());
        let place_origin = [0, 1, 2].map(|k| place_origin[k] + spread_apart[k]);
        let offset = Vec3::new(-place_origin[1], place_origin[2] + dip, -place_origin[0]);
        let sway = Mat4::from_translation(offset)
            * Mat4::from_rotation_x((-(bob.pitch + place_angles[0] + vault_tip)).to_radians())
            * Mat4::from_rotation_y((bob.yaw + place_angles[1]).to_radians())
            * Mat4::from_rotation_z((bob.roll + place_angles[2]).to_radians());
        let eye_from_world = sway * tag_view_to_camera() * view.inverse();
        // The melee weapon up: where it is this frame, and the arms reaching for it.
        vm.melee_grip = None;
        vm.left_grip = None;
        vm.glows = [None; 2];
        if held.0 && vm.weapon.hand == 0 {
            // Only a shield blocks (the right button, crate::hands).
            let goal = if input.ads.held && hands.left_shield() {
                1.0
            } else {
                0.0
            };
            vm.melee_block += (goal - vm.melee_block).clamp(-dt / BLOCK_SECS, dt / BLOCK_SECS);
            let two = melee_weapon.two_handed;
            let fists = melee_weapon.subclass == 255;
            let axe = hand_anim::is_axe_or_hammer(melee_weapon.subclass);
            let dual = hands.left == crate::hands::Hand::Weapon && !two;
            let carry = |g: melee_pose::Grip| melee_pose::Grip {
                at: sway.transform_point3(g.at),
                blade: sway.transform_vector3(g.blade).normalize(),
                edge: sway.transform_vector3(g.edge).normalize(),
                hand: g.hand,
            };
            // Into and out of the sprint's carry smoothly.
            let sprint_goal = if vm.sprint_started.is_some() {
                1.0
            } else {
                0.0
            };
            vm.melee_sprint += (sprint_goal - vm.melee_sprint)
                .clamp(-dt / SPRINT_BLEND_SECS, dt / SPRINT_BLEND_SECS);
            let right_spell = matches!(hands.right, crate::hands::Hand::Spell { .. });
            let left_spell = matches!(hands.left, crate::hands::Hand::Spell { .. });
            // What the hands hold and do (crate::hand_anim::compose): the tracks' own first-person
            // clips, the hand-made keys (crate::melee_pose) where a clip is missing or with
            // MW2_HANDS_HANDMADE set.
            let mut now = hand_anim::HandsNow {
                two,
                fists,
                axe,
                dual,
                right_spell,
                left: hands.left,
                t,
                drawn: vm.melee_drawn,
                sprint: vm.melee_sprint,
                block: vm.melee_block,
                swing: None,
                left_swing: None,
                cast: [0, 1]
                    .map(|side| hands.cast_at[side].map(|at| (t - at) / melee_pose::CAST_SECS)),
                handmade: std::env::var_os("MW2_HANDS_HANDMADE").is_some(),
                tool: systems.tool_out().map_or(0, |a| a.id() as u8),
                relaxed: {
                    vm.relax = hand_anim::relax_step(vm.relax, hands.relaxed, dt);
                    vm.relax
                },
            };
            // Drawn on V.
            vm.melee_drawn =
                (vm.melee_drawn + dt / hand_anim::draw_secs(&now, &mut clips)).min(1.0);
            now.drawn = vm.melee_drawn;
            now.swing = vm
                .melee_swing
                .filter(|&(_, at, secs)| t - at < secs)
                .map(|(m, at, secs)| (m, (t - at) / secs, secs))
                .or_else(|| probe_melee_pose(t).map(|(m, k)| (m, k, 0.5)))
                .and_then(|(m, k, secs)| match m {
                    melee_pose::Motion::Swing { from_right } => Some((from_right, k, secs)),
                    _ => None,
                });
            now.left_swing = vm
                .left_swing
                .filter(|&(_, at, secs)| t - at < secs)
                .map(|(from_right, at, secs)| (from_right, (t - at) / secs, secs));
            let composed = hand_anim::compose(&now, &mut clips);
            let (right, left) = vm.melee_settle.apply(t, composed);
            let right = carry(right);
            let left = left.map(carry);
            if right_spell {
                vm.glows[0] = Some((melee_pose::glow_at(&right), hands.school[0]));
            }
            if let (true, Some(l)) = (left_spell, left) {
                vm.glows[1] = Some((melee_pose::glow_at(&l), hands.school[1]));
            }
            let parents: Vec<Option<usize>> = vm.dobj.bones.iter().map(|b| b.parent).collect();
            let (mut right, mut left) = (right, left);
            hold_melee(
                &mut world,
                &parents,
                vm.arm_joints,
                eye_from_world,
                &mut right,
                left.as_mut(),
                &hand_anim::hold(&now),
            );
            // Each spell's glow in its hand, where the hand's shape holds one (the tracks'
            // `MagicNode`), on the hand as the arms reached it.
            let at = |i: usize| eye_from_world.transform_point3(world[i].w_axis.truncate());
            for (side, grip) in [(0, Some(right)), (1, left)] {
                if let (Some((_, school)), Some(j), Some(g)) =
                    (vm.glows[side], vm.arm_joints[side], grip)
                {
                    vm.glows[side] = Some((g.hand.magic_at(at(j[2]), at(j[3]), at(j[6])), school));
                }
            }
            vm.melee_grip = Some(right);
            vm.left_grip = left;
        } else {
            vm.melee_swing = None;
            vm.left_swing = None;
            vm.melee_block = 0.0;
            vm.melee_drawn = 0.0;
            vm.melee_sprint = 0.0;
            vm.melee_settle = hand_anim::Settle::default();
        }
        let skin = vm.dobj.skin_matrices(&world);
        vm.pose.clone_from(&world);
        vm.eye_from_world = eye_from_world;
        if std::env::var_os("MW2_PROBE_MELEE").is_some()
            && (t * 4.0).fract() < dt * 4.0
            && let (Some(g), Some(j)) = (vm.melee_grip, vm.arm_joints[0])
        {
            let wrist = eye_from_world.transform_point3(world[j[2]].w_axis.truncate());
            info!(
                "probe: melee grip {g:?} right wrist {wrist:?} swing {:?}",
                vm.melee_swing
            );
        }
        if std::env::var_os("MW2_PROBE_ADS").is_some()
            && vm.ads > 0.0
            && (t * 2.0).fract() < dt * 2.0
        {
            info!(
                "probe: muzzle in eye (in) {:?} sway {:?} bob {:?} place {:?} {:?}",
                vm.muzzle_eye().map(|v| v / INCHES_TO_YARDS),
                offset,
                (bob.pitch, bob.yaw, bob.roll),
                place_origin,
                place_angles
            );
        }
        // Every bone's eye-space skinning matrix, once per frame, in both forms the skin reads.
        let bones: Vec<(Mat4, [f32; 16])> = skin
            .iter()
            .map(|m| {
                let m = eye_from_world * *m;
                (m, m.to_cols_array())
            })
            .collect();
        let fallback = (eye_from_world, eye_from_world.to_cols_array());

        for (part_index, part) in vm.weapon.parts.iter().enumerate() {
            let base = vm.bone_base[part_index];
            let skel = &part.skel;
            let bone = |b: u16| bones.get(base + usize::from(b)).unwrap_or(&fallback);
            for (surface, handle) in part.surfaces.iter().zip(&vm.meshes[part_index]) {
                let Some(mesh) = meshes.get_mut(handle) else {
                    continue;
                };
                let (v0, vn) = surface.vertices;
                let mut positions = Vec::with_capacity(vn);
                let mut colors = Vec::with_capacity(vn);
                for i in v0..v0 + vn {
                    let p = Vec3::from_array(skel.positions[i]);
                    let n =
                        Vec3::from_array(skel.normals.get(i).copied().unwrap_or([0.0, 0.0, 1.0]));
                    let s = skel.vert_skin.get(i).copied().unwrap_or_default();
                    let (primary, primary_cols) = bone(s.bones[0]);
                    // A rigid vertex is one bone; a blended one goes through MW2's own weighted
                    // skin (`dpvs_iw4`), as IW4L's `skin_packed_into` does.
                    let posed = if s.weights[1..].iter().all(|w| *w <= 0.0) {
                        primary.transform_point3(p)
                    } else {
                        let mut extras = [(primary_cols, 0u16); 3];
                        let mut count = 0;
                        for k in 1..4 {
                            if s.weights[k] > 0.0 {
                                extras[count] = (&bone(s.bones[k]).1, s.weight_u16[k]);
                                count += 1;
                            }
                        }
                        Vec3::from_array(dpvs_iw4::skin_packed_weighted_point(
                            p.to_array(),
                            primary_cols,
                            &extras[..count],
                        ))
                    };
                    let normal = primary.transform_vector3(n).normalize_or_zero();
                    positions.push(posed.to_array());
                    colors.push(view_light.lit(normal));
                }
                mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
                mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
            }
        }
    }
}

/// The scene's light as the gun sees it: WoW's ambient and sun (`Light.dbc`, as benilla lights
/// its models: `clamp(ambient + diffuse · max(N·L, 0))`), the sun turned into the eye's frame.
/// Without a scene light, the layer's own key light.
#[derive(Resource, Debug, Clone, Copy, PartialEq)]
pub struct ViewLight {
    pub ambient: Vec3,
    pub diffuse: Vec3,
    /// Toward the sun, in the eye's frame.
    pub to_sun: Vec3,
}

impl Default for ViewLight {
    fn default() -> Self {
        Self {
            ambient: Vec3::splat(0.45),
            diffuse: Vec3::splat(0.55),
            to_sun: KEY_LIGHT.normalize(),
        }
    }
}

impl ViewLight {
    /// From the scene's light (`sun_dir` the way the sunlight travels, in the world) and the
    /// camera's rotation.
    pub fn from_scene(ambient: Vec3, diffuse: Vec3, sun_dir: Vec3, camera: Quat) -> Self {
        Self {
            ambient,
            diffuse,
            to_sun: (camera.inverse() * -sun_dir).normalize_or_zero(),
        }
    }

    /// A vertex colour for a normal in the eye's frame.
    pub fn lit(&self, normal: Vec3) -> [f32; 4] {
        let c = (self.ambient + self.diffuse * normal.dot(self.to_sun).max(0.0))
            .clamp(Vec3::ZERO, Vec3::ONE);
        [c.x, c.y, c.z, 1.0]
    }
}

/// How far (yd) the eye looks toward the sun for a caster: the Medium shadow reach.
const SUN_SHADE_REACH: f32 = 150.0;
/// How fast the gun's sun share follows the eye in and out of shade (per second).
const SUN_SHADE_RATE: f32 = 8.0;

/// The share of the sun the eye stands in: 0 when a collider (terrain, a building, a hulled
/// doodad) lies between it and the sun, 1 in the open. With the atmosphere's shadows on, the world
/// around the eye darkens under a roof; the gun is not in the shadow maps (it would cast onto the
/// world from the eye), so it takes the eye's own verdict instead.
fn eye_in_sun(spatial: &avian3d::prelude::SpatialQuery, eye: Vec3, sun_dir: Vec3) -> f32 {
    let Ok(to_sun) = Dir3::new(-sun_dir) else {
        return 1.0;
    };
    let filter = benilla_world::collision::WorldCollision::body_filter();
    if spatial
        .cast_ray(eye, to_sun, SUN_SHADE_REACH, true, &filter)
        .is_some()
    {
        0.0
    } else {
        1.0
    }
}

/// This frame's view light from benilla's scene light and the world camera.
pub fn update_view_light(
    lighting: Option<Res<benilla_world::lighting::WowLighting>>,
    atmosphere: Option<Res<benilla_world::atmosphere::AtmosphereSettings>>,
    spatial: avian3d::prelude::SpatialQuery,
    time: Res<Time>,
    // The camera's Transform, final for this frame: its GlobalTransform is copied early by
    // benilla, before or after the recoil and sway land, and the gun's shading flickered.
    cameras: Query<&Transform, With<WorldCamera>>,
    mut light: ResMut<ViewLight>,
    mut sun_share: Local<Option<f32>>,
    objects: benilla_app::Objects,
    indoor: Query<&benilla_world::interior::ParticleLight>,
) {
    let (Some(l), Some(cam)) = (lighting, cameras.iter().next()) else {
        return;
    };
    // The atmosphere's shadows reach the gun as the eye's own sun share, eased so a step into a
    // doorway dims it rather than snapping.
    let strength = atmosphere.map_or(0.0, |a| a.shadow_strength());
    let share = if strength > 0.0 {
        let target = eye_in_sun(&spatial, cam.translation, l.sun_dir);
        let k = 1.0 - (-SUN_SHADE_RATE * time.delta_secs()).exp();
        let s = sun_share.map_or(target, |s| s + (target - s) * k);
        let s = if (s - target).abs() < 1e-3 { target } else { s };
        *sun_share = Some(s);
        1.0 - strength * (1.0 - s)
    } else {
        *sun_share = None;
        1.0
    };
    // Indoors in a lit room the gun takes the body's own room light (benilla's `ParticleLight`,
    // the term the character's model is lit by there), not the day/night sky's.
    let room = objects
        .player()
        .and_then(|me| indoor.get(me).ok())
        .map(|p| Vec3::from_array(p.0));
    let (ambient, diffuse) = match room {
        Some(c) => (c * 0.7, c * 0.3),
        None => (Vec3::from_array(l.ambient), Vec3::from_array(l.diffuse) * share),
    };
    let next = ViewLight::from_scene(ambient, diffuse, l.sun_dir, cam.rotation);
    if *light != next {
        *light = next;
    }
}

impl Viewmodel {
    /// Which hand's gun this is (akimbo: 1 the left), and whether it is half of a pair.
    pub fn hand(&self) -> (u8, bool) {
        (self.weapon.hand, self.weapon.akimbo())
    }

    /// The weapon this viewmodel shows (`m4_heartbeat_mp`): the one still in hand through a
    /// swap, when the loadout already names the next.
    pub fn weapon_name(&self) -> &str {
        &self.weapon.name
    }

    /// The muzzle in the eye's frame (the world camera's local space, yards), this frame: the
    /// viewmodel works in MW2's inches under a root scaled to yards.
    pub fn muzzle_eye(&self) -> Option<Vec3> {
        let m = self.pose.get(self.flash_bone?)?;
        Some((self.eye_from_world * *m).w_axis.truncate() * INCHES_TO_YARDS)
    }

    /// The first bone whose name holds `part` (`heartbeat`, `j_gun`).
    pub fn bone_named(&self, part: &str) -> Option<usize> {
        self.dobj
            .bones
            .iter()
            .position(|b| b.name.to_ascii_lowercase().contains(part))
    }

    /// A bone's matrix in the viewmodel root's frame (MW2 inches), this frame.
    pub fn bone_in_root(&self, bone: usize) -> Option<Mat4> {
        Some(self.eye_from_world * *self.pose.get(bone)?)
    }

    /// Where the melee weapon is this frame (the eye's frame, inches), while it is up.
    pub fn melee_grip(&self) -> Option<melee_pose::Grip> {
        self.melee_grip
    }

    /// The knife hand's wrist bone (MW2 knifes with the left hand, the gun dropped in the right),
    /// and whether a knife swing is playing.
    pub fn knife_hand(&self) -> (Option<usize>, bool) {
        let swinging = self
            .one_shot
            .is_some_and(|p| matches!(p.slot, anim::MELEE | anim::MELEE_CHARGE));
        (self.arm_joints[1].map(|j| j[2]), swinging)
    }
}

/// `MW2_PROBE_MELEE_POSE=<right|left|power|block|ready>:<0..1>,...`: hold the melee weapon
/// still that far through a swing (or the block), to see it; a list takes each in turn for
/// four seconds.
fn probe_melee_pose(t: f32) -> Option<(melee_pose::Motion, f32)> {
    let v = std::env::var("MW2_PROBE_MELEE_POSE").ok()?;
    let poses: Vec<&str> = v.split(',').collect();
    let (what, k) = poses[(t / 4.0) as usize % poses.len()].split_once(':')?;
    let motion = match what {
        "right" => melee_pose::Motion::Swing { from_right: true },
        "left" => melee_pose::Motion::Swing { from_right: false },
        "power" => melee_pose::Motion::Power,
        "block" => melee_pose::Motion::Block,
        _ => melee_pose::Motion::Ready,
    };
    Some((motion, k.parse().ok()?))
}

/// Seconds the weapon takes to go to the hip as a sprint starts, and back.
pub(crate) const SPRINT_BLEND_SECS: f32 = 0.25;

/// Seconds the block takes to come up or go down.
pub(crate) const BLOCK_SECS: f32 = 0.15;

/// In the idle the right fist holds the pistol grip (its axis up, the barrel ahead) and the
/// left the handguard (its axis ahead, the gun's top up): each hand keeps that grip, turned onto
/// the weapon's hilt.
const IDLE_GRIP: [(Vec3, Vec3); 2] = [
    (Vec3::new(0.0, 0.95, 0.3), Vec3::NEG_Z),
    (Vec3::NEG_Z, Vec3::NEG_Y),
];
/// The elbows bend down and out.
const ELBOW_POLE: [Vec3; 2] = [Vec3::new(0.7, -1.0, 0.3), Vec3::new(-0.7, -1.0, 0.3)];
/// Where the shoulders are while a melee weapon, a spell or a shield is up (eye frame, inches):
/// a body's, beside and below the eye and a little behind it. A gun's idle puts them anywhere
/// its animator needed (a rifle's right shoulder a foot behind the eye for the stock, a launcher's
/// or a pump shotgun's before it), and from there the arm reaching a hand before the face could
/// only cross the near plane in view, or leave its open end, cut from the body, on screen.
pub(crate) const MELEE_SHOULDERS: [Vec3; 2] =
    [Vec3::new(7.5, -7.5, 4.0), Vec3::new(-7.5, -7.5, 4.0)];
/// How near the eye a wrist may come (inches deep), eased over the next [`WRIST_SOFT`]: a swing
/// carried across the face moves forward just enough to pass it.
pub(crate) const WRIST_CLEAR: f32 = 10.0;
pub(crate) const WRIST_SOFT: f32 = 4.0;
/// How far from the eye the line from the shoulder to the wrist passes, at least (inches), past
/// the arm's girth.
pub(crate) const ARM_LINE_CLEAR: f32 = 2.0;
/// What the arms keep clear of (crate::melee_pose::clear_pole): the near plane the layer draws
/// first person with (crate::fp_body::FP_NEARCLIP, inches) and its window at the stock 45 degree
/// field on a 21:9 screen; the girth is the arm's, set per hand ([`ARM_GIRTH_PER_HAND`]).
pub(crate) const ARM_NEAR_WINDOW: melee_pose::NearWindow = melee_pose::NearWindow {
    near: crate::fp_body::FP_NEARCLIP * 36.0,
    half_w: crate::fp_body::FP_NEARCLIP * 36.0 * 1.0355 + 1.0,
    half_h: crate::fp_body::FP_NEARCLIP * 36.0 * 0.4557 + 1.0,
    girth: 0.0,
};
/// A character's arm girth (its widest vertex off the upper arm) against its hand's reach, at the
/// widest of the races measured (`examples/wow_arm_cut.rs`: human 0.64, dwarf 0.69, tauren 0.75,
/// orc 0.79): the WoW arm is drawn at the hand's scale, so MW2's hand gives its girth.
pub(crate) const ARM_GIRTH_PER_HAND: f32 = 0.8;

/// How far round the hilt from the shoulder's side a wrist may sit (degrees, seen down the hilt).
pub(crate) const WRIST_SIDE_MOST: f32 = 75.0;

/// How far a forearm may be turned (degrees): the thumb's side against the elbow's inside, 0 a
/// handshake's thumb up; the palm turns about 90 either way from there.
pub(crate) const FOREARM_ROLL_MOST: f32 = 80.0;

/// How far an arm reaches, as a share of its length, before its reach is eased ([`soft_reach`]),
/// and the most it reaches: the tracks' arms are longer than the character's, and its strikes and
/// casts reach to 30 inches before the eye; the arm locked straight and the weapon drawn there
/// floated up to 16 inches beyond the fist.
const REACH_KNEE: f32 = 0.8;
const REACH_MOST: f32 = 0.97;

/// A wrist target `t` brought within an arm `len` long from the shoulder `s`: untouched within
/// [`REACH_KNEE`] of its length, eased toward [`REACH_MOST`] beyond, never past it.
pub(crate) fn soft_reach(s: Vec3, t: Vec3, len: f32) -> Vec3 {
    let d = t.distance(s);
    let (knee, most) = (len * REACH_KNEE, len * REACH_MOST);
    if d <= knee || d < 1e-4 {
        return t;
    }
    let eased = knee + (most - knee) * (1.0 - (-(d - knee) / (most - knee)).exp());
    s + (t - s) * (eased / d)
}

/// Reach the arms (in the skeleton's frame, `world`) for the melee weapon held at `g` (the eye's
/// frame): the right fist on the grip, the left below it on a two-hander (moved as the right fist
/// was) or down out of sight. Each grip is left where its fist ended: the weapon stays in the
/// hand. `how` says how each hand holds what it holds (crate::hand_anim::Hold).
pub(crate) fn hold_melee(
    world: &mut [Mat4],
    parents: &[Option<usize>],
    arms: [Option<[usize; 9]>; 2],
    eye_from_world: Mat4,
    right: &mut melee_pose::Grip,
    mut left: Option<&mut melee_pose::Grip>,
    how: &hand_anim::Hold,
) {
    let world_from_eye = eye_from_world.inverse();
    let eye_turn = Quat::from_mat4(&eye_from_world).normalize();
    let right_asked = right.at;
    for (side, joints) in arms.iter().enumerate() {
        let Some(j) = joints else {
            continue;
        };
        let at = |w: &[Mat4], i: usize| eye_from_world.transform_point3(w[i].w_axis.truncate());
        // The arm hung from a body's shoulder.
        let shift = world_from_eye.transform_vector3(MELEE_SHOULDERS[side] - at(world, j[0]));
        let m = Mat4::from_translation(shift);
        for (w, under) in world.iter_mut().zip(melee_pose::subtree(parents, j[0])) {
            if under {
                *w = m * *w;
            }
        }
        // A two-hander's left hand (on the hilt below the right, crate::hand_anim::compose)
        // moved as the right fist was.
        if side == 1
            && how.two_handed
            && let Some(l) = left.as_deref_mut()
        {
            l.at += right.at - right_asked;
        }
        let (wrist, knuckle) = (at(world, j[2]), at(world, j[3]));
        let (sh, el) = (at(world, j[0]), at(world, j[1]));
        let window = melee_pose::NearWindow {
            girth: ARM_GIRTH_PER_HAND * wrist.distance(at(world, j[5])),
            ..ARM_NEAR_WINDOW
        };
        // The hand's turn in the idle, in the eye's frame.
        let idle_hand = eye_turn * Quat::from_mat4(&world[j[2]]).normalize();
        // The fist's middle, a little short of the knuckles.
        let fist = wrist.lerp(knuckle, 0.9);
        // The hand's grip; an empty left hand hangs down out of sight, turned as it was.
        let grip = if side == 0 {
            Some(*right)
        } else {
            left.as_deref().copied()
        };
        let g = grip.unwrap_or(*right);
        let fist_at = match grip {
            Some(g) => g.at,
            None => melee_pose::off_hand(right, false, hand_anim::HILT_SPAN),
        };
        // The fist round the hilt, the wrist where the tracks' is from its grip: the hilt lies
        // across the palm, the wrist behind it and down it. The gun's pistol grip alone put the
        // wrist up the hilt, the hand bent 90 degrees off the forearm at the ready.
        let turn = melee_pose::weapon_turn(IDLE_GRIP[side].0, IDLE_GRIP[side].1, &g);
        let turn = melee_pose::wrist_toward(turn, &g, turn * (wrist - fist), how.wrists[side]);
        // ...but never on the hilt's far side from the shoulder: a two-hander's left hand at the
        // end of a swing across the body had its hand folded back 170 degrees.
        let turn = melee_pose::wrist_within(
            turn,
            g.blade,
            turn * (wrist - fist),
            sh - fist_at,
            WRIST_SIDE_MOST,
        );
        let reach_for = fist_at + turn * (wrist - fist);
        // Kept off the face, within the arm's reach, and the arm to it clear of the eye; the
        // hand's grip (and what it holds) moved with it.
        let mut target = reach_for;
        target.z = melee_pose::ease_depth(target.z, WRIST_CLEAR, WRIST_SOFT);
        let target = soft_reach(sh, target, sh.distance(el) + el.distance(wrist));
        // The elbow down and out, turned only as far as keeps the arm out of the near plane.
        let (target, pole) = melee_pose::clear_reach(
            sh,
            target,
            sh.distance(el),
            el.distance(wrist),
            ELBOW_POLE[side],
            &window,
            ARM_LINE_CLEAR + window.girth,
        );
        let moved = target - reach_for;
        match (side, grip) {
            (0, _) => right.at += moved,
            (_, Some(_)) => {
                if let Some(l) = left.as_deref_mut() {
                    l.at += moved;
                }
            }
            _ => {}
        }
        let pole = world_from_eye.transform_vector3(pole);
        let arm = [j[0], j[1], j[2]];
        melee_pose::reach(
            world,
            parents,
            arm,
            world_from_eye.transform_point3(target),
            pole,
        );
        // The hand turned onto the hilt: the idle's grip, turned as the weapon is (a
        // one-hander's off hand hangs as the arm left it).
        if grip.is_some() {
            let now = eye_turn * Quat::from_mat4(&world[j[2]]).normalize();
            let wanted = turn * idle_hand;
            let delta = eye_turn.inverse() * (wanted * now.inverse()) * eye_turn;
            let pivot = world[j[2]].w_axis.truncate();
            melee_pose::turn_bones(world, &melee_pose::subtree(parents, j[2]), delta, pivot);
            // A hand round no hilt (a spell, a fist, a shield's strap) turns on its own length
            // until the forearm is wrung no further than a forearm turns.
            if how.free[side] {
                let p = |w: &[Mat4], i: usize| at(w, j[i]);
                let (s, e, w, k, th) = (
                    p(world, 0),
                    p(world, 1),
                    p(world, 2),
                    p(world, 3),
                    p(world, 6),
                );
                let roll = melee_pose::forearm_unwring(s, e, w, k, th, FOREARM_ROLL_MOST);
                if roll != Quat::IDENTITY {
                    let turn = eye_turn.inverse() * roll * eye_turn;
                    let pivot = world[j[2]].w_axis.truncate();
                    melee_pose::turn_bones(world, &melee_pose::subtree(parents, j[2]), turn, pivot);
                }
            }
            // The weapon in the fist as the arm left it (an arm short of its target, a moment's
            // slack in the reach).
            let held = at(world, j[2]).lerp(at(world, j[3]), 0.9);
            match side {
                0 => right.at = held,
                _ => {
                    if let Some(l) = left.as_deref_mut() {
                        l.at = held;
                    }
                }
            }
        }
    }
}

/// The gun's surfaces hide while the melee weapon is up (crate::melee::MeleeHeld), and a
/// launcher's round (part 2) shows only while one is loaded, going in or leaving the tube.
pub fn hide_gun_for_melee(
    time: Res<Time>,
    held: Res<crate::melee::MeleeHeld>,
    weapon: Res<Weapon>,
    mut parts: Query<(&ViewmodelPart, &mut Visibility)>,
) {
    let round = round_shown(
        weapon.mag,
        weapon.reloading(),
        time.elapsed_secs() - weapon.last_shot,
    );
    for (part, mut vis) in &mut parts {
        let shown = match part.0 {
            0 => true,
            1 => !held.0,
            _ => !held.0 && round,
        };
        let want = if shown {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
    }
}

/// Seconds a launched round stays drawn on its way out of the tube (the fire clip carries it).
const ROUND_LEAVES_SECS: f32 = 0.5;

/// A launcher's round is drawn while the tube holds one, through a reload, and for the moment
/// after a shot while the fire clip carries it out.
pub fn round_shown(mag: u32, reloading: bool, since_shot: f32) -> bool {
    mag > 0 || reloading || since_shot < ROUND_LEAVES_SECS
}

/// The heartbeat sensor's screen on the gun: crate::heartbeat draws the radar on it.
#[derive(Component)]
pub struct MotionTrackerScreen;

/// A drawn surface of a viewmodel part (0 the arms, 1 the gun).
#[derive(Component)]
pub struct ViewmodelPart(pub usize);

/// Keep the root under the live world camera (benilla respawns it with the world).
pub fn world_camera(cameras: &Query<Entity, With<WorldCamera>>) -> Option<Entity> {
    cameras.iter().next()
}

/// The clip for a shot: aimed, MW2's aimed fire (its last-shot clip on the last round); at the
/// hip, the fire or last-shot clip. Aimed with no aimed clips, none: the hip clip would throw the
/// gun off the aim, and MW2 kicks the gun instead.
pub fn fire_slot(ads: f32, empty: bool, has: impl Fn(usize) -> bool) -> Option<usize> {
    let (fire, last) = if ads > 0.5 {
        (anim::ADS_FIRE, anim::ADS_LASTSHOT)
    } else {
        (anim::FIRE, anim::LAST_SHOT)
    };
    if empty && has(last) {
        Some(last)
    } else {
        Some(fire).filter(|&s| has(s))
    }
}

/// A shot's clip (played over the base pose rather than in place of it).
/// Whether a viewmodel is a thrown weapon's (a grenade, a knife, a charge) rather than a gun.
pub fn is_thrown_weapon(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.contains("grenade")
        || ["semtex", "throwingknife", "c4", "claymore"]
            .iter()
            .any(|w| n.starts_with(w))
}

/// A grenade in hand's clip and its time: while cooking, the pin pulled and the arm held back
/// (`HOLD_FIRE`, held at its end); just after a throw, the throw (`FIRE`). Otherwise none, and
/// the idle plays. A clip the weapon lacks plays nothing.
pub fn grenade_clip(
    t: f32,
    cooking: Option<f32>,
    thrown_at: Option<f32>,
    hold_secs: Option<f32>,
    throw_secs: Option<f32>,
) -> Option<(usize, f32)> {
    if let (Some(at), Some(d)) = (cooking, hold_secs) {
        return Some((anim::HOLD_FIRE, (t - at).clamp(0.0, d)));
    }
    if let (Some(at), Some(d)) = (thrown_at, throw_secs)
        && t >= at
        && t - at < d
    {
        return Some((anim::FIRE, t - at));
    }
    None
}

pub fn is_fire_slot(slot: usize) -> bool {
    matches!(
        slot,
        anim::FIRE | anim::LAST_SHOT | anim::ADS_FIRE | anim::ADS_LASTSHOT
    )
}

/// How far a hand's gun sits off centre for akimbo (MW2's axes: forward, left, up; inches):
/// `dual_wield_view_model_origin_add`, the left gun twice as far to its side. Only a pair
/// spreads: a single gun's definition can carry a spacing it never uses (the M4's 7 inches).
pub fn dual_offset(hand: u8, offset: f32, akimbo: bool) -> [f32; 3] {
    if !akimbo || offset == 0.0 {
        return [0.0; 3];
    }
    let side = if hand == 0 {
        [0.0, -1.0, 0.0]
    } else {
        [0.0, 1.0, 0.0]
    };
    weapon_iw4::dual_wield_view_model_origin_add(i32::from(hand), side, offset)
}

/// Whether a session counter (shots, swings, clicks) moved since this gun last looked. A new gun
/// starts unset and takes the count as it finds it: the shots before the swap are not its own.
fn counted(seen: &mut Option<u64>, now: u64) -> bool {
    let moved = seen.is_some_and(|s| s != now);
    *seen = Some(now);
    moved
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_grenade_pulls_its_pin_while_cooking_and_throws_on_release() {
        use super::{anim, grenade_clip, is_thrown_weapon};
        assert!(is_thrown_weapon("frag_grenade_mp"));
        assert!(is_thrown_weapon("semtex_mp|"));
        assert!(!is_thrown_weapon("m4_mp"));
        let (hold, throw) = (Some(0.6), Some(0.8));
        // Cooking: the pullback, held at its end however long the cook.
        let (slot, at) = grenade_clip(10.2, Some(10.0), None, hold, throw).unwrap();
        assert_eq!(slot, anim::HOLD_FIRE);
        assert!((at - 0.2).abs() < 1e-3);
        assert_eq!(grenade_clip(13.0, Some(10.0), None, hold, throw), Some((anim::HOLD_FIRE, 0.6)));
        // Released: the throw, then nothing (the idle) once it is over.
        let (slot, at) = grenade_clip(20.3, None, Some(20.0), hold, throw).unwrap();
        assert_eq!(slot, anim::FIRE);
        assert!((at - 0.3).abs() < 1e-4);
        assert_eq!(grenade_clip(21.0, None, Some(20.0), hold, throw), None);
        // A grenade without a pullback clip just throws.
        assert_eq!(grenade_clip(10.2, Some(10.0), None, None, throw), None);
    }

    #[test]
    fn the_gun_is_lit_by_the_scene() {
        use super::ViewLight;
        // Noon: the sun straight down onto a face looking up.
        let noon = ViewLight {
            ambient: Vec3::splat(0.3),
            diffuse: Vec3::splat(0.6),
            to_sun: Vec3::Y,
        };
        let near = |a: [f32; 4], b: [f32; 4]| a.iter().zip(b).all(|(x, y)| (x - y).abs() < 1e-5);
        assert!(near(noon.lit(Vec3::Y), [0.9, 0.9, 0.9, 1.0]));
        assert!(near(noon.lit(Vec3::NEG_Y), [0.3, 0.3, 0.3, 1.0])); // facing away: the ambient
        // Clamped like WoW's.
        let bright = ViewLight {
            ambient: Vec3::splat(0.8),
            diffuse: Vec3::splat(0.8),
            to_sun: Vec3::Y,
        };
        assert_eq!(bright.lit(Vec3::Y), [1.0, 1.0, 1.0, 1.0]);
        // The world's sun as the eye sees it: turned by the camera's rotation.
        let cam = Quat::from_rotation_y(std::f32::consts::FRAC_PI_2);
        let v = ViewLight::from_scene(Vec3::ZERO, Vec3::ONE, Vec3::NEG_X, cam);
        assert!((v.to_sun - cam.inverse() * Vec3::X).length() < 1e-6);
    }

    #[test]
    fn a_launchers_round_shows_while_loaded() {
        use super::round_shown;
        assert!(round_shown(1, false, 10.0)); // in the tube
        assert!(round_shown(0, false, 0.1)); // on its way out
        assert!(!round_shown(0, false, 2.0)); // fired: the tube is empty
        assert!(round_shown(0, true, 2.0)); // the reload brings the next
    }

    #[test]
    fn akimbo_spreads_the_guns_apart() {
        use super::dual_offset;
        assert_eq!(dual_offset(0, 0.0, true), [0.0; 3]); // no spacing: where it was
        let right = dual_offset(0, 4.0, true);
        let left = dual_offset(1, 4.0, true);
        assert!(right[1] < 0.0 && left[1] > 0.0); // MW2's axes: +y is left
        assert_eq!((right[0], right[2], left[0], left[2]), (0.0, 0.0, 0.0, 0.0));
    }

    #[test]
    fn a_single_gun_ignores_its_akimbo_spacing() {
        use super::dual_offset;
        // The M4's definition carries a 7 inch spacing for a dual wield it never uses: the gun
        // stays centred, so its sights line up when aimed.
        assert_eq!(dual_offset(0, 7.0, false), [0.0; 3]);
    }

    #[test]
    fn the_bob_moves_smoothly_between_cycle_steps() {
        use super::smooth_bob;
        let at = |c: f32| smooth_bob(Stance::Stand, false, 0.0, 150.0, c);
        let (a, mid, b) = (at(40.0), at(40.5), at(41.0));
        let lo = a.pitch.min(b.pitch);
        let hi = a.pitch.max(b.pitch);
        assert!(mid.pitch >= lo - 1e-4 && mid.pitch <= hi + 1e-4);
        assert!((at(255.5).pitch - at(255.5).pitch).abs() < 1e-6); // wraps without a panic
    }

    #[test]
    fn aiming_drops_the_stance_offsets_and_the_bob() {
        use super::{aimed_bob, aimed_placement};
        let (o, a) = aimed_placement([1.0, 2.0, -3.0], [4.0, 0.0, 2.0], 0.0);
        assert_eq!((o, a), ([1.0, 2.0, -3.0], [4.0, 0.0, 2.0]));
        let (o, a) = aimed_placement([1.0, 2.0, -3.0], [4.0, 0.0, 2.0], 0.25);
        assert_eq!((o[0], a[0]), (0.5, 2.0)); // MW2 fades it by 1 - 2 * ads
        assert_eq!(
            aimed_placement([1.0; 3], [1.0; 3], 0.6),
            ([0.0; 3], [0.0; 3])
        );
        let bob = smooth_bob(Stance::Stand, false, 0.0, 150.0, 64.0);
        assert_eq!(aimed_bob(bob, 1.0).pitch, 0.0);
        assert_eq!(aimed_bob(bob, 0.0).pitch, bob.pitch);
    }

    #[test]
    fn an_aimed_shot_plays_the_aimed_clip_or_none() {
        use super::fire_slot;
        use crate::mw2::load::anim;
        let all = |_: usize| true;
        let no_ads = |s: usize| s != anim::ADS_FIRE && s != anim::ADS_LASTSHOT;
        assert_eq!(fire_slot(0.0, false, all), Some(anim::FIRE));
        assert_eq!(fire_slot(0.0, true, all), Some(anim::LAST_SHOT));
        assert_eq!(fire_slot(1.0, false, all), Some(anim::ADS_FIRE));
        assert_eq!(fire_slot(1.0, true, all), Some(anim::ADS_LASTSHOT));
        // Aimed without aimed clips: the gun kick alone, never the hip clip.
        assert_eq!(fire_slot(1.0, false, no_ads), None);
        assert!(super::is_fire_slot(anim::ADS_FIRE) && !super::is_fire_slot(anim::RELOAD));
    }

    #[test]
    fn a_new_gun_does_not_replay_old_events() {
        use super::counted;
        let mut seen = None;
        assert!(!counted(&mut seen, 7)); // a new gun: the session's shots so far are not its
        assert!(!counted(&mut seen, 7));
        assert!(counted(&mut seen, 8)); // its own shot
        assert!(!counted(&mut seen, 8));
    }

    use super::*;

    fn crawl_placement() -> super::super::load::Placement {
        super::super::load::Placement {
            prone_ofs: [0.0, 0.0, -1.0],
            ducked_ofs: [0.0, 0.5, -0.5],
            movement: fastfile_iw4::WeaponMovementOfsCapture {
                prone_move: [1.0, 2.0, -3.0],
                prone_rot: [5.0, 10.0, -20.0],
                pos_prone_move_rate: 10.0,
                pos_prone_rot_rate: 10.0,
                pos_move_rate: 10.0,
                pos_rot_rate: 10.0,
                ..Default::default()
            },
        }
    }

    #[test]
    fn prone_crawl_tilts_toward_the_weapons_prone_offsets() {
        let p = crawl_placement();
        let mut st = weapon_iw4::WeaponPlacementState::default();
        let mut out = ([0.0; 3], [0.0; 3]);
        for _ in 0..300 {
            out = stance_placement(&mut st, &p, Stance::Prone, 0.0, 30.0, 30.0, 0.016);
        }
        let (origin, angles) = out;
        for (a, b) in angles.iter().zip([5.0, 10.0, -20.0]) {
            assert!((a - b).abs() < 0.1, "angles {angles:?}");
        }
        // The prone offset plus the crawl's move.
        for (a, b) in origin.iter().zip([1.0, 2.0, -4.0]) {
            assert!((a - b).abs() < 0.1, "origin {origin:?}");
        }
    }

    #[test]
    fn still_crouched_settles_on_the_ducked_offset() {
        let p = crawl_placement();
        let mut st = weapon_iw4::WeaponPlacementState::default();
        let mut out = ([0.0; 3], [0.0; 3]);
        for _ in 0..300 {
            out = stance_placement(&mut st, &p, Stance::Crouch, 0.0, 0.0, 114.0, 0.016);
        }
        assert!(
            (out.0[1] - 0.5).abs() < 0.05 && (out.0[2] + 0.5).abs() < 0.05,
            "{out:?}"
        );
        assert!(out.1.iter().all(|a| a.abs() < 1e-3));
    }

    #[test]
    fn a_shotgun_reload_runs_start_shells_end() {
        let spas = ReloadTiming {
            secs: 0.667,
            segmented: true,
            start_secs: 1.0,
            end_secs: 1.0,
            per_segment: 1,
            ..Default::default()
        };
        assert_eq!(
            reload_segment(0.5, &spas, 3),
            (anim::RELOAD_START, 0.0, 1.0)
        );
        let (slot, began, secs) = reload_segment(1.0 + 0.667 * 1.5, &spas, 3);
        assert_eq!((slot, secs), (anim::RELOAD, 0.667));
        assert!((began - (1.0 + 0.667)).abs() < 1e-5); // the second shell
        let (slot, began, _) = reload_segment(3.5, &spas, 3);
        assert_eq!(slot, anim::RELOAD_END);
        assert!((began - 3.001).abs() < 1e-4);
    }

    #[test]
    fn a_reload_clip_is_stretched_to_the_weapons_time() {
        assert!((clip_rate(Some(0.87), 0.667) - 0.87 / 0.667).abs() < 1e-6);
        assert_eq!(clip_rate(None, 2.0), 1.0);
        assert_eq!(clip_rate(Some(2.0), 0.0), 1.0); // no time: as recorded
    }

    #[test]
    fn knife_clips() {
        assert_eq!(melee_slot(true, true), anim::MELEE_CHARGE); // the lunge stabs
        assert_eq!(melee_slot(false, true), anim::MELEE); // a plain knife slices
        assert_eq!(melee_slot(true, false), anim::MELEE); // no stab clip: slice
        assert_eq!((anim::MELEE, anim::MELEE_CHARGE), (7, 8));
    }

    #[test]
    fn bob_inputs_map_stances() {
        use benilla_app::fusion::Stance;
        assert_eq!(
            bob_inputs(Stance::Prone, false, 0.0, 50.0, 0).view_height_target,
            11
        );
        assert_eq!(
            bob_inputs(Stance::Crouch, false, 0.0, 50.0, 0).view_height_target,
            40
        );
        assert_eq!(
            bob_inputs(Stance::Stand, false, 0.0, 50.0, 0).view_height_target,
            60
        );
        let sprint = bob_inputs(Stance::Stand, true, 0.0, 50.0, 0);
        assert_ne!(sprint.pm_flags & playerstate_iw4::pm_flags::SPRINTING, 0);
    }

    #[test]
    fn still_has_no_bob() {
        use benilla_app::fusion::Stance;
        let b = weapon_bob(Stance::Stand, false, 0.0, 0.0, 17);
        assert_eq!((b.pitch, b.yaw, b.roll), (0.0, 0.0, 0.0));
        let moving = weapon_bob(Stance::Stand, false, 0.0, 190.0, 40);
        assert!(moving.pitch != 0.0 || moving.yaw != 0.0);
    }

    #[test]
    fn prone_bobs_less_than_standing() {
        use benilla_app::fusion::Stance;
        let size = |b: weapon_iw4::WeaponBobState| b.pitch.abs() + b.yaw.abs();
        let peak = |s| {
            (0..=255u8)
                .map(|c| size(weapon_bob(s, false, 0.0, 100.0, c)))
                .fold(0.0f32, f32::max)
        };
        assert!(peak(Stance::Prone) < peak(Stance::Crouch));
        assert!(peak(Stance::Crouch) <= peak(Stance::Stand));
    }

    #[test]
    fn wow_run_is_mw2_run() {
        // WoW's 7 yd/s run is MW2's 190 in/s run: MW2's bob is tuned for its own speeds.
        assert!((mw2_speed(7.0) - 190.0).abs() < 1e-3);
    }

    #[test]
    fn sprint_plays_in_then_loops() {
        assert_eq!(
            sprint_clip(0.1, Some(0.33), Some(1.0)),
            Some((anim::SPRINT_IN, 0.1))
        );
        let (slot, time) = sprint_clip(1.83, Some(0.33), Some(1.0)).unwrap();
        assert_eq!(slot, anim::SPRINT_LOOP);
        assert!((time - 0.5).abs() < 1e-4);
        // No in-clip: straight to the loop.
        assert_eq!(
            sprint_clip(0.2, None, Some(1.0)).map(|c| c.0),
            Some(anim::SPRINT_LOOP)
        );
        // No loop: hold the end of the in-clip.
        assert_eq!(
            sprint_clip(5.0, Some(0.33), None),
            Some((anim::SPRINT_IN, 0.33))
        );
        assert_eq!(sprint_clip(1.0, None, None), None);
    }
}
