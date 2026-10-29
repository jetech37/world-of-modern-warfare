//! A placed model's point lights, particle emitters and ribbon trails, baked at its placement.

use benilla_assets::coords::wow_to_bevy;
use benilla_assets::{ModelEmitter, ModelLight};
use benilla_formats::WmoLight;
use bevy::prelude::*;

use crate::lighting::WorldPointLight;
use crate::particles;

/// Unit intensity in the `PointLight` convention, which the packer's `/(4π)` undoes to exactly
/// `diffuse_color × diffuse_intensity`; `wow_model.wgsl` applies the falloff `1/(0.7d + 0.03d²)`.
const POINT_LIGHT_INTENSITY: f32 = 4.0 * std::f32::consts::PI;
/// The ≤3-nearest selection radius (yd) around the lit unit's anchor, not a cutoff (a GL light has
/// none; att(48 yd) ≈ 0.01), wide enough to reach a hall's fixtures ~20 yd from its centre.
const POINT_LIGHT_RANGE: f32 = 48.0;

/// The [`WorldPointLight`] for an authored M2 or WMO MOLT light, `color` its linear RGB. As in the
/// reference it lights terrain, M2s and WMO walls, each taking its unit's ≤3 nearest, diffuse only.
pub fn point_light(color: [f32; 3], intensity_scale: f32) -> WorldPointLight {
    WorldPointLight {
        color,
        intensity: POINT_LIGHT_INTENSITY * intensity_scale.max(0.0),
        range: POINT_LIGHT_RANGE,
    }
}

fn spawn_point_light(
    commands: &mut Commands,
    world: Vec3,
    color: [f32; 3],
    intensity_scale: f32,
    // The light's rooms inside a building, `None` outdoors: a torch prop in a culled room is not
    // in the reference's scene that frame, so it lights nothing.
    room: Option<crate::wmo_portal::WmoGroupVis>,
    out: &mut Vec<Entity>,
) {
    let mut e = commands.spawn((
        point_light(color, intensity_scale),
        Transform::from_translation(world),
    ));
    if let Some(room) = room {
        e.insert(crate::lighting::LightRooms(room));
    }
    out.push(e.id());
}

/// Spawns a light for each casting M2 point light (`type == 1`) at its rest position, since a
/// placed prop never animates its light bone; `room` comes off the placement's [`emitter_fade`].
/// A lamp-like model that authors none gets the atmosphere's stand-in ([`lamp_stand_in`]).
pub(super) fn spawn_lights_for(
    commands: &mut Commands,
    lights: &[ModelLight],
    transform: Transform,
    room: Option<&crate::wmo_portal::WmoGroupVis>,
    out: &mut Vec<Entity>,
    model: (&str, Option<&benilla_formats::M2Bounds>),
) {
    if !lights.iter().any(|l| l.def.casts()) {
        if let Some((local, color)) = lamp_stand_in(model.0, model.1) {
            let world = transform.transform_point(local);
            let mut e = commands.spawn((
                point_light(color, 1.0),
                crate::lighting::AtmosphereLamp,
                Transform::from_translation(world),
            ));
            if let Some(room) = room {
                e.insert(crate::lighting::LightRooms(room.clone()));
            }
            out.push(e.id());
        }
        return;
    }
    for l in lights.iter().map(|l| &l.def) {
        if !l.casts() {
            continue; // directional lights feed the ambient; a static `0` visibility key is dark
        }
        let world = transform.transform_point(wow_to_bevy(l.position));
        spawn_point_light(
            commands,
            world,
            l.diffuse_color,
            l.diffuse_intensity,
            room.cloned(),
            out,
        );
    }
}

/// The atmosphere's stand-in light for a lamp-like doodad that authors no M2 light (street lamps,
/// lanterns, candles, braziers by file name): `(model-space Bevy position, linear colour)`, at
/// the top fifth of its box, where the flame or glass sits. It lights only through the
/// atmosphere's per-pixel lamps ([`crate::lighting::AtmosphereLamp`]), never the reference's
/// per-vertex three.
pub(crate) fn lamp_stand_in(
    path: &str,
    bounds: Option<&benilla_formats::M2Bounds>,
) -> Option<(Vec3, [f32; 3])> {
    let name = path
        .rsplit(['/', '\\'])
        .next()
        .unwrap_or(path)
        .to_ascii_lowercase();
    // Colour × intensity, the authored convention: a candle is a dim warm point, a brazier or
    // torch a strong orange, a lamp or lantern a warm yellow.
    let color = if name.contains("candle") {
        [0.55, 0.38, 0.18]
    } else if name.contains("brazier") || name.contains("torch") || name.contains("firepit") {
        [1.3, 0.75, 0.3]
    } else if name.contains("lamp") || name.contains("lantern") || name.contains("sconce") {
        [1.15, 0.85, 0.45]
    } else {
        return None;
    };
    let b = bounds?;
    let lo = bevy::math::Vec3::from(b.bbox_min);
    let hi = bevy::math::Vec3::from(b.bbox_max);
    if (hi - lo).max_element() <= 0.0 {
        return None;
    }
    // WoW model space is z-up: the top fifth of the box, centred over its footprint.
    let c = (lo + hi) * 0.5;
    let wow = [c.x, c.y, lo.z + (hi.z - lo.z) * 0.85];
    Some((wow_to_bevy(wow), color))
}

/// Spawns a light for each omni MOLT light (`type == 0`) under the M2 falloff, which the disk
/// `attenStart`/`attenEnd` do not shape (`0x695c00`). A light belongs to the rooms whose MOLR
/// names it; one that no group names is ungated.
pub(super) fn spawn_wmo_lights_for(
    commands: &mut Commands,
    lights: &[WmoLight],
    group_light_refs: &[Vec<u16>],
    instance: Option<Entity>,
    transform: Transform,
    out: &mut Vec<Entity>,
) {
    for (i, l) in lights.iter().enumerate() {
        if !l.is_omni() {
            continue; // spot/directional/ambient MOLT types are not point sources
        }
        let i = i as u16;
        let groups: std::sync::Arc<[u16]> = group_light_refs
            .iter()
            .enumerate()
            .filter(|(_, refs)| refs.contains(&i))
            .map(|(g, _)| g as u16)
            .collect();
        let world = transform.transform_point(wow_to_bevy(l.position));
        spawn_point_light(
            commands,
            world,
            l.color,
            l.intensity,
            instance
                .filter(|_| !groups.is_empty())
                .map(|instance| crate::wmo_portal::WmoGroupVis { instance, groups }),
            out,
        );
    }
}

/// A placed model's one draw-set gate, shared by its mesh, emitters, ribbons, lights and anim host.
/// `fade` is `(radius, model-space centre)`; `None` or empty `groups` means no room gate.
pub(super) fn emitter_fade(
    transform: Transform,
    fade: (f32, Vec3),
    instance: Option<Entity>,
    groups: Option<&std::sync::Arc<[u16]>>,
) -> particles::EmitterFade {
    let room = instance.zip(groups.filter(|g| !g.is_empty()));
    particles::EmitterFade {
        instance,
        room: room.map(|(instance, groups)| crate::wmo_portal::WmoGroupVis {
            instance,
            groups: groups.clone(),
        }),
        ..particles::EmitterFade::sphere(fade.0, transform.transform_point(fade.1))
    }
}

/// Spawns a model's emitters into `out`. With an anim `host` each rides its bone's anchor, which
/// holds a bone that never animates at its rest pivot, so riding is exact for every emitter.
pub(super) fn spawn_emitters_for(
    commands: &mut Commands,
    emitters: &[ModelEmitter],
    transform: Transform,
    host: Option<&super::assemble::PlacementHost>,
    fade: &particles::EmitterFade,
    out: &mut Vec<Entity>,
) {
    let arm = host.and_then(|h| h.arm);
    for em in emitters {
        let owner = host
            .and_then(|h| h.anchor(em.def.bone))
            .map(|a| (a, em.bone_pivot));
        if let Some(e) = particles::spawn_emitter(
            commands,
            em,
            transform,
            particles::EmitterFrames {
                owner,
                anchor: None, // anchor at the placement: an animated bone never drags the cloud
                // An animated doodad's rig goes only when its placement unloads, the model's end,
                // so the pool is freed rather than left draining at the old spot.
                on_owner_loss: particles::OwnerLoss::Free,
                ..default()
            },
            // A placed doodad re-rolls its variation every play window, so its emitters read the
            // slot and clip time off the host's live player, as the reference's animate kernel
            // (`0x714260`) does for every model.
            match arm {
                Some(root) => particles::EmitClock::Host(root),
                None => particles::EmitClock::Pinned,
            },
        ) {
            commands.entity(e).insert(fade.clone());
            out.push(e);
        }
    }
}

/// Spawns a model's ribbon trails, each riding its host bone's anchor or, on a static placement,
/// `fallback_owner`; a trail despawns with its owner, so none joins a despawn list.
pub(super) fn spawn_ribbons_for(
    commands: &mut Commands,
    ribbons: &[benilla_assets::ModelRibbon],
    transform: Transform,
    host: Option<&super::assemble::PlacementHost>,
    fallback_owner: Option<Entity>,
    // The emitters' gate, carried because a joint or submesh owner states no fade sphere or rooms.
    fade: &particles::EmitterFade,
) {
    let arm = host.and_then(|h| h.arm);
    for rb in ribbons {
        let (owner, use_pivot) = match host.and_then(|h| h.anchor(rb.def.bone)) {
            Some(a) => (a, true),
            None => match fallback_owner {
                Some(e) => (e, false),
                None => continue, // nothing to ride (an entity-less placement)
            },
        };
        // The `+0xc0` enable gate reads the live sequence, as the emitters do; an unanimated
        // placement has no clock and rests in `Stand`.
        crate::ribbons::spawn_ribbon(
            commands,
            rb,
            owner,
            use_pivot,
            transform.scale.max_element(),
            arm.map_or(
                crate::ribbons::RibbonSeq::Fixed(0),
                crate::ribbons::RibbonSeq::Host,
            ),
            // No model-alpha source: a placed prop / effect instance is always drawn.
            None,
            Some(fade.clone()),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bounds(min: [f32; 3], max: [f32; 3]) -> benilla_formats::M2Bounds {
        benilla_formats::M2Bounds {
            sphere_radius: 1.0,
            bbox_min: min,
            bbox_max: max,
            vert_max_from_origin: 1.0,
            ring_footprint: 1.0,
            pivot_z: None,
            stand_box_z: max[2] - min[2],
            swim_pivot_drop: 0.0,
        }
    }

    /// A lamp-like doodad gets a stand-in near its top; anything else, or a boxless model, none.
    #[test]
    fn a_lamp_without_a_light_gets_a_stand_in_at_its_top() {
        let b = bounds([-0.5, -0.5, 0.0], [0.5, 0.5, 4.0]);
        let (pos, color) = lamp_stand_in(
            "World\\Generic\\Human\\Passive Doodads\\StreetLamps\\StreetLamp01.m2",
            Some(&b),
        )
        .expect("a street lamp");
        assert!(
            (pos.y - 3.4).abs() < 1e-4,
            "the top of a 4 yd post: {pos:?}"
        );
        assert!(color[0] > color[2], "a warm light");
        assert!(lamp_stand_in("World/Generic/Tree01.m2", Some(&b)).is_none());
        assert!(lamp_stand_in("World/Generic/Candle01.m2", None).is_none());
        let flat = bounds([0.0; 3], [0.0; 3]);
        assert!(lamp_stand_in("World/Generic/Brazier.m2", Some(&flat)).is_none());
    }
}
