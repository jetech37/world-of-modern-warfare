//! The layer's windows, opened one way: a HUD button (anyone, anywhere, the limited view) or an
//! NPC (a trainer or innkeeper for kits, a stable master for companions, a plot coordinator for
//! homesteads; the full view, told by the server). Each window reads [`OpenPanel`].

use bevy::prelude::*;

/// A window of the layer.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Panel {
    /// Kits: loadouts, talents, killstreaks (crate::cac_ui).
    Kits,
    /// Companions: the collection and the Bestiary (crate::companion_ui).
    Companions,
    /// Construction and the homestead (crate::build).
    Construction,
    /// Every profession, its trade window and its Utility abilities (crate::professions).
    Professions,
    /// The quest log's neighbour: achievements, kills and deaths (crate::achievements).
    Journal,
}

/// What the opener allows.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Access {
    /// From the HUD: looking, and what may be done anywhere.
    Hud,
    /// From an NPC the server named: everything that window offers there.
    Npc,
}

/// Open (or, from the HUD when already open, close) a window.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpenPanel {
    pub panel: Panel,
    pub access: Access,
}

/// A page of the kit window (crate::cac_ui), in its tabs' order.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum KitPage {
    Loadout = 0,
    Talents = 1,
    Abilities = 2,
    Killstreaks = 3,
}

impl KitPage {
    /// The page's index (the kit window's `page`).
    pub const fn index(self) -> u8 {
        self as u8
    }
}

/// Show the kit window on a page from the HUD (N the talents, P the abilities, and the stock
/// talent frame's and spellbook's toggles, which lead here): open on it; already on it, close.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct ShowKitPage(pub KitPage);

pub struct PanelsPlugin;

impl Plugin for PanelsPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<OpenPanel>().add_message::<ShowKitPage>();
    }
}
