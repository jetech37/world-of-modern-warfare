//! The action bar opened to a layer built on top (benilla-mw2's action systems), on WoW's own
//! bars so their icons, cooldowns, drags and layout stay the stock ones:
//!
//! - **The layer's own entries**: a saved slot of kind [`ACTION_KIND_FUSION`] holds one of the
//!   layer's abilities (its id in the low 24 bits). It is saved like any slot (the server stores
//!   the kind), shows the look the layer gives the id ([`FusionActionBar::looks`]), moves by drag
//!   as a spell does, and a press on it is the layer's ([`FusionActionBar::fusion_pressed`]).
//! - **The main bar's view** ([`FusionActionBar::main_bar`]): while set, `ActionButton1` shows the
//!   Lua action [`MainBarView::first`] and the next ones follow, whatever the page, only the first
//!   [`MainBarView::shown`] buttons are drawn and take the mouse, and their hotkey text is the
//!   layer's keys. The bonus bar (a stance or form page) is left as stock.
//! - **The layer's rule for slot writes** ([`FusionActionBar::write_rule`]): a drag the rule
//!   refuses is put back where it was picked up from.
//! - **Ready mode** ([`FusionActionBar::ready_mode`]): a spell or ability pressed on any bar is
//!   readied for a hand ([`FusionActionBar::readied`]) instead of cast; the layer casts it later
//!   through [`FusionActionBar::use_now`], which runs it as `UseAction` would, at the selection
//!   as it stands. Items, macros and self-casts still go straight through.

use std::collections::HashMap;

use bevy::prelude::*;

use crate::ui_action::PlayerActions;

/// The kind byte of a layer entry: a kind no stock slot has (vmangos `ActionButtonType` uses 0x00,
/// 0x01, 0x40 and 0x80).
pub const ACTION_KIND_FUSION: u8 = 0x20;
/// The main bar's buttons (`ActionButton1`-`12`).
pub const MAIN_BAR_BUTTONS: u8 = 12;

/// How one of the layer's abilities looks on a slot.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct FusionLook {
    /// The icon (`Interface\Icons\…`).
    pub texture: String,
    /// The number on the button (grenades held), 0 for none.
    pub count: u32,
    /// Lit as the action in use (the gun in hand).
    pub checked: bool,
    /// Usable now (greyed when not: a killstreak not earned yet).
    pub usable: bool,
}

/// A slot's mark the layer draws over WoW's own button (the stock `Border` glow an equipped
/// item wears, tinted; the `Name` text as a corner label; a tooltip line under the stock one):
/// what a hand holds, and what can be held. Keyed by Lua action id in [`FusionActionBar::marks`].
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SlotMark {
    /// The border glow's colour and alpha.
    pub border: [f32; 4],
    /// The label at the button's foot (WoW colour escapes allowed), empty for none.
    pub label: String,
    /// A line added under the button's tooltip, and its colour.
    pub tip: Option<(String, [f32; 3])>,
}

/// Which actions the main bar's buttons show while the layer has them.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MainBarView {
    /// The Lua action id `ActionButton1` shows; button `i` shows `first + i - 1`.
    pub first: u32,
    /// How many of the twelve buttons are drawn (the rest are hidden and take no mouse).
    pub shown: u8,
    /// The hotkey text of each shown button, in order.
    pub keys: Vec<String>,
    /// What the page numeral beside the bar reads (the stock page arrows are hidden).
    pub page_text: String,
}

/// A spell or ability pressed in ready mode.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReadiedAction {
    /// The Lua action id pressed.
    pub action: u32,
    /// The spell it holds.
    pub spell_id: u32,
    /// Clicked with the right mouse button (the layer readies it in that button's hand).
    pub right_button: bool,
}

/// A press on a layer entry.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FusionPress {
    /// The layer's ability id.
    pub ability: u32,
    /// The Lua action id pressed.
    pub action: u32,
    pub right_button: bool,
}

/// The layer's say over a slot write the player makes: `(lua action id, packed)`, true to keep it.
pub type WriteRule = fn(u32, u32) -> bool;

/// The layer's hands swing through the fusion (WOMW's Classic and Utility systems, whose swings
/// the server strikes itself): while set, a spell that initiates auto-attack (the send-time tail
/// `0x6e51b5`, the GO-deferred start `0x6e83c0`) does not start WoW's swing. Deviation from the
/// reference, for the layer only: Attack (6603) pressed or `AttackTarget()` still attacks.
#[derive(Resource, Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FusionNoSpellSwing(pub bool);

#[derive(Resource, Debug, Default)]
pub struct FusionActionBar {
    /// How each of the layer's ability ids looks.
    pub looks: HashMap<u32, FusionLook>,
    /// The main bar's view, while the layer has it.
    pub main_bar: Option<MainBarView>,
    /// Ready mode: spells pressed are readied, not cast.
    pub ready_mode: bool,
    /// This frame's presses on layer entries; the layer drains them.
    pub fusion_pressed: Vec<FusionPress>,
    /// This frame's readied spells; the layer drains them.
    pub readied: Vec<ReadiedAction>,
    /// Actions (Lua ids) the layer uses now, as `UseAction` does.
    pub use_now: Vec<u32>,
    /// A unit to select before [`Self::use_now`] runs (the crosshair's), as a click would.
    pub select: Option<(Entity, u64)>,
    /// Every saved slot's spell, by Lua action id (spells only), refreshed with the bar: what a
    /// hand can hold, for the layer's display.
    pub spells: HashMap<u32, u32>,
    /// Every saved layer entry: its ability id by Lua action id.
    pub fusion_slots: HashMap<u32, u32>,
    /// Every saved slot, packed (`kind << 24 | id`), by Lua action id.
    pub saved: HashMap<u32, u32>,
    /// Slot writes the layer makes (`(lua action id, packed)`, 0 clears), saved as a drag's are.
    pub writes: Vec<(u32, u32)>,
    /// The layer's rule for the player's slot writes; none keeps every write.
    pub write_rule: Option<WriteRule>,
    /// The saved bar has arrived this session (`SMSG_ACTION_BUTTONS`).
    pub loaded: bool,
    /// Each ability id's tooltip, as `GameTooltip:SetAction` shows it on its button.
    pub tips: HashMap<u32, Vec<benilla_ui::script::LayerTipLine>>,
    /// Each marked slot's mark (equippable, and in which hand), by Lua action id; the rest are
    /// stock.
    pub marks: HashMap<u32, SlotMark>,
}

/// Where a `UseAction` press goes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum PressRoute {
    /// To the cast path, as always.
    Use,
    /// To the layer: a press on one of its entries.
    Fusion(FusionPress),
    /// Readied for a hand.
    Ready(ReadiedAction),
}

/// Route a press: a layer entry to the layer, a saved spell in ready mode (not a self-cast) to a
/// hand, else the cast path.
pub(crate) fn route_press(
    bar: &FusionActionBar,
    press: benilla_ui::script::ActionUse,
) -> PressRoute {
    let action = press.action;
    if let Some(&ability) = bar.fusion_slots.get(&action) {
        return PressRoute::Fusion(FusionPress {
            ability,
            action,
            right_button: press.right_button,
        });
    }
    if bar.ready_mode && !press.on_self {
        if let Some(&spell_id) = bar.spells.get(&action) {
            return PressRoute::Ready(ReadiedAction {
                action,
                spell_id,
                right_button: press.right_button,
            });
        }
    }
    PressRoute::Use
}

/// Whether the player's slot write is refused by the layer's rule. A clear (a pickup) never is.
pub(crate) fn refuses_write(rule: Option<WriteRule>, lua_id: u32, packed: u32) -> bool {
    packed != 0 && rule.is_some_and(|keep| !keep(lua_id, packed))
}

/// The Lua that points the main bar at the layer's view (or back at the stock page with none):
/// `ActionButton_GetPagedID` and `ActionButton_UpdateHotkeys` wrapped once per VM for the main
/// bar's buttons only. Returns false until the stock action bar is loaded.
pub(crate) fn main_bar_lua(view: Option<&MainBarView>) -> String {
    let (first, shown, keys, page) = match view {
        Some(v) => (
            (v.first.saturating_sub(1)).to_string(),
            v.shown.min(MAIN_BAR_BUTTONS),
            format!(
                "{{{}}}",
                v.keys
                    .iter()
                    .map(|k| format!("{:?}", k))
                    .collect::<Vec<_>>()
                    .join(", ")
            ),
            format!("{:?}", v.page_text),
        ),
        None => ("nil".into(), MAIN_BAR_BUTTONS, "nil".into(), "nil".into()),
    };
    format!(
        r#"if not ActionButton1 or not ActionButton_GetPagedID then return false end
if not BENILLA_FUSION_PAGED then
  BENILLA_FUSION_PAGED = ActionButton_GetPagedID
  ActionButton_GetPagedID = function(button)
    local first = BENILLA_FUSION_FIRST
    if first and not button.isBonus then
      local name = button:GetName()
      if name and string.find(name, "^ActionButton%d+$") then return first + button:GetID() end
    end
    return BENILLA_FUSION_PAGED(button)
  end
  BENILLA_FUSION_HOTKEYS = ActionButton_UpdateHotkeys
  ActionButton_UpdateHotkeys = function(kind)
    BENILLA_FUSION_HOTKEYS(kind)
    local keys = BENILLA_FUSION_KEYS
    if keys and BENILLA_FUSION_FIRST and this and not this.isBonus then
      local name = this:GetName()
      if name and string.find(name, "^ActionButton%d+$") then
        getglobal(name .. "HotKey"):SetText(keys[this:GetID()] or "")
      end
    end
  end
end
BENILLA_FUSION_FIRST = {first}
BENILLA_FUSION_KEYS = {keys}
local was = this
for i = 1, {all} do
  local b = getglobal("ActionButton" .. i)
  if b then
    if i <= {shown} then b:SetAlpha(1) b:EnableMouse(1) else b:SetAlpha(0) b:EnableMouse(nil) end
    this = b
    ActionButton_UpdateHotkeys(b.buttonType)
  end
end
this = was
-- The stock page arrows page nothing the layer shows; the bonus bar needs page 1.
for _, n in ipairs({{"ActionBarUpButton", "ActionBarDownButton"}}) do
  local f = getglobal(n)
  if f then if BENILLA_FUSION_FIRST then f:Hide() else f:Show() end end
end
CURRENT_ACTIONBAR_PAGE = 1
ChangeActionBarPage()
local page = {page}
if page and MainMenuBarPageNumber then MainMenuBarPageNumber:SetText(page) end
return true"#,
        all = MAIN_BAR_BUTTONS,
    )
}

/// The Lua that draws the layer's slot marks: `ActionButton_Update` and `ActionButton_SetTooltip`
/// wrapped once per VM (a mark after the stock look, which hides the border and resets the name;
/// a line after the stock tooltip), the marks table set, and every bar's buttons repainted.
/// Returns false until the stock action bar is loaded.
pub(crate) fn marks_lua(marks: &HashMap<u32, SlotMark>) -> String {
    let mut ids: Vec<&u32> = marks.keys().collect();
    ids.sort();
    let rows: Vec<String> = ids
        .into_iter()
        .map(|id| {
            let m = &marks[id];
            let (tip, tc) = match &m.tip {
                Some((t, c)) => (format!("{t:?}"), *c),
                None => ("nil".into(), [1.0; 3]),
            };
            format!(
                "[{id}] = {{{}, {}, {}, {}, {:?}, {tip}, {}, {}, {}}}",
                m.border[0], m.border[1], m.border[2], m.border[3], m.label, tc[0], tc[1], tc[2]
            )
        })
        .collect();
    format!(
        r#"if not ActionButton1 or not ActionButton_Update or not ActionButton_GetPagedID then return false end
if not BENILLA_FUSION_MARKED then
  BENILLA_FUSION_MARKED = ActionButton_Update
  BENILLA_FUSION_MARK = function(b)
    local marks = BENILLA_FUSION_MARKS
    local m = marks and marks[ActionButton_GetPagedID(b)]
    if not m then return end
    local border = getglobal(b:GetName() .. "Border")
    if border then border:SetVertexColor(m[1], m[2], m[3], m[4]) border:Show() end
    local name = getglobal(b:GetName() .. "Name")
    if name and m[5] ~= "" then name:SetText(m[5]) end
  end
  ActionButton_Update = function()
    BENILLA_FUSION_MARKED()
    if this then BENILLA_FUSION_MARK(this) end
  end
  BENILLA_FUSION_TIP = ActionButton_SetTooltip
  ActionButton_SetTooltip = function()
    BENILLA_FUSION_TIP()
    local marks = BENILLA_FUSION_MARKS
    local m = marks and this and marks[ActionButton_GetPagedID(this)]
    if m and m[6] and GameTooltip:IsOwned(this) then
      GameTooltip:AddLine(m[6], m[7], m[8], m[9])
      GameTooltip:Show()
    end
  end
end
BENILLA_FUSION_MARKS = {{ {rows} }}
local was = this
for _, bar in ipairs({{"ActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton", "MultiBarRightButton", "MultiBarLeftButton"}}) do
  for i = 1, 12 do
    local b = getglobal(bar .. i)
    if b then this = b ActionButton_Update() end
  end
end
this = was
return true"#,
        rows = rows.join(", "),
    )
}

/// Leaving the world: the next character's bar has not arrived yet.
pub(crate) fn leave_world(mut bar: ResMut<FusionActionBar>, mut actions: ResMut<PlayerActions>) {
    actions.loaded = false;
    actions.last_pickup = None;
    bar.loaded = false;
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bar() -> FusionActionBar {
        let mut b = FusionActionBar::default();
        b.spells.insert(1, 133); // Fireball on the first slot
        b.fusion_slots.insert(193, 5); // a layer entry
        b
    }

    fn press(action: u32, on_self: bool) -> benilla_ui::script::ActionUse {
        benilla_ui::script::ActionUse {
            action,
            on_self,
            right_button: false,
        }
    }

    #[test]
    fn presses_go_to_the_cast_path_unless_the_layer_takes_them() {
        let mut b = bar();
        assert_eq!(route_press(&b, press(1, false)), PressRoute::Use);
        assert_eq!(route_press(&b, press(14, false)), PressRoute::Use);
        assert_eq!(
            route_press(&b, press(193, false)),
            PressRoute::Fusion(FusionPress {
                ability: 5,
                action: 193,
                right_button: false
            })
        );
        b.ready_mode = true;
        assert_eq!(
            route_press(&b, press(1, false)),
            PressRoute::Ready(ReadiedAction {
                action: 1,
                spell_id: 133,
                right_button: false
            })
        );
        assert_eq!(route_press(&b, press(1, true)), PressRoute::Use); // a self-cast goes through
        assert_eq!(route_press(&b, press(2, false)), PressRoute::Use); // no spell there (an item)

        // A layer entry is the layer's in ready mode too.
        assert!(matches!(
            route_press(&b, press(193, false)),
            PressRoute::Fusion(_)
        ));

        // A right click readies it for the right button's hand.
        let right = benilla_ui::script::ActionUse {
            right_button: true,
            ..press(1, false)
        };
        assert_eq!(
            route_press(&b, right),
            PressRoute::Ready(ReadiedAction {
                action: 1,
                spell_id: 133,
                right_button: true
            })
        );
    }

    #[test]
    fn the_layers_rule_refuses_placements_but_never_a_pickup() {
        fn only_low(lua: u32, _packed: u32) -> bool {
            lua <= 60
        }
        assert!(!refuses_write(None, 200, 0x0000_0085));
        assert!(!refuses_write(Some(only_low), 1, 0x0000_0085));
        assert!(refuses_write(Some(only_low), 200, 0x0000_0085));
        assert!(
            !refuses_write(Some(only_low), 200, 0),
            "a clear always goes"
        );
    }

    #[test]
    fn the_main_bar_lua_names_the_view() {
        let lua = main_bar_lua(Some(&MainBarView {
            first: 193,
            shown: 6,
            keys: vec!["1".into(), "Q".into()],
            page_text: "M2".into(),
        }));
        assert!(lua.contains("BENILLA_FUSION_FIRST = 192"));
        assert!(lua.contains(r#"BENILLA_FUSION_KEYS = {"1", "Q"}"#));
        assert!(lua.contains("if i <= 6 then"));
        assert!(lua.contains(r#"local page = "M2""#));
        let stock = main_bar_lua(None);
        assert!(stock.contains("BENILLA_FUSION_FIRST = nil"));
        assert!(stock.contains("if i <= 12 then"));
    }

    #[test]
    fn the_marks_lua_wraps_once_and_lists_each_mark() {
        let mut marks = HashMap::new();
        marks.insert(
            3,
            SlotMark {
                border: [1.0, 0.82, 0.0, 1.0],
                label: "R".into(),
                tip: Some(("Equipped: Main Hand".into(), [1.0, 0.82, 0.0])),
            },
        );
        let lua = marks_lua(&marks);
        assert!(lua.contains("if not BENILLA_FUSION_MARKED then"));
        assert!(lua.contains("[3] = {1, 0.82, 0, 1, \"R\", \"Equipped: Main Hand\""));
        assert!(marks_lua(&HashMap::new()).contains("BENILLA_FUSION_MARKS = {  }"));
    }

}
