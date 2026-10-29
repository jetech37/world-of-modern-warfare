//! Drawing the pieces: each in its builder's style (crate::build::look) as textured boxes with
//! their trim, in WoW's own world textures read from the player's install, or as WoW's models.
//! Lit as WoW lights the world (from the scene's `Light.dbc` sun, baked into vertex colours and
//! redone as the light moves): the boxes as a WMO's exterior, `clamp(ambient + diffuse ·
//! max(N·L, 0))` with a little shade at their foot, and the models as an exterior doodad,
//! `clamp(ambient + diffuse · (4/17)(0.375 + 2μ + 1.875μ²))` times the model's own colour (its
//! M2Color), so they sit in the scene as the world's own do. Darker as they are shot up, and in
//! build mode tinted by how well they are held up. Every collision box is also a static collider
//! on the body's layer: floors, stairs and roofs carry you; walls stop you.

use std::sync::Arc;

use avian3d::prelude::*;
use bevy::asset::RenderAssetUsages;
use bevy::image::{ImageAddressMode, ImageSampler, ImageSamplerDescriptor};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use super::look::{self, DrawBox, Tex, Uv};
use super::pieces::{BoxDef, PieceType, boxes_of, structure};
use super::{Building, Flame, Piece, PieceBox, PieceRoot};
use crate::tracer::from_wow;

const FLAME: &str = "mpq://Particles/Flame02.blp";

#[derive(Resource)]
pub struct BuildAssets {
    /// A material for each texture the styles and the structures' blocks use, by its path.
    pub textures: HashMap<&'static str, Handle<StandardMaterial>>,
    pub images: Vec<Handle<Image>>,
    /// For a texture the table lacks: plain grey.
    pub fallback: Handle<StandardMaterial>,
    pub ghost_ok: Handle<StandardMaterial>,
    pub ghost_bad: Handle<StandardMaterial>,
    /// Each style's flame.
    pub flames: Vec<Handle<StandardMaterial>>,
    pub flame_quad: Handle<Mesh>,
}

impl BuildAssets {
    pub fn material(&self, path: &str) -> Handle<StandardMaterial> {
        self.textures
            .get(path)
            .cloned()
            .unwrap_or_else(|| self.fallback.clone())
    }
}

/// The scene's light as the pieces take it, and a counter bumped when it moves.
#[derive(Resource, Default, Clone, Copy, PartialEq)]
pub struct BuildLight {
    pub ambient: Vec3,
    pub diffuse: Vec3,
    /// Toward the sun, Bevy world space.
    pub to_sun: Vec3,
    pub generation: u32,
}

impl BuildLight {
    /// A WMO's exterior and the boxes: the fixed-function sun.
    pub fn lit(&self, world_normal: Vec3) -> Vec3 {
        (self.ambient + self.diffuse * world_normal.dot(self.to_sun).max(0.0))
            .clamp(Vec3::ZERO, Vec3::ONE)
    }

    /// An exterior doodad: `Model2.bls`'s order-2 lobe at a doodad's intensity of 1
    /// (benilla's `wow_model.wgsl`), softer on the faces turned from the sun than the boxes'.
    pub fn lit_doodad(&self, world_normal: Vec3) -> Vec3 {
        let mu = world_normal.dot(self.to_sun).clamp(-1.0, 1.0);
        let lobe = (4.0 / 17.0) * (0.375 + 2.0 * mu + 1.875 * mu * mu);
        (self.ambient + self.diffuse * lobe).clamp(Vec3::ZERO, Vec3::ONE)
    }
}

/// On a piece's (or a plot marker's) drawn mesh: whose it is, how it is lit, and the light and
/// tint its colours were made with.
#[derive(Component)]
pub struct PieceMesh {
    pub id: u32,
    pub yaw: f32,
    pub made: Option<(u32, [u8; 4])>,
    /// Each vertex's own colour the light multiplies: a box's shade at its foot, a model's
    /// M2Color. None is white.
    pub base: Option<Arc<Vec<[f32; 3]>>>,
    /// Lit as a doodad (a model) rather than as a WMO (the boxes).
    pub doodad: bool,
    /// Unlit (an M2 batch with the unlit flag: a window's glow).
    pub emissive: bool,
}

impl PieceMesh {
    pub fn boxes(id: u32, yaw: f32, base: Arc<Vec<[f32; 3]>>) -> Self {
        Self {
            id,
            yaw,
            made: None,
            base: Some(base),
            doodad: false,
            emissive: false,
        }
    }

    pub fn model(id: u32, yaw: f32, t: &super::models::Template) -> Self {
        Self {
            id,
            yaw,
            made: None,
            base: t.tint.clone(),
            doodad: true,
            emissive: t.emissive,
        }
    }
}

pub fn setup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
    meshes: Option<ResMut<Assets<Mesh>>>,
) {
    let (Some(mut materials), Some(mut meshes)) = (materials, meshes) else {
        return;
    };
    let mut textures = HashMap::default();
    let mut images = Vec::new();
    let block_textures = super::pieces::STRUCTURES
        .iter()
        .flat_map(|s| s.blocks.iter().map(|b| b.texture));
    for path in look::all_textures()
        .into_iter()
        .map(|t| t.path)
        .chain(block_textures)
    {
        if textures.contains_key(path) {
            continue;
        }
        let image: Handle<Image> = assets.load(look::mpq_url(path));
        images.push(image.clone());
        textures.insert(
            path,
            materials.add(StandardMaterial {
                base_color_texture: Some(image),
                unlit: true,
                ..default()
            }),
        );
    }
    let ghost = |c: Color| StandardMaterial {
        base_color: c,
        unlit: true,
        alpha_mode: AlphaMode::Blend,
        cull_mode: None,
        ..default()
    };
    let flame_image: Handle<Image> = assets.load(FLAME);
    let flames = look::STYLES
        .iter()
        .map(|s| {
            materials.add(StandardMaterial {
                base_color: Color::srgb(s.flame[0], s.flame[1], s.flame[2]),
                base_color_texture: Some(flame_image.clone()),
                unlit: true,
                alpha_mode: AlphaMode::Add,
                cull_mode: None,
                ..default()
            })
        })
        .collect();
    commands.insert_resource(BuildAssets {
        textures,
        images,
        fallback: materials.add(StandardMaterial {
            base_color: Color::srgb(0.55, 0.5, 0.45),
            unlit: true,
            ..default()
        }),
        ghost_ok: materials.add(ghost(Color::srgba(0.35, 0.95, 0.45, 0.2))),
        ghost_bad: materials.add(ghost(Color::srgba(1.0, 0.25, 0.2, 0.25))),
        flames,
        flame_quad: meshes.add(flame_quad()),
    });
    commands.insert_resource(BuildLight {
        ambient: Vec3::splat(0.5),
        diffuse: Vec3::splat(0.5),
        to_sun: Vec3::new(0.3, 0.8, 0.2).normalize(),
        generation: 1,
    });
}

/// WoW's textures load clamped; a wall three textures wide wants them repeated.
pub fn repeat_textures(
    assets: Option<Res<BuildAssets>>,
    mut events: MessageReader<AssetEvent<Image>>,
    mut images: ResMut<Assets<Image>>,
) {
    let Some(assets) = assets else {
        events.clear();
        return;
    };
    for ev in events.read() {
        let AssetEvent::LoadedWithDependencies { id } = ev else {
            continue;
        };
        if !assets.images.iter().any(|h| h.id() == *id) {
            continue;
        }
        if let Some(image) = images.get_mut(*id) {
            image.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor {
                address_mode_u: ImageAddressMode::Repeat,
                address_mode_v: ImageAddressMode::Repeat,
                ..ImageSamplerDescriptor::linear()
            });
        }
    }
}

/// A piece's root: its origin and yaw, in Bevy space.
pub fn root_transform(pos: Vec3, yaw: f32) -> Transform {
    Transform::from_translation(from_wow(pos.to_array())).with_rotation(Quat::from_rotation_y(yaw))
}

/// A box's place under its piece's root (Bevy space): centre, rotation, full sizes.
fn box_local(center: [f32; 3], half: [f32; 3], pitch: f32) -> (Vec3, Quat, Vec3) {
    (
        from_wow(center),
        Quat::from_rotation_x(pitch),
        Vec3::new(half[1] * 2.0, half[2] * 2.0, half[0] * 2.0),
    )
}

/// Surfaces under a piece's root (Bevy space): positions, normals, texture coordinates, and each
/// vertex's own shade.
#[derive(Default)]
pub struct Geometry {
    positions: Vec<[f32; 3]>,
    normals: Vec<[f32; 3]>,
    uvs: Vec<[f32; 2]>,
    shade: Vec<[f32; 3]>,
    indices: Vec<u32>,
}

/// A box's axes in the piece's frame (WoW): its x, y and z after its pitch (its +x tipped up
/// toward +z).
fn axes(b: &BoxDef) -> [Vec3; 3] {
    let (s, c) = b.pitch.sin_cos();
    [Vec3::new(c, 0.0, s), Vec3::Y, Vec3::new(-s, 0.0, c)]
}

/// How dark a face is at its foot: a wall shaded where it meets the ground, its underside more.
fn foot_shade(p: Vec3, n: Vec3) -> f32 {
    if n.z < -0.5 {
        return 0.7;
    }
    let up = (p.z / 0.9).clamp(0.0, 1.0);
    0.82 + 0.18 * up * up * (3.0 - 2.0 * up)
}

/// The six faces of a drawn box into `out`, one geometry per texture.
fn push_box(out: &mut Vec<(Tex, Geometry)>, d: &DrawBox) {
    let ax = axes(&d.b);
    let c = Vec3::from_array(d.b.center);
    let h = Vec3::from_array(d.b.half);
    // Each face: its normal axis and sign, and the two along it (B × C = the normal).
    for (a, bi, ci, sign) in [
        (0usize, 1usize, 2usize, 1.0f32),
        (0, 2, 1, -1.0),
        (1, 2, 0, 1.0),
        (1, 0, 2, -1.0),
        (2, 0, 1, 1.0),
        (2, 1, 0, -1.0),
    ] {
        let n = ax[a] * sign;
        let tex = if n.z > 0.5 {
            d.top
        } else if n.z < -0.5 {
            d.bottom
        } else {
            d.side
        };
        let mid = c + n * h[a];
        let corners: Vec<Vec3> = [(-1.0f32, -1.0f32), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]
            .iter()
            .map(|(sb, sc)| mid + ax[bi] * (h[bi] * sb) + ax[ci] * (h[ci] * sc))
            .collect();
        let uvs = face_uvs(d.uv, tex.yards, n, &corners);
        let g = match out.iter_mut().position(|(t, _)| t.path == tex.path) {
            Some(i) => &mut out[i].1,
            None => {
                out.push((tex, Geometry::default()));
                &mut out.last_mut().unwrap().1
            }
        };
        let base = g.positions.len() as u32;
        for (p, uv) in corners.iter().zip(uvs) {
            g.positions.push(from_wow(p.to_array()).to_array());
            g.normals.push(from_wow(n.to_array()).to_array());
            g.uvs.push(uv);
            g.shade.push([foot_shade(*p, n); 3]);
        }
        g.indices
            .extend_from_slice(&[base, base + 1, base + 2, base, base + 2, base + 3]);
    }
}

/// A face's texture coordinates (in repeats) at its four corners (piece frame, WoW axes).
/// Across runs to the right as you face it, and down from its top; a floor's runs along the
/// piece's x and y.
pub fn face_uvs(uv: Uv, yards: f32, n: Vec3, corners: &[Vec3]) -> [[f32; 2]; 4] {
    let flat = n.z.abs() >= 0.9;
    let (across, up) = if flat {
        (Vec3::X, Vec3::Y)
    } else {
        let across = Vec3::Z.cross(n).normalize();
        (across, n.cross(across))
    };
    let raw: Vec<(f32, f32)> = corners.iter().map(|p| (p.dot(across), p.dot(up))).collect();
    let (u0, u1) = raw
        .iter()
        .fold((f32::MAX, f32::MIN), |(a, b), r| (a.min(r.0), b.max(r.0)));
    let (v0, v1) = raw
        .iter()
        .fold((f32::MAX, f32::MIN), |(a, b), r| (a.min(r.1), b.max(r.1)));
    let mut out = [[0.0; 2]; 4];
    for (i, (u, v)) in raw.iter().enumerate() {
        out[i] = match uv {
            Uv::Panel(top) if !flat => [(u + 1.5) / yards, (top - v) / yards],
            Uv::Whole => [
                (u - u0) / (u1 - u0).max(1e-4),
                (v1 - v) / (v1 - v0).max(1e-4),
            ],
            Uv::Grain if (u1 - u0) > (v1 - v0) => {
                // The long way along the texture's height.
                [(v1 - v) / yards, (u - u0) / yards]
            }
            _ if flat => [u / yards, v / yards],
            _ => [(u - u0) / yards, (v1 - v) / yards],
        };
    }
    out
}

/// What is drawn of a piece, one geometry per texture.
pub fn geometry(kind: PieceType, open: bool) -> Vec<(Tex, Geometry)> {
    let mut out = Vec::new();
    for d in look::draw_boxes(kind, open) {
        push_box(&mut out, &d);
    }
    out
}

/// A structure's blocks (a city wall's masonry), one geometry per texture.
fn block_geometry(kind: PieceType) -> Vec<(Tex, Geometry)> {
    let mut out = Vec::new();
    for b in structure(kind).map_or(&[][..], |s| s.blocks) {
        let t = Tex {
            path: b.texture,
            yards: 3.0,
        };
        push_box(
            &mut out,
            &DrawBox {
                b: BoxDef {
                    center: b.center,
                    half: b.half,
                    pitch: 0.0,
                },
                side: t,
                top: t,
                bottom: t,
                uv: Uv::Fit,
            },
        );
    }
    out
}

fn mesh_from(g: Geometry) -> Mesh {
    let n = g.positions.len();
    let mut mesh = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    );
    mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, g.positions);
    mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, g.normals);
    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, g.uvs);
    mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; n]);
    mesh.insert_indices(Indices::U32(g.indices));
    mesh
}

/// The ghost: everything the piece draws, as one mesh (one see-through material).
pub fn ghost_mesh(kind: PieceType) -> Mesh {
    if kind.base() == PieceType::Sandbags {
        return sandbag_mesh();
    }
    let mut all = Geometry::default();
    for (_, g) in geometry(kind, false)
        .into_iter()
        .chain(block_geometry(kind))
    {
        let base = all.positions.len() as u32;
        all.positions.extend(g.positions);
        all.normals.extend(g.normals);
        all.uvs.extend(g.uvs);
        all.shade.extend(g.shade);
        all.indices.extend(g.indices.iter().map(|i| i + base));
    }
    mesh_from(all)
}

/// A line of sandbags as MW2 stacks them: two staggered layers of plump, flattened bags (four
/// below, three above) along the piece's 3-yard length, under the piece's root (Bevy axes: the
/// line runs along x, WoW's y).
pub fn sandbag_mesh() -> Mesh {
    let bag = Mesh::from(Capsule3d::new(0.2, 0.42));
    let mut out: Option<Mesh> = None;
    for (layer, count, y) in [(0usize, 4usize, 0.2f32), (1, 3, 0.56)] {
        for i in 0..count {
            let span = 3.0 / 4.0;
            let x = -1.5 + span * (i as f32 + 0.5) + if layer == 1 { span * 0.5 } else { 0.0 };
            // A capsule lies along its own y: laid flat along the line, squashed to a bag.
            let t =
                Transform::from_translation(Vec3::new(x, y, if layer == 1 { 0.05 } else { 0.0 }))
                    .with_rotation(Quat::from_rotation_z(std::f32::consts::FRAC_PI_2))
                    .with_scale(Vec3::new(0.55, 1.0, 1.15));
            let piece = bag.clone().transformed_by(t);
            out = Some(match out {
                None => piece,
                Some(mut m) => {
                    let _ = m.merge(&piece);
                    m
                }
            });
        }
    }
    let mut mesh = out.unwrap_or_else(|| Mesh::from(Cuboid::default()));
    let n = mesh.count_vertices();
    mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; n]);
    mesh
}

fn flame_quad() -> Mesh {
    let (w, h) = (0.5f32, 1.0f32);
    let mut mesh = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    );
    // Two crossed quads standing on the origin.
    let mut p = Vec::new();
    let mut uv = Vec::new();
    for d in [Vec3::X, Vec3::Z] {
        for (x, y, u, v) in [
            (-w, 0.0, 0.0, 1.0),
            (w, 0.0, 1.0, 1.0),
            (w, h, 1.0, 0.0),
            (-w, h, 0.0, 0.0),
        ] {
            p.push((d * x + Vec3::Y * y).to_array());
            uv.push([u, v]);
        }
    }
    let n = p.len();
    mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, p);
    mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 0.0, 1.0]; n]);
    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
    mesh.insert_indices(Indices::U32(vec![0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]));
    mesh
}

/// A piece's colour: dimmer as it is shot up; in build mode, how well it is held up (Valheim's
/// blue, green, yellow, red).
pub fn tint(health: f32, support: f32, build_mode: bool) -> [f32; 4] {
    let h = health.clamp(0.0, 1.0);
    let mut c = Vec3::splat(0.55 + 0.45 * h) * Vec3::new(1.0, 0.9 + 0.1 * h, 0.85 + 0.15 * h);
    if build_mode {
        let s = support.clamp(0.0, 1.0);
        let k = if s >= 0.9 {
            Vec3::new(0.45, 0.65, 1.0)
        } else if s >= 0.6 {
            Vec3::new(0.45, 1.0, 0.45)
        } else if s >= 0.35 {
            Vec3::new(1.0, 0.9, 0.3)
        } else {
            Vec3::new(1.0, 0.3, 0.25)
        };
        c = c.lerp(k, 0.55);
    }
    [c.x, c.y, c.z, 1.0]
}

/// The material a broken piece's chunks take: its main face's.
pub fn debris_material(assets: &BuildAssets, kind: PieceType) -> Handle<StandardMaterial> {
    let s = look::style_of(kind);
    let t = look::swatch(kind).unwrap_or(
        if super::pieces::def(kind).material == super::pieces::Material::Stone {
            s.stone_wall
        } else {
            s.beam
        },
    );
    assets.material(t.path)
}

pub fn spawn_piece(
    commands: &mut Commands,
    assets: &BuildAssets,
    meshes: &mut Assets<Mesh>,
    _light: &BuildLight,
    p: &Piece,
    model: Option<&[super::models::Template]>,
) -> Entity {
    let w = &p.wire;
    // Drawn as its model when it has one (in a turret's moving layers, crate::build::turret),
    // with a structure's blocks; else as its style's boxes.
    type Drawn = Vec<(Handle<Mesh>, Handle<StandardMaterial>, Arc<Vec<[f32; 3]>>)>;
    let mut drawn: Drawn = Vec::new();
    let add = |drawn: &mut Drawn, geometry: Vec<(Tex, Geometry)>, meshes: &mut Assets<Mesh>| {
        for (t, mut g) in geometry {
            let shade = Arc::new(std::mem::take(&mut g.shade));
            drawn.push((meshes.add(mesh_from(g)), assets.material(t.path), shade));
        }
    };
    let blocks = structure(w.kind).is_some_and(|s| !s.blocks.is_empty());
    match model {
        Some(_) => {}
        None if w.kind.base() == PieceType::Sandbags => {
            let mesh = sandbag_mesh();
            let n = mesh.count_vertices();
            drawn.push((
                meshes.add(mesh),
                assets.material(look::style_of(w.kind).cloth.path),
                Arc::new(vec![[1.0; 3]; n]),
            ));
        }
        // A structure of blocks alone draws them, not its collision.
        None if blocks => {}
        // The mailbox and the dummy: the server's own object stands there and is what is seen.
        None if super::pieces::furniture(w.kind).is_some_and(|f| f.model.is_none()) => {}
        None => add(&mut drawn, geometry(w.kind, w.open), meshes),
    }
    if blocks {
        add(&mut drawn, block_geometry(w.kind), meshes);
    }
    let mut root = commands.spawn((
        Name::new(format!("mw2 piece {}", w.id)),
        PieceRoot,
        root_transform(w.pos, w.yaw),
        Visibility::Visible,
        RigidBody::Static,
    ));
    root.with_children(|c| {
        for (mesh, material, shade) in drawn {
            c.spawn((
                Mesh3d(mesh),
                MeshMaterial3d(material),
                Transform::default(),
                PieceMesh::boxes(w.id, w.yaw, shade),
            ));
        }
        for bx in &boxes_of(w.kind, w.open) {
            let (center, rot, size) = box_local(bx.center, bx.half, bx.pitch);
            c.spawn((
                PieceBox(w.id),
                Collider::cuboid(size.x, size.y, size.z),
                Transform::from_translation(center).with_rotation(rot),
            ));
        }
        let flame_at = match w.kind.base() {
            PieceType::Campfire => Some((Vec3::new(0.0, 0.25, 0.0), 1.0)),
            PieceType::Torch => Some((Vec3::new(0.0, 1.75, 0.0), 0.35)),
            _ => None,
        };
        if let Some((at, scale)) = flame_at {
            let style = usize::from(w.kind.style()).min(assets.flames.len() - 1);
            c.spawn((
                Flame {
                    phase: (w.id % 97) as f32,
                },
                Mesh3d(assets.flame_quad.clone()),
                MeshMaterial3d(assets.flames[style].clone()),
                Transform::from_translation(at).with_scale(Vec3::splat(scale)),
            ))
            .insert(bevy::light::NotShadowCaster);
        }
    });
    let root = root.id();
    if let Some(templates) = model {
        let layers = super::turret::drawn_layers(templates, meshes);
        let (id, yaw) = (w.id, w.yaw);
        super::turret::spawn_model(commands, root, id, layers, model_scale(w.kind), |e, i| {
            if let Some(t) = templates.get(i) {
                e.insert(PieceMesh::model(id, yaw, t));
            }
        });
    }
    root
}

/// How much larger than its source a piece's model is drawn.
pub fn model_scale(kind: PieceType) -> f32 {
    if kind == PieceType::SentryGun {
        super::turret::SENTRY_SCALE
    } else if kind.is_portal() {
        super::pieces::PORTAL_SCALE
    } else {
        1.0
    }
}

/// A mesh's colours from the light: each vertex's own colour times the light on its normal (the
/// doodad's lobe or the boxes' sun), times `tint`.
pub fn light_colors(
    light: &BuildLight,
    normals: &[[f32; 3]],
    turn: Quat,
    pm: &PieceMesh,
    tint: [f32; 4],
) -> Vec<[f32; 4]> {
    normals
        .iter()
        .enumerate()
        .map(|(i, n)| {
            let n = turn * Vec3::from_array(*n);
            let lit = if pm.emissive {
                Vec3::ONE
            } else if pm.doodad {
                light.lit_doodad(n)
            } else {
                light.lit(n)
            };
            let own = pm
                .base
                .as_ref()
                .and_then(|b| b.get(i))
                .map_or(Vec3::ONE, |c| Vec3::from_array(*c));
            let c = lit * own;
            [c.x * tint[0], c.y * tint[1], c.z * tint[2], 1.0]
        })
        .collect()
}

/// Follow WoW's light, and colour each piece's mesh by the light, its health and (in build mode)
/// its support; only what changed is written. A plot's markers (id 0) take the light alone.
pub fn relight(
    lighting: Option<Res<benilla_world::lighting::WowLighting>>,
    mut light: ResMut<BuildLight>,
    build: Res<Building>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut drawn: Query<(&mut PieceMesh, &Mesh3d)>,
) {
    if let Some(l) = lighting {
        let ambient = Vec3::from_array(l.ambient);
        let diffuse = Vec3::from_array(l.diffuse);
        let to_sun = (-l.sun_dir).normalize_or_zero();
        let moved = (ambient - light.ambient).abs().max_element() > 0.02
            || (diffuse - light.diffuse).abs().max_element() > 0.02
            || to_sun.dot(light.to_sun) < 0.999;
        if moved {
            light.ambient = ambient;
            light.diffuse = diffuse;
            light.to_sun = to_sun;
            light.generation = light.generation.wrapping_add(1);
        }
    }
    for (mut pm, mesh3d) in &mut drawn {
        let t = if pm.id == super::plots::MARKER_ID {
            [1.0; 4]
        } else {
            let Some(p) = build.pieces.get(&pm.id) else {
                continue;
            };
            tint(p.wire.health, p.wire.support, build.on)
        };
        let key = (
            light.generation,
            [
                (t[0] * 255.0) as u8,
                (t[1] * 255.0) as u8,
                (t[2] * 255.0) as u8,
                build.on as u8,
            ],
        );
        if pm.made == Some(key) {
            continue;
        }
        let Some(mesh) = meshes.get_mut(&mesh3d.0) else {
            continue;
        };
        let Some(bevy::mesh::VertexAttributeValues::Float32x3(normals)) =
            mesh.attribute(Mesh::ATTRIBUTE_NORMAL)
        else {
            continue;
        };
        let colors = light_colors(&light, normals, Quat::from_rotation_y(pm.yaw), &pm, t);
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
        pm.made = Some(key);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mesh_of(kind: PieceType) -> Geometry {
        let mut all = Geometry::default();
        for (_, g) in geometry(kind, false) {
            let base = all.positions.len() as u32;
            all.positions.extend(g.positions);
            all.normals.extend(g.normals);
            all.uvs.extend(g.uvs);
            all.shade.extend(g.shade);
            all.indices.extend(g.indices.iter().map(|i| i + base));
        }
        all
    }

    #[test]
    fn a_wall_draws_its_face_and_trim_wound_outward_and_lit_from_the_sun() {
        let g = mesh_of(PieceType::WoodWall);
        // The face, two posts, a top plate and a sill: five boxes of six faces.
        assert_eq!(g.positions.len(), 5 * 24);
        // Every face's winding agrees with its normal (counter-clockwise seen from outside).
        for tri in g.indices.chunks(3) {
            let [a, b, c] =
                [tri[0], tri[1], tri[2]].map(|i| Vec3::from_array(g.positions[i as usize]));
            let n = Vec3::from_array(g.normals[tri[0] as usize]);
            assert!((b - a).cross(c - a).dot(n) > 0.0);
        }
        // Darker at its foot than at its top.
        assert!(g.shade.iter().any(|s| s[0] < 0.9) && g.shade.iter().any(|s| s[0] > 0.99));
        let light = BuildLight {
            ambient: Vec3::splat(0.3),
            diffuse: Vec3::splat(0.6),
            to_sun: Vec3::Y,
            generation: 0,
        };
        assert!(light.lit(Vec3::Y).x > light.lit(Vec3::X).x);
        assert_eq!(light.lit(Vec3::NEG_Y), Vec3::splat(0.3));
        // A doodad faced to the sun takes the full sun, one side-on less than the boxes' half,
        // and one turned away a little.
        assert!((light.lit_doodad(Vec3::Y).x - 0.9).abs() < 1e-4);
        assert!(light.lit_doodad(Vec3::X).x < 0.3 + 0.6 * 0.1);
        assert!(light.lit_doodad(Vec3::NEG_Y).x > 0.3);
    }

    #[test]
    fn a_walls_face_shows_one_panel_and_reads_the_same_from_both_sides() {
        let wall = look::draw_boxes(PieceType::WoodWall, false)[0];
        let ax = axes(&wall.b);
        let c = Vec3::from_array(wall.b.center);
        let h = Vec3::from_array(wall.b.half);
        for sign in [1.0f32, -1.0] {
            let n = ax[0] * sign;
            let mid = c + n * h[0];
            let corners: Vec<Vec3> = [(-1.0f32, -1.0f32), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]
                .iter()
                .map(|(a, b)| mid + Vec3::Y * (h.y * a) + Vec3::Z * (h.z * b))
                .collect();
            let uv = face_uvs(Uv::Panel(3.0), 3.0, n, &corners);
            let us: Vec<f32> = uv.iter().map(|t| t[0]).collect();
            let vs: Vec<f32> = uv.iter().map(|t| t[1]).collect();
            // One panel across and down, its top at the wall's top.
            assert!(
                (us.iter().cloned().fold(f32::MIN, f32::max)
                    - us.iter().cloned().fold(f32::MAX, f32::min)
                    - 1.0)
                    .abs()
                    < 1e-4
            );
            assert!((vs.iter().cloned().fold(f32::MIN, f32::max) - 1.0).abs() < 1e-4);
            assert!(vs.iter().cloned().fold(f32::MAX, f32::min).abs() < 1e-4);
            // Across runs to the viewer's right on both faces: at the corner to the right of
            // the face's normal (up × normal) u is the larger.
            let right = Vec3::Z.cross(n);
            let i_right = (0..4)
                .max_by(|a, b| corners[*a].dot(right).total_cmp(&corners[*b].dot(right)))
                .unwrap();
            let i_left = (0..4)
                .min_by(|a, b| corners[*a].dot(right).total_cmp(&corners[*b].dot(right)))
                .unwrap();
            assert!(uv[i_right][0] > uv[i_left][0]);
        }
    }

    #[test]
    fn a_post_takes_its_grain_up_and_a_plate_along() {
        let up = [
            Vec3::new(0.19, -0.08, 0.0),
            Vec3::new(0.19, 0.08, 0.0),
            Vec3::new(0.19, 0.08, 3.0),
            Vec3::new(0.19, -0.08, 3.0),
        ];
        let uv = face_uvs(Uv::Grain, 1.5, Vec3::X, &up);
        let dv = uv.iter().map(|t| t[1]).fold(f32::MIN, f32::max)
            - uv.iter().map(|t| t[1]).fold(f32::MAX, f32::min);
        assert!((dv - 2.0).abs() < 1e-4);
        let along = [
            Vec3::new(0.19, -1.3, 2.9),
            Vec3::new(0.19, 1.3, 2.9),
            Vec3::new(0.19, 1.3, 3.0),
            Vec3::new(0.19, -1.3, 3.0),
        ];
        let uv = face_uvs(Uv::Grain, 1.3, Vec3::X, &along);
        let dv = uv.iter().map(|t| t[1]).fold(f32::MIN, f32::max)
            - uv.iter().map(|t| t[1]).fold(f32::MAX, f32::min);
        assert!((dv - 2.0).abs() < 1e-4);
    }

    #[test]
    fn every_style_draws_in_its_own_textures() {
        for style in 0..look::STYLE_COUNT {
            let wall = PieceType::WoodWall.styled(style).unwrap();
            let textures: Vec<&str> = geometry(wall, false).iter().map(|(t, _)| t.path).collect();
            assert!(textures.contains(&look::STYLES[usize::from(style)].wall.path));
        }
        // The city wall draws its blocks.
        let wall = super::super::pieces::STRUCTURES
            .iter()
            .find(|s| s.name == "City wall")
            .unwrap();
        assert!(!block_geometry(PieceType(wall.id)).is_empty());
        assert!(ghost_mesh(PieceType(wall.id)).count_vertices() > 0);
    }

    #[test]
    fn the_box_sizes_turn_into_bevy_axes() {
        // A wall: thin along WoW x (Bevy z), wide along WoW y (Bevy x), tall along WoW z (Bevy y).
        let (c, _, size) = box_local([0.0, 0.0, 1.5], [0.15, 1.5, 1.5], 0.0);
        assert_eq!(size, Vec3::new(3.0, 3.0, 0.3));
        assert_eq!(c, Vec3::new(0.0, 1.5, 0.0));
    }

    #[test]
    fn support_colours_run_blue_to_red() {
        let blue = tint(1.0, 1.0, true);
        let red = tint(1.0, 0.2, true);
        assert!(blue[2] > blue[0] && red[0] > red[2]);
        assert_eq!(tint(1.0, 0.2, false), tint(1.0, 1.0, false));
        assert!(tint(0.1, 1.0, false)[0] < tint(1.0, 1.0, false)[0]);
    }

    #[test]
    fn a_models_own_colour_darkens_its_light() {
        let light = BuildLight {
            ambient: Vec3::splat(0.4),
            diffuse: Vec3::splat(0.6),
            to_sun: Vec3::Y,
            generation: 0,
        };
        let mut pm = PieceMesh {
            id: 1,
            yaw: 0.0,
            made: None,
            base: Some(Arc::new(vec![[0.5, 0.5, 0.5]])),
            doodad: true,
            emissive: false,
        };
        let c = light_colors(&light, &[[0.0, 1.0, 0.0]], Quat::IDENTITY, &pm, [1.0; 4]);
        assert!((c[0][0] - 0.5).abs() < 1e-4);
        pm.emissive = true;
        pm.base = None;
        let c = light_colors(&light, &[[0.0, -1.0, 0.0]], Quat::IDENTITY, &pm, [1.0; 4]);
        assert_eq!(c[0][0], 1.0);
    }
}
