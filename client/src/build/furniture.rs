//! Furniture's two small dialogs (the server's `Furniture.h`), built from the live UI's own Lua
//! as crate::build::locks builds its:
//!
//! - **Page**: a book's pages (or a stash's or a trophy's word), Prev and Next between them.
//! - **Stash**: a plot's shared stash on the left, the bags' storable items on the right; a click
//!   on a stash row takes it out, on a bag row puts it in (shift-click: one at a time). Op 9.
//!
//! Lua to Rust: requests wait in `BenillaMw2Furniture.out`, taken each frame by [`poll`].

use benilla_app::fusion::{CHANNEL_BUILD, ChannelMessage, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::Building;
use super::wire::{self, FurniturePage, StashView};
use crate::Mw2Set;
use crate::lua;
use crate::mode::FpsState;

/// What the server said that the dialogs show.
#[derive(Clone, Debug, PartialEq)]
pub enum FurnitureEvent {
    Page(FurniturePage),
    Stash(StashView),
}

/// A press in a dialog, for the server.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FurnitureRequest {
    Put { piece: u32, bag: u8, slot: u8, count: u16 },
    Take { piece: u32, index: u16, count: u16 },
}

impl FurnitureRequest {
    pub fn body(&self) -> Vec<u8> {
        match *self {
            FurnitureRequest::Put {
                piece,
                bag,
                slot,
                count,
            } => wire::encode_stash_put(piece, bag, slot, count),
            FurnitureRequest::Take {
                piece,
                index,
                count,
            } => wire::encode_stash_take(piece, index, count),
        }
    }
}

/// A request as the Lua left it: kind 1 put (a bag, b slot), 2 take (a index); `one` a single
/// item rather than the stack.
pub fn request_of(kind: i64, piece: i64, a: i64, b: i64, one: bool) -> Option<FurnitureRequest> {
    let piece = u32::try_from(piece).ok()?;
    let count = u16::from(one);
    match kind {
        1 => Some(FurnitureRequest::Put {
            piece,
            bag: u8::try_from(a).ok()?,
            slot: u8::try_from(b).ok()?,
            count,
        }),
        2 => Some(FurnitureRequest::Take {
            piece,
            index: u16::try_from(a).ok()?,
            count,
        }),
        _ => None,
    }
}

fn lua_list<T>(items: &[T], f: impl Fn(&T) -> String) -> String {
    format!(
        "{{{}}}",
        items.iter().map(f).collect::<Vec<_>>().join(", ")
    )
}

/// The Lua call that shows a page.
pub fn open_page_lua(p: &FurniturePage) -> String {
    format!(
        "if BenillaMw2Furniture and BenillaMw2Furniture.OpenPage then BenillaMw2Furniture.OpenPage({}, {}, {}) end",
        p.piece,
        lua::quote(&p.title),
        lua_list(&p.lines, |l| lua::quote(l))
    )
}

/// The stash's heading: how full it is.
pub fn stash_line(v: &StashView) -> String {
    format!(
        "{} of {} slots used. Click to take out or put in; shift-click for one.",
        v.stash.len(),
        v.slots
    )
}

/// The Lua call that shows the stash.
pub fn open_stash_lua(v: &StashView) -> String {
    format!(
        "if BenillaMw2Furniture and BenillaMw2Furniture.OpenStash then BenillaMw2Furniture.OpenStash({}, {}, {}, {}, {}, {}, {}, {}) end",
        v.piece,
        lua::quote(&stash_line(v)),
        lua_list(&v.stash, |s| s.0.to_string()),
        lua_list(&v.stash, |s| s.1.to_string()),
        lua_list(&v.bags, |b| b.0.to_string()),
        lua_list(&v.bags, |b| b.1.to_string()),
        lua_list(&v.bags, |b| b.2.to_string()),
        lua_list(&v.bags, |b| b.3.to_string()),
    )
}

pub const POLL_LUA: &str = r#"
local L = BenillaMw2Furniture
if not L then return false, false, nil, nil, nil, nil, nil end
local shown = L.Shown and L.Shown() or false
local r = L.out and table.remove(L.out, 1)
if r then return true, shown, r.kind, r.piece, r.a, r.b, r.one end
return true, shown, nil, nil, nil, nil, nil
"#;

/// The two dialogs, built once per world VM (Lua 5.0, as the 1.12 client runs it).
pub const INSTALL_LUA: &str = r#"
if BenillaMw2Furniture then return end
local L = { out = {}, errors = {} }
BenillaMw2Furniture = L
local ROWS = 10

local function make(kind, name, parent, template)
  if template then
    local ok, f = pcall(CreateFrame, kind, name, parent, template)
    if ok and f then return f, true end
  end
  local f = CreateFrame(kind, name, parent)
  if name then setglobal(name, f) end
  return f, false
end

local function font(parent, name, template)
  local fs = parent:CreateFontString(name, "ARTWORK")
  local object = template and getglobal(template)
  if object then fs:SetFontObject(object) end
  return fs
end

local function button(parent, name, text, width)
  local b, templated = make("Button", name, parent, "UIPanelButtonTemplate")
  b:SetWidth(width or 100)
  b:SetHeight(22)
  b:SetText(text)
  if not templated and b.SetNormalFontObject then pcall(b.SetNormalFontObject, b, GameFontNormal) end
  return b
end

local function dialog(name, width, height, title)
  local d = CreateFrame("Frame", name, UIParent)
  d:Hide()
  d:SetWidth(width)
  d:SetHeight(height)
  d:SetPoint("CENTER", UIParent, "CENTER", 0, 60)
  d:SetFrameStrata("DIALOG")
  d:EnableMouse(true)
  if d.SetToplevel then d:SetToplevel(true) end
  if d.SetClampedToScreen then d:SetClampedToScreen(true) end
  if BenillaMw2Movable then BenillaMw2Movable(d, name, "CENTER", "CENTER", 0, 60) end
  pcall(d.SetBackdrop, d, {
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
  })
  if UISpecialFrames then table.insert(UISpecialFrames, name) end
  d:SetScript("OnShow", function() PlaySound("igMainMenuOpen") end)
  local header = d:CreateTexture(name .. "Header", "ARTWORK")
  header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
  header:SetWidth(300)
  header:SetHeight(64)
  header:SetPoint("TOP", d, "TOP", 0, 12)
  local t = font(d, name .. "Title", "GameFontNormal")
  t:SetPoint("TOP", header, "TOP", 0, -14)
  t:SetText(title)
  d.title = t
  local close = button(d, name .. "Close", "Close", 90)
  close:SetPoint("BOTTOM", d, "BOTTOM", 0, 16)
  close:SetScript("OnClick", function() d:Hide() end)
  return d
end

local function itemName(id)
  local name = GetItemInfo and GetItemInfo(id)
  return name or ("Item " .. id)
end

-- ── The page ──
local function page()
  local d = dialog("BenillaMw2FurniturePage", 380, 300, "")
  L.page = d
  local text = font(d, "BenillaMw2FurniturePageText", "GameFontHighlight")
  text:SetPoint("TOPLEFT", d, "TOPLEFT", 26, -36)
  text:SetWidth(328)
  text:SetHeight(196)
  text:SetJustifyH("LEFT")
  text:SetJustifyV("TOP")
  local num = font(d, "BenillaMw2FurniturePageNumber", "GameFontNormalSmall")
  num:SetPoint("BOTTOM", d, "BOTTOM", 0, 46)
  local prev = button(d, "BenillaMw2FurniturePagePrev", "Prev", 70)
  prev:SetPoint("BOTTOMLEFT", d, "BOTTOMLEFT", 20, 16)
  local nextb = button(d, "BenillaMw2FurniturePageNext", "Next", 70)
  nextb:SetPoint("BOTTOMRIGHT", d, "BOTTOMRIGHT", -20, 16)
  local function show()
    local n = table.getn(L.lines or {})
    text:SetText((L.lines or {})[L.at] or "")
    if n > 1 then num:SetText("Page " .. L.at .. " of " .. n) else num:SetText("") end
    if L.at > 1 then prev:Enable() else prev:Disable() end
    if L.at < n then nextb:Enable() else nextb:Disable() end
  end
  prev:SetScript("OnClick", function() if L.at > 1 then L.at = L.at - 1 show() end end)
  nextb:SetScript("OnClick", function() if L.at < table.getn(L.lines) then L.at = L.at + 1 show() end end)
  function L.OpenPage(piece, title, lines)
    if L.stash then L.stash:Hide() end
    L.lines = lines
    L.at = 1
    d.title:SetText(title)
    show()
    d:Show()
  end
end

-- ── The stash ──
local function rowButton(parent, name, x, y)
  local b = CreateFrame("Button", name, parent)
  setglobal(name, b)
  b:SetWidth(170)
  b:SetHeight(18)
  b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
  local fs = font(b, name .. "Text", "GameFontHighlightSmall")
  fs:SetPoint("LEFT", b, "LEFT", 4, 0)
  fs:SetWidth(166)
  fs:SetJustifyH("LEFT")
  b.text = fs
  local hl = b:CreateTexture(nil, "HIGHLIGHT")
  hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
  hl:SetAllPoints(b)
  if b.RegisterForClicks then b:RegisterForClicks("LeftButtonUp") end
  return b
end

local function stash()
  local d = dialog("BenillaMw2FurnitureStash", 400, 340, "Stash")
  L.stash = d
  local line = font(d, "BenillaMw2FurnitureStashLine", "GameFontHighlightSmall")
  line:SetPoint("TOPLEFT", d, "TOPLEFT", 22, -32)
  line:SetWidth(356)
  line:SetJustifyH("LEFT")
  local left = font(d, nil, "GameFontNormalSmall")
  left:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -50)
  left:SetText("In the stash")
  local right = font(d, nil, "GameFontNormalSmall")
  right:SetPoint("TOPLEFT", d, "TOPLEFT", 206, -50)
  right:SetText("In your bags")
  L.srows, L.brows = {}, {}
  L.spage, L.bpage = 0, 0
  local function render()
    for i = 1, ROWS do
      local si = L.spage * ROWS + i
      local b = L.srows[i]
      if L.sitems and L.sitems[si] then
        b.text:SetText(itemName(L.sitems[si]) .. " x" .. L.scounts[si])
        b.index = si - 1
        b:Show()
      else
        b:Hide()
      end
      local bi = L.bpage * ROWS + i
      local c = L.brows[i]
      if L.bitems and L.bitems[bi] then
        c.text:SetText(itemName(L.bitems[bi]) .. " x" .. L.bcounts[bi])
        c.bag = L.bbags[bi]
        c.slot = L.bslots[bi]
        c:Show()
      else
        c:Hide()
      end
    end
  end
  for i = 1, ROWS do
    local s = rowButton(d, "BenillaMw2FurnitureStashS" .. i, 20, -64 - (i - 1) * 20)
    s:SetScript("OnClick", function()
      table.insert(L.out, { kind = 2, piece = L.piece, a = this.index, b = 0, one = IsShiftKeyDown() and true or false })
    end)
    L.srows[i] = s
    local b = rowButton(d, "BenillaMw2FurnitureStashB" .. i, 204, -64 - (i - 1) * 20)
    b:SetScript("OnClick", function()
      table.insert(L.out, { kind = 1, piece = L.piece, a = this.bag, b = this.slot, one = IsShiftKeyDown() and true or false })
    end)
    L.brows[i] = b
  end
  local function pager(name, x, which, list)
    local p = button(d, name .. "Prev", "<", 26)
    p:SetPoint("TOPLEFT", d, "TOPLEFT", x, -270)
    p:SetScript("OnClick", function() if L[which] > 0 then L[which] = L[which] - 1 render() end end)
    local n = button(d, name .. "Next", ">", 26)
    n:SetPoint("LEFT", p, "RIGHT", 4, 0)
    n:SetScript("OnClick", function()
      if (L[which] + 1) * ROWS < table.getn(L[list] or {}) then L[which] = L[which] + 1 render() end
    end)
  end
  pager("BenillaMw2FurnitureStashSP", 24, "spage", "sitems")
  pager("BenillaMw2FurnitureStashBP", 208, "bpage", "bitems")
  function L.OpenStash(piece, text, sitems, scounts, bbags, bslots, bitems, bcounts)
    if L.page then L.page:Hide() end
    if L.piece ~= piece then L.spage, L.bpage = 0, 0 end
    L.piece = piece
    L.sitems, L.scounts = sitems, scounts
    L.bbags, L.bslots, L.bitems, L.bcounts = bbags, bslots, bitems, bcounts
    if L.spage * ROWS >= table.getn(sitems) then L.spage = 0 end
    if L.bpage * ROWS >= table.getn(bitems) then L.bpage = 0 end
    line:SetText(text)
    render()
    d:Show()
  end
end

function L.Shown()
  return (L.page and L.page:IsShown()) or (L.stash and L.stash:IsShown()) or false
end

for _, step in ipairs({ page, stash }) do
  local ok, err = pcall(step)
  if not ok then table.insert(L.errors, tostring(err)) end
end
"#;

const INSTALL_ERRORS_LUA: &str = "local e = BenillaMw2Furniture and BenillaMw2Furniture.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Polled {
    pub installed: bool,
    pub shown: bool,
    pub request: Option<FurnitureRequest>,
}

pub fn poll(script: &UiScript) -> Polled {
    type Row = (
        bool,
        bool,
        Option<i64>,
        Option<i64>,
        Option<i64>,
        Option<i64>,
        Option<bool>,
    );
    let Some((installed, shown, kind, piece, a, b, one)) = lua::eval::<Row>(script, POLL_LUA)
    else {
        return Polled::default();
    };
    if !installed {
        lua::install_movable(script);
        lua::run(script, INSTALL_LUA);
        if let Some(Some(e)) = lua::eval::<Option<String>>(script, INSTALL_ERRORS_LUA) {
            warn!("benilla-mw2: furniture install: {e}");
        }
    }
    let request = match (kind, piece) {
        (Some(k), Some(p)) => request_of(k, p, a.unwrap_or(-1), b.unwrap_or(0), one.unwrap_or(false)),
        _ => None,
    };
    Polled {
        installed,
        shown,
        request,
    }
}

#[derive(Resource, Default)]
pub struct FurnitureUi {
    held_cursor: bool,
    pub shown: bool,
}

pub struct FurniturePlugin;

impl Plugin for FurniturePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<FurnitureUi>()
            .add_systems(Update, drive.in_set(Mw2Set::Hud))
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

fn drive(
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut build: ResMut<Building>,
    mut ui: ResMut<FurnitureUi>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        build.furniture_events.clear();
        ui.shown = false;
        return;
    };
    for event in std::mem::take(&mut build.furniture_events) {
        match event {
            FurnitureEvent::Page(p) => lua::run(&script, &open_page_lua(&p)),
            FurnitureEvent::Stash(s) => lua::run(&script, &open_stash_lua(&s)),
        }
    }
    let polled = poll(&script);
    ui.shown = polled.shown;
    if let Some(r) = polled.request {
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_BUILD,
            body: r.body(),
        }));
    }
}

fn hold_cursor(mut ui: ResMut<FurnitureUi>, mut state: ResMut<FpsState>) {
    if ui.shown {
        if !state.panel {
            state.panel = true;
        }
        ui.held_cursor = true;
    } else if ui.held_cursor {
        ui.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::super::pieces::{self, FURNITURE, FurnitureUse, Mount, PieceType};
    use super::super::{Interact, interact_action};
    use super::*;
    use bevy::math::Vec3;

    fn by_name(name: &str) -> PieceType {
        PieceType(FURNITURE.iter().find(|f| f.name == name).expect(name).id)
    }

    #[test]
    fn the_catalogue_follows_the_server_s_ids_and_reads_as_pieces() {
        for (i, f) in FURNITURE.iter().enumerate() {
            assert_eq!(usize::from(f.id), usize::from(pieces::FURNITURE_BASE) + i);
            let t = PieceType(f.id);
            assert_eq!(PieceType::from_u8(f.id), Some(t), "{}", f.name);
            assert!(pieces::is_furniture(t) && pieces::structure(t).is_none());
            assert_eq!(pieces::def(t).name, f.name);
            assert_eq!(pieces::def(t).ground_only, f.mount != Mount::Wall);
            assert_eq!(pieces::is_floor(t), f.top > 0.0);
            assert_eq!(
                super::super::menu::category_of(pieces::Choice::Piece(t)),
                super::super::menu::Category::Furniture
            );
        }
        assert!(FURNITURE.len() >= 50);
        // The server's own objects stand in the mailbox and the dummy.
        assert!(pieces::furniture(by_name("Mailbox")).unwrap().model.is_none());
        assert!(pieces::furniture(by_name("Training dummy")).unwrap().model.is_none());
        assert!(pieces::furniture(by_name("Chest")).unwrap().model.is_some());
    }

    #[test]
    fn f_uses_furniture_out_of_build_mode_but_not_plain_decor() {
        let chair = by_name("Wooden chair");
        assert_eq!(interact_action(chair, 2.0, false, false), Some(Interact::Use));
        assert_eq!(interact_action(chair, 2.0, true, true), None);
        assert_eq!(interact_action(chair, 9.0, false, false), None);
        assert_eq!(interact_action(by_name("Candle"), 1.0, false, false), None);
        assert_eq!(interact_action(by_name("Inn table"), 1.0, false, false), None);
        assert_eq!(
            interact_action(by_name("Chest"), 2.0, false, true),
            Some(Interact::Use)
        );
        assert_eq!(
            wire::encode_furniture_use(7),
            vec![wire::OP_FURNITURE_USE, 7, 0, 0, 0]
        );
        let uses: Vec<FurnitureUse> = FURNITURE.iter().map(|f| f.use_).collect();
        for u in [
            FurnitureUse::Seat,
            FurnitureUse::Bed,
            FurnitureUse::Stash,
            FurnitureUse::Book,
            FurnitureUse::Station,
            FurnitureUse::Mailbox,
            FurnitureUse::Dummy,
            FurnitureUse::Trophy,
            FurnitureUse::Stage,
        ] {
            assert!(uses.contains(&u), "{u:?}");
        }
    }

    #[test]
    fn furniture_goes_on_the_grid_in_fifteen_degree_steps_or_free() {
        let chair = by_name("Wooden chair");
        let aim = Vec3::new(10.13, 20.38, 5.0);
        let p = pieces::place_furniture(chair, aim, Vec3::Z, 0.0, 0, false);
        assert_eq!((p.pos.x, p.pos.y, p.pos.z), (10.25, 20.5, 5.0));
        // Facing the builder (its front toward them), a turn a step.
        assert!((p.yaw - std::f32::consts::PI).abs() < 1e-4);
        let q = pieces::place_furniture(chair, aim, Vec3::Z, 0.0, 1, false);
        assert!((q.yaw - (std::f32::consts::PI + pieces::FURNITURE_TURN)).abs() < 1e-4);
        let r = pieces::place_furniture(chair, aim, Vec3::Z, 0.1, 0, false);
        assert!((r.yaw - std::f32::consts::PI).abs() < 1e-4, "snapped");
        let free = pieces::place_furniture(chair, aim, Vec3::Z, 0.1, 0, true);
        assert_eq!(free.pos, aim);
        assert!((free.yaw - (std::f32::consts::PI + 0.1)).abs() < 1e-4);
    }

    #[test]
    fn a_wall_piece_hangs_flat_on_the_wall_it_is_aimed_at() {
        let shield = by_name("Wall shield");
        let f = pieces::furniture(shield).unwrap();
        // A wall facing +y.
        let aim = Vec3::new(3.0, 4.0, 101.3);
        let p = pieces::place_furniture(shield, aim, Vec3::Y, 1.0, 5, false);
        assert!((p.yaw - std::f32::consts::FRAC_PI_2).abs() < 1e-4, "{}", p.yaw);
        // Its back face lies on the wall.
        let back = p.to_world(Vec3::new(f.center[0] - f.half[0], 0.0, 0.0));
        assert!((back.y - aim.y).abs() < 1e-3 && (back.x - aim.x).abs() < 1e-3);
        // Its middle at the aimed height, on the grid.
        assert!((p.pos.z + f.center[2] - 101.25).abs() < 1e-3);
        // Aimed at the floor it is placed as anything else (and the ghost says it can't hang).
        let floor = pieces::place_furniture(shield, aim, Vec3::Z, 0.0, 0, false);
        assert_eq!(floor.pos.x, 3.0);
    }

    #[test]
    fn the_stash_dialogs_presses_are_the_servers_messages() {
        let put = request_of(1, 9, 255, 23, false).unwrap();
        assert_eq!(put.body(), vec![wire::OP_STASH, 9, 0, 0, 0, 1, 255, 23, 0, 0]);
        let one = request_of(2, 9, 300, 0, true).unwrap();
        assert_eq!(one.body(), vec![wire::OP_STASH, 9, 0, 0, 0, 2, 44, 1, 1, 0]);
        assert!(request_of(3, 9, 0, 0, false).is_none());
        assert!(request_of(1, 9, 256, 0, false).is_none());
        assert!(request_of(2, -1, 0, 0, false).is_none());
    }

    #[test]
    fn the_server_s_pages_and_stash_decode() {
        let mut body = vec![wire::OP_FURNITURE_PAGE, 5, 0, 0, 0, 5, 5];
        body.extend_from_slice(b"Title");
        body.push(2);
        for l in ["one", "two"] {
            body.extend_from_slice(&(l.len() as u16).to_le_bytes());
            body.extend_from_slice(l.as_bytes());
        }
        let Some(wire::ServerMsg::FurniturePage(p)) = wire::decode(&body) else {
            panic!("a page")
        };
        assert_eq!((p.piece, p.kind, p.title.as_str()), (5, 5, "Title"));
        assert_eq!(p.lines, vec!["one".to_string(), "two".to_string()]);
        let lua = open_page_lua(&p);
        assert!(lua.contains("OpenPage(5, \"Title\", {\"one\", \"two\"})"), "{lua}");

        let mut s = vec![wire::OP_STASH_VIEW, 7, 0, 0, 0, 16, 0, 1, 0];
        s.extend_from_slice(&2589u32.to_le_bytes());
        s.extend_from_slice(&20u16.to_le_bytes());
        s.extend_from_slice(&[1, 255, 23]);
        s.extend_from_slice(&4470u32.to_le_bytes());
        s.extend_from_slice(&3u16.to_le_bytes());
        let Some(wire::ServerMsg::Stash(v)) = wire::decode(&s) else {
            panic!("a stash")
        };
        assert_eq!(v.slots, 16);
        assert_eq!(v.stash, vec![(2589, 20)]);
        assert_eq!(v.bags, vec![(255, 23, 4470, 3)]);
        assert!(stash_line(&v).starts_with("1 of 16 slots"));
        assert!(open_stash_lua(&v).contains("{2589}, {20}, {255}, {23}, {4470}, {3}"));
    }

    #[test]
    fn the_dialogs_install_and_a_click_leaves_a_request() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        assert!(!poll(&s).installed);
        assert_eq!(s.eval::<Option<String>>(INSTALL_ERRORS_LUA).unwrap(), None);
        let v = StashView {
            piece: 3,
            slots: 8,
            stash: vec![(2589, 5)],
            bags: vec![(255, 24, 4470, 2)],
        };
        lua::run(&s, &open_stash_lua(&v));
        assert!(poll(&s).shown);
        lua::run(
            &s,
            "this = BenillaMw2FurnitureStashB1 BenillaMw2FurnitureStashB1:GetScript(\"OnClick\")()",
        );
        assert_eq!(
            poll(&s).request,
            Some(FurnitureRequest::Put {
                piece: 3,
                bag: 255,
                slot: 24,
                count: 0
            })
        );
        lua::run(
            &s,
            "this = BenillaMw2FurnitureStashS1 BenillaMw2FurnitureStashS1:GetScript(\"OnClick\")()",
        );
        assert_eq!(
            poll(&s).request,
            Some(FurnitureRequest::Take {
                piece: 3,
                index: 0,
                count: 0
            })
        );
        // A page closes the stash and shows its first page.
        let page = FurniturePage {
            piece: 4,
            kind: 5,
            title: "A Builder's Primer".into(),
            lines: vec!["One".into(), "Two".into()],
        };
        lua::run(&s, &open_page_lua(&page));
        assert_eq!(
            s.eval::<Option<String>>("return BenillaMw2FurniturePageText:GetText()")
                .unwrap()
                .as_deref(),
            Some("One")
        );
        assert_eq!(
            s.eval::<bool>("return BenillaMw2FurnitureStash:IsShown() and true or false")
                .unwrap(),
            false
        );
    }
}
