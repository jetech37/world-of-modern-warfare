//! Reading MW2's own first-person weapons out of the player's MW2 install, with IW4L's asset
//! layer (`third_party/iw4l`): walk the multiplayer zones, keep every viewmodel XModel, every
//! weapon definition and every XAnim, then assemble one weapon's gun, arms, textures,
//! animations and sound alias names. Runs once, off the main thread.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use asset_anim::{AnimClip, AssetNamespace, XAnimBuild, XAnimCatalog};
use asset_material::{
    MaterialCatalog, MaterialDefinitions, MaterialDrawMode, TS_COLOR_MAP, TS_DETAIL_MAP,
};
use asset_model::{ModelSkel, VIEWHANDS_NAME, capture_xmodel_skel};
use asset_transport::{
    GamesRoot, ZoneMemory, find_zone_file, game_main_for_zone, games_root_from_env, open_zone,
};
use fastfile_iw4::{
    AssetLinkSink, AssetSink, AssetType, Ptr, ScriptStrings, XAnimPartsGeometry, ZonePtr,
    ZoneStream, load_asset_at_observed, load_zone,
};

/// The multiplayer zones the weapons live in, in load order: a later zone's asset of the same
/// name wins, as the patch zone overrides `common_mp` in the game.
const ZONES: [&str; 2] = ["common_mp", "patch_mp"];

/// Weapon animation slots (`asset_iw4::size::weap_anim`): the first-person clips used here.
pub mod anim {
    pub const IDLE: usize = 1;
    pub const FIRE: usize = 3;
    /// A grenade's pin pulled and held back to cook (MW2's `holdFireAnim`, the pullback); its
    /// `FIRE` is the throw.
    pub const HOLD_FIRE: usize = 4;
    pub const LAST_SHOT: usize = 5;
    /// The knife: a slice, and the lunging stab.
    pub const MELEE: usize = 7;
    pub const MELEE_CHARGE: usize = 8;
    pub const RELOAD: usize = 9;
    pub const RELOAD_EMPTY: usize = 0xA;
    /// A segmented reload (the shotguns): into it, then `RELOAD` once per shell, then out.
    pub const RELOAD_START: usize = 0xB;
    pub const RELOAD_END: usize = 0xC;
    pub const RAISE: usize = 0xD;
    pub const FIRST_RAISE: usize = 0xE;
    pub const DROP: usize = 0x10;
    pub const SPRINT_IN: usize = 0x17;
    pub const SPRINT_LOOP: usize = 0x18;
    pub const SPRINT_OUT: usize = 0x19;
    pub const ADS_FIRE: usize = 0x20;
    pub const ADS_LASTSHOT: usize = 0x21;
    pub const ADS_UP: usize = 0x23;
    pub const ADS_DOWN: usize = 0x24;
    /// `WEAPON_ANIM_COUNT`.
    pub const COUNT: usize = 37;
}

/// One weapon definition's names, read during the walk.
#[derive(Clone, Debug, Default)]
pub struct WeaponRecord {
    pub name: String,
    pub gun: Option<String>,
    pub hands: Option<String>,
    pub anims: Vec<Option<String>>,
    /// A dual-wield definition's clip sets for each hand (MW2's akimbo), and how far apart the
    /// guns sit (inches).
    pub anims_right: Vec<Option<String>>,
    pub anims_left: Vec<Option<String>>,
    pub dual_offset: f32,
    /// `WeaponDef::hideTags`: bones whose surfaces the game does not draw (attachment mounts).
    pub hide_tags: Vec<String>,
    pub placement: Placement,
    pub sounds: WeaponSounds,
    pub reload: crate::weapon::ReloadTiming,
    pub stats: WeaponStats,
    pub scope: ScopeRecord,
}

/// A scope as the client draws it: the overlay's picture and its size as a share of MW2's
/// 640x480 screen height, and whether it is the thermal sight.
#[derive(Clone, Debug)]
pub struct Scope {
    pub image: Option<Arc<RgbaImage>>,
    /// Width and height in MW2's 480-high screen units.
    pub width: f32,
    pub height: f32,
    pub thermal: bool,
}

/// Whether an overlay is the thermal sight's (MW2 aims thermal through its night-scope overlay).
pub fn is_thermal_overlay(overlay: &str, weapon: &str) -> bool {
    weapon.contains("thermal") || overlay.contains("night")
}

/// A weapon definition's aiming overlay: the full-screen scope MW2 shows instead of the gun when
/// a sniper scope or the thermal sight is aimed (`overlayMaterial`, its size in MW2's 640x480
/// screen, and the overlay's reticle and interface kinds).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ScopeRecord {
    pub overlay: Option<String>,
    pub reticle: i32,
    pub interface: i32,
    pub width: f32,
    pub height: f32,
}

/// A weapon definition's gameplay numbers, as MW2 has them (inches, milliseconds), for the
/// catalog and Create-a-Class' stat bars.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WeaponStats {
    pub display_name: Option<String>,
    pub world_model: Option<String>,
    pub damage: i32,
    pub min_damage: i32,
    pub max_damage_range: f32,
    pub min_damage_range: f32,
    pub fire_time_ms: i32,
    /// A bolt or pump's cycle after each shot.
    pub rechamber_time_ms: i32,
    pub bolt_action: bool,
    pub shots_per_fire: i32,
    pub clip_size: i32,
    pub max_ammo: i32,
    pub start_ammo: i32,
    pub move_speed_scale: f32,
    pub ads_move_speed_scale: f32,
    pub ads_zoom_fov: f32,
    pub hip_spread_stand_min: f32,
    pub hip_spread_stand_max: f32,
    /// MW2's whole spread model for the gun (crate::spread).
    pub spread: crate::spread::SpreadStats,
    pub melee_damage: i32,
    pub weap_class: i32,
    pub inventory_type: i32,
    pub fire_type: i32,
    pub penetrate_type: i32,
    pub explosion_radius: i32,
    pub explosion_inner_damage: i32,
    pub explosion_outer_damage: i32,
    pub alternate: Option<String>,
    /// The HUD icon material (`hud_icon_m4carbine`), for Create-a-Class.
    pub hud_icon: Option<String>,
}

/// The gun's stance placement (`WeaponDef`): where it sits crouched and prone, and how it moves
/// and turns while walking, crouch-walking and crawling. MW2's own numbers, per weapon.
#[derive(Clone, Copy, Debug, Default)]
pub struct Placement {
    pub ducked_ofs: [f32; 3],
    pub prone_ofs: [f32; 3],
    pub movement: fastfile_iw4::WeaponMovementOfsCapture,
}

/// The weapon's sound alias names (first-person variants where the game has them).
#[derive(Clone, Debug, Default)]
pub struct WeaponSounds {
    pub fire: Option<String>,
    /// The gun as others hear it (its third-person fire sound).
    pub fire_npc: Option<String>,
    pub fire_last: Option<String>,
    pub empty: Option<String>,
    pub reload: Option<String>,
    pub reload_empty: Option<String>,
    pub raise: Option<String>,
    pub putaway: Option<String>,
    pub melee: Option<String>,
}

/// One drawable surface of a viewmodel part.
#[derive(Clone, Debug)]
pub struct Surface {
    /// First vertex and count into the part's vertex arrays.
    pub vertices: (usize, usize),
    /// First index and count into the part's index array.
    pub indices: (usize, usize),
    /// The colour map's top mip as RGBA8, `None` when it could not be found or decoded.
    pub color: Option<Arc<RgbaImage>>,
    /// How the material's colour pass draws (its state bits): opaque, alpha-tested or blended.
    /// The colour map's alpha only cuts or blends when the state says so.
    pub draw: MaterialDrawMode,
    /// The share of the colour that shows: 1, or a scope's or sight's glass, mostly clear.
    pub opacity: f32,
    /// A colour the texture is multiplied by: white, or a reflex sight's red (MW2's reflex shader
    /// tints its greyscale reticle).
    pub tint: [f32; 3],
    /// The bones the surface is skinned to (`XSurface::partBits`), for the hide test.
    pub part_bits: [u32; 6],
    /// The heartbeat sensor's screen, where the radar is drawn.
    pub motion_tracker: bool,
}

#[derive(Clone, Debug)]
pub struct RgbaImage {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}

/// The gun or the arms: its captured mesh and skeleton, and its LOD 0 surfaces.
#[derive(Clone, Debug)]
pub struct Part {
    pub skel: Arc<ModelSkel>,
    pub surfaces: Vec<Surface>,
}

/// Everything the viewmodel needs for one weapon.
#[derive(Clone, Debug)]
pub struct LoadedWeapon {
    pub name: String,
    /// The arms first, then the gun: the order the DObj is built in.
    pub parts: Vec<Part>,
    /// Clips by weapon animation slot.
    pub clips: Vec<Option<Arc<AnimClip>>>,
    /// Bone names whose surfaces stay hidden.
    pub hide_tags: Vec<String>,
    pub placement: Placement,
    pub sounds: WeaponSounds,
    pub reload: crate::weapon::ReloadTiming,
    /// The full-screen scope this definition aims through (a sniper scope, the thermal sight).
    pub scope: Option<Arc<Scope>>,
    /// The definition's aimed field of view in degrees (MW2's `adsZoomFov`: iron sights 50, an
    /// ACOG 30), against MW2's 65 at the hip.
    pub ads_fov: Option<f32>,
    /// The definition's spread numbers (crate::spread).
    pub spread: crate::spread::SpreadStats,
    /// The hand this gun is in: 0 the right (every gun), 1 the left (akimbo's second gun).
    pub hand: u8,
    /// Akimbo: the left gun's clips (`left_hand` builds it), and how far apart the guns sit.
    pub left_clips: Option<Vec<Option<Arc<AnimClip>>>>,
    pub dual_offset: f32,
    /// The install's `main` folder, for streamed sounds and images.
    pub main: PathBuf,
}

impl LoadedWeapon {
    /// Two guns, one in each hand (MW2's akimbo).
    pub fn akimbo(&self) -> bool {
        self.left_clips.is_some() || self.hand == 1
    }

    /// The left hand's gun of an akimbo pair: the same gun posed by the left hand's clips.
    pub fn left_hand(&self) -> Option<LoadedWeapon> {
        let clips = self.left_clips.clone()?;
        Some(LoadedWeapon {
            hand: 1,
            clips,
            left_clips: None,
            ..self.clone()
        })
    }
}

/// The zone walk's sink: materials, XAnims, viewmodel XModels and weapon names.
#[derive(Default)]
struct WalkSink {
    strings: ScriptStrings,
    materials: MaterialCatalog,
    xanims: XAnimBuild,
    models: HashMap<String, ModelSkel>,
    weapons: HashMap<String, WeaponRecord>,
    xmodel_surfaces: HashMap<Ptr, Ptr>,
    xmodel_surface_names: HashMap<Ptr, Ptr>,
    xmodel_names: HashMap<Ptr, Ptr>,
    /// This zone's WeaponDefs by body, with the names of the record that owns each: a
    /// combination sharing another's definition (`famas_xmags_mp`) names no model or sounds of
    /// its own and takes them from here. Pointers are zone-relative: cleared per zone.
    def_owners: HashMap<Ptr, DefOwner>,
}

/// A WeaponDef's model and sound names, as its owning record read them.
#[derive(Clone, Debug, Default)]
struct DefOwner {
    gun: Option<String>,
    hands: Option<String>,
    sounds: WeaponSounds,
    /// Its animations (an attachment variant shares the definition, and with it every clip:
    /// without them it has no aim, reload or raise).
    anims: Vec<Option<String>>,
    anims_right: Vec<Option<String>>,
    anims_left: Vec<Option<String>>,
}

/// Fill what a record sharing another's WeaponDef left empty from that definition's owner.
fn inherit(record: &mut WeaponRecord, owner: &DefOwner) {
    let fill = |mine: &mut Option<String>, theirs: &Option<String>| {
        if mine.is_none() {
            mine.clone_from(theirs);
        }
    };
    fill(&mut record.gun, &owner.gun);
    fill(&mut record.hands, &owner.hands);
    let (m, o) = (&mut record.sounds, &owner.sounds);
    fill(&mut m.fire, &o.fire);
    fill(&mut m.fire_npc, &o.fire_npc);
    fill(&mut m.fire_last, &o.fire_last);
    fill(&mut m.empty, &o.empty);
    fill(&mut m.reload, &o.reload);
    fill(&mut m.reload_empty, &o.reload_empty);
    fill(&mut m.raise, &o.raise);
    fill(&mut m.putaway, &o.putaway);
    fill(&mut m.melee, &o.melee);
    let fill_anims = |mine: &mut Vec<Option<String>>, theirs: &Vec<Option<String>>| {
        if mine.iter().all(Option::is_none) {
            mine.clone_from(theirs);
        }
    };
    fill_anims(&mut record.anims, &owner.anims);
    fill_anims(&mut record.anims_right, &owner.anims_right);
    fill_anims(&mut record.anims_left, &owner.anims_left);
}

/// An asset name as the zone wrote it: a leading `,` marks a reference to an asset loaded from
/// another zone (`,viewmodel_model_1887` in the patch zone) and is not part of the name.
fn asset_name(raw: &str) -> Option<&str> {
    Some(raw.strip_prefix(',').unwrap_or(raw)).filter(|s| !s.is_empty())
}

/// An akimbo pair (`usp_akimbo_mp`, `glock_akimbo_fmj_mp`): the attachment in the name.
pub fn is_akimbo(name: &str) -> bool {
    name.split('_').any(|p| p.eq_ignore_ascii_case("akimbo"))
}

/// World models kept beside the viewmodels: building draws them (crate::build).
pub const WORLD_MODELS: &[&str] = &["sentry_minigun"];

/// A gun's third-person model (`weapon_m4`): kept so NPCs can carry MW2's guns.
pub fn is_world_gun(name: &str) -> bool {
    name.to_ascii_lowercase().starts_with("weapon_")
}

/// The front-most point of a model (MW2's world guns point along +X): its muzzle.
pub fn front_point(positions: &[[f32; 3]]) -> Option<bevy::math::Vec3> {
    positions
        .iter()
        .copied()
        .map(bevy::math::Vec3::from_array)
        .max_by(|a, b| a.x.total_cmp(&b.x))
}

/// A gun's muzzle: its flash tag, when it is clear of the grip along X, else the mesh's.
pub fn muzzle_from(
    grip: bevy::math::Vec3,
    flash: Option<bevy::math::Vec3>,
    mesh: bevy::math::Vec3,
) -> bevy::math::Vec3 {
    flash
        .filter(|f| (f.x - grip.x).abs() > 1.0)
        .map(|f| bevy::math::Vec3::new(f.x, 0.0, grip.z))
        .unwrap_or(mesh)
}

/// A gun's grip and muzzle from its mesh (MW2 axes, +Z up): the grip over the lowest vertex
/// (the handle hangs lowest), lifted to the mesh's mid-height; the muzzle the far end of the
/// longer side of it along X, at the grip's height. A pistol whose model faces -X reads as one.
pub fn grip_and_muzzle(
    positions: &[[f32; 3]],
) -> Option<(bevy::math::Vec3, bevy::math::Vec3)> {
    use bevy::math::Vec3;
    let ps: Vec<Vec3> = positions.iter().copied().map(Vec3::from_array).collect();
    let low = ps.iter().copied().min_by(|a, b| a.z.total_cmp(&b.z))?;
    let (min_x, max_x) = ps
        .iter()
        .fold((f32::MAX, f32::MIN), |(lo, hi), p| (lo.min(p.x), hi.max(p.x)));
    let top = ps.iter().map(|p| p.z).fold(f32::MIN, f32::max);
    let grip = Vec3::new(low.x, 0.0, (low.z + top) * 0.5);
    let far_x = if max_x - low.x >= low.x - min_x { max_x } else { min_x };
    Some((grip, Vec3::new(far_x, 0.0, grip.z)))
}

/// An MW2 gun as an NPC carries it: its world model at its bind pose (MW2 inches, the grip at
/// the origin, pointing +X) and its muzzle.
#[derive(Clone)]
pub struct WorldGun {
    pub part: Part,
    pub muzzle: bevy::math::Vec3,
    /// Where the hand holds it (MW2 inches): under the handle, at the drawn mesh's lowest point
    /// (a pistol's butt, a rifle's grip or magazine), at the mesh's mid-height.
    pub grip: bevy::math::Vec3,
}

fn is_viewmodel(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.starts_with("viewmodel_") || n.starts_with("viewhands_")
}

fn read_str(stream: &ZoneStream<'_>, ptr: Option<Ptr>) -> Option<String> {
    ptr.and_then(|p| stream.cstr(p).ok())
        .and_then(asset_name)
        .map(str::to_owned)
}

/// `WeaponDef::szXAnims`: a pointer array of clip names, one per animation slot.
fn read_anims(stream: &ZoneStream<'_>, array: Option<Ptr>) -> Vec<Option<String>> {
    let mut out = vec![None; anim::COUNT];
    let Some(array) = array else {
        return out;
    };
    for (i, slot) in out.iter_mut().enumerate() {
        if let Ok(ZonePtr::Offset(q)) = stream.ptr_at(array, i * stream.pointer_bytes()) {
            *slot = read_str(stream, Some(stream.resolve_alias(q)));
        }
    }
    out
}

/// `WeaponDef::hideTags`: 32 script-string ids, zero for an empty slot.
fn read_hide_tags(
    stream: &ZoneStream<'_>,
    strings: &ScriptStrings,
    array: Option<Ptr>,
) -> Vec<String> {
    let Some(array) = array else {
        return Vec::new();
    };
    (0..32)
        .filter_map(|i| stream.u16_at(array, i * 2).ok())
        .filter(|&id| id != 0)
        .filter_map(|id| strings.get(stream, id))
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

/// How a material draws when its state bits say nothing, from MW2's technique set name: a lit
/// blend (`mc_l_sm_b0...`, a scope's or sight's glass) and an unlit blend blend by alpha; a reflex
/// sight (the red dot's and the holographic's projected reticle) adds, so only the dot glows; an
/// effect (the ACOG's etched reticle) blends by its alpha. `None`: draw it opaque.
pub fn draw_from_technique(technique: &str) -> Option<MaterialDrawMode> {
    let t = technique.to_ascii_lowercase();
    if t.starts_with("mc_reflexsight") || t.starts_with("wc_reflexsight") {
        return Some(MaterialDrawMode::Additive);
    }
    if t.contains("_add") {
        return Some(MaterialDrawMode::Additive);
    }
    if t.contains("effect") || t.contains("blend") || t.split('_').any(|w| w.starts_with("b0")) {
        return Some(MaterialDrawMode::Blend);
    }
    None
}

/// How much of a surface shows: a scope's or sight's glass (a lit blend named lens) is mostly
/// clear, as MW2's lens shader draws it with a faint tint; everything else is as its texture says.
/// A sight's surface over the eyepiece (the thermal's screens, a sniper scope's white cap):
/// drawn opaque by MW2, but at the hip it hides what the lens would show, so it is glass here.
pub fn is_sight_screen(material: &str) -> bool {
    let m = material.to_ascii_lowercase();
    m.contains("thermal_screen") || m.contains("scope_white")
}

/// The heartbeat sensor's screen (`mtl_motion_tracker_screen`): the radar is drawn on it
/// (crate::heartbeat), not the install's still picture.
pub fn is_motion_tracker_screen(material: &str) -> bool {
    material
        .to_ascii_lowercase()
        .contains("motion_tracker_screen")
}

/// How much of a sight screen shows at the hip.
const SIGHT_SCREEN_OPACITY: f32 = 0.1;

pub fn glass_opacity(material: &str, technique: &str) -> f32 {
    let m = material.to_ascii_lowercase();
    let lit_blend = technique
        .to_ascii_lowercase()
        .split('_')
        .any(|w| w.starts_with("b0"));
    if lit_blend && (m.contains("lens") || m.contains("glass")) {
        0.12
    } else {
        1.0
    }
}

/// Bake a detail map into a colour map, as MW2's detail shaders blend it per pixel: the colour
/// times twice the detail (tiled `scale` times across the colour's UVs), where the colour map's
/// alpha marks the painted areas the detail shows on. A camo is a gun's detail map (the woodland
/// pattern on `viewmodel_m4_woodland`); the plain guns carry a faint paint texture there.
pub fn bake_detail(color: &RgbaImage, detail: &RgbaImage, scale: [f32; 2]) -> RgbaImage {
    let (cw, ch) = (color.width as usize, color.height as usize);
    let (dw, dh) = (detail.width.max(1) as usize, detail.height.max(1) as usize);
    let mut out = color.pixels.clone();
    for y in 0..ch {
        for x in 0..cw {
            let u = (x as f32 + 0.5) / cw as f32 * scale[0];
            let v = (y as f32 + 0.5) / ch as f32 * scale[1];
            let dx = ((u.rem_euclid(1.0)) * dw as f32) as usize % dw;
            let dy = ((v.rem_euclid(1.0)) * dh as f32) as usize % dh;
            let d = &detail.pixels[(dy * dw + dx) * 4..(dy * dw + dx) * 4 + 3];
            let i = (y * cw + x) * 4;
            let mask = out[i + 3] as f32 / 255.0;
            for c in 0..3 {
                let base = out[i + c] as f32;
                let detailed = base * (d[c] as f32 / 255.0) * 2.0;
                out[i + c] = (base + (detailed - base) * mask).round().clamp(0.0, 255.0) as u8;
            }
            // The colour is opaque wherever the material draws opaque; alpha was the mask.
            out[i + 3] = 255;
        }
    }
    RgbaImage {
        width: color.width,
        height: color.height,
        pixels: out,
    }
}

/// A reflex sight's reticle colour (the red dot's and the holographic's): MW2 draws the
/// greyscale reticle in red; anything else is untinted.
pub fn reticle_tint(technique: &str) -> [f32; 3] {
    if technique.to_ascii_lowercase().contains("reflexsight") {
        [1.0, 0.12, 0.08]
    } else {
        [1.0, 1.0, 1.0]
    }
}

/// The gun model to draw: the camo's when the install has it, else the combination's own, else
/// its gun's (a combination such as `m4_eotech_mp` can share the gun's definition and name no
/// model).
pub fn pick_gun_model(
    camo: Option<&str>,
    variant: Option<&str>,
    base: Option<&str>,
    has_model: impl Fn(&str) -> bool,
) -> Option<String> {
    camo.filter(|m| has_model(m))
        .or(variant)
        .or(base)
        .map(str::to_owned)
}

/// WeaponDef milliseconds to seconds; MW2 leaves an unused time at 0.
fn ms(v: i32) -> f32 {
    v.max(0) as f32 / 1000.0
}

impl WalkSink {
    fn capture_weapon(&mut self, stream: &ZoneStream<'_>) {
        let Some(g) = stream.weapon() else {
            return;
        };
        let Some(name) = read_str(stream, g.name) else {
            return;
        };
        let either = |player: Option<Ptr>, world: Option<Ptr>| {
            read_str(stream, player).or_else(|| read_str(stream, world))
        };
        let mut record = WeaponRecord {
            gun: read_str(stream, g.gun_xmodel_name),
            hands: read_str(stream, g.hand_xmodel_name),
            anims: read_anims(stream, g.sz_xanims),
            anims_right: read_anims(stream, g.sz_xanims_right),
            anims_left: read_anims(stream, g.sz_xanims_left),
            dual_offset: g.dual_wield_view_model_offset,
            hide_tags: read_hide_tags(stream, &self.strings, g.hide_tags),
            placement: Placement {
                ducked_ofs: g.ducked_ofs,
                prone_ofs: g.prone_ofs,
                movement: g.movement,
            },
            sounds: WeaponSounds {
                fire: either(g.fire_sound_player_name, g.fire_sound_name),
                fire_npc: either(g.fire_sound_name, g.fire_sound_player_name),
                fire_last: either(g.fire_last_sound_player_name, g.fire_last_sound_name),
                empty: either(g.empty_fire_sound_player_name, g.empty_fire_sound_name),
                reload: either(g.reload_sound_player_name, g.reload_sound_name),
                reload_empty: either(g.reload_empty_sound_player_name, g.reload_empty_sound_name),
                raise: either(g.raise_sound_player_name, g.raise_sound_name),
                putaway: either(g.putaway_sound_player_name, g.putaway_sound_name),
                melee: read_str(stream, g.melee_swipe_sound_player_name),
            },
            stats: WeaponStats {
                display_name: read_str(stream, g.display_name),
                world_model: read_str(stream, g.world_model_name),
                damage: g.damage,
                min_damage: g.min_damage,
                max_damage_range: g.max_damage_range,
                min_damage_range: g.min_damage_range,
                fire_time_ms: g.fire_time_ms,
                rechamber_time_ms: g.rechamber_time_ms,
                bolt_action: g.bolt_action,
                shots_per_fire: g.shots_per_fire,
                clip_size: g.clip_size,
                max_ammo: g.max_ammo,
                start_ammo: g.start_ammo,
                move_speed_scale: g.move_speed_scale,
                ads_move_speed_scale: g.ads_move_speed_scale,
                ads_zoom_fov: g.ads_zoom_fov,
                hip_spread_stand_min: g.hip_spread_stand_min,
                hip_spread_stand_max: g.hip_spread_stand_max,
                spread: crate::spread::SpreadStats {
                    stand_min: g.hip_spread_stand_min,
                    stand_max: g.hip_spread_stand_max,
                    ducked_min: g.hip_spread_ducked_min,
                    ducked_max: g.hip_spread_ducked_max,
                    prone_min: g.hip_spread_prone_min,
                    prone_max: g.hip_spread_prone_max,
                    decay_rate: g.hip_spread_decay_rate,
                    fire_add: g.hip_spread_fire_add,
                    move_add: g.hip_spread_move_add,
                    turn_add: g.hip_spread_turn_add,
                    ducked_decay: g.hip_spread_ducked_decay,
                    prone_decay: g.hip_spread_prone_decay,
                    ads: g.ads_spread,
                },
                melee_damage: g.melee_damage,
                weap_class: g.weap_class,
                inventory_type: g.inventory_type,
                fire_type: g.fire_type,
                penetrate_type: g.penetrate_type,
                explosion_radius: g.explosion_radius,
                explosion_inner_damage: g.explosion_inner_damage,
                explosion_outer_damage: g.explosion_outer_damage,
                alternate: read_str(stream, g.alternate_weapon_name),
                hud_icon: g
                    .hud_icon_slot
                    .and_then(|slot| self.materials.linked_asset_name(slot))
                    .map(str::to_owned),
            },
            scope: ScopeRecord {
                overlay: g
                    .overlay_material_slot
                    .and_then(|slot| self.materials.linked_asset_name(slot))
                    .map(str::to_owned),
                reticle: g.overlay_reticle,
                interface: g.overlay_interface,
                width: g.ads_overlay_width,
                height: g.ads_overlay_height,
            },
            reload: crate::weapon::ReloadTiming {
                secs: ms(g.reload_time_ms),
                empty_secs: ms(g.reload_empty_time_ms),
                segmented: g.segmented_reload,
                start_secs: ms(g.reload_start_time_ms),
                end_secs: ms(g.reload_end_time_ms),
                per_segment: g.reload_ammo_add.max(0) as u32,
            },
            name: name.clone(),
        };
        if let Some(def) = g.weap_def {
            if g.gun_xmodel_name.is_some() {
                self.def_owners.insert(
                    def,
                    DefOwner {
                        gun: record.gun.clone(),
                        hands: record.hands.clone(),
                        sounds: record.sounds.clone(),
                        anims: record.anims.clone(),
                        anims_right: record.anims_right.clone(),
                        anims_left: record.anims_left.clone(),
                    },
                );
            } else if let Some(owner) = self.def_owners.get(&def) {
                inherit(&mut record, owner);
            }
        }
        self.weapons.insert(name.to_ascii_lowercase(), record);
    }
}

impl AssetSink for WalkSink {
    fn set_script_strings(&mut self, strings: ScriptStrings) {
        self.strings = strings;
        self.xanims.set_strings(strings);
    }

    fn load_asset(
        &mut self,
        s: &mut ZoneStream<'_>,
        _index: usize,
        ty: AssetType,
        slot: Ptr,
    ) -> fastfile_iw4::Result<()> {
        load_asset_at_observed(s, ty, slot, self)?;
        Ok(())
    }
}

impl AssetLinkSink for WalkSink {
    fn loaded(
        &mut self,
        stream: &ZoneStream<'_>,
        ty: AssetType,
        slot: Ptr,
        insert_slot: Option<Ptr>,
    ) -> fastfile_iw4::Result<()> {
        self.materials.loaded(stream, ty, slot, insert_slot)?;
        match ty {
            AssetType::Weapon => self.capture_weapon(stream),
            AssetType::XModel => {
                if let Some(geometry) = stream.xmodel()
                    && let Some(name) = read_str(stream, geometry.name)
                    && (is_viewmodel(&name)
                        || is_world_gun(&name)
                        || WORLD_MODELS.contains(&name.to_ascii_lowercase().as_str()))
                    && let Some(skel) =
                        capture_xmodel_skel(stream, &self.strings, geometry, Some(&self.materials))
                {
                    self.models.insert(name.to_ascii_lowercase(), skel);
                }
            }
            _ => {}
        }
        Ok(())
    }

    fn alias(&mut self, ty: AssetType, slot: Ptr, target: Ptr) -> fastfile_iw4::Result<()> {
        self.materials.alias(ty, slot, target)?;
        if ty == AssetType::XModel
            && let Some(&name) = self.xmodel_names.get(&target)
        {
            self.xmodel_names.insert(slot, name);
        }
        Ok(())
    }

    fn linked_asset_name(&self, slot: Ptr) -> Option<&str> {
        self.materials.linked_asset_name(slot)
    }

    fn capture_xanim(
        &mut self,
        s: &ZoneStream<'_>,
        geometry: XAnimPartsGeometry,
    ) -> fastfile_iw4::Result<()> {
        self.xanims.capture_xanim(s, geometry)
    }

    fn remember_xmodel_surface_name(&mut self, slot: Ptr, name: Ptr) {
        self.xmodel_surface_names.insert(slot, name);
    }

    fn xmodel_surface_name(&self, slot: Ptr) -> Option<Ptr> {
        self.xmodel_surface_names.get(&slot).copied()
    }

    fn remember_xmodel_surfaces(&mut self, slot: Ptr, surfaces: Ptr) {
        self.xmodel_surfaces.insert(slot, surfaces);
    }

    fn xmodel_surfaces(&self, slot: Ptr) -> Option<Ptr> {
        self.xmodel_surfaces.get(&slot).copied()
    }

    fn remember_xmodel_name(&mut self, slot: Ptr, insert_slot: Option<Ptr>, name: Ptr) {
        self.xmodel_names.insert(slot, name);
        if let Some(insert_slot) = insert_slot {
            self.xmodel_names.insert(insert_slot, name);
        }
    }

    fn xmodel_name_ptr(&self, slot: Ptr) -> Option<Ptr> {
        self.xmodel_names.get(&slot).copied()
    }
}

/// The MW2 install's zones, catalogued.
pub struct Mw2Library {
    pub root: GamesRoot,
    pub main: PathBuf,
    pub weapons: HashMap<String, WeaponRecord>,
    models: HashMap<String, ModelSkel>,
    materials: MaterialDefinitions,
    /// Walk-local material row to published row.
    material_rows: Vec<Option<usize>>,
    clips: XAnimCatalog,
}

/// Find the install: `IW4L_GAMES` (the folder holding game trees, as IW4L reads it), or
/// `MW2_PATH` (the MW2 folder itself).
pub fn games_root() -> Result<GamesRoot, String> {
    if std::env::var_os("IW4L_GAMES").is_none()
        && let Some(mw2) = std::env::var_os("MW2_PATH")
    {
        return Ok(GamesRoot(PathBuf::from(mw2)));
    }
    games_root_from_env().map_err(|e| format!("{e} (set MW2_PATH to your Modern Warfare 2 folder)"))
}

impl Mw2Library {
    /// Walk the multiplayer zones. Takes a few seconds and a few hundred MB while it runs.
    pub fn open() -> Result<Self, String> {
        let root = games_root()?;
        let mut sink = WalkSink::default();
        let mut main = None;
        for (i, zone) in ZONES.iter().enumerate() {
            let file = match find_zone_file(&root, zone) {
                Ok(file) => file,
                // `common_mp` is required; the patch zone is not in every install.
                Err(e) if i == 0 => return Err(format!("{zone}: {e}")),
                Err(_) => continue,
            };
            if main.is_none() {
                main = Some(game_main_for_zone(&file.path)?);
            }
            walk(&file.path, &mut sink).map_err(|e| format!("{zone}: {e}"))?;
            sink.materials.prepare_for_next_zone();
            sink.def_owners.clear();
        }
        let main = main.ok_or("no MW2 zone found")?;
        let WalkSink {
            materials,
            xanims,
            models,
            weapons,
            ..
        } = sink;
        let (materials, material_rows) = materials.publish();
        Ok(Self {
            root,
            main,
            weapons,
            models,
            materials,
            material_rows,
            clips: xanims.publish(),
        })
    }

    /// Each LOD 0 surface of a viewmodel: its material, technique set, draw mode and textures
    /// (semantic: image), for diagnosing how a surface draws.
    /// A viewmodel's bone names in skeleton order (`--bones` in the exporter).
    /// A captured world model ([`WORLD_MODELS`]) with its surfaces, at its bind pose.
    pub fn world_model(&self, name: &str) -> Option<Part> {
        self.models
            .get(&name.to_ascii_lowercase())
            .map(|skel| self.part(skel))
    }

    /// A weapon in another's clothes: `variant`'s clips, sounds and timing on `look`'s model,
    /// camo and hidden attachments (the primary's underbarrel launcher is the rifle in hand).
    pub fn weapon_dressed(
        &self,
        variant: &str,
        base: &str,
        look: &str,
        look_base: &str,
        camo_model: Option<&str>,
    ) -> Result<LoadedWeapon, String> {
        let mut w = self.weapon_with(variant, base, None)?;
        let dress = self.weapon_with(look, look_base, camo_model)?;
        w.parts = dress.parts;
        w.hide_tags = dress.hide_tags;
        Ok(w)
    }

    /// The world model a catalog gun (`m4_mp`) shows in an NPC's hands.
    pub fn world_gun(&self, weapon: &str) -> Option<WorldGun> {
        let record = self.weapons.get(&weapon.to_ascii_lowercase())?;
        let skel = self
            .models
            .get(&record.stats.world_model.as_deref()?.to_ascii_lowercase())?;
        // The gun bare: the attachment mounts its definition hides (sights, silencers) stay off.
        let mut hidden = [0u32; 6];
        for (bone, name) in skel.bone_names.iter().enumerate() {
            if record
                .hide_tags
                .iter()
                .any(|t| t.eq_ignore_ascii_case(name))
            {
                anim_iw4::set_hide_part_bit(&mut hidden, bone);
            }
        }
        let mut part = self.part(skel);
        part.surfaces
            .retain(|s| !anim_iw4::surface_hidden(&s.part_bits, &hidden, 0));
        // The muzzle of the bare gun, not of a silencer it does not carry.
        let shown: Vec<[f32; 3]> = part
            .surfaces
            .iter()
            .filter_map(|s| {
                skel.positions
                    .get(s.vertices.0..s.vertices.0 + s.vertices.1)
            })
            .flatten()
            .copied()
            .collect();
        // The grip and muzzle from the drawn mesh: the model's tags did not agree with it (the
        // USP's flash tag sat behind its handle).
        let (grip, mesh_muzzle) = grip_and_muzzle(&shown)?;
        // The muzzle is the flash tag where the model has one: a long stock outreaches the barrel
        // from the grip (the AK47, the AUG, the CheyTac), so the mesh's longer side pointed them
        // backwards. Measured from the grip, never the origin (a pistol's origin is not its grip).
        let flash = skel
            .bone_names
            .iter()
            .position(|n| n.eq_ignore_ascii_case("tag_flash"))
            .and_then(|i| skel.bones.get(i))
            .map(|b| bevy::math::Vec3::from_array(b.trans));
        let muzzle = muzzle_from(grip, flash, mesh_muzzle);
        Some(WorldGun {
            muzzle,
            grip,
            part,
        })
    }

    pub fn bone_names(&self, model: &str) -> Vec<String> {
        self.models
            .get(&model.to_ascii_lowercase())
            .and_then(|m| m.pose.as_ref())
            .map(|p| p.bone_names.clone())
            .unwrap_or_default()
    }

    pub fn describe_surfaces(&self, model: &str) -> Vec<String> {
        let Some(skel) = self.models.get(&model.to_ascii_lowercase()) else {
            return vec![format!("no model {model}")];
        };
        skel.surfaces_for_lod(0)
            .map(|i| {
                let material = skel
                    .surface_materials
                    .get(i)
                    .copied()
                    .flatten()
                    .and_then(|walk| self.material_rows.get(walk.get()).copied().flatten())
                    .and_then(|row| self.materials.materials.get(row));
                let Some(m) = material else {
                    return format!("surface {i}: no material");
                };
                let textures: Vec<String> = m
                    .textures
                    .iter()
                    .map(|t| {
                        let image = t
                            .image
                            .and_then(|ix| self.materials.images.get(ix))
                            .map_or("-".to_owned(), |im| im.name.to_string());
                        format!("{}:{image}", t.semantic)
                    })
                    .collect();
                let constants: Vec<String> = m
                    .constants
                    .iter()
                    .map(|c| {
                        let name = String::from_utf8_lossy(&c.name);
                        format!("{}={:?}", name.trim_end_matches('\0'), c.literal)
                    })
                    .collect();
                format!(
                    "surface {i}: {} tech={} draw={:?} textures=[{}] constants=[{}]",
                    m.name,
                    m.technique_set,
                    self.materials.agreed_draw_mode(m),
                    textures.join(" "),
                    constants.join(" ")
                )
            })
            .collect()
    }

    /// The viewmodel XModels the walk kept (guns and arms), by lower-case name.
    pub fn viewmodel_names(&self) -> impl Iterator<Item = &str> {
        self.models.keys().map(String::as_str)
    }

    /// Assemble one weapon (`m4_mp`, `cheytac_mp`, ...), falling back to `fallback` when the
    /// install has no such weapon.
    pub fn weapon(&self, name: &str, fallback: &str) -> Result<LoadedWeapon, String> {
        self.weapon_with(name, fallback, None)
    }

    /// Assemble an attachment combination's weapon definition (`m4_acog_silencer_mp`) of the gun
    /// `base`, with a camo's model (`viewmodel_m4_woodland`) when given and in the install. A
    /// combination's definition can share its gun's: what it leaves empty (the model, the clips,
    /// the hands) comes from `base`.
    pub fn weapon_with(
        &self,
        variant: &str,
        base: &str,
        camo_model: Option<&str>,
    ) -> Result<LoadedWeapon, String> {
        let base_record = self.weapons.get(&base.to_ascii_lowercase());
        let record = self
            .weapons
            .get(&variant.to_ascii_lowercase())
            .or(base_record)
            .ok_or_else(|| format!("no weapon `{variant}` or `{base}` in the MW2 zones"))?;
        let base_record = base_record.unwrap_or(record);
        let gun = pick_gun_model(
            camo_model,
            record.gun.as_deref(),
            base_record.gun.as_deref(),
            |m| self.models.contains_key(&m.to_ascii_lowercase()),
        )
        .ok_or_else(|| format!("`{}` has no first-person model", record.name))?;
        let hands = std::env::var("MW2_HANDS")
            .ok()
            .or_else(|| record.hands.clone())
            .or_else(|| base_record.hands.clone())
            .unwrap_or_else(|| VIEWHANDS_NAME.to_owned());
        let anims = if record.anims.iter().any(Option::is_some) {
            &record.anims
        } else {
            &base_record.anims
        };
        let mut parts = Vec::with_capacity(2);
        for model in [hands.as_str(), gun.as_str()] {
            let skel = self
                .models
                .get(&model.to_ascii_lowercase())
                .ok_or_else(|| format!("viewmodel `{model}` is not in the MW2 zones"))?;
            parts.push(self.part(skel));
        }
        // A launcher's loaded round is a model of its own (`viewmodel_rpg7_rocket`), hung on the
        // gun's `tag_clip`, which the fire and reload clips move: out on a shot, in on a reload.
        if let Some(skel) = self
            .models
            .get(&format!("{}_rocket", gun.to_ascii_lowercase()))
        {
            parts.push(self.part(skel));
        }
        let load_clips = |names: &Vec<Option<String>>| -> Vec<Option<Arc<AnimClip>>> {
            names
                .iter()
                .map(|n| {
                    n.as_deref()
                        .and_then(|n| self.clips.clip(AssetNamespace::Iw4, n))
                })
                .collect()
        };
        // Akimbo: each hand plays its own set. MW2 ships left-hand clips for ordinary guns too
        // (its left-handed option), so only the akimbo variants are a pair.
        let akimbo = is_akimbo(&record.name) && record.anims_left.iter().any(Option::is_some);
        let clips = if akimbo && record.anims_right.iter().any(Option::is_some) {
            load_clips(&record.anims_right)
        } else {
            load_clips(anims)
        };
        let left_clips = akimbo.then(|| load_clips(&record.anims_left));
        Ok(LoadedWeapon {
            name: record.name.clone(),
            parts,
            clips,
            hide_tags: record.hide_tags.clone(),
            placement: record.placement,
            sounds: record.sounds.clone(),
            reload: record.reload,
            scope: record.scope.overlay.as_ref().map(|name| {
                Arc::new(Scope {
                    image: self.material_image(name),
                    width: record.scope.width,
                    height: record.scope.height,
                    thermal: is_thermal_overlay(name, &record.name),
                })
            }),
            ads_fov: (record.stats.ads_zoom_fov > 0.0).then_some(record.stats.ads_zoom_fov),
            // A rifle, sniper or LMG aims true (crate::spread::ADS_CAP_DEG).
            spread: record.stats.spread.aimed_capped(record.stats.weap_class),
            hand: 0,
            left_clips,
            dual_offset: record.dual_offset,
            main: self.main.clone(),
        })
    }

    fn part(&self, skel: &ModelSkel) -> Part {
        let mut cache: HashMap<usize, Option<Arc<RgbaImage>>> = HashMap::new();
        let mut baked: HashMap<(usize, usize, u32, u32), Arc<RgbaImage>> = HashMap::new();
        let surfaces = skel
            .surfaces_for_lod(0)
            .filter_map(|i| {
                let vertices = *skel.surface_vertex_ranges.get(i)?;
                let indices = *skel.surface_index_ranges.get(i)?;
                let material = skel
                    .surface_materials
                    .get(i)
                    .copied()
                    .flatten()
                    .and_then(|walk| self.material_rows.get(walk.get()).copied().flatten())
                    .and_then(|row| self.materials.materials.get(row));
                let image = material.and_then(|m| {
                    m.textures
                        .iter()
                        .find(|t| t.semantic == TS_COLOR_MAP)
                        .and_then(|t| t.image)
                });
                let technique = material
                    .map(|m| m.technique_set.to_string())
                    .unwrap_or_default();
                let name = material.map(|m| m.name.to_string()).unwrap_or_default();
                let screen = is_sight_screen(&name);
                let draw = if screen {
                    MaterialDrawMode::Blend
                } else {
                    material
                        .and_then(|m| self.materials.agreed_draw_mode(m))
                        .or_else(|| draw_from_technique(&technique))
                        .unwrap_or(MaterialDrawMode::Opaque)
                };
                let opacity = if screen {
                    SIGHT_SCREEN_OPACITY
                } else {
                    glass_opacity(&name, &technique)
                };
                let tint = reticle_tint(&technique);
                let mut color = image.and_then(|img| {
                    cache
                        .entry(img)
                        .or_insert_with(|| self.decode(img).map(Arc::new))
                        .clone()
                });
                // The detail map (a camo, or the plain gun's paint), baked in as MW2's shader
                // blends it, on surfaces that draw opaque.
                let detail = material.and_then(|m| {
                    let img = m
                        .textures
                        .iter()
                        .find(|t| t.semantic == TS_DETAIL_MAP)?
                        .image?;
                    let scale = m
                        .constants
                        .iter()
                        .find(|c| c.name.starts_with(b"detailScale"))
                        .map_or([1.0, 1.0], |c| [c.literal[0], c.literal[1]]);
                    Some((img, scale))
                });
                if draw == MaterialDrawMode::Opaque
                    && let (Some(base), Some((detail_img, scale))) = (&color, detail)
                    && let Some(d) = cache
                        .entry(detail_img)
                        .or_insert_with(|| self.decode(detail_img).map(Arc::new))
                        .clone()
                {
                    let key = (
                        Arc::as_ptr(base) as usize,
                        detail_img,
                        scale[0].to_bits(),
                        scale[1].to_bits(),
                    );
                    color = Some(
                        baked
                            .entry(key)
                            .or_insert_with(|| Arc::new(bake_detail(base, &d, scale)))
                            .clone(),
                    );
                }
                Some(Surface {
                    vertices,
                    indices,
                    color,
                    draw,
                    opacity,
                    tint,
                    part_bits: skel.surface_part_bits.get(i).copied().unwrap_or_default(),
                    motion_tracker: is_motion_tracker_screen(&name),
                })
            })
            .collect();
        Part {
            skel: Arc::new(skel.clone()),
            surfaces,
        }
    }

    /// A material's textures by name: semantic, image name, size, format, whether it decodes.
    pub fn describe_material(&self, name: &str) -> Vec<String> {
        let Some(m) = self
            .materials
            .materials
            .iter()
            .find(|m| m.name.to_string().eq_ignore_ascii_case(name))
        else {
            return vec![format!("no material {name}")];
        };
        m.textures
            .iter()
            .map(|t| {
                match t
                    .image
                    .and_then(|i| self.materials.images.get(i).map(|im| (i, im)))
                {
                    Some((i, im)) => format!(
                        "{}: {} {}x{} fmt={:#x} payload={} decodes={}",
                        t.semantic,
                        im.name,
                        im.width,
                        im.height,
                        im.format,
                        im.payload.len(),
                        self.decode(i).is_some()
                    ),
                    None => format!("{}: no image", t.semantic),
                }
            })
            .collect()
    }

    /// Every material name the walk kept (for finding icons and overlays).
    pub fn material_names(&self) -> impl Iterator<Item = String> + '_ {
        self.materials.materials.iter().map(|m| m.name.to_string())
    }

    /// A material's colour map by the material's name (an overlay, an icon), decoded.
    pub fn material_image(&self, name: &str) -> Option<Arc<RgbaImage>> {
        let material = self
            .materials
            .materials
            .iter()
            .find(|m| m.name.to_string().eq_ignore_ascii_case(name))?;
        let image = material
            .textures
            .iter()
            .find(|t| t.semantic == TS_COLOR_MAP)
            .or_else(|| material.textures.first())
            .and_then(|t| t.image)?;
        self.decode(image).map(Arc::new)
    }

    /// An image's top mip as RGBA8: from the zone when it carries the pixels, else from the
    /// install's IWDs.
    fn decode(&self, index: usize) -> Option<RgbaImage> {
        let image = self.materials.images.get(index)?;
        let from_zone = (!image.payload.is_empty())
            .then(|| {
                asset_material::decode_zone_image_rgba(
                    u32::from(image.width),
                    u32::from(image.height),
                    image.format,
                    &image.payload,
                )
                .ok()
            })
            .flatten();
        let (width, height, pixels) = from_zone.or_else(|| {
            asset_material::decode_ui_image_from_main(&self.main, image.name.as_str())
                .ok()
                .flatten()
        })?;
        Some(RgbaImage {
            width,
            height,
            pixels,
        })
    }
}

fn walk(path: &Path, sink: &mut WalkSink) -> Result<(), String> {
    let image = open_zone(path).map_err(|e| format!("{e:?}"))?;
    let header = image.header().map_err(|e| format!("{e:?}"))?;
    let mut memory = ZoneMemory::for_header(&header);
    let mut stream = memory.stream(&image.bytes).map_err(|e| format!("{e:?}"))?;
    load_zone(&mut stream, sink).map_err(|e| format!("{e:?}"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_reference_to_another_zones_asset_names_the_asset() {
        assert_eq!(
            asset_name(",viewmodel_model_1887"),
            Some("viewmodel_model_1887")
        );
        assert_eq!(asset_name("viewmodel_m4"), Some("viewmodel_m4"));
        assert_eq!(asset_name(","), None);
    }

    #[test]
    fn only_akimbo_variants_are_pairs() {
        assert!(super::is_akimbo("usp_akimbo_mp"));
        assert!(super::is_akimbo("glock_akimbo_fmj_mp"));
        assert!(!super::is_akimbo("fal_xmags_mp"));
        assert!(!super::is_akimbo("m4_mp"));
    }

    #[test]
    fn a_shared_definition_takes_its_owners_model_and_sounds() {
        let owner = DefOwner {
            gun: Some("viewmodel_famas".into()),
            hands: Some("viewhands_tf141".into()),
            sounds: WeaponSounds {
                fire: Some("weap_famas_fire_plr".into()),
                ..Default::default()
            },
            anims: vec![
                None,
                Some("viewmodel_famas_idle".into()),
                Some("viewmodel_famas_ads_up".into()),
            ],
            ..Default::default()
        };
        let mut shared = WeaponRecord::default(); // famas_xmags_mp: an alias, nothing of its own
        inherit(&mut shared, &owner);
        assert_eq!(shared.gun.as_deref(), Some("viewmodel_famas"));
        assert_eq!(shared.sounds.fire.as_deref(), Some("weap_famas_fire_plr"));
        // The variant aims, reloads and raises with the owner's clips.
        assert_eq!(shared.anims[2].as_deref(), Some("viewmodel_famas_ads_up"));
        let mut own = WeaponRecord {
            gun: Some("viewmodel_famas_silencer".into()),
            ..Default::default()
        };
        own.sounds.fire = Some("weap_famas_fire_silenced".into());
        inherit(&mut own, &owner); // its own names stand
        assert_eq!(own.sounds.fire.as_deref(), Some("weap_famas_fire_silenced"));
        assert_eq!(own.gun.as_deref(), Some("viewmodel_famas_silencer"));
    }

    #[test]
    fn a_camo_is_baked_where_the_paint_is() {
        use super::{RgbaImage, bake_detail};
        // Two texels: painted (alpha 255) and bare metal (alpha 0), both grey 100.
        let color = RgbaImage {
            width: 2,
            height: 1,
            pixels: vec![100, 100, 100, 255, 100, 100, 100, 0],
        };
        // A detail of one green texel at 0.5 red, 1.0 green, 0.25 blue.
        let detail = RgbaImage {
            width: 1,
            height: 1,
            pixels: vec![128, 255, 64, 255],
        };
        let out = bake_detail(&color, &detail, [1.0, 1.0]);
        assert_eq!(&out.pixels[0..4], &[100, 200, 50, 255]); // painted: x2 detail
        assert_eq!(&out.pixels[4..8], &[100, 100, 100, 255]); // metal: untouched
    }

    #[test]
    fn the_night_scope_is_thermal() {
        use super::is_thermal_overlay;
        assert!(is_thermal_overlay(
            "scope_overlay_m14_night",
            "cheytac_thermal_mp"
        ));
        assert!(is_thermal_overlay(
            "scope_overlay_m14_night",
            "m4_thermal_mp"
        ));
        assert!(!is_thermal_overlay("scope_overlay_m40a3", "cheytac_mp"));
    }

    #[test]
    fn scope_glass_and_reticles_are_see_through() {
        use super::{draw_from_technique, glass_opacity};
        use asset_material::MaterialDrawMode as M;
        // The ACOG's, red dot's and holographic's glass: a lit blend, mostly clear.
        assert_eq!(draw_from_technique("mc_l_sm_b0c0n0s0p0"), Some(M::Blend));
        assert!(glass_opacity("mc/mtl_weapon_acog_2_lens", "mc_l_sm_b0c0n0s0") < 0.2);
        assert!(glass_opacity("mc/mtl_weapon_eotech_2_lense", "mc_l_sm_b0c0n0s0p0") < 0.2);
        // The red dot and the holographic's projected reticle glow.
        assert_eq!(draw_from_technique("mc_reflexsight"), Some(M::Additive));
        // The ACOG's reticle blends by its alpha.
        assert_eq!(draw_from_technique("mc_effect_replace"), Some(M::Blend));
        // The gun itself stays opaque.
        assert_eq!(draw_from_technique("mc_l_sm_r0c0d0n0s0p0"), None);
        assert_eq!(
            glass_opacity("mc/mtl_weapon_m4_short", "mc_l_sm_r0c0d0n0s0p0"),
            1.0
        );
        // The thermal sight's screens over the eyepiece (opaque in the install) see through at
        // the hip, like the other sights' glass.
        assert!(super::is_motion_tracker_screen(
            "mc/mtl_motion_tracker_screen"
        ));
        assert!(!super::is_motion_tracker_screen(
            "mc/mtl_weapon_motion_tracker"
        )); // its case
        assert!(super::is_sight_screen("mc/mtl_weapon_thermal_screen"));
        assert!(super::is_sight_screen("mc/mtl_weapon_thermal_screen_off"));
        assert!(!super::is_sight_screen("mc/mtl_weapon_thermal"));
        // The sniper scopes' eyepiece cap: plain white, opaque in the install.
        assert!(super::is_sight_screen("mc/mtl_weapon_scope_white"));
        assert!(!super::is_sight_screen("mc/mtl_weapon_scope01"));
        // Reflex reticles are red; the rest untinted.
        assert!(super::reticle_tint("mc_reflexsight")[1] < 0.5);
        assert_eq!(super::reticle_tint("mc_effect_replace"), [1.0, 1.0, 1.0]);
    }

    #[test]
    fn a_variant_without_a_model_uses_the_base_gun() {
        let none = |_: &str| false;
        assert_eq!(
            super::pick_gun_model(None, None, Some("viewmodel_m4"), none).as_deref(),
            Some("viewmodel_m4")
        );
        assert_eq!(
            super::pick_gun_model(None, Some("viewmodel_m4_x"), Some("viewmodel_m4"), none)
                .as_deref(),
            Some("viewmodel_m4_x")
        );
    }

    #[test]
    fn a_camo_picks_its_model() {
        let has = |m: &str| m == "viewmodel_m4_woodland";
        assert_eq!(
            super::pick_gun_model(
                Some("viewmodel_m4_woodland"),
                None,
                Some("viewmodel_m4"),
                has
            )
            .as_deref(),
            Some("viewmodel_m4_woodland")
        );
        // A camo the install lacks: the plain gun.
        assert_eq!(
            super::pick_gun_model(Some("viewmodel_m4_arctic"), None, Some("viewmodel_m4"), has)
                .as_deref(),
            Some("viewmodel_m4")
        );
    }
    use super::*;

    #[test]
    fn viewmodels_are_recognised() {
        assert!(is_viewmodel("viewmodel_m4"));
        assert!(is_viewmodel("viewhands_us_army"));
        assert!(!is_viewmodel("weapon_m4"));
        assert!(!is_viewmodel("body_us_army_ar"));
    }

    #[test]
    fn world_guns_are_kept_for_npcs() {
        assert!(is_world_gun("weapon_m4"));
        assert!(is_world_gun("WEAPON_AK47_TACTICAL"));
        assert!(!is_world_gun("viewmodel_m4"));
        assert!(!is_world_gun("body_us_army_ar"));
    }

    #[test]
    fn a_long_stock_does_not_turn_the_gun_around() {
        use bevy::math::Vec3;
        // weapon_ak47_tactical: grip x -2.6, the stock out to -22 (the mesh's longer side),
        // tag_flash at +14.2: the flash decides.
        let grip = Vec3::new(-2.64, 0.0, -2.8);
        let m = super::muzzle_from(grip, Some(Vec3::new(14.24, 0.0, 0.0)), Vec3::new(-22.0, 0.0, -2.8));
        assert!(m.x > grip.x, "the barrel is forward of the grip");
        // weapon_usp: grip x -10, tag_flash at -4.47: forward of the grip, so +X.
        let grip = Vec3::new(-10.0, 0.0, -1.7);
        let m = super::muzzle_from(grip, Some(Vec3::new(-4.47, 0.0, 0.3)), Vec3::new(-4.19, 0.0, -1.7));
        assert!(m.x > grip.x);
        // No tag: the mesh's.
        assert_eq!(super::muzzle_from(grip, None, Vec3::X), Vec3::X);
    }

    #[test]
    fn a_guns_grip_is_under_its_handle_and_its_muzzle_the_long_side() {
        use bevy::math::Vec3;
        // A pistol facing -X: the butt at x 2, hanging to z -5; the slide out to x -6.
        let pistol = [[2.0, 0.0, -5.0], [3.0, 0.0, 1.0], [-6.0, 0.0, 1.0], [-6.0, 0.0, 0.0]];
        let (grip, muzzle) = super::grip_and_muzzle(&pistol).unwrap();
        assert_eq!(grip.x, 2.0);
        assert_eq!(muzzle.x, -6.0, "the long side is the barrel");
        // A rifle: the magazine lowest at x 1, the stock back to -9, the barrel to +14.
        let rifle = [[1.0, 0.0, -6.0], [-9.0, 0.0, 0.0], [14.0, 0.0, 1.0]];
        let (grip, muzzle) = super::grip_and_muzzle(&rifle).unwrap();
        assert_eq!((grip.x, muzzle.x), (1.0, 14.0));
        assert!(grip.z > -6.0 && grip.z < 1.0, "lifted into the gun");
        assert_eq!(super::grip_and_muzzle(&[]), None);
        let _ = Vec3::ZERO;
    }

    #[test]
    fn a_world_guns_muzzle_is_its_front() {
        let muzzle = front_point(&[[0.0, 0.0, 0.0], [20.0, 0.5, 2.0], [-6.0, 0.0, -3.0]]);
        assert_eq!(muzzle, Some(bevy::math::Vec3::new(20.0, 0.5, 2.0)));
        assert_eq!(front_point(&[]), None);
    }
}
