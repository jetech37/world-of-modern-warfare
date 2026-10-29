//! Utility's tools (WOMW; the server's `Fusion::Gathering` and `Fusion::Maintenance`): the trades'
//! tools held in the hands in first person. A tool's slot key on Utility's bar readies it
//! (crate::action_systems), the server puts its model in the hands (the melee profile), and the
//! fire button swings it (crate::weapon's swing, with the tool's own stroke, crate::hand_anim).
//! Each swing that lands on what the tool works goes to the server as a strike (channel 10 op 3):
//!
//! - the Mining Pick at an ore vein, the Woodcutting Axe at a lumber tree, bare hands
//!   (Herbalism) at a herb, the Skinning Knife at a skinnable corpse: 2 to 4 strikes by the
//!   gatherer's skill, the last of which runs WoW's own gathering (the loot window, the skill-up);
//! - the Fishing Pole: a strike casts the line, the next reels it in (WoW's bobber);
//! - the Blacksmith Hammer at an anvil or forge opens Blacksmithing;
//! - the Repair Hammer at a damaged built piece mends it a tenth a strike, paid with its own
//!   materials; aimed at a piece it shows the piece's health and decay under the crosshair.
//!
//! Holding fire keeps striking at the tool's pace (not the pole, nor the smith's hammer). The
//! server answers each strike (op 4): the progress shows under the crosshair with a sound.

use avian3d::prelude::SpatialQuery;
use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use benilla_ui::script::UiScript;
use benilla_world::view::WorldCamera;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::action_systems::{Ability, ActionSystems, CHANNEL};
use crate::build::{Building, PieceBox};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::use_key::{Reticle, UseKind};

/// Client to server: a strike (`kOpStrike`): tool id u8, target u64.
pub const OP_STRIKE: u8 = 3;
/// Server to client: how it went (`kOpStruck`): tool u8, outcome u8, done u8, needed u8.
pub const OP_STRUCK: u8 = 4;
/// How far a hand tool reaches (yards, the server's `Gathering::kReach`).
pub const REACH: f32 = 5.5;

/// How a strike went (the server's `Gathering::Outcome`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    Struck,
    Gathered,
    Cast,
    Reeled,
    Repaired,
    NoTool,
    NotLearned,
    NoTarget,
    TooFar,
    WrongTarget,
    TooSoon,
    Failed,
    NoMaterials,
    NotDamaged,
    RecentlyDamaged,
    NotYours,
    Opened,
    /// Minstrelsy (crate::minstrel): the song begins, or why not.
    Performing,
    InCombat,
    SkillTooLow,
    NotAtInn,
    NotAtFire,
    Cooldown,
    NoSong,
}

impl Outcome {
    pub fn from_u8(v: u8) -> Option<Outcome> {
        use Outcome::*;
        const ALL: [Outcome; 24] = [
            Struck,
            Gathered,
            Cast,
            Reeled,
            Repaired,
            NoTool,
            NotLearned,
            NoTarget,
            TooFar,
            WrongTarget,
            TooSoon,
            Failed,
            NoMaterials,
            NotDamaged,
            RecentlyDamaged,
            NotYours,
            Opened,
            Performing,
            InCombat,
            SkillTooLow,
            NotAtInn,
            NotAtFire,
            Cooldown,
            NoSong,
        ];
        ALL.get(usize::from(v)).copied()
    }

    /// A strike that did its work (as opposed to a refusal, which the server says itself).
    pub fn landed(self) -> bool {
        matches!(
            self,
            Outcome::Struck
                | Outcome::Gathered
                | Outcome::Cast
                | Outcome::Reeled
                | Outcome::Repaired
                | Outcome::Opened
                | Outcome::Performing
        )
    }
}

/// The server's answer to a strike.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Struck {
    pub tool: u8,
    pub outcome: Outcome,
    pub done: u8,
    pub needed: u8,
}

pub fn decode_struck(body: &[u8]) -> Option<Struck> {
    match body {
        [OP_STRUCK, tool, outcome, done, needed] => Some(Struck {
            tool: *tool,
            outcome: Outcome::from_u8(*outcome)?,
            done: *done,
            needed: *needed,
        }),
        _ => None,
    }
}

pub fn encode_strike(tool: Ability, target: u64) -> Vec<u8> {
    let mut b = vec![OP_STRIKE, tool.id() as u8];
    b.extend_from_slice(&target.to_le_bytes());
    b
}

/// What a tool works, as the client aims it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Aims {
    /// A GameObject: a vein, a herb, a lumber tree, an anvil.
    Object,
    /// A dead creature.
    Corpse,
    /// A built piece.
    Piece,
    /// Nothing aimed: the line goes out ahead (or the bobber comes in).
    Water,
    /// An instrument: played, not struck (crate::minstrel sends its strike with the song).
    Song,
}

pub fn aims(tool: Ability) -> Aims {
    match tool {
        a if a.is_instrument() => Aims::Song,
        Ability::SkinningKnife => Aims::Corpse,
        Ability::RepairHammer => Aims::Piece,
        Ability::FishingPole => Aims::Water,
        _ => Aims::Object,
    }
}

/// Whether holding fire keeps striking (a pole would reel its own cast in, the smith's hammer
/// would open the window again and again).
pub fn repeats(tool: Ability) -> bool {
    !matches!(tool, Ability::FishingPole | Ability::BlacksmithHammer) && !tool.is_instrument()
}

/// The strike's target: what the crosshair picked (its kind, guid and distance from us) or the
/// built piece under it (id, distance along the crosshair), as the tool works. None: the swing
/// cuts the air (nothing is sent).
pub fn strike_target(
    tool: Ability,
    picked: Option<(UseKind, u64, f32)>,
    piece: Option<(u32, f32)>,
) -> Option<u64> {
    // A little over the server's reach: it measures again, to the target's edge.
    let near = |d: f32| d <= REACH + 1.5;
    match aims(tool) {
        Aims::Water => Some(0),
        Aims::Song => None,
        Aims::Piece => piece.filter(|p| near(p.1)).map(|p| u64::from(p.0)),
        Aims::Corpse => picked
            .filter(|p| p.0 == UseKind::DeadUnit && near(p.2))
            .map(|p| p.1),
        Aims::Object => picked
            .filter(|p| p.0 == UseKind::GameObject && near(p.2))
            .map(|p| p.1),
    }
}

/// The line under the crosshair after a strike: the progress, or what came of it. None for a
/// refusal (the server says those itself) and a gather (the loot window opens).
pub fn struck_text(s: &Struck) -> Option<String> {
    let name = Ability::from_id(u32::from(s.tool)).map_or("Tool", |a| a.name());
    match s.outcome {
        Outcome::Struck if s.needed > 1 => Some(format!("{name}  {}/{}", s.done, s.needed)),
        Outcome::Repaired => Some(format!("Repaired: {}%", s.done)),
        Outcome::Cast => Some("The line is out: strike again when it bites".into()),
        _ => None,
    }
}

/// WoW's sound for a strike that landed: a sound kit of the player's own install
/// (SoundEntries.dbc, by name as `PlaySound` takes it), the ones WoW's own gathering plays: the
/// pick on rock, a herb pulled, skinning, the line cast and reeled in, the smith's clang; the axe
/// a mace's blow on wood.
pub fn strike_sound(tool: Ability, outcome: Outcome) -> Option<&'static str> {
    if !outcome.landed() {
        return None;
    }
    Some(match (tool, outcome) {
        (Ability::FishingPole, Outcome::Cast) => SOUND_CAST,
        (Ability::FishingPole, _) => SOUND_REEL,
        (Ability::MiningPick, _) => SOUND_PICK,
        (Ability::WoodcuttingAxe, _) => SOUND_AXE,
        (Ability::SkinningKnife, _) => SOUND_KNIFE,
        (Ability::Herbalism, _) => SOUND_HERB,
        (Ability::BlacksmithHammer | Ability::RepairHammer, _) => SOUND_HAMMER,
        _ => return None,
    })
}

const SOUND_PICK: &str = "Gather_Mining";
const SOUND_AXE: &str = "Mace1H_ArmorWood";
const SOUND_KNIFE: &str = "Skinning";
const SOUND_HERB: &str = "Gather_Herb";
const SOUND_HAMMER: &str = "BlackSmithing";
const SOUND_CAST: &str = "Fishing Cast";
const SOUND_REEL: &str = "Fishing Reel in";

/// The tools' state: the swings already struck, the line under the crosshair and until when, and
/// the piece the Repair Hammer is aimed at.
#[derive(Resource, Default)]
pub struct Tools {
    seen_swings: u64,
    pub line: Option<(String, f32)>,
    pub aimed_piece: Option<u32>,
    /// The gather in progress, for the HUD's bar: the tool, the strikes done and needed, and
    /// until when it shows.
    pub progress: Option<Gather>,
}

/// A gather under way: [`Struck`]'s count, shown for a moment after each strike.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Gather {
    pub tool: Ability,
    pub done: u8,
    pub needed: u8,
    pub until: f32,
}

impl Gather {
    /// How far along it is, 0 to 1.
    pub fn share(&self) -> f32 {
        if self.needed == 0 {
            1.0
        } else {
            (f32::from(self.done) / f32::from(self.needed)).clamp(0.0, 1.0)
        }
    }
}

/// What a strike's answer makes of the gather: struck towards a count, it runs on; gathered, it
/// shows full for a moment; anything else leaves it as it was.
pub fn gather_after(s: &Struck, tool: Ability, now: f32, was: Option<Gather>) -> Option<Gather> {
    match s.outcome {
        Outcome::Struck if s.needed > 1 => Some(Gather {
            tool,
            done: s.done,
            needed: s.needed,
            until: now + GATHER_SHOW_SECS,
        }),
        Outcome::Gathered => Some(Gather {
            tool,
            done: s.needed.max(1),
            needed: s.needed.max(1),
            until: now + 0.6,
        }),
        _ => was,
    }
}

/// How long the gather bar stays up after a strike.
pub const GATHER_SHOW_SECS: f32 = 2.5;

#[derive(Component)]
struct ToolLine;

pub struct ToolsPlugin;

impl Plugin for ToolsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Tools>()
            .add_systems(Startup, spawn_line)
            .add_systems(
                Update,
                ready_tool
                    .in_set(Mw2Set::Mode)
                    .after(crate::action_systems::ActionSystemsSet),
            )
            .add_systems(
                Update,
                (strike, read_struck, show_line).chain().in_set(Mw2Set::Hud),
            );
    }
}

/// Before the weapon reads the trigger: a tool in the hands makes the swing a tool's strike, and
/// a held trigger strikes again as soon as the tool has swung.
fn ready_tool(state: Res<FpsState>, systems: Res<ActionSystems>, mut input: ResMut<Mw2Input>) {
    let tool = systems
        .tool_out()
        .filter(|_| state.active && !state.building);
    input.tool = tool.is_some();
    if let Some(tool) = tool
        && repeats(tool)
        && input.fire.held
    {
        input.fire.pressed = true;
    }
}

/// Each swing of a tool: what it struck goes to the server; with the Repair Hammer, the piece
/// under the crosshair is kept for the line.
#[allow(clippy::too_many_arguments)]
fn strike(
    state: Res<FpsState>,
    systems: Res<ActionSystems>,
    weapon: Res<crate::weapon::Weapon>,
    build: Res<Building>,
    reticle: Reticle,
    spatial: SpatialQuery,
    boxes: Query<&PieceBox>,
    camera: Query<&Transform, With<WorldCamera>>,
    mut tools: ResMut<Tools>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let swung = weapon.knife_swings != tools.seen_swings;
    tools.seen_swings = weapon.knife_swings;
    let tool = systems
        .tool_out()
        .filter(|_| state.active && !state.building);
    let Some(tool) = tool else {
        tools.aimed_piece = None;
        return;
    };
    let Ok(cam) = camera.single() else {
        return;
    };
    let ray = Ray3d::new(cam.translation, cam.forward());
    let piece = (aims(tool) == Aims::Piece)
        .then(|| {
            spatial.cast_ray(
                ray.origin,
                ray.direction,
                REACH + 1.5,
                true,
                &benilla_world::collision::WorldCollision::body_filter(),
            )
        })
        .flatten()
        .and_then(|h| Some((boxes.get(h.entity).ok()?.0, h.distance)))
        .filter(|(id, _)| build.pieces.contains_key(id));
    tools.aimed_piece = piece.map(|p| p.0);
    if !swung {
        return;
    }
    let picked = match aims(tool) {
        Aims::Object | Aims::Corpse => reticle
            .pick(ray, state.self_guid)
            .map(|p| (p.hit.kind, p.guid, p.apart.unwrap_or(p.hit.distance))),
        _ => None,
    };
    let Some(target) = strike_target(tool, picked, piece) else {
        return;
    };
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL,
        body: encode_strike(tool, target),
    }));
}

/// The server's answers: the line and the sound; at an anvil, Blacksmithing's own window.
fn read_struck(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut tools: ResMut<Tools>,
    script: Option<NonSend<UiScript>>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL {
            continue;
        }
        let Some(s) = decode_struck(&m.body) else {
            continue;
        };
        let Some(tool) = Ability::from_id(u32::from(s.tool)) else {
            continue;
        };
        if let Some(text) = struck_text(&s) {
            tools.line = Some((text, time.elapsed_secs() + 1.5));
        }
        tools.progress = gather_after(&s, tool, time.elapsed_secs(), tools.progress);
        let Some(script) = script.as_ref() else {
            continue;
        };
        if let Some(sound) = strike_sound(tool, s.outcome) {
            crate::lua::sound(script, sound);
        }
        // The smith's trade window, as its spellbook entry opens it.
        if s.outcome == Outcome::Opened {
            crate::lua::run(script, "CastSpellByName(\"Blacksmithing\")");
        }
    }
}

fn spawn_line(mut commands: Commands, assets: Res<AssetServer>) {
    commands.spawn((
        Name::new("mw2 tool line"),
        ToolLine,
        Node {
            position_type: PositionType::Absolute,
            left: percent(50),
            top: percent(62),
            margin: UiRect::left(px(-250)),
            width: px(500),
            justify_content: JustifyContent::Center,
            ..default()
        },
        Text::new(""),
        crate::hud::FontUnits(12.0),
        TextFont::from_font_size(15.0).with_font(assets.load(crate::wow_ui::FONT)),
        TextColor(Color::srgb(0.95, 0.95, 0.95)),
        TextLayout::new_with_justify(Justify::Center),
        TextShadow {
            offset: Vec2::splat(1.5),
            color: Color::srgba(0.0, 0.0, 0.0, 0.85),
        },
        GlobalZIndex(i32::MAX - 1),
        Pickable::IGNORE,
    ));
}

/// The line under the crosshair: a strike's progress for a moment, else the piece the Repair
/// Hammer is aimed at (its health and decay).
fn show_line(
    time: Res<Time>,
    state: Res<FpsState>,
    tools: Res<Tools>,
    build: Res<Building>,
    mut lines: Query<&mut Text, With<ToolLine>>,
) {
    let t = time.elapsed_secs();
    let s = match (&tools.line, tools.aimed_piece) {
        _ if !state.active => String::new(),
        (Some((text, until)), _) if t < *until => text.clone(),
        (_, Some(id)) => crate::build::repair_text(&build, id),
        _ => String::new(),
    };
    for mut text in &mut lines {
        if text.0 != s {
            text.0 = s.clone();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_gather_fills_with_each_strike_and_shows_full_when_done() {
        let struck = |outcome, done, needed| Struck {
            tool: 10,
            outcome,
            done,
            needed,
        };
        let g = gather_after(
            &struck(Outcome::Struck, 1, 3),
            Ability::MiningPick,
            10.0,
            None,
        )
        .expect("under way");
        assert!((g.share() - 1.0 / 3.0).abs() < 1e-6);
        assert_eq!(g.until, 10.0 + GATHER_SHOW_SECS);
        let g = gather_after(
            &struck(Outcome::Struck, 2, 3),
            Ability::MiningPick,
            11.0,
            Some(g),
        )
        .unwrap();
        assert!((g.share() - 2.0 / 3.0).abs() < 1e-6);
        let done = gather_after(
            &struck(Outcome::Gathered, 3, 3),
            Ability::MiningPick,
            12.0,
            Some(g),
        )
        .unwrap();
        assert_eq!(done.share(), 1.0);
        // One strike's gather has no count to fill.
        assert_eq!(
            gather_after(
                &struck(Outcome::Struck, 1, 1),
                Ability::Herbalism,
                1.0,
                None
            ),
            None
        );
        // A refusal leaves it be.
        assert_eq!(
            gather_after(
                &struck(Outcome::Cast, 0, 0),
                Ability::FishingPole,
                1.0,
                Some(done)
            ),
            Some(done)
        );
    }

    #[test]
    fn the_strike_and_its_answer_match_the_server() {
        // The server's action_system_tool_ops_round_trip.
        let b = encode_strike(Ability::RepairHammer, 0x0102_0304_0506_0708);
        assert_eq!(b, vec![3, 16, 8, 7, 6, 5, 4, 3, 2, 1]);
        assert_eq!(
            decode_struck(&[4, 10, 1, 2, 3]),
            Some(Struck {
                tool: 10,
                outcome: Outcome::Gathered,
                done: 2,
                needed: 3
            })
        );
        assert_eq!(decode_struck(&[4, 10, 24, 0, 0]), None);
        assert_eq!(Outcome::from_u8(17), Some(Outcome::Performing));
        assert_eq!(Outcome::from_u8(23), Some(Outcome::NoSong));
        assert_eq!(decode_struck(&[4, 10, 1, 2]), None);
        assert_eq!(Outcome::from_u8(16), Some(Outcome::Opened));
        assert_eq!(
            crate::action_systems::tool_body(Some(Ability::MiningPick)),
            vec![2, 10]
        );
        assert_eq!(crate::action_systems::tool_body(None), vec![2, 0]);
    }

    #[test]
    fn each_tool_strikes_what_it_works_within_reach() {
        let vein = Some((UseKind::GameObject, 77, 3.0));
        let corpse = Some((UseKind::DeadUnit, 88, 2.0));
        let far = Some((UseKind::GameObject, 77, 9.0));
        assert_eq!(strike_target(Ability::MiningPick, vein, None), Some(77));
        assert_eq!(strike_target(Ability::MiningPick, far, None), None);
        assert_eq!(strike_target(Ability::MiningPick, corpse, None), None);
        assert_eq!(
            strike_target(Ability::SkinningKnife, corpse, None),
            Some(88)
        );
        assert_eq!(strike_target(Ability::SkinningKnife, vein, None), None);
        assert_eq!(strike_target(Ability::Herbalism, vein, None), Some(77));
        assert_eq!(strike_target(Ability::FishingPole, None, None), Some(0));
        assert_eq!(
            strike_target(Ability::RepairHammer, vein, Some((5, 2.0))),
            Some(5)
        );
        assert_eq!(strike_target(Ability::RepairHammer, vein, None), None);
        assert_eq!(
            strike_target(Ability::RepairHammer, None, Some((5, 9.0))),
            None
        );
        // Holding fire: not the pole (it would reel its own cast), not the smith's hammer.
        assert!(repeats(Ability::MiningPick) && repeats(Ability::RepairHammer));
        assert!(!repeats(Ability::FishingPole) && !repeats(Ability::BlacksmithHammer));
    }

    #[test]
    fn a_strike_says_its_progress_and_lands_a_sound() {
        let s = |outcome, done, needed| Struck {
            tool: Ability::MiningPick.id() as u8,
            outcome,
            done,
            needed,
        };
        assert_eq!(
            struck_text(&s(Outcome::Struck, 2, 4)).as_deref(),
            Some("Mining Pick  2/4")
        );
        assert_eq!(struck_text(&s(Outcome::Gathered, 4, 4)), None);
        assert_eq!(struck_text(&s(Outcome::NoTool, 0, 0)), None);
        assert_eq!(
            struck_text(&s(Outcome::Repaired, 80, 100)).as_deref(),
            Some("Repaired: 80%")
        );
        assert!(strike_sound(Ability::MiningPick, Outcome::Struck).is_some());
        assert!(strike_sound(Ability::MiningPick, Outcome::TooFar).is_none());
        assert_eq!(
            strike_sound(Ability::FishingPole, Outcome::Cast),
            Some(SOUND_CAST)
        );
    }

    #[test]
    fn tools_go_on_utility_alone_and_ready_from_its_bar() {
        use crate::action_systems::{System, packed, write_allowed};
        for a in Ability::ALL.into_iter().filter(|a| a.is_tool()) {
            assert_eq!(a.systems(), &[System::Utility]);
            assert!(write_allowed(61, packed(a)), "{a:?} on Utility's bar");
            assert!(!write_allowed(1, packed(a)), "{a:?} not on Classic's");
            assert!(!write_allowed(193, packed(a)), "{a:?} not on Modern's");
            assert!(!write_allowed(80, packed(a)), "{a:?} not on a form page");
        }
        assert!(!Ability::BareHands.is_tool());
        assert!(Ability::RepairHammer.is_tool() && Ability::MiningPick.is_tool());
    }
}
