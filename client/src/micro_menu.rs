//! The stock micro menu (`MainMenuBarMicroButtons.xml`) and a few stock frames, reshaped for the
//! layer from the live UI's own Lua, as a 1.12 addon reshapes them; nothing in the fork changes:
//!
//! - The row is **Character, Kits, Quests, Social, Professions, Companions, Map, Main Menu**.
//!   Kits wears the Talents button's art (kits hold the talents now); Professions and
//!   Companions wear the Character button's frame with a stock icon where its portrait sits. A
//!   click opens the layer's window through [`OpenPanel`] with [`Access::Hud`].
//! - The Spellbook, Talents and Help buttons are parked under a hidden frame, so the stock code
//!   that shows them (the level-10 talent gate) shows nothing.
//! - The stock talent frame and spellbook lead to the kit window: `ToggleTalentFrame` (unbound by
//!   default, N being Companions'; any binding of it) opens its Talents page and `ToggleSpellBook`
//!   (P) its Abilities page, and a talent frame shown any other way is hidden again, so stock
//!   talent points are never spent from the interface (the server refuses them as well). The
//!   layer's own kit keys (crate::keybinds) act while it is on (crate::input); these hooks hold
//!   with it off.
//! - Every button's tooltip names the key bound now, as the stock micro buttons do; the quest
//!   log's says it holds the achievements too.
//! - The quest log gets the character frame's tabs under it, **Quests** and **Achievements**; the
//!   achievements are the layer's own window (crate::achievements), which carries the same tabs.
//! - The Escape menu's Support rung (benilla's `GameMenuAdapters.xml`, disabled there) opens the
//!   stock help frame the Help button opened.
//! - The honor tab gets a **Free-for-all PvP** check box: the flag crate::fusion raises.
//!
//! Lua to Rust: a click appends its window's name to the global `BenillaMw2Queue`, which
//! [`drive`] takes and clears each frame.

use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;
use crate::panels::{Access, KitPage, OpenPanel, Panel, ShowKitPage};

/// The global a click leaves its window's name in, `;`-separated.
pub const QUEUE: &str = "BenillaMw2Queue";

/// Take the queue and say whether the micro menu is already reshaped in this VM (a reload or a
/// relog builds a new one). A talent frame that showed after all (an addon's `ShowUIPanel`) goes
/// again, to the kit window's Talents page.
pub const POLL_LUA: &str = "if TalentFrame and TalentFrame:IsVisible() then TalentFrame:Hide() \
     BenillaMw2Queue = (BenillaMw2Queue or \"\") .. \"talents;\" end \
     local q = BenillaMw2Queue BenillaMw2Queue = nil return BenillaMw2MicroMenu ~= nil, q";

/// The reshape, run once per world VM.
pub const INSTALL_LUA: &str = r#"
if BenillaMw2MicroMenu or not MainMenuBarArtFrame or not CharacterMicroButton then return end
BenillaMw2MicroMenu = {}
-- The first quest log tab's centre from the frame's bottom-left. CharacterFrameTab1 sits at
-- (60, 62) under the character frame's art; the quest log's art ends lower (its Abandon and
-- Close buttons sit at y 54), so its tabs hang just under that edge.
local QUEST_TAB_X, QUEST_TAB_Y = 60, 34

local function queue(name)
  BenillaMw2Queue = (BenillaMw2Queue or "") .. name .. ";"
end

-- ── The stock buttons that leave the row ─────────────────────────────────────────────────────
-- Parked under a hidden parent: the stock code may Show() them, nothing draws.
local parked = CreateFrame("Frame", "BenillaMw2ParkedMicroButtons", UIParent)
parked:Hide()
for _, b in ipairs({ SpellbookMicroButton, TalentMicroButton, HelpMicroButton }) do
  if b then
    b:Hide()
    b:SetParent(parked)
  end
end

-- ── The new buttons: the stock micro button's size, hit band, highlight and tooltip ──────────
local BUTTONS = "Interface\\Buttons\\"
local NORMAL_TC = { 0.2, 0.8, 0.08, 0.92 }
local PUSHED_TC = { 0.2666, 0.8666, 0.0134, 0.8534 }

-- A command's key as bound now (crate::keybinds), as the window shows it; nil when unbound.
local function keytext(command)
  local key = GetBindingKey(command)
  if not key then return nil end
  if GetBindingText then return GetBindingText(key, "KEY_") end
  return key
end
BenillaMw2MicroMenu.keytext = keytext

-- `label (key)` with the key bound now; `{WOMW_X}` in `text` filled in, `{PETBAR}` the pet bar's.
local function keyed(label, command)
  local k = command and keytext(command)
  if k then return label .. " (" .. k .. ")" end
  return label
end
local function fill(text)
  return (string.gsub(text, "{([%w_]+)}", function(c)
    if c == "PETBAR" then
      local a, z = keytext("BONUSACTIONBUTTON1"), keytext("BONUSACTIONBUTTON10")
      if a and z then return a .. " to " .. z end
      return a or "the Companion bar keys"
    end
    return keytext(c) or "Not Bound"
  end))
end
BenillaMw2MicroMenu.keyed = keyed
BenillaMw2MicroMenu.fill = fill

local function tooltip(b, label, newbie, command)
  b:SetScript("OnEnter", function()
    local title, body = keyed(label, command), fill(newbie)
    if GameTooltip_AddNewbieTip then
      GameTooltip_AddNewbieTip(title, 1.0, 1.0, 1.0, body)
    else
      GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
      GameTooltip:SetText(title, 1.0, 1.0, 1.0)
      GameTooltip:Show()
    end
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

-- `art` is a micro button texture stem (its -Up/-Down, and -Disabled when `disabled`): the
-- Talents button's, or the Character button's frame (`UI-MicroButtonCharacter`, as
-- CharacterMicroButton_OnLoad sets it). `icon`, when given, sits in the Character button's
-- portrait window and is pushed and dimmed as its portrait is (CharacterMicroButton_SetPushed).
local function micro(name, after, art, disabled, icon, label, newbie, panel, command)
  local b = CreateFrame("Button", name, MainMenuBarArtFrame)
  b:SetWidth(29)
  b:SetHeight(58)
  if b.SetHitRectInsets then b:SetHitRectInsets(0, 0, 18, 0) end
  b:SetPoint("BOTTOMLEFT", after, "BOTTOMRIGHT", -3, 0)
  b:SetNormalTexture(BUTTONS .. art .. "-Up")
  b:SetPushedTexture(BUTTONS .. art .. "-Down")
  if disabled then b:SetDisabledTexture(BUTTONS .. art .. "-Disabled") end
  b:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
  local hl = b.GetHighlightTexture and b:GetHighlightTexture()
  if hl and hl.SetBlendMode then hl:SetBlendMode("ADD") end
  if icon then
    local t = b:CreateTexture(name .. "Icon", "OVERLAY")
    t:SetWidth(18)
    t:SetHeight(25)
    t:SetPoint("TOP", b, "TOP", 0, -28)
    t:SetTexture(icon)
    t:SetTexCoord(NORMAL_TC[1], NORMAL_TC[2], NORMAL_TC[3], NORMAL_TC[4])
    b:SetScript("OnMouseDown", function()
      t:SetTexCoord(PUSHED_TC[1], PUSHED_TC[2], PUSHED_TC[3], PUSHED_TC[4])
      t:SetAlpha(0.5)
    end)
    b:SetScript("OnMouseUp", function()
      t:SetTexCoord(NORMAL_TC[1], NORMAL_TC[2], NORMAL_TC[3], NORMAL_TC[4])
      t:SetAlpha(1.0)
    end)
  end
  tooltip(b, label, newbie, command)
  b:SetScript("OnClick", function() queue(panel) end)
  b:Show()
  return b
end

-- ── The row: Character, Kits, Quests, Social, Professions, Companions, Map, Main Menu ───────
-- Kits takes the second seat; the quest log and the stock social button follow it; then ours;
-- then the stock map and main menu buttons (the main menu button hangs off the map's, as
-- stock). The stock talent gate re-seats the quest log on every level, so it is seated here.
local kits = micro("BenillaMw2KitsMicroButton", CharacterMicroButton, "UI-MicroButton-Talents", true, nil,
  "Kits", "Your kits: loadouts, talents, abilities and killstreaks. Trainers and innkeepers let you change them.",
  "kits", "WOMW_KITS")

local function seat_quest_log()
  QuestLogMicroButton:ClearAllPoints()
  QuestLogMicroButton:SetPoint("BOTTOMLEFT", kits, "BOTTOMRIGHT", -3, 0)
  SocialsMicroButton:ClearAllPoints()
  SocialsMicroButton:SetPoint("BOTTOMLEFT", QuestLogMicroButton, "BOTTOMRIGHT", -3, 0)
end
seat_quest_log()
function UpdateTalentButton()
  TalentMicroButton:Hide()
  seat_quest_log()
end

local professions = micro("BenillaMw2ProfessionsMicroButton", SocialsMicroButton, "UI-MicroButtonCharacter", false,
  "Interface\\Icons\\Trade_BlackSmithing",
  "Professions", "All your professions: their skills, their trade windows, and the abilities they put on Utility's bar (Construction's Build Structure among them).",
  "professions", "WOMW_BUILD")
local companions = micro("BenillaMw2CompanionsMicroButton", professions, "UI-MicroButtonCharacter", false,
  "Interface\\Icons\\Ability_Hunter_BeastCall",
  "Companions", "Your companions and the Bestiary. Companions drop from creatures; use one's item to call it, {PETBAR} to command it. Stable masters breed and retrain them.",
  "companions", "WOMW_COMPANIONS")
WorldMapMicroButton:ClearAllPoints()
WorldMapMicroButton:SetPoint("BOTTOMLEFT", companions, "BOTTOMRIGHT", -3, 0)

-- The quest log's button says it leads to the achievements too.
tooltip(QuestLogMicroButton, "Quests & Achievements",
  "Your quest log, and your achievements on its second tab ({WOMW_JOURNAL}).", "TOGGLEQUESTLOG")

-- ── The stock talent frame and spellbook: the kit window's pages ─────────────────────────────
-- Kits hold the talents now, and the Abilities page lists what the spellbook did. The bindings
-- (TOGGLETALENTS, TOGGLESPELLBOOK, TOGGLEPETBOOK) and any addon reach these globals.
function ToggleTalentFrame()
  if TalentFrame and TalentFrame:IsVisible() then TalentFrame:Hide() end
  queue("talents")
end
function ToggleSpellBook()
  if SpellBookFrame and SpellBookFrame:IsVisible() then HideUIPanel(SpellBookFrame) end
  queue("abilities")
end
-- No stock talent is learnt from this interface (the server refuses one anyway).
function LearnTalent() end

-- ── The quest log's tabs: Quests (this window) and Achievements (the layer's) ───────────────
local function quest_tabs()
  if not QuestLogFrame then return end
  local function tab(id, text)
    local name = "QuestLogFrameTab" .. id
    local ok, t = pcall(CreateFrame, "Button", name, QuestLogFrame, "CharacterFrameTabButtonTemplate")
    if not ok then
      -- Without the stock tab template (a test kit): a plain button of the tab's size.
      t = CreateFrame("Button", name, QuestLogFrame)
      t:SetWidth(80)
      t:SetHeight(32)
    end
    t:SetID(id)
    t:SetText(text)
    -- The template sizes itself to its caption on show; this sizes it now as well.
    if PanelTemplates_TabResize then pcall(PanelTemplates_TabResize, 0, t) end
    return t
  end
  local quests = tab(1, "Quests")
  quests:SetPoint("CENTER", QuestLogFrame, "BOTTOMLEFT", QUEST_TAB_X, QUEST_TAB_Y)
  local achievements = tab(2, "Achievements")
  achievements:SetPoint("LEFT", quests, "RIGHT", -16, 0)
  quests:SetScript("OnClick", function() PlaySound("igCharacterInfoTab") end)
  achievements:SetScript("OnClick", function()
    PlaySound("igCharacterInfoTab")
    HideUIPanel(QuestLogFrame)
    queue("journal")
  end)
  if PanelTemplates_SetNumTabs then
    PanelTemplates_SetNumTabs(QuestLogFrame, 2)
    PanelTemplates_SetTab(QuestLogFrame, 1)
  end
end

-- ── The Escape menu: Support, opening the stock help frame the Help button opened ───────────
local function help_entry()
  local support = BenillaGameMenuButtonSupport
  if not support then return end
  support:SetText("Support")
  support:Enable()
  support:SetScript("OnClick", function()
    PlaySound("igMainMenuOption")
    HideUIPanel(GameMenuFrame)
    ToggleHelpFrame()
  end)
end

-- ── The honor tab: the free-for-all PvP flag ────────────────────────────────────────────────
-- A check box on the stock HonorFrame; a click asks Rust to raise or lower the flag
-- (crate::fusion's `ffa`), and Rust says where it stands (SetFfa).
function BenillaMw2MicroMenu.SetFfa(on)
  local c = BenillaMw2HonorFrameFfa
  if c then c:SetChecked(on and 1 or nil) end
end
local function honor_ffa()
  if not HonorFrame then return end
  local ok, c = pcall(CreateFrame, "CheckButton", "BenillaMw2HonorFrameFfa", HonorFrame, "UICheckButtonTemplate")
  if not ok or not c then
    c = CreateFrame("CheckButton", "BenillaMw2HonorFrameFfa", HonorFrame)
  end
  c:SetWidth(24)
  c:SetHeight(24)
  c:SetPoint("TOPRIGHT", HonorFrame, "TOPRIGHT", -44, -40)
  local label = getglobal("BenillaMw2HonorFrameFfaText")
  if not label then label = c:CreateFontString("BenillaMw2HonorFrameFfaText", "ARTWORK") end
  if GameFontNormalSmall then label:SetFontObject(GameFontNormalSmall) end
  label:ClearAllPoints()
  label:SetPoint("RIGHT", c, "LEFT", -2, 1)
  label:SetText("Free-for-all PvP")
  c:SetScript("OnClick", function()
    if this:GetChecked() then
      PlaySound("igMainMenuOptionCheckBoxOn")
      queue("ffa_on")
    else
      PlaySound("igMainMenuOptionCheckBoxOff")
      queue("ffa_off")
    end
  end)
  c:SetScript("OnEnter", function()
    GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
    GameTooltip:SetText("Free-for-all PvP", 1.0, 1.0, 1.0)
    GameTooltip:AddLine("Anyone flagged may attack you, and you them. It goes up at once and comes down after 5 minutes out of combat.", nil, nil, nil, 1)
    GameTooltip:Show()
  end)
  c:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

-- Each its own step: one that fails leaves the others standing, and says why.
BenillaMw2MicroMenu.errors = {}
for _, step in ipairs({ quest_tabs, help_entry, honor_ffa }) do
  local ok, err = pcall(step)
  if not ok then table.insert(BenillaMw2MicroMenu.errors, tostring(err)) end
end
"#;

/// What the install's steps raised, `\n`-joined (nothing when all went in).
const INSTALL_ERRORS_LUA: &str = "local e = BenillaMw2MicroMenu and BenillaMw2MicroMenu.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

/// What a queued click asks for: a window, the kit window on a page, or the free-for-all flag.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Click {
    Panel(Panel),
    KitPage(KitPage),
    Ffa(bool),
}

/// What a queue names, in click order; an unknown name is skipped.
pub fn parse_queue(q: &str) -> Vec<Click> {
    q.split(';')
        .filter_map(|n| match n.trim() {
            "kits" => Some(Click::Panel(Panel::Kits)),
            "companions" => Some(Click::Panel(Panel::Companions)),
            "construction" => Some(Click::Panel(Panel::Construction)),
            "professions" => Some(Click::Panel(Panel::Professions)),
            "journal" => Some(Click::Panel(Panel::Journal)),
            "talents" => Some(Click::KitPage(KitPage::Talents)),
            "abilities" => Some(Click::KitPage(KitPage::Abilities)),
            "ffa_on" => Some(Click::Ffa(true)),
            "ffa_off" => Some(Click::Ffa(false)),
            _ => None,
        })
        .collect()
}

pub struct MicroMenuPlugin;

impl Plugin for MicroMenuPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, drive.in_set(Mw2Set::Mode));
    }
}

/// Reshape the micro menu once per world VM, and turn its clicks into [`OpenPanel`]s.
fn drive(
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut open: MessageWriter<OpenPanel>,
    mut pages: MessageWriter<ShowKitPage>,
    mut fusion: ResMut<crate::fusion::Fusion>,
    mut shown_ffa: Local<Option<bool>>,
) {
    // The glue screens run a VM of their own; only the world's has the bar.
    let Some(script) = script.filter(|_| state.in_world) else {
        *shown_ffa = None;
        return;
    };
    let clicks = poll(&script);
    if clicks.is_empty() && *shown_ffa == Some(fusion.ffa) {
        return;
    }
    for click in clicks {
        info!("benilla-mw2: micro menu opens {click:?}");
        match click {
            Click::Panel(panel) => {
                open.write(OpenPanel {
                    panel,
                    access: Access::Hud,
                });
            }
            Click::KitPage(page) => {
                pages.write(ShowKitPage(page));
            }
            Click::Ffa(on) => {
                if fusion.ffa != on {
                    fusion.ffa = on;
                    info!(
                        "benilla-mw2: free-for-all flag {}",
                        if on {
                            "on"
                        } else {
                            "off (in 5 minutes out of combat)"
                        }
                    );
                }
            }
        }
    }
    // The honor tab's box shows the flag as it stands (F9 flips it too).
    if *shown_ffa != Some(fusion.ffa) {
        *shown_ffa = Some(fusion.ffa);
        crate::lua::run(&script, &ffa_lua(fusion.ffa));
    }
}

/// The Lua that shows the flag in the honor tab's box.
pub fn ffa_lua(on: bool) -> String {
    format!(
        "if BenillaMw2MicroMenu and BenillaMw2MicroMenu.SetFfa then BenillaMw2MicroMenu.SetFfa({on}) end"
    )
}

/// Install when this VM lacks the reshape; take what was clicked.
pub fn poll(script: &UiScript) -> Vec<Click> {
    let Some((installed, queue)) = crate::lua::eval::<(bool, Option<String>)>(script, POLL_LUA)
    else {
        return Vec::new();
    };
    if !installed {
        crate::lua::run(script, INSTALL_LUA);
        if let Some(Some(e)) = crate::lua::eval::<Option<String>>(script, INSTALL_ERRORS_LUA) {
            warn!("benilla-mw2: micro menu install: {e}");
        }
    }
    queue.as_deref().map(parse_queue).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_queue_reads_in_click_order() {
        assert_eq!(
            parse_queue(
                "kits;journal;bogus;construction;professions;companions;talents;abilities;ffa_on;ffa_off"
            ),
            vec![
                Click::Panel(Panel::Kits),
                Click::Panel(Panel::Journal),
                Click::Panel(Panel::Construction),
                Click::Panel(Panel::Professions),
                Click::Panel(Panel::Companions),
                Click::KitPage(KitPage::Talents),
                Click::KitPage(KitPage::Abilities),
                Click::Ffa(true),
                Click::Ffa(false),
            ]
        );
        assert!(parse_queue("").is_empty());
    }

    #[test]
    fn the_stock_talents_and_spellbook_lead_to_the_kit_window() {
        let s = harness();
        poll(&s);
        s.run("ToggleTalentFrame()").unwrap();
        assert_eq!(poll(&s), vec![Click::KitPage(KitPage::Talents)]);
        s.run("ToggleSpellBook(BOOKTYPE_SPELL)").unwrap();
        assert_eq!(poll(&s), vec![Click::KitPage(KitPage::Abilities)]);
        // A talent frame shown some other way is hidden at the next poll, and leads there too.
        s.run(
            "TalentFrame = CreateFrame(\"Frame\", \"TalentFrame\", UIParent) \
             TalentFrame:SetWidth(10) TalentFrame:SetHeight(10) \
             TalentFrame:SetPoint(\"CENTER\", UIParent, \"CENTER\", 0, 0) TalentFrame:Show()",
        )
        .unwrap();
        assert_eq!(poll(&s), vec![Click::KitPage(KitPage::Talents)]);
        assert!(!s.frame_visible("TalentFrame"));
        assert!(poll(&s).is_empty());
        // Nothing learns a stock talent from the interface.
        s.run("LearnTalent(1, 1)").unwrap();
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    /// Stand-ins for the stock frames the reshape touches, at the stock anchors
    /// (`MainMenuBarMicroButtons.xml`): the row from x 552, 29 wide, chained at -3.
    const STOCK: &str = r#"
        MainMenuBarArtFrame = CreateFrame("Frame", "MainMenuBarArtFrame", UIParent)
        MainMenuBarArtFrame:SetWidth(1024) MainMenuBarArtFrame:SetHeight(53)
        MainMenuBarArtFrame:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", 0, 0)
        local prev
        for _, n in ipairs({ "CharacterMicroButton", "SpellbookMicroButton", "TalentMicroButton",
                             "QuestLogMicroButton", "SocialsMicroButton", "WorldMapMicroButton",
                             "MainMenuMicroButton", "HelpMicroButton" }) do
          local b = CreateFrame("Button", n, MainMenuBarArtFrame)
          b:SetWidth(29) b:SetHeight(58)
          if prev then
            b:SetPoint("BOTTOMLEFT", prev, "BOTTOMRIGHT", -3, 0)
          else
            b:SetPoint("BOTTOMLEFT", MainMenuBarArtFrame, "BOTTOMLEFT", 552, 2)
          end
          prev = b
        end
        for _, n in ipairs({ "MainMenuBarLeftEndCap", "MainMenuBarRightEndCap" }) do
          local c = CreateFrame("Frame", n, MainMenuBarArtFrame)
          c:SetWidth(128) c:SetHeight(128)
          c:SetPoint("BOTTOM", MainMenuBarArtFrame, "BOTTOM", 0, 0)
          c:Show()
        end
        HelpOpened = 0
        function ToggleHelpFrame() HelpOpened = HelpOpened + 1 end
        function UpdateTalentButton()
          TalentMicroButton:Show()
          QuestLogMicroButton:SetPoint("BOTTOMLEFT", TalentMicroButton, "BOTTOMRIGHT", -2, 0)
        end
        QuestLogFrame = CreateFrame("Frame", "QuestLogFrame", UIParent)
        QuestLogFrame:SetWidth(384) QuestLogFrame:SetHeight(512)
        QuestLogFrame:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 0, -104)
        GameMenuFrame = CreateFrame("Frame", "GameMenuFrame", UIParent)
        local s = CreateFrame("Button", "BenillaGameMenuButtonSupport", GameMenuFrame)
        s:SetWidth(144) s:SetHeight(21)
        s:Disable()
        function HideUIPanel(f) f:Hide() end
        function ShowUIPanel(f) f:Show() end
    "#;

    fn harness() -> UiScript {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        s.run(STOCK).expect("the stand-ins");
        s
    }

    fn left(s: &UiScript, name: &str) -> f64 {
        s.eval::<f64>(&format!("return {name}:GetLeft()"))
            .unwrap_or_else(|e| panic!("{name}: {e}"))
    }

    fn shown(s: &UiScript, name: &str) -> bool {
        s.eval::<bool>(&format!("return {name}:IsVisible() and true or false"))
            .unwrap_or_else(|e| panic!("{name}: {e}"))
    }

    #[test]
    fn the_row_is_character_kits_quests_social_professions_companions_map_menu() {
        let mut s = harness();
        assert!(poll(&s).is_empty(), "the first poll installs");
        assert!(s.eval::<bool>("return BenillaMw2MicroMenu ~= nil").unwrap());
        assert_eq!(
            s.eval::<Option<String>>(INSTALL_ERRORS_LUA).unwrap(),
            None,
            "every step went in"
        );
        // The stock talent gate runs on every level: it must not bring Talents back.
        s.run("UpdateTalentButton()").unwrap();
        s.resolve();
        let stride = 26.0;
        let x0 = left(&s, "CharacterMicroButton");
        for (i, name) in [
            "CharacterMicroButton",
            "BenillaMw2KitsMicroButton",
            "QuestLogMicroButton",
            "SocialsMicroButton",
            "BenillaMw2ProfessionsMicroButton",
            "BenillaMw2CompanionsMicroButton",
            "WorldMapMicroButton",
            "MainMenuMicroButton",
        ]
        .iter()
        .enumerate()
        {
            assert_eq!(left(&s, name), x0 + stride * i as f64, "{name}'s seat");
            assert!(shown(&s, name), "{name} shows");
        }
        for gone in [
            "SpellbookMicroButton",
            "TalentMicroButton",
            "HelpMicroButton",
        ] {
            assert!(!shown(&s, gone), "{gone} leaves the row");
        }
        // Eight buttons, as the stock row: it ends where it did, short of the bags.
        let end = left(&s, "MainMenuMicroButton") + 29.0;
        assert_eq!(end, x0 + stride * 7.0 + 29.0);
        // A second poll does not build it twice.
        assert!(poll(&s).is_empty());
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn each_button_opens_its_window_and_hovers_a_tooltip() {
        let s = harness();
        poll(&s);
        for (button, panel) in [
            ("BenillaMw2KitsMicroButton", Panel::Kits),
            ("BenillaMw2CompanionsMicroButton", Panel::Companions),
            ("BenillaMw2ProfessionsMicroButton", Panel::Professions),
        ] {
            s.run(&format!("{button}:Click()")).unwrap();
            assert_eq!(poll(&s), vec![Click::Panel(panel)], "{button}");
            assert!(
                s.eval::<bool>(&format!("return {button}:GetScript(\"OnEnter\") ~= nil"))
                    .unwrap(),
                "{button} has a tooltip"
            );
        }
        assert!(poll(&s).is_empty(), "the queue is taken once");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    /// The tooltips name the keys bound now: rebinding one changes what they say.
    #[test]
    fn the_tooltips_show_the_keys_bound_now() {
        let s = harness();
        poll(&s);
        s.run(r#"SetBinding("K", "WOMW_KITS") SetBinding("B", "WOMW_BUILD")
                 SetBinding("CTRL-1", "BONUSACTIONBUTTON1") SetBinding("CTRL-0", "BONUSACTIONBUTTON10")"#)
            .unwrap();
        let keyed = |c: &str| {
            s.eval::<String>(&format!(
                r#"return BenillaMw2MicroMenu.keyed("Kits", "{c}")"#
            ))
            .unwrap()
        };
        assert_eq!(keyed("WOMW_KITS"), "Kits (K)");
        assert_eq!(keyed("WOMW_CONSTRUCTION"), "Kits", "unbound: no key");
        let fill = |t: &str| {
            s.eval::<String>(&format!(r#"return BenillaMw2MicroMenu.fill("{t}")"#))
                .unwrap()
        };
        assert_eq!(fill("Press {WOMW_BUILD} to build."), "Press B to build.");
        assert_eq!(
            fill("{PETBAR} to command it"),
            "CTRL-1 to CTRL-0 to command it"
        );
        s.run(r#"SetBinding("B") SetBinding("J", "WOMW_BUILD")"#)
            .unwrap();
        assert_eq!(fill("Press {WOMW_BUILD} to build."), "Press J to build.");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn the_quest_logs_achievements_tab_hands_over_to_the_achievements() {
        let s = harness();
        poll(&s);
        s.run("QuestLogFrame:Show()").unwrap();
        assert_eq!(
            s.eval::<String>("return QuestLogFrameTab2:GetText()")
                .unwrap(),
            "Achievements"
        );
        s.run("QuestLogFrameTab2:Click()").unwrap();
        assert!(
            !s.frame_visible("QuestLogFrame"),
            "the quest log steps aside"
        );
        assert_eq!(poll(&s), vec![Click::Panel(Panel::Journal)]);
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn the_escape_menus_support_rung_opens_help() {
        let s = harness();
        poll(&s);
        let (text, enabled): (String, bool) = s
            .eval(
                "local b = BenillaGameMenuButtonSupport \
                 return b:GetText(), b:IsEnabled() == 1 or b:IsEnabled() == true",
            )
            .unwrap();
        assert_eq!(text, "Support");
        assert!(enabled);
        s.run("BenillaGameMenuButtonSupport:Click()").unwrap();
        assert_eq!(s.eval::<i64>("return HelpOpened").unwrap(), 1);
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    /// The honor tab's box raises and lowers the free-for-all flag, and shows it as it stands.
    #[test]
    fn the_honor_tab_carries_the_free_for_all_flag() {
        let s = harness();
        s.run(
            "HonorFrame = CreateFrame(\"Frame\", \"HonorFrame\", UIParent) \
             HonorFrame:SetWidth(384) HonorFrame:SetHeight(512) \
             HonorFrame:SetPoint(\"TOPLEFT\", UIParent, \"TOPLEFT\", 0, 0) HonorFrame:Show()",
        )
        .unwrap();
        poll(&s);
        assert_eq!(
            s.eval::<String>("return BenillaMw2HonorFrameFfaText:GetText()")
                .unwrap(),
            "Free-for-all PvP"
        );
        s.run("BenillaMw2HonorFrameFfa:Click()").unwrap();
        assert_eq!(poll(&s), vec![Click::Ffa(true)]);
        s.run("BenillaMw2HonorFrameFfa:Click()").unwrap();
        assert_eq!(poll(&s), vec![Click::Ffa(false)]);
        // Rust shows where the flag stands (F9 flips it too).
        s.run(&ffa_lua(true)).unwrap();
        assert!(
            s.eval::<bool>("return BenillaMw2HonorFrameFfa:GetChecked() and true or false")
                .unwrap()
        );
        s.run(&ffa_lua(false)).unwrap();
        assert!(
            !s.eval::<bool>("return BenillaMw2HonorFrameFfa:GetChecked() and true or false")
                .unwrap()
        );
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    /// The quest log's button names the achievements and its key as bound now.
    #[test]
    fn the_quest_buttons_tooltip_names_the_achievements() {
        let s = harness();
        s.run(r#"SetBinding("L", "TOGGLEQUESTLOG")"#).unwrap();
        poll(&s);
        assert!(
            s.eval::<bool>("return QuestLogMicroButton:GetScript(\"OnEnter\") ~= nil")
                .unwrap()
        );
        assert_eq!(
            s.eval::<String>(
                r#"return BenillaMw2MicroMenu.keyed("Quests & Achievements", "TOGGLEQUESTLOG")"#
            )
            .unwrap(),
            "Quests & Achievements (L)"
        );
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }
}
