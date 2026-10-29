//! The hand spells' looks, all WoW's own: each spell's `SpellVisual` kits (`SpellVisual.dbc` ×
//! `SpellVisualKit.dbc` × `SpellVisualEffectName.dbc`, read from the install) pick the models, and
//! benilla plays them with their particles (`benilla_app::fusion::FusionEffect`, `FusionUnitFx`).
//!
//! - A readied hand holds the spell's precast models (its channel, cast or state kit's when it has
//!   none), growing as it charges: on the viewmodel's hand in first person, on the character's
//!   spell-hand attachment in third person. The release plays the cast kit's hand model once.
//! - The bolt is the spell's missile model and its impact the impact kit (benilla, from
//!   crate::spellcast's `FusionSpellMissile` and `FusionSpellImpact`); a bolt that fizzles out
//!   plays its impact at [`FIZZLE_SCALE`] where it fades.
//! - A spray pours its own cone: a cone or breath model of its kits (Cone of Cold's own cone),
//!   else its school's (WoW's unused Cone of Fire for fire, Frost Breath for frost, Acid Breath for
//!   nature), else its cast hand model; one pass every [`STREAM_PULSE_SECS`], each kept on the aim.
//!   Yours from the hand, others' from the server's word (op 44).
//! - Someone else's bolt leaving plays the cast kit's hand model on their right hand.
//!
//! Performance: benilla caps the live effects ([`benilla_app::fusion::LAYER_EFFECT_CAP`]) and
//! drops a world effect out of reach; here at most [`MAX_STREAMS`] sprays are drawn (the nearest),
//! none past [`STREAM_REACH`], and the fizzles of one frame are capped at [`MAX_FIZZLES_A_FRAME`].

use std::collections::HashMap;
use std::sync::Arc;

use bevy::prelude::*;

use benilla_app::fusion::{FusionEffect, FusionUnitFx};
use benilla_assets::{LockRecover, WorldAssets};
use benilla_formats::{SpellVisualCatalog, WORLD_EFFECT_TAG};

use crate::Mw2Set;
use crate::hands::{Hand, Hands};
use crate::melee::MeleeHeld;
use crate::mode::FpsState;
use crate::spellcast::{HandReleased, HandSpells, Shape, SprayEvent, bolt_launch_point};

/// The cone a spray is drawn with when its own isn't known (yours: the server's rows are 20 and
/// 30 degrees).
pub const SPRAY_DRAW_HALF_ANGLE_DEG: f32 = 25.0;
/// Someone's spray not heard of for this long has stopped (the server tells its course every
/// 150 ms while it pours).
pub const SPRAY_STALE_SECS: f32 = 0.8;

/// `SpellVisualKit` slot tags (benilla_formats::KIT_SLOT_TAGS): the spell hands, the base.
pub const TAG_LEFT_HAND: u16 = 0x15;
pub const TAG_RIGHT_HAND: u16 = 0x16;
pub const TAG_BASE: u16 = 0x13;

/// A fizzled bolt's puff: its impact at this share of its size.
pub const FIZZLE_SCALE: f32 = 0.35;
/// A new pass of a spray's cone this often: a cone model emits for most of its 0.77 s pass, so
/// two overlap into a steady stream.
pub const STREAM_PULSE_SECS: f32 = 0.5;
/// The yards a cone model reaches at its authored size (Cone of Cold's, made for its 10 yards).
pub const CONE_MODEL_REACH: f32 = 10.0;
/// Sprays drawn at once, the nearest first, and how far away one still is.
pub const MAX_STREAMS: usize = 6;
pub const STREAM_REACH: f32 = 60.0;
pub const MAX_FIZZLES_A_FRAME: usize = 4;
/// A first-person hand's effect at rest, in world yards' scale (the viewmodel's hands are
/// half a yard from the eye, where a full-size hand effect would fill the view).
pub const HAND_FX_SCALE: f32 = 0.45;

/// The cone model a spray of a school pours when its own kits have none.
pub fn school_stream(school: u32) -> Option<&'static str> {
    match school {
        2 => Some("Spells\\ConeofFire_Hand.mdx"),
        4 => Some("Spells\\ConeofCold_Hand.mdx"),
        3 => Some("Spells\\AcidBreath.mdx"),
        _ => None,
    }
}

/// What a spell looks like in the hands and the world: model paths, from its visual's kits.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Look {
    /// Held in each hand while readied (side 0 the right fist, 1 the left).
    pub hold: [Option<String>; 2],
    /// Played once at each hand on the release.
    pub cast: [Option<String>; 2],
    /// Where it strikes: the impact kit's models, else the area model.
    pub impact: Vec<String>,
    /// A spray's cone.
    pub stream: Option<String>,
    /// A zone it leaves on the ground: the area model, else the impact or state kit's models at
    /// the feet.
    pub zone: Vec<String>,
}

/// A kit's models as `(slot tag, path)`, in slot order.
pub fn kit_models(visuals: &SpellVisualCatalog, kit: u32) -> Vec<(u16, String)> {
    let Some(kit) = visuals.kit(kit) else {
        return Vec::new();
    };
    kit.effects()
        .filter_map(|(tag, effect)| visuals.effect_path(effect).map(|p| (tag, p.to_string())))
        .collect()
}

/// The model a kit hangs on one hand (side 0 the right fist): its slot for that spell hand, else
/// the other hand's (a one-handed kit such as Cone of Cold's), never a chest or base model.
pub fn hand_model(models: &[(u16, String)], side: usize) -> Option<String> {
    let (own, other) = if side == 0 {
        (TAG_RIGHT_HAND, TAG_LEFT_HAND)
    } else {
        (TAG_LEFT_HAND, TAG_RIGHT_HAND)
    };
    models
        .iter()
        .find(|(t, _)| *t == own)
        .or_else(|| models.iter().find(|(t, _)| *t == other))
        .map(|(_, p)| p.clone())
}

fn is_cone(path: &str) -> bool {
    let p = path.to_ascii_lowercase();
    p.contains("cone") || p.contains("breath")
}

/// A spell's look from its `SpellVisual` row (`Spell.dbc` column 115) and its school.
pub fn look_of(visuals: &SpellVisualCatalog, visual: u32, school: u32) -> Look {
    let mut look = Look::default();
    let Some(st) = visuals.stages(visual).copied() else {
        look.stream = school_stream(school).map(str::to_string);
        return look;
    };
    let precast = kit_models(visuals, st.precast);
    let cast = kit_models(visuals, st.cast);
    let channel = kit_models(visuals, st.channel);
    let state = kit_models(visuals, st.state);
    let impact = kit_models(visuals, st.impact);
    let area = (st.area_effect != 0)
        .then(|| visuals.effect_path(st.area_effect))
        .flatten()
        .map(str::to_string);
    for side in 0..2 {
        look.hold[side] = [&precast, &channel, &cast, &state]
            .into_iter()
            .find_map(|k| hand_model(k, side));
        look.cast[side] = [&cast, &precast]
            .into_iter()
            .find_map(|k| hand_model(k, side));
    }
    look.impact = impact.iter().map(|(_, p)| p.clone()).collect();
    look.impact.dedup();
    if look.impact.is_empty() {
        look.impact.extend(area.clone());
    }
    look.stream = [&cast, &precast, &channel, &impact]
        .into_iter()
        .flat_map(|k| k.iter())
        .map(|(_, p)| p)
        .find(|p| is_cone(p))
        .cloned()
        .or_else(|| school_stream(school).map(str::to_string))
        .or_else(|| look.cast[0].clone());
    let at_feet = |k: &[(u16, String)]| -> Vec<String> {
        k.iter()
            .filter(|(t, _)| *t == TAG_BASE || *t == WORLD_EFFECT_TAG)
            .map(|(_, p)| p.clone())
            .collect()
    };
    look.zone = match area {
        Some(a) => vec![a],
        None => {
            let mut z = at_feet(&impact);
            if z.is_empty() {
                z = at_feet(&state);
            }
            if z.is_empty() {
                z = look.impact.clone();
            }
            z
        }
    };
    look
}

/// The spells' looks: the visual tables (read once from the install) and each spell's look,
/// worked out the first time it is asked for.
#[derive(Resource, Default)]
pub struct SpellLooks {
    visuals: Option<Arc<SpellVisualCatalog>>,
    tried: bool,
    cache: HashMap<u32, Arc<Look>>,
}

impl SpellLooks {
    pub fn get(&mut self, table: &crate::hands::SpellTable, spell: u32) -> Option<Arc<Look>> {
        if let Some(l) = self.cache.get(&spell) {
            return Some(l.clone());
        }
        let visuals = self.visuals.as_ref()?;
        let s = table.0.as_ref()?.get(spell)?;
        let look = Arc::new(look_of(visuals, s.visual, s.school));
        self.cache.insert(spell, look.clone());
        Some(look)
    }
}

/// Someone else's spray, Bevy space.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct OtherSpray {
    pub spell: u32,
    pub from: Vec3,
    pub dir: Vec3,
    pub range: f32,
    pub half_angle_deg: f32,
    pub heard: f32,
}

/// The sprays others pour, by (caster, hand).
#[derive(Resource, Default, Debug)]
pub struct Sprays {
    pub others: HashMap<(u64, u8), OtherSpray>,
}

impl Sprays {
    /// The server's word of a spray (op 44) at `t`: pouring, or stopped.
    pub fn heard(&mut self, e: &SprayEvent, t: f32) {
        let key = (e.caster, e.hand);
        if !e.on {
            self.others.remove(&key);
            return;
        }
        let from = crate::tracer::from_wow(e.from);
        let dir = (crate::tracer::from_wow([
            e.from[0] + e.dir[0],
            e.from[1] + e.dir[1],
            e.from[2] + e.dir[2],
        ]) - from)
            .normalize_or_zero();
        if dir == Vec3::ZERO {
            return;
        }
        self.others.insert(
            key,
            OtherSpray {
                spell: e.spell,
                from,
                dir,
                range: e.range,
                half_angle_deg: e.half_angle_deg,
                heard: t,
            },
        );
    }

    /// Forget the sprays not heard of lately.
    pub fn forget_stale(&mut self, t: f32) {
        self.others.retain(|_, s| t - s.heard <= SPRAY_STALE_SECS);
    }
}

/// A bolt fizzled out here (Bevy space): its puff.
#[derive(Message, Clone, Copy, Debug)]
pub struct Fizzle {
    pub at: Vec3,
    pub spell: u32,
}

/// Someone else let a spell go from the hand (op 42's launch): its cast flash on their hand.
#[derive(Message, Clone, Copy, Debug)]
pub struct CastSeen {
    pub caster: Entity,
    pub spell: u32,
}

/// The [`FusionEffect`] keys: a kind in the top byte, then what names it.
pub mod key {
    pub const HAND: u64 = 1 << 56;
    pub const STREAM: u64 = 2 << 56;
    pub const ZONE: u64 = 3 << 56;
    /// A first-person hand's held effect.
    pub fn hand(side: usize) -> u64 {
        HAND | side as u64
    }
    /// One of a spray's two overlapping passes: the caster (0 for yours), its hand, the pass.
    pub fn stream(caster: u64, hand: u8, pass: u8) -> u64 {
        STREAM
            | ((caster & 0x00ff_ffff_ffff) << 2)
            | (u64::from(hand & 1) << 1)
            | u64::from(pass & 1)
    }
    pub fn zone(serial: u32, i: usize) -> u64 {
        ZONE | (u64::from(serial) << 4) | (i as u64 & 0xf)
    }
}

/// The [`FusionUnitFx`] keys on your own body in third person: the hand's hold, its release.
const UNIT_HOLD: u32 = 1;
const UNIT_CAST: u32 = 2;

/// The axis a cone model pours along, model space in Bevy axes. WoW's cone models
/// (`ConeofCold_Hand`, `ConeofFire_Hand`) are not built facing north: their cone bodies are plane
/// emitters on unrotated bones with a negative emission speed, so they pour down the model's
/// WoW −Z, Bevy −Y (checked against the install in the tests). Placed as a unit faces, a cone
/// pours into the ground.
pub const CONE_MODEL_AXIS: Vec3 = Vec3::NEG_Y;

/// A spray's cone placed: at `from`, its [`CONE_MODEL_AXIS`] turned onto `dir`, its authored
/// reach scaled to `range`.
pub fn stream_transform(from: Vec3, dir: Vec3, range: f32) -> Transform {
    let scale = (range / CONE_MODEL_REACH).clamp(0.3, 3.0);
    // `looking_to` turns −Z onto the aim; first +90° about X turns the cone's −Y onto −Z.
    let look = Transform::IDENTITY
        .looking_to(dir.normalize_or(Vec3::NEG_Z), Vec3::Y)
        .rotation;
    Transform::from_translation(from)
        .with_rotation(look * Quat::from_rotation_x(std::f32::consts::FRAC_PI_2))
        .with_scale(Vec3::splat(scale))
}

/// The sprays drawn this frame: the nearest [`MAX_STREAMS`] within [`STREAM_REACH`] of `eye`.
pub fn streams_drawn<T: Copy>(mut all: Vec<(T, Vec3)>, eye: Vec3) -> Vec<T> {
    all.retain(|(_, at)| at.distance(eye) <= STREAM_REACH);
    all.sort_by(|a, b| {
        a.1.distance_squared(eye)
            .total_cmp(&b.1.distance_squared(eye))
    });
    all.into_iter().take(MAX_STREAMS).map(|(k, _)| k).collect()
}

/// One spray's passes: when the next is due and which of the two it is.
#[derive(Clone, Copy, Debug, Default)]
struct Stream {
    next: f32,
    pass: u8,
    seen: bool,
}

/// What this module has playing, to place, replace and stop it.
#[derive(Resource, Default)]
pub struct FxPlaying {
    /// First person: each hand's held model and the viewmodel it hangs on.
    hand: [Option<(String, Entity)>; 2],
    /// Third person: each hand's held model on your body.
    body: [Option<(String, Entity)>; 2],
    streams: HashMap<(u64, u8), Stream>,
}

pub struct SpellFxPlugin;

impl Plugin for SpellFxPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Sprays>()
            .init_resource::<SpellLooks>()
            .init_resource::<FxPlaying>()
            .add_message::<Fizzle>()
            .add_message::<CastSeen>()
            .add_systems(
                Update,
                (load_looks, hold_in_hands, pour, flash_and_fizzle)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn load_looks(world: Option<Res<WorldAssets>>, mut looks: ResMut<SpellLooks>) {
    if looks.tried || looks.visuals.is_some() {
        return;
    }
    let Some(world) = world else {
        return;
    };
    looks.tried = true;
    let mut chain = world.chain.lock_recover();
    match benilla_formats::load_spell_visual_catalog(&mut chain) {
        Ok(c) => looks.visuals = Some(Arc::new(c)),
        Err(e) => warn!("benilla-mw2: no spell visuals: {e:#}"),
    }
}

/// The spell in a hand (side 0 the right fist) while the hands are up.
fn readied(hands: &Hands, side: usize) -> Option<u32> {
    match if side == 0 { hands.right } else { hands.left } {
        Hand::Spell { spell_id, .. } => Some(spell_id),
        _ => None,
    }
}

/// Each readied hand holds its spell's precast model: on the viewmodel's hand in first person
/// (growing as it charges, crate::spellcast::glow_scale), on your body's spell hand in third.
#[allow(clippy::too_many_arguments)]
fn hold_in_hands(
    time: Res<Time>,
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    table: Res<crate::hands::SpellTable>,
    third: Res<crate::thirdperson::ThirdPerson>,
    objects: benilla_app::Objects,
    viewmodels: Query<(Entity, &crate::mw2::viewmodel::Viewmodel)>,
    mut looks: ResMut<SpellLooks>,
    mut playing: ResMut<FxPlaying>,
    mut fx: MessageWriter<FusionEffect>,
    mut unit_fx: MessageWriter<FusionUnitFx>,
) {
    let t = time.elapsed_secs();
    let vm = viewmodels.iter().find(|(_, vm)| vm.hand().0 == 0);
    let me = state.self_guid.and_then(|g| objects.entity(g));
    let up = state.active && held.0;
    for side in 0..2 {
        let model = readied(&hands, side)
            .filter(|_| up)
            .and_then(|spell| looks.get(&table, spell))
            .and_then(|l| l.hold[side].clone());
        // First person: on the viewmodel's hand, where its glow sits.
        let fp = model.clone().filter(|_| !third.on).and_then(|m| {
            let (vm_entity, vm) = vm?;
            let (at, _) = vm.glows[side]?;
            Some((m, vm_entity, at))
        });
        match fp {
            Some((m, vm_entity, at)) => {
                let k = HAND_FX_SCALE * crate::spellcast::glow_scale(&hands, side, t) / 1.3;
                // The viewmodel's root is in MW2 inches: the effect's own scale undoes it.
                let tf = Transform::from_translation(at)
                    .with_scale(Vec3::splat(k / crate::mw2::viewmodel::INCHES_TO_YARDS));
                if playing.hand[side].as_ref() != Some(&(m.clone(), vm_entity)) {
                    fx.write(FusionEffect::Play {
                        key: key::hand(side),
                        path: m.clone(),
                        parent: Some(vm_entity),
                        transform: tf,
                        looping: true,
                        size: k,
                        ground: false,
                    });
                    playing.hand[side] = Some((m, vm_entity));
                } else {
                    fx.write(FusionEffect::Place {
                        key: key::hand(side),
                        transform: tf,
                        size: k,
                    });
                }
            }
            None => {
                if playing.hand[side].take().is_some() {
                    fx.write(FusionEffect::Stop {
                        key: key::hand(side),
                    });
                }
            }
        }
        // Third person: on your body's spell hand.
        let body = model.filter(|_| third.on).zip(me);
        let tag = if side == 0 {
            TAG_RIGHT_HAND
        } else {
            TAG_LEFT_HAND
        };
        let hold_key = (UNIT_HOLD << 1) | side as u32;
        match body {
            Some((m, e)) => {
                if playing.body[side].as_ref() != Some(&(m.clone(), e)) {
                    if let Some((_, old)) = playing.body[side].take() {
                        unit_fx.write(FusionUnitFx::Stop {
                            entity: old,
                            key: hold_key,
                        });
                    }
                    unit_fx.write(FusionUnitFx::Hold {
                        entity: e,
                        key: hold_key,
                        tag,
                        path: m.clone(),
                    });
                    playing.body[side] = Some((m, e));
                }
            }
            None => {
                if let Some((_, old)) = playing.body[side].take() {
                    unit_fx.write(FusionUnitFx::Stop {
                        entity: old,
                        key: hold_key,
                    });
                }
            }
        }
    }
}

/// The sprays pouring: yours from the hand (in third person, from your body), others' from the
/// server's word, each a cone model pass every [`STREAM_PULSE_SECS`] kept on its aim.
#[allow(clippy::too_many_arguments)]
fn pour(
    time: Res<Time>,
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    aim: Res<crate::aim::Aim>,
    spells: Res<HandSpells>,
    table: Res<crate::hands::SpellTable>,
    third: Res<crate::thirdperson::ThirdPerson>,
    objects: benilla_app::Objects,
    bodies: Query<&GlobalTransform>,
    mut sprays: ResMut<Sprays>,
    mut looks: ResMut<SpellLooks>,
    mut playing: ResMut<FxPlaying>,
    mut fx: MessageWriter<FusionEffect>,
) {
    let t = time.elapsed_secs();
    sprays.forget_stale(t);
    let eye = aim.ray.map_or(Vec3::ZERO, |r| r.origin);

    // ((caster, hand), spell, from, dir, range) for every spray pouring now.
    let mut pouring: Vec<((u64, u8), u32, Vec3, Vec3, f32)> = Vec::new();
    if state.active
        && held.0
        && let Some(ray) = aim.ray
    {
        let me = state
            .self_guid
            .and_then(|g| objects.entity(g))
            .and_then(|e| bodies.get(e).ok());
        for side in 0..2u8 {
            let s = usize::from(side);
            if hands.spraying[s].is_none() {
                continue;
            }
            let Some(spell_id) = readied(&hands, s) else {
                continue;
            };
            let Some(info) = spells.0.get(&spell_id).filter(|i| i.shape == Shape::Spray) else {
                continue;
            };
            let dir = *ray.direction;
            // In third person the camera is behind you: the stream leaves your hand.
            let from = match me.filter(|_| third.on) {
                Some(body) => {
                    let right = dir.cross(Vec3::Y).normalize_or(Vec3::X);
                    let flat = Vec3::new(dir.x, 0.0, dir.z).normalize_or(Vec3::NEG_Z);
                    body.translation()
                        + Vec3::Y * 1.3
                        + flat * 0.45
                        + right * if side == 0 { 0.25 } else { -0.25 }
                }
                None => bolt_launch_point(ray, side),
            };
            pouring.push((
                (0, side),
                spell_id,
                from,
                dir,
                f32::from(info.radius.max(1)),
            ));
        }
    }
    let others: Vec<_> = sprays
        .others
        .iter()
        .map(|(k, s)| (*k, s.spell, s.from, s.dir, s.range))
        .collect();
    let drawn = streams_drawn(others.iter().map(|o| (o.0, o.2)).collect::<Vec<_>>(), eye);
    pouring.extend(others.into_iter().filter(|o| drawn.contains(&o.0)));

    for st in playing.streams.values_mut() {
        st.seen = false;
    }
    for (who, spell, from, dir, range) in pouring {
        let Some(path) = looks.get(&table, spell).and_then(|l| l.stream.clone()) else {
            continue;
        };
        let tf = stream_transform(from, dir, range);
        let size = tf.scale.x;
        let st = playing.streams.entry(who).or_default();
        st.seen = true;
        if t >= st.next {
            st.next = t + STREAM_PULSE_SECS;
            st.pass ^= 1;
            fx.write(FusionEffect::Play {
                key: key::stream(who.0, who.1, st.pass),
                path,
                parent: None,
                transform: tf,
                looping: false,
                size,
                ground: false,
            });
        }
        // Both passes follow the aim (the older one's particles already out stay where they are).
        for pass in 0..2 {
            fx.write(FusionEffect::Place {
                key: key::stream(who.0, who.1, pass),
                transform: tf,
                size,
            });
        }
    }
    // A spray let go: its passes end now, their particles finishing on their own.
    playing.streams.retain(|who, st| {
        if !st.seen {
            for pass in 0..2 {
                fx.write(FusionEffect::Stop {
                    key: key::stream(who.0, who.1, pass),
                });
            }
        }
        st.seen
    });
}

/// The release's flash at the hand (yours, others'), and a fizzled bolt's puff.
#[allow(clippy::too_many_arguments)]
fn flash_and_fizzle(
    time: Res<Time>,
    state: Res<FpsState>,
    hands: Res<Hands>,
    table: Res<crate::hands::SpellTable>,
    third: Res<crate::thirdperson::ThirdPerson>,
    objects: benilla_app::Objects,
    viewmodels: Query<(Entity, &crate::mw2::viewmodel::Viewmodel)>,
    mut released: MessageReader<HandReleased>,
    mut seen: MessageReader<CastSeen>,
    mut fizzles: MessageReader<Fizzle>,
    mut looks: ResMut<SpellLooks>,
    mut fx: MessageWriter<FusionEffect>,
    mut unit_fx: MessageWriter<FusionUnitFx>,
) {
    let t = time.elapsed_secs();
    let vm = viewmodels.iter().find(|(_, vm)| vm.hand().0 == 0);
    let me = state.self_guid.and_then(|g| objects.entity(g));
    for r in released.read() {
        let Some(look) = looks.get(&table, r.spell) else {
            continue;
        };
        let sides: &[usize] = match r.hand {
            crate::spellcast::wire::HAND_LEFT_BUTTON => &[0],
            crate::spellcast::wire::HAND_RIGHT_BUTTON => &[1],
            _ => &[0, 1],
        };
        for &side in sides {
            let Some(m) = look.cast[side].clone() else {
                continue;
            };
            if third.on {
                if let Some(e) = me {
                    unit_fx.write(FusionUnitFx::Once {
                        entity: e,
                        key: (UNIT_CAST << 1) | side as u32,
                        tag: if side == 0 {
                            TAG_RIGHT_HAND
                        } else {
                            TAG_LEFT_HAND
                        },
                        path: m,
                    });
                }
            } else if let Some((vm_entity, vm)) = vm
                && let Some((at, _)) = vm.glows[side]
            {
                let k = HAND_FX_SCALE * crate::spellcast::glow_scale(&hands, side, t) / 1.3;
                fx.write(FusionEffect::Play {
                    key: 0,
                    path: m,
                    parent: Some(vm_entity),
                    transform: Transform::from_translation(at)
                        .with_scale(Vec3::splat(k / crate::mw2::viewmodel::INCHES_TO_YARDS)),
                    looping: false,
                    size: k,
                    ground: false,
                });
            }
        }
    }
    for c in seen.read() {
        if let Some(m) = looks.get(&table, c.spell).and_then(|l| l.cast[0].clone()) {
            unit_fx.write(FusionUnitFx::Once {
                entity: c.caster,
                key: UNIT_CAST << 1,
                tag: TAG_RIGHT_HAND,
                path: m,
            });
        }
    }
    for f in fizzles.read().take(MAX_FIZZLES_A_FRAME) {
        let Some(look) = looks.get(&table, f.spell) else {
            continue;
        };
        for m in &look.impact {
            fx.write(FusionEffect::Play {
                key: 0,
                path: m.clone(),
                parent: None,
                transform: Transform::from_translation(f.at).with_scale(Vec3::splat(FIZZLE_SCALE)),
                looping: false,
                size: FIZZLE_SCALE,
                ground: false,
            });
        }
    }
    fizzles.clear();
}

#[cfg(test)]
mod tests {
    use super::*;
    use benilla_formats::{VisualKit, VisualStages};

    #[test]
    fn others_sprays_come_and_go_with_the_servers_word() {
        let mut s = Sprays::default();
        let mut e = SprayEvent {
            caster: 7,
            spell: 2948,
            hand: 0,
            on: true,
            from: [10.0, 20.0, 30.0],
            dir: [1.0, 0.0, 0.0],
            range: 10.0,
            half_angle_deg: 20.0,
        };
        s.heard(&e, 1.0);
        let got = s.others[&(7, 0)];
        // WoW +X (north) is Bevy -Z.
        assert!((got.dir - Vec3::NEG_Z).length() < 1e-5);
        assert_eq!(got.from, crate::tracer::from_wow([10.0, 20.0, 30.0]));
        // Its other hand is its own spray.
        e.hand = 1;
        s.heard(&e, 1.0);
        assert_eq!(s.others.len(), 2);
        // Stopped: gone; not heard of lately: gone.
        e.on = false;
        s.heard(&e, 1.1);
        assert_eq!(s.others.len(), 1);
        s.forget_stale(1.0 + SPRAY_STALE_SECS + 0.1);
        assert!(s.others.is_empty());
    }

    fn kit(slots: &[(usize, u32)], world: Option<u32>) -> VisualKit {
        let mut k = VisualKit::default();
        for &(i, e) in slots {
            k.effect_slots[i] = Some(e);
        }
        k.world_effect = world;
        k
    }

    /// A synthetic chain shaped like Frostbolt's and Cone of Cold's: precast at both spell hands,
    /// a one-handed cast, a chest impact, a cone in the cast kit.
    fn catalog() -> SpellVisualCatalog {
        // Slot indices of KIT_SLOT_TAGS: 1 chest (0x22), 2 base (0x13), 3 left (0x15), 4 right (0x16).
        let kits = HashMap::from([
            (10, kit(&[(3, 100), (4, 101)], None)),
            (11, kit(&[(3, 102)], None)),
            (12, kit(&[(1, 103)], None)),
            (13, kit(&[(3, 104)], None)),
            (14, kit(&[(2, 105)], Some(106))),
        ]);
        let paths = HashMap::from([
            (100, "Spells\\Ice_Precast_Low_Hand.mdx".to_string()),
            (101, "Spells\\Ice_Precast_Med_Hand.mdx".to_string()),
            (102, "Spells\\Ice_Cast_Hand.mdx".to_string()),
            (103, "Spells\\Ice_ImpactDD_Med_Chest.mdx".to_string()),
            (104, "Spells\\ConeofCold_Hand.mdx".to_string()),
            (105, "Spells\\Moonfire_Impact_Base.mdx".to_string()),
            (106, "Spells\\Frost_Nova_State.mdx".to_string()),
            (107, "Spells\\Blizzard_Area.mdx".to_string()),
        ]);
        let visuals = HashMap::from([
            (
                1,
                VisualStages {
                    precast: 10,
                    cast: 11,
                    impact: 12,
                    ..Default::default()
                },
            ),
            (
                2,
                VisualStages {
                    precast: 10,
                    cast: 13,
                    impact: 12,
                    ..Default::default()
                },
            ),
            (
                3,
                VisualStages {
                    cast: 11,
                    impact: 14,
                    ..Default::default()
                },
            ),
            (
                4,
                VisualStages {
                    area_effect: 107,
                    ..Default::default()
                },
            ),
        ]);
        SpellVisualCatalog::from_tables_with_paths(visuals, kits, paths)
    }

    #[test]
    fn each_hand_holds_its_own_spell_hand_slot_and_casts_from_it() {
        let c = catalog();
        let l = look_of(&c, 1, 4);
        // Side 0 is the right fist: the right spell hand's slot (0x16).
        assert_eq!(
            l.hold[0].as_deref(),
            Some("Spells\\Ice_Precast_Med_Hand.mdx")
        );
        assert_eq!(
            l.hold[1].as_deref(),
            Some("Spells\\Ice_Precast_Low_Hand.mdx")
        );
        // A one-handed cast kit serves both hands.
        assert_eq!(l.cast[0].as_deref(), Some("Spells\\Ice_Cast_Hand.mdx"));
        assert_eq!(l.cast[1].as_deref(), Some("Spells\\Ice_Cast_Hand.mdx"));
        assert_eq!(
            l.impact,
            vec!["Spells\\Ice_ImpactDD_Med_Chest.mdx".to_string()]
        );
        // No cone of its own: its school's.
        assert_eq!(l.stream.as_deref(), school_stream(4));
        // No precast: the cast kit's hand model is held.
        let instant = look_of(&c, 3, 6);
        assert_eq!(
            instant.hold[0].as_deref(),
            Some("Spells\\Ice_Cast_Hand.mdx")
        );
        // An arcane spray with no cone anywhere pours its cast hand model.
        assert_eq!(instant.stream.as_deref(), Some("Spells\\Ice_Cast_Hand.mdx"));
    }

    #[test]
    fn a_spray_pours_its_own_cone_before_its_schools() {
        let c = catalog();
        // Cone of Cold's own cone model in its cast kit, even as a fire spray.
        assert_eq!(
            look_of(&c, 2, 2).stream.as_deref(),
            Some("Spells\\ConeofCold_Hand.mdx")
        );
        // No visual row at all: the school's cone.
        assert_eq!(look_of(&c, 999, 2).stream.as_deref(), school_stream(2));
        assert_eq!(look_of(&c, 999, 1).stream, None);
    }

    #[test]
    fn a_zone_shows_its_area_model_else_what_lands_at_the_feet() {
        let c = catalog();
        // The impact kit's base and world-plant models, not a chest one.
        let l = look_of(&c, 3, 3);
        assert_eq!(
            l.zone,
            vec![
                "Spells\\Moonfire_Impact_Base.mdx".to_string(),
                "Spells\\Frost_Nova_State.mdx".to_string()
            ]
        );
        // An area model wins, and is the impact of a spell with no impact kit.
        let area = look_of(&c, 4, 4);
        assert_eq!(area.zone, vec!["Spells\\Blizzard_Area.mdx".to_string()]);
        assert_eq!(area.impact, area.zone);
        // Only a chest impact: the impact itself.
        assert_eq!(look_of(&c, 1, 4).zone, look_of(&c, 1, 4).impact);
    }

    #[test]
    fn a_stream_points_its_cone_along_the_aim_scaled_to_its_reach() {
        let dir = Vec3::new(1.0, 0.0, 0.0);
        let tf = stream_transform(Vec3::new(1.0, 2.0, 3.0), dir, 9.0);
        // The cone's own axis lies along the aim, for any aim, straight up and down included.
        assert!((tf.rotation * CONE_MODEL_AXIS - dir).length() < 1e-5);
        for d in [
            Vec3::NEG_Z,
            Vec3::new(0.3, -0.4, -0.8).normalize(),
            Vec3::new(-0.5, 0.6, 0.2).normalize(),
            Vec3::Y,
            Vec3::NEG_Y,
        ] {
            let r = stream_transform(Vec3::ZERO, d, 10.0).rotation;
            assert!((r * CONE_MODEL_AXIS - d).length() < 1e-4, "{d}");
        }
        assert!((tf.scale.x - 0.9).abs() < 1e-6);
        assert_eq!(tf.translation, Vec3::new(1.0, 2.0, 3.0));
    }

    #[test]
    fn only_the_nearest_sprays_in_reach_are_drawn() {
        let eye = Vec3::ZERO;
        let mut all: Vec<(u32, Vec3)> = (0..10)
            .map(|i| (i, Vec3::new(5.0 * i as f32 + 1.0, 0.0, 0.0)))
            .collect();
        all.push((99, Vec3::new(STREAM_REACH + 1.0, 0.0, 0.0)));
        let drawn = streams_drawn(all, eye);
        assert_eq!(drawn.len(), MAX_STREAMS);
        assert_eq!(drawn, (0..MAX_STREAMS as u32).collect::<Vec<_>>());
        assert!(!drawn.contains(&99), "out of reach");
    }

    #[test]
    fn effect_keys_never_collide_across_kinds_hands_or_passes() {
        let mut keys = vec![key::hand(0), key::hand(1), key::zone(1, 0), key::zone(1, 1)];
        for caster in [0u64, 7, 0x1234_5678_9abc] {
            for hand in 0..2 {
                for pass in 0..2 {
                    keys.push(key::stream(caster, hand, pass));
                }
            }
        }
        let n = keys.len();
        keys.sort_unstable();
        keys.dedup();
        assert_eq!(keys.len(), n);
        assert!(keys.iter().all(|&k| k != 0), "0 is the anonymous key");
    }

    /// The file a `SpellVisualEffectName` path names: its `.mdx` is shipped as `.m2`.
    fn m2_of(p: &str) -> String {
        let stem = p.rsplit_once('.').map_or(p, |(s, _)| s);
        format!("{stem}.m2")
    }

    /// Against the install: the shipped kits pick WoW's own models for the hand spells.
    #[test]
    fn the_installs_kits_pick_each_hand_spells_models() {
        let data = benilla_formats::wow_data_or_skip!();
        let mut chain = benilla_formats::Chain::open(&data).expect("open the install");
        let spells = benilla_formats::load_spell_catalog(&mut chain).expect("Spell.dbc");
        let visuals = benilla_formats::load_spell_visual_catalog(&mut chain).expect("visuals");
        let look = |id: u32| {
            let s = spells.get(id).expect("the spell");
            look_of(&visuals, s.visual, s.school)
        };
        let lower = |p: &Option<String>| p.as_deref().map(str::to_ascii_lowercase);
        // Fireball: the fire precast in both hands, its cast flash, its impact.
        let fireball = look(133);
        assert_eq!(
            lower(&fireball.hold[0]).as_deref(),
            Some("spells\\fire_precast_hand.mdx")
        );
        assert_eq!(
            lower(&fireball.hold[1]).as_deref(),
            Some("spells\\fire_precast_hand.mdx")
        );
        assert_eq!(
            lower(&fireball.cast[0]).as_deref(),
            Some("spells\\fire_cast_hand.mdx")
        );
        assert!(!fireball.impact.is_empty());
        // Frostbolt holds the frost precast.
        assert_eq!(
            lower(&look(116).hold[0]).as_deref(),
            Some("spells\\ice_precast_low_hand.mdx")
        );
        // Cone of Cold pours its own cone; Scorch, whose kits have none, the fire twin of it.
        assert_eq!(
            lower(&look(120).stream).as_deref(),
            Some("spells\\coneofcold_hand.mdx")
        );
        assert_eq!(
            lower(&look(2948).stream).as_deref(),
            Some("spells\\coneoffire_hand.mdx")
        );
        // Every school fallback is a model the install ships.
        for school in [2, 3, 4] {
            let m2 = m2_of(school_stream(school).expect("a school cone"));
            assert!(chain.read(&m2).is_ok(), "{m2} in the install");
        }
        // Moonfire's zone is its moonlit base model, never a chest one.
        let moonfire = look(8921);
        assert!(!moonfire.zone.is_empty());
        assert!(
            moonfire
                .zone
                .iter()
                .all(|p| p.to_ascii_lowercase().contains("base"))
        );
        // Every model any of them names is in the install.
        for id in [133, 116, 120, 2948, 8921, 10, 2120, 122, 5143, 686] {
            let l = look(id);
            let all = l
                .hold
                .iter()
                .chain(&l.cast)
                .flatten()
                .chain(&l.impact)
                .chain(&l.zone);
            for p in all.chain(l.stream.iter()) {
                let m2 = m2_of(p);
                assert!(chain.read(&m2).is_ok(), "spell {id}: {m2} in the install");
            }
        }
    }

    /// Against the install: WoW's cone models pour along [`CONE_MODEL_AXIS`]. Their cone bodies
    /// (the geometry-model emitters) and flake/ember streams are plane emitters whose birth
    /// velocity is `speed · (0, 0, 1)` about their bone; the bones rest unturned or tilted at
    /// most 35°, so every stream lies within that of the axis and the bodies on it.
    #[test]
    fn the_installs_cone_models_pour_along_the_cone_axis() {
        let data = benilla_formats::wow_data_or_skip!();
        let mut chain = benilla_formats::Chain::open(&data).expect("open the install");
        for path in ["Spells\\ConeofCold_Hand.m2", "Spells\\ConeofFire_Hand.m2"] {
            let bytes = chain.read(path).expect("the cone model");
            let emitters = benilla_formats::parse_m2_particle_emitters(&bytes).expect("emitters");
            let anims = benilla_formats::parse_m2_animations(&bytes);
            // A bone's rest rotation (sequence 0's first key; WoW quaternion x, y, z, w).
            let rest = |bone: u16| -> Quat {
                anims
                    .first()
                    .and_then(|a| a.bones.iter().find(|b| b.bone == bone))
                    .and_then(|b| b.rotation.first())
                    .map_or(Quat::IDENTITY, |(_, q)| Quat::from_array(*q).normalize())
            };
            let (mut bodies, mut sum) = (0, Vec3::ZERO);
            for e in &emitters {
                if e.shape != benilla_formats::ParticleShape::Plane {
                    continue;
                }
                let now = e.params.sample(Some(0), 0.0, 0.0);
                if now.emission_speed == 0.0 || now.vertical_range > 0.5 {
                    continue;
                }
                let wow = rest(e.bone) * Vec3::Z * now.emission_speed.signum();
                let bevy = benilla_assets::coords::wow_to_bevy(wow.to_array());
                assert!(
                    bevy.dot(CONE_MODEL_AXIS) > 35f32.to_radians().cos() - 1e-3,
                    "{path}: emitter on bone {} pours {bevy}",
                    e.bone
                );
                if e.geometry_model.is_some() {
                    bodies += 1;
                    assert!(
                        bevy.dot(CONE_MODEL_AXIS) > 0.999,
                        "{path}: body pours {bevy}"
                    );
                }
                sum += bevy;
            }
            assert!(bodies >= 1, "{path} has a cone body");
            let mean = sum.normalize();
            assert!(mean.dot(CONE_MODEL_AXIS) > 0.95, "{path}: mean pour {mean}");
            // Placed on an aim from the hand, the pour follows the aim.
            let aim = Vec3::new(0.2, -0.1, -0.97).normalize();
            let tf = stream_transform(Vec3::ZERO, aim, 10.0);
            assert!((tf.rotation * mean).dot(aim) > 0.95);
        }
    }
}
