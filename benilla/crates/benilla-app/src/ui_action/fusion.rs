//! Synchronize the layer's action-bar model with the stock action store and FrameXML VM.

use std::collections::HashMap;

use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::PlayerActions;
use crate::fusion_bar::{
    main_bar_lua, marks_lua, FusionActionBar, MainBarView, ACTION_KIND_FUSION,
};

/// What [`sync_bar`] last put into a VM.
#[derive(Default)]
pub(crate) struct SyncMemo {
    /// The view applied, `Some(None)` the stock one.
    view: Option<Option<MainBarView>>,
    /// The tooltips pushed.
    tips: Option<HashMap<u32, Vec<benilla_ui::script::LayerTipLine>>>,
    /// The slot marks drawn (crate::fusion_bar::marks_lua).
    marks: Option<HashMap<u32, crate::fusion_bar::SlotMark>>,
}

/// Keep the bar's copies current: the looks into the action store (re-resolving the slots when
/// they change), the saved spells and layer entries out of it, and the main bar's view into the
/// VM when it changes (or the VM is new).
pub(crate) fn sync_bar(
    script: Option<NonSendMut<UiScript>>,
    mut bar: ResMut<FusionActionBar>,
    mut actions: ResMut<PlayerActions>,
    mut memo: Local<crate::ui_script::VmMemo<SyncMemo>>,
) {
    if actions.fusion_looks != bar.looks {
        actions.fusion_looks = bar.looks.clone();
        actions.dirty = true;
    }
    if bar.loaded != actions.loaded {
        bar.loaded = actions.loaded;
    }
    if actions.is_changed() || bar.spells.is_empty() {
        let of_kind = |kind: u8| -> HashMap<u32, u32> {
            actions
                .buttons
                .iter()
                .filter(|(_, b)| b.kind == kind)
                .map(|(slot, b)| (u32::from(*slot) + 1, b.action))
                .collect()
        };
        let spells = of_kind(benilla_protocol::messages::ACTION_KIND_SPELL);
        if spells != bar.spells {
            bar.spells = spells;
        }
        let fusion = of_kind(ACTION_KIND_FUSION);
        if fusion != bar.fusion_slots {
            bar.fusion_slots = fusion;
        }
        let saved: HashMap<u32, u32> = actions
            .buttons
            .iter()
            .map(|(slot, b)| (u32::from(*slot) + 1, (u32::from(b.kind) << 24) | b.action))
            .collect();
        if saved != bar.saved {
            bar.saved = saved;
        }
    }
    let Some(mut script) = script else {
        return;
    };
    let memo = memo.get(&script);
    if memo.tips.as_ref() != Some(&bar.tips) {
        memo.tips = Some(bar.tips.clone());
        script.set_layer_tips(bar.tips.clone());
    }
    // The slot marks, whenever they change (or the VM is new and has some to draw).
    if memo.marks.as_ref() != Some(&bar.marks) && (memo.marks.is_some() || !bar.marks.is_empty()) {
        match script.eval::<bool>(&marks_lua(&bar.marks)) {
            Ok(true) => memo.marks = Some(bar.marks.clone()),
            Ok(false) => {}
            Err(e) => {
                warn!("fusion_bar: slot marks: {e}");
                memo.marks = Some(bar.marks.clone());
            }
        }
    }
    if memo.view.as_ref() == Some(&bar.main_bar) {
        return;
    }
    // A VM that never had a view needs no restore.
    if memo.view.is_none() && bar.main_bar.is_none() {
        memo.view = Some(None);
        return;
    }
    match script.eval::<bool>(&main_bar_lua(bar.main_bar.as_ref())) {
        Ok(true) => memo.view = Some(bar.main_bar.clone()),
        Ok(false) => {}
        Err(e) => {
            warn!("fusion_bar: main bar view: {e}");
            memo.view = Some(bar.main_bar.clone());
        }
    }
}
