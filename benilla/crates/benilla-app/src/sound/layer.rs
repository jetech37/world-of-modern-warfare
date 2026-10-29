//! A layer's sounds ([`crate::fusion::LayerSound`], benilla-mw2's flight): one looping
//! `SoundEntries` kit, 2D, whose gain the layer sets each frame (the wind of a fast flight), and
//! one-shot kits by name (a wing flap). Not a 1.12 behaviour.

use bevy::prelude::*;

use benilla_assets::WorldAssets;
use benilla_world::schedule::WorldStage;

use super::kit::{
    play_kit_ext, set_source_kit_gain, source_kit_playing, stop_source_kit, KitRef, PlayExtras,
    SoundCategory, SoundKits, Volume,
};
use super::{AudioListener, SoundConfig, SoundOutput};

/// The loop's channel owner, and the kit it plays.
#[derive(Default)]
struct LoopState {
    source: Option<Entity>,
    kit: Option<u32>,
}

#[allow(clippy::too_many_arguments)]
fn layer_sounds(
    mut commands: Commands,
    mut layer: ResMut<crate::fusion::LayerSound>,
    kits: Option<ResMut<SoundKits>>,
    assets: Option<Res<WorldAssets>>,
    mut out: NonSendMut<SoundOutput>,
    config: Res<SoundConfig>,
    listener: Res<AudioListener>,
    mut st: Local<LoopState>,
) {
    let (Some(mut kits), Some(assets)) = (kits, assets) else {
        layer.once.clear();
        return;
    };
    let source = *st.source.get_or_insert_with(|| commands.spawn_empty().id());
    let want = layer
        .loop_kit
        .as_deref()
        .and_then(|n| kits.catalog().by_name(n).map(|k| k.id));
    if st.kit.is_some() && st.kit != want {
        stop_source_kit(&mut out, source, st.kit.unwrap());
        st.kit = None;
    }
    if let Some(id) = want {
        if !source_kit_playing(&out, source, id) {
            let _ = play_kit_ext(
                &mut kits,
                &assets,
                &mut out,
                &config,
                listener.pos,
                KitRef::Id(id),
                None,
                SoundCategory::Sfx,
                PlayExtras {
                    source: Some(source),
                    force_loop: true,
                    dedupe_exempt: true,
                    ..default()
                },
            );
        }
        st.kit = Some(id);
        set_source_kit_gain(&mut out, source, id, layer.loop_gain);
    }
    for (name, gain) in std::mem::take(&mut layer.once) {
        let _ = play_kit_ext(
            &mut kits,
            &assets,
            &mut out,
            &config,
            listener.pos,
            KitRef::Name(&name),
            None,
            SoundCategory::Sfx,
            PlayExtras {
                dedupe_exempt: true,
                volume_mult: Volume(gain),
                ..default()
            },
        );
    }
}

pub(super) fn plugin(app: &mut App) {
    app.add_systems(Update, layer_sounds.in_set(WorldStage::Present));
}
