//! The layer's windows in the classic WoW look, and the one place they are built from: a window
//! is a FrameXML dialog (the `UI-DialogBox-Border` edge on its tiled background, the gold
//! `UI-DialogBox-Header` with the title in Friz Quadrata, the X of `UIPanelCloseButton`) or a
//! tooltip-style panel (`UI-Tooltip-Border`), with the red `UIPanelButtonTemplate` buttons, the
//! character frame's tabs hanging under it, and `UIPanelScrollFrameTemplate` scroll lists.
//!
//! The art is the player's own, read once from the install (`WorldAssets`); without it (tests, a
//! missing file) every piece falls back to plain colours. Sizes are authored in WoW UI units, as
//! FrameXML's are, and drawn at the client's seam (`windowH/768 × uiScale`): [`u`] turns units into
//! pixels at the seam the session started at, and each window's transform follows the seam as it
//! changes, scaling the window down further when it would not fit the screen.
//!
//! Every movable window drags by its title and its background (`SetMovable`, `RegisterForDrag`),
//! stays inside the game window (`SetClampedToScreen`), and keeps where it was put, between opens
//! and sessions ([`WindowPositions`], in the account's [`POSITIONS_CVAR`]); a small reset button
//! (the character model's turn-left arrow) beside its close button puts it back. Any window built
//! with [`spawn_window`] and `movable: true` gets all of it. While the pointer is on one, the
//! world does not take the click.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};

use benilla_app::fusion::LayerPointerOver;
use benilla_assets::{WorldAssets, sprite_image, sprite_image_tiled};
use benilla_ui::script::UiScript;
use bevy::ecs::hierarchy::ChildSpawner;
use bevy::input::mouse::{MouseScrollUnit, MouseWheel};
use bevy::picking::Pickable;
use bevy::prelude::*;
use bevy::ui::{FocusPolicy, RelativeCursorPosition, UiGlobalTransform, UiSystems};
use bevy::window::PrimaryWindow;

use crate::mode::FpsState;

/// The font the stock FrameXML's `GameFontNormal` family uses.
pub const FONT: &str = "mpq://Fonts/FRIZQT__.ttf";
/// `NORMAL_FONT_COLOR`: titles, labels, a button's caption.
pub const GOLD: Color = Color::srgb(1.0, 0.82, 0.0);
/// `HIGHLIGHT_FONT_COLOR`: body text, a hovered caption.
pub const WHITE: Color = Color::srgb(1.0, 1.0, 1.0);
/// `GRAY_FONT_COLOR`: disabled captions and notes.
pub const GREY: Color = Color::srgb(0.5, 0.5, 0.5);
/// Secondary body text, between white and the disabled grey.
pub const DIM: Color = Color::srgb(0.75, 0.75, 0.75);
/// `GREEN_FONT_COLOR`.
pub const GREEN: Color = Color::srgb(0.1, 1.0, 0.1);
/// `RED_FONT_COLOR`.
pub const RED: Color = Color::srgb(1.0, 0.1, 0.1);

// ── Type and spacing ─────────────────────────────────────────────────────────────────────────────
//
// One scale for every window, in UI units: the stock `GameFontNormalLarge`, `GameFontNormal`,
// `GameFontNormalSmall` and the notes under a line, and the gaps between sections and lines.

/// A window's own big line (a selected entry's name).
pub const TITLE_SIZE: f32 = 14.0;
/// A section header, a list line, body text.
pub const BODY_SIZE: f32 = 12.0;
/// The second line of a list entry, a label.
pub const SMALL_SIZE: f32 = 11.0;
/// Notes and hints.
pub const NOTE_SIZE: f32 = 10.0;
/// Above a section header.
pub const SECTION_GAP: f32 = 8.0;
/// Between the lines of a list.
pub const LINE_GAP: f32 = 2.0;
/// Inside an inset panel.
pub const INSET_PAD: f32 = 6.0;

/// An item quality's colour (`ITEM_QUALITY_COLORS`): poor, common, uncommon, rare, epic,
/// legendary, artifact.
pub fn quality_color(quality: u8) -> Color {
    match quality {
        0 => Color::srgb(0.62, 0.62, 0.62),
        2 => Color::srgb(0.12, 1.0, 0.0),
        3 => Color::srgb(0.0, 0.44, 0.87),
        4 => Color::srgb(0.64, 0.21, 0.93),
        5 => Color::srgb(1.0, 0.5, 0.0),
        6 => Color::srgb(0.9, 0.8, 0.5),
        _ => WHITE,
    }
}

// ── Units ────────────────────────────────────────────────────────────────────────────────────────

/// Pixels per UI unit for building, as `f32` bits: 1 until the first world frame fixes it.
static UNIT_BITS: AtomicU32 = AtomicU32::new(0x3F80_0000);

/// Logical pixels per UI unit that windows are built at.
pub fn unit() -> f32 {
    f32::from_bits(UNIT_BITS.load(Ordering::Relaxed))
}

fn set_unit(k: f32) {
    UNIT_BITS.store(k.to_bits(), Ordering::Relaxed);
}

/// `n` UI units as a `Val`.
pub fn u(n: f32) -> Val {
    Val::Px(n * unit())
}

/// `n` UI units in logical pixels.
pub fn uf(n: f32) -> f32 {
    n * unit()
}

/// The client's seam: window pixels per UI unit, `windowH/768 × uiScale` (benilla's
/// `ui_script::seam_scale`), 1 for a window not yet sized.
pub fn seam_scale(window_h: f32, ui_scale: f32) -> f32 {
    if window_h > 0.0 {
        window_h / 768.0 * ui_scale
    } else {
        1.0
    }
}

/// `GetCVar("uiScale")` as a scale: the dial's own `[0.5, 1.5]`, 0.9 (benilla's default) when
/// unreadable.
pub fn parse_ui_scale(cvar: Option<&str>) -> f32 {
    cvar.and_then(|s| s.trim().parse::<f32>().ok())
        .filter(|v| v.is_finite())
        .map_or(0.9, |v| v.clamp(0.5, 1.5))
}

/// The scale a window is drawn at: the seam it wants, or less when its laid-out `size` would not
/// fit the `viewport` with `margin` pixels clear on every side. Never above `want`.
pub fn fit_scale(size: Vec2, viewport: Vec2, margin: f32, want: f32) -> f32 {
    if size.x <= 0.0 || size.y <= 0.0 {
        return want;
    }
    let room = (viewport - Vec2::splat(2.0 * margin)).max(Vec2::ONE);
    want.min((room / size).min_element()).max(0.05)
}

/// A window's top-left kept inside the viewport (`SetClampedToScreen`); a window wider or taller
/// than the viewport keeps its top or left edge on screen.
pub fn clamp_position(pos: Vec2, size: Vec2, viewport: Vec2) -> Vec2 {
    let max = (viewport - size).max(Vec2::ZERO);
    pos.clamp(Vec2::ZERO, max)
}

/// The UI's seam, as the world's Lua reports `uiScale`.
#[derive(Resource, Debug)]
pub struct UiSeam {
    pub ui_scale: f32,
    /// [`unit`] is fixed for the session.
    frozen: bool,
    next_poll: f32,
}

impl Default for UiSeam {
    fn default() -> Self {
        Self {
            ui_scale: 0.9,
            frozen: false,
            next_poll: 0.0,
        }
    }
}

/// In the world: read `uiScale` now and then, and fix the build unit at the first world frame's
/// seam, so the windows' text is drawn crisp at the size they show.
fn track_seam(
    time: Res<Time>,
    state: Res<FpsState>,
    window: Query<&Window, With<PrimaryWindow>>,
    script: Option<NonSend<UiScript>>,
    mut seam: ResMut<UiSeam>,
) {
    if !state.in_world {
        return;
    }
    let t = time.elapsed_secs();
    if t >= seam.next_poll {
        seam.next_poll = t + 1.0;
        if let Some(script) = script.as_deref() {
            let cvar: Option<Option<String>> =
                crate::lua::eval(script, "return GetCVar(\"uiScale\")");
            let s = parse_ui_scale(cvar.flatten().as_deref());
            if (seam.ui_scale - s).abs() > 1e-4 {
                seam.ui_scale = s;
            }
        }
    }
    if !seam.frozen
        && let Ok(w) = window.single()
    {
        let k = seam_scale(w.height(), seam.ui_scale).clamp(0.4, 4.0);
        set_unit(k);
        seam.frozen = true;
        info!("benilla-mw2: layer windows built at {k:.3} px per UI unit");
    }
}

// ── Art ──────────────────────────────────────────────────────────────────────────────────────────

/// A piece of the stock interface art.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Art {
    DialogBorder,
    DialogBg,
    TooltipBorder,
    TooltipBg,
    Header,
    ButtonUp,
    ButtonDown,
    ButtonHighlight,
    ButtonDisabled,
    CloseUp,
    CloseDown,
    CloseHighlight,
    TabActive,
    TabInactive,
    TabHighlight,
    RowHighlight,
    ScrollUpUp,
    ScrollUpDown,
    ScrollUpDisabled,
    ScrollDownUp,
    ScrollDownDown,
    ScrollDownDisabled,
    Knob,
    /// The rule under a section's title (`UI-DialogBox-Divider`, the reputation detail's).
    Divider,
    /// The square hover glow of an icon button (`ButtonHilight-Square`).
    IconHighlight,
    /// The character model's turn-left arrow (`UI-RotationLeft-Button`): a window's reset button.
    ResetUp,
    ResetDown,
    /// The round hover glow of the rotation buttons (`ButtonHilight-Round`).
    RoundHighlight,
    /// `UIDropDownMenuTemplate`'s box (`CharacterCreate-LabelFrame`).
    DropDownFrame,
}

/// How a file is made ready to draw.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Prep {
    /// As stored, clamped.
    Plain,
    /// Wrapping, for a tiled background.
    Tiled,
    /// A backdrop `edgeFile` strip, rebuilt as one nine-slice sheet.
    Edges,
    /// An `alphaMode="ADD"` texture, turned into an alpha-blended one.
    Additive,
}

const ART: [(Art, &str, Prep); 29] = [
    (
        Art::DropDownFrame,
        "Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame",
        Prep::Plain,
    ),
    (
        Art::ResetUp,
        "Interface\\Buttons\\UI-RotationLeft-Button-Up",
        Prep::Plain,
    ),
    (
        Art::ResetDown,
        "Interface\\Buttons\\UI-RotationLeft-Button-Down",
        Prep::Plain,
    ),
    (
        Art::RoundHighlight,
        "Interface\\Buttons\\ButtonHilight-Round",
        Prep::Additive,
    ),
    (
        Art::Divider,
        "Interface\\DialogFrame\\UI-DialogBox-Divider",
        Prep::Plain,
    ),
    (
        Art::IconHighlight,
        "Interface\\Buttons\\ButtonHilight-Square",
        Prep::Additive,
    ),
    (
        Art::DialogBorder,
        "Interface\\DialogFrame\\UI-DialogBox-Border",
        Prep::Edges,
    ),
    (
        Art::DialogBg,
        "Interface\\DialogFrame\\UI-DialogBox-Background",
        Prep::Tiled,
    ),
    (
        Art::TooltipBorder,
        "Interface\\Tooltips\\UI-Tooltip-Border",
        Prep::Edges,
    ),
    (
        Art::TooltipBg,
        "Interface\\Tooltips\\UI-Tooltip-Background",
        Prep::Tiled,
    ),
    (
        Art::Header,
        "Interface\\DialogFrame\\UI-DialogBox-Header",
        Prep::Plain,
    ),
    (
        Art::ButtonUp,
        "Interface\\Buttons\\UI-Panel-Button-Up",
        Prep::Plain,
    ),
    (
        Art::ButtonDown,
        "Interface\\Buttons\\UI-Panel-Button-Down",
        Prep::Plain,
    ),
    (
        Art::ButtonHighlight,
        "Interface\\Buttons\\UI-Panel-Button-Highlight",
        Prep::Additive,
    ),
    (
        Art::ButtonDisabled,
        "Interface\\Buttons\\UI-Panel-Button-Disabled",
        Prep::Plain,
    ),
    (
        Art::CloseUp,
        "Interface\\Buttons\\UI-Panel-MinimizeButton-Up",
        Prep::Plain,
    ),
    (
        Art::CloseDown,
        "Interface\\Buttons\\UI-Panel-MinimizeButton-Down",
        Prep::Plain,
    ),
    (
        Art::CloseHighlight,
        "Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight",
        Prep::Additive,
    ),
    (
        Art::TabActive,
        "Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab",
        Prep::Plain,
    ),
    (
        Art::TabInactive,
        "Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab",
        Prep::Plain,
    ),
    (
        Art::TabHighlight,
        "Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight",
        Prep::Additive,
    ),
    (
        Art::RowHighlight,
        "Interface\\QuestFrame\\UI-QuestTitleHighlight",
        Prep::Additive,
    ),
    (
        Art::ScrollUpUp,
        "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up",
        Prep::Plain,
    ),
    (
        Art::ScrollUpDown,
        "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Down",
        Prep::Plain,
    ),
    (
        Art::ScrollUpDisabled,
        "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Disabled",
        Prep::Plain,
    ),
    (
        Art::ScrollDownUp,
        "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up",
        Prep::Plain,
    ),
    (
        Art::ScrollDownDown,
        "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Down",
        Prep::Plain,
    ),
    (
        Art::ScrollDownDisabled,
        "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Disabled",
        Prep::Plain,
    ),
    (
        Art::Knob,
        "Interface\\Buttons\\UI-ScrollBar-Knob",
        Prep::Plain,
    ),
];

/// The art read from the install: each piece's image and its size in texels.
#[derive(Resource, Default)]
pub struct WowArt {
    images: HashMap<Art, (Handle<Image>, Vec2)>,
    tried: bool,
}

impl WowArt {
    pub fn get(&self, art: Art) -> Option<&(Handle<Image>, Vec2)> {
        self.images.get(&art)
    }

    pub fn has(&self, art: Art) -> bool {
        self.images.contains_key(&art)
    }
}

/// A backdrop `edgeFile` (eight `e`×`e` cells in a row: left, right, top, bottom, then the
/// top-left, top-right, bottom-left and bottom-right corners) as one `3e`×`3e` nine-slice sheet,
/// the centre clear. Top and bottom are stored turned a quarter (the reference maps the atlas's u
/// to screen y and v to screen x reversed), so their cells read texel `(k·e + y, e − 1 − x)`, as
/// benilla's glue backdrops do.
pub fn edge_nine_slice(e: usize, strip: &[u8]) -> Vec<u8> {
    let side = 3 * e;
    let mut out = vec![0u8; side * side * 4];
    let src = |x: usize, y: usize| {
        let i = (y * 8 * e + x) * 4;
        &strip[i..i + 4]
    };
    for y in 0..e {
        for x in 0..e {
            // (cell column, cell row) in the sheet ← texel of the strip.
            let cells: [((usize, usize), (usize, usize)); 8] = [
                ((0, 1), (x, y)),                 // left
                ((2, 1), (e + x, y)),             // right
                ((1, 0), (2 * e + y, e - 1 - x)), // top, turned back upright
                ((1, 2), (3 * e + y, e - 1 - x)), // bottom, likewise
                ((0, 0), (4 * e + x, y)),         // top-left
                ((2, 0), (5 * e + x, y)),         // top-right
                ((0, 2), (6 * e + x, y)),         // bottom-left
                ((2, 2), (7 * e + x, y)),         // bottom-right
            ];
            for ((cx, cy), (sx, sy)) in cells {
                let o = ((cy * e + y) * side + cx * e + x) * 4;
                out[o..o + 4].copy_from_slice(src(sx, sy));
            }
        }
    }
    out
}

/// An `alphaMode="ADD"` texture as an alpha-blended one: the light it adds (`rgb·a`) becomes the
/// alpha, its hue the colour, so blending it over a dark frame adds about what the stock blend
/// adds.
pub fn additive_to_alpha(rgba: &mut [u8]) {
    for px in rgba.chunks_exact_mut(4) {
        let a = f32::from(px[3]) / 255.0;
        let c = [px[0], px[1], px[2]].map(|v| f32::from(v) * a);
        let m = c.iter().copied().fold(0.0f32, f32::max);
        if m <= 0.0 {
            px.copy_from_slice(&[0, 0, 0, 0]);
            continue;
        }
        for i in 0..3 {
            px[i] = (c[i] / m * 255.0).round().clamp(0.0, 255.0) as u8;
        }
        px[3] = m.round().clamp(0.0, 255.0) as u8;
    }
}

/// Read every piece once, when the install is open.
fn load_art(
    mut art: ResMut<WowArt>,
    world: Option<ResMut<WorldAssets>>,
    mut images: ResMut<Assets<Image>>,
) {
    let Some(mut world) = world else {
        return;
    };
    art.tried = true;
    let mut missing = Vec::new();
    for (key, path, prep) in ART {
        let Some((w, h, mut rgba)) = world.decode_rgba(path) else {
            missing.push(path);
            continue;
        };
        let (image, size) = match prep {
            Prep::Plain => (sprite_image(w, h, rgba), Vec2::new(w as f32, h as f32)),
            Prep::Tiled => (
                sprite_image_tiled(w, h, rgba),
                Vec2::new(w as f32, h as f32),
            ),
            Prep::Additive => {
                additive_to_alpha(&mut rgba);
                (sprite_image(w, h, rgba), Vec2::new(w as f32, h as f32))
            }
            Prep::Edges => {
                if h == 0 || w != 8 * h {
                    warn!("benilla-mw2: {path} is {w}x{h}, not an edge strip");
                    continue;
                }
                let e = h as usize;
                let sheet = edge_nine_slice(e, &rgba);
                let side = 3 * h;
                (sprite_image(side, side, sheet), Vec2::splat(side as f32))
            }
        };
        art.images.insert(key, (images.add(image), size));
    }
    if missing.is_empty() {
        info!("benilla-mw2: layer windows wear the stock interface art");
    } else {
        warn!("benilla-mw2: interface art missing, plain colours for: {missing:?}");
    }
}

fn art_untried(art: Res<WowArt>) -> bool {
    !art.tried
}

/// How a piece of art fills its node. Texture coordinates are FrameXML's `TexCoords` (left,
/// right, top, bottom, 0 to 1).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Fit {
    /// The `tc` region, stretched.
    Stretch([f32; 4]),
    /// The `tc` region with caps `cap` of the texture's width wide kept in proportion, the middle
    /// stretched (a button, a tab, the header).
    Caps([f32; 4], f32),
    /// A nine-slice backdrop edge drawn `edge` units thick, its runs tiling.
    Edge(f32),
    /// A background tiled every `period` units.
    Tile(f32),
}

/// FrameXML's whole texture.
pub const FULL: [f32; 4] = [0.0, 1.0, 0.0, 1.0];

/// A node drawing a piece of art over (or, without the art, instead of) its fallback colours:
/// `fill` its background and `edge` its border.
#[derive(Component, Clone, Copy, Debug, PartialEq)]
pub struct ArtLayer {
    pub art: Art,
    pub fit: Fit,
    pub fill: Color,
    pub edge: Color,
}

impl ArtLayer {
    pub fn new(art: Art, fit: Fit) -> Self {
        Self {
            art,
            fit,
            fill: Color::NONE,
            edge: Color::NONE,
        }
    }

    pub fn fallback(mut self, fill: Color, edge: Color) -> Self {
        self.fill = fill;
        self.edge = edge;
        self
    }
}

fn tc_rect(tc: [f32; 4], size: Vec2) -> Rect {
    Rect::new(
        tc[0] * size.x,
        tc[2] * size.y,
        tc[1] * size.x,
        tc[3] * size.y,
    )
}

/// The image region and mode a fit takes on a texture of `size` texels, at `unit` pixels per UI
/// unit.
pub fn image_mode(fit: Fit, size: Vec2, unit: f32) -> (Option<Rect>, NodeImageMode) {
    match fit {
        Fit::Stretch(tc) => (Some(tc_rect(tc, size)), NodeImageMode::Stretch),
        Fit::Caps(tc, cap) => {
            let cap = cap * size.x;
            (
                Some(tc_rect(tc, size)),
                NodeImageMode::Sliced(TextureSlicer {
                    border: BorderRect {
                        min_inset: Vec2::new(cap, 0.0),
                        max_inset: Vec2::new(cap, 0.0),
                    },
                    center_scale_mode: SliceScaleMode::Stretch,
                    sides_scale_mode: SliceScaleMode::Stretch,
                    // The caps follow the node's height (the slicer's own min ratio).
                    max_corner_scale: 64.0,
                }),
            )
        }
        Fit::Edge(edge) => {
            let e = (size.y / 3.0).max(1.0);
            let s = edge * unit / e;
            (
                None,
                NodeImageMode::Sliced(TextureSlicer {
                    border: BorderRect::all(e),
                    center_scale_mode: SliceScaleMode::Stretch,
                    sides_scale_mode: SliceScaleMode::Tile { stretch_value: s },
                    max_corner_scale: s,
                }),
            )
        }
        Fit::Tile(period) => (
            None,
            NodeImageMode::Tiled {
                tile_x: true,
                tile_y: true,
                stretch_value: period * unit / size.x.max(1.0),
            },
        ),
    }
}

/// Dress every new or changed [`ArtLayer`] (and all of them once the art arrives): the art when
/// the install has it, else the fallback colours.
fn skin(
    art: Res<WowArt>,
    mut layers: Query<(
        Ref<ArtLayer>,
        &mut ImageNode,
        &mut BackgroundColor,
        &mut BorderColor,
    )>,
) {
    let all = art.is_changed();
    for (layer, mut img, mut bg, mut border) in &mut layers {
        if !all && !layer.is_changed() {
            continue;
        }
        match art.get(layer.art) {
            Some((handle, size)) => {
                let (rect, mode) = image_mode(layer.fit, *size, unit());
                if img.image != *handle {
                    img.image = handle.clone();
                }
                img.rect = rect;
                img.image_mode = mode;
                bg.set_if_neq(BackgroundColor(Color::NONE));
                border.set_if_neq(BorderColor::all(Color::NONE));
            }
            None => {
                bg.set_if_neq(BackgroundColor(layer.fill));
                border.set_if_neq(BorderColor::all(layer.edge));
            }
        }
    }
}

/// A full-size node under its parent's content for a piece of art.
fn layer_node() -> Node {
    Node {
        position_type: PositionType::Absolute,
        left: px(0),
        top: px(0),
        width: percent(100),
        height: percent(100),
        ..default()
    }
}

// ── Text ─────────────────────────────────────────────────────────────────────────────────────────

/// A font string: `size` UI units of Friz Quadrata in `color`, with the stock fonts' drop shadow.
pub fn text(font: &Handle<Font>, s: &str, size: f32, color: Color) -> impl Bundle + use<> {
    (
        Text::new(s),
        TextFont::from_font_size(uf(size)).with_font(font.clone()),
        TextColor(color),
        TextShadow {
            offset: Vec2::splat(uf(1.0).max(1.0)),
            color: Color::srgba(0.0, 0.0, 0.0, 0.85),
        },
        Pickable::IGNORE,
    )
}

// ── Buttons ──────────────────────────────────────────────────────────────────────────────────────

/// Which stock button a [`WowButton`] is.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Kind {
    /// `UIPanelButtonTemplate`.
    #[default]
    Panel,
    /// `CharacterFrameTabButtonTemplate`.
    Tab,
    /// `UIPanelCloseButton`.
    Close,
    /// A list line with the quest log's highlight.
    Row,
    ScrollUp,
    ScrollDown,
    /// A movable window's reset button, beside its close button.
    Reset,
}

/// A stock-looking button's state, which its window sets: `selected` is `LockHighlight` (a
/// tab's active state, a list's chosen line), `disabled` greys it.
#[derive(Component, Clone, Copy, Debug, Default, PartialEq)]
pub struct WowButton {
    pub selected: bool,
    pub disabled: bool,
    pub kind: Kind,
}

impl WowButton {
    pub fn new(kind: Kind) -> Self {
        Self { kind, ..default() }
    }
}

/// A button's face (its normal, pushed or disabled texture).
#[derive(Component)]
struct Face;

/// A button's highlight texture.
#[derive(Component)]
struct Glow;

/// A button's caption, which takes the stock font colours for its state.
#[derive(Component)]
pub struct ButtonText;

/// The caption's colour, the face, whether the highlight shows and the plain colours without
/// art, for a button in this state.
fn looks(b: &WowButton, i: Interaction) -> (Option<Art>, bool, Color, Color) {
    let hot = i != Interaction::None;
    let pressed = i == Interaction::Pressed;
    match b.kind {
        Kind::Panel => {
            let face = if b.disabled {
                Art::ButtonDisabled
            } else if pressed {
                Art::ButtonDown
            } else {
                Art::ButtonUp
            };
            let caption = if b.disabled {
                GREY
            } else if hot || b.selected {
                WHITE
            } else {
                GOLD
            };
            let fill = if b.disabled {
                Color::srgba(0.16, 0.13, 0.12, 0.9)
            } else if pressed {
                Color::srgba(0.25, 0.03, 0.02, 1.0)
            } else if hot {
                Color::srgba(0.55, 0.1, 0.05, 1.0)
            } else if b.selected {
                Color::srgba(0.5, 0.3, 0.04, 0.95)
            } else {
                Color::srgba(0.4, 0.05, 0.03, 0.95)
            };
            (
                Some(face),
                (hot || b.selected) && !b.disabled,
                caption,
                fill,
            )
        }
        Kind::Tab => {
            let face = if b.selected {
                Art::TabActive
            } else {
                Art::TabInactive
            };
            let caption = if b.disabled {
                GREY
            } else if b.selected || hot {
                WHITE
            } else {
                GOLD
            };
            let fill = if b.selected {
                Color::srgba(0.18, 0.15, 0.1, 0.95)
            } else if hot {
                Color::srgba(0.12, 0.1, 0.07, 0.95)
            } else {
                Color::srgba(0.05, 0.05, 0.05, 0.9)
            };
            (Some(face), hot && !b.selected && !b.disabled, caption, fill)
        }
        Kind::Close => {
            let face = if pressed {
                Art::CloseDown
            } else {
                Art::CloseUp
            };
            let fill = if hot {
                Color::srgba(0.6, 0.1, 0.05, 1.0)
            } else {
                Color::srgba(0.4, 0.05, 0.03, 0.95)
            };
            (Some(face), hot, WHITE, fill)
        }
        Kind::Reset => {
            let face = if pressed {
                Art::ResetDown
            } else {
                Art::ResetUp
            };
            let fill = if hot {
                Color::srgba(0.45, 0.35, 0.1, 1.0)
            } else {
                Color::srgba(0.25, 0.2, 0.08, 0.9)
            };
            (Some(face), hot, GOLD, fill)
        }
        Kind::Row => {
            let caption = if b.disabled {
                GREY
            } else if hot || b.selected {
                WHITE
            } else {
                GOLD
            };
            let fill = if b.selected {
                GOLD.with_alpha(0.25)
            } else if hot && !b.disabled {
                GOLD.with_alpha(0.12)
            } else {
                Color::NONE
            };
            (None, (hot || b.selected) && !b.disabled, caption, fill)
        }
        Kind::ScrollUp | Kind::ScrollDown => {
            let up = b.kind == Kind::ScrollUp;
            let face = match (b.disabled, pressed, up) {
                (true, _, true) => Art::ScrollUpDisabled,
                (true, _, false) => Art::ScrollDownDisabled,
                (false, true, true) => Art::ScrollUpDown,
                (false, true, false) => Art::ScrollDownDown,
                (false, false, true) => Art::ScrollUpUp,
                (false, false, false) => Art::ScrollDownUp,
            };
            let fill = if b.disabled {
                Color::srgba(0.2, 0.2, 0.2, 0.6)
            } else if hot {
                Color::srgba(0.6, 0.45, 0.1, 0.95)
            } else {
                Color::srgba(0.45, 0.32, 0.06, 0.9)
            };
            let caption = if b.disabled { GREY } else { GOLD };
            (Some(face), false, caption, fill)
        }
    }
}

/// Each button's face, highlight and caption colour for its state.
fn button_visuals(
    art: Res<WowArt>,
    buttons: Query<(&WowButton, &Interaction, &Children)>,
    mut faces: Query<&mut ArtLayer, (With<Face>, Without<Glow>)>,
    mut glows: Query<(&mut ArtLayer, &mut ImageNode), (With<Glow>, Without<Face>)>,
    mut captions: Query<(&mut TextColor, &mut Visibility), With<ButtonText>>,
) {
    for (b, i, children) in &buttons {
        let (face, glow, caption, fill) = looks(b, *i);
        for c in children.iter() {
            if let Ok(mut layer) = faces.get_mut(c) {
                // A missing disabled face falls back to the normal one, greyed by the caption.
                let want = face
                    .filter(|f| art.has(*f) || !art.has(Art::ButtonUp))
                    .unwrap_or(match b.kind {
                        Kind::Panel => Art::ButtonUp,
                        _ => layer.art,
                    });
                if layer.art != want || layer.fill != fill {
                    layer.art = want;
                    layer.fill = fill;
                }
            }
            if let Ok((mut layer, mut img)) = glows.get_mut(c) {
                let a = if glow { 1.0 } else { 0.0 };
                if img.color.alpha() != a {
                    img.color.set_alpha(a);
                }
                // A list line without art shows its state as a fill.
                let want = if b.kind == Kind::Row {
                    fill
                } else {
                    Color::NONE
                };
                if layer.fill != want {
                    layer.fill = want;
                }
            }
            if let Ok((mut t, mut vis)) = captions.get_mut(c) {
                if t.0 != caption {
                    t.0 = caption;
                }
                // Without art the close button's X (the reset button's R) is its caption.
                let shown = if (b.kind == Kind::Close && art.has(Art::CloseUp))
                    || (b.kind == Kind::Reset && art.has(Art::ResetUp))
                {
                    Visibility::Hidden
                } else {
                    Visibility::Inherited
                };
                if *vis != shown {
                    *vis = shown;
                }
            }
        }
    }
}

/// Something children spawn from: a `with_children` builder, or a bundle's `SpawnWith` spawner.
pub trait SpawnChild {
    fn spawn_child(&mut self, bundle: impl Bundle);
}

impl SpawnChild for ChildSpawnerCommands<'_> {
    fn spawn_child(&mut self, bundle: impl Bundle) {
        self.spawn(bundle);
    }
}

impl SpawnChild for ChildSpawner<'_> {
    fn spawn_child(&mut self, bundle: impl Bundle) {
        self.spawn(bundle);
    }
}

/// A face and a highlight under a button's caption.
fn faces(b: &mut impl SpawnChild, face: Option<(Art, Fit)>, glow: Option<(Art, Fit)>) {
    if let Some((art, fit)) = face {
        b.spawn_child((
            Face,
            ArtLayer::new(art, fit),
            layer_node(),
            ImageNode::default(),
            Pickable::IGNORE,
        ));
    }
    if let Some((art, fit)) = glow {
        b.spawn_child((
            Glow,
            ArtLayer::new(art, fit),
            layer_node(),
            ImageNode {
                color: Color::WHITE.with_alpha(0.0),
                ..default()
            },
            Pickable::IGNORE,
        ));
    }
}

/// The quest log's highlight on a list line built by hand (a [`Kind::Row`] button with content of
/// its own); spawn it first, under the content.
pub fn row_glow(b: &mut ChildSpawnerCommands) {
    faces(b, None, Some((Art::RowHighlight, Fit::Stretch(FULL))));
}

/// `UIPanelButtonTemplate`'s region of its textures (80×22 of 128×32), caps 12 texels wide.
const PANEL_TC: [f32; 4] = [0.0, 0.625, 0.0, 0.6875];
const PANEL_CAP: f32 = 0.09375;

/// A stock button: its state, its node, `extra` (its action), and under the caption its face and
/// highlight.
fn stock_button<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    size: f32,
    state: WowButton,
    node: Node,
    extra: B,
    face: Option<(Art, Fit)>,
    glow: Option<(Art, Fit)>,
) -> impl Bundle + use<B> {
    let font = font.clone();
    let label = label.to_owned();
    (
        Button,
        state,
        node,
        extra,
        Children::spawn(SpawnWith(move |b: &mut ChildSpawner| {
            faces(b, face, glow);
            b.spawn((text(&font, &label, size, GOLD), ButtonText));
        })),
    )
}

fn panel_faces() -> (Option<(Art, Fit)>, Option<(Art, Fit)>) {
    (
        Some((Art::ButtonUp, Fit::Caps(PANEL_TC, PANEL_CAP))),
        Some((Art::ButtonHighlight, Fit::Caps(PANEL_TC, PANEL_CAP))),
    )
}

/// A red panel button `w`×`h` units with a centred caption; `extra` is its action.
pub fn button_sized<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    w: f32,
    h: f32,
    extra: B,
) -> impl Bundle + use<B> {
    let (face, glow) = panel_faces();
    stock_button(
        font,
        label,
        12.0,
        WowButton::new(Kind::Panel),
        Node {
            width: u(w),
            height: u(h),
            margin: UiRect::all(u(1.0)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
        extra,
        face,
        glow,
    )
}

/// A red panel button 22 units tall, as wide as its caption needs, in the given state.
pub fn button_fit<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    state: WowButton,
    extra: B,
) -> impl Bundle + use<B> {
    let (face, glow) = panel_faces();
    stock_button(
        font,
        label,
        12.0,
        state,
        Node {
            min_width: u(60.0),
            height: u(22.0),
            padding: UiRect::horizontal(u(14.0)),
            margin: UiRect::all(u(1.0)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
        extra,
        face,
        glow,
    )
}

/// A red panel button 22 units tall.
pub fn button<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    w: f32,
    extra: B,
) -> impl Bundle + use<B> {
    button_sized(font, label, w, 22.0, extra)
}

/// The character frame's tab textures: caps 20 texels of 128, the art's full height.
const TAB_CAP: f32 = 0.15625;

/// A tab for under a window (`CharacterFrameTabButtonTemplate`): gold caption, white when it is
/// the shown page.
pub fn tab<B: Bundle>(font: &Handle<Font>, label: &str, extra: B) -> impl Bundle + use<B> {
    stock_button(
        font,
        label,
        10.0,
        WowButton::new(Kind::Tab),
        Node {
            min_width: u(70.0),
            height: u(32.0),
            padding: UiRect::new(u(20.0), u(20.0), u(0.0), u(4.0)),
            margin: UiRect::right(u(-14.0)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
        extra,
        Some((Art::TabInactive, Fit::Caps(FULL, TAB_CAP))),
        Some((Art::TabHighlight, Fit::Stretch(FULL))),
    )
}

/// A list line `w` units wide (`h` tall) with the quest log's highlight, its caption to the left.
pub fn row_button<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    w: f32,
    h: f32,
    extra: B,
) -> impl Bundle + use<B> {
    stock_button(
        font,
        label,
        12.0,
        WowButton::new(Kind::Row),
        Node {
            width: u(w),
            height: u(h),
            padding: UiRect::horizontal(u(6.0)),
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
        extra,
        None,
        Some((Art::RowHighlight, Fit::Stretch(FULL))),
    )
}

/// `UIPanelCloseButton`, 32 units at the frame's top right; without art, an X.
fn close_button<B: Bundle>(font: &Handle<Font>, extra: B) -> impl Bundle + use<B> {
    stock_button(
        font,
        "X",
        14.0,
        WowButton::new(Kind::Close),
        Node {
            position_type: PositionType::Absolute,
            right: u(2.0),
            top: u(2.0),
            width: u(32.0),
            height: u(32.0),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            ..default()
        },
        extra,
        Some((Art::CloseUp, Fit::Stretch(FULL))),
        Some((Art::CloseHighlight, Fit::Stretch(FULL))),
    )
}

/// A movable window's reset button: the character model's turn-left arrow, 20 units, in the
/// frame's top right left of the close button (`right` units in from the edge); without art, an R.
fn reset_button(font: &Handle<Font>, root: Entity, right: f32) -> impl Bundle {
    stock_button(
        font,
        "R",
        11.0,
        WowButton::new(Kind::Reset),
        Node {
            position_type: PositionType::Absolute,
            right: u(right),
            top: u(8.0),
            width: u(20.0),
            height: u(20.0),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            ..default()
        },
        (
            ResetPosition(root),
            WowTip::new(
                "Reset Position",
                "Puts this window back where it opens by default.",
            ),
        ),
        Some((Art::ResetUp, Fit::Stretch(FULL))),
        Some((Art::RoundHighlight, Fit::Stretch(FULL))),
    )
}

// ── Windows ──────────────────────────────────────────────────────────────────────────────────────

/// The frame a window wears.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Frame {
    /// `UI-DialogBox-Border` on `UI-DialogBox-Background`, edge 32, insets 11/12.
    Dialog,
    /// `UI-Tooltip-Border` on `UI-Tooltip-Background`, edge 16, insets 5 (`GameTooltip`).
    Tooltip,
    /// An inset well inside a window (the auction house's and trade skill frame's lists): the
    /// tooltip background darkened, its border thinner and greyed.
    Inset,
}

/// Where a window sits before it is moved: the point `at` (fractions of the screen) under the
/// window's point `pivot` (fractions of the window), `offset` UI units away.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Anchor {
    pub at: Vec2,
    pub pivot: Vec2,
    pub offset: Vec2,
}

impl Anchor {
    pub const CENTER: Self = Self {
        at: Vec2::splat(0.5),
        pivot: Vec2::splat(0.5),
        offset: Vec2::ZERO,
    };

    /// The window's top-left for a `size` window on a `viewport`, `seam` pixels per unit.
    pub fn place(&self, viewport: Vec2, size: Vec2, seam: f32) -> Vec2 {
        viewport * self.at - size * self.pivot + self.offset * seam
    }
}

/// What a window is, for [`spawn_window`].
pub struct WindowSpec {
    /// Its name, which keys where it was moved to.
    pub key: &'static str,
    /// The header's title; empty for none.
    pub title: String,
    pub frame: Frame,
    /// Its width in units; `None` sizes it to its content.
    pub width: Option<f32>,
    pub movable: bool,
    pub anchor: Anchor,
    /// Its `GlobalZIndex`; the window last taken up draws over all.
    pub z: i32,
}

/// A window's root.
#[derive(Component, Debug)]
pub struct WowWindow {
    pub key: &'static str,
    pub movable: bool,
    pub anchor: Anchor,
    pub z: i32,
    /// The unit it was built at.
    built_unit: f32,
    /// Its top-left on screen as last placed, logical pixels.
    pos: Option<Vec2>,
}

/// The frame inside a window's root (the root also holds the tabs under it).
#[derive(Component)]
pub struct WowFrame;

/// Pressing here drags the window `0`.
#[derive(Component)]
struct DragHandle(Entity);

/// Pressing here puts the window `0` back at its default place, forgetting where it was moved.
#[derive(Component)]
pub struct ResetPosition(pub Entity);

/// The window's frame padding: the backdrop's inset and a little air, more on top for a header.
fn frame_padding(frame: Frame, header: bool) -> UiRect {
    match frame {
        Frame::Dialog => UiRect::new(
            u(18.0),
            u(18.0),
            if header { u(30.0) } else { u(18.0) },
            u(16.0),
        ),
        Frame::Tooltip => UiRect::all(u(9.0)),
        Frame::Inset => UiRect::all(u(INSET_PAD)),
    }
}

/// A frame's backdrop: its tiled background inside the insets and its edge, under everything.
fn backdrop(f: &mut ChildSpawnerCommands, frame: Frame) {
    let (base, edge_tint) = match frame {
        Frame::Inset => (0.0, Color::srgb(0.65, 0.65, 0.65)),
        _ => (0.85, Color::WHITE),
    };
    let (bg, period, inset, tint, border, edge, fill, line) = match frame {
        Frame::Dialog => (
            Art::DialogBg,
            32.0,
            [11.0, 12.0, 12.0, 11.0],
            Color::WHITE,
            Art::DialogBorder,
            32.0,
            Color::srgba(0.03, 0.03, 0.04, 0.92),
            GOLD.with_alpha(0.7),
        ),
        Frame::Tooltip => (
            Art::TooltipBg,
            16.0,
            [4.0, 4.0, 4.0, 4.0],
            // `TOOLTIP_DEFAULT_BACKGROUND_COLOR`.
            Color::srgba(0.09, 0.09, 0.19, 0.92),
            Art::TooltipBorder,
            16.0,
            Color::srgba(0.05, 0.05, 0.1, 0.9),
            Color::srgba(0.6, 0.6, 0.6, 0.8),
        ),
        Frame::Inset => (
            Art::TooltipBg,
            16.0,
            [3.0, 3.0, 3.0, 3.0],
            Color::srgba(0.0, 0.0, 0.0, 0.55),
            Art::TooltipBorder,
            12.0,
            Color::srgba(0.0, 0.0, 0.0, 0.45),
            Color::srgba(0.5, 0.5, 0.5, 0.7),
        ),
    };
    let inside = Node {
        position_type: PositionType::Absolute,
        left: u(inset[0]),
        right: u(inset[1]),
        top: u(inset[2]),
        bottom: u(inset[3]),
        ..default()
    };
    // The stock background tile is mostly clear; under it a dark base, so the window reads as a
    // panel over the world the way the stock dialogs do, not a tint.
    if base > 0.0 {
        f.spawn((
            inside.clone(),
            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, base)),
            Pickable::IGNORE,
        ));
    }
    f.spawn((
        ArtLayer::new(bg, Fit::Tile(period)).fallback(fill, Color::NONE),
        inside,
        ImageNode {
            color: tint,
            ..default()
        },
        Pickable::IGNORE,
    ));
    f.spawn((
        ArtLayer::new(border, Fit::Edge(edge)).fallback(Color::NONE, line),
        Node {
            border: UiRect::all(px(if frame == Frame::Inset { 1 } else { 2 })),
            ..layer_node()
        },
        ImageNode {
            color: edge_tint,
            ..default()
        },
        Pickable::IGNORE,
    ));
}

/// A dialog's backdrop (`UI-DialogBox-Border` on its background) under a node the caller places
/// itself: a dropdown's list (`DropDownList1`'s) or a popup inside a window.
pub fn dialog_backdrop(f: &mut ChildSpawnerCommands) {
    backdrop(f, Frame::Dialog);
}

/// `UIDropDownMenuTemplate`'s visible band of its label frame (the texture is 64 texels tall, the
/// box about half of it) and its caps (25 of 128 texels each side).
const DROPDOWN_TC: [f32; 4] = [0.0, 1.0, 0.22, 0.78];
const DROPDOWN_CAP: f32 = 0.1953125;

/// The text of a dropdown box (the chosen item), which its window sets.
#[derive(Component)]
pub struct DropDownText;

/// A dropdown box `w` units wide (`UIDropDownMenuTemplate`): the label frame, the chosen item's
/// text (carrying `text_extra`) and the down arrow at its right. The whole box is one button
/// carrying `extra` (its action: open or close the list).
pub fn dropdown_box<B: Bundle, T: Bundle>(
    p: &mut ChildSpawnerCommands,
    font: &Handle<Font>,
    w: f32,
    text_extra: T,
    extra: B,
) -> Entity {
    p.spawn((
        Button,
        WowButton::new(Kind::Row),
        Node {
            width: u(w),
            height: u(28.0),
            padding: UiRect::new(u(18.0), u(26.0), u(0.0), u(0.0)),
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            overflow: Overflow::clip_x(),
            ..default()
        },
        extra,
    ))
    .with_children(|b| {
        b.spawn((
            ArtLayer::new(Art::DropDownFrame, Fit::Caps(DROPDOWN_TC, DROPDOWN_CAP))
                .fallback(Color::srgba(0.0, 0.0, 0.0, 0.6), GREY.with_alpha(0.8)),
            Node {
                border: UiRect::all(px(1)),
                ..layer_node()
            },
            ImageNode::default(),
            Pickable::IGNORE,
        ));
        b.spawn((text(font, "", BODY_SIZE, WHITE), DropDownText, text_extra));
        // The arrow: the scroll bar's down button, as the stock box's own.
        b.spawn((
            ArtLayer::new(Art::ScrollDownUp, Fit::Stretch([0.25, 0.75, 0.25, 0.75]))
                .fallback(Color::srgba(0.45, 0.32, 0.06, 0.9), Color::NONE),
            Node {
                position_type: PositionType::Absolute,
                right: u(4.0),
                top: u(4.0),
                width: u(20.0),
                height: u(20.0),
                ..default()
            },
            ImageNode::default(),
            Pickable::IGNORE,
        ));
    })
    .id()
}

/// A dropdown's list (`DropDownList1`): a dialog-bordered column `w` units wide, hidden until
/// its box is pressed, hanging under the box (`top` units below its parent's top) over what
/// follows. `extra` rides on it; `items` fills it (rows from [`dropdown_item`]).
pub fn dropdown_list(
    p: &mut ChildSpawnerCommands,
    w: f32,
    top: f32,
    extra: impl Bundle,
    items: impl FnOnce(&mut ChildSpawnerCommands),
) -> Entity {
    p.spawn((
        Node {
            position_type: PositionType::Absolute,
            left: u(0.0),
            top: u(top),
            width: u(w),
            flex_direction: FlexDirection::Column,
            padding: UiRect::new(u(14.0), u(14.0), u(14.0), u(14.0)),
            display: Display::None,
            ..default()
        },
        ZIndex(100),
        FocusPolicy::Block,
        extra,
    ))
    .with_children(|l| {
        dialog_backdrop(l);
        items(l);
    })
    .id()
}

/// One line of a dropdown's list (`UIDropDownMenuButtonTemplate`): white text, the quest log's
/// highlight under the pointer; `extra` is its action.
pub fn dropdown_item<B: Bundle>(
    font: &Handle<Font>,
    label: &str,
    w: f32,
    extra: B,
) -> impl Bundle + use<B> {
    stock_button(
        font,
        label,
        BODY_SIZE,
        WowButton::new(Kind::Row),
        Node {
            width: u(w),
            height: u(18.0),
            padding: UiRect::horizontal(u(6.0)),
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
        extra,
        None,
        Some((Art::RowHighlight, Fit::Stretch(FULL))),
    )
}

/// A thin rule between a dropdown's groups of lines.
pub fn dropdown_separator(p: &mut ChildSpawnerCommands) {
    p.spawn((
        Node {
            height: px(1),
            margin: UiRect::vertical(u(3.0)),
            align_self: AlignSelf::Stretch,
            ..default()
        },
        BackgroundColor(GREY.with_alpha(0.6)),
        Pickable::IGNORE,
    ));
}

/// An inset well (the stock lists' sunken panels) holding `content`: `node` is its layout
/// (padded by [`INSET_PAD`] when it sets no padding of its own), `extra` rides on it. Returns it.
pub fn inset(
    p: &mut ChildSpawnerCommands,
    node: Node,
    extra: impl Bundle,
    content: impl FnOnce(&mut ChildSpawnerCommands),
) -> Entity {
    let padding = if node.padding == UiRect::default() {
        UiRect::all(u(INSET_PAD))
    } else {
        node.padding
    };
    p.spawn((Node { padding, ..node }, extra))
        .with_children(|c| {
            backdrop(c, Frame::Inset);
            content(c);
        })
        .id()
}

/// The rule under a section's title: `UI-DialogBox-Divider`'s ends kept in proportion, its middle
/// stretched across the column; without the art, a thin gold line.
const DIVIDER_H: f32 = 16.0;
const DIVIDER_CAP: f32 = 0.25;

/// A section's header, as the stock panels head a group: its title in gold over the divider,
/// the whole width of its column. Returns the header.
pub fn section_header(p: &mut ChildSpawnerCommands, font: &Handle<Font>, title: &str) -> Entity {
    p.spawn(Node {
        flex_direction: FlexDirection::Column,
        align_self: AlignSelf::Stretch,
        flex_shrink: 0.0,
        margin: UiRect::new(u(0.0), u(0.0), u(SECTION_GAP * 0.5), u(0.0)),
        ..default()
    })
    .with_children(|h| {
        h.spawn((
            Node {
                margin: UiRect::left(u(2.0)),
                ..default()
            },
            children![text(font, title, BODY_SIZE, GOLD)],
        ));
        h.spawn((
            ArtLayer::new(Art::Divider, Fit::Caps(FULL, DIVIDER_CAP))
                .fallback(Color::NONE, GOLD.with_alpha(0.45)),
            Node {
                width: percent(100),
                height: u(DIVIDER_H * 0.5),
                margin: UiRect::top(u(1.0)),
                border: UiRect::top(px(1)),
                ..default()
            },
            ImageNode::default(),
            Pickable::IGNORE,
        ));
    })
    .id()
}

/// An icon `size` units square in a thin border (a quality's colour, or the slot's dark edge),
/// as the stock lists frame item and spell icons; `None` leaves the empty slot.
pub fn icon_frame(image: Option<Handle<Image>>, size: f32, border: Color) -> impl Bundle {
    icon_frame_lit(image, size, border, true)
}

/// [`icon_frame`], greyed when not `lit` (what is not had or not earned yet).
pub fn icon_frame_lit(
    image: Option<Handle<Image>>,
    size: f32,
    border: Color,
    lit: bool,
) -> impl Bundle {
    let shown = image.is_some();
    let tint = if lit {
        Color::WHITE
    } else {
        Color::srgb(0.4, 0.4, 0.4)
    };
    (
        Node {
            width: u(size),
            height: u(size),
            border: UiRect::all(px(1)),
            flex_shrink: 0.0,
            ..default()
        },
        BorderColor::all(border),
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.6)),
        ImageNode {
            image: image.unwrap_or_default(),
            color: if shown { tint } else { Color::NONE },
            ..default()
        },
        Pickable::IGNORE,
    )
}

/// The dark edge of an icon with no quality to show.
pub const SLOT_EDGE: Color = Color::srgba(0.0, 0.0, 0.0, 0.9);

/// A `GameTooltip`'s backdrop under a node the caller places itself (a tooltip that follows the
/// pointer inside a window, rather than a window of its own).
pub fn tooltip_backdrop(f: &mut ChildSpawnerCommands) {
    backdrop(f, Frame::Tooltip);
}

// ── Tooltips ─────────────────────────────────────────────────────────────────────────────────────

/// One line of a tooltip.
#[derive(Debug, Clone, PartialEq)]
pub struct TipLine {
    pub text: String,
    pub color: Color,
    pub size: f32,
}

impl TipLine {
    pub fn new(text: impl Into<String>, color: Color, size: f32) -> Self {
        Self {
            text: text.into(),
            color,
            size,
        }
    }
}

/// A tooltip's width in UI units (`GameTooltip`'s for a spell).
pub const TIP_WIDTH: f32 = 270.0;

/// What a control says when the pointer rests on it, beside the pointer in `GameTooltip`'s look.
/// Put it on anything with an [`Interaction`] (every stock-looking button has one).
#[derive(Component, Clone, Debug, Default, PartialEq)]
pub struct WowTip(pub Vec<TipLine>);

impl WowTip {
    /// A control's name in white and what it does in gold, as the stock newbie tooltips
    /// (`GameTooltip_AddNewbieTip`).
    pub fn new(title: &str, body: &str) -> Self {
        let mut lines = vec![TipLine::new(title, WHITE, 13.0)];
        if !body.is_empty() {
            lines.push(TipLine::new(body, GOLD, SMALL_SIZE));
        }
        Self(lines)
    }
}

/// A tooltip's body under a node the caller places: the backdrop, then each line (wrapping at
/// the tooltip's width).
pub fn tip_body(t: &mut ChildSpawnerCommands, font: &Handle<Font>, lines: &[TipLine]) {
    tooltip_backdrop(t);
    for l in lines {
        t.spawn((
            Node {
                max_width: u(TIP_WIDTH - 18.0),
                ..default()
            },
            children![text(font, &l.text, l.size, l.color)],
        ));
    }
}

/// A tooltip's lines with their `{WOMW_X}` keys filled in as bound now (crate::keybinds), so a
/// tip names the key the player pressed, not the default.
pub fn tip_keys(lines: &[TipLine], keymap: Option<&crate::keybinds::Keymap>) -> Vec<TipLine> {
    lines
        .iter()
        .map(|l| match keymap {
            Some(k) if l.text.contains('{') => TipLine {
                text: crate::keybinds::fill_keys(&l.text, k),
                ..l.clone()
            },
            _ => l.clone(),
        })
        .collect()
}

/// The one tooltip the [`WowTip`]s share, over every window.
#[derive(Component)]
struct TipRoot;

/// The hovered [`WowTip`] beside the pointer: right of and under it, or flipped where the
/// screen's edge comes first. Hidden while the gun has the mouse.
#[allow(clippy::type_complexity)]
fn show_tip(
    mut commands: Commands,
    assets: Option<Res<AssetServer>>,
    state: Option<Res<FpsState>>,
    window: Query<&Window, With<PrimaryWindow>>,
    tips: Query<(Entity, &Interaction, &WowTip, &InheritedVisibility)>,
    keymap: Option<Res<crate::keybinds::Keymap>>,
    mut roots: Query<(Entity, &mut Node, &ComputedNode, &mut Visibility), With<TipRoot>>,
    mut shown: Local<Option<(Entity, Vec<TipLine>)>>,
) {
    let owns = state.is_some_and(|s| s.owns_input);
    let hovered = tips
        .iter()
        .filter(|(_, i, tip, v)| **i != Interaction::None && v.get() && !tip.0.is_empty())
        .map(|(e, _, tip, _)| (e, tip))
        .next()
        .filter(|_| !owns);
    let Ok((root, mut node, size, mut vis)) = roots.single_mut() else {
        if hovered.is_some() {
            // Spawned the first time it is wanted; it shows from the next frame.
            commands.spawn((
                Name::new("layer tooltip"),
                TipRoot,
                Node {
                    position_type: PositionType::Absolute,
                    flex_direction: FlexDirection::Column,
                    padding: UiRect::all(u(9.0)),
                    row_gap: u(2.0),
                    max_width: u(TIP_WIDTH),
                    ..default()
                },
                GlobalZIndex(i32::MAX),
                Visibility::Hidden,
                Pickable::IGNORE,
            ));
        }
        return;
    };
    let Some((source, tip)) = hovered else {
        *shown = None;
        if *vis != Visibility::Hidden {
            *vis = Visibility::Hidden;
        }
        return;
    };
    if shown
        .as_ref()
        .is_none_or(|(e, l)| *e != source || *l != tip.0)
    {
        *shown = Some((source, tip.0.clone()));
        let Some(assets) = assets else {
            return;
        };
        let font: Handle<Font> = assets.load(FONT);
        let lines = tip_keys(&tip.0, keymap.as_deref());
        commands
            .entity(root)
            .despawn_related::<Children>()
            .with_children(|t| tip_body(t, &font, &lines));
        // Laid out this frame; placed and shown on the next.
        if *vis != Visibility::Hidden {
            *vis = Visibility::Hidden;
        }
        return;
    }
    let Some(cursor) = window.single().ok().and_then(Window::cursor_position) else {
        return;
    };
    let viewport = window
        .single()
        .map_or(Vec2::ZERO, |w| Vec2::new(w.width(), w.height()));
    let tip_size = size.size * size.inverse_scale_factor;
    let pos = tip_position(cursor, tip_size, viewport, uf(16.0));
    if node.left != Val::Px(pos.x) {
        node.left = Val::Px(pos.x);
    }
    if node.top != Val::Px(pos.y) {
        node.top = Val::Px(pos.y);
    }
    if *vis != Visibility::Inherited {
        *vis = Visibility::Inherited;
    }
}

/// A tooltip's top-left beside the pointer: `gap` right of and under it, or left of and above it
/// where it would leave the screen; always on it.
pub fn tip_position(cursor: Vec2, size: Vec2, viewport: Vec2, gap: f32) -> Vec2 {
    let x = if cursor.x + gap + size.x > viewport.x {
        cursor.x - gap - size.x
    } else {
        cursor.x + gap
    };
    let y = if cursor.y + gap + size.y > viewport.y {
        cursor.y - gap - size.y
    } else {
        cursor.y + gap
    };
    clamp_position(Vec2::new(x, y), size, viewport).round()
}

/// `UI-DialogBox-Header`'s banner (AceGUI's crop of it), its caps a tenth of the texture.
const HEADER_TC: [f32; 4] = [0.21, 0.77, 0.0, 0.63];
const HEADER_CAP: f32 = 0.1;

/// Spawn a window: the root (carrying `root_extra`), its frame with the backdrop, the header and,
/// when `close` is given, the close button carrying it (the window's own close action); then
/// `content` inside the frame and `below` under it (its tabs). Returns the root.
pub fn spawn_window(
    commands: &mut Commands,
    font: &Handle<Font>,
    spec: WindowSpec,
    root_extra: impl Bundle,
    close: Option<impl Bundle>,
    content: impl FnOnce(&mut ChildSpawnerCommands),
    below: impl FnOnce(&mut ChildSpawnerCommands),
) -> Entity {
    let header = !spec.title.is_empty();
    let root = commands
        .spawn((
            Name::new(spec.key),
            WowWindow {
                key: spec.key,
                movable: spec.movable,
                anchor: spec.anchor,
                z: spec.z,
                built_unit: unit(),
                pos: None,
            },
            Node {
                position_type: PositionType::Absolute,
                left: px(0),
                top: px(0),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::FlexStart,
                ..default()
            },
            // Unseen until it is placed after its first layout.
            UiTransform::from_scale(Vec2::ZERO),
            RelativeCursorPosition::default(),
            GlobalZIndex(if spec.movable { spec.z.saturating_sub(1) } else { spec.z }),
            root_extra,
        ))
        .id();
    commands.entity(root).with_children(|r| {
        r.spawn((
            WowFrame,
            Node {
                flex_direction: FlexDirection::Column,
                width: spec.width.map_or(Val::Auto, u),
                // Room above for the header's overhang.
                margin: UiRect::top(if header { u(12.0) } else { u(0.0) }),
                padding: frame_padding(spec.frame, header),
                ..default()
            },
            FocusPolicy::Block,
        ))
        .with_children(|f| {
            backdrop(f, spec.frame);
            if spec.movable {
                f.spawn((
                    DragHandle(root),
                    Interaction::default(),
                    FocusPolicy::Block,
                    layer_node(),
                ));
            }
            if header {
                f.spawn(Node {
                    position_type: PositionType::Absolute,
                    left: px(0),
                    right: px(0),
                    top: u(-12.0),
                    justify_content: JustifyContent::Center,
                    ..default()
                })
                .with_children(|h| {
                    let mut plate = h.spawn((
                        Node {
                            min_width: u(156.0),
                            height: u(40.0),
                            padding: UiRect::new(u(32.0), u(32.0), u(13.0), u(0.0)),
                            justify_content: JustifyContent::Center,
                            ..default()
                        },
                        FocusPolicy::Block,
                    ));
                    if spec.movable {
                        plate.insert((DragHandle(root), Interaction::default()));
                    }
                    plate.with_children(|p| {
                        p.spawn((
                            ArtLayer::new(Art::Header, Fit::Caps(HEADER_TC, HEADER_CAP))
                                .fallback(Color::srgba(0.12, 0.1, 0.06, 0.95), GOLD),
                            Node {
                                border: UiRect::all(px(1)),
                                ..layer_node()
                            },
                            ImageNode::default(),
                            Pickable::IGNORE,
                        ));
                        p.spawn(text(font, &spec.title, 12.0, GOLD));
                    });
                });
            }
            let closes = close.is_some();
            if let Some(close) = close {
                f.spawn(close_button(font, close));
            }
            if spec.movable {
                // Left of the close button, or in its place on a window without one.
                f.spawn(reset_button(font, root, if closes { 34.0 } else { 8.0 }));
            }
            content(f);
        });
        below(r);
    });
    root
}

/// The row of tabs under a window's frame, as the character frame's hang under it.
pub fn tab_strip(r: &mut ChildSpawnerCommands, tabs: impl FnOnce(&mut ChildSpawnerCommands)) {
    r.spawn(Node {
        flex_direction: FlexDirection::Row,
        margin: UiRect::new(u(12.0), u(0.0), u(-6.0), u(0.0)),
        ..default()
    })
    .with_children(tabs);
}

/// `StaticPopup1`'s width and its place: the top of the screen, 135 units down.
pub const POPUP_W: f32 = 320.0;
const POPUP_ANCHOR: Anchor = Anchor {
    at: Vec2::new(0.5, 0.0),
    pivot: Vec2::new(0.5, 0.0),
    offset: Vec2::new(0.0, 135.0),
};

/// A question in `StaticPopup`'s look (a dialog frame, the text in `GameFontHighlight`, two
/// buttons under it, the menu's open and close sounds), over every window. `yes` and `no` are
/// each a caption and what its button carries (the caller's action); the caller closes it, on
/// either button and on Esc, by despawning the returned root.
pub fn spawn_popup(
    commands: &mut Commands,
    font: &Handle<Font>,
    key: &'static str,
    question: &str,
    root_extra: impl Bundle,
    yes: (&str, impl Bundle),
    no: (&str, impl Bundle),
) -> Entity {
    let spec = WindowSpec {
        key,
        title: String::new(),
        frame: Frame::Dialog,
        width: Some(POPUP_W),
        movable: false,
        anchor: POPUP_ANCHOR,
        z: i32::MAX,
    };
    spawn_window(
        commands,
        font,
        spec,
        (root_extra, WindowSounds::MENU),
        None::<()>,
        |root| {
            root.spawn(Node {
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                row_gap: u(10.0),
                ..default()
            })
            .with_children(|c| {
                c.spawn((
                    Node {
                        max_width: u(POPUP_W - 40.0),
                        ..default()
                    },
                    children![(
                        text(font, question, BODY_SIZE, WHITE),
                        TextLayout::new_with_justify(Justify::Center),
                    )],
                ));
                c.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    column_gap: u(8.0),
                    ..default()
                })
                .with_children(|r| {
                    r.spawn(button(font, yes.0, 128.0, yes.1));
                    r.spawn(button(font, no.0, 128.0, no.1));
                });
            });
        },
        |_| {},
    )
}

/// The CVar the windows' places are kept in (`RegisterCVar`, persisted in benilla's config with
/// the account's other settings): `key=x,y;…`, each the top-left as a share of the screen.
pub const POSITIONS_CVAR: &str = "womwWindowPositions";

/// Where each window sits when it is not at its anchor.
#[derive(Resource, Debug, Default)]
pub struct WindowPositions {
    /// Where the player moved a window, by key: its top-left as a share of the screen, so it
    /// keeps its place through a resolution or UI scale change. Saved between sessions.
    pub moved: HashMap<String, Vec2>,
    /// Where a window's own code seats it before it is moved (the achievements window on the
    /// quest log's corner), top-left in logical pixels; for the session only.
    pub seats: HashMap<&'static str, Vec2>,
    /// `moved` changed since it was saved.
    pub dirty: bool,
    /// The saved places were read in (once a session, from the world's first VM).
    pub loaded: bool,
}

impl WindowPositions {
    /// Where `key` is drawn on a `viewport`, if anywhere but its anchor: where it was moved,
    /// else where its code seated it.
    pub fn get(&self, key: &str, viewport: Vec2) -> Option<Vec2> {
        self.moved
            .get(key)
            .map(|share| *share * viewport)
            .or_else(|| self.seats.get(key).copied())
    }

    /// The player put `key`'s top-left at `pos` (logical pixels on `viewport`).
    pub fn set_moved(&mut self, key: &str, pos: Vec2, viewport: Vec2) {
        let share = pos / viewport.max(Vec2::ONE);
        if self.moved.get(key) != Some(&share) {
            self.moved.insert(key.to_string(), share);
            self.dirty = true;
        }
    }

    /// Seat `key` at `pos` (logical pixels) until the player moves it.
    pub fn seat(&mut self, key: &'static str, pos: Vec2) {
        self.seats.insert(key, pos);
    }

    /// Forget where `key` was moved: back to its seat or its anchor.
    pub fn reset(&mut self, key: &str) {
        if self.moved.remove(key).is_some() {
            self.dirty = true;
        }
    }

    /// The saved form: `key=x,y` by key, `;`-joined, four places each.
    pub fn encode(&self) -> String {
        let mut parts: Vec<String> = self
            .moved
            .iter()
            .map(|(k, p)| format!("{k}={:.4},{:.4}", p.x, p.y))
            .collect();
        parts.sort();
        parts.join(";")
    }

    /// Read a saved form in, keeping any place already set this session; a malformed entry or
    /// one off the screen is skipped.
    pub fn merge_saved(&mut self, saved: &str) {
        for entry in saved.split(';') {
            let Some((key, xy)) = entry.trim().split_once('=') else {
                continue;
            };
            let Some((x, y)) = xy.split_once(',') else {
                continue;
            };
            let (Ok(x), Ok(y)) = (x.trim().parse::<f32>(), y.trim().parse::<f32>()) else {
                continue;
            };
            let on_screen = (0.0..=1.0).contains(&x) && (0.0..=1.0).contains(&y);
            if key.is_empty() || !on_screen {
                continue;
            }
            self.moved.entry(key.to_string()).or_insert(Vec2::new(x, y));
        }
    }
}

/// A drag in flight: the window and where on it the pointer took it.
#[derive(Resource, Debug, Default)]
pub struct Drag {
    on: Option<(Entity, Vec2)>,
}

/// Pixels kept clear between a window and the screen's edge when it is fitted.
const FIT_MARGIN: f32 = 8.0;

/// A press on a window's title or background picks it up and brings it to the front; it follows
/// the pointer until the button is let go. A press on its reset button puts it back.
#[allow(clippy::too_many_arguments)]
fn drag(
    mouse: Res<ButtonInput<MouseButton>>,
    window: Query<&Window, With<PrimaryWindow>>,
    state: Res<FpsState>,
    handles: Query<(&DragHandle, &Interaction)>,
    resets: Query<(&ResetPosition, &Interaction), Changed<Interaction>>,
    mut roots: Query<(Entity, &WowWindow, &mut GlobalZIndex)>,
    mut positions: ResMut<WindowPositions>,
    mut drag: ResMut<Drag>,
    mut front: Local<Option<Entity>>,
) {
    let Ok(w) = window.single() else {
        return;
    };
    let viewport = Vec2::new(w.width(), w.height());
    let cursor = w.cursor_position();
    let mut reset = false;
    for (r, i) in &resets {
        if *i == Interaction::Pressed
            && let Ok((_, win, _)) = roots.get(r.0)
        {
            positions.reset(win.key);
            drag.on = None;
            reset = true;
        }
    }
    if mouse.just_pressed(MouseButton::Left)
        && !reset
        && !state.owns_input
        && let Some(c) = cursor
    {
        for (h, i) in &handles {
            if *i != Interaction::Pressed {
                continue;
            }
            if let Ok((_, w, _)) = roots.get(h.0)
                && w.movable
                && let Some(pos) = w.pos
            {
                drag.on = Some((h.0, c - pos));
                *front = Some(h.0);
            }
        }
    }
    if !mouse.pressed(MouseButton::Left) {
        drag.on = None;
    }
    if let (Some((root, grab)), Some(c)) = (drag.on, cursor)
        && let Ok((_, w, _)) = roots.get(root)
    {
        // Only a real move counts: a click on the title leaves the window where it opens.
        if w.pos.is_none_or(|p| (c - grab - p).length() >= 1.0) {
            positions.set_moved(w.key, c - grab, viewport);
        }
    }
    for (e, w, mut z) in &mut roots {
        let want = window_z(w, *front, e);
        if z.0 != want {
            z.0 = want;
        }
    }
}

/// A window's `GlobalZIndex`: a movable one a step under its own (the one last taken up just
/// under the top), so a popup or a fixed window at the top always draws over the movable ones.
fn window_z(w: &WowWindow, front: Option<Entity>, e: Entity) -> i32 {
    if !w.movable {
        return w.z;
    }
    match front {
        Some(f) if f == e => i32::MAX - 1,
        Some(_) => w.z.saturating_sub(1).min(i32::MAX - 2),
        None => w.z.saturating_sub(1),
    }
}

/// The saved places read in once in the world, and written back when a drag or a reset has
/// changed them (never mid-drag): the account's [`POSITIONS_CVAR`].
fn persist_positions(
    state: Res<FpsState>,
    drag: Res<Drag>,
    script: Option<NonSend<UiScript>>,
    mut positions: ResMut<WindowPositions>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        return;
    };
    if !positions.loaded {
        positions.loaded = true;
        let saved = crate::lua::eval::<Option<String>>(&script, &load_positions_lua()).flatten();
        positions.merge_saved(saved.as_deref().unwrap_or(""));
    }
    if positions.dirty && drag.on.is_none() {
        positions.dirty = false;
        crate::lua::run(&script, &save_positions_lua(&positions.encode()));
    }
}

/// Register the places' CVar and read it.
pub fn load_positions_lua() -> String {
    format!("RegisterCVar(\"{POSITIONS_CVAR}\", \"\") return GetCVar(\"{POSITIONS_CVAR}\")")
}

/// Register the places' CVar and write `saved` to it.
pub fn save_positions_lua(saved: &str) -> String {
    format!(
        "RegisterCVar(\"{POSITIONS_CVAR}\", \"\") SetCVar(\"{POSITIONS_CVAR}\", {})",
        crate::lua::quote(saved)
    )
}

/// Size and place every window: the seam's scale over the one it was built at, less when it
/// would not fit; where it was moved to or its anchor, kept on screen.
fn place(
    window: Query<&Window, With<PrimaryWindow>>,
    seam: Res<UiSeam>,
    positions: Res<WindowPositions>,
    mut roots: Query<(&mut WowWindow, &ComputedNode, &mut Node, &mut UiTransform)>,
) {
    let Ok(w) = window.single() else {
        return;
    };
    let viewport = Vec2::new(w.width(), w.height());
    let s_now = seam_scale(viewport.y, seam.ui_scale);
    for (mut win, computed, mut node, mut transform) in &mut roots {
        if computed.is_empty() {
            continue;
        }
        let layout = computed.size * computed.inverse_scale_factor;
        let want = s_now / win.built_unit.max(0.01);
        let s = fit_scale(layout, viewport, FIT_MARGIN, want);
        let shown = layout * s;
        let pos = positions
            .get(win.key, viewport)
            .unwrap_or_else(|| win.anchor.place(viewport, shown, s_now));
        let pos = clamp_position(pos, shown, viewport).round();
        if win.pos.is_none() {
            info!(
                "benilla-mw2: window {} placed at {pos:?}, {shown:?} shown ({layout:?} laid out at \
                 {:.3} px/unit, drawn x{s:.3}), viewport {viewport:?}",
                win.key, win.built_unit
            );
        }
        if win.pos != Some(pos) {
            win.pos = Some(pos);
        }
        // The transform scales about the node's centre.
        let left = pos.x - (layout.x - shown.x) / 2.0;
        let top = pos.y - (layout.y - shown.y) / 2.0;
        if node.left != Val::Px(left) {
            node.left = Val::Px(left);
        }
        if node.top != Val::Px(top) {
            node.top = Val::Px(top);
        }
        if (transform.scale - Vec2::splat(s)).abs().max_element() > 1e-4 {
            transform.scale = Vec2::splat(s);
        }
    }
}

/// Tell benilla when a press belongs to one of these windows, so the world does not also take it.
fn pointer_over(
    state: Option<Res<FpsState>>,
    drag: Res<Drag>,
    roots: Query<(&RelativeCursorPosition, &InheritedVisibility), With<WowWindow>>,
    mut over: ResMut<LayerPointerOver>,
) {
    let owns = state.is_some_and(|s| s.owns_input);
    let on = drag.on.is_some() || (!owns && roots.iter().any(|(r, v)| v.get() && r.cursor_over()));
    if over.0 != on {
        over.0 = on;
    }
}

// ── Scroll frames ────────────────────────────────────────────────────────────────────────────────

/// A scrolling list's view (`UIPanelScrollFrameTemplate`'s scroll child): the wheel over it, its
/// arrows and its thumb move it `step` units a notch.
#[derive(Component, Debug)]
pub struct ScrollView {
    pub step: f32,
}

#[derive(Component)]
struct ScrollArrow {
    view: Entity,
    dir: f32,
}

#[derive(Component)]
struct ScrollThumb {
    view: Entity,
}

#[derive(Component)]
struct ScrollTrack;

/// The scroll bar's width in units.
const BAR_W: f32 = 16.0;

/// A scroll frame `h` units tall (`w` wide, or the rest of the parent's width): its view carries
/// `view_extra` and is filled by `fill`; the bar to its right. Returns the view.
pub fn scroll_frame(
    p: &mut ChildSpawnerCommands,
    h: f32,
    w: Option<f32>,
    view_extra: impl Bundle,
    fill: impl FnOnce(&mut ChildSpawnerCommands),
) -> Entity {
    let max_h = h;
    let mut view = Entity::PLACEHOLDER;
    p.spawn(Node {
        flex_direction: FlexDirection::Row,
        width: w.map_or(Val::Auto, u),
        // Without a width, the rest of the parent's row (or all of its column).
        flex_grow: if w.is_none() { 1.0 } else { 0.0 },
        min_width: u(0.0),
        height: u(h),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|row| {
        view = row
            .spawn((
                ScrollView { step: 24.0 },
                Node {
                    flex_direction: FlexDirection::Column,
                    flex_grow: 1.0,
                    flex_shrink: 1.0,
                    min_width: u(0.0),
                    max_height: u(max_h),
                    overflow: Overflow::scroll_y(),
                    padding: UiRect::right(u(4.0)),
                    ..default()
                },
                ScrollPosition::default(),
                RelativeCursorPosition::default(),
                view_extra,
            ))
            .with_children(fill)
            .id();
        row.spawn(Node {
            flex_direction: FlexDirection::Column,
            width: u(BAR_W),
            flex_shrink: 0.0,
            ..default()
        })
        .with_children(|bar| {
            bar.spawn(scroll_arrow(view, -1.0));
            bar.spawn((
                ScrollTrack,
                Node {
                    flex_grow: 1.0,
                    min_height: u(16.0),
                    ..default()
                },
                BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
            ))
            .with_children(|track| {
                track.spawn((
                    ScrollThumb { view },
                    Button,
                    Node {
                        position_type: PositionType::Absolute,
                        left: px(0),
                        top: px(0),
                        width: u(BAR_W),
                        height: u(BAR_W),
                        display: Display::None,
                        ..default()
                    },
                    ArtLayer::new(Art::Knob, Fit::Stretch([0.25, 0.75, 0.25, 0.75]))
                        .fallback(GOLD.with_alpha(0.6), Color::NONE),
                    ImageNode::default(),
                ));
            });
            bar.spawn(scroll_arrow(view, 1.0));
        });
    });
    view
}

fn scroll_arrow(view: Entity, dir: f32) -> impl Bundle {
    let kind = if dir < 0.0 {
        Kind::ScrollUp
    } else {
        Kind::ScrollDown
    };
    let face = if dir < 0.0 {
        Art::ScrollUpUp
    } else {
        Art::ScrollDownUp
    };
    (
        Button,
        WowButton::new(kind),
        ScrollArrow { view, dir },
        Node {
            width: u(BAR_W),
            height: u(BAR_W),
            flex_shrink: 0.0,
            ..default()
        },
        Children::spawn(SpawnWith(move |b: &mut ChildSpawner| {
            faces(
                b,
                Some((face, Fit::Stretch([0.25, 0.75, 0.25, 0.75]))),
                None,
            );
        })),
    )
}

/// bevy_ui clips a node's children to its laid-out size about its drawn centre and ignores the
/// `UiTransform` scale [`place`] draws a window at: drawn smaller than laid out, a list showed
/// past the bottom of its view (the Companions window's index ran over its inset and the tabs);
/// drawn larger, it was cut short. Each scroll view's subtree is clipped again at its drawn size.
/// Runs after bevy's `update_clipping_system`, whose new `CalculatedClip`s are in by then.
pub(crate) fn clip_scaled_views(
    views: Query<Entity, With<ScrollView>>,
    children: Query<&Children>,
    mut nodes: Query<(
        &Node,
        &ComputedNode,
        &UiGlobalTransform,
        Option<&mut bevy::ui::CalculatedClip>,
    )>,
) {
    for view in &views {
        let Ok((node, computed, transform, clip)) = nodes.get(view) else {
            continue;
        };
        let inherited = clip.map(|c| c.clip);
        let below = drawn_child_clip(node, computed, transform, inherited);
        if let Ok(kids) = children.get(view) {
            for &kid in kids {
                reclip(kid, below, &children, &mut nodes);
            }
        }
    }
}

fn reclip(
    e: Entity,
    clip: Option<Rect>,
    children: &Query<&Children>,
    nodes: &mut Query<(
        &Node,
        &ComputedNode,
        &UiGlobalTransform,
        Option<&mut bevy::ui::CalculatedClip>,
    )>,
) {
    let Ok((node, computed, transform, calc)) = nodes.get_mut(e) else {
        return;
    };
    // As bevy does: a hidden node clips itself and everything under it away.
    let clip = if node.display == Display::None {
        Some(Rect::default())
    } else {
        clip
    };
    if let (Some(mut calc), Some(c)) = (calc, clip)
        && calc.clip != c
    {
        calc.clip = c;
    }
    let below = drawn_child_clip(node, computed, transform, clip);
    if let Ok(kids) = children.get(e) {
        for &kid in kids {
            reclip(kid, below, children, nodes);
        }
    }
}

/// The clip a node gives its children, at the size it is drawn: its content box scaled by its
/// transform, on the axes it does not let overflow, within what it inherited.
pub(crate) fn drawn_child_clip(
    node: &Node,
    computed: &ComputedNode,
    transform: &UiGlobalTransform,
    inherited: Option<Rect>,
) -> Option<Rect> {
    if node.overflow.is_visible() {
        return inherited;
    }
    let (scale, _, centre) = transform.to_scale_angle_translation();
    let scale = scale.abs();
    let mut r = Rect::from_center_size(centre, computed.size() * scale);
    let inset = match node.overflow_clip_margin.visual_box {
        bevy::ui::OverflowClipBox::BorderBox => None,
        bevy::ui::OverflowClipBox::ContentBox => Some(computed.content_inset()),
        bevy::ui::OverflowClipBox::PaddingBox => Some(computed.border()),
    };
    if let Some(inset) = inset {
        r.min += inset.min_inset * scale;
        r.max -= inset.max_inset * scale;
    }
    r = r.inflate(
        node.overflow_clip_margin.margin.max(0.0) / computed.inverse_scale_factor * scale.y,
    );
    if node.overflow.x == OverflowAxis::Visible {
        r.min.x = f32::NEG_INFINITY;
        r.max.x = f32::INFINITY;
    }
    if node.overflow.y == OverflowAxis::Visible {
        r.min.y = f32::NEG_INFINITY;
        r.max.y = f32::INFINITY;
    }
    Some(inherited.map_or(r, |c| c.intersect(r)))
}

/// The farthest a view scrolls, logical pixels: its content past its own height.
pub fn scroll_max(content_h: f32, view_h: f32) -> f32 {
    (content_h - view_h).max(0.0)
}

/// The thumb's offset down a `track` pixels tall for a view at `pos` of `max`.
pub fn thumb_offset(pos: f32, max: f32, track: f32, thumb: f32) -> f32 {
    if max <= 0.0 {
        return 0.0;
    }
    (pos / max).clamp(0.0, 1.0) * (track - thumb).max(0.0)
}

/// The wheel over a view, its arrows and its thumb drag it; each view stays in range, its thumb
/// shows where it is and its arrows grey at the ends.
#[allow(clippy::type_complexity)]
fn scroll(
    mut wheel: MessageReader<MouseWheel>,
    mouse: Res<ButtonInput<MouseButton>>,
    window: Query<&Window, With<PrimaryWindow>>,
    mut views: Query<(
        &ScrollView,
        &ComputedNode,
        &mut ScrollPosition,
        &RelativeCursorPosition,
        &InheritedVisibility,
    )>,
    arrows: Query<(&ScrollArrow, &Interaction), Changed<Interaction>>,
    mut arrow_state: Query<(&ScrollArrow, &mut WowButton)>,
    mut thumbs: Query<
        (
            Entity,
            &ScrollThumb,
            &Interaction,
            &UiGlobalTransform,
            &ChildOf,
            &mut Node,
        ),
        Without<ScrollView>,
    >,
    tracks: Query<&ComputedNode, (With<ScrollTrack>, Without<ScrollView>)>,
    mut grab: Local<Option<(Entity, f32, f32)>>,
) {
    let notches: f32 = wheel
        .read()
        .map(|ev| match ev.unit {
            MouseScrollUnit::Line => ev.y,
            MouseScrollUnit::Pixel => ev.y / 20.0,
        })
        .sum();
    if notches != 0.0 {
        for (v, _, mut pos, cursor, vis) in &mut views {
            if vis.get() && cursor.cursor_over() {
                pos.0.y -= notches * uf(v.step);
            }
        }
    }
    for (a, i) in &arrows {
        if *i == Interaction::Pressed
            && let Ok((v, _, mut pos, _, _)) = views.get_mut(a.view)
        {
            pos.0.y += a.dir * uf(v.step);
        }
    }
    // The thumb: taken on a press, it follows the pointer.
    let cursor_y = window
        .single()
        .ok()
        .and_then(Window::cursor_position)
        .map(|c| c.y);
    if !mouse.pressed(MouseButton::Left) {
        *grab = None;
    }
    if mouse.just_pressed(MouseButton::Left)
        && let Some(y) = cursor_y
    {
        for (e, t, i, ..) in &thumbs {
            if *i == Interaction::Pressed
                && let Ok((_, _, pos, _, _)) = views.get(t.view)
            {
                *grab = Some((e, y, pos.0.y));
            }
        }
    }
    for (e, t, _, global, parent, mut node) in &mut thumbs {
        let Ok((_, computed, mut pos, _, _)) = views.get_mut(t.view) else {
            continue;
        };
        let inv = computed.inverse_scale_factor;
        let max = scroll_max(computed.content_size.y * inv, computed.size.y * inv);
        let track = tracks
            .get(parent.parent())
            .map_or(0.0, |c| c.size.y * c.inverse_scale_factor);
        let thumb = uf(BAR_W);
        if let (Some((g, y0, start)), Some(y)) = (*grab, cursor_y)
            && g == e
        {
            // Screen pixels to layout pixels: the window's own scale.
            let shown = global.matrix2.y_axis.length().max(0.01);
            let room = (track - thumb).max(1.0);
            pos.0.y = start + (y - y0) / shown * max / room;
        }
        let clamped = pos.0.y.clamp(0.0, max);
        if pos.0.y != clamped {
            pos.0.y = clamped;
        }
        let display = if max > 0.0 {
            Display::Flex
        } else {
            Display::None
        };
        if node.display != display {
            node.display = display;
        }
        let top = Val::Px(thumb_offset(clamped, max, track, thumb));
        if node.top != top {
            node.top = top;
        }
    }
    for (a, mut b) in &mut arrow_state {
        let Ok((_, computed, pos, _, _)) = views.get(a.view) else {
            continue;
        };
        let inv = computed.inverse_scale_factor;
        let max = scroll_max(computed.content_size.y * inv, computed.size.y * inv);
        let disabled = if a.dir < 0.0 {
            pos.0.y <= 0.5
        } else {
            pos.0.y >= max - 0.5
        };
        if b.disabled != disabled {
            b.disabled = disabled;
        }
    }
}

/// A world a window's build system can run in, for tests: an asset server for its font and its
/// lists' icons.
#[cfg(test)]
pub(crate) fn test_world() -> World {
    let mut app = App::new();
    app.add_plugins((
        bevy::app::TaskPoolPlugin::default(),
        bevy::asset::AssetPlugin::default(),
    ));
    app.init_asset::<Font>();
    app.init_asset::<Image>();
    app.insert_resource(crate::keybinds::Keymap::defaults());
    std::mem::take(app.world_mut())
}

/// An app that lays windows out as the game does, for tests: bevy_ui's own layout on a headless
/// 1280x720 screen, the windows placed by [`place`]. Spawn into its world, then `update`.
#[cfg(test)]
pub(crate) fn layout_app() -> App {
    use bevy::camera::{Camera2d, RenderTargetInfo};
    let mut app = App::new();
    app.add_plugins((
        MinimalPlugins,
        bevy::asset::AssetPlugin::default(),
        bevy::text::TextPlugin,
        bevy::ui::UiPlugin,
    ));
    app.init_asset::<Image>();
    app.init_asset::<bevy::image::TextureAtlasLayout>();
    app.add_plugins((
        bevy::input::InputPlugin,
        bevy::window::WindowPlugin {
            primary_window: None,
            exit_condition: bevy::window::ExitCondition::DontExit,
            close_when_requested: false,
            ..default()
        },
        bevy::picking::DefaultPickingPlugins,
    ));
    app.init_resource::<UiSeam>();
    app.init_resource::<WindowPositions>();
    app.world_mut().spawn((
        Window {
            resolution: bevy::window::WindowResolution::new(1280, 720),
            ..default()
        },
        PrimaryWindow,
    ));
    let mut cam = Camera::default();
    cam.computed.target_info = Some(RenderTargetInfo {
        physical_size: UVec2::new(1280, 720),
        scale_factor: 1.0,
    });
    app.world_mut()
        .spawn((Camera2d, cam, bevy::ui::IsDefaultUiCamera));
    app.add_systems(PostUpdate, place.before(UiSystems::Prepare));
    app.add_systems(
        PostUpdate,
        clip_scaled_views.after(bevy::ui::update::update_clipping_system),
    );
    app
}

/// Give [`layout_app`] a real font under [`FONT`]'s handle (the system's Arial, as the install's
/// Friz Quadrata is not there in tests), so text is measured; `false` without one.
#[cfg(test)]
pub(crate) fn measure_text(app: &mut App) -> bool {
    let Ok(bytes) = std::fs::read("C:\\Windows\\Fonts\\arial.ttf") else {
        return false;
    };
    let Ok(font) = Font::try_from_bytes(bytes) else {
        return false;
    };
    let handle: Handle<Font> = app.world().resource::<AssetServer>().load(FONT);
    app.world_mut()
        .resource_mut::<Assets<Font>>()
        .insert(handle.id(), font)
        .is_ok()
}

/// A laid-out node's rectangle on screen, logical pixels (its transform's scale applied).
#[cfg(test)]
pub(crate) fn screen_rect(world: &World, e: Entity) -> Rect {
    let n = world.get::<ComputedNode>(e).expect("laid out");
    let g = world.get::<UiGlobalTransform>(e).expect("placed");
    let inv = n.inverse_scale_factor;
    let half = n.size * Vec2::new(g.matrix2.x_axis.x, g.matrix2.y_axis.y) / 2.0;
    Rect::from_corners((g.translation - half) * inv, (g.translation + half) * inv)
}

/// Whether a node is laid out: not under a `Display::None`, nor a `Visibility::Hidden`.
#[cfg(test)]
pub(crate) fn drawn(world: &World, e: Entity) -> bool {
    let mut at = Some(e);
    while let Some(x) = at {
        if world
            .get::<Node>(x)
            .is_some_and(|n| n.display == Display::None)
            || world.get::<Visibility>(x) == Some(&Visibility::Hidden)
        {
            return false;
        }
        at = world.get::<ChildOf>(x).map(|c| c.parent());
    }
    true
}

// ── Sounds ───────────────────────────────────────────────────────────────────────────────────────

/// The sounds a window plays as it opens and closes, the stock frame's it stands in for
/// (SoundEntries kits by name, as `PlaySound` takes them). Put it on a window's root: it opens as
/// it is spawned or shown and closes as it is despawned or hidden; a window rebuilt in place
/// (despawned and spawned again under its key) plays neither.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
pub struct WindowSounds {
    pub open: &'static str,
    pub close: &'static str,
}

impl WindowSounds {
    /// `CharacterFrame`'s.
    pub const CHARACTER: Self = Self {
        open: "igCharacterInfoOpen",
        close: "igCharacterInfoClose",
    };
    /// `SpellBookFrame`'s.
    pub const SPELLBOOK: Self = Self {
        open: "igSpellBookOpen",
        close: "igSpellBookClose",
    };
    /// `QuestLogFrame`'s, and the world map's.
    pub const QUEST_LOG: Self = Self {
        open: "igQuestLogOpen",
        close: "igQuestLogClose",
    };
    /// The game menu's, and the dialogs'.
    pub const MENU: Self = Self {
        open: "igMainMenuOpen",
        close: "igMainMenuClose",
    };
}

/// A tab being picked (`PanelTemplates_Tab_OnClick`'s sound).
pub const TAB_SOUND: &str = "igCharacterInfoTab";
/// A small button or a box ticked (the reset button's, as the Lua dialogs' and the map's).
pub const CHECK_ON_SOUND: &str = "igMainMenuOptionCheckBoxOn";

/// Sounds waiting for the next frame's `PlaySound`, in order. Anything may push one.
#[derive(Resource, Default, Debug)]
pub struct UiSounds(pub Vec<&'static str>);

/// The sounds for the open windows going from `before` to `now`: an open for each window that
/// appeared, a close for each that is gone (by key, in key order).
pub fn window_sound_changes(
    before: &HashMap<&'static str, WindowSounds>,
    now: &HashMap<&'static str, WindowSounds>,
) -> Vec<&'static str> {
    let mut gone: Vec<_> = before.iter().filter(|(k, _)| !now.contains_key(*k)).collect();
    gone.sort_by_key(|(k, _)| **k);
    let mut came: Vec<_> = now.iter().filter(|(k, _)| !before.contains_key(*k)).collect();
    came.sort_by_key(|(k, _)| **k);
    gone.into_iter()
        .map(|(_, s)| s.close)
        .chain(came.into_iter().map(|(_, s)| s.open))
        .collect()
}

/// Queue the windows' open and close sounds, a tab's when one not shown is picked, and the reset
/// button's.
fn window_sounds(
    windows: Query<(&WowWindow, &WindowSounds, Option<&Visibility>)>,
    tabs: Query<(&Interaction, &WowButton), Changed<Interaction>>,
    resets: Query<&Interaction, (With<ResetPosition>, Changed<Interaction>)>,
    mut open: Local<HashMap<&'static str, WindowSounds>>,
    mut sounds: ResMut<UiSounds>,
) {
    let now: HashMap<_, _> = windows
        .iter()
        .filter(|(_, _, v)| v.is_none_or(|v| *v != Visibility::Hidden))
        .map(|(w, s, _)| (w.key, *s))
        .collect();
    if now != *open {
        sounds.0.extend(window_sound_changes(&open, &now));
        *open = now;
    }
    if tabs.iter().any(|(i, b)| {
        *i == Interaction::Pressed && b.kind == Kind::Tab && !b.selected && !b.disabled
    }) {
        sounds.0.push(TAB_SOUND);
    }
    if resets.iter().any(|i| *i == Interaction::Pressed) {
        sounds.0.push(CHECK_ON_SOUND);
    }
}

/// Play the queued sounds; out of the world, drop them.
fn play_ui_sounds(
    state: Option<Res<FpsState>>,
    script: Option<NonSend<UiScript>>,
    mut sounds: ResMut<UiSounds>,
) {
    if sounds.0.is_empty() {
        return;
    }
    let queued = std::mem::take(&mut sounds.0);
    let Some(script) = script.filter(|_| state.is_some_and(|s| s.in_world)) else {
        return;
    };
    for kit in queued {
        crate::lua::sound(&script, kit);
    }
}

// ── The plugin ───────────────────────────────────────────────────────────────────────────────────

/// The shared window machinery: art, seam, dragging, fitting, scroll frames and button looks.
pub struct WowUiPlugin;

/// After every window's own systems, before the layout: dress, drag, scroll and place.
#[derive(SystemSet, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct WowUiSet;

impl Plugin for WowUiPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<WowArt>()
            .init_resource::<UiSeam>()
            .init_resource::<WindowPositions>()
            .init_resource::<Drag>()
            .init_resource::<UiSounds>()
            .init_resource::<LayerPointerOver>()
            .add_systems(PreUpdate, pointer_over.after(UiSystems::Focus))
            .add_systems(
                Update,
                (load_art.run_if(art_untried), track_seam)
                    .chain()
                    .in_set(crate::Mw2Set::Mode)
                    .after(crate::mode::update_mode)
                    .before(crate::build::BuildInput),
            )
            .add_systems(
                PostUpdate,
                (drag, scroll, button_visuals, skin, place, show_tip)
                    .chain()
                    .in_set(WowUiSet)
                    .before(UiSystems::Prepare),
            )
            .add_systems(Update, persist_positions.in_set(crate::Mw2Set::Hud))
            .add_systems(
                PostUpdate,
                (window_sounds, play_ui_sounds)
                    .chain()
                    .after(WowUiSet)
                    .before(UiSystems::Prepare),
            )
            .add_systems(
                PostUpdate,
                clip_scaled_views.after(bevy::ui::update::update_clipping_system),
            );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_seam_follows_the_window_and_the_dial() {
        assert_eq!(seam_scale(768.0, 1.0), 1.0);
        assert!((seam_scale(720.0, 0.9) - 0.84375).abs() < 1e-6);
        assert!((seam_scale(2160.0, 0.9) - 2.53125).abs() < 1e-6);
        assert_eq!(seam_scale(0.0, 0.9), 1.0); // not yet sized
        assert_eq!(parse_ui_scale(Some("0.75")), 0.75);
        assert_eq!(parse_ui_scale(Some("9")), 1.5); // the dial's range
        assert_eq!(parse_ui_scale(Some("banana")), 0.9);
        assert_eq!(parse_ui_scale(None), 0.9);
    }

    #[test]
    fn a_window_too_big_for_the_screen_scales_down_to_fit() {
        let screen = Vec2::new(1280.0, 720.0);
        // Fits: drawn at the seam it wants.
        assert_eq!(fit_scale(Vec2::new(600.0, 400.0), screen, 8.0, 1.0), 1.0);
        // Too tall: down to the height left between the margins.
        let s = fit_scale(Vec2::new(600.0, 900.0), screen, 8.0, 1.0);
        assert!((s - 704.0 / 900.0).abs() < 1e-6);
        assert!(900.0 * s <= 720.0 - 16.0 + 1e-3);
        // Never grown past what it wants, however much room there is.
        assert_eq!(fit_scale(Vec2::new(100.0, 100.0), screen, 8.0, 1.5), 1.5);
        // Every common size: a 640×560 window authored at the 720p seam, shown at each screen's
        // seam, always lands inside.
        for (w, h) in [
            (1280.0, 720.0),
            (1366.0, 768.0),
            (1920.0, 1080.0),
            (2560.0, 1440.0),
            (3840.0, 2160.0),
        ] {
            let built = seam_scale(720.0, 0.9);
            let layout = Vec2::new(640.0, 560.0) * built;
            let want = seam_scale(h, 0.9) / built;
            let s = fit_scale(layout, Vec2::new(w, h), 8.0, want);
            assert!(layout.x * s <= w && layout.y * s <= h, "{w}x{h}");
        }
    }

    #[test]
    fn windows_stay_on_screen() {
        let screen = Vec2::new(1280.0, 720.0);
        let size = Vec2::new(400.0, 300.0);
        assert_eq!(
            clamp_position(Vec2::new(100.0, 50.0), size, screen),
            Vec2::new(100.0, 50.0)
        );
        // Dragged off any edge: back to it.
        assert_eq!(
            clamp_position(Vec2::new(-50.0, -10.0), size, screen),
            Vec2::ZERO
        );
        assert_eq!(
            clamp_position(Vec2::new(5000.0, 5000.0), size, screen),
            Vec2::new(880.0, 420.0)
        );
        // Bigger than the screen: its top-left stays on it.
        assert_eq!(
            clamp_position(Vec2::new(30.0, 30.0), Vec2::new(2000.0, 900.0), screen),
            Vec2::ZERO
        );
    }

    #[test]
    fn anchors_place_a_window() {
        let screen = Vec2::new(1000.0, 800.0);
        let size = Vec2::new(200.0, 100.0);
        assert_eq!(
            Anchor::CENTER.place(screen, size, 1.0),
            Vec2::new(400.0, 350.0)
        );
        let top_left = Anchor {
            at: Vec2::ZERO,
            pivot: Vec2::ZERO,
            offset: Vec2::new(16.0, 100.0),
        };
        assert_eq!(top_left.place(screen, size, 2.0), Vec2::new(32.0, 200.0));
    }

    // An e=2 strip holding each texel's (x, y) in r/g, so every copied texel names its source.
    #[test]
    fn the_edge_strip_becomes_a_nine_slice_sheet() {
        let e = 2usize;
        let (w, h) = (8 * e, e);
        let mut strip = vec![0u8; w * h * 4];
        for y in 0..h {
            for x in 0..w {
                let i = (y * w + x) * 4;
                strip[i] = x as u8;
                strip[i + 1] = y as u8;
                strip[i + 3] = 0xFF;
            }
        }
        let sheet = edge_nine_slice(e, &strip);
        let side = 3 * e;
        assert_eq!(sheet.len(), side * side * 4);
        let at = |x: usize, y: usize| {
            let i = (y * side + x) * 4;
            (sheet[i], sheet[i + 1], sheet[i + 3])
        };
        assert_eq!(at(0, 0), (8, 0, 0xFF)); // top-left corner ← cell 4
        assert_eq!(at(5, 0), (11, 0, 0xFF)); // top-right ← cell 5, its last column
        assert_eq!(at(0, 4), (12, 0, 0xFF)); // bottom-left ← cell 6
        assert_eq!(at(5, 5), (15, 1, 0xFF)); // bottom-right ← cell 7
        assert_eq!(at(0, 2), (0, 0, 0xFF)); // left run ← cell 0, upright
        assert_eq!(at(4, 3), (2, 1, 0xFF)); // right run ← cell 1
        // The top run, turned back: sheet (2+x, y) ← strip (2e + y, e − 1 − x).
        assert_eq!(at(2, 0), (4, 1, 0xFF));
        assert_eq!(at(3, 0), (4, 0, 0xFF));
        assert_eq!(at(2, 1), (5, 1, 0xFF));
        // The bottom run likewise from cell 3.
        assert_eq!(at(2, 4), (6, 1, 0xFF));
        assert_eq!(at(3, 5), (7, 0, 0xFF));
        // The centre is clear.
        assert_eq!(at(2, 2).2, 0);
        assert_eq!(at(3, 3).2, 0);
    }

    #[test]
    fn additive_art_blends_as_light() {
        let mut px = [255, 128, 0, 255, 0, 0, 0, 255, 100, 100, 100, 128];
        additive_to_alpha(&mut px);
        // Bright orange: opaque, its hue kept.
        assert_eq!(&px[0..4], &[255, 128, 0, 255]);
        // Black adds nothing: fully clear.
        assert_eq!(&px[4..8], &[0, 0, 0, 0]);
        // A half-alpha grey adds a quarter-ish white.
        assert_eq!(&px[8..12], &[255, 255, 255, 50]);
    }

    #[test]
    fn fits_slice_the_art_at_the_unit() {
        // The dialog edge: a 96-texel sheet, 32 units thick at 1.5 px a unit.
        let (rect, mode) = image_mode(Fit::Edge(32.0), Vec2::splat(96.0), 1.5);
        assert_eq!(rect, None);
        let NodeImageMode::Sliced(s) = mode else {
            panic!("sliced");
        };
        assert_eq!(s.border, BorderRect::all(32.0));
        assert!((s.max_corner_scale - 1.5).abs() < 1e-6);
        // A panel button: its 80×22 region, 12-texel caps.
        let (rect, mode) = image_mode(Fit::Caps(PANEL_TC, PANEL_CAP), Vec2::new(128.0, 32.0), 1.0);
        assert_eq!(rect, Some(Rect::new(0.0, 0.0, 80.0, 22.0)));
        let NodeImageMode::Sliced(s) = mode else {
            panic!("sliced");
        };
        assert_eq!(s.border.min_inset, Vec2::new(12.0, 0.0));
        // A background tiling every 32 units of a 64-texel tile at 2 px a unit: one texel a pixel.
        let (_, mode) = image_mode(Fit::Tile(32.0), Vec2::splat(64.0), 2.0);
        assert_eq!(
            mode,
            NodeImageMode::Tiled {
                tile_x: true,
                tile_y: true,
                stretch_value: 1.0
            }
        );
    }

    #[test]
    fn a_scroll_bar_follows_its_view() {
        assert_eq!(scroll_max(300.0, 200.0), 100.0);
        assert_eq!(scroll_max(100.0, 200.0), 0.0); // nothing to scroll
        assert_eq!(thumb_offset(0.0, 100.0, 116.0, 16.0), 0.0);
        assert_eq!(thumb_offset(50.0, 100.0, 116.0, 16.0), 50.0);
        assert_eq!(thumb_offset(100.0, 100.0, 116.0, 16.0), 100.0);
        assert_eq!(thumb_offset(500.0, 100.0, 116.0, 16.0), 100.0);
        assert_eq!(thumb_offset(10.0, 0.0, 116.0, 16.0), 0.0);
    }

    #[test]
    fn buttons_take_the_stock_states() {
        let panel = WowButton::new(Kind::Panel);
        let (face, glow, caption, _) = looks(&panel, Interaction::None);
        assert_eq!((face, glow, caption), (Some(Art::ButtonUp), false, GOLD));
        let (face, glow, caption, _) = looks(&panel, Interaction::Pressed);
        assert_eq!((face, glow, caption), (Some(Art::ButtonDown), true, WHITE));
        let off = WowButton {
            disabled: true,
            ..panel
        };
        let (face, glow, caption, _) = looks(&off, Interaction::Hovered);
        assert_eq!(
            (face, glow, caption),
            (Some(Art::ButtonDisabled), false, GREY)
        );
        let tab = WowButton {
            selected: true,
            ..WowButton::new(Kind::Tab)
        };
        let (face, glow, caption, _) = looks(&tab, Interaction::Hovered);
        assert_eq!((face, glow, caption), (Some(Art::TabActive), false, WHITE));
        let row = WowButton::new(Kind::Row);
        assert!(looks(&row, Interaction::Hovered).1);
        assert!(!looks(&row, Interaction::None).1);
    }

    #[test]
    fn a_window_spawns_with_every_widget() {
        // Bevy panics on a bundle that repeats a component; spawn one of everything.
        #[derive(Component)]
        struct Marker;
        let mut world = World::new();
        let font = Handle::<Font>::default();
        let spec = WindowSpec {
            key: "test",
            title: "Test".into(),
            frame: Frame::Dialog,
            width: Some(300.0),
            movable: true,
            anchor: Anchor::CENTER,
            z: 5,
        };
        let root = {
            let mut commands = world.commands();
            spawn_window(
                &mut commands,
                &font,
                spec,
                Marker,
                Some(Marker),
                |c| {
                    c.spawn(button(&font, "Save", 90.0, Marker));
                    c.spawn(button_fit(
                        &font,
                        "Fit",
                        WowButton::new(Kind::Panel),
                        Marker,
                    ));
                    c.spawn(row_button(&font, "Row", 100.0, 18.0, Marker));
                    c.spawn((Button, WowButton::new(Kind::Row)))
                        .with_children(row_glow);
                    scroll_frame(c, 100.0, None, Marker, |v| {
                        v.spawn(text(&font, "line", 12.0, WHITE));
                    });
                },
                |b| tab_strip(b, |t| _ = t.spawn(tab(&font, "Tab", Marker))),
            )
        };
        world.flush();
        assert!(world.get::<WowWindow>(root).is_some());
        let mut buttons = world.query::<&WowButton>();
        // Close, reset, panel, fit, row, hand-built row, two arrows, tab.
        assert_eq!(buttons.iter(&world).count(), 9);
        let mut resets = world.query::<&ResetPosition>();
        assert_eq!(
            resets.single(&world).unwrap().0,
            root,
            "the reset button is the window's"
        );
        let mut layers = world.query::<&ArtLayer>();
        assert!(layers.iter(&world).any(|l| l.art == Art::DialogBorder));
        assert!(layers.iter(&world).any(|l| l.art == Art::Header));
    }

    /// Lay out and place one 660-unit window on a headless 3200x1800 (200 %) screen with bevy_ui's
    /// own layout, anchored 16, 104 units from the top-left or seated at `seat`; its drawn rect.
    fn drawn_rect(seat: Option<Vec2>) -> Rect {
        use bevy::camera::{Camera2d, RenderTargetInfo};
        let mut app = App::new();
        app.add_plugins((
            MinimalPlugins,
            bevy::asset::AssetPlugin::default(),
            bevy::text::TextPlugin,
            bevy::ui::UiPlugin,
        ));
        app.init_asset::<Image>();
        app.init_asset::<bevy::image::TextureAtlasLayout>();
        app.add_plugins((
            bevy::input::InputPlugin,
            bevy::window::WindowPlugin {
                primary_window: None,
                exit_condition: bevy::window::ExitCondition::DontExit,
                close_when_requested: false,
                ..default()
            },
            bevy::picking::DefaultPickingPlugins,
        ));
        app.init_resource::<UiSeam>();
        app.init_resource::<WindowPositions>();
        let mut res = bevy::window::WindowResolution::new(3200, 1800);
        res.set_scale_factor_override(Some(2.0));
        app.world_mut().spawn((
            Window {
                resolution: res,
                ..default()
            },
            PrimaryWindow,
        ));
        let mut cam = Camera::default();
        cam.computed.target_info = Some(RenderTargetInfo {
            physical_size: UVec2::new(3200, 1800),
            scale_factor: 2.0,
        });
        app.world_mut()
            .spawn((Camera2d, cam, bevy::ui::IsDefaultUiCamera));
        app.add_systems(PostUpdate, place.before(UiSystems::Prepare));
        if let Some(seat) = seat {
            app.world_mut()
                .resource_mut::<WindowPositions>()
                .seat("probe", seat);
        }
        // Built at the test's unit (1 px per unit), drawn at the 900-high seam (~1.05): the
        // transform's scale is in play, as in the game when the seam moves after the build.
        let font = Handle::<Font>::default();
        let root = {
            let world = app.world_mut();
            let mut q = bevy::ecs::system::SystemState::<Commands>::new(world);
            let mut commands = q.get_mut(world);
            let root = spawn_window(
                &mut commands,
                &font,
                WindowSpec {
                    key: "probe",
                    title: "Probe".into(),
                    frame: Frame::Dialog,
                    width: Some(660.0),
                    movable: true,
                    anchor: Anchor {
                        at: Vec2::ZERO,
                        pivot: Vec2::ZERO,
                        offset: Vec2::new(16.0, 104.0),
                    },
                    z: 5,
                },
                (),
                Some(()),
                |f| {
                    f.spawn(Node {
                        height: u(400.0),
                        width: u(100.0),
                        ..default()
                    });
                },
                |below| {
                    tab_strip(below, |t| {
                        _ = t.spawn(Node {
                            width: u(70.0),
                            height: u(32.0),
                            ..default()
                        })
                    })
                },
            );
            q.apply(world);
            root
        };
        for _ in 0..6 {
            app.update();
        }
        // The drawn rectangle, logical pixels: the node's centre, its size at its scale.
        let world = app.world();
        let n = world.get::<ComputedNode>(root).unwrap();
        let g = world.get::<UiGlobalTransform>(root).unwrap();
        let inv = n.inverse_scale_factor;
        let half = n.size * Vec2::new(g.matrix2.x_axis.x, g.matrix2.y_axis.y) / 2.0;
        Rect::from_corners((g.translation - half) * inv, (g.translation + half) * inv)
    }

    /// A scroll view in a window drawn smaller or larger than it was laid out (a fitted window)
    /// clips its rows where it is drawn: nothing of a long list shows below the view, and nothing
    /// inside it is cut short (the Companions window's index ran over its inset and the tabs).
    #[test]
    fn a_scaled_scroll_view_clips_at_its_drawn_size() {
        for scale in [0.5f32, 1.5] {
            let mut app = layout_app();
            let (view, last) = {
                let world = app.world_mut();
                let mut q = bevy::ecs::system::SystemState::<Commands>::new(world);
                let mut commands = q.get_mut(world);
                let mut view = Entity::PLACEHOLDER;
                let mut last = Entity::PLACEHOLDER;
                commands
                    .spawn((
                        Node {
                            position_type: PositionType::Absolute,
                            left: px(200),
                            top: px(200),
                            flex_direction: FlexDirection::Column,
                            ..default()
                        },
                        UiTransform::from_scale(Vec2::splat(scale)),
                    ))
                    .with_children(|root| {
                        view = scroll_frame(root, 100.0, Some(120.0), (), |v| {
                            for _ in 0..20 {
                                last = v
                                    .spawn(Node {
                                        width: u(80.0),
                                        height: u(30.0),
                                        flex_shrink: 0.0,
                                        ..default()
                                    })
                                    .id();
                            }
                        });
                    });
                q.apply(world);
                (view, last)
            };
            for _ in 0..4 {
                app.update();
            }
            let world = app.world();
            let n = world.get::<ComputedNode>(view).unwrap();
            let g = world.get::<UiGlobalTransform>(view).unwrap();
            let (s, _, c) = g.to_scale_angle_translation();
            assert!((s.y - scale).abs() < 1e-3, "drawn at {s:?}");
            let (top, bottom) = (c.y - n.size().y * s.y / 2.0, c.y + n.size().y * s.y / 2.0);
            let clip = world
                .get::<bevy::ui::CalculatedClip>(last)
                .expect("a row under a scroll view is clipped")
                .clip;
            assert!(
                (clip.max.y - bottom).abs() < 1.0 && (clip.min.y - top).abs() < 1.0,
                "at x{scale}: clipped to {clip:?}, drawn {top}..{bottom}"
            );
        }
    }

    /// A real bevy_ui layout (taffy, the UI transform) at the live test's screen, 3200x1800 at
    /// 200 % (1600x900 logical): a window anchored top-left, or seated where it was put, is drawn
    /// with its top-left exactly there, whatever scale it is drawn at.
    #[test]
    fn a_window_is_drawn_where_it_is_anchored_or_seated() {
        let k = seam_scale(900.0, 0.9);
        let drawn = drawn_rect(None);
        let want = Vec2::new(16.0, 104.0) * k;
        assert!(
            (drawn.min - want.round()).abs().max_element() <= 1.0,
            "anchored: drawn at {:?}, wanted {want:?}",
            drawn.min
        );
        // 660 units wide at the seam.
        assert!((drawn.width() - 660.0 * k).abs() <= 2.0, "{drawn:?}");
        let seat = Vec2::new(40.0, 63.0);
        let drawn = drawn_rect(Some(seat));
        assert!(
            (drawn.min - seat).abs().max_element() <= 1.0,
            "seated: drawn at {:?}",
            drawn.min
        );
    }

    #[test]
    fn the_window_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((
            load_art,
            track_seam,
            drag,
            scroll,
            button_visuals,
            skin,
            place,
            pointer_over,
            show_tip,
            persist_positions,
            window_sounds,
            play_ui_sounds,
        ));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }

    #[test]
    fn a_popup_asks_with_two_buttons_and_no_close_or_reset() {
        #[derive(Component)]
        struct Yes;
        #[derive(Component)]
        struct No;
        let mut world = World::new();
        let font = Handle::<Font>::default();
        let root = {
            let mut commands = world.commands();
            spawn_popup(&mut commands, &font, "ask", "Sure?", (), ("Yes", Yes), ("No", No))
        };
        world.flush();
        let e = Entity::from_raw_u32(1).unwrap();
        let w = world.get::<WowWindow>(root).unwrap();
        assert!(!w.movable, "popups stay put");
        let (over_front, over_none) = (window_z(w, Some(e), root), window_z(w, None, root));
        assert_eq!(world.get::<WindowSounds>(root), Some(&WindowSounds::MENU));
        let mut kinds = world.query::<&WowButton>();
        let kinds: Vec<Kind> = kinds.iter(&world).map(|b| b.kind).collect();
        assert_eq!(kinds, [Kind::Panel, Kind::Panel]);
        assert_eq!(world.query::<&ResetPosition>().iter(&world).count(), 0);
        assert_eq!(world.query::<&Yes>().iter(&world).count(), 1);
        assert_eq!(world.query::<&No>().iter(&world).count(), 1);
        // Over every movable window, even the one last taken up.
        let front = WowWindow {
            key: "kits",
            movable: true,
            anchor: Anchor::CENTER,
            z: i32::MAX,
            built_unit: 1.0,
            pos: None,
        };
        assert!(window_z(&front, Some(e), e) < over_front);
        assert!(window_z(&front, None, e) < over_none);
    }

    #[test]
    fn windows_sound_as_they_open_and_close_and_tabs_as_they_are_picked() {
        let mut world = World::new();
        world.init_resource::<UiSounds>();
        let mut schedule = Schedule::default();
        schedule.add_systems(window_sounds);
        let window = |key| WowWindow {
            key,
            movable: true,
            anchor: Anchor::CENTER,
            z: 0,
            built_unit: 1.0,
            pos: None,
        };
        let kits = world.spawn((window("kits"), WindowSounds::CHARACTER)).id();
        world.spawn(window("silent"));
        let hidden = world
            .spawn((window("menu"), WindowSounds::MENU, Visibility::Hidden))
            .id();
        schedule.run(&mut world);
        assert_eq!(world.resource::<UiSounds>().0, ["igCharacterInfoOpen"]);
        world.resource_mut::<UiSounds>().0.clear();
        // Rebuilt in place: the same key, despawned and spawned again, plays nothing.
        world.despawn(kits);
        let kits = world.spawn((window("kits"), WindowSounds::CHARACTER)).id();
        schedule.run(&mut world);
        assert!(world.resource::<UiSounds>().0.is_empty());
        // A tab not shown, picked; the shown one, picked again, is quiet.
        world.spawn((Interaction::Pressed, WowButton::new(Kind::Tab)));
        world.spawn((
            Interaction::Pressed,
            WowButton {
                selected: true,
                ..WowButton::new(Kind::Tab)
            },
        ));
        schedule.run(&mut world);
        assert_eq!(world.resource::<UiSounds>().0, [TAB_SOUND]);
        world.resource_mut::<UiSounds>().0.clear();
        world.despawn(kits);
        *world.get_mut::<Visibility>(hidden).unwrap() = Visibility::Inherited;
        schedule.run(&mut world);
        assert_eq!(
            world.resource::<UiSounds>().0,
            ["igCharacterInfoClose", "igMainMenuOpen"]
        );
    }

    #[test]
    fn a_moved_window_keeps_its_share_of_the_screen_and_survives_a_save() {
        let screen = Vec2::new(1600.0, 900.0);
        let mut p = WindowPositions::default();
        assert_eq!(p.get("kits", screen), None, "unmoved: its anchor");
        p.seat("kits", Vec2::new(10.0, 20.0));
        assert_eq!(p.get("kits", screen), Some(Vec2::new(10.0, 20.0)), "seated");
        p.set_moved("kits", Vec2::new(400.0, 225.0), screen);
        assert!(p.dirty);
        assert_eq!(
            p.get("kits", screen),
            Some(Vec2::new(400.0, 225.0)),
            "moved wins"
        );
        // At another resolution it keeps its share of the screen.
        let small = Vec2::new(800.0, 450.0);
        assert_eq!(p.get("kits", small), Some(Vec2::new(200.0, 112.5)));
        p.set_moved("guide", Vec2::new(0.0, 450.0), screen);
        let saved = p.encode();
        assert_eq!(saved, "guide=0.0000,0.5000;kits=0.2500,0.2500");
        // Read back in a new session; a place set before the read stays.
        let mut q = WindowPositions::default();
        q.set_moved("guide", Vec2::new(800.0, 0.0), screen);
        q.merge_saved(&saved);
        assert_eq!(q.get("kits", screen), Some(Vec2::new(400.0, 225.0)));
        assert_eq!(q.get("guide", screen), Some(Vec2::new(800.0, 0.0)));
        // Nonsense and off-screen entries are skipped.
        let mut r = WindowPositions::default();
        r.merge_saved("a=1;b=x,y;=0.1,0.1;c=2.0,0.5;d=0.5,0.5;;");
        assert_eq!(r.moved.len(), 1);
        assert_eq!(r.get("d", screen), Some(Vec2::new(800.0, 450.0)));
        // Reset: back to the seat (or the anchor), and the change is to be saved.
        p.dirty = false;
        p.reset("kits");
        assert!(p.dirty);
        assert_eq!(p.get("kits", screen), Some(Vec2::new(10.0, 20.0)));
        p.dirty = false;
        p.reset("kits");
        assert!(!p.dirty, "nothing to forget: nothing to save");
    }

    /// A world with what [`drag`] reads: a 1600x900 window, the mouse, the layer's state.
    fn drag_world() -> (World, Entity) {
        let mut world = World::new();
        world.init_resource::<ButtonInput<MouseButton>>();
        world.init_resource::<WindowPositions>();
        world.init_resource::<Drag>();
        world.insert_resource(FpsState::default());
        world.spawn((
            Window {
                resolution: bevy::window::WindowResolution::new(1600, 900),
                ..default()
            },
            PrimaryWindow,
        ));
        let root = world
            .spawn((
                WowWindow {
                    key: "kits",
                    movable: true,
                    anchor: Anchor::CENTER,
                    z: 5,
                    built_unit: 1.0,
                    pos: Some(Vec2::new(100.0, 100.0)),
                },
                GlobalZIndex(5),
            ))
            .id();
        (world, root)
    }

    fn set_cursor(world: &mut World, at: Vec2) {
        let mut q = world.query_filtered::<&mut Window, With<PrimaryWindow>>();
        q.single_mut(world).unwrap().set_cursor_position(Some(at));
    }

    /// A press on the title picks the window up, it follows the pointer, and the place it is let
    /// go at is kept; its reset button forgets it.
    #[test]
    fn a_window_drags_by_its_title_and_resets() {
        use bevy::ecs::system::RunSystemOnce;
        let (mut world, root) = drag_world();
        let handle = world.spawn((DragHandle(root), Interaction::Pressed)).id();
        set_cursor(&mut world, Vec2::new(150.0, 110.0));
        world
            .resource_mut::<ButtonInput<MouseButton>>()
            .press(MouseButton::Left);
        world.run_system_once(drag).unwrap();
        // Picked up 50, 10 from its corner; a click alone moves nothing.
        assert!(world.resource::<WindowPositions>().moved.is_empty());
        world.resource_mut::<ButtonInput<MouseButton>>().clear();
        set_cursor(&mut world, Vec2::new(450.0, 235.0));
        world.run_system_once(drag).unwrap();
        let screen = Vec2::new(1600.0, 900.0);
        assert_eq!(
            world.resource::<WindowPositions>().get("kits", screen),
            Some(Vec2::new(400.0, 225.0))
        );
        // Let go: the drag ends where it was.
        world
            .resource_mut::<ButtonInput<MouseButton>>()
            .release(MouseButton::Left);
        world.entity_mut(handle).insert(Interaction::None);
        set_cursor(&mut world, Vec2::new(900.0, 800.0));
        world.run_system_once(drag).unwrap();
        assert_eq!(
            world.resource::<WindowPositions>().get("kits", screen),
            Some(Vec2::new(400.0, 225.0))
        );
        // The reset button.
        world.spawn((ResetPosition(root), Interaction::Pressed));
        world.run_system_once(drag).unwrap();
        let p = world.resource::<WindowPositions>();
        assert_eq!(p.get("kits", screen), None, "back at its anchor");
        assert!(p.dirty, "and saved so");
    }

    /// The places go to the account's CVar in the world's Lua and come back from it next
    /// session; a reset writes the empty list.
    #[test]
    fn window_places_persist_in_the_account_cvar() {
        use bevy::ecs::system::RunSystemOnce;
        let script = UiScript::new().expect("a VM");
        let mut world = World::new();
        world.insert_non_send_resource(script);
        world.init_resource::<Drag>();
        world.insert_resource(FpsState {
            in_world: true,
            ..Default::default()
        });
        let screen = Vec2::new(1600.0, 900.0);
        let mut p = WindowPositions::default();
        p.set_moved("guide", Vec2::new(800.0, 450.0), screen);
        world.insert_resource(p);
        world.run_system_once(persist_positions).unwrap();
        let saved = |w: &World| {
            w.non_send_resource::<UiScript>()
                .eval::<String>(&format!("return GetCVar(\"{POSITIONS_CVAR}\")"))
                .unwrap()
        };
        assert_eq!(saved(&world), "guide=0.5000,0.5000");
        assert!(!world.resource::<WindowPositions>().dirty);
        // A new session (the same VM's CVar standing in for the saved config): read back.
        world.insert_resource(WindowPositions::default());
        world.run_system_once(persist_positions).unwrap();
        assert_eq!(
            world.resource::<WindowPositions>().get("guide", screen),
            Some(Vec2::new(800.0, 450.0))
        );
        world.resource_mut::<WindowPositions>().reset("guide");
        world.run_system_once(persist_positions).unwrap();
        assert_eq!(saved(&world), "");
    }

    #[test]
    fn a_tooltip_sits_beside_the_pointer_and_stays_on_screen() {
        let screen = Vec2::new(1280.0, 720.0);
        let size = Vec2::new(200.0, 80.0);
        // Room: right of and under the pointer.
        assert_eq!(
            tip_position(Vec2::new(100.0, 100.0), size, screen, 16.0),
            Vec2::new(116.0, 116.0)
        );
        // At the right edge: left of it; at the bottom: above it.
        assert_eq!(
            tip_position(Vec2::new(1200.0, 700.0), size, screen, 16.0),
            Vec2::new(984.0, 604.0)
        );
        // Never off the top-left.
        let p = tip_position(Vec2::new(5.0, 5.0), Vec2::new(2000.0, 900.0), screen, 16.0);
        assert_eq!(p, Vec2::ZERO);
    }

    #[test]
    fn the_shared_widgets_spawn() {
        let mut world = World::new();
        let font = Handle::<Font>::default();
        {
            let mut commands = world.commands();
            let root = commands.spawn(Node::default()).id();
            commands.entity(root).with_children(|p| {
                section_header(p, &font, "Section");
                inset(p, Node::default(), WowTip::new("Inset", "A well."), |c| {
                    c.spawn(icon_frame(None, 28.0, SLOT_EDGE));
                    c.spawn(text(&font, "line", BODY_SIZE, WHITE));
                });
                p.spawn(Node::default())
                    .with_children(|t| tip_body(t, &font, &WowTip::new("A", "B").0));
            });
        }
        world.flush();
        let mut layers = world.query::<&ArtLayer>();
        assert!(layers.iter(&world).any(|l| l.art == Art::Divider));
        assert!(layers.iter(&world).any(|l| l.art == Art::TooltipBorder));
        let mut tips = world.query::<&WowTip>();
        assert_eq!(tips.iter(&world).count(), 1);
        // A newbie tip: the name in white, what it does in gold.
        let tip = WowTip::new("Save & Equip", "Saves the kit.");
        assert_eq!((tip.0[0].color, tip.0[1].color), (WHITE, GOLD));
        assert_eq!(WowTip::new("Close", "").0.len(), 1);
        // Keys in a tip are the ones bound now.
        let mut keys = crate::keybinds::Keymap::defaults();
        let tip = WowTip::new("Door", "{WOMW_INTERACT} on it sets its lock.");
        assert_eq!(tip_keys(&tip.0, Some(&keys))[1].text, "F on it sets its lock.");
        keys = crate::keybinds::Keymap::from_pairs([("G", "WOMW_INTERACT")]);
        assert_eq!(tip_keys(&tip.0, Some(&keys))[1].text, "G on it sets its lock.");
        assert_eq!(tip_keys(&tip.0, None)[1].text, tip.0[1].text);
    }
}
