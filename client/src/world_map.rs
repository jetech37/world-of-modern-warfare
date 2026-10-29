//! The world map as a window: the stock `WorldMapFrame` (the player's own `WorldMapFrame.xml`,
//! its art, zones, overlays, POIs, player arrow and group blips) reshaped from the live UI's own
//! Lua, as a 1.12 map addon reshapes it, so the game goes on around it:
//!
//! - **M** and the menu bar's Map button (both call `ToggleWorldMap`) open it as a movable,
//!   resizable window on the stock parchment, not the full-screen panel: no black-out, the rest
//!   of the interface stays up, the keyboard stays the game's, and the gun keeps the mouse (hold
//!   Left Alt for the cursor, as for every window). Esc closes it.
//! - Drag it by its frame; it keeps its place and size between opens and sessions (the account
//!   CVars below). The round arrow by its close button puts it back; the grip at its bottom right,
//!   or Ctrl + mouse wheel over the map, sizes it.
//! - It fades while the player moves (configurable) and comes back when they stop or point at it.
//! - The player's and the cursor's map coordinates show along its bottom edge.
//! - The stock zone navigation works as it does full-screen (continent and zone dropdowns, a
//!   click zooms into the zone under it, a right click zooms out); the mouse wheel zooms in at the
//!   cursor and out. The map follows the player into a new zone while it shows their zone.
//! - An Options button on the map, and rows on the options window's Interface page, switch the
//!   fade, its opacity, the size, the coordinates and the group icons, and go back to the stock
//!   full-screen map (**Full-Screen World Map**).
//!
//! The design follows the feature set of the OpenMap addon (MIT, CurseForge); none of its code or
//! art is used. Everything drawn is the player's own install, read at runtime.
//!
//! Rust tells the Lua when the player is moving and whether the cursor is free ([`state_lua`]);
//! the Lua does the rest on the map's own `OnUpdate`, which runs only while the map shows.

use benilla_app::Objects;
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;

/// The reshape, run once per world VM (a reload or a relog builds a new one).
pub const INSTALL_LUA: &str = r#"
if BenillaMw2WorldMap or not WorldMapFrame or not WorldMapButton or not WorldMapDetailFrame then return end
local M = {}
BenillaMw2WorldMap = M
local f = WorldMapFrame

-- ── Settings: the account's CVars, read into M when they change ─────────────────────────────
M.CVARS = {
  womwMapFullScreen = "0",   -- "1": the stock full-screen map
  womwMapScale = "0.65",     -- the window's scale over the stock 1024 x 768 map
  womwMapPos = "",           -- "x,y": the window's top-left as shares of the screen; "" = home
  womwMapFade = "1",         -- fade while moving
  womwMapFadeAlpha = "0.35", -- the faded opacity
  womwMapCoords = "1",       -- the coordinates line
  womwMapGroup = "1",        -- party and raid icons
  womwMapLock = "0",         -- no dragging
}
M.MIN_SCALE, M.MAX_SCALE = 0.3, 1.0
-- Opacity per second while fading, and seconds between coordinate refreshes.
M.FADE_SPEED = 2.5
M.COORD_PERIOD = 0.1
-- The stock map's size, the window's in its own units.
M.WIDTH, M.HEIGHT = 1024, 768
for k, v in pairs(M.CVARS) do RegisterCVar(k, v) end

local function clamp(x, lo, hi)
  if x < lo then return lo end
  if x > hi then return hi end
  return x
end

function M.Get(k)
  local v = GetCVar(k)
  if v == nil then return M.CVARS[k] end
  return v
end

local function num(k, lo, hi)
  return clamp(tonumber(M.Get(k)) or tonumber(M.CVARS[k]), lo, hi)
end

-- A number as the options window stores one: two places, trailing zeros off.
function M.Num(x)
  local s = string.gsub(string.format("%.2f", x), "%.?0+$", "")
  return s
end

function M.ReadOptions()
  M.fade = M.Get("womwMapFade") == "1"
  M.fadeAlpha = num("womwMapFadeAlpha", 0.05, 1)
  M.coords = M.Get("womwMapCoords") == "1"
  M.group = M.Get("womwMapGroup") == "1"
  M.lock = M.Get("womwMapLock") == "1"
  M.scale = num("womwMapScale", M.MIN_SCALE, M.MAX_SCALE)
end

-- Set by Rust (crate::world_map): the player is moving; the cursor is out (not mouselook).
M.moving = nil
M.cursor = 1
function M.SetState(moving, cursor)
  M.moving = moving
  M.cursor = cursor
end

-- ── Where the window sits ─────────────────────────────────────────────────────────────────
-- The screen in screen units (`GetCursorPosition`'s), UIParent being the whole screen.
local function screen()
  local s = UIParent:GetEffectiveScale()
  return UIParent:GetWidth() * s, UIParent:GetHeight() * s
end

-- Scaled, sized and anchored: at its saved top-left, else at home on the right of the screen.
-- `WorldMapFrame` has no parent, so an offset is in its own units: screen units over its scale.
function M.Seat()
  f:SetScale(M.scale)
  f:ClearAllPoints()
  f:SetWidth(M.WIDTH)
  f:SetHeight(M.HEIGHT)
  local fe = f:GetEffectiveScale()
  local sw, sh = screen()
  local _, _, x, y = string.find(M.Get("womwMapPos") or "", "^([%d%.]+),([%d%.]+)$")
  x, y = tonumber(x), tonumber(y)
  if x and y then
    f:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", x * sw / fe, y * sh / fe)
  else
    local ue = UIParent:GetEffectiveScale()
    f:SetPoint("RIGHT", UIParent, "RIGHT", -24 * ue / fe, 16 * ue / fe)
  end
end

-- The window's top-left as shares of the screen; nil before it is laid out.
function M.TopLeft()
  local l, t = f:GetLeft(), f:GetTop()
  local sw, sh = screen()
  if not l or not t or sw <= 0 or sh <= 0 then return nil end
  local fe = f:GetEffectiveScale()
  return clamp(l * fe / sw, 0, 1), clamp(t * fe / sh, 0, 1)
end

local function keep_top_left()
  local x, y = M.TopLeft()
  if x then SetCVar("womwMapPos", string.format("%.4f,%.4f", x, y)) end
end

function M.SavePosition()
  keep_top_left()
  M.Seat()
end

-- A new size about the window's top-left, which stays put.
function M.SetScale(s)
  s = clamp(s, M.MIN_SCALE, M.MAX_SCALE)
  if M.windowed then keep_top_left() end
  SetCVar("womwMapScale", M.Num(s))
  M.scale = s
  if M.windowed then M.Seat() end
end

function M.Reset()
  SetCVar("womwMapPos", "")
  SetCVar("womwMapScale", M.CVARS.womwMapScale)
  M.ReadOptions()
  if M.windowed then M.Seat() end
end

-- ── The cursor on the map ───────────────────────────────────────────────────────────────────
-- The cursor's map position (0..1 east, 0..1 south) over `WorldMapButton`, the same law the
-- stock click and hover use (WorldMapFrame.lua's `WorldMapButton_OnClick`); nil off the map.
function M.CursorUV()
  local b = WorldMapButton
  local l, t, w, h = b:GetLeft(), b:GetTop(), b:GetWidth(), b:GetHeight()
  if not l or not t or not w or not h or w <= 0 or h <= 0 then return nil end
  local x, y = GetCursorPosition()
  local s = b:GetEffectiveScale()
  local u, v = (x / s - l) / w, (t - y / s) / h
  if u < 0 or u > 1 or v < 0 or v > 1 then return nil end
  return u, v
end

-- "43.8, 65.8": a map position as the percent coordinates players trade.
function M.FormatCoords(u, v)
  return string.format("%.1f, %.1f", u * 100, v * 100)
end

-- ── Our pieces on the stock frame, shown only as a window ───────────────────────────────────
local bar = CreateFrame("Frame", "BenillaMw2WorldMapBar", f)
bar:SetAllPoints(f)
bar:Hide()
M.bar = bar

local player = bar:CreateFontString("BenillaMw2WorldMapPlayerCoords", "OVERLAY", "GameFontNormal")
player:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 24, 10)
player:SetJustifyH("LEFT")
local cursor = bar:CreateFontString("BenillaMw2WorldMapCursorCoords", "OVERLAY", "GameFontNormal")
cursor:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -48, 10)
cursor:SetJustifyH("RIGHT")

function M.UpdateCoords()
  if not M.coords then
    player:SetText("")
    cursor:SetText("")
    return
  end
  local px, py = GetPlayerMapPosition("player")
  if px and py and (px ~= 0 or py ~= 0) then
    player:SetText("Player: " .. M.FormatCoords(px, py))
  else
    player:SetText("Player: --")
  end
  local u, v
  if M.cursor then u, v = M.CursorUV() end
  if u then
    cursor:SetText("Cursor: " .. M.FormatCoords(u, v))
  else
    cursor:SetText("")
  end
end

-- The reset button: the stock model frame's turn-left arrow, by the close button.
local reset = CreateFrame("Button", "BenillaMw2WorldMapResetButton", bar)
reset:SetWidth(24)
reset:SetHeight(24)
reset:SetPoint("RIGHT", WorldMapFrameCloseButton, "LEFT", 2, 0)
reset:SetNormalTexture("Interface\\Buttons\\UI-RotationLeft-Button-Up")
reset:SetPushedTexture("Interface\\Buttons\\UI-RotationLeft-Button-Down")
reset:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Round")
reset:SetScript("OnClick", function()
  PlaySound("igMainMenuOptionCheckBoxOn")
  M.Reset()
end)
reset:SetScript("OnEnter", function()
  GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
  GameTooltip:SetText("Reset Position", 1.0, 1.0, 1.0)
  GameTooltip:AddLine("Puts the map back where it opens by default, at its default size.", nil, nil, nil, 1)
  GameTooltip:Show()
end)
reset:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- The size grip: a gold staircase in the bottom-right corner; drag it, the top-left stays.
local grip = CreateFrame("Button", "BenillaMw2WorldMapSizeGrip", bar)
grip:SetWidth(28)
grip:SetHeight(28)
grip:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -8, 8)
grip:EnableMouse(true)
for i = 0, 2 do
  for j = 0, i do
    local t = grip:CreateTexture(nil, "OVERLAY")
    t:SetWidth(5)
    t:SetHeight(5)
    t:SetTexture(1.0, 0.82, 0.0, 0.9)
    t:SetPoint("BOTTOMRIGHT", grip, "BOTTOMRIGHT", -2 - 8 * j, 2 + 8 * (i - j))
  end
end
grip:SetScript("OnMouseDown", function() M.BeginSizing() end)
grip:SetScript("OnMouseUp", function() M.EndSizing() end)
grip:SetScript("OnEnter", function()
  GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
  GameTooltip:SetText("Resize", 1.0, 1.0, 1.0)
  GameTooltip:AddLine("Drag to size the map. Ctrl + mouse wheel over the map sizes it too.", nil, nil, nil, 1)
  GameTooltip:Show()
end)
grip:SetScript("OnLeave", function() GameTooltip:Hide() end)

function M.BeginSizing()
  keep_top_left()
  M.sizing = 1
end

function M.EndSizing()
  M.sizing = nil
end

-- The scale that puts the window's bottom-right under the cursor, its top-left held.
function M.SizeToCursor()
  local fe = f:GetEffectiveScale()
  local l, t = f:GetLeft(), f:GetTop()
  if not l or not t then return end
  local x, y = GetCursorPosition()
  local s = math.max((x - l * fe) / M.WIDTH, (t * fe - y) / M.HEIGHT)
  s = clamp(s, M.MIN_SCALE, M.MAX_SCALE)
  if math.abs(s - M.scale) >= 0.005 then M.SetScale(s) end
end

-- ── The options menu on the map ─────────────────────────────────────────────────────────────
local options = CreateFrame("Button", "BenillaMw2WorldMapOptionsButton", bar, "UIPanelButtonTemplate")
options:SetWidth(96)
options:SetHeight(22)
options:SetPoint("TOPRIGHT", WorldMapPositioningGuide, "TOPRIGHT", -40, -38)
options:SetText("Options")
local menu = CreateFrame("Frame", "BenillaMw2WorldMapOptionsMenu", bar, "UIDropDownMenuTemplate")
menu:Hide()

-- Each entry flips its CVar; the last two act.
M.MENU = {
  { "Fade While Moving", "womwMapFade" },
  { "Show Coordinates", "womwMapCoords" },
  { "Show Group Icons", "womwMapGroup" },
  { "Lock Position", "womwMapLock" },
}

function M.Toggle(cvar)
  if M.Get(cvar) == "1" then SetCVar(cvar, "0") else SetCVar(cvar, "1") end
  M.Apply()
end

function M.InitMenu()
  local info = { text = "World Map", isTitle = 1 }
  UIDropDownMenu_AddButton(info)
  for _, e in ipairs(M.MENU) do
    local cvar = e[2]
    info = {}
    info.text = e[1]
    info.checked = M.Get(cvar) == "1"
    info.keepShownOnClick = 1
    info.func = function() M.Toggle(cvar) end
    UIDropDownMenu_AddButton(info)
  end
  info = {}
  info.text = "Reset Position and Size"
  info.func = function() M.Reset() end
  UIDropDownMenu_AddButton(info)
  info = {}
  info.text = "Use Full-Screen Map"
  info.func = function() M.SetFullScreen(1) end
  UIDropDownMenu_AddButton(info)
end

options:SetScript("OnClick", function()
  UIDropDownMenu_Initialize(BenillaMw2WorldMapOptionsMenu, M.InitMenu, "MENU")
  ToggleDropDownMenu(1, nil, BenillaMw2WorldMapOptionsMenu, "BenillaMw2WorldMapOptionsButton", 0, 0)
end)

-- ── Per frame while the map shows: fade, size, coordinates ──────────────────────────────────
M.alpha = 1
M.clock = 0

-- The opacity the window heads for: faded while the player moves, unless the cursor is on it.
function M.TargetAlpha()
  if M.fade and M.moving and not (M.cursor and MouseIsOver(f)) then return M.fadeAlpha end
  return 1
end

function M.OnUpdate(elapsed)
  elapsed = elapsed or 0
  if M.sizing then M.SizeToCursor() end
  local target = M.TargetAlpha()
  if M.alpha ~= target then
    local step = M.FADE_SPEED * elapsed
    if M.alpha > target then
      M.alpha = math.max(target, M.alpha - step)
    else
      M.alpha = math.min(target, M.alpha + step)
    end
    f:SetAlpha(M.alpha)
  end
  M.clock = M.clock - elapsed
  if M.clock <= 0 then
    M.clock = M.COORD_PERIOD
    M.UpdateCoords()
  end
end

local driver = CreateFrame("Frame", "BenillaMw2WorldMapDriver", bar)
driver:SetScript("OnUpdate", function() M.OnUpdate(arg1) end)
-- Into a new zone with the map on the player's zone: the map goes along.
driver:RegisterEvent("ZONE_CHANGED_NEW_AREA")
driver:SetScript("OnEvent", function() M.OnZoneChanged() end)

function M.Follow()
  M.followC, M.followZ = GetCurrentMapContinent(), GetCurrentMapZone()
end

function M.OnZoneChanged()
  if not M.windowed or not f:IsVisible() then return end
  if M.followC == GetCurrentMapContinent() and M.followZ == GetCurrentMapZone() then
    SetMapToCurrentZone()
    M.Follow()
  end
end

-- ── The map's own controls, as a window ─────────────────────────────────────────────────────
f:SetScript("OnDragStart", function()
  if M.windowed and not M.lock then this:StartMoving() end
end)
f:SetScript("OnDragStop", function()
  this:StopMovingOrSizing()
  -- Our CVar keeps the place; the layout cache stays out of it.
  if this.SetUserPlaced then this:SetUserPlaced(false) end
  if M.windowed then M.SavePosition() end
end)

-- The wheel: in at the cursor, out a level; with Ctrl, the window's size.
function M.OnWheel(delta)
  if not M.windowed or not delta then return end
  if IsControlKeyDown() then
    M.SetScale(M.scale + 0.05 * delta)
  elseif delta > 0 then
    local u, v = M.CursorUV()
    if u then ProcessMapClick(u, v) end
  else
    WorldMapZoomOutButton_OnClick()
  end
end
WorldMapButton:SetScript("OnMouseWheel", function() M.OnWheel(arg1) end)

-- The stock OnShow scales the map to the screen (`SetupFullscreenScale`); a window keeps its own.
local stockOnShow = f:GetScript("OnShow")
f:SetScript("OnShow", function()
  if stockOnShow then stockOnShow() end
  if M.windowed then
    M.Seat()
    M.alpha = 1
    f:SetAlpha(1)
    M.clock = 0
    M.Follow()
  end
end)

-- M and the menu bar's Map button.
M.StockToggleWorldMap = ToggleWorldMap
function ToggleWorldMap()
  if M.windowed then
    if f:IsShown() then f:Hide() else f:Show() end
  else
    M.StockToggleWorldMap()
  end
end

-- ── The two modes ───────────────────────────────────────────────────────────────────────────
M.stockPanel = UIPanelWindows and UIPanelWindows["WorldMapFrame"]

local function special(add)
  if not UISpecialFrames then return end
  for i = table.getn(UISpecialFrames), 1, -1 do
    if UISpecialFrames[i] == "WorldMapFrame" then table.remove(UISpecialFrames, i) end
  end
  if add then table.insert(UISpecialFrames, "WorldMapFrame") end
end

function M.ToWindow()
  if f:IsShown() and not M.windowed then HideUIPanel(f) end
  M.windowed = 1
  -- Out of the panel manager: shown and hidden as itself, never the full-screen slot.
  if UIPanelWindows then UIPanelWindows["WorldMapFrame"] = nil end
  f:SetFrameStrata("MEDIUM")
  f:EnableKeyboard(false)
  f:EnableMouse(true)
  f:SetMovable(true)
  f:SetClampedToScreen(true)
  f:RegisterForDrag("LeftButton")
  WorldMapButton:EnableMouseWheel(true)
  if BlackoutWorld then BlackoutWorld:Hide() end
  -- Esc closes it, as the stock windows that are not panels.
  special(1)
  bar:Show()
  M.Seat()
  f:SetAlpha(M.alpha)
end

function M.ToStock()
  if f:IsShown() then f:Hide() end
  M.windowed = nil
  if UIPanelWindows then UIPanelWindows["WorldMapFrame"] = M.stockPanel end
  f:SetFrameStrata("FULLSCREEN")
  f:EnableKeyboard(true)
  f:EnableMouse(false)
  f:SetMovable(false)
  f:SetClampedToScreen(false)
  WorldMapButton:EnableMouseWheel(false)
  if BlackoutWorld then BlackoutWorld:Show() end
  special(nil)
  bar:Hide()
  M.sizing = nil
  M.alpha = 1
  f:SetAlpha(1)
  f:ClearAllPoints()
  f:SetAllPoints(UIParent)
end

-- The group's blips: hidden by opacity, so the stock update that shows them each frame can.
function M.ApplyGroup()
  local a = 0
  if M.group then a = 1 end
  for i = 1, (MAX_PARTY_MEMBERS or 4) do
    local b = getglobal("WorldMapParty" .. i)
    if b then b:SetAlpha(a) b:EnableMouse(M.group) end
  end
  for i = 1, (MAX_RAID_MEMBERS or 40) do
    local b = getglobal("WorldMapRaid" .. i)
    if b then b:SetAlpha(a) b:EnableMouse(M.group) end
  end
end

-- Every setting as it stands now. The first call in the stock mode touches nothing.
function M.Apply()
  M.ReadOptions()
  if M.Get("womwMapFullScreen") == "1" then
    if M.windowed then M.ToStock() end
  else
    M.ToWindow()
  end
  M.ApplyGroup()
  if f:IsVisible() and M.windowed then M.UpdateCoords() end
end

function M.SetFullScreen(on)
  if on then SetCVar("womwMapFullScreen", "1") else SetCVar("womwMapFullScreen", "0") end
  CloseDropDownMenus()
  M.Apply()
end

-- ── Rows on the options window's Interface page ─────────────────────────────────────────────
WOMW_TOOLTIP_MAP_FULLSCREEN = "Opens the classic full-screen world map instead of the movable map window."
WOMW_TOOLTIP_MAP_FADE = "The map window fades while you move, and comes back when you stop or point at it."
WOMW_TOOLTIP_MAP_FADE_ALPHA = "How visible the map window stays while it is faded."
WOMW_TOOLTIP_MAP_SCALE = "The map window's size. Its bottom-right grip and Ctrl + mouse wheel size it too."
WOMW_TOOLTIP_MAP_COORDS = "Shows your coordinates and the cursor's along the bottom of the map window."
WOMW_TOOLTIP_MAP_GROUP = "Shows your party and raid on the world map."

M.ROWS = {
  { "RowWorldMapFullScreen", "check", "womwMapFullScreen", "Full-Screen World Map", "WOMW_TOOLTIP_MAP_FULLSCREEN" },
  { "RowWorldMapFade", "check", "womwMapFade", "Fade Map While Moving", "WOMW_TOOLTIP_MAP_FADE" },
  { "RowWorldMapFadeAlpha", "slider", "womwMapFadeAlpha", "Faded Map Opacity", "WOMW_TOOLTIP_MAP_FADE_ALPHA", 0.1, 1, 0.05 },
  { "RowWorldMapScale", "slider", "womwMapScale", "Map Window Size", "WOMW_TOOLTIP_MAP_SCALE", M.MIN_SCALE, M.MAX_SCALE, 0.05 },
  { "RowWorldMapCoords", "check", "womwMapCoords", "Show Map Coordinates", "WOMW_TOOLTIP_MAP_COORDS" },
  { "RowWorldMapGroup", "check", "womwMapGroup", "Show Group on Map", "WOMW_TOOLTIP_MAP_GROUP" },
}

local function option_rows()
  local page = BenillaOptionsFrameContainerBodyInterface
  local rows = BENILLA_OPTIONS_PAGE_ROWS and BENILLA_OPTIONS_PAGE_ROWS.Interface
  if not page or not rows or not BenillaOptionsStrFuncRow_OnLoad then return end
  for _, r in ipairs(M.ROWS) do
    local key, kind, cvar, label, tip = r[1], r[2], r[3], r[4], r[5]
    local last = getglobal(page:GetName() .. rows[table.getn(rows)])
    local name = page:GetName() .. key
    local template = "BenillaOptionsCheckboxRowTemplate"
    if kind == "slider" then template = "BenillaOptionsSliderRowTemplate" end
    local ok, row = pcall(CreateFrame, "Frame", name, page, template)
    local templated = ok and row
    if not templated then
      -- Without the options window's templates (a test kit): a plain row.
      row = CreateFrame("Frame", name, page)
      row:SetHeight(26)
    end
    row:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -9)
    row:SetPoint("TOPRIGHT", last, "BOTTOMRIGHT", 0, -9)
    BenillaOptionsStrFuncRow_OnLoad(row,
      function() return M.Get(cvar) end,
      function(v)
        SetCVar(cvar, v)
        M.Apply()
      end,
      function() return M.CVARS[cvar] end,
      label, tip)
    if kind == "slider" and templated and BenillaOptionsSliderRow_Setup then
      BenillaOptionsSliderRow_Setup(row, r[6], r[7], r[8])
    end
    table.insert(rows, key)
  end
end

option_rows()
M.Apply()
"#;

/// Whether this VM has the reshape, and whether it has a map to reshape.
const POLL_LUA: &str = "return BenillaMw2WorldMap ~= nil, WorldMapFrame ~= nil";

/// Install the reshape in this VM when it has the stock map and not yet the reshape; true when
/// this call installed it.
pub fn install(script: &UiScript) -> bool {
    match crate::lua::eval::<(bool, bool)>(script, POLL_LUA) {
        Some((false, true)) => {
            crate::lua::run(script, INSTALL_LUA);
            true
        }
        _ => false,
    }
}

/// The Lua that tells the map whether the player is moving and whether the cursor is out.
pub fn state_lua(moving: bool, cursor: bool) -> String {
    format!(
        "if BenillaMw2WorldMap then BenillaMw2WorldMap.SetState({}, {}) end",
        if moving { "1" } else { "nil" },
        if cursor { "1" } else { "nil" }
    )
}

/// Yards per second over the ground that count as moving.
const MOVING_SPEED: f32 = 1.0;
/// Seconds still before the player counts as stopped: a step's stutter does not flash the map.
const STOP_DELAY: f32 = 0.3;

/// Whether the player is moving, from their ground position each frame.
#[derive(Debug, Default)]
pub struct MoveWatch {
    last: Option<Vec2>,
    still: f32,
    moving: bool,
}

impl MoveWatch {
    /// Take this frame's ground position (WoW x, y in yards) after `dt` seconds; the state.
    pub fn update(&mut self, ground: Vec2, dt: f32) -> bool {
        let speed = match self.last {
            Some(last) if dt > 0.0 => last.distance(ground) / dt,
            _ => 0.0,
        };
        self.last = Some(ground);
        if speed > MOVING_SPEED {
            self.still = 0.0;
            self.moving = true;
        } else {
            self.still += dt;
            if self.still >= STOP_DELAY {
                self.moving = false;
            }
        }
        self.moving
    }

    pub fn reset(&mut self) {
        *self = MoveWatch::default();
    }
}

/// Seconds between checks that the world VM has the reshape (a reload builds a new VM).
const INSTALL_CHECK_SECS: f32 = 0.5;

pub struct WorldMapPlugin;

impl Plugin for WorldMapPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, drive.in_set(Mw2Set::Hud));
    }
}

/// Reshape the map once per world VM, and tell it when the player moves and the cursor is out;
/// a call into Lua only when either changes.
fn drive(
    state: Res<FpsState>,
    time: Res<Time>,
    objects: Objects,
    transforms: Query<&GlobalTransform>,
    script: Option<NonSend<UiScript>>,
    mut watch: Local<MoveWatch>,
    mut pushed: Local<Option<(bool, bool)>>,
    mut check: Local<f32>,
) {
    let Some(script) = script.filter(|_| crate::capture_keep::in_world(state.in_world, "map")) else {
        watch.reset();
        *pushed = None;
        *check = 0.0;
        return;
    };
    *check -= time.delta_secs();
    if *check <= 0.0 {
        *check = INSTALL_CHECK_SECS;
        if install(&script) {
            *pushed = None;
        }
        // The capture switch shows the map (client/docs/CAPTURE.md).
        if crate::capture_keep::keeps("map") {
            crate::lua::run(&script, "if WorldMapFrame and not WorldMapFrame:IsShown() then ShowUIPanel(WorldMapFrame) end");
        }
    }
    let ground = objects
        .player()
        .and_then(|e| transforms.get(e).ok())
        .map(|g| {
            let w = benilla_assets::coords::bevy_to_wow(g.translation());
            Vec2::new(w[0], w[1])
        });
    let moving = ground.is_some_and(|p| watch.update(p, time.delta_secs()));
    let cursor = !state.owns_input;
    if *pushed != Some((moving, cursor)) {
        *pushed = Some((moving, cursor));
        crate::lua::run(&script, &state_lua(moving, cursor));
    }
}

#[cfg(test)]
mod tests;
