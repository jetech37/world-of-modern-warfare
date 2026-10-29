//! The MW2 overlay, as a bevy_ui tree drawn over the WoW interface (benilla renders bevy_ui on its
//! interface camera after its own quads). Text uses the install's own `FRIZQT__.TTF`, read through
//! benilla's `mpq://` source, so nothing is bundled.

use benilla_app::{ObjectType, Objects};
use benilla_ui::script::UiScript;
use bevy::picking::Pickable;
use bevy::prelude::*;
use bevy::ui::Val2;

use crate::combat::Combat;
use crate::config::{FireMode, Loadout};
use crate::mode::FpsState;
use crate::streaks::{Banner, FeedLine, Sound, Streak};
use crate::weapon::Weapon;
use crate::{Mw2Set, lua};

use crate::wow_ui::FONT;
const WHITE: Color = Color::srgb(0.95, 0.95, 0.95);
const GOLD: Color = Color::srgb(1.0, 0.82, 0.25);
const RED: Color = Color::srgb(0.9, 0.12, 0.1);
const GREEN: Color = Color::srgb(0.45, 0.95, 0.4);

/// Radar dots on the minimap at most.
const RADAR_BLIPS: usize = 48;
/// Kill feed lines shown.
const FEED_LINES: usize = 5;

#[derive(Resource)]
struct HudFont(Handle<Font>);

#[derive(Component)]
struct HudRoot;
#[derive(Component)]
struct Vignette;
#[derive(Component)]
struct CrossBar(Vec2);
#[derive(Component)]
struct RedDot;
#[derive(Component)]
struct HitBar;
#[derive(Component)]
struct FeedColumn;
#[derive(Component)]
struct FeedEntry {
    born: f32,
}
/// The banner's column, hidden whole once its text has faded.
#[derive(Component)]
struct BannerBox;
#[derive(Component)]
struct BannerTitle;
#[derive(Component)]
struct BannerSub;
#[derive(Component)]
struct WeaponName;
#[derive(Component)]
struct AmmoText;
#[derive(Component)]
struct MantleHintText;
#[derive(Component)]
struct StaminaBar;
#[derive(Component)]
struct StaminaFill;
/// Modern's gear under the gun: the special round loaded, the grenades left.
#[derive(Component)]
struct GearText;
/// Utility's gather bar under the tool's name.
#[derive(Component)]
struct GatherBar;
#[derive(Component)]
struct GatherFill;
/// Utility's centre dot, the crosshair of bare hands and tools.
#[derive(Component)]
struct CentreDot;

/// A HUD text's size in WoW UI units, drawn at the client's seam so it scales with the screen
/// and `uiScale` as the stock interface does ([`scale_hud`]).
#[derive(Component, Clone, Copy, Debug, PartialEq)]
pub struct FontUnits(pub f32);

/// Logical pixels per UI unit this frame (`windowH/768 × uiScale`, crate::wow_ui::seam_scale).
#[derive(Resource, Clone, Copy, Debug, PartialEq)]
pub struct HudScale(pub f32);

impl Default for HudScale {
    fn default() -> Self {
        HudScale(1.0)
    }
}
/// The gun HUD's column (the ammunition over the weapon's name), seated clear of the bag bar.
#[derive(Component)]
pub struct AmmoColumn;
#[derive(Component)]
struct RadarLabel;
/// A line in the top-left column under the player frame (the UAV's time, the killstreaks'
/// timers), this many UI units under its top.
#[derive(Component, Clone, Copy, Debug, PartialEq)]
pub struct TopLeftSlot(pub f32);
#[derive(Component)]
struct Flash;
#[derive(Component)]
struct NukeText;

/// When the current banner went up.
#[derive(Resource, Default)]
struct BannerClock {
    born: f32,
}

pub struct HudPlugin;

impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BannerClock>()
            .init_resource::<BagBar>()
            .init_resource::<HudScale>()
            .init_resource::<BarStack>()
            .add_systems(Startup, spawn_hud)
            .add_systems(
                Update,
                (
                    scale_hud,
                    show_hud,
                    play_sounds,
                    draw_vignette,
                    draw_crosshair,
                    draw_feed,
                    draw_banner,
                    read_bag_bar,
                    place_ammo,
                    draw_column,
                    draw_stamina,
                    draw_mantle_hint,
                    mark_minimap,
                    draw_nuke,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// The drop shadow every HUD text carries, drawn in its own colour: fading a `TextColor` leaves it.
const SHADOW: Color = Color::srgba(0.0, 0.0, 0.0, 0.85);

/// A HUD text `size` UI units tall (scaled at the seam by [`scale_hud`]).
fn text(font: &Handle<Font>, s: impl Into<String>, size: f32, color: Color) -> impl Bundle {
    (
        Text::new(s),
        FontUnits(size),
        TextFont::from_font_size(size).with_font(font.clone()),
        TextColor(color),
        TextShadow {
            offset: Vec2::splat(1.5),
            color: SHADOW,
        },
        Pickable::IGNORE,
    )
}

fn abs(node: Node) -> Node {
    Node {
        position_type: PositionType::Absolute,
        ..node
    }
}

/// A bar centered on the screen's middle, moved by its `UiTransform`.
fn center_bar(w: f32, h: f32, color: Color) -> impl Bundle {
    (
        abs(Node {
            left: percent(50),
            top: percent(50),
            width: px(w),
            height: px(h),
            ..default()
        }),
        BackgroundColor(color),
        UiTransform::default(),
        Pickable::IGNORE,
    )
}

fn spawn_hud(mut commands: Commands, assets: Res<AssetServer>, mut images: ResMut<Assets<Image>>) {
    let edge_tex = images.add(crate::downed::edge_image());
    let font: Handle<Font> = assets.load(FONT);
    commands.insert_resource(HudFont(font.clone()));
    commands.insert_resource(BannerClock { born: -10.0 });
    let f = &font;

    commands
        .spawn((
            Name::new("mw2 hud"),
            HudRoot,
            abs(Node {
                width: percent(100),
                height: percent(100),
                ..default()
            }),
            GlobalZIndex(i32::MAX - 1),
            Visibility::Hidden,
            Pickable::IGNORE,
        ))
        .with_children(|root| {
            // Damage: soft red edges that deepen as health falls.
            root.spawn((
                Vignette,
                abs(Node {
                    width: percent(100),
                    height: percent(100),
                    ..default()
                }),
                ImageNode {
                    image: edge_tex.clone(),
                    color: Color::WHITE.with_alpha(0.0),
                    image_mode: NodeImageMode::Stretch,
                    ..default()
                },
                Pickable::IGNORE,
            ));

            // Crosshair: four bars that bloom apart, the red dot when aimed, and the hitmarker.
            for dir in [Vec2::X, Vec2::NEG_X, Vec2::Y, Vec2::NEG_Y] {
                let (w, h) = if dir.x != 0.0 {
                    (10.0, 2.0)
                } else {
                    (2.0, 10.0)
                };
                root.spawn((CrossBar(dir), center_bar(w, h, WHITE)));
            }
            root.spawn((RedDot, center_bar(4.0, 4.0, RED)));
            root.spawn((CentreDot, center_bar(3.0, 3.0, WHITE)));
            for (dx, dy) in [(1.0, 1.0), (-1.0, 1.0), (1.0, -1.0), (-1.0, -1.0)] {
                root.spawn((
                    HitBar,
                    abs(Node {
                        left: percent(50),
                        top: percent(50),
                        width: px(9),
                        height: px(2),
                        ..default()
                    }),
                    BackgroundColor(Color::NONE),
                    UiTransform {
                        translation: Val2::px(dx * 8.0 - 4.5, dy * 8.0 - 1.0),
                        rotation: Rot2::degrees(if dx * dy > 0.0 { 45.0 } else { -45.0 }),
                        ..default()
                    },
                    Pickable::IGNORE,
                ));
            }

            // Kill feed, left, above the chat.
            root.spawn((
                FeedColumn,
                abs(Node {
                    left: px(24),
                    bottom: percent(42),
                    flex_direction: FlexDirection::Column,
                    row_gap: px(3),
                    ..default()
                }),
                Pickable::IGNORE,
            ));

            spawn_banner(root, f);

            // MW2's mantle hint, under the crosshair.
            root.spawn((
                abs(Node {
                    width: percent(100),
                    top: percent(58),
                    justify_content: JustifyContent::Center,
                    ..default()
                }),
                Pickable::IGNORE,
            ))
            .with_children(|b| {
                b.spawn((
                    MantleHintText,
                    text(f, "Press SPACE to climb", 14.0, WHITE),
                    Visibility::Hidden,
                ));
            });

            // The gun: its ammunition over its name, bottom right just above the bag bar
            // (`place_ammo` seats it from where the stock bags sit at the current scale).
            root.spawn((
                AmmoColumn,
                abs(Node {
                    right: px(FALLBACK_RIGHT),
                    bottom: px(FALLBACK_BOTTOM),
                    flex_direction: FlexDirection::Column,
                    align_items: AlignItems::FlexEnd,
                    ..default()
                }),
                Pickable::IGNORE,
            ))
            .with_children(|b| {
                b.spawn((AmmoText, text(f, "", AMMO_UNITS, WHITE)));
                b.spawn((WeaponName, text(f, "", NAME_UNITS, GOLD)));
                b.spawn((GearText, text(f, "", GEAR_UNITS, WHITE)));
                // Utility's gather, filling with each strike.
                b.spawn((
                    GatherBar,
                    Node {
                        width: px(GAUGE_W_UNITS),
                        height: px(GATHER_H_UNITS),
                        margin: UiRect::top(px(2)),
                        border: UiRect::all(px(1)),
                        ..default()
                    },
                    BorderColor::all(Color::srgba(0.0, 0.0, 0.0, 0.9)),
                    BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.5)),
                    Visibility::Hidden,
                    Pickable::IGNORE,
                ))
                .with_child((
                    GatherFill,
                    Node {
                        width: percent(0),
                        height: percent(100),
                        ..default()
                    },
                    // The stock casting bar's gold.
                    BackgroundColor(Color::srgb(1.0, 0.7, 0.0)),
                    Pickable::IGNORE,
                ));
                // Sprint stamina, shown only while it is short of full.
                b.spawn((
                    StaminaBar,
                    Node {
                        width: px(GAUGE_W_UNITS),
                        height: px(STAMINA_H_UNITS),
                        margin: UiRect::top(px(3)),
                        ..default()
                    },
                    BackgroundColor(Color::srgba(1.0, 1.0, 1.0, 0.15)),
                    Visibility::Hidden,
                    Pickable::IGNORE,
                ))
                .with_child((
                    StaminaFill,
                    Node {
                        width: percent(100),
                        height: percent(100),
                        ..default()
                    },
                    BackgroundColor(Color::srgba(1.0, 1.0, 1.0, 0.6)),
                    Pickable::IGNORE,
                ));
            });

            // The UAV's time left, top left under the player frame (its dots are on the minimap).
            root.spawn((
                RadarLabel,
                TopLeftSlot(0.0),
                abs(Node {
                    left: px(20),
                    top: px(110),
                    ..default()
                }),
                text(f, "", 11.0, GREEN),
            ));

            // Nuke countdown and the white-out.
            root.spawn((
                abs(Node {
                    width: percent(100),
                    top: percent(28),
                    justify_content: JustifyContent::Center,
                    ..default()
                }),
                Pickable::IGNORE,
            ))
            .with_children(|b| {
                b.spawn((NukeText, text(f, "", 20.0, RED)));
            });
            root.spawn((
                Flash,
                abs(Node {
                    width: percent(100),
                    height: percent(100),
                    ..default()
                }),
                BackgroundColor(Color::NONE),
                Pickable::IGNORE,
            ));
        });
}

/// The banner: a centred column of title over subtitle, hidden until one goes up.
fn spawn_banner(root: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    root.spawn((
        BannerBox,
        abs(Node {
            width: percent(100),
            top: percent(16),
            flex_direction: FlexDirection::Column,
            align_items: AlignItems::Center,
            row_gap: px(4),
            ..default()
        }),
        Visibility::Hidden,
        Pickable::IGNORE,
    ))
    .with_children(|b| {
        b.spawn((BannerTitle, text(f, "", 26.0, GOLD)));
        b.spawn((BannerSub, text(f, "", 13.0, WHITE)));
    });
}

fn show_hud(state: Res<FpsState>, mut root: Query<&mut Visibility, With<HudRoot>>) {
    let shown = state.active;
    for mut v in &mut root {
        let want = if shown {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
}

fn play_sounds(
    state: Res<FpsState>,
    mut sounds: MessageReader<Sound>,
    script: Option<NonSend<UiScript>>,
) {
    let Some(script) = script.filter(|_| state.active) else {
        sounds.clear();
        return;
    };
    for Sound(kit) in sounds.read() {
        lua::sound(&script, kit);
    }
}

fn draw_vignette(
    time: Res<Time>,
    combat: Res<Combat>,
    state: Res<FpsState>,
    mut q: Query<&mut ImageNode, With<Vignette>>,
) {
    let t = time.elapsed_secs();
    let missing = (1.0 - combat.health_fraction).clamp(0.0, 1.0);
    // The blood creeps in under 60 % health and flares for a moment on every hit.
    let base = ((missing - 0.4) / 0.6).clamp(0.0, 1.0) * 0.55;
    let flare = (1.0 - (t - combat.last_hurt) / 0.6).clamp(0.0, 1.0) * 0.35;
    let a = if state.active {
        (base + flare).min(0.8)
    } else {
        0.0
    };
    for mut img in &mut q {
        img.color = Color::WHITE.with_alpha(a);
    }
}

/// How the crosshair is drawn in a system: Modern's four bars bloom with the gun's spread (MW2's),
/// Classic's sit still and gold (a spell or a swing has no cone), Utility's is a dot (the hands
/// and the tools reach what is under it).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CrosshairStyle {
    /// The four bars, `length` pixels long; none for a dot alone.
    pub bars: bool,
    pub length: f32,
    /// The bars follow the gun's spread; otherwise they sit at [`STILL_GAP`].
    pub bloom: bool,
    /// The centre dot.
    pub dot: bool,
    pub color: Color,
}

/// Classic's bars' fixed gap from the centre, in pixels.
pub const STILL_GAP: f32 = 5.0;

pub fn crosshair_style(system: crate::action_systems::System) -> CrosshairStyle {
    use crate::action_systems::System;
    match system {
        // The Companion bar is never the hands' system (`active`); it reads as Modern.
        System::Modern | System::Companion => CrosshairStyle {
            bars: true,
            length: 10.0,
            bloom: true,
            dot: false,
            color: WHITE,
        },
        System::Classic => CrosshairStyle {
            bars: true,
            length: 6.0,
            bloom: false,
            dot: false,
            color: GOLD,
        },
        System::Utility => CrosshairStyle {
            bars: false,
            length: 0.0,
            bloom: false,
            dot: true,
            color: WHITE,
        },
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw_crosshair(
    time: Res<Time>,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    streak: Res<Streak>,
    aim: Res<crate::aim::Aim>,
    systems: Res<crate::action_systems::ActionSystems>,
    mut bars: Query<
        (&CrossBar, &mut Node, &mut UiTransform, &mut BackgroundColor),
        (Without<RedDot>, Without<HitBar>, Without<CentreDot>),
    >,
    mut dot: Query<
        (&mut UiTransform, &mut BackgroundColor),
        (
            With<RedDot>,
            Without<CrossBar>,
            Without<HitBar>,
            Without<CentreDot>,
        ),
    >,
    mut centre: Query<
        (&mut UiTransform, &mut BackgroundColor),
        (
            With<CentreDot>,
            Without<CrossBar>,
            Without<HitBar>,
            Without<RedDot>,
        ),
    >,
    mut hits: Query<
        &mut BackgroundColor,
        (
            With<HitBar>,
            Without<CrossBar>,
            Without<RedDot>,
            Without<CentreDot>,
        ),
    >,
    fusion: Res<crate::fusion::Fusion>,
) {
    let t = time.elapsed_secs();
    let style = crosshair_style(systems.active);
    // Over a hostile-looking unit the crosshair goes red, as MW2's does over an enemy.
    let on_unit = aim.target.as_ref().is_some_and(|a| !a.service);
    let colour = if on_unit { RED } else { style.color };
    let hip = 1.0 - weapon.ads;
    let alpha = if state.owns_input { hip } else { hip * 0.35 };
    // The bars sit at the cone's edge: a few pixels per degree of spread.
    let gap = if style.bloom {
        4.0 + weapon.spread * 4.0
    } else {
        STILL_GAP
    };
    for (bar, mut node, mut tf, mut bg) in &mut bars {
        let size = if bar.0.x != 0.0 {
            Vec2::new(style.length, 2.0)
        } else {
            Vec2::new(2.0, style.length)
        };
        if node.width != px(size.x) || node.height != px(size.y) {
            node.width = px(size.x);
            node.height = px(size.y);
        }
        let off = bar.0 * (gap + style.length / 2.0);
        tf.translation = Val2::px(off.x - size.x / 2.0, -off.y - size.y / 2.0);
        bg.0 = if style.bars {
            colour.with_alpha(alpha)
        } else {
            Color::NONE
        };
    }
    for (mut tf, mut bg) in &mut centre {
        tf.translation = Val2::px(-1.5, -1.5);
        bg.0 = if style.dot {
            colour.with_alpha(alpha.max(0.35))
        } else {
            Color::NONE
        };
    }
    for (mut tf, mut bg) in &mut dot {
        tf.translation = Val2::px(-2.0, -2.0);
        // A stand-in sight, for the plain HUD: an MW2 gun aims through its own.
        bg.0 = if weapon.ads_fov.is_some() || !style.bloom {
            Color::NONE
        } else {
            RED.with_alpha(weapon.ads.powi(2))
        };
    }
    // The fusion server confirms hits itself; its headshots mark gold.
    let hit_at = streak.hit_at.max(fusion.last_hit);
    let hit = (1.0 - (t - hit_at) / 0.25).clamp(0.0, 1.0);
    let hit_colour = if streak.hit_fatal {
        RED
    } else if t - fusion.last_headshot < 0.25 {
        GOLD
    } else {
        WHITE
    };
    for mut bg in &mut hits {
        bg.0 = hit_colour.with_alpha(hit);
    }
}

fn draw_feed(
    mut commands: Commands,
    time: Res<Time>,
    font: Res<HudFont>,
    mut feed: MessageReader<FeedLine>,
    column: Query<Entity, With<FeedColumn>>,
    mut lines: Query<(Entity, &FeedEntry, &mut TextColor)>,
) {
    let t = time.elapsed_secs();
    let Ok(column) = column.single() else {
        return;
    };
    for FeedLine(s) in feed.read() {
        commands
            .entity(column)
            .with_child((FeedEntry { born: t }, text(&font.0, s.clone(), 12.0, WHITE)));
    }
    let mut live: Vec<(f32, Entity)> = lines.iter().map(|(e, l, _)| (l.born, e)).collect();
    live.sort_by(|a, b| b.0.total_cmp(&a.0));
    for (_, e) in live.iter().skip(FEED_LINES) {
        commands.entity(*e).despawn();
    }
    for (e, line, mut colour) in &mut lines {
        let age = t - line.born;
        if age > 7.0 {
            commands.entity(e).despawn();
        } else {
            colour.0 = colour.0.with_alpha((7.0 - age).clamp(0.0, 1.0));
        }
    }
}

/// Show the latest banner and fade it out by its fourth second: the text, its shadow (which keeps
/// a colour of its own) and then the whole column, so nothing of it stays on screen.
fn draw_banner(
    time: Res<Time>,
    mut clock: ResMut<BannerClock>,
    mut banners: MessageReader<Banner>,
    mut title: Query<
        (&mut Text, &mut TextColor, &mut TextShadow),
        (With<BannerTitle>, Without<BannerSub>),
    >,
    mut sub: Query<
        (&mut Text, &mut TextColor, &mut TextShadow),
        (With<BannerSub>, Without<BannerTitle>),
    >,
    mut column: Query<&mut Visibility, With<BannerBox>>,
) {
    let t = time.elapsed_secs();
    if let Some(b) = banners.read().last() {
        clock.born = t;
        for (mut s, ..) in &mut title {
            s.0.clone_from(&b.title);
        }
        for (mut s, ..) in &mut sub {
            s.0.clone_from(&b.subtitle);
        }
    }
    let a = (3.5 - (t - clock.born)).clamp(0.0, 1.0);
    let shadow = SHADOW.with_alpha(SHADOW.alpha() * a);
    for (_, mut c, mut sh) in &mut title {
        c.0 = GOLD.with_alpha(a);
        sh.color = shadow;
    }
    for (_, mut c, mut sh) in &mut sub {
        c.0 = WHITE.with_alpha(a);
        sh.color = shadow;
    }
    let want = if a > 0.0 {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut column {
        v.set_if_neq(want);
    }
}

/// What the gun HUD's column says, by system: Modern the magazine and the reserve over the gun's
/// name and its gear (the special round loaded, the grenades left); Classic nothing of its own
/// (health and mana are the stock unit frames', the hands' spells the bar's), only the melee
/// weapon while it is up; Utility the tool in the hands and its gather.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ColumnText {
    pub ammo: String,
    pub name: String,
    pub gear: String,
    /// The gather bar's fill, while one runs.
    pub gather: Option<f32>,
}

/// Modern's gear line: the Armory's special round when one is loaded, then the grenades left.
pub fn gear_line(round: u8, lethal: u32, tactical: u32) -> String {
    let mut parts = Vec::new();
    if round != 0
        && let Some((name, _)) = crate::fusion::ROUNDS.get(usize::from(round))
    {
        parts.push(format!("{name} rounds"));
    }
    parts.push(format!("Lethal {lethal}"));
    parts.push(format!("Tactical {tactical}"));
    parts.join("   ")
}

/// The column for a system. `gun` is the ammunition text and the gun's name; `melee` names the
/// melee weapon while it is up (V), in place of the gun.
pub fn column_text(
    system: crate::action_systems::System,
    gun: Option<(String, String)>,
    melee: Option<&str>,
    gear: String,
    tool: Option<crate::action_systems::Ability>,
    gather: Option<f32>,
) -> ColumnText {
    use crate::action_systems::{Ability, System};
    match system {
        System::Modern | System::Companion => match (gun, melee) {
            (_, Some(m)) => ColumnText {
                name: m.to_string(),
                ..default()
            },
            (Some((ammo, name)), None) => ColumnText {
                ammo,
                name,
                gear,
                gather: None,
            },
            (None, None) => ColumnText::default(),
        },
        System::Classic => ColumnText {
            name: melee.unwrap_or_default().to_string(),
            ..default()
        },
        System::Utility => ColumnText {
            name: tool.unwrap_or(Ability::BareHands).name().to_string(),
            gather,
            ..default()
        },
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw_column(
    time: Res<Time>,
    weapon: Res<Weapon>,
    loadout: Res<Loadout>,
    systems: Res<crate::action_systems::ActionSystems>,
    equip: Res<crate::equipment::EquipState>,
    fusion: Res<crate::fusion::Fusion>,
    tools: Res<crate::tools::Tools>,
    mut name: Query<&mut Text, (With<WeaponName>, Without<AmmoText>, Without<GearText>)>,
    mut ammo: Query<
        (&mut Text, &mut TextColor),
        (With<AmmoText>, Without<WeaponName>, Without<GearText>),
    >,
    mut gear: Query<&mut Text, (With<GearText>, Without<AmmoText>, Without<WeaponName>)>,
    mut gather_bar: Query<&mut Visibility, With<GatherBar>>,
    mut gather_fill: Query<&mut Node, With<GatherFill>>,
    held: Res<crate::melee::MeleeHeld>,
    melee_weapon: Res<crate::melee::MeleeWeapon>,
) {
    // The melee weapon up (V): its kind in place of the gun, and no ammunition.
    let melee_up = loadout.fire_mode == FireMode::Melee || loadout.shield || held.0;
    let reserve = match weapon.reserve.or(weapon.server_reserve) {
        Some(n) => n.to_string(),
        // FRIZQT has no infinity glyph.
        None => "--".to_string(),
    };
    let gun = (!melee_up).then(|| {
        let a = if weapon.akimbo {
            // Each gun's magazine, left gun first as they sit in the hands.
            format!("{} | {} / {}", weapon.mag_left, weapon.mag, reserve)
        } else {
            format!("{} / {}", weapon.mag, reserve)
        };
        (a, loadout.weapon.clone())
    });
    let melee = if held.0 {
        Some(crate::melee::kind_name(
            melee_weapon.subclass,
            melee_weapon.two_handed,
        ))
    } else if melee_up {
        Some(loadout.weapon.as_str())
    } else {
        None
    };
    let t = time.elapsed_secs();
    let gather = tools
        .progress
        .filter(|g| t < g.until && Some(g.tool) == systems.tool_out())
        .map(|g| g.share());
    let col = column_text(
        systems.active,
        gun,
        melee,
        gear_line(fusion.round, equip.lethal_left, equip.tactical_left),
        systems.tool_out(),
        gather,
    );
    for mut s in &mut name {
        set(&mut s, &col.name);
    }
    for mut s in &mut gear {
        set(&mut s, &col.gear);
    }
    let low = !melee_up
        && weapon.mag * 4 <= crate::weapon::hand_mag_size(loadout.mag_size, weapon.akimbo);
    for (mut s, mut c) in &mut ammo {
        set(&mut s, &col.ammo);
        // Low reads red, as the stock out-of-range red; a reload greys the count until it is in.
        c.0 = ammo_colour(low, weapon.reloading());
    }
    let want = if col.gather.is_some() {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut gather_bar {
        v.set_if_neq(want);
    }
    for mut n in &mut gather_fill {
        let w = percent(col.gather.unwrap_or(0.0) * 100.0);
        if n.width != w {
            n.width = w;
        }
    }
}

/// The ammunition count's colour: grey while reloading, red when the magazine is low.
pub fn ammo_colour(low: bool, reloading: bool) -> Color {
    if reloading {
        GREY_TEXT
    } else if low {
        RED
    } else {
        WHITE
    }
}

// ── The gun HUD's seat ───────────────────────────────────────────────────────────────────────────

/// The ammunition's height and the weapon name's, in WoW UI units (`NumberFontNormalHuge`'s
/// neighbourhood and `GameFontNormal`'s), drawn at the client's seam so they scale as the stock
/// action bar does.
const AMMO_UNITS: f32 = 18.0;
const NAME_UNITS: f32 = 11.0;
/// The gear line's height (`GameFontHighlightSmall`'s).
const GEAR_UNITS: f32 = 10.0;
/// The gather and sprint gauges: their width and heights, in UI units.
const GAUGE_W_UNITS: f32 = 96.0;
const GATHER_H_UNITS: f32 = 6.0;
const STAMINA_H_UNITS: f32 = 3.0;
/// The column's size at its fullest, in UI units: "12 | 30 / 210" over a long gun name, the
/// gear line, the gather bar and the sprint gauge. The layout tests seat this box.
pub const COLUMN_UNITS: Vec2 = Vec2::new(
    170.0,
    AMMO_UNITS + NAME_UNITS + GEAR_UNITS + GATHER_H_UNITS + 2.0 + STAMINA_H_UNITS + 3.0 + 6.0,
);
/// The UAV's label and the killstreaks' timers: top left, under the player frame
/// (`PlayerFrame`, 100 units tall from the screen's top), in UI units.
pub const TOP_LEFT_UNITS: Vec2 = Vec2::new(20.0, 106.0);
/// UI units kept between the gun HUD and the bag bar (or a bar above it), and from the screen's
/// right edge.
pub const GAP_UNITS: f32 = 6.0;
/// Where the column sits before the interface has been read (logical pixels).
const FALLBACK_RIGHT: f32 = 36.0;
const FALLBACK_BOTTOM: f32 = 130.0;
/// Seconds between reads of the bag bar.
const BAG_POLL_SECS: f32 = 0.5;
/// `GRAY_FONT_COLOR`.
const GREY_TEXT: Color = Color::srgb(0.5, 0.5, 0.5);

/// Where the stock bag bar sits, as fractions of the screen from its bottom-left corner, and the
/// top of whatever stands on it (the bottom-right multibar, when shown over the bags).
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq)]
pub struct BagBar {
    pub rect: Option<ScreenRect>,
}

/// A rectangle in screen fractions, `y` up as WoW's frames measure it.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ScreenRect {
    pub left: f32,
    pub bottom: f32,
    pub right: f32,
    pub top: f32,
}

/// The Lua that measures the bag bar: the screen's size, then the union of the backpack, the four
/// bag slots and the key ring, then the highest top standing over them (the bottom-right
/// multibar, the XP bar or its max-level art, the watched reputation's bar: the gun HUD and its
/// sprint gauge sit above all of them), all in the same scale-1 units
/// (`GetLeft() × GetEffectiveScale()`).
pub const BAG_BAR_LUA: &str = r#"
local sw = UIParent:GetWidth() * UIParent:GetEffectiveScale()
local sh = UIParent:GetHeight() * UIParent:GetEffectiveScale()
local l, b, r, t
for _, n in ipairs({ "MainMenuBarBackpackButton", "CharacterBag0Slot", "CharacterBag1Slot",
                     "CharacterBag2Slot", "CharacterBag3Slot", "KeyRingButton" }) do
  local f = getglobal(n)
  if f and f:IsVisible() and f:GetLeft() then
    local s = f:GetEffectiveScale()
    local fl, fb, fr, ft = f:GetLeft() * s, f:GetBottom() * s, f:GetRight() * s, f:GetTop() * s
    l = l and math.min(l, fl) or fl
    b = b and math.min(b, fb) or fb
    r = r and math.max(r, fr) or fr
    t = t and math.max(t, ft) or ft
  end
end
if not l then return nil end
local floor = t
for _, n in ipairs({ "MultiBarBottomRight", "MainMenuExpBar", "MainMenuBarMaxLevelBar",
                     "ReputationWatchBar" }) do
  local over = getglobal(n)
  if over and over:IsVisible() and over:GetLeft() then
    local s = over:GetEffectiveScale()
    if over:GetLeft() * s < r and over:GetRight() * s > l then
      floor = math.max(floor, over:GetTop() * s)
    end
  end
end
return sw, sh, l, b, r, t, floor
"#;

/// The top of the action bar and what stacks on it, as a share of the screen's height from its
/// bottom: the class buffs sit over it.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq)]
pub struct BarStack {
    pub top: Option<f32>,
}

/// The Lua that measures [`BarStack`]: the screen's height, then the highest top among the bar,
/// its wells, the system's name, the XP and reputation bars, the bottom multibars, the stance,
/// pet and bonus bars, and the casting bar's seat (shown or not: the buffs do not jump while a
/// spell is cast), in scale-1 units.
pub const BAR_STACK_LUA: &str = r#"
local sh = UIParent:GetHeight() * UIParent:GetEffectiveScale()
local top
local function over(f, s)
  if f and f:GetTop() then
    local t = f:GetTop() * (s or f:GetEffectiveScale())
    top = top and math.max(top, t) or t
  end
end
for _, n in ipairs({ "MainMenuBar", "BenillaMw2SystemLabelFrame", "MainMenuExpBar", "ReputationWatchBar",
                     "MainMenuBarMaxLevelBar", "MultiBarBottomLeft", "MultiBarBottomRight",
                     "ShapeshiftBarFrame", "PetActionBarFrame", "BonusActionBarFrame", "ActionButton1" }) do
  local f = getglobal(n)
  if f and f:IsVisible() then over(f) end
end
if MainMenuBarTexture0 and MainMenuBarArtFrame and MainMenuBarArtFrame:IsVisible() then
  over(MainMenuBarTexture0, MainMenuBarArtFrame:GetEffectiveScale())
end
over(CastingBarFrame)
if not top or not (sh > 0) then return nil end
return sh, top
"#;

/// [`BAR_STACK_LUA`]'s answer as a share of the screen's height.
pub fn bar_stack_from_lua(sh: f32, top: f32) -> Option<f32> {
    (sh > 0.0 && sh.is_finite() && top.is_finite()).then(|| (top / sh).clamp(0.0, 1.0))
}

/// [`BAG_BAR_LUA`]'s answer as screen fractions: the bags' rectangle, its top raised to the
/// highest thing standing on them. `None` when the screen or the bags could not be measured.
pub fn bag_bar_from_lua(v: [f32; 7]) -> Option<ScreenRect> {
    let [sw, sh, l, b, r, t, floor] = v;
    if !(sw > 0.0 && sh > 0.0) || !v.iter().all(|x| x.is_finite()) || r <= l || t <= b {
        return None;
    }
    Some(ScreenRect {
        left: l / sw,
        bottom: b / sh,
        right: r / sw,
        top: floor.max(t) / sh,
    })
}

/// The column's `right` and `bottom` (logical pixels) on a `viewport`: its right edge on the
/// bags' right edge (kept `gap` pixels inside the screen), its bottom `gap` above the bags.
pub fn ammo_seat(bags: Option<ScreenRect>, viewport: Vec2, gap: f32) -> (f32, f32) {
    let Some(bags) = bags else {
        return (FALLBACK_RIGHT, FALLBACK_BOTTOM);
    };
    let right = (viewport.x * (1.0 - bags.right)).max(gap);
    let bottom = viewport.y * bags.top + gap;
    (right, bottom)
}

fn read_bag_bar(
    time: Res<Time>,
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut next: Local<f32>,
    mut bags: ResMut<BagBar>,
    mut stack: ResMut<BarStack>,
) {
    let t = time.elapsed_secs();
    if !state.in_world || t < *next {
        return;
    }
    *next = t + BAG_POLL_SECS;
    let Some(script) = script else {
        return;
    };
    // A quiet read: an interface without the bags answers nil, which is no error.
    type O = Option<f32>;
    let v = script.eval::<(O, O, O, O, O, O, O)>(BAG_BAR_LUA).ok();
    let rect = v.and_then(|(a, b, c, d, e, f, g)| bag_bar_from_lua([a?, b?, c?, d?, e?, f?, g?]));
    if bags.rect != rect {
        bags.rect = rect;
    }
    let top = script
        .eval::<(Option<f32>, Option<f32>)>(BAR_STACK_LUA)
        .ok()
        .and_then(|(sh, top)| bar_stack_from_lua(sh?, top?));
    if stack.top != top {
        stack.top = top;
    }
}

/// Pixels per UI unit this frame, and every [`FontUnits`] text sized by it.
fn scale_hud(
    seam: Option<Res<crate::wow_ui::UiSeam>>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut scale: ResMut<HudScale>,
    mut fonts: Query<(&FontUnits, &mut TextFont)>,
) {
    let Ok(w) = window.single() else {
        return;
    };
    let ui_scale = seam.map_or(0.9, |s| s.ui_scale);
    let k = crate::wow_ui::seam_scale(w.height(), ui_scale).clamp(0.4, 4.0);
    if (scale.0 - k).abs() > 1e-4 {
        scale.0 = k;
    }
    for (units, mut font) in &mut fonts {
        let size = units.0 * k;
        if (font.font_size - size).abs() > 0.01 {
            font.font_size = size;
        }
    }
}

/// Seat the gun HUD clear of the bag bar, its gauges and the UAV's label at the client's seam.
#[allow(clippy::type_complexity)]
fn place_ammo(
    bags: Res<BagBar>,
    scale: Res<HudScale>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut column: Query<&mut Node, With<AmmoColumn>>,
    mut gauges: Query<
        (&mut Node, Has<GatherBar>),
        (Or<(With<GatherBar>, With<StaminaBar>)>, Without<AmmoColumn>),
    >,
    mut top_left: Query<
        (&mut Node, &TopLeftSlot),
        (Without<AmmoColumn>, Without<GatherBar>, Without<StaminaBar>),
    >,
) {
    let Ok(w) = window.single() else {
        return;
    };
    let k = scale.0;
    let (right, bottom) = ammo_seat(bags.rect, Vec2::new(w.width(), w.height()), GAP_UNITS * k);
    for mut n in &mut column {
        if n.right != px(right) || n.bottom != px(bottom) {
            n.right = px(right);
            n.bottom = px(bottom);
        }
    }
    for (mut n, gather) in &mut gauges {
        let h = if gather {
            GATHER_H_UNITS
        } else {
            STAMINA_H_UNITS
        };
        let (gw, gh) = (px(GAUGE_W_UNITS * k), px(h * k));
        if n.width != gw || n.height != gh {
            n.width = gw;
            n.height = gh;
        }
    }
    for (mut n, slot) in &mut top_left {
        let (l, t) = (
            px(TOP_LEFT_UNITS.x * k),
            px((TOP_LEFT_UNITS.y + slot.0) * k),
        );
        if n.left != l || n.top != t {
            n.left = l;
            n.top = t;
        }
    }
}

/// MW2's radar on WoW's minimap: every hostile unit while a UAV is up, otherwise the units we
/// are fighting and those attacking us (MW2's red dots for enemies firing).
#[allow(clippy::too_many_arguments)]
fn mark_minimap(
    time: Res<Time>,
    state: Res<FpsState>,
    streak: Res<Streak>,
    combat: Res<Combat>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    mut marks: ResMut<benilla_app::fusion::MinimapMarks>,
    mut label: Query<&mut Text, With<RadarLabel>>,
) {
    let t = time.elapsed_secs();
    let uav = streak.uav_active(t);
    for mut s in &mut label {
        if uav && state.active {
            set(&mut s, &format!("UAV {:.0}", streak.uav_until - t));
        } else {
            set(&mut s, "");
        }
    }
    marks.0.clear();
    if !state.active {
        return;
    }
    let engaged: Vec<u64> = combat.engaged().collect();
    let pulse = 0.7 + 0.3 * (t * 4.0).sin().abs();
    for (guid, entity, fields) in objects.iter() {
        if Some(guid) == state.self_guid || fields.unit_health().unwrap_or(0) == 0 {
            continue;
        }
        let player = match fields.object_type() {
            Some(ObjectType::Unit) if fields.unit_npc_flags() == 0 => false,
            Some(ObjectType::Player) => true,
            _ => continue,
        };
        let attacking = state.self_guid.is_some() && fields.unit_target() == state.self_guid;
        // A ghillie mantle keeps a player off the UAV's sweep (crate::prestige).
        let ghillie = player
            && fields
                .player_visible_item_entry(crate::prestige::SLOT_BACK)
                .is_some_and(crate::prestige::is_ghillie);
        if !radar_shows(uav && !ghillie, engaged.contains(&guid), attacking) {
            continue;
        }
        let Ok(gt) = bodies.get(entity) else {
            continue;
        };
        let [x, y, _] = crate::fusion::to_wow(gt.translation());
        let [r, g, b] = if player {
            [1.0, 0.55, 0.1]
        } else {
            [1.0, 0.12, 0.1]
        };
        marks.0.push(benilla_app::fusion::MinimapMark {
            wow_xy: [x, y],
            rgba: [r, g, b, pulse],
            scale: 1.0,
        });
        if marks.0.len() == RADAR_BLIPS {
            break;
        }
    }
}

/// Whether the radar shows a hostile unit: all of them under a UAV, else those we fight and
/// those attacking us.
pub fn radar_shows(uav: bool, engaged: bool, attacking_us: bool) -> bool {
    uav || engaged || attacking_us
}

fn draw_nuke(
    time: Res<Time>,
    streak: Res<Streak>,
    mut text_q: Query<&mut Text, With<NukeText>>,
    mut flash: Query<&mut BackgroundColor, With<Flash>>,
) {
    let t = time.elapsed_secs();
    for mut s in &mut text_q {
        match streak.nuke_at {
            Some(at) => set(
                &mut s,
                &format!("TACTICAL NUKE INCOMING  {:.0}", (at - t).max(0.0).ceil()),
            ),
            None => set(&mut s, ""),
        }
    }
    for mut bg in &mut flash {
        bg.0 = Color::srgba(1.0, 0.97, 0.9, streak.flash.min(1.0));
    }
}

/// Write a text only when it changed, so an unchanged frame relays nothing out.
/// MW2's mantle hint, while a terrain ledge is in front (`crate::movement`).
fn draw_mantle_hint(
    hint: Res<crate::movement::MantleHint>,
    keymap: Res<crate::keybinds::Keymap>,
    mut text: Query<(&mut Visibility, &mut Text), With<MantleHintText>>,
) {
    let want = if hint.show {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for (mut v, mut t) in &mut text {
        if *v != want {
            *v = want;
        }
        // WoW's jump key, as bound now.
        if hint.show {
            let s = format!("Press {} to climb", keymap.jump_label().to_uppercase());
            if t.0 != s {
                t.0 = s;
            }
        }
    }
}

/// The sprint stamina bar: hidden at full, otherwise filled to what is left.
fn draw_stamina(
    sprint: Res<crate::sprint::Sprint>,
    mut bar: Query<&mut Visibility, With<StaminaBar>>,
    mut fill: Query<&mut Node, With<StaminaFill>>,
) {
    let share = (sprint.stamina / crate::sprint::SPRINT_MAX).clamp(0.0, 1.0);
    let want = if share < 1.0 {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut bar {
        if *v != want {
            *v = want;
        }
    }
    for mut n in &mut fill {
        let w = percent(share * 100.0);
        if n.width != w {
            n.width = w;
        }
    }
}

fn set(text: &mut Text, s: &str) {
    if text.0 != s {
        text.0.clear();
        text.0.push_str(s);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The stock bag bar in scale-1 screen units: the 1024-wide `MainMenuBar` centred at the
    /// bottom at `ui_scale`, its bags from x 820 to 1018 and y 2 to 41 of the bar, and the
    /// bottom-right multibar 42 tall on top of the bar when `multibar`.
    fn stock_bags(aspect: f32, ui_scale: f32, multibar: bool) -> [f32; 7] {
        let (sw, sh) = (768.0 * aspect, 768.0);
        let x0 = sw / 2.0 - 512.0 * ui_scale;
        let (l, r) = (x0 + 820.0 * ui_scale, x0 + 1018.0 * ui_scale);
        let (b, t) = (2.0 * ui_scale, 41.0 * ui_scale);
        let floor = if multibar { 95.0 * ui_scale } else { t };
        [sw, sh, l, b, r, t, floor]
    }

    /// The column's on-screen rectangle (pixels, y up) at its seat, at its widest likely text
    /// ("12 | 30 / 210" over a long gun name).
    fn column_rect(seat: (f32, f32), viewport: Vec2, k: f32) -> Rect {
        let size = COLUMN_UNITS * k;
        let right = viewport.x - seat.0;
        Rect::new(right - size.x, seat.1, right, seat.1 + size.y)
    }

    #[test]
    fn the_gun_hud_never_overlaps_the_bags_at_any_scale_or_shape() {
        for (w, h) in [
            (1024.0, 768.0),
            (1280.0, 720.0),
            (1920.0, 1080.0),
            (2560.0, 1080.0),
        ] {
            for ui_scale in [0.64, 0.9, 1.0] {
                for multibar in [false, true] {
                    let viewport = Vec2::new(w, h);
                    let raw = stock_bags(w / h, ui_scale, multibar);
                    let bags = bag_bar_from_lua(raw).expect("measured");
                    let k = crate::wow_ui::seam_scale(h, ui_scale);
                    let seat = ammo_seat(Some(bags), viewport, GAP_UNITS * k);
                    let col = column_rect(seat, viewport, k);
                    let bag_px =
                        Rect::new(bags.left * w, bags.bottom * h, bags.right * w, bags.top * h);
                    let what = format!("{w}x{h} at {ui_scale}, multibar {multibar}");
                    assert!(
                        col.intersect(bag_px).is_empty(),
                        "{what}: {col:?} on {bag_px:?}"
                    );
                    assert!(col.min.y >= bag_px.max.y, "{what}: above the bags");
                    // On screen, and its right edge on the bags' right edge.
                    assert!(col.max.x <= w && col.min.x >= 0.0, "{what}: on screen");
                    assert!(
                        (col.max.x - bag_px.max.x).abs() < 0.5,
                        "{what}: flush right"
                    );
                    // Close: no more than a bar's height above whatever it stands on.
                    assert!(col.min.y - bag_px.max.y <= 12.0 * k, "{what}: just above");
                }
            }
        }
    }

    #[test]
    fn an_unmeasured_bag_bar_keeps_the_old_seat() {
        assert_eq!(
            ammo_seat(None, Vec2::new(1920.0, 1080.0), 8.0),
            (36.0, 130.0)
        );
        assert_eq!(
            bag_bar_from_lua([0.0, 768.0, 1.0, 1.0, 2.0, 2.0, 2.0]),
            None
        );
        assert_eq!(
            bag_bar_from_lua([1024.0, 768.0, 5.0, 1.0, 2.0, 2.0, 2.0]),
            None
        );
        assert_eq!(
            bag_bar_from_lua([1024.0, 768.0, 1.0, 1.0, 2.0, 2.0, f32::NAN]),
            None
        );
    }

    #[test]
    fn the_ammo_count_greys_for_a_reload_and_reddens_when_low() {
        assert_eq!(ammo_colour(false, false), WHITE);
        assert_eq!(ammo_colour(true, false), RED);
        assert_eq!(ammo_colour(true, true), GREY_TEXT);
    }

    /// The measuring Lua runs in benilla's own VM and unions what is there: stand-in frames at
    /// the stock bag places, the key ring absent.
    #[test]
    fn the_bag_bar_lua_measures_the_stock_frames() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        s.run(
            r#"
            local root = CreateFrame("Frame", "UIParent")
            root:SetWidth(1024) root:SetHeight(768)
            root:SetPoint("BOTTOMLEFT", nil, "BOTTOMLEFT", 0, 0)
            UIParent = root
            local function at(name, x, y, w, h)
              local f = CreateFrame("Frame", name, UIParent)
              f:SetWidth(w) f:SetHeight(h)
              f:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", x, y)
              f:Show()
            end
            at("MainMenuBarBackpackButton", 981, 2, 37, 37)
            at("CharacterBag0Slot", 946, 4, 30, 30)
            at("CharacterBag3Slot", 841, 4, 30, 30)
            "#,
        )
        .expect("stand-ins");
        s.resolve();
        type O = Option<f32>;
        let (a, b, c, d, e, f, g) = s
            .eval::<(O, O, O, O, O, O, O)>(BAG_BAR_LUA)
            .expect("the bag bar Lua runs");
        let v = [a, b, c, d, e, f, g].map(|x| x.expect("a number"));
        let k = v[0] / 1024.0; // the VM's own UIParent scale
        let rect = bag_bar_from_lua(v).expect("measured");
        assert!((rect.left * v[0] - 841.0 * k).abs() < 0.5, "{v:?}");
        assert!((rect.right * v[0] - 1018.0 * k).abs() < 0.5, "{v:?}");
        assert!((rect.top * v[1] - 39.0 * k).abs() < 0.5, "{v:?}");
    }

    /// The sprint gauge (the gun HUD's bottom) sits above the XP bar and the watched
    /// reputation's bar where they run over the bags, as the stock main bar lays them (the XP
    /// bar the bar's full 1024 wide on its top edge, the reputation's above it), at common
    /// screen shapes.
    #[test]
    fn the_gun_hud_clears_the_xp_and_reputation_bars() {
        for (w, h) in [(1024.0f32, 768.0f32), (1920.0, 1080.0), (2560.0, 1080.0)] {
            let mut s = UiScript::new().expect("a VM");
            s.set_screen_size(w, h);
            s.run(
                r#"
                local root = CreateFrame("Frame", "UIParent")
                root:SetWidth(1024) root:SetHeight(768)
                root:SetPoint("BOTTOMLEFT", nil, "BOTTOMLEFT", 0, 0)
                UIParent = root
                local function at(name, x, y, w, h)
                  local f = CreateFrame("Frame", name, UIParent)
                  f:SetWidth(w) f:SetHeight(h)
                  f:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", x, y)
                  f:Show()
                end
                at("MainMenuBarBackpackButton", 981, 2, 37, 37)
                at("CharacterBag3Slot", 841, 4, 30, 30)
                at("MainMenuExpBar", 0, 40, 1024, 13)
                at("ReputationWatchBar", 0, 53, 1024, 11)
                "#,
            )
            .expect("stand-ins");
            s.resolve();
            type O = Option<f32>;
            let measure = |s: &UiScript| {
                let (a, b, c, d, e, f, g) = s
                    .eval::<(O, O, O, O, O, O, O)>(BAG_BAR_LUA)
                    .expect("the bag bar Lua runs");
                bag_bar_from_lua([a, b, c, d, e, f, g].map(|x| x.expect("a number")))
                    .expect("measured")
            };
            let rect = measure(&s);
            // In UIParent's units: the reputation bar's top, 53 + 11.
            assert!((rect.top * 768.0 - 64.0).abs() < 0.5, "{w}x{h}: {rect:?}");
            let viewport = Vec2::new(w, h);
            let (_, bottom) = ammo_seat(Some(rect), viewport, 6.0);
            assert!(
                bottom >= viewport.y * 64.0 / 768.0 + 6.0 - 0.01,
                "{w}x{h}: the gauge clears the bars"
            );
            // With the bars hidden (max level, nothing watched): back down on the bags.
            s.run("MainMenuExpBar:Hide() ReputationWatchBar:Hide()")
                .unwrap();
            assert!((measure(&s).top * 768.0 - 39.0).abs() < 0.5, "{w}x{h}");
        }
    }

    #[test]
    fn each_system_has_its_crosshair() {
        use crate::action_systems::System;
        let modern = crosshair_style(System::Modern);
        assert!(
            modern.bars && modern.bloom && !modern.dot,
            "MW2's blooming bars"
        );
        let classic = crosshair_style(System::Classic);
        assert!(classic.bars && !classic.bloom && !classic.dot, "still bars");
        assert_eq!(classic.color, GOLD);
        assert!(classic.length < modern.length);
        let utility = crosshair_style(System::Utility);
        assert!(
            !utility.bars && utility.dot,
            "a dot for the hands and tools"
        );
    }

    #[test]
    fn the_column_says_what_each_system_holds() {
        use crate::action_systems::{Ability, System};
        let gun = || Some(("30 / 210".to_string(), "M4A1".to_string()));
        let gear = gear_line(1, 2, 1);
        assert_eq!(gear, "Incendiary rounds   Lethal 2   Tactical 1");
        assert_eq!(
            gear_line(0, 0, 3),
            "Lethal 0   Tactical 3",
            "no plain round named"
        );
        let m = column_text(System::Modern, gun(), None, gear.clone(), None, None);
        assert_eq!(
            (m.ammo.as_str(), m.name.as_str(), m.gear.as_str()),
            ("30 / 210", "M4A1", gear.as_str())
        );
        // The melee weapon up: its name alone.
        let knife = column_text(
            System::Modern,
            gun(),
            Some("Dagger"),
            gear.clone(),
            None,
            None,
        );
        assert_eq!(
            (
                knife.ammo.as_str(),
                knife.name.as_str(),
                knife.gear.as_str()
            ),
            ("", "Dagger", "")
        );
        // Classic: nothing of the gun's; health and mana are the unit frames'.
        let c = column_text(System::Classic, gun(), None, gear.clone(), None, None);
        assert_eq!(c, ColumnText::default());
        // Utility: the tool in the hands and its gather.
        let u = column_text(
            System::Utility,
            gun(),
            None,
            gear.clone(),
            Some(Ability::MiningPick),
            Some(0.5),
        );
        assert_eq!(
            (u.name.as_str(), u.ammo.as_str(), u.gather),
            ("Mining Pick", "", Some(0.5))
        );
        let bare = column_text(System::Utility, None, None, gear, None, None);
        assert_eq!(bare.name, "Hands");
    }

    #[test]
    fn the_bar_stack_is_a_share_of_the_screen() {
        assert_eq!(bar_stack_from_lua(768.0, 76.8), Some(0.1));
        assert_eq!(bar_stack_from_lua(0.0, 10.0), None);
        assert_eq!(bar_stack_from_lua(768.0, f32::NAN), None);
    }

    #[test]
    fn the_radar_shows_what_a_uav_or_a_fight_reveals() {
        use super::radar_shows;
        assert!(radar_shows(true, false, false));
        assert!(radar_shows(false, true, false));
        assert!(radar_shows(false, false, true)); // firing at us
        assert!(!radar_shows(false, false, false));
    }

    /// The HUD's banner under the real `show_hud` and `draw_banner`, the layer on, the clock
    /// stepping 100 ms a frame.
    fn banner_app() -> App {
        use bevy::time::TimeUpdateStrategy;
        let mut app = App::new();
        app.add_plugins(MinimalPlugins)
            .insert_resource(TimeUpdateStrategy::ManualDuration(
                std::time::Duration::from_millis(100),
            ))
            .insert_resource(FpsState {
                active: true,
                ..Default::default()
            })
            .insert_resource(BannerClock { born: -10.0 })
            .add_message::<Banner>()
            .add_systems(Update, (show_hud, draw_banner).chain());
        let font = Handle::<Font>::default();
        app.world_mut()
            .commands()
            .spawn((HudRoot, Visibility::Hidden))
            .with_children(|root| spawn_banner(root, &font));
        app.world_mut().flush();
        app
    }

    /// Anything of the banner drawn: the HUD and its column shown, and some text with ink in
    /// its glyphs or its shadow.
    fn banner_shows(world: &mut World) -> bool {
        let shown = |v: &Visibility| *v != Visibility::Hidden;
        let root = world
            .query_filtered::<&Visibility, With<HudRoot>>()
            .iter(world)
            .all(shown);
        let column = world
            .query_filtered::<&Visibility, With<BannerBox>>()
            .iter(world)
            .all(shown);
        let ink = world
            .query::<(&Text, &TextColor, &TextShadow)>()
            .iter(world)
            .any(|(t, c, s)| !t.0.is_empty() && (c.0.alpha() > 0.0 || s.color.alpha() > 0.0));
        root && column && ink
    }

    /// A banner ("WORLD OF MODERN WARFARE") goes up and is gone by its fourth second: the glyphs, the
    /// drop shadow that keeps its own colour, and the column itself.
    #[test]
    fn the_banner_is_gone_after_four_seconds() {
        let mut app = banner_app();
        app.update();
        assert!(!banner_shows(app.world_mut()), "nothing before the banner");

        app.world_mut().write_message(Banner {
            title: "WORLD OF MODERN WARFARE".into(),
            subtitle: String::new(),
        });
        app.update();
        assert!(banner_shows(app.world_mut()), "the banner is up");
        let born = app.world().resource::<BannerClock>().born;
        let title = app
            .world_mut()
            .query_filtered::<&Text, With<BannerTitle>>()
            .single(app.world())
            .unwrap()
            .0
            .clone();
        assert_eq!(title, "WORLD OF MODERN WARFARE");

        let elapsed = |app: &App| app.world().resource::<Time>().elapsed_secs() - born;
        while elapsed(&app) < 2.0 {
            app.update();
        }
        assert!(banner_shows(app.world_mut()), "still up at 2 s");
        while elapsed(&app) < 4.0 {
            app.update();
        }
        assert!(!banner_shows(app.world_mut()), "gone at 4 s");
        let column = app
            .world_mut()
            .query_filtered::<&Visibility, With<BannerBox>>()
            .single(app.world())
            .copied()
            .unwrap();
        assert_eq!(column, Visibility::Hidden, "the column is hidden whole");
        for (c, s) in app
            .world_mut()
            .query::<(&TextColor, &TextShadow)>()
            .iter(app.world())
        {
            assert_eq!(c.0.alpha(), 0.0);
            assert_eq!(s.color.alpha(), 0.0, "the shadow fades with the text");
        }
    }
}
