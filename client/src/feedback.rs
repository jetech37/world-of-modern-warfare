//! Bug reports and suggestions from the game: the Escape menu's **Bugs / Feedback** rung
//! (under Support) opens a classic dialog, built from the live UI's own Lua as an addon
//! builds one: a type (Bug, Suggestion, Other), a title line, a description of up to 1000
//! characters, Submit and Cancel.
//!
//! A report goes up on fusion channel 9, its own (the server's `fusion/Feedback.h`): op 1 the
//! report, op 2 a question (may a report be sent now?), and op 1 back the answer. The server
//! holds each account to its limits (one per 10 minutes, five a day by default) and adds where the
//! player stands; the client only greys its button with the countdown the server gave.
//!
//! Lua to Rust: the dialog leaves its report in `BenillaMw2Feedback.out` and a question in
//! `BenillaMw2Feedback.ask`, which [`poll`] takes each frame; the answers go back through
//! `BenillaMw2Feedback.Reply`.

use std::collections::VecDeque;

use benilla_app::BuildId;
use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::lua;
use crate::mode::FpsState;

/// The reports' own fusion channel (channels 1-8 are the build, world, achievements, companion,
/// downed, stats, classes and prestige channels).
pub const CHANNEL_FEEDBACK: u8 = 9;
/// Op 1, client to server: a report. Server to client: the answer.
pub const OP_SUBMIT: u8 = 1;
pub const OP_REPLY: u8 = 1;
/// Op 2, client to server: may a report be sent now?
pub const OP_ASK: u8 = 2;
/// The answer's size: `[1, result u8, wait u32, id u32, left today u8]`.
pub const REPLY_SIZE: usize = 11;

/// What a report may hold, in characters, as the server cleans it (`Feedback.h`).
pub const MAX_TITLE_CHARS: usize = 80;
pub const MIN_TITLE_CHARS: usize = 3;
pub const MAX_BODY_CHARS: usize = 1000;
pub const MAX_VERSION_CHARS: usize = 64;
/// The wire's limits in bytes: a title's length is one byte, a body's two (at most 4096).
const MAX_TITLE_BYTES: usize = 255;
const MAX_BODY_BYTES: usize = 4096;

/// The environment variable the launcher sets to the published version it installed.
pub const VERSION_ENV: &str = "WOMW_VERSION";

/// A report's type, the server's `Kind`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Bug = 1,
    Suggestion = 2,
    Other = 3,
}

impl Kind {
    pub fn from_u8(k: u8) -> Option<Kind> {
        match k {
            1 => Some(Kind::Bug),
            2 => Some(Kind::Suggestion),
            3 => Some(Kind::Other),
            _ => None,
        }
    }
}

/// The server's answer, its `Result`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    Sent = 0,
    Ready = 1,
    Cooldown = 2,
    DailyLimit = 3,
    Disabled = 4,
    Invalid = 5,
    Failed = 6,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Reply {
    pub outcome: Outcome,
    pub wait_secs: u32,
    pub id: u32,
    pub left_today: u8,
}

/// `s` cut to `max` characters.
pub fn clip_chars(s: &str, max: usize) -> &str {
    match s.char_indices().nth(max) {
        Some((at, _)) => &s[..at],
        None => s,
    }
}

/// `s` cut to at most `max` bytes, on a character boundary.
fn clip_bytes(s: &str, max: usize) -> &str {
    if s.len() <= max {
        return s;
    }
    let mut end = max;
    while !s.is_char_boundary(end) {
        end -= 1;
    }
    &s[..end]
}

/// Op 1: `[1, kind u8, title_len u8, title, body_len u16, body, version_len u8, version]`. Each
/// field is cut to what the server keeps (the server cleans and cuts again).
pub fn encode_submit(kind: Kind, title: &str, body: &str, version: &str) -> Vec<u8> {
    let title = clip_bytes(clip_chars(title, MAX_TITLE_CHARS), MAX_TITLE_BYTES);
    let body = clip_bytes(clip_chars(body, MAX_BODY_CHARS), MAX_BODY_BYTES);
    let version = clip_bytes(clip_chars(version, MAX_VERSION_CHARS), 255);
    let mut out = Vec::with_capacity(6 + title.len() + body.len() + version.len());
    out.push(OP_SUBMIT);
    out.push(kind as u8);
    out.push(title.len() as u8);
    out.extend_from_slice(title.as_bytes());
    out.extend_from_slice(&(body.len() as u16).to_le_bytes());
    out.extend_from_slice(body.as_bytes());
    out.push(version.len() as u8);
    out.extend_from_slice(version.as_bytes());
    out
}

/// Op 2: the question.
pub fn encode_ask() -> Vec<u8> {
    vec![OP_ASK]
}

/// Op 1 back: `[1, result u8, wait u32, id u32, left today u8]`.
pub fn decode_reply(body: &[u8]) -> Option<Reply> {
    if body.len() != REPLY_SIZE || body[0] != OP_REPLY {
        return None;
    }
    let outcome = match body[1] {
        0 => Outcome::Sent,
        1 => Outcome::Ready,
        2 => Outcome::Cooldown,
        3 => Outcome::DailyLimit,
        4 => Outcome::Disabled,
        5 => Outcome::Invalid,
        6 => Outcome::Failed,
        _ => return None,
    };
    Some(Reply {
        outcome,
        wait_secs: u32::from_le_bytes(body[2..6].try_into().ok()?),
        id: u32::from_le_bytes(body[6..10].try_into().ok()?),
        left_today: body[10],
    })
}

/// A wait in words: "less than a minute", "1 minute", "7 minutes", "about 3 hours".
pub fn wait_text(secs: u32) -> String {
    if secs < 60 {
        return "less than a minute".into();
    }
    let minutes = secs.div_ceil(60);
    if minutes < 90 {
        return format!("{minutes} minute{}", if minutes == 1 { "" } else { "s" });
    }
    let hours = (minutes + 30) / 60;
    format!("about {hours} hour{}", if hours == 1 { "" } else { "s" })
}

/// What the dialog says of an answer: the line, and whether it is good news. `None`: nothing
/// to say (a report may be sent).
pub fn reply_message(r: &Reply) -> Option<(String, bool)> {
    let wait = wait_text(r.wait_secs);
    Some(match r.outcome {
        Outcome::Sent => (format!("Thanks, your report (#{}) was sent.", r.id), true),
        Outcome::Ready => return None,
        Outcome::Cooldown => (format!("You can send another report in {wait}."), false),
        Outcome::DailyLimit => (
            format!("That's all the reports for today. You can send another in {wait}."),
            false,
        ),
        Outcome::Disabled => ("This realm isn't taking reports right now.".into(), false),
        Outcome::Invalid => (
            format!("Please give the report a title (at least {MIN_TITLE_CHARS} characters)."),
            false,
        ),
        Outcome::Failed => (
            "The report couldn't be kept. Please try again later.".into(),
            false,
        ),
    })
}

/// The version a report carries: the launcher's published version ([`VERSION_ENV`]), else this
/// build's own (`0.2.0 b17be27`). Printable ASCII only, at most 64 characters.
pub fn client_version(published: Option<&str>, build: Option<&BuildId>) -> String {
    let clean = |s: &str| -> String {
        let s: String = s
            .chars()
            .filter(|c| c.is_ascii_graphic() || *c == ' ')
            .take(MAX_VERSION_CHARS)
            .collect();
        s.trim().to_string()
    };
    if let Some(v) = published.map(clean).filter(|v| !v.is_empty()) {
        return v;
    }
    let own = build
        .map(|b| clean(&format!("{} {}", b.version, b.short)))
        .unwrap_or_default();
    if own.is_empty() { "unknown".into() } else { own }
}

// ── The dialog, in the live UI's Lua ─────────────────────────────────────────────────────────────

/// Take what the dialog left: (installed, ask, kind, title, body).
pub const POLL_LUA: &str = r#"
local F = BenillaMw2Feedback
if not F then return false, false, nil, nil, nil end
local ask, out = F.ask, F.out
F.ask = nil
F.out = nil
if out then return true, ask and true or false, out.kind, out.title, out.body end
return true, ask and true or false, nil, nil, nil
"#;

/// The rung and the dialog, built once per world VM (Lua 5.0, as the 1.12 client runs it).
pub const INSTALL_LUA: &str = r#"
if BenillaMw2Feedback or not GameMenuFrame or not BenillaGameMenuButtonSupport then return end
local F = { kind = 1, readyAt = 0, errors = {} }
BenillaMw2Feedback = F
local LABEL = "Bugs / Feedback"
local MAX_TITLE, MIN_TITLE, MAX_BODY = 80, 3, 1000
local KINDS = { "Bug", "Suggestion", "Other" }

-- A frame of a stock template, or a plain one where the template is missing (a test kit).
-- A refused template may leave its half-made frame under the name: the plain one takes it.
local function make(kind, name, parent, template)
  if template then
    local ok, f = pcall(CreateFrame, kind, name, parent, template)
    if ok and f then return f, true end
  end
  local f = CreateFrame(kind, name, parent)
  if name then setglobal(name, f) end
  return f, false
end

-- A font string in a stock font object, when the UI has it.
local function font(parent, name, template, layer)
  local fs = parent:CreateFontString(name, layer or "ARTWORK")
  local object = template and getglobal(template)
  if object then fs:SetFontObject(object) end
  return fs
end

local function chars(s)
  local _, n = string.gsub(s or "", "[^\128-\191]", "")
  return n
end

local function trim(s)
  s = string.gsub(s or "", "^%s+", "")
  s = string.gsub(s, "%s+$", "")
  return s
end

local function now()
  return GetTime and GetTime() or 0
end

-- Seconds until a report may go, from the server's last answer.
function F.WaitLeft()
  local left = (F.readyAt or 0) - now()
  if left > 0 then return left end
  return 0
end

local function clock(secs)
  local s = math.ceil(secs)
  local m = math.floor(s / 60)
  return string.format("%d:%02d", m, s - m * 60)
end

-- ── The Escape menu's rung, under Support ───────────────────────────────────────────
local function menu_rung()
  local support = BenillaGameMenuButtonSupport
  local b, templated = make("Button", "BenillaMw2GameMenuButtonFeedback", GameMenuFrame, "GameMenuButtonTemplate")
  if not templated then
    b:SetWidth(144)
    b:SetHeight(21)
  end
  b:SetPoint("TOP", support, "BOTTOM", 0, 0)
  b:SetText(LABEL)
  if GameMenuButtonMacros then
    GameMenuButtonMacros:ClearAllPoints()
    GameMenuButtonMacros:SetPoint("TOP", b, "BOTTOM", 0, 0)
  end
  GameMenuFrame:SetHeight(GameMenuFrame:GetHeight() + b:GetHeight())
  b:SetScript("OnClick", function()
    PlaySound("igMainMenuOption")
    HideUIPanel(GameMenuFrame)
    F.Open()
  end)
  b:SetScript("OnEnter", function()
    GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
    GameTooltip:SetText(LABEL, 1.0, 1.0, 1.0)
    GameTooltip:AddLine("Tell us about a bug, or suggest something. Where you are and what you target go with it.", nil, nil, nil, 1)
    GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  F.rung = b
  -- Each time the menu opens, ask the server where the account stands.
  local Ref_OnShow = GameMenuFrame:GetScript("OnShow")
  GameMenuFrame:SetScript("OnShow", function()
    if Ref_OnShow then Ref_OnShow() end
    F.ask = true
    F.Refresh()
  end)
  b:SetScript("OnUpdate", function()
    F.tick = (F.tick or 0) + 1
    if F.tick >= 10 then
      F.tick = 0
      F.Refresh()
    end
  end)
end

-- ── The dialog ───────────────────────────────────────────────────────────────────────────────
local function dialog()
  local d = CreateFrame("Frame", "BenillaMw2FeedbackFrame", UIParent)
  d:Hide()
  d:SetWidth(384)
  d:SetHeight(372)
  d:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
  d:SetFrameStrata("DIALOG")
  d:EnableMouse(true)
  if d.SetToplevel then d:SetToplevel(true) end
  if d.SetClampedToScreen then d:SetClampedToScreen(true) end
  -- Dragged by its title and background, kept where it was put, reset by its arrow
  -- (crate::lua::MOVABLE_LUA).
  if BenillaMw2Movable then
    BenillaMw2Movable(d, "feedback", "CENTER", "CENTER", 0, 40)
  end
  pcall(d.SetBackdrop, d, {
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
  })
  if UISpecialFrames then table.insert(UISpecialFrames, "BenillaMw2FeedbackFrame") end

  local header = d:CreateTexture("BenillaMw2FeedbackFrameHeader", "ARTWORK")
  header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
  header:SetWidth(320)
  header:SetHeight(64)
  header:SetPoint("TOP", d, "TOP", 0, 12)
  local title = font(d, "BenillaMw2FeedbackFrameTitle", "GameFontNormal")
  title:SetPoint("TOP", header, "TOP", 0, -14)
  title:SetText("Bugs / Feedback")

  -- The type: three check buttons that act as one choice.
  local typeLabel = font(d, nil, "GameFontNormal")
  typeLabel:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -36)
  typeLabel:SetText("Type")
  F.checks = {}
  local prev
  for i = 1, 3 do
    local id = i
    local c, templated = make("CheckButton", "BenillaMw2FeedbackFrameKind" .. i, d, "UICheckButtonTemplate")
    if not templated then
      c:SetWidth(24)
      c:SetHeight(24)
    end
    if prev then
      c:SetPoint("LEFT", prev, "LEFT", 104, 0)
    else
      c:SetPoint("TOPLEFT", d, "TOPLEFT", 64, -30)
    end
    local label = getglobal("BenillaMw2FeedbackFrameKind" .. i .. "Text")
    if not label then label = font(c, "BenillaMw2FeedbackFrameKind" .. i .. "Text", "GameFontHighlight") end
    label:ClearAllPoints()
    label:SetPoint("LEFT", c, "RIGHT", 0, 1)
    label:SetText(KINDS[i])
    c:SetScript("OnClick", function()
      PlaySound("igMainMenuOptionCheckBoxOn")
      F.SetKind(id)
    end)
    F.checks[i] = c
    prev = c
  end

  -- The title line.
  local titleLabel = font(d, nil, "GameFontNormal")
  titleLabel:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -66)
  titleLabel:SetText("Title")
  local t, templated = make("EditBox", "BenillaMw2FeedbackFrameTitleBox", d, "InputBoxTemplate")
  t:SetWidth(326)
  t:SetHeight(20)
  t:SetPoint("TOPLEFT", d, "TOPLEFT", 32, -82)
  t:SetAutoFocus(false)
  t:SetMaxLetters(MAX_TITLE)
  if not templated then t:SetFontObject(ChatFontNormal or GameFontHighlight) end
  t:SetScript("OnEscapePressed", function() this:ClearFocus() end)
  t:SetScript("OnTabPressed", function() F.body:SetFocus() end)
  t:SetScript("OnEnterPressed", function() F.body:SetFocus() end)
  F.title = t

  -- The description, in a dark inset that scrolls.
  local bodyLabel = font(d, nil, "GameFontNormal")
  bodyLabel:SetPoint("TOPLEFT", d, "TOPLEFT", 24, -112)
  bodyLabel:SetText("Description")
  local counter = font(d, "BenillaMw2FeedbackFrameCounter", "GameFontHighlightSmall")
  counter:SetPoint("TOPRIGHT", d, "TOPRIGHT", -26, -114)
  counter:SetText("0 / " .. MAX_BODY)
  F.counter = counter

  local inset = CreateFrame("Frame", "BenillaMw2FeedbackFrameInset", d)
  inset:SetPoint("TOPLEFT", d, "TOPLEFT", 20, -128)
  inset:SetPoint("BOTTOMRIGHT", d, "BOTTOMRIGHT", -20, 74)
  pcall(inset.SetBackdrop, inset, {
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
  })
  if inset.SetBackdropColor then pcall(inset.SetBackdropColor, inset, 0, 0, 0, 0.8) end
  inset:EnableMouse(true)
  inset:SetScript("OnMouseDown", function() F.body:SetFocus() end)

  local scroll, scrolled = make("ScrollFrame", "BenillaMw2FeedbackFrameScroll", inset, "UIPanelScrollFrameTemplate")
  scroll:SetPoint("TOPLEFT", inset, "TOPLEFT", 8, -8)
  scroll:SetPoint("BOTTOMRIGHT", inset, "BOTTOMRIGHT", -28, 8)
  local b = CreateFrame("EditBox", "BenillaMw2FeedbackFrameBodyBox", scrolled and scroll or inset)
  b:SetMultiLine(true)
  b:SetAutoFocus(false)
  b:SetMaxLetters(MAX_BODY)
  b:SetWidth(300)
  b:SetHeight(140)
  b:SetFontObject(ChatFontNormal or GameFontHighlight)
  if scrolled then
    scroll:SetScrollChild(b)
  else
    b:SetPoint("TOPLEFT", inset, "TOPLEFT", 8, -8)
  end
  b:SetScript("OnEscapePressed", function() this:ClearFocus() end)
  b:SetScript("OnTabPressed", function() F.title:SetFocus() end)
  b:SetScript("OnTextChanged", function()
    F.counter:SetText(chars(this:GetText()) .. " / " .. MAX_BODY)
    F.follow = true
  end)
  if scrolled then
    -- The caret's row (arg2 is its top, 0 or below, arg4 its height) stays in view: the
    -- stock ScrollingEdit_* functions do this in later clients.
    b:SetScript("OnCursorChanged", function()
      F.caretY = -(arg2 or 0)
      F.caretH = arg4 or 0
      F.follow = true
    end)
    b:SetScript("OnUpdate", function()
      if not F.follow then return end
      F.follow = false
      pcall(function()
        scroll:UpdateScrollChildRect()
        local top = scroll:GetVerticalScroll()
        local height = scroll:GetHeight()
        local y, h = F.caretY or 0, F.caretH or 0
        if y < top then
          scroll:SetVerticalScroll(y)
        elseif y + h > top + height then
          scroll:SetVerticalScroll(y + h - height)
        end
      end)
    end)
  end
  F.body = b

  -- The answer line, and the buttons.
  local status = font(d, "BenillaMw2FeedbackFrameStatus", "GameFontHighlightSmall")
  status:SetWidth(336)
  status:SetHeight(28)
  status:SetPoint("BOTTOM", d, "BOTTOM", 0, 44)
  status:SetText("")
  F.status = status

  local function button(name, text, x)
    local btn = make("Button", name, d, "UIPanelButtonTemplate")
    btn:SetWidth(110)
    btn:SetHeight(22)
    btn:SetPoint("BOTTOM", d, "BOTTOM", x, 20)
    btn:SetText(text)
    return btn
  end
  F.submit = button("BenillaMw2FeedbackFrameSubmit", "Submit", -60)
  F.cancel = button("BenillaMw2FeedbackFrameCancel", "Cancel", 60)
  F.submit:SetScript("OnClick", function() F.Submit() end)
  F.cancel:SetScript("OnClick", function()
    PlaySound("igMainMenuOptionCheckBoxOff")
    F.Close()
  end)

  d:SetScript("OnUpdate", function()
    -- No answer within 15 seconds: the report did not get through.
    if F.sending and now() - (F.sentAt or 0) > 15 then
      F.sending = false
      F.SetStatus("No answer from the server. Please try again later.", false)
      F.Refresh()
    end
    F.tickDialog = (F.tickDialog or 0) + 1
    if F.tickDialog >= 10 then
      F.tickDialog = 0
      F.Refresh()
    end
  end)
  d:SetScript("OnShow", function() PlaySound("igMainMenuOpen") end)
  d:SetScript("OnHide", function()
    if F.title then F.title:ClearFocus() end
    if F.body then F.body:ClearFocus() end
  end)
  F.frame = d
end

function F.SetKind(id)
  F.kind = id
  for i = 1, 3 do
    if F.checks and F.checks[i] then F.checks[i]:SetChecked(i == id and 1 or nil) end
  end
end

function F.SetStatus(text, good)
  if not F.status then return end
  F.status:SetText(text or "")
  if good then
    F.status:SetTextColor(0.1, 1.0, 0.1)
  else
    F.status:SetTextColor(1.0, 0.1, 0.1)
  end
end

-- The rung's caption and the Submit button follow the countdown.
function F.Refresh()
  local left = F.WaitLeft()
  if F.rung then
    if left > 0 then
      F.rung:SetText("Next report in " .. clock(left))
      F.rung:Disable()
    else
      F.rung:SetText(LABEL)
      F.rung:Enable()
    end
  end
  if F.submit then
    if F.sending or F.closed or left > 0 then F.submit:Disable() else F.submit:Enable() end
  end
end

function F.Open()
  if not F.frame then return end
  F.sent = false
  F.sending = false
  F.SetStatus("", true)
  F.cancel:SetText("Cancel")
  F.SetKind(F.kind or 1)
  F.frame:Show()
  F.title:SetFocus()
  F.ask = true
  F.Refresh()
end

function F.Close()
  if F.frame then F.frame:Hide() end
end

function F.Submit()
  if F.sending then return end
  local title = trim(F.title:GetText())
  if chars(title) < MIN_TITLE then
    F.SetStatus("Please give the report a title (at least " .. MIN_TITLE .. " characters).", false)
    F.title:SetFocus()
    return
  end
  if F.WaitLeft() > 0 then return end
  PlaySound("igMainMenuOptionCheckBoxOn")
  F.out = { kind = F.kind or 1, title = title, body = F.body:GetText() or "" }
  F.sending = true
  F.sentAt = now()
  F.status:SetTextColor(1.0, 1.0, 1.0)
  F.status:SetText("Sending...")
  F.title:ClearFocus()
  F.body:ClearFocus()
  F.Refresh()
end

-- The server's answer (crate::feedback): `which` "submit" or "ask"; `message` "" when there is
-- nothing to say; `wait` seconds until a report may go; `closed` the realm takes none.
function F.Reply(which, good, message, wait, closed)
  F.closed = closed and true or false
  if wait and wait > 0 then F.readyAt = now() + wait else F.readyAt = 0 end
  if which == "submit" then
    F.sending = false
    F.SetStatus(message, good)
    if good then
      F.sent = true
      F.title:SetText("")
      F.body:SetText("")
      F.cancel:SetText("Close")
      if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(message, 0.1, 1.0, 0.1) end
    end
  elseif not F.sent and not F.sending then
    -- A question's answer: say why a report can't go now, or clear an old reason.
    F.SetStatus(message, false)
  end
  F.Refresh()
end

for _, step in ipairs({ menu_rung, dialog }) do
  local ok, err = pcall(step)
  if not ok then table.insert(F.errors, tostring(err)) end
end
"#;

/// What the install's steps raised, `\n`-joined (nothing when all went in).
const INSTALL_ERRORS_LUA: &str = "local e = BenillaMw2Feedback and BenillaMw2Feedback.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

/// What a frame of the dialog asked for.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Polled {
    pub installed: bool,
    pub ask: bool,
    pub report: Option<(Kind, String, String)>,
}

/// Install when this VM lacks the dialog; take what it left.
pub fn poll(script: &UiScript) -> Polled {
    type Row = (bool, bool, Option<i64>, mlua::Value, mlua::Value);
    let Some((installed, ask, kind, title, body)) = lua::eval::<Row>(script, POLL_LUA) else {
        return Polled::default();
    };
    if !installed {
        lua::install_movable(script);
        lua::run(script, INSTALL_LUA);
        if let Some(Some(e)) = lua::eval::<Option<String>>(script, INSTALL_ERRORS_LUA) {
            warn!("benilla-mw2: feedback install: {e}");
        }
    }
    // Lua strings are bytes: anything not UTF-8 is replaced, and the server cleans the rest.
    let text = |v: &mlua::Value| match v {
        mlua::Value::String(s) => String::from_utf8_lossy(&s.as_bytes()).into_owned(),
        _ => String::new(),
    };
    let report = kind
        .and_then(|k| u8::try_from(k).ok())
        .and_then(Kind::from_u8)
        .map(|k| (k, text(&title), text(&body)));
    Polled {
        installed,
        ask,
        report,
    }
}

/// Which request an answer belongs to: the server answers in order.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Asked {
    Submit,
    Ask,
}

/// The requests in flight, oldest first.
#[derive(Resource, Default)]
pub struct Pending(pub VecDeque<Asked>);

/// The Lua call that hands an answer to the dialog.
pub fn reply_lua(which: Asked, r: &Reply) -> String {
    let (message, good) = reply_message(r).unwrap_or_default();
    let wait = match r.outcome {
        Outcome::Cooldown | Outcome::DailyLimit => r.wait_secs,
        _ => 0,
    };
    format!(
        "if BenillaMw2Feedback and BenillaMw2Feedback.Reply then BenillaMw2Feedback.Reply({}, {}, {}, {}, {}) end",
        lua::quote(match which {
            Asked::Submit => "submit",
            Asked::Ask => "ask",
        }),
        good,
        lua::quote(&message),
        wait,
        r.outcome == Outcome::Disabled,
    )
}

pub struct FeedbackPlugin;

impl Plugin for FeedbackPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Pending>()
            .add_systems(Update, drive.in_set(Mw2Set::Mode));
    }
}

/// Build the dialog once per world VM, send what it asks, and hand it the server's answers.
fn drive(
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    build: Option<Res<BuildId>>,
    mut pending: ResMut<Pending>,
    mut send: MessageWriter<SendFusionChannel>,
    mut received: MessageReader<FusionChannelReceived>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        received.clear();
        pending.0.clear();
        return;
    };
    for FusionChannelReceived(m) in received.read() {
        if m.channel != CHANNEL_FEEDBACK {
            continue;
        }
        let Some(r) = decode_reply(&m.body) else {
            continue;
        };
        let which = pending.0.pop_front().unwrap_or(Asked::Ask);
        info!("benilla-mw2: feedback {which:?} answered {r:?}");
        lua::run(&script, &reply_lua(which, &r));
        // After a report, ask again: the answer starts the countdown.
        if which == Asked::Submit && r.outcome == Outcome::Sent {
            pending.0.push_back(Asked::Ask);
            send.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL_FEEDBACK,
                body: encode_ask(),
            }));
        }
    }
    let polled = poll(&script);
    if polled.ask {
        pending.0.push_back(Asked::Ask);
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_FEEDBACK,
            body: encode_ask(),
        }));
    }
    if let Some((kind, title, body)) = polled.report {
        let published = std::env::var(VERSION_ENV).ok();
        let version = client_version(published.as_deref(), build.as_deref());
        info!("benilla-mw2: feedback sends a {kind:?} report");
        pending.0.push_back(Asked::Submit);
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_FEEDBACK,
            body: encode_submit(kind, &title, &body, &version),
        }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_report_is_the_servers_bytes() {
        // The same bytes as the server's test (FeedbackTests.cpp).
        assert_eq!(
            encode_submit(Kind::Suggestion, "Hi", "a\nb", "v1"),
            vec![1, 2, 2, b'H', b'i', 3, 0, b'a', b'\n', b'b', 2, b'v', b'1']
        );
        assert_eq!(encode_ask(), vec![2]);
    }

    #[test]
    fn a_report_is_cut_to_what_the_server_keeps() {
        let title = "t".repeat(200);
        let body = "é".repeat(1500);
        let bytes = encode_submit(Kind::Bug, &title, &body, &"v".repeat(300));
        assert_eq!(bytes[2] as usize, MAX_TITLE_CHARS);
        let body_len = u16::from_le_bytes([bytes[3 + 80], bytes[4 + 80]]) as usize;
        assert_eq!(body_len, MAX_BODY_CHARS * 2, "1000 characters of two bytes");
        let version_at = 5 + 80 + body_len;
        assert_eq!(bytes[version_at] as usize, MAX_VERSION_CHARS);
        assert_eq!(bytes.len(), version_at + 1 + MAX_VERSION_CHARS);
        // Four-byte characters: 1000 of them still fit the wire's 4096.
        let wide = encode_submit(Kind::Other, "abc", &"😀".repeat(1200), "");
        assert_eq!(u16::from_le_bytes([wide[6], wide[7]]), 4000);
        assert_eq!(clip_chars("héllo", 2), "hé");
        assert_eq!(clip_chars("hi", 5), "hi");
    }

    #[test]
    fn the_servers_answer_reads() {
        // The same bytes as the server's test: a cooldown of 300 s, report 7, 3 left today.
        let r = decode_reply(&[1, 2, 44, 1, 0, 0, 7, 0, 0, 0, 3]).expect("an answer");
        assert_eq!(
            r,
            Reply {
                outcome: Outcome::Cooldown,
                wait_secs: 300,
                id: 7,
                left_today: 3
            }
        );
        assert!(decode_reply(&[1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0]).is_none(), "unknown result");
        assert!(decode_reply(&[1, 0, 0, 0, 0, 0, 0, 0, 0, 0]).is_none(), "short");
        assert!(decode_reply(&[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]).is_none(), "another op");
    }

    #[test]
    fn waits_read_as_words() {
        assert_eq!(wait_text(0), "less than a minute");
        assert_eq!(wait_text(59), "less than a minute");
        assert_eq!(wait_text(60), "1 minute");
        assert_eq!(wait_text(61), "2 minutes");
        assert_eq!(wait_text(599), "10 minutes");
        assert_eq!(wait_text(89 * 60), "89 minutes");
        assert_eq!(wait_text(90 * 60), "about 2 hours");
        assert_eq!(wait_text(86_400 - 5000), "about 23 hours");
    }

    #[test]
    fn each_answer_has_its_line() {
        let r = |outcome, wait_secs| Reply {
            outcome,
            wait_secs,
            id: 12,
            left_today: 4,
        };
        assert_eq!(
            reply_message(&r(Outcome::Sent, 0)),
            Some(("Thanks, your report (#12) was sent.".into(), true))
        );
        assert_eq!(reply_message(&r(Outcome::Ready, 0)), None);
        assert_eq!(
            reply_message(&r(Outcome::Cooldown, 420)),
            Some(("You can send another report in 7 minutes.".into(), false))
        );
        let (daily, good) = reply_message(&r(Outcome::DailyLimit, 7200)).unwrap();
        assert!(!good && daily.contains("about 2 hours"), "{daily}");
        for o in [Outcome::Disabled, Outcome::Invalid, Outcome::Failed] {
            let (line, good) = reply_message(&r(o, 0)).unwrap();
            assert!(!good && !line.is_empty());
        }
    }

    #[test]
    fn the_version_is_the_launchers_else_the_builds() {
        let build = BuildId {
            version: "0.2.0",
            short: "b17be27",
            ..Default::default()
        };
        assert_eq!(client_version(Some("2026-10-18 12:00"), Some(&build)), "2026-10-18 12:00");
        assert_eq!(client_version(Some("  "), Some(&build)), "0.2.0 b17be27");
        assert_eq!(client_version(None, Some(&build)), "0.2.0 b17be27");
        assert_eq!(client_version(None, None), "unknown");
        // Control characters never reach the report.
        assert_eq!(client_version(Some("v1\n\x1b[2J"), None), "v1[2J");
        assert_eq!(client_version(Some(&"9".repeat(100)), None).len(), MAX_VERSION_CHARS);
    }

    #[test]
    fn an_answer_reaches_the_dialog_as_a_lua_call() {
        let sent = Reply {
            outcome: Outcome::Sent,
            wait_secs: 0,
            id: 3,
            left_today: 4,
        };
        let call = reply_lua(Asked::Submit, &sent);
        assert!(call.contains(r#"Reply("submit", true, "Thanks, your report (#3) was sent.", 0, false)"#), "{call}");
        let closed = Reply {
            outcome: Outcome::Disabled,
            ..sent
        };
        assert!(reply_lua(Asked::Ask, &closed).contains(r#"("ask", false, "This realm isn't taking reports right now.", 0, true)"#));
        let wait = Reply {
            outcome: Outcome::Cooldown,
            wait_secs: 300,
            ..sent
        };
        assert!(reply_lua(Asked::Ask, &wait).contains(", 300, false)"));
    }

    // ── The Lua: the rung and the dialog, against stand-ins for the stock frames ───────────────

    /// The Escape menu as benilla's `GameMenuAdapters.xml` leaves it: Support, then Macros.
    const STOCK: &str = r#"
        GameMenuFrame = CreateFrame("Frame", "GameMenuFrame", UIParent)
        GameMenuFrame:SetWidth(200) GameMenuFrame:SetHeight(267)
        GameMenuFrame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
        MenuShows = 0
        GameMenuFrame:SetScript("OnShow", function() MenuShows = MenuShows + 1 end)
        local s = CreateFrame("Button", "BenillaGameMenuButtonSupport", GameMenuFrame)
        s:SetWidth(144) s:SetHeight(21)
        s:SetPoint("TOP", GameMenuFrame, "TOP", 0, -100)
        local m = CreateFrame("Button", "GameMenuButtonMacros", GameMenuFrame)
        m:SetWidth(144) m:SetHeight(21)
        m:SetPoint("TOP", s, "BOTTOM", 0, 0)
        function HideUIPanel(f) f:Hide() end
        function ShowUIPanel(f) f:Show() end
        Clock = 1000
        function GetTime() return Clock end
    "#;

    fn harness() -> UiScript {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        s.run(STOCK).expect("the stand-ins");
        s
    }

    fn installed() -> UiScript {
        let s = harness();
        let p = poll(&s);
        assert!(!p.installed, "the first poll installs");
        assert_eq!(
            s.eval::<Option<String>>(INSTALL_ERRORS_LUA).unwrap(),
            None,
            "every step went in"
        );
        assert!(poll(&s).installed);
        s
    }

    fn text(s: &UiScript, name: &str) -> String {
        s.eval::<Option<String>>(&format!("return {name}:GetText()"))
            .unwrap_or_else(|e| panic!("{name}: {e}"))
            .unwrap_or_default()
    }

    fn enabled(s: &UiScript, name: &str) -> bool {
        s.eval::<bool>(&format!(
            "local e = {name}:IsEnabled() return e == 1 or e == true"
        ))
        .unwrap_or_else(|e| panic!("{name}: {e}"))
    }

    fn top(s: &UiScript, name: &str) -> f64 {
        s.eval::<f64>(&format!("return {name}:GetTop()"))
            .unwrap_or_else(|e| panic!("{name}: {e}"))
    }

    #[test]
    fn the_rung_sits_under_help_and_the_menu_grows_by_one() {
        let mut s = installed();
        s.resolve();
        assert_eq!(text(&s, "BenillaMw2GameMenuButtonFeedback"), "Bugs / Feedback");
        let support = top(&s, "BenillaGameMenuButtonSupport");
        assert_eq!(top(&s, "BenillaMw2GameMenuButtonFeedback"), support - 21.0);
        assert_eq!(top(&s, "GameMenuButtonMacros"), support - 42.0);
        assert_eq!(s.eval::<f64>("return GameMenuFrame:GetHeight()").unwrap(), 288.0);
        // A second poll does not build it twice.
        poll(&s);
        assert_eq!(s.eval::<f64>("return GameMenuFrame:GetHeight()").unwrap(), 288.0);
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn opening_the_menu_asks_and_the_rung_opens_the_dialog() {
        let s = installed();
        s.run("GameMenuFrame:Hide() GameMenuFrame:Show()").unwrap();
        assert_eq!(s.eval::<i64>("return MenuShows").unwrap(), 1, "the stock OnShow still runs");
        assert!(poll(&s).ask, "the menu asks where the account stands");
        assert!(!poll(&s).ask, "once");
        s.run("BenillaMw2GameMenuButtonFeedback:Click()").unwrap();
        assert!(!s.frame_visible("GameMenuFrame"));
        assert!(s.frame_visible("BenillaMw2FeedbackFrame"));
        assert!(poll(&s).ask, "the dialog asks as it opens");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn a_report_needs_a_title_and_goes_up_as_typed() {
        let mut s = installed();
        s.run("BenillaMw2Feedback.Open()").unwrap();
        poll(&s);
        s.run("BenillaMw2FeedbackFrameTitleBox:SetText(\" x \") BenillaMw2FeedbackFrameSubmit:Click()")
            .unwrap();
        assert!(poll(&s).report.is_none(), "too short a title stays here");
        assert!(text(&s, "BenillaMw2FeedbackFrameStatus").contains("title"));
        s.run(
            "BenillaMw2FeedbackFrameKind2:Click() \
             BenillaMw2FeedbackFrameTitleBox:SetText(\"  Gryphon stuck  \") \
             BenillaMw2FeedbackFrameBodyBox:SetText(\"It hovers.\\nForever.\") \
             BenillaMw2FeedbackFrameSubmit:Click()",
        )
        .unwrap();
        let p = poll(&s);
        assert_eq!(
            p.report,
            Some((
                Kind::Suggestion,
                "Gryphon stuck".to_string(),
                "It hovers.\nForever.".to_string()
            ))
        );
        assert_eq!(text(&s, "BenillaMw2FeedbackFrameStatus"), "Sending...");
        assert!(!enabled(&s, "BenillaMw2FeedbackFrameSubmit"), "one at a time");
        // The edit box tells its text change at the next frame.
        s.tick(0.0);
        assert_eq!(text(&s, "BenillaMw2FeedbackFrameCounter"), "19 / 1000");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn the_answers_show_and_the_countdown_greys_the_rung() {
        let s = installed();
        s.run("BenillaMw2Feedback.Open()").unwrap();
        s.run(
            "BenillaMw2FeedbackFrameTitleBox:SetText(\"Hogger is too strong\") \
             BenillaMw2FeedbackFrameSubmit:Click()",
        )
        .unwrap();
        poll(&s);
        let sent = Reply {
            outcome: Outcome::Sent,
            wait_secs: 0,
            id: 5,
            left_today: 4,
        };
        s.run(&reply_lua(Asked::Submit, &sent)).unwrap();
        assert_eq!(
            text(&s, "BenillaMw2FeedbackFrameStatus"),
            "Thanks, your report (#5) was sent."
        );
        assert_eq!(text(&s, "BenillaMw2FeedbackFrameTitleBox"), "", "the form is cleared");
        assert_eq!(text(&s, "BenillaMw2FeedbackFrameCancel"), "Close");
        // The question after it: ten minutes to wait. The thanks stay; the rung counts down.
        let wait = Reply {
            outcome: Outcome::Cooldown,
            wait_secs: 600,
            id: 0,
            left_today: 4,
        };
        s.run(&reply_lua(Asked::Ask, &wait)).unwrap();
        assert_eq!(
            text(&s, "BenillaMw2FeedbackFrameStatus"),
            "Thanks, your report (#5) was sent."
        );
        assert_eq!(text(&s, "BenillaMw2GameMenuButtonFeedback"), "Next report in 10:00");
        assert!(!enabled(&s, "BenillaMw2GameMenuButtonFeedback"));
        assert!(!enabled(&s, "BenillaMw2FeedbackFrameSubmit"));
        s.run("Clock = Clock + 61 BenillaMw2Feedback.Refresh()").unwrap();
        assert_eq!(text(&s, "BenillaMw2GameMenuButtonFeedback"), "Next report in 8:59");
        // Reopened while waiting: the dialog says why Submit is grey.
        s.run("BenillaMw2Feedback.Close() BenillaMw2Feedback.Open()").unwrap();
        s.run(&reply_lua(Asked::Ask, &Reply { wait_secs: 539, ..wait })).unwrap();
        assert_eq!(
            text(&s, "BenillaMw2FeedbackFrameStatus"),
            "You can send another report in 9 minutes."
        );
        // The wait over: the rung is back.
        s.run("Clock = Clock + 600 BenillaMw2Feedback.Refresh()").unwrap();
        assert_eq!(text(&s, "BenillaMw2GameMenuButtonFeedback"), "Bugs / Feedback");
        assert!(enabled(&s, "BenillaMw2GameMenuButtonFeedback"));
        assert!(enabled(&s, "BenillaMw2FeedbackFrameSubmit"));
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn a_refused_report_keeps_the_form_and_cancel_closes() {
        let s = installed();
        s.run("BenillaMw2Feedback.Open()").unwrap();
        s.run(
            "BenillaMw2FeedbackFrameTitleBox:SetText(\"Too many\") \
             BenillaMw2FeedbackFrameSubmit:Click()",
        )
        .unwrap();
        poll(&s);
        let daily = Reply {
            outcome: Outcome::DailyLimit,
            wait_secs: 3600,
            id: 0,
            left_today: 0,
        };
        s.run(&reply_lua(Asked::Submit, &daily)).unwrap();
        assert!(text(&s, "BenillaMw2FeedbackFrameStatus").contains("all the reports for today"));
        assert_eq!(text(&s, "BenillaMw2FeedbackFrameTitleBox"), "Too many", "kept to send later");
        s.run("BenillaMw2FeedbackFrameCancel:Click()").unwrap();
        assert!(!s.frame_visible("BenillaMw2FeedbackFrame"));
        // A server that never answers: the dialog gives up after 15 seconds.
        s.run("Clock = Clock + 3600 BenillaMw2Feedback.Open() BenillaMw2Feedback.Refresh()")
            .unwrap();
        s.run("BenillaMw2FeedbackFrameSubmit:Click()").unwrap();
        assert!(poll(&s).report.is_some());
        s.run("Clock = Clock + 16").unwrap();
        s.run("BenillaMw2FeedbackFrame:GetScript(\"OnUpdate\")()").unwrap();
        assert!(text(&s, "BenillaMw2FeedbackFrameStatus").contains("No answer"));
        assert!(enabled(&s, "BenillaMw2FeedbackFrameSubmit"));
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }
}
