//! The stock main action bar (`MainMenuBar.xml`, `ActionBarFrame.xml`) re-fitted to the action
//! systems' six-slot pages, from the live UI's own Lua as a 1.12 addon re-fits it; nothing in the
//! fork changes, and with the layer off the bar is put back as stock:
//!
//! - **Six larger buttons**: `ActionButton1`-`6` at [`LARGE_SCALE`] (Interface options, *Large
//!   Action Buttons*, on by default; off draws them at the stock size), on the stock art's first
//!   piece (`MainMenuBarTexture0`, which holds exactly six button wells) scaled with them, so the
//!   wells, borders, cooldown sweeps, hotkey and count text stay the stock ones. The second piece
//!   (buttons 7-12's wells) is dropped and the bar closes up: the page piece and the micro menu,
//!   then the bags piece, as the stock bar has them. The whole bar stays centred at the bottom.
//! - **The XP bar, the max-level bar and the watched reputation's bar** run over the micro menu
//!   and the bags (the large wells rise past them), their stock textures fitted to the span.
//! - **The current system**: its emblem (an icon in a quick-slot border, where the stock page
//!   arrows sat) beside the page numeral (`M1`, `C3`, `U2`), its name and page over the bar's top
//!   left (*Show Action System Label*, on by default), and the emblem's tooltip naming the keys
//!   bound now (X, Z, C, Alt with the wheel, Ctrl for crouch, H for the nameplates).
//! - **The stock pet bar** (`PetActionBarFrame`) is never shown while the layer has the bar: the
//!   companion's bar is the Companion system's (C, crate::companion_bar). Off, it is stock again. A switch fades the six buttons in
//!   and flashes the name, as the stock bonus bar's slide marks a stance change.
//! - **Show Gryphons** (off by default): the end caps, at the shorter bar's ends.
//! - The frames that stack on the bar (`UIParent_ManageFramePositions`: the bottom multibars, the
//!   pet and stance bars, the casting bar, the chat frames' default seats, the bags' column) sit
//!   above the taller wells; the bottom multibars centred over the bar. A form's bonus bar
//!   (`BonusActionBarFrame`) slides over the six wells at their scale, its first six buttons.
//! - The nameplate key (H) says what it did, in the stock `UIErrorsFrame`.
//!
//! The Rust side ([`drive`]) installs it once per world VM and tells it the system and page as
//! they change; the layout's numbers are [`layout`]'s.

use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::action_systems::{ActionSystems, System};
use crate::mode::FpsState;

/// The six buttons' scale (and their wells'), with *Large Action Buttons* on.
pub const LARGE_SCALE: f32 = 1.42;
/// One stock art piece (`MainMenuBarTexture0`-`3`): 256 wide, 43 tall.
pub const PIECE_W: f32 = 256.0;
pub const ART_H: f32 = 43.0;
/// `MainMenuBar`'s stock height and width.
pub const BAR_H: f32 = 53.0;
pub const BAR_W: f32 = 1024.0;
/// The watched reputation's art top over the bar, stacked on the XP bar (`ReputationFrame.lua`).
pub const REP_TOP: f32 = 63.0;
/// `MultiBarBottomLeft`'s stock bottom: `ActionButton1`'s top (40) and 17.
pub const STOCK_MULTIBAR_BOTTOM: f32 = 57.0;
/// The room the system's name takes over the wells.
pub const LABEL_ROOM: f32 = 13.0;
/// A switch's fade, in seconds.
pub const FADE_SECS: f32 = 0.25;

/// The re-fitted bar's numbers, in `MainMenuBar`'s units (y up from its bottom).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BarLayout {
    /// The six buttons' scale and their wells'.
    pub scale: f32,
    /// The wells' piece.
    pub big_w: f32,
    pub big_h: f32,
    /// The whole bar: the wells, the page piece, the bags piece.
    pub width: f32,
    /// Where the XP (and reputation) bar runs.
    pub xp_left: f32,
    pub xp_width: f32,
    /// The top of what stands over the bar's left (the wells, or the XP and reputation bars).
    pub left_top: f32,
    /// `MultiBarBottomLeft`'s bottom, clear of the system's name.
    pub multibar_bottom: f32,
    /// What the managed frames' base offsets gain, and what the offsets for a bottom multibar
    /// gain on top (`UIPARENT_MANAGED_FRAME_POSITIONS`).
    pub raise: f32,
    pub raise_stack: f32,
}

/// The layout for the large or the stock-sized buttons.
pub fn layout(large: bool) -> BarLayout {
    let scale = if large { LARGE_SCALE } else { 1.0 };
    let big_w = PIECE_W * scale;
    let big_h = ART_H * scale;
    let width = big_w + 2.0 * PIECE_W;
    // Wells taller than the stock art rise past the XP bar's floor: the bars move right of them.
    let over = big_h > ART_H + 0.5;
    let (xp_left, xp_width) = if over {
        (big_w, 2.0 * PIECE_W)
    } else {
        (0.0, width)
    };
    let left_top = if over { big_h } else { REP_TOP };
    let multibar_bottom = (left_top + LABEL_ROOM).max(STOCK_MULTIBAR_BOTTOM);
    let raise = if left_top > BAR_H {
        (left_top - BAR_H).ceil() + 2.0
    } else {
        0.0
    };
    let raise_stack = ((multibar_bottom - STOCK_MULTIBAR_BOTTOM).ceil() + 2.0 - raise).max(0.0);
    BarLayout {
        scale,
        big_w,
        big_h,
        width,
        xp_left,
        xp_width,
        left_top,
        multibar_bottom,
        raise,
        raise_stack,
    }
}

impl BarLayout {
    /// As a Lua table constructor.
    fn lua(&self) -> String {
        format!(
            "{{ scale = {}, big_w = {}, big_h = {}, width = {}, xp_left = {}, xp_width = {}, \
             left_top = {}, multibar_base = {}, raise = {}, raise_stack = {} }}",
            self.scale,
            self.big_w,
            self.big_h,
            self.width,
            self.xp_left,
            self.xp_width,
            self.left_top,
            self.multibar_bottom - BAR_H,
            self.raise,
            self.raise_stack,
        )
    }
}

/// A system's emblem (`Interface\Icons\`): the rifle, the sword, the pick.
pub fn emblem(system: System) -> &'static str {
    match system {
        System::Modern => "Interface\\Icons\\INV_Weapon_Rifle_06",
        System::Classic => "Interface\\Icons\\INV_Sword_04",
        System::Utility => "Interface\\Icons\\INV_Pick_02",
        System::Companion => "Interface\\Icons\\Ability_Hunter_BeastCall",
    }
}

/// What the bar shows: the layer's view of it, or the stock bar.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BarView {
    pub system: System,
    /// 1-based.
    pub page: u8,
    pub pages: u8,
}

impl BarView {
    pub fn of(systems: &ActionSystems) -> BarView {
        BarView {
            system: systems.shown(),
            page: systems.page() + 1,
            pages: systems.shown().pages(),
        }
    }
}

/// The Lua that shows `view` (none: the stock bar back).
pub fn set_lua(view: Option<&BarView>) -> String {
    match view {
        Some(v) => format!(
            "if BenillaMw2Bar then BenillaMw2Bar.Set(true, {}, {}, {}, {}) end",
            crate::lua::quote(v.system.name()),
            crate::lua::quote(emblem(v.system)),
            v.page,
            v.pages
        ),
        None => "if BenillaMw2Bar then BenillaMw2Bar.Set(nil) end".to_string(),
    }
}

/// The install, run once per world VM: [`INSTALL_BODY`] with the two layouts.
pub fn install_lua() -> String {
    format!(
        "if BenillaMw2Bar or not MainMenuBar or not MainMenuBarArtFrame or not ActionButton1 then return end\n\
         local LAYOUTS = {{ large = {}, small = {} }}\nlocal FADE_SECS = {}\n{}",
        layout(true).lua(),
        layout(false).lua(),
        FADE_SECS,
        INSTALL_BODY
    )
}

/// The install's body (`LAYOUTS` and `FADE_SECS` set before it).
pub const INSTALL_BODY: &str = r#"
local B = { on = nil, system = nil, errors = {} }
BenillaMw2Bar = B
B.LAYOUTS = LAYOUTS
B.installed = GetTime and GetTime() or 0

local LARGE, LABEL, GRYPHONS = "womwLargeActionButtons", "womwShowSystemLabel", "womwShowGryphons"
RegisterCVar(LARGE, "1")
RegisterCVar(LABEL, "1")
RegisterCVar(GRYPHONS, "0")

-- A command's key as bound now, as the window shows it; nil when unbound.
local function keytext(command)
  local key = GetBindingKey and GetBindingKey(command)
  if not key then return nil end
  if GetBindingText then return GetBindingText(key, "KEY_") end
  return key
end
B.keytext = keytext

-- ── The stock places, for the way back ──────────────────────────────────────────────────────
local STOCK_PIECE_X = { -384, -128, 128, 384 }
local STOCK_MANAGED = {}
if UIPARENT_MANAGED_FRAME_POSITIONS then
  for k, v in pairs(UIPARENT_MANAGED_FRAME_POSITIONS) do
    local c = {}
    for a, b in pairs(v) do c[a] = b end
    STOCK_MANAGED[k] = c
  end
end

-- ── The system's emblem, its name and page, and the switch's fade ───────────────────────────
local emblem = CreateFrame("Button", "BenillaMw2SystemButton", MainMenuBarArtFrame)
emblem:SetWidth(20)
emblem:SetHeight(20)
local icon = emblem:CreateTexture("BenillaMw2SystemButtonIcon", "ARTWORK")
icon:SetAllPoints(emblem)
icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
local border = emblem:CreateTexture("BenillaMw2SystemButtonBorder", "OVERLAY")
border:SetTexture("Interface\\Buttons\\UI-Quickslot2")
border:SetWidth(36)
border:SetHeight(36)
border:SetPoint("CENTER", emblem, "CENTER", 0, 0)
emblem:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square")
local hl = emblem.GetHighlightTexture and emblem:GetHighlightTexture()
if hl and hl.SetBlendMode then hl:SetBlendMode("ADD") end
emblem:Hide()

local label_frame = CreateFrame("Frame", "BenillaMw2SystemLabelFrame", MainMenuBarArtFrame)
label_frame:SetWidth(160)
label_frame:SetHeight(12)
local label = label_frame:CreateFontString("BenillaMw2SystemLabel", "OVERLAY")
if GameFontNormalSmall then label:SetFontObject(GameFontNormalSmall) end
label:SetJustifyH("LEFT")
label:SetPoint("BOTTOMLEFT", label_frame, "BOTTOMLEFT", 0, 0)
label_frame:Hide()

-- The tooltip: the system, its page, and the keys bound now.
local function line(text, key)
  return text .. ": " .. (keytext(key) or "Unbound")
end
function B.Tooltip()
  if not B.system then return end
  GameTooltip:SetOwner(emblem, "ANCHOR_RIGHT")
  GameTooltip:SetText(B.system .. " System", 1.0, 1.0, 1.0)
  GameTooltip:AddLine("Page " .. B.page .. " of " .. B.pages, NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.r or 1.0,
    NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.g or 0.82, NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.b or 0.0)
  GameTooltip:AddLine(line("Next page", "WOMW_PAGENEXT"), 1.0, 1.0, 1.0)
  GameTooltip:AddLine(line("Previous page", "WOMW_PAGEPREV"), 1.0, 1.0, 1.0)
  GameTooltip:AddLine(line("Modern / Classic", "WOMW_SWITCH"), 1.0, 1.0, 1.0)
  GameTooltip:AddLine(line("Weapons away (Utility)", "WOMW_SHEATHE"), 1.0, 1.0, 1.0)
  GameTooltip:AddLine(line("Companion bar", "WOMW_COMPANION"), 1.0, 1.0, 1.0)
  GameTooltip:AddLine("Crouch: tap Left Ctrl", 1.0, 1.0, 1.0)
  if B.system == "Companion" then
    GameTooltip:AddLine("Your companion's moves and commands. Right-click a move: autocast on or off. Ctrl+Space: Launch on a flying companion.", NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.r or 1.0,
      NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.g or 0.82, NORMAL_FONT_COLOR and NORMAL_FONT_COLOR.b or 0.0, 1)
  end
  GameTooltip:AddLine(line("Nameplates", "NAMEPLATES"), 1.0, 1.0, 1.0)
  GameTooltip:Show()
end
emblem:SetScript("OnEnter", function() B.Tooltip() end)
emblem:SetScript("OnLeave", function() GameTooltip:Hide() end)

local function show_label()
  if B.on and GetCVar(LABEL) ~= "0" then label_frame:Show() else label_frame:Hide() end
end

-- The six buttons' alpha and the name's colour along a switch's fade (0 just switched, 1 done).
local function paint(a)
  if not B.on then return end
  for i = 1, 6 do
    local b = getglobal("ActionButton" .. i)
    if b then b:SetAlpha(0.25 + 0.75 * a) end
  end
  label:SetTextColor(1.0, 1.0 - 0.18 * a, 1.0 - a)
end
B.paint = paint

local driver = CreateFrame("Frame", "BenillaMw2BarDriver", UIParent)
driver:SetScript("OnUpdate", function()
  if not B.fade then return end
  B.fade = B.fade + (arg1 or 0)
  local a = B.fade / FADE_SECS
  if a >= 1 then
    a = 1
    B.fade = nil
  end
  paint(a)
end)

-- ── The layout ──────────────────────────────────────────────────────────────────────────────
local function place_caps()
  local g = B.g
  local on = GetCVar(GRYPHONS) == "1"
  for i, cap in ipairs({ MainMenuBarLeftEndCap, MainMenuBarRightEndCap }) do
    if cap then
      cap:ClearAllPoints()
      if g then
        if i == 1 then
          cap:SetPoint("BOTTOM", MainMenuBarArtFrame, "BOTTOMLEFT", -32, 0)
        else
          cap:SetPoint("BOTTOM", MainMenuBarArtFrame, "BOTTOMRIGHT", 32, 0)
        end
      else
        cap:SetPoint("BOTTOM", MainMenuBarArtFrame, "BOTTOM", i == 1 and -544 or 544, 0)
      end
      if on then cap:Show() else cap:Hide() end
    end
  end
end
B.ApplyGryphons = place_caps

-- The XP bar's (and its kin's) four art pieces fitted to `w`, chained from the left.
local function fit_pieces(prefix, w)
  local q = w / 4
  for i = 0, 3 do
    local t = getglobal(prefix .. i)
    if t then t:SetWidth(q) end
  end
end

-- The watched reputation's bar over the XP bar's span (the stock update seats it over 1024).
local function fit_rep()
  if not ReputationWatchBar or not ReputationWatchStatusBar then return end
  local g = B.g
  local w = g and g.xp_width or 1024
  ReputationWatchBar:SetWidth(w)
  ReputationWatchStatusBar:SetWidth(w)
  fit_pieces("ReputationWatchBarTexture", w)
  fit_pieces("ReputationXPBarTexture", w)
  if g then
    ReputationWatchBar:ClearAllPoints()
    if MainMenuExpBar:IsShown() then
      ReputationWatchBar:SetPoint("BOTTOMLEFT", MainMenuBar, "TOPLEFT", g.xp_left, -3)
    else
      ReputationWatchBar:SetPoint("TOPLEFT", MainMenuBar, "TOPLEFT", g.xp_left, 0)
    end
  end
end
B.fit_rep = fit_rep
if ReputationWatchBar_Update then
  local stock_update = ReputationWatchBar_Update
  ReputationWatchBar_Update = function(level)
    stock_update(level)
    fit_rep()
  end
end

local function xp_bar()
  local g = B.g
  local w, x = g and g.xp_width or 1024, g and g.xp_left or 0
  MainMenuExpBar:SetWidth(w)
  MainMenuExpBar:ClearAllPoints()
  if g then
    MainMenuExpBar:SetPoint("TOPLEFT", MainMenuBar, "TOPLEFT", x, 0)
  else
    MainMenuExpBar:SetPoint("TOP", MainMenuBar, "TOP", 0, 0)
  end
  local q = w / 4
  for i = 0, 3 do
    local t = getglobal("MainMenuXPBarTexture" .. i)
    if t then
      t:SetWidth(g and q or 256)
      t:ClearAllPoints()
      if g then
        t:SetPoint("BOTTOMLEFT", MainMenuExpBar, "BOTTOMLEFT", q * i, 3)
      else
        t:SetPoint("BOTTOM", MainMenuExpBar, "BOTTOM", STOCK_PIECE_X[i + 1], 3)
      end
    end
  end
  if MainMenuBarMaxLevelBar then
    MainMenuBarMaxLevelBar:SetWidth(w)
    MainMenuBarMaxLevelBar:ClearAllPoints()
    if g then
      MainMenuBarMaxLevelBar:SetPoint("TOPLEFT", MainMenuBar, "TOPLEFT", x, -11)
    else
      MainMenuBarMaxLevelBar:SetPoint("TOP", MainMenuBar, "TOP", 0, -11)
    end
    fit_pieces("MainMenuMaxLevelBar", w)
    if MainMenuMaxLevelBar0 then
      MainMenuMaxLevelBar0:ClearAllPoints()
      MainMenuMaxLevelBar0:SetPoint("BOTTOMLEFT", MainMenuBarMaxLevelBar, "TOPLEFT", 0, 0)
    end
  end
  -- The rested tick and fill are seated from the bar's width.
  if ExhaustionTick_Update and UnitXPMax and UnitXPMax("player") and UnitXPMax("player") > 0 then
    local was = event
    event = "PLAYER_XP_UPDATE"
    pcall(ExhaustionTick_Update)
    event = was
  end
end

local function managed()
  if not UIPARENT_MANAGED_FRAME_POSITIONS then return end
  local g = B.g
  for k, stock in pairs(STOCK_MANAGED) do
    local v = {}
    for a, b in pairs(stock) do v[a] = b end
    if g then
      if v.baseY then v.baseY = v.baseY + g.raise end
      for _, f in ipairs({ "bottomLeft", "bottomEither", "bottomRight" }) do
        if v[f] then v[f] = v[f] + g.raise_stack end
      end
    end
    UIPARENT_MANAGED_FRAME_POSITIONS[k] = v
  end
  if g and STOCK_MANAGED.MultiBarBottomLeft then
    -- The two bottom multibars centred over the bar, over the system's name.
    local w = MultiBarBottomLeft and MultiBarBottomLeft:GetWidth() or 500
    UIPARENT_MANAGED_FRAME_POSITIONS.MultiBarBottomLeft = { baseY = g.multibar_base,
      anchorTo = "MainMenuBar", point = "BOTTOMLEFT", rpoint = "TOP", xOffset = -(2 * w + 10) / 2 }
  end
  if UIParent_ManageFramePositions then pcall(UIParent_ManageFramePositions) end
end

local function bonus_bar()
  if not BonusActionBarFrame then return end
  local g = B.g
  BonusActionBarFrame:SetScale(g and g.scale or 1)
  -- Its mouse-enabled frame no wider than the six wells: the micro menu stays clickable.
  BonusActionBarFrame:SetWidth(g and 252 or 505)
  if BonusActionBarTexture1 then
    if g then BonusActionBarTexture1:Hide() else BonusActionBarTexture1:Show() end
  end
  for i = 7, 12 do
    local b = getglobal("BonusActionButton" .. i)
    if b then
      b:SetAlpha(g and 0 or 1)
      if g then b:EnableMouse(nil) else b:EnableMouse(1) end
    end
  end
end

function B.Layout()
  local g = nil
  if B.on then
    g = (GetCVar(LARGE) == "0") and LAYOUTS.small or LAYOUTS.large
  end
  B.g = g
  MainMenuBar:SetWidth(g and g.width or 1024)
  for i = 0, 3 do
    local t = getglobal("MainMenuBarTexture" .. i)
    if t then
      t:ClearAllPoints()
      t:SetWidth(256)
      t:SetHeight(43)
      t:Show()
      if not g then t:SetPoint("BOTTOM", MainMenuBarArtFrame, "BOTTOM", STOCK_PIECE_X[i + 1], 0) end
    end
  end
  if g then
    MainMenuBarTexture0:SetWidth(g.big_w)
    MainMenuBarTexture0:SetHeight(g.big_h)
    MainMenuBarTexture0:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", 0, 0)
    MainMenuBarTexture1:Hide()
    MainMenuBarTexture2:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", g.big_w, 0)
    MainMenuBarTexture3:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", g.big_w + 256, 0)
  end
  for i = 1, 12 do
    local b = getglobal("ActionButton" .. i)
    if b then b:SetScale((g and i <= 6) and g.scale or 1) end
  end
  xp_bar()
  fit_rep()
  if not g and ReputationWatchBar_Update then pcall(ReputationWatchBar_Update) end
  place_caps()
  if MainMenuBarPageNumber then
    MainMenuBarPageNumber:ClearAllPoints()
    if g then
      MainMenuBarPageNumber:SetPoint("CENTER", MainMenuBarArtFrame, "BOTTOMLEFT", g.big_w + 30, 21.5)
    else
      MainMenuBarPageNumber:SetPoint("CENTER", MainMenuBarArtFrame, "CENTER", 30, -5)
    end
  end
  if CharacterMicroButton then
    CharacterMicroButton:ClearAllPoints()
    CharacterMicroButton:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", g and (g.big_w + 40) or 552, 2)
  end
  emblem:ClearAllPoints()
  label_frame:ClearAllPoints()
  if g then
    emblem:SetPoint("CENTER", MainMenuBarArtFrame, "BOTTOMLEFT", g.big_w + 11, 21.5)
    label_frame:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", 8, g.left_top + 1)
    emblem:Show()
  else
    emblem:Hide()
    for i = 1, 6 do
      local b = getglobal("ActionButton" .. i)
      if b then b:SetAlpha(1) end
    end
  end
  show_label()
  bonus_bar()
  managed()
end

local function show_system()
  if not B.system then return end
  icon:SetTexture(B.icon)
  label:SetText(B.system .. "  |cffffffff" .. B.page .. "/" .. B.pages .. "|r")
end

-- The stock pet bar: the Companion system has the companion's bar while the layer is on.
local function pet_bar()
  local f = PetActionBarFrame
  if not f then return end
  if B.on then
    if f:IsShown() then f:Hide() end
  elseif UnitExists and UnitExists("pet") and ShowPetActionBar then
    pcall(ShowPetActionBar)
  end
end
B.pet_bar = pet_bar
if PetActionBarFrame and PetActionBarFrame.GetScript then
  local stock_show = PetActionBarFrame:GetScript("OnShow")
  PetActionBarFrame:SetScript("OnShow", function()
    if stock_show then stock_show() end
    if B.on then this:Hide() end
  end)
end

-- `Set(true, name, icon, page, pages)` shows a system's bar; `Set(nil)` puts the stock one back.
function B.Set(on, system, icon_path, page, pages)
  local was = B.on
  B.on = on and true or nil
  if B.on ~= was then B.Layout() pet_bar() end
  if not B.on then
    B.system, B.fade = nil, nil
    return
  end
  if B.system and B.system ~= system then
    B.fade = 0
    paint(0)
  end
  B.system, B.icon, B.page, B.pages = system, icon_path, page, pages
  show_system()
  if not B.fade then paint(1) end
end

-- ── The Interface options: Large Action Buttons, Show Action System Label, Show Gryphons ────
WOMW_TOOLTIP_LARGE_ACTION_BUTTONS = "Draws the six action buttons larger, on wells sized to them. Off, they are the stock size."
WOMW_TOOLTIP_SHOW_SYSTEM_LABEL = "Shows the action system in use (Modern, Classic, Utility or Companion) and its page over the action bar."
WOMW_TOOLTIP_SHOW_GRYPHONS = "Shows the gryphons at the ends of the main action bar."
local function option_row(key, text, tip, cvar, default, apply)
  local page = BenillaOptionsFrameContainerBodyInterface
  local rows = BENILLA_OPTIONS_PAGE_ROWS and BENILLA_OPTIONS_PAGE_ROWS.Interface
  if not page or not rows or not BenillaOptionsStrFuncRow_OnLoad then return end
  local last = getglobal(page:GetName() .. rows[table.getn(rows)])
  local name = page:GetName() .. key
  local ok, row = pcall(CreateFrame, "Frame", name, page, "BenillaOptionsCheckboxRowTemplate")
  if not ok or not row then
    -- Without the options window's template (a test kit): a plain row.
    row = CreateFrame("Frame", name, page)
    row:SetHeight(26)
  end
  row:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -9)
  row:SetPoint("TOPRIGHT", last, "BOTTOMRIGHT", 0, -9)
  BenillaOptionsStrFuncRow_OnLoad(row,
    function() return GetCVar(cvar) end,
    function(v) SetCVar(cvar, v) apply() end,
    function() return default end,
    text, tip)
  table.insert(rows, key)
end
for _, step in ipairs({
  function() option_row("RowLargeActionButtons", "Large Action Buttons", "WOMW_TOOLTIP_LARGE_ACTION_BUTTONS", LARGE, "1", function() if B.on then B.Layout() end end) end,
  function() option_row("RowShowSystemLabel", "Show Action System Label", "WOMW_TOOLTIP_SHOW_SYSTEM_LABEL", LABEL, "1", show_label) end,
  function() option_row("RowShowGryphons", "Show Gryphons", "WOMW_TOOLTIP_SHOW_GRYPHONS", GRYPHONS, "0", place_caps) end,
  place_caps,
}) do
  local ok, err = pcall(step)
  if not ok then table.insert(B.errors, tostring(err)) end
end

-- ── The nameplate key (H): what it did, in the stock error frame's yellow ───────────────────
local function plates(on)
  if not B.on or not UIErrorsFrame or (GetTime and GetTime() - B.installed < 3) then return end
  local k = keytext("NAMEPLATES")
  local text = on and "Nameplates shown" or "Nameplates hidden"
  if k then text = text .. " (" .. k .. ")" end
  UIErrorsFrame:AddMessage(text, 1.0, 0.82, 0.0, 1.0, UIERRORS_HOLD_TIME)
end
for name, on in pairs({ ShowNameplates = true, HideNameplates = false }) do
  local stock = getglobal(name)
  if stock then
    local shown = on
    setglobal(name, function() stock() plates(shown) end)
  end
end
"#;

/// What the install's steps raised, `\n`-joined (nothing when all went in).
pub const INSTALL_ERRORS_LUA: &str = "local e = BenillaMw2Bar and BenillaMw2Bar.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

pub struct MainBarPlugin;

impl Plugin for MainBarPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(
            Update,
            drive
                .in_set(Mw2Set::Hud)
                .after(crate::action_systems::ActionSystemsSet),
        );
    }
}

/// Install the re-fit once per world VM and keep it on the active system's page.
fn drive(
    state: Res<FpsState>,
    systems: Res<ActionSystems>,
    script: Option<NonSend<UiScript>>,
    mut sent: Local<Option<Option<BarView>>>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        *sent = None;
        return;
    };
    if !crate::lua::eval::<bool>(&script, "return BenillaMw2Bar ~= nil").unwrap_or(false) {
        crate::lua::run(&script, &install_lua());
        if let Some(Some(e)) = crate::lua::eval::<Option<String>>(&script, INSTALL_ERRORS_LUA) {
            warn!("benilla-mw2: main bar install: {e}");
        }
        *sent = None;
    }
    let view = state.active.then(|| BarView::of(&systems));
    if sent.as_ref() != Some(&view) {
        crate::lua::run(&script, &set_lua(view.as_ref()));
        *sent = Some(view);
    }
}

#[cfg(test)]
#[path = "main_bar_tests.rs"]
mod tests;
