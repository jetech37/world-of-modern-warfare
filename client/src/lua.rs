//! Calls into the client's own Lua 1.12 API. benilla keeps most of its game state crate-private,
//! but the stock API reaches all of it, exactly as an addon or macro would.

use benilla_ui::script::UiScript;
use bevy::log::warn;

/// Run a chunk, logging (not raising) a Lua error.
pub fn run(script: &UiScript, chunk: &str) {
    if let Err(e) = script.run(chunk) {
        warn!("benilla-mw2: lua error in `{chunk}`: {e}");
    }
}

/// Evaluate an expression chunk (`return ...`), `None` on a Lua error.
pub fn eval<T: mlua::FromLuaMulti>(script: &UiScript, chunk: &str) -> Option<T> {
    match script.eval::<T>(chunk) {
        Ok(v) => Some(v),
        Err(e) => {
            warn!("benilla-mw2: lua error in `{chunk}`: {e}");
            None
        }
    }
}

/// A Lua string literal for `s`.
pub fn quote(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// Play a SoundEntries kit by name (`PlaySound`); an unknown name plays nothing.
pub fn sound(script: &UiScript, kit: &str) {
    run(script, &format!("PlaySound({})", quote(kit)));
}

/// The CVar the Lua dialogs' places are kept in, as crate::wow_ui keeps the windows' (persisted
/// with the account's settings): `key=x,y;…`, each the top-left as a share of `UIParent`.
pub const FRAME_POSITIONS_CVAR: &str = "womwFramePositions";

/// `BenillaMw2Movable(frame, key, point, relativePoint, x, y)`: the layer's Lua dialogs (the
/// feedback dialog, the locks) drag by their title and background, keep where they were put
/// between opens and sessions, and carry a reset button (the character model's turn-left arrow)
/// at their top right that puts them back at the given anchor on `UIParent`. Defined once per
/// world VM; [`install_movable`] runs it before a dialog's own install.
pub const MOVABLE_LUA: &str = r#"
if BenillaMw2Movable then return end
local M = { saved = {}, loaded = nil }
BenillaMw2FramePositions = M
local CVAR = "womwFramePositions"

local function load()
  if M.loaded then return end
  M.loaded = 1
  if RegisterCVar then RegisterCVar(CVAR, "") end
  local s = GetCVar and GetCVar(CVAR) or ""
  for k, x, y in string.gfind(s or "", "([^=;]+)=([%d%.]+),([%d%.]+)") do
    local fx, fy = tonumber(x), tonumber(y)
    if fx and fy and fx <= 1 and fy <= 1 then M.saved[k] = { fx, fy } end
  end
end

local function save()
  local parts = {}
  for k, p in pairs(M.saved) do
    table.insert(parts, k .. "=" .. string.format("%.4f,%.4f", p[1], p[2]))
  end
  table.sort(parts)
  if RegisterCVar then RegisterCVar(CVAR, "") end
  SetCVar(CVAR, table.concat(parts, ";"))
end

-- Where the frame sits: where it was put, else its anchor.
local function seat(f)
  local p = M.saved[f.womwKey]
  f:ClearAllPoints()
  if p then
    f:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", p[1] * UIParent:GetWidth(), p[2] * UIParent:GetHeight())
  else
    local a = f.womwAnchor
    f:SetPoint(a[1], UIParent, a[2], a[3], a[4])
  end
end
M.seat = seat

-- The frame's top-left now, as shares of UIParent; nil before it is laid out.
local function share(f)
  local l, t = f:GetLeft(), f:GetTop()
  local w, h = UIParent:GetWidth(), UIParent:GetHeight()
  if not l or not t or not w or not h or w <= 0 or h <= 0 then return nil end
  local k = 1
  if f.GetEffectiveScale and UIParent.GetEffectiveScale then
    k = f:GetEffectiveScale() / UIParent:GetEffectiveScale()
  end
  local x, y = l * k / w, t * k / h
  if x < 0 then x = 0 end
  if y < 0 then y = 0 end
  if x > 1 then x = 1 end
  if y > 1 then y = 1 end
  return x, y
end

function M.Reset(f)
  M.saved[f.womwKey] = nil
  save()
  seat(f)
end

function BenillaMw2Movable(f, key, point, relativePoint, x, y)
  load()
  f.womwKey = key
  f.womwAnchor = { point, relativePoint, x, y }
  f:EnableMouse(true)
  if f.SetClampedToScreen then f:SetClampedToScreen(true) end
  if f.SetMovable and f.RegisterForDrag then
    f:SetMovable(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function()
      this:StopMovingOrSizing()
      local sx, sy = share(this)
      if sx then
        M.saved[this.womwKey] = { sx, sy }
        save()
        seat(this)
      end
    end)
  end
  -- The reset button: the stock model frame's turn-left arrow, at the top right.
  local name = f:GetName() and (f:GetName() .. "ResetPosition")
  local r = CreateFrame("Button", name, f)
  r:SetWidth(22)
  r:SetHeight(22)
  r:SetPoint("TOPRIGHT", f, "TOPRIGHT", -10, -10)
  r:SetNormalTexture("Interface\\Buttons\\UI-RotationLeft-Button-Up")
  r:SetPushedTexture("Interface\\Buttons\\UI-RotationLeft-Button-Down")
  r:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Round")
  local hl = r.GetHighlightTexture and r:GetHighlightTexture()
  if hl and hl.SetBlendMode then hl:SetBlendMode("ADD") end
  r:SetScript("OnClick", function()
    PlaySound("igMainMenuOptionCheckBoxOn")
    M.Reset(this:GetParent())
  end)
  r:SetScript("OnEnter", function()
    GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
    GameTooltip:SetText("Reset Position", 1.0, 1.0, 1.0)
    GameTooltip:AddLine("Puts this window back where it opens by default.", nil, nil, nil, 1)
    GameTooltip:Show()
  end)
  r:SetScript("OnLeave", function() GameTooltip:Hide() end)
  f.womwReset = r
  -- Seated now and at every show, so a place set while it was hidden holds.
  local shown = f:GetScript("OnShow")
  f:SetScript("OnShow", function()
    seat(this)
    if shown then shown() end
  end)
  seat(f)
  return r
end
"#;

/// Define [`MOVABLE_LUA`]'s helper in this VM (once; again is a no-op).
pub fn install_movable(script: &UiScript) {
    run(script, MOVABLE_LUA);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quotes_escape() {
        assert_eq!(quote("Auto Shot"), "\"Auto Shot\"");
        assert_eq!(quote("a\"b\\c"), "\"a\\\"b\\\\c\"");
    }

    /// A dialog made movable sits at its anchor, keeps where it was put (in the account CVar,
    /// read back by a fresh VM's helper), and its reset button puts it back.
    #[test]
    fn a_lua_dialog_drags_remembers_and_resets() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        // A bare VM has no stock UIParent: one at the reference's 1024x768.
        s.run(
            r#"local root = CreateFrame("Frame", "UIParent")
               root:SetWidth(1024) root:SetHeight(768)
               root:SetPoint("BOTTOMLEFT", nil, "BOTTOMLEFT", 0, 0) UIParent = root"#,
        )
        .unwrap();
        let dialog = r#"
            local d = CreateFrame("Frame", "TestDialog", UIParent)
            d:SetWidth(200) d:SetHeight(100)
            BenillaMw2Movable(d, "test", "CENTER", "CENTER", 0, 60)
            d:Show()
        "#;
        install_movable(&s);
        s.run(dialog).unwrap();
        s.resolve();
        let corner = |s: &UiScript| {
            s.eval::<(f64, f64)>("return TestDialog:GetLeft(), TestDialog:GetTop()")
                .unwrap()
        };
        let (w, h) = s
            .eval::<(f64, f64)>("return UIParent:GetWidth(), UIParent:GetHeight()")
            .unwrap();
        let home = corner(&s);
        assert!(
            (home.0 - (w / 2.0 - 100.0)).abs() < 0.5,
            "centred: {home:?}"
        );
        assert!(
            (home.1 - (h / 2.0 + 60.0 + 50.0)).abs() < 0.5,
            "60 up: {home:?}"
        );
        // Moved (as StartMoving leaves it), then let go: the place is kept as shares. `this` is
        // the frame in a real OnDragStop; the test calls it the same way.
        s.run(
            "this = TestDialog TestDialog:ClearAllPoints() \
             TestDialog:SetPoint(\"TOPLEFT\", UIParent, \"BOTTOMLEFT\", 100, 300) \
             TestDialog:GetScript(\"OnDragStop\")()",
        )
        .unwrap();
        s.resolve();
        let saved = s
            .eval::<String>(&format!("return GetCVar(\"{FRAME_POSITIONS_CVAR}\")"))
            .unwrap();
        assert!(saved.starts_with("test="), "saved: {saved}");
        let moved = corner(&s);
        assert!(
            (moved.0 - 100.0).abs() < 0.5 && (moved.1 - 300.0).abs() < 0.5,
            "{moved:?}"
        );
        // Hidden and shown: still there.
        s.run("TestDialog:Hide() TestDialog:Show()").unwrap();
        s.resolve();
        let again = corner(&s);
        assert!(
            (again.0 - 100.0).abs() < 0.5 && (again.1 - 300.0).abs() < 0.5,
            "{again:?}"
        );
        // The next session: a fresh helper reads the CVar back.
        s.run("BenillaMw2Movable = nil").unwrap();
        install_movable(&s);
        s.run(
            "local d = CreateFrame(\"Frame\", \"TestDialog2\", UIParent) \
             d:SetWidth(200) d:SetHeight(100) \
             BenillaMw2Movable(d, \"test\", \"CENTER\", \"CENTER\", 0, 60) d:Show()",
        )
        .unwrap();
        s.resolve();
        let (l, t) = s
            .eval::<(f64, f64)>("return TestDialog2:GetLeft(), TestDialog2:GetTop()")
            .unwrap();
        assert!(
            (l - 100.0).abs() < 0.5 && (t - 300.0).abs() < 0.5,
            "{l} {t}"
        );
        // Reset: back at the anchor, the place forgotten.
        s.run("TestDialog2ResetPosition:Click()").unwrap();
        s.resolve();
        let (l, t) = s
            .eval::<(f64, f64)>("return TestDialog2:GetLeft(), TestDialog2:GetTop()")
            .unwrap();
        assert!(
            (l - home.0).abs() < 0.5 && (t - home.1).abs() < 0.5,
            "{l} {t}"
        );
        assert_eq!(
            s.eval::<String>(&format!("return GetCVar(\"{FRAME_POSITIONS_CVAR}\")"))
                .unwrap(),
            ""
        );
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }
}
