//! Pieces drawn as real models instead of boxes: WoW's own props from the player's install (a
//! campfire, a bed, tents, barrels: `Item`/`World` M2s read through the patch chain) and MW2's
//! sentry turret from the MW2 install. Each model is built once into template meshes (piece-local
//! Bevy space, yards) with their textures; every placed piece gets its own copy, as its colours
//! carry its light and damage (crate::build::render).

use std::sync::Arc;

use bevy::asset::RenderAssetUsages;
use bevy::image::{ImageAddressMode, ImageSampler, ImageSamplerDescriptor};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};

use benilla_assets::{LockRecover, WorldAssets};
use benilla_formats::{ModelBlend, parse_m2_render_submeshes};

use super::pieces::PieceModel;
use crate::mw2::Mw2Assets;
use crate::tracer::from_wow;

/// One drawn surface of a model: its mesh (piece-local Bevy space) and material.
#[derive(Clone)]
pub struct Template {
    pub mesh: Mesh,
    pub material: Handle<StandardMaterial>,
    /// What moves it: a turret's layers turn apart (crate::build::turret); all else is Base.
    pub layer: Layer,
    /// Each vertex's own colour (a WoW model's constant M2Color), which the light multiplies;
    /// None is white.
    pub tint: Option<Arc<Vec<[f32; 3]>>>,
    /// Unlit: an M2 batch with the unlit flag (a window's glow) takes no light.
    pub emissive: bool,
}

/// A turret's moving layers, innermost last: each turns with those before it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Layer {
    /// The tripod: fixed.
    #[default]
    Base,
    /// The ring that turns to face.
    Yaw,
    /// The gun body: turns and tips.
    Pitch,
    /// The barrels: turn, tip and spin.
    Spin,
}

/// A model's templates as one mesh, `scale` times its size, for a ghost (one see-through
/// material for the whole model).
pub fn merged_mesh(templates: &[Template], scale: f32) -> Mesh {
    use bevy::mesh::VertexAttributeValues;
    let (mut positions, mut normals, mut indices) = (Vec::new(), Vec::new(), Vec::new());
    for t in templates {
        let (Some(VertexAttributeValues::Float32x3(p)), Some(VertexAttributeValues::Float32x3(n))) = (
            t.mesh.attribute(Mesh::ATTRIBUTE_POSITION),
            t.mesh.attribute(Mesh::ATTRIBUTE_NORMAL),
        ) else {
            continue;
        };
        let base = positions.len() as u32;
        positions.extend(p.iter().map(|v| [v[0] * scale, v[1] * scale, v[2] * scale]));
        normals.extend_from_slice(n);
        match t.mesh.indices() {
            Some(Indices::U32(i)) => indices.extend(i.iter().map(|k| k + base)),
            Some(Indices::U16(i)) => indices.extend(i.iter().map(|k| u32::from(*k) + base)),
            None => {}
        }
    }
    let uvs = vec![[0.0; 2]; positions.len()];
    mesh(positions, normals, uvs, indices)
}

/// The layer of a surface skinned to these bones (MW2's sentry: `tag_aim_pivot` turns,
/// `tag_aim_animated` and the ammo feed tip, `j_spin` spins).
pub fn turret_layer(bones: &[String]) -> Layer {
    let has = |n: &str| bones.iter().any(|b| b.eq_ignore_ascii_case(n));
    if has("j_spin") {
        Layer::Spin
    } else if [
        "tag_aim_animated",
        "j_ammo_cover",
        "j_bullet_chain",
        "mg01",
        "tag_aim",
    ]
    .iter()
    .any(|n| has(n))
    {
        Layer::Pitch
    } else if has("tag_aim_pivot") {
        Layer::Yaw
    } else {
        Layer::Base
    }
}

pub enum ModelState {
    Ready(Arc<Vec<Template>>),
    /// The install is not read yet (the MW2 library loads in the background).
    Pending,
    Failed,
}

#[derive(Resource, Default)]
pub struct ModelCache {
    built: HashMap<PieceModel, Option<Arc<Vec<Template>>>>,
}

fn mesh(
    positions: Vec<[f32; 3]>,
    normals: Vec<[f32; 3]>,
    uvs: Vec<[f32; 2]>,
    indices: Vec<u32>,
) -> Mesh {
    let n = positions.len();
    let mut m = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    );
    m.insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    m.insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals);
    m.insert_attribute(Mesh::ATTRIBUTE_UV_0, uvs);
    m.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; n]);
    m.insert_indices(Indices::U32(indices));
    m
}

impl ModelCache {
    #[allow(clippy::too_many_arguments)]
    pub fn get(
        &mut self,
        model: PieceModel,
        world: Option<&WorldAssets>,
        mw2: &Mw2Assets,
        assets: &AssetServer,
        materials: &mut Assets<StandardMaterial>,
        images: &mut Assets<Image>,
    ) -> ModelState {
        if let Some(built) = self.built.get(&model) {
            return built.clone().map_or(ModelState::Failed, ModelState::Ready);
        }
        let built = match model {
            PieceModel::Wow(path) => {
                let Some(world) = world else {
                    return ModelState::Pending;
                };
                build_wow(path, world, assets, materials, [0.0; 3], 0.0)
            }
            // A structure: each of its models where it stands in the piece's frame, as one.
            PieceModel::Structure(id) => {
                let Some(world) = world else {
                    return ModelState::Pending;
                };
                let Some(s) = super::pieces::structure(super::pieces::PieceType(id)) else {
                    return ModelState::Failed;
                };
                // A structure of blocks alone (a city wall) has no models: ready, empty, and its
                // blocks are drawn beside (crate::build::render).
                if s.parts.is_empty() {
                    let ready = Arc::new(Vec::new());
                    self.built.insert(model, Some(ready.clone()));
                    return ModelState::Ready(ready);
                }
                let mut all = Vec::new();
                let mut whole = true;
                for p in s.parts {
                    match build_wow(p.model, world, assets, materials, p.at, p.yaw) {
                        Some(t) => all.extend(t),
                        None => whole = false,
                    }
                }
                whole.then_some(all)
            }
            PieceModel::Mw2(name) => match mw2 {
                Mw2Assets::Ready(lib) => lib
                    .world_model(name)
                    .map(|part| build_mw2(&part, materials, images)),
                Mw2Assets::Cataloguing(_) => return ModelState::Pending,
                _ => None,
            },
        };
        let built = built.filter(|t| !t.is_empty()).map(Arc::new);
        if built.is_none() {
            warn!("benilla-mw2: no model for {model:?}; the piece draws as its boxes");
        }
        self.built.insert(model, built.clone());
        built.map_or(ModelState::Failed, ModelState::Ready)
    }
}

/// Where a model stands in its piece's frame (WoW axes): turned `yaw` about z, then moved `at`.
pub fn place_wow(v: [f32; 3], at: [f32; 3], yaw: f32) -> [f32; 3] {
    let (s, c) = yaw.sin_cos();
    [
        v[0] * c - v[1] * s + at[0],
        v[0] * s + v[1] * c + at[1],
        v[2] + at[2],
    ]
}

/// A WoW M2 as templates: positions from model space (WoW axes, yards), placed at `at` turned
/// `yaw` in the piece's frame, into Bevy's.
fn build_wow(
    path: &str,
    world: &WorldAssets,
    assets: &AssetServer,
    materials: &mut Assets<StandardMaterial>,
    at: [f32; 3],
    yaw: f32,
) -> Option<Vec<Template>> {
    let bytes = world.chain.lock_recover().read(path).ok()?;
    let dir = path.rsplit_once('\\').map_or("", |(d, _)| d);
    let subs = parse_m2_render_submeshes(&bytes, dir, &[]).ok()?;
    let mut out = Vec::new();
    for sub in subs {
        // Particles and glow cards aside: the model's solid surfaces.
        if sub.billboard.is_some()
            || !matches!(sub.blend, ModelBlend::Opaque | ModelBlend::AlphaTest)
        {
            continue;
        }
        let positions = sub
            .positions
            .iter()
            .map(|p| from_wow(place_wow(*p, at, yaw)).to_array())
            .collect();
        let n = sub.positions.len();
        let normals = if sub.normals.len() == n {
            sub.normals
                .iter()
                .map(|p| from_wow(place_wow(*p, [0.0; 3], yaw)).to_array())
                .collect()
        } else {
            vec![[0.0, 1.0, 0.0]; n]
        };
        let uvs = if sub.uvs.len() == n {
            sub.uvs.clone()
        } else {
            vec![[0.0; 2]; n]
        };
        // The constant M2Color, as the world draws the model: it darkens the light.
        let tint = (sub.vertex_colors.len() == n
            && sub
                .vertex_colors
                .iter()
                .any(|c| c[0] < 0.99 || c[1] < 0.99 || c[2] < 0.99))
        .then(|| {
            Arc::new(
                sub.vertex_colors
                    .iter()
                    .map(|c| [c[0], c[1], c[2]])
                    .collect(),
            )
        });
        let texture = sub.texture.as_deref().map(|t| {
            let t = t.to_ascii_lowercase().replace('\\', "/");
            let t = t.strip_suffix(".blp").unwrap_or(&t).to_string();
            assets.load::<Image>(format!("mpq://{t}.blp"))
        });
        out.push(Template {
            layer: Layer::Base,
            tint,
            emissive: sub.emissive,
            mesh: mesh(positions, normals, uvs, sub.indices.clone()),
            material: materials.add(StandardMaterial {
                base_color_texture: texture,
                unlit: true,
                alpha_mode: if sub.blend == ModelBlend::AlphaTest {
                    AlphaMode::Mask(0.5)
                } else {
                    AlphaMode::Opaque
                },
                cull_mode: if sub.two_sided {
                    None
                } else {
                    Some(bevy::render::render_resource::Face::Back)
                },
                ..default()
            }),
        });
    }
    Some(out)
}

/// An MW2 model as templates: its bind pose (MW2 inches, x forward, y left, z up — the piece's
/// own frame) into Bevy yards.
pub(crate) fn build_mw2(
    part: &crate::mw2::load::Part,
    materials: &mut Assets<StandardMaterial>,
    images: &mut Assets<Image>,
) -> Vec<Template> {
    let skel = &part.skel;
    let inch = |p: [f32; 3]| from_wow([p[0] / 36.0, p[1] / 36.0, p[2] / 36.0]).to_array();
    let mut out = Vec::new();
    for surface in &part.surfaces {
        let (v0, vn) = surface.vertices;
        let (i0, ni) = surface.indices;
        if vn == 0 || ni == 0 || v0 + vn > skel.positions.len() || i0 + ni > skel.indices.len() {
            continue;
        }
        let positions = skel.positions[v0..v0 + vn]
            .iter()
            .map(|p| inch(*p))
            .collect();
        let normals = skel.normals[v0..v0 + vn]
            .iter()
            .map(|n| from_wow(*n).to_array())
            .collect();
        let uvs = skel
            .uvs
            .get(v0..v0 + vn)
            .map(<[_]>::to_vec)
            .unwrap_or_else(|| vec![[0.0; 2]; vn]);
        let indices = skel.indices[i0..i0 + ni]
            .iter()
            .map(|&i| i.saturating_sub(v0 as u32))
            .collect();
        let texture = surface.color.as_ref().map(|img| {
            let mut image = Image::new(
                Extent3d {
                    width: img.width,
                    height: img.height,
                    depth_or_array_layers: 1,
                },
                TextureDimension::D2,
                img.pixels.clone(),
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
        // The bones the surface is skinned to name the layer it turns with.
        let bones: Vec<String> = skel
            .bone_names
            .iter()
            .enumerate()
            .filter(|(b, _)| anim_iw4::hide_part_bit(&surface.part_bits, *b))
            .map(|(_, n)| n.clone())
            .collect();
        out.push(Template {
            layer: turret_layer(&bones),
            tint: None,
            emissive: false,
            mesh: mesh(positions, normals, uvs, indices),
            material: materials.add(StandardMaterial {
                base_color: Color::srgb(0.25, 0.25, 0.25).with_alpha(1.0),
                base_color_texture: texture.clone(),
                unlit: true,
                cull_mode: None,
                ..default()
            }),
        });
        if texture.is_some()
            && let Some(m) = out.last().map(|t| t.material.clone())
            && let Some(mat) = materials.get_mut(&m)
        {
            mat.base_color = Color::WHITE;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_sentrys_surfaces_sort_into_its_moving_layers() {
        let names = |n: &[&str]| n.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        assert_eq!(turret_layer(&names(&["tag_origin"])), Layer::Base);
        assert_eq!(turret_layer(&names(&["tag_aim_pivot"])), Layer::Yaw);
        assert_eq!(
            turret_layer(&names(&["tag_aim_animated", "j_ammo_cover"])),
            Layer::Pitch
        );
        assert_eq!(turret_layer(&names(&["j_spin"])), Layer::Spin);
    }
}
