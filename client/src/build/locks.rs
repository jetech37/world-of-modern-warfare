//! Door and portal locks, and the portal's list: three small classic dialogs built from the live
//! UI's own Lua, as an addon builds one (crate::feedback builds its the same way).
//!
//! - **Lock** (the owner, F on their door or portal in build mode): Unlocked, Owner only, Owner
//!   and party, Owner and guild, or a code of 4 to 8 digits, typed into a hidden-text box. Save
//!   sends the build channel's op 4; the server keeps only a salted hash of the code and tells no
//!   other client anything but the mode.
//! - **Keypad** (anyone at a code lock: the server asks with op 19): the digits on buttons or
//!   typed, OK sends op 5. A wrong code says so; too many wrong codes wait a while.
//! - **Portal** (F on a portal; the server's list is op 20): a button for each place it opens on
//!   (the player's faction's capitals, their homestead) and how long the hearthstone's cooldown,
//!   which it shares, still runs. A press sends op 7.
//!
//! Lua to Rust: the dialogs leave requests in `BenillaMw2Locks.out`, taken each frame by [`poll`];
//! Rust opens them and tells them what happened through `BenillaMw2Locks.Open*` and `.Status`.

use benilla_app::fusion::{CHANNEL_BUILD, ChannelMessage, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::Building;
use super::wire::{self, LockMode, PortalMenu};
use crate::Mw2Set;
use crate::lua;
use crate::mode::FpsState;

/// What the build code asks of the dialogs, and what the server said that they show.
#[derive(Clone, Debug, PartialEq)]
pub enum LockEvent {
    /// The owner's lock dialog for a piece: its lock now, and whether it is a portal.
    Settings {
        piece: u32,
        mode: LockMode,
        portal: bool,
    },
    /// The server wants a code for a piece.
    AskCode { piece: u32, portal: bool },
    /// A portal's list.
    Portal(PortalMenu),
    /// The server refused something (a wrong code, too many tries, a cooldown...).
    Refused(u8),
    /// A door swung (a code that worked): its keypad closes.
    Swung(u32),
}

/// A press in a dialog, for the server.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LockRequest {
    Set {
        piece: u32,
        mode: LockMode,
        code: String,
    },
    Code {
        piece: u32,
        code: String,
    },
    Travel {
        piece: u32,
        destination: u8,
    },
}

impl LockRequest {
    /// The build channel message it is.
    pub fn body(&self) -> Vec<u8> {
        match self {
            LockRequest::Set { piece, mode, code } => {
                // A code only goes with a code lock.
                let code = if *mode == LockMode::Code {
                    code.as_str()
                } else {
                    ""
                };
                wire::encode_set_lock(*piece, *mode, code)
            }
            LockRequest::Code { piece, code } => wire::encode_try_code(*piece, code),
            LockRequest::Travel { piece, destination } => {
                wire::encode_portal_travel(*piece, *destination)
            }
        }
    }
}

/// A request as the Lua left it: (kind 1 set, 2 code, 3 travel; piece; a mode or destination;
/// a code). None when it makes no sense.
pub fn request_of(kind: i64, piece: i64, a: i64, code: &str) -> Option<LockRequest> {
    let piece = u32::try_from(piece).ok()?;
    match kind {
        1 => {
            let mode = u8::try_from(a).ok().filter(|m| *m <= 4)?;
            Some(LockRequest::Set {
                piece,
                mode: LockMode::from_u8(mode),
                code: code.to_string(),
            })
        }
        2 => Some(LockRequest::Code {
            piece,
            code: code.to_string(),
        }),
        3 => Some(LockRequest::Travel {
            piece,
            destination: u8::try_from(a).ok()?,
        }),
        _ => None,
    }
}

/// A wait in words, as the server's `Portals::WaitText`: "under a minute", "58 min".
pub fn wait_text(secs: u32) -> String {
    if secs < 60 {
        "under a minute".into()
    } else {
        format!("{} min", secs.div_ceil(60))
    }
}

/// The portal dialog's line over its buttons.
pub fn portal_line(cooldown_secs: u32) -> String {
    if cooldown_secs > 0 {
        format!(
            "It shares your hearthstone's cooldown: ready in {}.",
            wait_text(cooldown_secs)
        )
    } else {
        "Stepping through starts your hearthstone's cooldown (an hour).".into()
    }
}

/// The Lua call that opens the lock dialog.
pub fn open_lock_lua(piece: u32, mode: LockMode, portal: bool) -> String {
    format!(
        "if BenillaMw2Locks and BenillaMw2Locks.OpenLock then BenillaMw2Locks.OpenLock({piece}, {}, {portal}) end",
        mode as u8
    )
}

/// The Lua call that opens the keypad.
pub fn open_keypad_lua(piece: u32, portal: bool) -> String {
    format!(
        "if BenillaMw2Locks and BenillaMw2Locks.OpenKeypad then BenillaMw2Locks.OpenKeypad({piece}, {portal}) end"
    )
}

/// The Lua call that opens a portal's list.
pub fn open_portal_lua(m: &PortalMenu) -> String {
    let ids: Vec<String> = m.choices.iter().map(|(id, _)| id.to_string()).collect();
    let names: Vec<String> = m.choices.iter().map(|(_, n)| lua::quote(n)).collect();
    format!(
        "if BenillaMw2Locks and BenillaMw2Locks.OpenPortal then BenillaMw2Locks.OpenPortal({}, {}, {}, {{{}}}, {{{}}}) end",
        m.piece,
        m.cooldown_secs,
        lua::quote(&portal_line(m.cooldown_secs)),
        ids.join(", "),
        names.join(", ")
    )
}

/// The Lua call that shows a line in the open dialog.
pub fn status_lua(text: &str, good: bool) -> String {
    format!(
        "if BenillaMw2Locks and BenillaMw2Locks.Status then BenillaMw2Locks.Status({}, {good}) end",
        lua::quote(text)
    )
}

/// Take what the dialogs left: (installed, shown, kind, piece, a, code).
pub const POLL_LUA: &str = r#"
local L = BenillaMw2Locks
if not L then return false, false, nil, nil, nil, nil end
local shown = L.Shown and L.Shown() or false
local r = L.out and table.remove(L.out, 1)
if r then return true, shown, r.kind, r.piece, r.a, r.code end
return true, shown, nil, nil, nil, nil
"#;

/// The three dialogs, built once per world VM (Lua 5.0, as the 1.12 client runs it).
pub const INSTALL_LUA: &str = r#"
if BenillaMw2Locks then return end
local L = { out = {}, errors = {}, mode = 2 }
BenillaMw2Locks = L
local MODES = { "Unlocked", "Owner only", "Owner and party", "Owner and guild", "Code" }
local BLURBS = {
  "Anyone may use it.",
  "Only you (and your other characters).",
  "You and your party or raid.",
  "You and your guild.",
  "Anyone who knows the code. Not even the server keeps the code itself.",
}
local MIN_CODE, MAX_CODE = 4, 8

local function make(kind, name, parent, template)
  if template then
    local ok, f = pcall(CreateFrame, kind, name, parent, template)
    if ok and f then return f, true end
  end
  local f = CreateFrame(kind, name, parent)
  if name then setglobal(name, f) end
  return f, false
end

local function font(parent, name, template, layer)
  local fs = parent:CreateFontString(name, layer or "ARTWORK")
  local object = template and getglobal(template)
  if object then fs:SetFontObject(object) end
  return fs
end

local function digits(s)
  return s ~= nil and string.find(s, "^%d+$") ~= nil
end

local function goodCode(s)
  local n = string.len(s or "")
  return digits(s) and n >= MIN_CODE and n <= MAX_CODE
end

-- A classic dialog frame: the dialog box, its header and title, closed by Escape.
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
  -- Dragged by its title and background, kept where it was put, reset by its arrow
  -- (crate::lua::MOVABLE_LUA).
  if BenillaMw2Movable then
    BenillaMw2Movable(d, name, "CENTER", "CENTER", 0, 60)
  end
  pcall(d.SetBackdrop, d, {
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
  })
  if UISpecialFrames then table.insert(UISpecialFrames, name) end
  -- Opens as the stock popups do.
  d:SetScript("OnShow", function() PlaySound("igMainMenuOpen") end)
  local header = d:CreateTexture(name .. "Header", "ARTWORK")
  header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
  header:SetWidth(256)
  header:SetHeight(64)
  header:SetPoint("TOP", d, "TOP", 0, 12)
  local t = font(d, name .. "Title", "GameFontNormal")
  t:SetPoint("TOP", header, "TOP", 0, -14)
  t:SetText(title)
  d.title = t
  local status = font(d, name .. "Status", "GameFontHighlightSmall")
  status:SetWidth(width - 40)
  status:SetHeight(28)
  status:SetPoint("BOTTOM", d, "BOTTOM", 0, 44)
  status:SetText("")
  d.status = status
  return d
end

local function button(parent, name, text, width)
  local b, templated = make("Button", name, parent, "UIPanelButtonTemplate")
  b:SetWidth(width or 100)
  b:SetHeight(22)
  b:SetText(text)
  if not templated and b.SetNormalFontObject then pcall(b.SetNormalFontObject, b, GameFontNormal) end
  return b
end

local function codeBox(name, parent)
  local e, templated = make("EditBox", name, parent, "InputBoxTemplate")
  e:SetWidth(120)
  e:SetHeight(20)
  e:SetAutoFocus(false)
  e:SetMaxLetters(MAX_CODE)
  if e.SetNumeric then e:SetNumeric(true) end
  if e.SetPassword then e:SetPassword(true) end
  if not templated then e:SetFontObject(ChatFontNormal or GameFontHighlight) end
  e:SetScript("OnEscapePressed", function() this:ClearFocus() end)
  return e
end

local function setStatus(d, text, good)
  if not d or not d.status then return end
  d.status:SetText(text or "")
  if good then d.status:SetTextColor(0.1, 1.0, 0.1) else d.status:SetTextColor(1.0, 0.1, 0.1) end
end

-- ── The lock: five choices and a code ────────────────────────────────────────────────────────
local function lockDialog()
  local d = dialog("BenillaMw2LockFrame", 340, 300, "Lock")
  L.lock = d
  local note = font(d, nil, "GameFontHighlightSmall")
  note:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -32)
  note:SetWidth(292)
  note:SetJustifyH("LEFT")
  note:SetText("Who may open it?")
  L.checks = {}
  for i = 1, 5 do
    local id = i - 1
    local c, templated = make("CheckButton", "BenillaMw2LockFrameMode" .. i, d, "UICheckButtonTemplate")
    if not templated then
      c:SetWidth(24)
      c:SetHeight(24)
    end
    c:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -46 - (i - 1) * 26)
    local label = getglobal("BenillaMw2LockFrameMode" .. i .. "Text")
    if not label then label = font(c, "BenillaMw2LockFrameMode" .. i .. "Text", "GameFontHighlight") end
    label:ClearAllPoints()
    label:SetPoint("LEFT", c, "RIGHT", 0, 1)
    label:SetText(MODES[i])
    c:SetScript("OnClick", function()
      PlaySound("igMainMenuOptionCheckBoxOn")
      L.SetMode(id)
    end)
    c:SetScript("OnEnter", function()
      GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
      GameTooltip:SetText(MODES[id + 1], 1.0, 1.0, 1.0)
      GameTooltip:AddLine(BLURBS[id + 1], nil, nil, nil, 1)
      GameTooltip:Show()
    end)
    c:SetScript("OnLeave", function() GameTooltip:Hide() end)
    L.checks[i] = c
  end
  local codeLabel = font(d, nil, "GameFontNormalSmall")
  codeLabel:SetPoint("TOPLEFT", d, "TOPLEFT", 52, -184)
  codeLabel:SetText("Code")
  local e = codeBox("BenillaMw2LockFrameCode", d)
  e:SetPoint("LEFT", codeLabel, "RIGHT", 12, 0)
  e:SetScript("OnEnterPressed", function() L.SaveLock() end)
  L.code = e
  local hint = font(d, nil, "GameFontDisableSmall")
  hint:SetPoint("TOPLEFT", d, "TOPLEFT", 52, -204)
  hint:SetWidth(260)
  hint:SetJustifyH("LEFT")
  L.codeHint = hint
  local save = button(d, "BenillaMw2LockFrameSave", "Save", 100)
  save:SetPoint("BOTTOM", d, "BOTTOM", -56, 18)
  save:SetScript("OnClick", function() L.SaveLock() end)
  local cancel = button(d, "BenillaMw2LockFrameCancel", "Cancel", 100)
  cancel:SetPoint("BOTTOM", d, "BOTTOM", 56, 18)
  cancel:SetScript("OnClick", function()
    PlaySound("igMainMenuOptionCheckBoxOff")
    d:Hide()
  end)
  d:SetScript("OnHide", function() if L.code then L.code:ClearFocus() end end)
end

function L.SetMode(m)
  L.mode = m
  for i = 1, 5 do
    if L.checks and L.checks[i] then L.checks[i]:SetChecked(i - 1 == m and 1 or nil) end
  end
  if not L.code then return end
  if m == 4 then
    L.code:Show()
    if L.current == 4 then
      L.codeHint:SetText("4 to 8 digits. Leave it empty to keep the code it has.")
    else
      L.codeHint:SetText("4 to 8 digits. Tell it to whoever should get in.")
    end
    L.code:SetFocus()
  else
    L.code:SetText("")
    L.code:ClearFocus()
    L.code:Hide()
    L.codeHint:SetText(BLURBS[m + 1])
  end
end

function L.OpenLock(piece, mode, portal)
  if not L.lock then return end
  L.HideAll()
  L.piece = piece
  L.current = mode
  if portal then L.lock.title:SetText("Portal Lock") else L.lock.title:SetText("Door Lock") end
  setStatus(L.lock, "", true)
  L.code:SetText("")
  L.lock:Show()
  L.SetMode(mode)
end

function L.SaveLock()
  local m = L.mode or 2
  local code = ""
  if m == 4 then
    code = L.code:GetText() or ""
    if code == "" and L.current ~= 4 then
      setStatus(L.lock, "Give it a code of 4 to 8 digits.", false)
      L.code:SetFocus()
      return
    end
    if code ~= "" and not goodCode(code) then
      setStatus(L.lock, "A code is 4 to 8 digits.", false)
      L.code:SetFocus()
      return
    end
  end
  PlaySound("igMainMenuOptionCheckBoxOn")
  table.insert(L.out, { kind = 1, piece = L.piece, a = m, code = code })
  L.code:SetText("")
  L.lock:Hide()
end

-- ── The keypad ──────────────────────────────────────────────────────────────────────────────
local function keypad()
  local d = dialog("BenillaMw2KeypadFrame", 236, 296, "Enter Code")
  L.keypad = d
  local e = codeBox("BenillaMw2KeypadFrameCode", d)
  e:SetPoint("TOP", d, "TOP", 4, -34)
  e:SetScript("OnEnterPressed", function() L.SubmitCode() end)
  L.entry = e
  local keys = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "Clear", "0", "OK" }
  for i = 1, 12 do
    local key = keys[i]
    local b = button(d, "BenillaMw2KeypadFrameKey" .. i, key, 52)
    b:SetHeight(28)
    local row = math.floor((i - 1) / 3)
    local col = (i - 1) - row * 3
    b:SetPoint("TOPLEFT", d, "TOPLEFT", 34 + col * 58, -64 - row * 34)
    b:SetScript("OnClick", function()
      PlaySound("igMainMenuOptionCheckBoxOn")
      if key == "Clear" then
        L.entry:SetText("")
      elseif key == "OK" then
        L.SubmitCode()
      else
        local s = L.entry:GetText() or ""
        if string.len(s) < MAX_CODE then L.entry:SetText(s .. key) end
      end
    end)
  end
  local cancel = button(d, "BenillaMw2KeypadFrameCancel", "Cancel", 100)
  cancel:SetPoint("BOTTOM", d, "BOTTOM", 0, 18)
  cancel:SetScript("OnClick", function()
    PlaySound("igMainMenuOptionCheckBoxOff")
    d:Hide()
  end)
  d:SetScript("OnHide", function() if L.entry then L.entry:SetText("") L.entry:ClearFocus() end end)
end

function L.OpenKeypad(piece, portal)
  if not L.keypad then return end
  L.HideAll()
  L.keypadPiece = piece
  if portal then L.keypad.title:SetText("Portal Code") else L.keypad.title:SetText("Door Code") end
  setStatus(L.keypad, "This lock needs its code.", true)
  L.keypad.status:SetTextColor(1.0, 0.82, 0.0)
  L.entry:SetText("")
  L.keypad:Show()
  L.entry:SetFocus()
end

function L.SubmitCode()
  local code = L.entry:GetText() or ""
  if not goodCode(code) then
    setStatus(L.keypad, "A code is 4 to 8 digits.", false)
    return
  end
  table.insert(L.out, { kind = 2, piece = L.keypadPiece, a = 0, code = code })
  L.entry:SetText("")
  L.keypad.status:SetTextColor(1.0, 1.0, 1.0)
  L.keypad.status:SetText("Trying the code...")
end

-- ── The portal's list ───────────────────────────────────────────────────────────────────────
local MAX_DEST = 8
local function portalList()
  local d = dialog("BenillaMw2PortalFrame", 300, 120 + MAX_DEST * 26, "Portal")
  L.portal = d
  local line = font(d, nil, "GameFontHighlightSmall")
  line:SetPoint("TOP", d, "TOP", 0, -34)
  line:SetWidth(256)
  L.portalLine = line
  L.dests = {}
  for i = 1, MAX_DEST do
    local b = button(d, "BenillaMw2PortalFrameDest" .. i, "", 200)
    b:SetPoint("TOP", d, "TOP", 0, -62 - (i - 1) * 26)
    b:Hide()
    b:SetScript("OnClick", function()
      PlaySound("igMainMenuOptionCheckBoxOn")
      table.insert(L.out, { kind = 3, piece = L.portalPiece, a = this.dest or 0, code = "" })
      d:Hide()
    end)
    L.dests[i] = b
  end
  local close = button(d, "BenillaMw2PortalFrameClose", "Close", 100)
  close:SetPoint("BOTTOM", d, "BOTTOM", 0, 18)
  close:SetScript("OnClick", function()
    PlaySound("igMainMenuOptionCheckBoxOff")
    d:Hide()
  end)
end

function L.OpenPortal(piece, cooldown, line, ids, names)
  if not L.portal then return end
  L.HideAll()
  L.portalPiece = piece
  L.portalLine:SetText(line or "")
  local n = table.getn(ids or {})
  for i = 1, MAX_DEST do
    local b = L.dests[i]
    if i <= n then
      b.dest = ids[i]
      b:SetText(names[i] or "?")
      if cooldown and cooldown > 0 then b:Disable() else b:Enable() end
      b:Show()
    else
      b:Hide()
    end
  end
  if n == 0 then L.portalLine:SetText("There is nowhere it can take you.") end
  L.portal:SetHeight(120 + math.max(n, 1) * 26)
  setStatus(L.portal, "", true)
  L.portal:Show()
end

-- ── Shared ──────────────────────────────────────────────────────────────────────────────────
function L.Status(text, good)
  local d
  if L.keypad and L.keypad:IsShown() then d = L.keypad
  elseif L.lock and L.lock:IsShown() then d = L.lock
  elseif L.portal and L.portal:IsShown() then d = L.portal end
  setStatus(d, text, good)
end

function L.HideAll()
  if L.lock then L.lock:Hide() end
  if L.keypad then L.keypad:Hide() end
  if L.portal then L.portal:Hide() end
end

function L.Close(which)
  local d = L[which]
  if d then d:Hide() end
end

function L.KeypadFor()
  if L.keypad and L.keypad:IsShown() then return L.keypadPiece end
  return nil
end

function L.Shown()
  return (L.lock and L.lock:IsShown()) or (L.keypad and L.keypad:IsShown()) or (L.portal and L.portal:IsShown()) or false
end

for _, step in ipairs({ lockDialog, keypad, portalList }) do
  local ok, err = pcall(step)
  if not ok then table.insert(L.errors, tostring(err)) end
end
"#;

const INSTALL_ERRORS_LUA: &str = "local e = BenillaMw2Locks and BenillaMw2Locks.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

/// What the dialogs left this frame.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Polled {
    pub installed: bool,
    pub shown: bool,
    pub request: Option<LockRequest>,
}

/// Install when this VM lacks the dialogs; take what they left.
pub fn poll(script: &UiScript) -> Polled {
    type Row = (
        bool,
        bool,
        Option<i64>,
        Option<i64>,
        Option<i64>,
        mlua::Value,
    );
    let Some((installed, shown, kind, piece, a, code)) = lua::eval::<Row>(script, POLL_LUA) else {
        return Polled::default();
    };
    if !installed {
        lua::install_movable(script);
        lua::run(script, INSTALL_LUA);
        if let Some(Some(e)) = lua::eval::<Option<String>>(script, INSTALL_ERRORS_LUA) {
            warn!("benilla-mw2: locks install: {e}");
        }
    }
    let code = match &code {
        mlua::Value::String(s) => String::from_utf8_lossy(&s.as_bytes()).into_owned(),
        _ => String::new(),
    };
    let request = match (kind, piece) {
        (Some(k), Some(p)) => request_of(k, p, a.unwrap_or(0), &code),
        _ => None,
    };
    Polled {
        installed,
        shown,
        request,
    }
}

/// The dialogs' own state on the Rust side.
#[derive(Resource, Default)]
pub struct LockUi {
    /// The cursor flag was ours to set.
    held_cursor: bool,
    /// A dialog is up (as the last poll saw it).
    pub shown: bool,
}

pub struct LocksPlugin;

impl Plugin for LocksPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<LockUi>()
            .add_systems(Update, drive.in_set(Mw2Set::Hud))
            // After every window's own systems: the cursor stays out while a dialog is up.
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

/// Build the dialogs once per world VM, open them as the build code and the server ask, and send
/// what they ask.
fn drive(
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut build: ResMut<Building>,
    mut ui: ResMut<LockUi>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        build.lock_events.clear();
        ui.shown = false;
        return;
    };
    for event in std::mem::take(&mut build.lock_events) {
        match event {
            LockEvent::Settings {
                piece,
                mode,
                portal,
            } => lua::run(&script, &open_lock_lua(piece, mode, portal)),
            LockEvent::AskCode { piece, portal } => {
                lua::run(&script, &open_keypad_lua(piece, portal))
            }
            LockEvent::Portal(m) => lua::run(&script, &open_portal_lua(&m)),
            LockEvent::Refused(r) => {
                lua::run(&script, &status_lua(wire::refusal_text(r), false));
            }
            LockEvent::Swung(id) => {
                // The code worked: the door swung, so its keypad goes.
                let open = lua::eval::<Option<i64>>(
                    &script,
                    "return BenillaMw2Locks and BenillaMw2Locks.KeypadFor and BenillaMw2Locks.KeypadFor() or nil",
                )
                .flatten();
                if open == Some(i64::from(id)) {
                    lua::run(
                        &script,
                        "if BenillaMw2Locks then BenillaMw2Locks.Close(\"keypad\") end",
                    );
                }
            }
        }
    }
    let polled = poll(&script);
    ui.shown = polled.shown;
    if let Some(r) = polled.request {
        info!("benilla-mw2: lock dialog sends {r:?}");
        if let LockRequest::Code { piece, .. } = &r {
            build.last_lock_piece = Some(*piece);
        }
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_BUILD,
            body: r.body(),
        }));
    }
}

fn hold_cursor(mut ui: ResMut<LockUi>, mut state: ResMut<FpsState>) {
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
    use super::*;

    #[test]
    fn a_dialogs_requests_are_the_servers_messages() {
        let set = request_of(1, 7, 4, "4711").unwrap();
        assert_eq!(
            set.body(),
            vec![wire::OP_SET_LOCK, 7, 0, 0, 0, 4, 4, b'4', b'7', b'1', b'1']
        );
        // A code goes only with a code lock.
        let guild = request_of(1, 7, 3, "4711").unwrap();
        assert_eq!(guild.body(), vec![wire::OP_SET_LOCK, 7, 0, 0, 0, 3, 0]);
        assert_eq!(
            request_of(2, 7, 0, "1234").unwrap().body(),
            wire::encode_try_code(7, "1234")
        );
        assert_eq!(
            request_of(3, 7, 100, "").unwrap().body(),
            wire::encode_portal_travel(7, 100)
        );
        // Nonsense from the Lua is dropped.
        assert!(request_of(1, 7, 9, "").is_none());
        assert!(request_of(4, 7, 0, "").is_none());
        assert!(request_of(1, -1, 2, "").is_none());
        assert!(request_of(3, 7, 300, "").is_none());
    }

    #[test]
    fn the_portal_dialog_says_the_shared_cooldown() {
        assert_eq!(wait_text(30), "under a minute");
        assert_eq!(wait_text(61), "2 min");
        assert!(portal_line(0).contains("starts your hearthstone's cooldown"));
        assert!(portal_line(1500).contains("ready in 25 min"));
        let m = PortalMenu {
            piece: 44,
            cooldown_secs: 0,
            choices: vec![(1, "Stormwind".into()), (100, "My homestead".into())],
        };
        let lua = open_portal_lua(&m);
        assert!(lua.contains("OpenPortal(44, 0, "), "{lua}");
        assert!(
            lua.contains("{1, 100}") && lua.contains("\"Stormwind\""),
            "{lua}"
        );
        assert!(open_lock_lua(5, LockMode::Code, true).contains("OpenLock(5, 4, true)"));
        assert!(open_keypad_lua(5, false).contains("OpenKeypad(5, false)"));
        assert!(status_lua("Wrong code", false).contains("Status(\"Wrong code\", false)"));
    }

    fn installed() -> UiScript {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        let p = poll(&s);
        assert!(!p.installed, "the first poll installs");
        assert_eq!(
            s.eval::<Option<String>>(INSTALL_ERRORS_LUA).unwrap(),
            None,
            "every dialog went in"
        );
        assert!(poll(&s).installed);
        s
    }

    fn text(s: &UiScript, name: &str) -> String {
        s.eval::<Option<String>>(&format!("return {name}:GetText()"))
            .unwrap_or_else(|e| panic!("{name}: {e}"))
            .unwrap_or_default()
    }

    #[test]
    fn the_owner_sets_a_code_lock_in_the_dialog() {
        let s = installed();
        s.run(&open_lock_lua(7, LockMode::Party, false)).unwrap();
        let p = poll(&s);
        assert!(p.shown && p.request.is_none());
        assert_eq!(text(&s, "BenillaMw2LockFrameTitle"), "Door Lock");
        // A code lock wants a code: too short says so and sends nothing.
        s.run("BenillaMw2LockFrameMode5:Click() BenillaMw2LockFrameCode:SetText(\"12\") BenillaMw2LockFrameSave:Click()")
            .unwrap();
        assert!(poll(&s).request.is_none());
        assert!(text(&s, "BenillaMw2LockFrameStatus").contains("4 to 8 digits"));
        s.run("BenillaMw2LockFrameCode:SetText(\"4711\") BenillaMw2LockFrameSave:Click()")
            .unwrap();
        let p = poll(&s);
        assert_eq!(
            p.request,
            Some(LockRequest::Set {
                piece: 7,
                mode: LockMode::Code,
                code: "4711".into()
            })
        );
        assert!(!p.shown, "saving closes it");
        // An owner's guild lock, no code.
        s.run(&open_lock_lua(8, LockMode::Code, true)).unwrap();
        assert_eq!(text(&s, "BenillaMw2LockFrameTitle"), "Portal Lock");
        s.run("BenillaMw2LockFrameMode4:Click() BenillaMw2LockFrameSave:Click()")
            .unwrap();
        assert_eq!(
            poll(&s).request,
            Some(LockRequest::Set {
                piece: 8,
                mode: LockMode::Guild,
                code: String::new()
            })
        );
        // A code lock kept as it is: no new code needed.
        s.run(&open_lock_lua(8, LockMode::Code, true)).unwrap();
        s.run("BenillaMw2LockFrameSave:Click()").unwrap();
        assert_eq!(
            poll(&s).request,
            Some(LockRequest::Set {
                piece: 8,
                mode: LockMode::Code,
                code: String::new()
            })
        );
    }

    #[test]
    fn the_keypad_sends_the_digits_pressed() {
        let s = installed();
        s.run(&open_keypad_lua(9, false)).unwrap();
        assert!(poll(&s).shown);
        // 4, 7, 1, 1 then OK (keys 4, 7, 1, 1 and 12).
        s.run(
            "BenillaMw2KeypadFrameKey4:Click() BenillaMw2KeypadFrameKey7:Click() \
             BenillaMw2KeypadFrameKey1:Click() BenillaMw2KeypadFrameKey1:Click() \
             BenillaMw2KeypadFrameKey12:Click()",
        )
        .unwrap();
        assert_eq!(
            poll(&s).request,
            Some(LockRequest::Code {
                piece: 9,
                code: "4711".into()
            })
        );
        // The server says no: the keypad says so and stays.
        s.run(&status_lua(
            wire::refusal_text(wire::REFUSED_WRONG_CODE),
            false,
        ))
        .unwrap();
        assert_eq!(text(&s, "BenillaMw2KeypadFrameStatus"), "Wrong code");
        assert!(poll(&s).shown);
        // C clears; too short sends nothing.
        s.run("BenillaMw2KeypadFrameKey1:Click() BenillaMw2KeypadFrameKey10:Click() BenillaMw2KeypadFrameKey12:Click()")
            .unwrap();
        assert!(poll(&s).request.is_none());
        s.run("BenillaMw2Locks.Close(\"keypad\")").unwrap();
        assert!(!poll(&s).shown);
    }

    #[test]
    fn the_portal_lists_its_places_and_greys_them_while_the_hearth_cools() {
        let s = installed();
        let m = PortalMenu {
            piece: 44,
            cooldown_secs: 0,
            choices: vec![(4, "Orgrimmar".into()), (100, "My homestead".into())],
        };
        s.run(&open_portal_lua(&m)).unwrap();
        assert!(poll(&s).shown);
        assert_eq!(text(&s, "BenillaMw2PortalFrameDest1"), "Orgrimmar");
        assert_eq!(text(&s, "BenillaMw2PortalFrameDest2"), "My homestead");
        s.run("BenillaMw2PortalFrameDest2:Click()").unwrap();
        let p = poll(&s);
        assert_eq!(
            p.request,
            Some(LockRequest::Travel {
                piece: 44,
                destination: 100
            })
        );
        assert!(!p.shown);
        // Cooling down: the buttons wait.
        let cooling = PortalMenu {
            cooldown_secs: 600,
            ..m
        };
        s.run(&open_portal_lua(&cooling)).unwrap();
        let enabled = s
            .eval::<bool>(
                "local e = BenillaMw2PortalFrameDest1:IsEnabled() return e == 1 or e == true",
            )
            .unwrap();
        assert!(!enabled);
    }

    #[test]
    fn the_dialogs_lua_names_every_mode_and_the_code_rules() {
        for m in LockMode::ALL {
            assert!(INSTALL_LUA.contains(m.name()), "{}", m.name());
        }
        assert!(INSTALL_LUA.contains("MIN_CODE, MAX_CODE = 4, 8"));
        assert_eq!((wire::CODE_MIN, wire::CODE_MAX), (4, 8));
        // Lua 5.0: no `#` length, no `%`.
        assert!(!INSTALL_LUA.contains('#') && !INSTALL_LUA.contains(" % "));
    }
}
