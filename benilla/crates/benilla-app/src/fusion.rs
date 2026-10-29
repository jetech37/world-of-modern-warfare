//! Azeroth Warfare's fusion packets, opened to crates built on top (benilla-mw2): send this
//! tick's shots and the stance/gait state as [`Message`]s, and read the server's hits, kills and
//! streaks back as [`FusionEventsReceived`]; save and equip Create-a-Class classes
//! ([`SendFusionLoadout`], [`FusionLoadoutReceived`]). The wire format is `benilla_protocol::fusion`.

use bevy::prelude::*;

pub use benilla_protocol::fusion::{pellet_direction, pellet_hash};
pub use benilla_protocol::fusion::{
    ChannelMessage, CHANNEL_BUILD, CHANNEL_WORLD, MAX_CHANNEL_BODY,
};
pub use benilla_protocol::fusion::{
    ClassLoadout, Event, EventKind, Gait, LoadoutError, LoadoutReply, LoadoutRequest,
    LoadoutStatus, Shot, Stance, State, CLASS_NAME_MAX, CLASS_SLOTS, EVENT_ASSIST, EVENT_FATAL,
    EVENT_HEADSHOT, EVENT_LEFT, EVENT_PROGRESS, MAX_SHOTS_PER_BATCH, NO_CLASS, PERK_PRO_MASK,
    SHOT_ADS, SHOT_AIRBORNE, SHOT_CLAIMS_HEAD, SHOT_LEFT, STATE_AIMING, STATE_FFA_OFF,
    STATE_FFA_ON, STATE_LUNGE, STATE_MANTLE, STATE_REARM, STATE_RELOADING, STATE_RELOAD_ONE,
};
use benilla_protocol::{SessionEvent, SessionEventKind};

pub use crate::fusion_bar::{
    FusionActionBar, FusionLook, FusionPress, MainBarView, ReadiedAction, SlotMark, WriteRule,
    ACTION_KIND_FUSION, MAIN_BAR_BUTTONS,
};

use crate::net::handlers::NetHandlerApp;
use crate::net::{ClientCommand, NetCommands};

/// Dots a layer built on top (benilla-mw2's radar: enemies a UAV shows, enemies firing) asks
/// the minimap to draw this frame, in WoW world yards. The layer rewrites the list each frame.
#[derive(Resource, Debug, Clone, Default)]
pub struct MinimapMarks(pub Vec<MinimapMark>);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MinimapMark {
    /// WoW world x and y (yards).
    pub wow_xy: [f32; 2],
    /// The dot's colour.
    pub rgba: [f32; 4],
    /// Its size against a party dot's 0.6.
    pub scale: f32,
}

/// The pointer is over one of a layer's own windows (benilla-mw2's bevy_ui panels), or a drag
/// begun on one is in flight: the press is the window's, as a press on a stock frame is, so the
/// world neither picks nor turns the camera with it. The layer writes it each frame in
/// `PreUpdate`; `PointerOverUi` ORs it in.
#[derive(Resource, Debug, Clone, Copy, Default)]
pub struct LayerPointerOver(pub bool);

/// The layer's raw-input pass in `PreUpdate`. Synthetic keys (the capture probes) are written
/// before it, so the layer sees a probe's press the frame it is made, as it sees a real one.
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct LayerInput;

/// A layer's own key bindings in the stock Key Bindings window: its commands, under one header,
/// in a binding plane of their own (`benilla_ui::script::keybind::in_layer_plane`), so a layer key
/// may be a stock key too (each taken only while the layer has it). Inserted before world entry;
/// read at each VM's birth. The commands' bodies do nothing: the layer reads its keys from
/// [`LayerBoundKeys`] and takes their presses itself, and the dispatcher never runs them.
#[derive(Resource, Debug, Clone, Default)]
pub struct LayerBindings {
    /// The prefix every layer command name starts with (`WOMW_`).
    pub prefix: String,
    /// The section's header suffix (`BINDING_HEADER_<header>`) and its display text.
    pub header: (String, String),
    /// Each command and its name in the window (`BINDING_NAME_<command>`), in list order.
    pub commands: Vec<(String, String)>,
    /// `bind` lines applied over the stock `WTF\DefaultBindings.wtf`, in order: the layer's
    /// default keys, and a stock key with no command, which unbinds that stock default.
    pub defaults: Vec<(String, String)>,
}

/// The live key table as `(chord, command)` pairs, both planes, rebuilt whenever a binding
/// changes (a rebind, a set switch, a load): what a layer reads its own keys, and the stock keys
/// it follows (JUMP, the pet bar), from. `version` moves with each rebuild.
#[derive(Resource, Debug, Clone, Default)]
pub struct LayerBoundKeys {
    pub version: u64,
    pub keys: Vec<(String, String)>,
}

impl LayerBoundKeys {
    /// The chords bound to `command`, in bind order.
    pub fn keys_of<'a>(&'a self, command: &'a str) -> impl Iterator<Item = &'a str> + 'a {
        self.keys
            .iter()
            .filter(move |(_, c)| c.eq_ignore_ascii_case(command))
            .map(|(k, _)| k.as_str())
    }
}

/// What the active keyboard layout names a key in a binding chord (`R`, `F5`, `SPACE`), as the
/// dispatcher names a press; `None` for a modifier or a key 1.12 cannot bind.
pub fn binding_key_name(code: KeyCode, layout: &LayoutNames) -> Option<String> {
    crate::bindings::chord::key_token(code, layout).map(|n| n.to_string())
}

pub use crate::bindings::LayoutNames;

/// Greet a creature by guid, as a right-click on a gossip NPC does (the layer's scripted runs).
#[derive(Message, Clone, Copy, Debug)]
pub struct SendGossipHello(pub u64);

/// The layer's use key on what its crosshair is over (benilla-mw2's F): the world click's
/// select-and-act on `entity`, minus Click to Move's walk and the attack arm. A unit or player is
/// selected; with `interact`, the right click's talk, loot, skin, corpse or GameObject leg runs
/// too, inside the stock interact ranges, so a gossip NPC talks, a vendor opens, a corpse loots
/// and a chest opens. A hostile is only selected.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct LayerUse {
    pub entity: Entity,
    pub interact: bool,
}

/// A layer draws the player's own body in first person (benilla-mw2's headless copy): the stock
/// first-person fade still hides the model, but the mount stays drawn (its subtree takes the
/// fade's release, the rider's seat and what hangs from it excepted) and the blob shadow stays
/// cast. Off, the stock fade hides all three.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct FirstPersonBody(pub bool);

/// A layer flies the server's GM flight (levitating swim) by its own physics: while `active` and
/// the mover levitates, each swim frame moves at `forward` yards per second along the level
/// facing plus `up`, whatever the keys (FORWARD stays on the wire, Space breaches nothing), with
/// the mover pitch held in `pitch_limits`. The mover writes back the pitch it flew and whether it
/// stood on ground. Not a 1.12 behaviour: the stock GM flight is the plain swim.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq)]
pub struct LayerFlight {
    pub active: bool,
    pub forward: f32,
    pub up: f32,
    /// Radians, up positive: (lowest, highest).
    pub pitch_limits: (f32, f32),
    /// Written by the mover each levitating frame.
    pub pitch: f32,
    pub grounded: bool,
    /// Whether the mover is in the levitating swim now (the server's grant arrived).
    pub levitating: bool,
}

/// A layer drives the animation of the mount the player rides (benilla-mw2's skyriding: glide,
/// flap, dive, take-off and landing poses), over the stock locomotion pick, which in GM flight is
/// the swim. While `play` holds a request the driver arms exactly that clip on the mount body
/// (no fallback chain: the layer picks among `clips`), cross-fading over `blend` seconds, and
/// writes its playback rate every frame. A new `serial` re-arms the same id (a second flap).
/// The driver writes back the mount body's entity, its clips and how far the armed one has run.
/// Not a 1.12 behaviour: the stock client plays the mount's locomotion only.
#[derive(Resource, Debug, Clone, Default, PartialEq)]
pub struct LayerMountAnim {
    pub play: Option<MountAnimRequest>,
    /// Written by the driver: the self player's mount body, if any.
    pub mount: Option<Entity>,
    /// Written by the driver: the mount model's clips, `(anim id, seconds, looping)`.
    pub clips: Vec<(u16, f32, bool)>,
    /// Written by the driver: seconds the armed layer clip has played (0 when none).
    pub elapsed: f32,
    /// A capture probe's: the request plays on every mount body, not only the player's.
    pub any_rider: bool,
}

/// A layer holds a standing unit at one settled frame of a clip (benilla-mw2's gun hold): by
/// unit, `(anim id, fraction of the clip's length)`. A moving unit, or one not listed, runs the
/// mode machine as before. Not a 1.12 behaviour.
#[derive(Resource, Debug, Clone, Default, PartialEq)]
pub struct LayerUnitHold(pub std::collections::HashMap<Entity, (u16, f32)>);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MountAnimRequest {
    pub id: u16,
    pub looping: bool,
    pub rate: f32,
    pub blend: f32,
    pub serial: u32,
}

/// A layer tilts the mount the player rides about a pivot in the mount body's frame (banking into
/// a turn, a pitch that eases): the driver of this seam writes the mount body's local transform
/// to `rotation` about `pivot` (yards, the host's frame) every frame, the identity when `None`.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq)]
pub struct LayerMountTilt(pub Option<(Quat, Vec3)>);

/// A layer's sound: a looping `SoundEntries` kit by name at `gain` (0..1, 2D) while `loop_kit` is
/// set (the wind of a fast flight), and one-shot kits queued in `once` by name and gain (a wing
/// flap), drained each frame.
#[derive(Resource, Debug, Clone, Default, PartialEq)]
pub struct LayerSound {
    pub loop_kit: Option<String>,
    pub loop_gain: f32,
    pub once: Vec<(String, f32)>,
}

/// A layer's camera cut: while on, the orbit distance jumps to its zoom target in the frame the
/// target moves (benilla-mw2's first/third-person switch), not the stock constant-speed glide.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LayerZoomSnap(pub bool);

/// The layer's view is first person: the camera sits in the player's own head, so effects hung on
/// the player's body (the blood spurt) would fill the screen; they are skipped while this is on.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LayerFirstPerson(pub bool);

/// Transports whose pose the server streams, keyed by guid: a layer's own moving decks (Azeroth
/// Warfare's player ships, `design/PLAYER_SHIPS.md`). 1.12 has no such object, so the layer
/// fills this from its own channel; [`crate::transport`] arms a listed type-11 object (which it
/// would otherwise park, having no `TransportAnimation` path) as a streamed transport and
/// dead-reckons it from the latest pose. Riding keys on the transport, not its drive, so a
/// streamed deck carries its riders as a boat's does.
#[derive(Resource, Debug, Clone, Default)]
pub struct StreamedDecks(pub bevy::platform::collections::HashMap<u64, StreamedDeck>);

/// One streamed pose, raw WoW coordinates and radians, with its rates for dead reckoning.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct StreamedDeck {
    pub pos: [f32; 3],
    pub heading: f32,
    pub vel: [f32; 3],
    pub yaw_rate: f32,
    /// Bumped on each new pose from the server; the drive re-bases on a change.
    pub seq: u32,
}

/// The guid of the transport our own body stands on this frame, for a layer (`None`: none).
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LayerRiding(pub Option<u64>);

/// The layer is on: spells and abilities learned or unlearned (a kit swap trades many at once)
/// are not announced in chat (crate::spell::net); recipes still are.
#[derive(Resource, Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LayerQuietSpellbook(pub bool);

/// The look the player's own mount aura wears while the layer rides a mount of its own on a
/// borrowed vanilla mount spell (a battle companion ridden on the wolf's 580 / 23250): its name
/// and icon (`Interface\Icons\…`). Deviation, for the layer only: while set, a helpful aura of the
/// player's whose spell applies `SPELL_AURA_MOUNTED` shows these in `UnitBuff("player")`,
/// `GetPlayerBuffTexture` and `GameTooltip:SetPlayerBuff` (crate::ui_aura) instead of the
/// spell's own. Other units' auras are never touched.
#[derive(Resource, Debug, Clone, Default, PartialEq, Eq)]
pub struct LayerMountLook(pub Option<MountLook>);

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct MountLook {
    pub name: String,
    pub icon: String,
}

/// Shots fired this frame; batched into `CMSG_FUSION_SHOTS` of at most [`MAX_SHOTS_PER_BATCH`].
#[derive(Message, Clone, Debug)]
pub struct SendFusionShots(pub Vec<Shot>);

/// The stance, gait, held gun and FFA requests, sent when they change.
#[derive(Message, Clone, Copy, Debug)]
pub struct SendFusionState(pub State);

/// `SMSG_FUSION_EVENTS`: what the server's referee decided about our shots and others' at us.
#[derive(Message, Clone, Debug)]
pub struct FusionEventsReceived(pub Vec<Event>);

/// `CMSG_FUSION_LOADOUT`: save Create-a-Class classes and equip one.
#[derive(Message, Clone, Debug)]
pub struct SendFusionLoadout(pub LoadoutRequest);

/// `SMSG_FUSION_LOADOUT`: the saved classes and the equip's outcome.
#[derive(Message, Clone, Debug)]
pub struct FusionLoadoutReceived(pub LoadoutReply);

/// `CMSG_FUSION_CHANNEL`: a feature's message to the server.
#[derive(Message, Clone, Debug)]
pub struct SendFusionChannel(pub ChannelMessage);

/// `SMSG_FUSION_CHANNEL`: a feature's message from the server.
#[derive(Message, Clone, Debug)]
pub struct FusionChannelReceived(pub ChannelMessage);

/// A hand spell's free-aimed bolt (the layer's two-hand casting): fly `spell_id`'s own missile
/// model (`SpellVisual` field 7, with its field-10 flight sound, as a GO's projectile) in a straight
/// line from `from` to `to` (Bevy space) at `speed` yards a second. It never homes and hands
/// nothing off on arrival: the layer tells its end apart ([`FusionSpellImpact`]). `key` names it.
#[derive(Message, Clone, Copy, Debug)]
pub struct FusionSpellMissile {
    pub key: u64,
    pub spell_id: u32,
    pub from: Vec3,
    pub to: Vec3,
    pub speed: f32,
}

/// A bolt's end: the flight named `key` stops where it is, and unless `spent` the spell's impact
/// plays, on `unit` as a missile's arrival would (its impact kit, the flinch) or, with no unit, at
/// `at` (Bevy space) in the world: the impact kit's models there as one-shots, its sound and shake.
#[derive(Message, Clone, Copy, Debug)]
pub struct FusionSpellImpact {
    pub key: u64,
    pub spell_id: u32,
    pub at: Vec3,
    pub unit: Option<Entity>,
    pub spent: bool,
}

/// A layer effect: a WoW effect model (a `SpellVisualEffectName` path such as
/// `Spells\Fire_Precast_Hand.mdx`) the layer places itself, with its particles and ribbons, as a
/// dest burst plays one: the hand-held glow of a spell charging in a first-person hand, a spray's
/// cone, a fizzle's puff, a zone's ground model. The reference has no such placement; its effects
/// ride kits and dest points only. `key` names a live one (0: anonymous); a `Play` of a live key
/// replaces it. At most [`LAYER_EFFECT_CAP`] live at once (the oldest one-shot gives way), and a
/// world-anchored one farther than [`LAYER_EFFECT_REACH`] from the camera is not played.
#[derive(Message, Clone, Debug)]
pub enum FusionEffect {
    /// Play `path` at `transform`: in `parent`'s frame when given (and alive), else the world's.
    /// `looping` repeats its first sequence until [`Self::Stop`]; else one pass, then it ends.
    /// `size` is the particles' size unit (1.0: as authored), for a scaled placement whose
    /// emitters do not take the instance scale; `ground` lays its flat quads on the terrain as
    /// decals, as a dest burst does.
    Play {
        key: u64,
        path: String,
        parent: Option<Entity>,
        transform: Transform,
        looping: bool,
        size: f32,
        ground: bool,
    },
    /// Move a live effect (its frame as it was played) and set its particles' size unit.
    Place {
        key: u64,
        transform: Transform,
        size: f32,
    },
    /// End a live effect: its models go at once, its particles finish where they are.
    Stop { key: u64 },
}

/// The most layer effects ([`FusionEffect`]) live at once.
pub const LAYER_EFFECT_CAP: usize = 48;
/// How far from the camera a world-anchored layer effect still plays (yards).
pub const LAYER_EFFECT_REACH: f32 = 120.0;

/// A layer effect on a unit's M2 attachment (a hand held up casting in third person, a cast's
/// flash at the hand): `path` at attachment `tag` (`SpellVisualKit`'s slot tags: 0x15 the left
/// hand, 0x16 the right) of `entity`'s model, as a kit's slot hangs it. `Hold` lasts until a
/// `Stop` of the same `key` (re-armed forever, a precast kit's lifecycle); `Once` plays one pass
/// (a cast kit's). `key` is the layer's own, kept apart from every spell id.
#[derive(Message, Clone, Debug)]
pub enum FusionUnitFx {
    Hold {
        entity: Entity,
        key: u32,
        tag: u16,
        path: String,
    },
    Once {
        entity: Entity,
        key: u32,
        tag: u16,
        path: String,
    },
    Stop {
        entity: Entity,
        key: u32,
    },
}

/// The bit a [`FusionUnitFx`] key carries so it never meets a spell id's own kit on the unit.
pub const LAYER_UNIT_FX_BIT: u32 = 0x4000_0000;

/// Ground reticles a layer shows this frame (WoW's own `Spell-Shadow-*` decal the stock AoE
/// targeting draws, `crate::target::reticle`): where an area spell charging in a hand will land, a
/// zone standing. The layer rewrites the list each frame; the stock targeting's own draws apart.
#[derive(Resource, Debug, Clone, Default)]
pub struct LayerGroundReticles(pub Vec<LayerReticle>);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LayerReticle {
    /// The centre, Bevy space.
    pub center: Vec3,
    /// Yards.
    pub radius: f32,
    /// The acceptable (green) texture, else the unacceptable (red).
    pub acceptable: bool,
    /// Its opacity, 0..1.
    pub alpha: f32,
}

/// A creature model a layer window shows (benilla-mw2's Companions window): the stable window's
/// pane (`PetStable.xml`'s `PetStableModel`, `SetPetStablePaperdoll` `0x4cb870`) baked for a
/// creature display with no world object, at `yaw`, while the stock stable is closed. The layer
/// writes it; `None` empties the pane. The bake is [`LayerCreaturePreviewImage`], rendered live
/// (half rate with `boothHalfRate`) while the layer holds it, so the model plays its Stand loop,
/// and framed to the model's box so the whole creature shows at every `yaw`.
///
/// Deviation: the stock pane frames through the model's own camera and draws only inside the
/// stock stable window.
#[derive(Resource, Debug, Clone, Copy, PartialEq)]
pub struct LayerCreaturePreview {
    pub display_id: Option<u32>,
    /// Radians about the model's up axis, the stock pane's `SetFacing`.
    pub yaw: f32,
}

impl Default for LayerCreaturePreview {
    fn default() -> Self {
        Self {
            display_id: None,
            // `Model_OnLoad`'s default facing (`UIParent.lua:1422`), the stock pane's.
            yaw: 0.61,
        }
    }
}

/// The stable pane's bake, for a layer window to draw [`LayerCreaturePreview`] with: a square
/// render target, in the UI's gamma bytes as every booth's is. `None` until the booths are up.
#[derive(Resource, Debug, Clone, Default)]
pub struct LayerCreaturePreviewImage(pub Option<Handle<Image>>);

/// The layer's own professions as stock skill lines (Construction, Minstrelsy: the client can't
/// learn a `SkillLine.dbc` row): the Skills tab lists each under its category's header with its
/// rank bar, as a stock line's ([`layer_skill_rows`]).
#[derive(Resource, Debug, Clone, Default, PartialEq)]
pub struct LayerSkillLines(pub Vec<LayerSkillLine>);

#[derive(Debug, Clone, Default, PartialEq)]
pub struct LayerSkillLine {
    /// The layer's own id (no `SkillLine.dbc` id: 9001 Construction, 9002 Minstrelsy).
    pub id: u32,
    pub name: String,
    pub value: u32,
    pub max: u32,
    pub description: String,
    /// The `SkillLineCategory` it joins (9 Secondary Skills, 11 Professions).
    pub category_id: u32,
}

/// The `SkillLineCategory` rows a layer line falls back to when the catalog lacks them.
pub(crate) fn layer_category_fallback(category_id: u32) -> (&'static str, u32) {
    match category_id {
        11 => ("Professions", 4),
        _ => ("Secondary Skills", 5),
    }
}

/// The layer's lines as Skills-tab rows; `category` names a category id (its header text and
/// display order) from `SkillLineCategory.dbc`, `None` where the catalog has no such row.
pub(crate) fn layer_skill_rows(
    lines: &LayerSkillLines,
    category: impl Fn(u32) -> Option<(String, u32)>,
) -> Vec<benilla_ui::script::SkillEntry> {
    lines
        .0
        .iter()
        .map(|l| {
            let (name, order) = category(l.category_id).unwrap_or_else(|| {
                let (n, o) = layer_category_fallback(l.category_id);
                (n.to_string(), o)
            });
            benilla_ui::script::SkillEntry {
                skill_id: l.id,
                name: l.name.clone(),
                value: l.value,
                max: l.max,
                mono: false,
                temp_bonus: 0,
                perm_bonus: 0,
                min_level: 0,
                cost_index: 0,
                category_id: l.category_id,
                category_name: name,
                category_order: order,
                description: l.description.clone(),
                abandonable: false,
            }
        })
        .collect()
}

/// The layer's own trade window on the stock `TradeSkillFrame` (Construction, Minstrelsy): while
/// no stock book is open, [`Self::book`] is what the frame shows, with the stock sounds, colours,
/// Create and Create All. `DoTradeSkill` on it lands in [`Self::dos`] (the recipe's id as the
/// spell id, and the count) for the layer to send; `CloseTradeSkill` (or a stock book opening)
/// clears the book and sets [`Self::closed`].
#[derive(Resource, Debug, Clone, Default)]
pub struct LayerTradeSkill {
    pub book: Option<benilla_ui::script::TradeSkillState>,
    pub dos: Vec<(u32, u32)>,
    pub closed: bool,
}

pub(crate) struct FusionPlugin;

impl Plugin for FusionPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SendFusionShots>()
            .add_message::<SendFusionState>()
            .add_message::<FusionEventsReceived>()
            .add_message::<SendFusionLoadout>()
            .add_message::<FusionLoadoutReceived>()
            .add_message::<SendFusionChannel>()
            .add_message::<SendGossipHello>()
            .init_resource::<LayerFirstPerson>()
            .init_resource::<LayerCreaturePreview>()
            .init_resource::<LayerCreaturePreviewImage>()
            .init_resource::<LayerQuietSpellbook>()
            .init_resource::<LayerMountLook>()
            .init_resource::<LayerSkillLines>()
            .init_resource::<LayerTradeSkill>()
            .add_message::<FusionChannelReceived>()
            .add_message::<FusionSpellMissile>()
            .add_message::<FusionSpellImpact>()
            .add_message::<FusionEffect>()
            .add_message::<FusionUnitFx>()
            .init_resource::<LayerGroundReticles>()
            .add_message::<PlayUnitAnim>()
            .add_systems(Update, forward_unit_anims)
            .init_resource::<crate::fusion_bar::FusionActionBar>()
            .init_resource::<crate::fusion_bar::FusionNoSpellSwing>()
            .add_systems(
                Update,
                crate::ui_action::fusion::sync_bar
                    .in_set(crate::ui_script::UiFeed)
                    .before(crate::ui_unit::UnitFeed),
            )
            .add_systems(
                OnExit(crate::char_select::ClientState::InWorld),
                crate::fusion_bar::leave_world,
            )
            .init_resource::<MinimapMarks>()
            .init_resource::<FirstPersonBody>()
            .init_resource::<LayerFlight>()
            .init_resource::<LayerMountAnim>()
            .init_resource::<LayerUnitHold>()
            .init_resource::<LayerMountTilt>()
            .init_resource::<LayerSound>()
            .init_resource::<LayerZoomSnap>()
            .init_resource::<StreamedDecks>()
            .init_resource::<LayerRiding>()
            // The layer's mount tilt is the last word on the mount body's local frame: after the
            // terrain conform, before propagation (out of Update's undeclared orders).
            .add_systems(
                PostUpdate,
                tilt_layer_mount
                    .after(crate::entities::conform::conform_units)
                    .before(bevy::transform::TransformSystems::Propagate),
            )
            .init_resource::<crate::fusion_spells::PendingSpellTweaks>()
            .init_resource::<crate::fusion_spells::PendingForms>()
            // Apply the server's spell definitions before this frame's feeds and casts read
            // them. Forms refine the base tweaks, so their shared catalog writes are ordered.
            .add_systems(
                Update,
                (
                    crate::fusion_spells::apply_spell_tweaks,
                    crate::fusion_spells::apply_forms,
                )
                    .chain()
                    .after(benilla_world::schedule::WorldStage::Net)
                    .before(crate::ui_script::UiFeed),
            )
            .add_systems(PostUpdate, forward_sends)
            .net_handler(SessionEventKind::FusionEvents, on_fusion_events)
            .net_handler(SessionEventKind::FusionLoadout, on_fusion_loadout)
            .net_handler(SessionEventKind::FusionChannel, on_fusion_channel);
    }
}

fn forward_sends(
    mut shots: MessageReader<SendFusionShots>,
    mut states: MessageReader<SendFusionState>,
    mut loadouts: MessageReader<SendFusionLoadout>,
    mut channel: MessageReader<SendFusionChannel>,
    mut hellos: MessageReader<SendGossipHello>,
    net: Option<Res<NetCommands>>,
) {
    let Some(net) = net else {
        hellos.clear();
        shots.clear();
        states.clear();
        loadouts.clear();
        channel.clear();
        return;
    };
    for h in hellos.read() {
        let _ = net.0.send(ClientCommand::GossipHello { guid: h.0 });
    }
    for m in channel.read() {
        let _ = net.0.send(ClientCommand::FusionChannel {
            message: m.0.clone(),
        });
    }
    for l in loadouts.read() {
        let _ = net.0.send(ClientCommand::FusionLoadout {
            request: l.0.clone(),
        });
    }
    let mut pending: Vec<Shot> = shots.read().flat_map(|s| s.0.iter().copied()).collect();
    while !pending.is_empty() {
        let rest = pending.split_off(pending.len().min(MAX_SHOTS_PER_BATCH));
        let _ = net.0.send(ClientCommand::FusionShots { shots: pending });
        pending = rest;
    }
    if let Some(state) = states.read().last() {
        let _ = net.0.send(ClientCommand::FusionState { state: state.0 });
    }
}

fn on_fusion_events(In(ev): In<SessionEvent>, mut out: MessageWriter<FusionEventsReceived>) {
    if let SessionEvent::FusionEvents { events } = ev {
        out.write(FusionEventsReceived(events));
    }
}

fn on_fusion_channel(In(ev): In<SessionEvent>, mut out: MessageWriter<FusionChannelReceived>) {
    if let SessionEvent::FusionChannel { message } = ev {
        out.write(FusionChannelReceived(message));
    }
}

fn on_fusion_loadout(In(ev): In<SessionEvent>, mut out: MessageWriter<FusionLoadoutReceived>) {
    if let SessionEvent::FusionLoadout { reply } = ev {
        out.write(FusionLoadoutReceived(reply));
    }
}

/// A unit's attachment points and held item models, for a mod that hangs its own model on a
/// body (an NPC's gun): read through [`UnitAttach`]. Attach ids are M2's (`HAND_RIGHT` 1,
/// `HAND_LEFT` 2); held slots are main 0, off 1, ranged 2.
pub const ATTACH_HAND_RIGHT: u16 = 1;
pub const ATTACH_HAND_LEFT: u16 = 2;
pub const HELD_SLOT_MAIN: usize = 0;
pub const HELD_SLOT_OFF: usize = 1;
pub const HELD_SLOT_RANGED: usize = 2;

#[derive(bevy::ecs::system::SystemParam)]
pub struct UnitAttach<'w, 's> {
    points: Query<'w, 's, &'static crate::entities::equipment::BoneAttach>,
    held: Query<'w, 's, &'static crate::entities::equipment::HeldAttached>,
    mounts: Query<'w, 's, &'static crate::entities::mount::MountChild>,
}

impl UnitAttach<'_, '_> {
    /// A body's attachment point: its bone and the Bevy-space offset from that bone's pivot, to
    /// parent a model under `RigPose::anchor_for(unit, bone)` at that offset.
    pub fn point(&self, unit: Entity, attach_id: u16) -> Option<(u16, Vec3)> {
        self.points.get(unit).ok()?.points.get(&attach_id).copied()
    }

    /// The mount body a rider sits on (its own entity, a child of the rider), if it has one built:
    /// where a layer plays the mount's side of a mounted rider's attack (a play there routes as any
    /// creature's, since a mount child has no mounted state of its own).
    pub fn mount_of(&self, rider: Entity) -> Option<Entity> {
        self.mounts.get(rider).ok().map(|m| m.0)
    }

    /// The root of the WoW item model a unit has in a held slot, to hide it (write its
    /// `Visibility` through a `Mut`; it is rebuilt when the slot's item changes).
    pub fn held_root(&self, unit: Entity, slot: usize) -> Option<Entity> {
        *self.held.get(unit).ok()?.spawned_slots().get(slot)?
    }
}

/// Play a one-shot animation on a unit (an `AnimationData.dbc` id, e.g. 49 `AttackRifle`), as
/// the server's emotes and spell kits do: full body standing, over the legs while moving.
#[derive(Message, Clone, Copy, Debug)]
pub struct PlayUnitAnim {
    pub entity: Entity,
    pub anim_id: u16,
}

fn forward_unit_anims(
    mut asks: MessageReader<PlayUnitAnim>,
    mut seq: ResMut<crate::creature_anim::PlaySeq>,
    mut out: MessageWriter<crate::creature_anim::EmoteAnim>,
) {
    for a in asks.read() {
        out.write(crate::creature_anim::EmoteAnim {
            entity: a.entity,
            anim_id: a.anim_id,
            seq: seq.next(),
            via_player: false,
        });
    }
}

#[cfg(test)]
mod layer_profession_tests {
    use super::*;

    #[test]
    fn the_layers_lines_join_their_category_as_stock_rows() {
        let lines = LayerSkillLines(vec![
            LayerSkillLine {
                id: 9001,
                name: "Construction".into(),
                value: 42,
                max: 75,
                description: "Building.".into(),
                category_id: 9,
            },
            LayerSkillLine {
                id: 9002,
                name: "Minstrelsy".into(),
                value: 1,
                max: 75,
                description: String::new(),
                category_id: 9,
            },
        ]);
        let rows = layer_skill_rows(&lines, |id| (id == 9).then(|| ("Secondary Skills".to_string(), 7)));
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].skill_id, 9001);
        assert_eq!((rows[0].value, rows[0].max), (42, 75));
        assert_eq!(rows[0].category_name, "Secondary Skills");
        assert_eq!(rows[0].category_order, 7);
        assert!(!rows[0].abandonable && !rows[0].mono);
        // A catalog without the row: the stock names stand in.
        let rows = layer_skill_rows(&lines, |_| None);
        assert_eq!(rows[1].category_name, "Secondary Skills");
    }

    #[test]
    fn the_layers_trade_book_starts_closed() {
        let t = LayerTradeSkill::default();
        assert!(t.book.is_none() && t.dos.is_empty() && !t.closed);
    }
}

/// [`LayerMountTilt`] onto the self player's mount body ([`LayerMountAnim::mount`]): its local
/// frame turned about the pivot, the identity once the layer lets go.
fn tilt_layer_mount(
    tilt: Res<LayerMountTilt>,
    anim: Res<LayerMountAnim>,
    mut last: Local<Option<Entity>>,
    mut mounts: Query<&mut Transform, With<crate::entities::mount::MountBody>>,
) {
    let target = anim.mount.filter(|_| tilt.0.is_some());
    if let Some(prev) = *last {
        if Some(prev) != target {
            if let Ok(mut t) = mounts.get_mut(prev) {
                t.rotation = Quat::IDENTITY;
                t.translation = Vec3::ZERO;
            }
        }
    }
    *last = target;
    let (Some(e), Some((rot, pivot))) = (target, tilt.0) else {
        return;
    };
    if let Ok(mut t) = mounts.get_mut(e) {
        t.rotation = rot;
        t.translation = pivot - rot * pivot;
    }
}
