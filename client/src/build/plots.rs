//! The homestead plots near the player, marked in the world as Azeroth marks a claim: the
//! player's own plot with a battle banner at each corner, a fence post every few yards along its
//! edges and a faint warm line on the ground along them; a plot the player is permitted on with
//! posts at its corners and a fainter cool line; anyone else's claimed plot with a stone post at
//! each corner and nothing more. The server sends the claimed plots near (the world channel's
//! op 55, `Homestead.h`); the markers are WoW's own doodads, drawn the way build pieces are, set
//! on the ground under each corner and post, and only for plots near the camera.

use avian3d::prelude::*;
use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};
use benilla_world::view::WorldCamera;
use bevy::asset::RenderAssetUsages;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use super::models::{self, Template};
use super::pieces::PieceModel;
use super::render::{PieceMesh, root_transform};
use super::wire::{self, NearPlot};
use super::{ModelParams, PieceBox};
use crate::Mw2Set;
use crate::fusion::to_wow;
use crate::tracer::from_wow;

/// The claimed plots near, as the server last sent them.
#[derive(Resource, Default)]
pub struct NearPlots {
    pub plots: Vec<NearPlot>,
}

/// The markers drawn, by plot: what they were drawn as, and their root.
#[derive(Resource, Default)]
struct Markers {
    drawn: HashMap<u32, (u8, [i32; 4], Entity)>,
    world: Option<Entity>,
}

/// A plot's markers' root.
#[derive(Component)]
struct PlotMarker;

/// The doodads: Elwynn's battle banner, its wooden fence post and its stone fence post.
const BANNER: &str =
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\BattleGladeBanner1\\BattleGladeBanner1.m2";
const WOOD_POST: &str = "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnWoodPost01.m2";
const STONE_POST: &str =
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnStoneFencePost.m2";

/// Plots are marked while some of them is this near the camera (yards), and cleared past the
/// far distance.
pub const DRAW_NEAR: f32 = 140.0;
/// The piece id a plot marker's meshes carry for crate::build::render::relight: lit as the
/// world's doodads are and following the light as it moves, with no piece's tint.
pub const MARKER_ID: u32 = u32::MAX;
pub const DRAW_FAR: f32 = 180.0;
/// Posts along the player's own plot's edges, about this far apart.
pub const POST_SPACING: f32 = 6.0;
/// The ground line: sampled this often along an edge, this wide, this far over the ground.
const LINE_STEP: f32 = 1.0;
const LINE_WIDTH: f32 = 0.5;
const LINE_LIFT: f32 = 0.06;
/// Markers stand this far inside the plot's edge (its build margin is a yard).
const INSET: f32 = 0.4;
/// At most this many plots are marked in a frame (each asks a few hundred ground rays).
const PER_FRAME: usize = 2;

pub struct PlotMarkersPlugin;

impl Plugin for PlotMarkersPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<NearPlots>()
            .init_resource::<Markers>()
            .add_systems(Update, (receive, draw_markers).chain().in_set(Mw2Set::Hud));
    }
}

fn receive(mut msgs: MessageReader<FusionChannelReceived>, mut near: ResMut<NearPlots>) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD || m.body.first() != Some(&wire::OP_NEAR_PLOTS) {
            continue;
        }
        match wire::decode_near_plots(&m.body) {
            Some(list) => near.plots = list,
            None => warn!("benilla-mw2: a plots-near message that does not decode"),
        }
    }
}

/// How a plot is marked: its corner doodad, whether posts run along its edges, and its ground
/// line's colour (none for anyone else's).
pub struct Look {
    pub corner: &'static str,
    pub edge_posts: bool,
    pub line: Option<Color>,
}

pub fn look(relation: u8) -> Look {
    match relation {
        wire::NEAR_MINE => Look {
            corner: BANNER,
            edge_posts: true,
            line: Some(Color::srgb(0.55, 0.42, 0.14)),
        },
        wire::NEAR_PERMITTED => Look {
            corner: WOOD_POST,
            edge_posts: false,
            line: Some(Color::srgb(0.16, 0.28, 0.38)),
        },
        _ => Look {
            corner: STONE_POST,
            edge_posts: false,
            line: None,
        },
    }
}

/// The corners of a plot's markers, inset, counter-clockwise from (x0, y0), with each one's
/// facing (out from the plot's middle, WoW radians).
pub fn corners(b: [f32; 4]) -> [(Vec2, f32); 4] {
    let (x0, y0, x1, y1) = (b[0] + INSET, b[1] + INSET, b[2] - INSET, b[3] - INSET);
    let mid = Vec2::new((x0 + x1) / 2.0, (y0 + y1) / 2.0);
    [
        Vec2::new(x0, y0),
        Vec2::new(x1, y0),
        Vec2::new(x1, y1),
        Vec2::new(x0, y1),
    ]
    .map(|p| {
        let out = p - mid;
        (p, out.y.atan2(out.x))
    })
}

/// The posts along a plot's edges between its corners, about `POST_SPACING` apart, each facing
/// along its edge.
pub fn edge_posts(b: [f32; 4]) -> Vec<(Vec2, f32)> {
    let c = corners(b);
    let mut out = Vec::new();
    for i in 0..4 {
        let (a, z) = (c[i].0, c[(i + 1) % 4].0);
        let len = a.distance(z);
        let n = (len / POST_SPACING).round().max(1.0) as usize;
        let along = (z - a).y.atan2((z - a).x);
        for k in 1..n {
            out.push((a.lerp(z, k as f32 / n as f32), along));
        }
    }
    out
}

/// The points along a plot's edge loop, `LINE_STEP` apart, closing on the first.
pub fn edge_loop(b: [f32; 4]) -> Vec<Vec2> {
    let c = corners(b);
    let mut out = Vec::new();
    for i in 0..4 {
        let (a, z) = (c[i].0, c[(i + 1) % 4].0);
        let n = (a.distance(z) / LINE_STEP).ceil().max(1.0) as usize;
        for k in 0..n {
            out.push(a.lerp(z, k as f32 / n as f32));
        }
    }
    out.push(c[0].0);
    out
}

/// Quantised bounds: a plot redrawn when its bounds move.
fn key(b: [f32; 4]) -> [i32; 4] {
    b.map(|v| (v * 4.0).round() as i32)
}

/// How far a point (WoW x, y) is from a plot's bounds.
pub fn distance_to(b: [f32; 4], p: Vec2) -> f32 {
    let dx = (b[0] - p.x).max(0.0).max(p.x - b[2]);
    let dy = (b[1] - p.y).max(0.0).max(p.y - b[3]);
    (dx * dx + dy * dy).sqrt()
}

#[allow(clippy::too_many_arguments)]
fn draw_markers(
    mut commands: Commands,
    near: Res<NearPlots>,
    mut markers: ResMut<Markers>,
    cameras: Query<(Entity, &GlobalTransform), With<WorldCamera>>,
    spatial: SpatialQuery,
    boxes: Query<(), With<PieceBox>>,
    roots: Query<Entity, With<PlotMarker>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut models: ModelParams,
) {
    let cam = cameras.iter().next();
    // A new world (a new map, a reconnect): the markers go, to be set again on its ground.
    let world = cam.map(|(e, _)| e);
    if markers.world != world {
        markers.world = world;
        markers.drawn.clear();
        for e in &roots {
            commands.entity(e).despawn();
        }
    }
    let Some((_, cam)) = cam else {
        return;
    };
    let eye = Vec3::from_array(to_wow(cam.translation()));
    let at = eye.truncate();
    // Gone, moved, changed hands or out of range: cleared.
    let stale: Vec<u32> = markers
        .drawn
        .iter()
        .filter(|(id, (rel, k, _))| {
            !near.plots.iter().any(|p| {
                p.id == **id
                    && p.relation == *rel
                    && key(p.bounds) == *k
                    && distance_to(p.bounds, at) < DRAW_FAR
            })
        })
        .map(|(id, _)| *id)
        .collect();
    for id in stale {
        if let Some((_, _, e)) = markers.drawn.remove(&id) {
            commands.entity(e).despawn();
        }
    }
    let filter = benilla_world::collision::WorldCollision::body_filter();
    // The ground under a point: the terrain and the world's models, never a built piece.
    let ground = |p: Vec2| -> Option<f32> {
        let from = from_wow([p.x, p.y, eye.z + 80.0]);
        spatial
            .cast_ray_predicate(from, Dir3::NEG_Y, 400.0, true, &filter, &|e| {
                !boxes.contains(e)
            })
            .map(|h| from.y - h.distance)
    };
    let mut built = 0;
    for p in &near.plots {
        if built >= PER_FRAME {
            break;
        }
        if markers.drawn.contains_key(&p.id) || distance_to(p.bounds, at) > DRAW_NEAR {
            continue;
        }
        let look = look(p.relation);
        // Every corner on the ground, or wait for the terrain to stream in.
        let cs = corners(p.bounds);
        let Some(corner_z) = cs
            .iter()
            .map(|(c, _)| ground(*c))
            .collect::<Option<Vec<f32>>>()
        else {
            continue;
        };
        // Its doodads read from the install (or wait for them).
        let corner_model = match models.get(PieceModel::Wow(look.corner)) {
            models::ModelState::Ready(t) => Some(t),
            models::ModelState::Pending => continue,
            models::ModelState::Failed => None,
        };
        let post_model = if look.edge_posts {
            match models.get(PieceModel::Wow(WOOD_POST)) {
                models::ModelState::Ready(t) => Some(t),
                models::ModelState::Pending => continue,
                models::ModelState::Failed => None,
            }
        } else {
            None
        };
        built += 1;
        let root = commands
            .spawn((
                Name::new(format!("mw2 plot {} markers", p.id)),
                PlotMarker,
                Transform::default(),
                Visibility::Visible,
            ))
            .id();
        let mut place = |templates: &[Template], pos: Vec2, z: f32, yaw: f32| {
            let doodad = commands
                .spawn((
                    root_transform(Vec3::new(pos.x, pos.y, z), yaw),
                    Visibility::Inherited,
                    ChildOf(root),
                ))
                .id();
            for t in templates {
                // Lit by crate::build::render::relight as the world's doodads are (their lobe,
                // their own colour), and again whenever the scene's light moves.
                commands.spawn((
                    Mesh3d(meshes.add(t.mesh.clone())),
                    MeshMaterial3d(t.material.clone()),
                    Transform::default(),
                    PieceMesh::model(MARKER_ID, yaw, t),
                    bevy::light::NotShadowCaster,
                    ChildOf(doodad),
                ));
            }
        };
        if let Some(t) = &corner_model {
            for ((c, yaw), z) in cs.iter().zip(&corner_z) {
                place(t, *c, *z, *yaw);
            }
        }
        if let Some(t) = &post_model {
            for (c, yaw) in edge_posts(p.bounds) {
                if let Some(z) = ground(c) {
                    place(t, c, z, yaw);
                }
            }
        }
        if let Some(color) = look.line {
            let points: Vec<Vec3> = edge_loop(p.bounds)
                .into_iter()
                .filter_map(|c| ground(c).map(|z| Vec3::new(c.x, c.y, z + LINE_LIFT)))
                .collect();
            if points.len() >= 2 {
                commands.spawn((
                    Mesh3d(meshes.add(ground_line(&points))),
                    MeshMaterial3d(models.materials.add(StandardMaterial {
                        base_color: color,
                        unlit: true,
                        alpha_mode: AlphaMode::Add,
                        cull_mode: None,
                        ..default()
                    })),
                    Transform::default(),
                    bevy::light::NotShadowCaster,
                    bevy::picking::Pickable::IGNORE,
                    ChildOf(root),
                ));
            }
        }
        markers
            .drawn
            .insert(p.id, (p.relation, key(p.bounds), root));
    }
}

/// A soft line on the ground through `points` (WoW, on the ground): three rows across its width,
/// bright in the middle and dark at both edges, so its additive glow fades out sideways.
pub fn ground_line(points: &[Vec3]) -> Mesh {
    let mut positions = Vec::new();
    let mut colors = Vec::new();
    let mut indices = Vec::new();
    let n = points.len();
    for i in 0..n {
        let prev = points[i.saturating_sub(1)];
        let next = points[(i + 1).min(n - 1)];
        let along = (next - prev).truncate().normalize_or_zero();
        let side = Vec2::new(-along.y, along.x) * (LINE_WIDTH / 2.0);
        let p = points[i];
        for (offset, bright) in [(-1.0, 0.0), (0.0, 1.0), (1.0, 0.0)] {
            let q = p + (side * offset).extend(0.0);
            positions.push(from_wow(q.to_array()).to_array());
            colors.push([bright, bright, bright, 1.0]);
        }
        if i + 1 < n {
            let a = (i * 3) as u32;
            let b = a + 3;
            for k in 0..2 {
                indices.extend_from_slice(&[a + k, b + k, a + k + 1, a + k + 1, b + k, b + k + 1]);
            }
        }
    }
    let count = positions.len();
    let mut m = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    );
    m.insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    m.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0, 1.0, 0.0]; count]);
    m.insert_attribute(Mesh::ATTRIBUTE_UV_0, vec![[0.0, 0.0]; count]);
    m.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
    m.insert_indices(Indices::U32(indices));
    m
}

#[cfg(test)]
mod tests {
    use super::*;

    const PLOT: [f32; 4] = [1000.0, 2000.0, 1024.0, 2024.0];

    #[test]
    fn the_players_own_plot_is_marked_plainest() {
        assert!(look(wire::NEAR_MINE).edge_posts && look(wire::NEAR_MINE).line.is_some());
        assert_eq!(look(wire::NEAR_MINE).corner, BANNER);
        // Others': a stone post at each corner, nothing between, no line.
        let other = look(wire::NEAR_CLAIMED);
        assert!(!other.edge_posts && other.line.is_none() && other.corner == STONE_POST);
        assert!(look(wire::NEAR_PERMITTED).line.is_some());
    }

    #[test]
    fn corners_stand_just_inside_and_face_out() {
        let c = corners(PLOT);
        assert_eq!(c[0].0, Vec2::new(1000.4, 2000.4));
        assert_eq!(c[2].0, Vec2::new(1023.6, 2023.6));
        // (x0, y0) faces away from the middle: down-left.
        assert!((c[0].1 - (-3.0 * std::f32::consts::FRAC_PI_4)).abs() < 1e-4);
    }

    #[test]
    fn posts_run_the_edges_about_six_yards_apart() {
        // A 24 yard plot: four spans an edge, three posts between its corners.
        let posts = edge_posts(PLOT);
        assert_eq!(posts.len(), 4 * 3);
        assert!(posts.iter().all(|(p, _)| {
            (p.x - 1000.4).abs() < 1e-3
                || (p.x - 1023.6).abs() < 1e-3
                || (p.y - 2000.4).abs() < 1e-3
                || (p.y - 2023.6).abs() < 1e-3
        }));
        // The line goes round and closes, a yard a step.
        let line = edge_loop(PLOT);
        assert_eq!(line.first(), line.last());
        assert!(line.len() >= 4 * 23);
        assert_eq!(distance_to(PLOT, Vec2::new(1010.0, 2010.0)), 0.0);
        assert_eq!(distance_to(PLOT, Vec2::new(1034.0, 2010.0)), 10.0);
    }

    #[test]
    fn the_ground_line_fades_to_its_edges() {
        let points = [
            Vec3::new(0.0, 0.0, 10.0),
            Vec3::new(1.0, 0.0, 10.0),
            Vec3::new(2.0, 0.0, 10.5),
        ];
        let m = ground_line(&points);
        assert_eq!(m.count_vertices(), 9);
        let Some(Indices::U32(i)) = m.indices() else {
            panic!("indices");
        };
        assert_eq!(i.len(), 2 * 2 * 6);
        let Some(bevy::mesh::VertexAttributeValues::Float32x4(c)) =
            m.attribute(Mesh::ATTRIBUTE_COLOR)
        else {
            panic!("colours");
        };
        assert_eq!((c[0][0], c[1][0], c[2][0]), (0.0, 1.0, 0.0));
    }
}
