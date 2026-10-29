//! The class framework (WOMW design section 2, `design/CLASS_CATALOG.md`; the server's
//! `fusion/Classes.h` and `game/Fusion/FusionClasses.cpp`): every class has three core abilities
//! and eighteen options bought once at the class trainer; a kit picks one option per tier.
//!
//! The server sends, on fusion channel 7: the class's catalog for the talent page (op 6: names,
//! tiers, archetypes and the spell whose icon and tooltip stand for each), the character's class
//! state (op 1: level, the kit in hand, its picks as applied, the options bought), the archetype
//! resources (op 2: Holy Power, Static and Life Force, the three left, each shown as a stacking
//! buff over the action bar: its icon, the count and a tooltip, no bar), units spotted for
//! the party (op 3: a marker over each for a while), and smoke clouds (op 5: drawn as the smoke
//! grenade's). The kit editor's talent page (crate::cac_ui) reads [`ClassPage`] and writes the
//! picks into the kit, which the loadout message saves near a trainer or innkeeper.
//!
//! The wire is the C++ encoders' byte for byte, little-endian, first byte the op.

use std::collections::HashMap;

use benilla_app::Objects;
use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::picking::Pickable;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;

pub use wire::*;

/// The class messages' wire (`Fusion::Classes` in `Classes.h` / `Classes.cpp`).
pub mod wire {
    /// The fusion channel of the class framework (`kChannelClasses`).
    pub const CHANNEL_CLASSES: u8 = 7;
    // Server to client.
    pub const OP_STATE: u8 = 1;
    pub const OP_METERS: u8 = 2;
    pub const OP_SPOTTED: u8 = 3;
    pub const OP_PICK_RESULT: u8 = 4;
    pub const OP_SMOKE: u8 = 5;
    pub const OP_PAGE: u8 = 6;
    // Client to server.
    pub const OP_ASK_STATE: u8 = 11;

    /// The kit's tiers (levels 10 to 60).
    pub const TIERS: usize = 6;
    /// `PageEntry::tier` of a core ability.
    pub const CORE_TIER: u8 = 0;

    /// `State::flags`: kits may be changed here (a trainer or innkeeper, the field editor, a GM).
    pub const STATE_CAN_EDIT: u8 = 0x01;
    /// `State::flags`: a GM, who may pick any option.
    pub const STATE_ADMIN: u8 = 0x02;

    /// What an option is (`Fusion::Classes::Kind`).
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    pub enum Kind {
        #[default]
        Ability,
        Passive,
        Form,
    }

    /// One core ability or option on the talent page (`PageEntry`).
    #[derive(Debug, Clone, PartialEq, Eq, Default)]
    pub struct PageEntry {
        pub id: u16,
        pub tier: u8,
        pub archetype: u8,
        pub kind: Kind,
        pub capstone: bool,
        /// The spell whose name, icon and tooltip stand for it (the rank the level allows).
        pub spell: u32,
        pub name: String,
    }

    /// The class's catalog (op 6).
    #[derive(Debug, Clone, PartialEq, Eq, Default)]
    pub struct Page {
        pub class: u8,
        pub archetypes: [String; 3],
        /// The core abilities, then the options in tier order.
        pub entries: Vec<PageEntry>,
    }

    impl Page {
        /// The option of a tier (1..=6) and archetype (0..3).
        pub fn option(&self, tier: u8, archetype: u8) -> Option<&PageEntry> {
            self.entries
                .iter()
                .find(|e| e.tier == tier && e.archetype == archetype)
        }

        pub fn entry(&self, id: u16) -> Option<&PageEntry> {
            self.entries.iter().find(|e| e.id == id)
        }
    }

    /// The character's class state (op 1).
    #[derive(Debug, Clone, PartialEq, Eq, Default)]
    pub struct ClassState {
        pub class: u8,
        pub level: u8,
        /// The kit in hand's slot, 255 none.
        pub kit: u8,
        pub flags: u8,
        /// Its picks as the server applied them.
        pub picks: [u16; TIERS],
        /// The options bought.
        pub learned: Vec<u16>,
    }

    /// One archetype resource (op 2).
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    pub struct Meter {
        pub resource: u8,
        pub value: u16,
        pub max: u16,
    }

    /// A smoke cloud (op 5): where, how wide, how long.
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct Smoke {
        pub at: [f32; 3],
        pub radius: f32,
        pub ms: u16,
    }

    /// `Fusion::Classes::Resource`: the three left, by their wire value (the others an earlier
    /// build had are gone; their numbers are not reused).
    pub const RESOURCE_HOLY_POWER: u8 = 2;
    pub const RESOURCE_STATIC: u8 = 9;
    pub const RESOURCE_LIFE_FORCE: u8 = 14;
    /// The Duelist's (2026-10-21), a number no earlier resource had.
    pub const RESOURCE_MOMENTUM: u8 = 16;

    /// `Fusion::Classes::Resource`'s names, by value.
    pub fn resource_name(r: u8) -> &'static str {
        match r {
            RESOURCE_HOLY_POWER => "Holy Power",
            RESOURCE_STATIC => "Static",
            RESOURCE_LIFE_FORCE => "Life Force",
            RESOURCE_MOMENTUM => "Momentum",
            _ => "",
        }
    }

    /// The buff icon a resource shows as (`Interface\Icons\...`).
    pub fn resource_icon(r: u8) -> Option<&'static str> {
        Some(match r {
            RESOURCE_HOLY_POWER => "Interface\\Icons\\Spell_Holy_PowerInfusion",
            RESOURCE_STATIC => "Interface\\Icons\\Spell_Nature_Lightning",
            RESOURCE_LIFE_FORCE => "Interface\\Icons\\Spell_Shadow_RaiseDead",
            RESOURCE_MOMENTUM => "Interface\\Icons\\Ability_Rogue_SliceDice",
            _ => return None,
        })
    }

    /// What a resource does, for its buff's tooltip.
    pub fn resource_text(r: u8) -> &'static str {
        match r {
            RESOURCE_HOLY_POWER => {
                "Crusader Strike and Repentance build it; Judgement and Divine Storm spend it all, \
                 stronger for each. Fades out of combat."
            }
            RESOURCE_STATIC => {
                "Lightning Bolt, Chain Lightning and Frost Shock build it; full, your next Lightning \
                 Bolt overloads. Fades out of combat."
            }
            RESOURCE_LIFE_FORCE => {
                "Kills and your minions falling fill it; raising the dead, Corpse Explosion and \
                 Bone Armor spend it."
            }
            RESOURCE_MOMENTUM => {
                "Each melee blow adds one (Sinister Strike and a riposte two); each adds 5% to your \
                 melee damage. Two seconds without a blow and it ebbs, one a second."
            }
            _ => "",
        }
    }

    struct Reader<'a> {
        b: &'a [u8],
        at: usize,
    }

    impl Reader<'_> {
        fn u8(&mut self) -> Option<u8> {
            let v = *self.b.get(self.at)?;
            self.at += 1;
            Some(v)
        }
        fn u16(&mut self) -> Option<u16> {
            let s = self.b.get(self.at..self.at + 2)?;
            self.at += 2;
            Some(u16::from_le_bytes([s[0], s[1]]))
        }
        fn u32(&mut self) -> Option<u32> {
            let s = self.b.get(self.at..self.at + 4)?;
            self.at += 4;
            Some(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        }
        fn u64(&mut self) -> Option<u64> {
            let s = self.b.get(self.at..self.at + 8)?;
            self.at += 8;
            Some(u64::from_le_bytes(s.try_into().ok()?))
        }
        fn f32(&mut self) -> Option<f32> {
            self.u32().map(f32::from_bits)
        }
        fn string(&mut self) -> Option<String> {
            let n = usize::from(self.u8()?);
            let s = self.b.get(self.at..self.at + n)?;
            self.at += n;
            Some(String::from_utf8_lossy(s).into_owned())
        }
        fn done(&self) -> bool {
            self.at == self.b.len()
        }
    }

    pub fn decode_page(b: &[u8]) -> Option<Page> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_PAGE {
            return None;
        }
        let class = r.u8()?;
        let archetypes = [r.string()?, r.string()?, r.string()?];
        let n = r.u8()?;
        let mut entries = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            let id = r.u16()?;
            let tier = r.u8()?;
            let archetype = r.u8()?;
            let kind = match r.u8()? {
                1 => Kind::Passive,
                2 => Kind::Form,
                _ => Kind::Ability,
            };
            let capstone = r.u8()? != 0;
            let spell = r.u32()?;
            let name = r.string()?;
            entries.push(PageEntry {
                id,
                tier,
                archetype,
                kind,
                capstone,
                spell,
                name,
            });
        }
        r.done().then_some(Page {
            class,
            archetypes,
            entries,
        })
    }

    pub fn decode_state(b: &[u8]) -> Option<ClassState> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_STATE {
            return None;
        }
        let class = r.u8()?;
        let level = r.u8()?;
        let kit = r.u8()?;
        let flags = r.u8()?;
        let mut picks = [0u16; TIERS];
        for p in &mut picks {
            *p = r.u16()?;
        }
        let n = r.u8()?;
        let learned = (0..n).map(|_| r.u16()).collect::<Option<Vec<_>>>()?;
        r.done().then_some(ClassState {
            class,
            level,
            kit,
            flags,
            picks,
            learned,
        })
    }

    pub fn decode_meters(b: &[u8]) -> Option<Vec<Meter>> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_METERS {
            return None;
        }
        let n = r.u8()?;
        let meters = (0..n)
            .map(|_| {
                Some(Meter {
                    resource: r.u8()?,
                    value: r.u16()?,
                    max: r.u16()?,
                })
            })
            .collect::<Option<Vec<_>>>()?;
        r.done().then_some(meters)
    }

    /// Op 3: the units spotted and for how long (ms).
    pub fn decode_spotted(b: &[u8]) -> Option<Vec<(u64, u16)>> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_SPOTTED {
            return None;
        }
        let n = r.u8()?;
        let spots = (0..n)
            .map(|_| Some((r.u64()?, r.u16()?)))
            .collect::<Option<Vec<_>>>()?;
        r.done().then_some(spots)
    }

    pub fn decode_smoke(b: &[u8]) -> Option<Smoke> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_SMOKE {
            return None;
        }
        let s = Smoke {
            at: [r.f32()?, r.f32()?, r.f32()?],
            radius: r.f32()?,
            ms: r.u16()?,
        };
        r.done().then_some(s)
    }

    pub fn encode_ask_state() -> Vec<u8> {
        vec![OP_ASK_STATE]
    }

    /// A tier's level (1..=6 → 10..=60).
    pub fn tier_level(tier: u8) -> u8 {
        tier.saturating_mul(10)
    }
}

/// What the server last said of the character's class.
#[derive(Resource, Debug, Default)]
pub struct ClassPage {
    pub page: Option<Page>,
    pub state: Option<ClassState>,
    pub meters: Vec<Meter>,
    /// Units spotted for the party, until (seconds, the app clock).
    pub spotted: HashMap<u64, f32>,
    /// Smoke clouds to draw (crate::equipment takes them).
    pub smokes: Vec<Smoke>,
    /// The class spells' reach as the server refitted them (op 7; benilla puts the rest of the
    /// refit into its own spell table): a donor cast on oneself is readied as a self cast.
    pub ranges: HashMap<u32, u32>,
    /// Their attributes as refitted (op 7): Maul strikes at once, not on the next swing.
    pub attributes: HashMap<u32, u32>,
}

impl ClassPage {
    pub fn learned(&self, id: u16) -> bool {
        self.state.as_ref().is_some_and(|s| s.learned.contains(&id))
    }

    pub fn level(&self) -> u8 {
        self.state.as_ref().map_or(1, |s| s.level)
    }

    pub fn admin(&self) -> bool {
        self.state
            .as_ref()
            .is_some_and(|s| s.flags & STATE_ADMIN != 0)
    }

    /// Whether a kit may pick this option: bought (a GM: any), its tier reached.
    pub fn can_pick(&self, e: &PageEntry) -> bool {
        e.tier != CORE_TIER
            && (self.admin() || (self.learned(e.id) && self.level() >= tier_level(e.tier)))
    }
}

/// A kit's picks after a click on an option: picked again, it leaves the tier; otherwise it takes
/// the tier's place (one per tier). An option the kit can't hold changes nothing.
pub fn toggle_pick(picks: &mut [u16; TIERS], page: &ClassPage, id: u16) -> bool {
    let Some(e) = page.page.as_ref().and_then(|p| p.entry(id)) else {
        return false;
    };
    if e.tier == CORE_TIER || usize::from(e.tier) > TIERS {
        return false;
    }
    let slot = &mut picks[usize::from(e.tier) - 1];
    if *slot == id {
        *slot = 0;
        return true;
    }
    if !page.can_pick(e) {
        return false;
    }
    *slot = id;
    true
}

/// A class resource as the buff the HUD shows: a stacking aura's icon, its count and tooltip.
#[derive(Debug, Clone, PartialEq)]
pub struct ClassBuff {
    pub resource: u8,
    pub name: &'static str,
    pub icon: &'static str,
    pub count: u16,
    pub max: u16,
    pub tip: Vec<crate::wow_ui::TipLine>,
}

/// The buffs the meters show: each resource with something in it (a buff with no stacks is no
/// buff), in the server's order. A resource this client does not know is left out.
pub fn class_buffs(meters: &[Meter]) -> Vec<ClassBuff> {
    use crate::wow_ui::{GOLD, TipLine, WHITE};
    meters
        .iter()
        .filter(|m| m.max > 0 && m.value > 0)
        .filter_map(|m| {
            let icon = resource_icon(m.resource)?;
            let name = resource_name(m.resource);
            Some(ClassBuff {
                resource: m.resource,
                name,
                icon,
                count: m.value,
                max: m.max,
                tip: vec![
                    TipLine::new(name, WHITE, 14.0),
                    TipLine::new(format!("{} of {}", m.value, m.max), WHITE, 11.0),
                    TipLine::new(resource_text(m.resource), GOLD, 12.0),
                ],
            })
        })
        .collect()
}

use crate::wow_ui::FONT;
const MARKERS: usize = 16;
/// The class buffs' row: one slot per resource there is.
const BUFF_SLOTS: usize = 3;
/// A buff icon's side in UI units (the stock buff button's 32), drawn at the seam.
const BUFF_PX: f32 = 32.0;
/// UI units between the bar's stack and the row, and the row's seat before the bar is measured.
const BUFF_GAP_UNITS: f32 = 6.0;
const BUFF_FALLBACK_UNITS: f32 = 96.0;

/// The row's `bottom` in pixels: over the action bar's stack (`stack` a share of the screen's
/// height), `k` pixels to a UI unit.
pub fn buff_row_bottom(stack: Option<f32>, viewport_h: f32, k: f32) -> f32 {
    match stack {
        Some(top) => top * viewport_h + BUFF_GAP_UNITS * k,
        None => BUFF_FALLBACK_UNITS * k,
    }
}

/// The class buffs' row over the action bar.
#[derive(Component)]
struct BuffRow;

/// One class buff's slot in the row.
#[derive(Component)]
struct BuffSlot(usize);

/// A slot's stack count.
#[derive(Component)]
struct BuffCount(usize);

#[derive(Component)]
struct SpotMarker(usize);

pub struct ClassesPlugin;

impl Plugin for ClassesPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ClassPage>()
            .add_systems(Startup, spawn_hud)
            .add_systems(
                Update,
                (read_channel, draw_buffs, place_buffs, draw_spotted)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// Ask the server for the class state (the kit editor does on opening: the edit flag follows
/// where the player stands).
pub fn ask_state(out: &mut MessageWriter<SendFusionChannel>) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL_CLASSES,
        body: encode_ask_state(),
    }));
}

fn read_channel(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut page: ResMut<ClassPage>,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_CLASSES {
            continue;
        }
        match m.body.first() {
            Some(&OP_PAGE) => page.page = decode_page(&m.body),
            Some(&OP_STATE) => {
                if let Some(s) = decode_state(&m.body) {
                    info!(
                        "benilla-mw2: class state: level {}, kit {}, picks {:?}, {} options bought",
                        s.level,
                        s.kit,
                        s.picks,
                        s.learned.len()
                    );
                    page.state = Some(s);
                }
            }
            Some(&OP_METERS) => {
                if let Some(m) = decode_meters(&m.body) {
                    page.meters = m;
                }
            }
            Some(&OP_SPOTTED) => {
                for (guid, ms) in decode_spotted(&m.body).unwrap_or_default() {
                    let until = t + f32::from(ms) / 1000.0;
                    let e = page.spotted.entry(guid).or_insert(until);
                    *e = e.max(until);
                }
            }
            Some(&benilla_app::fusion_spells::OP_SPELL_TWEAKS) => {
                for t in
                    benilla_app::fusion_spells::decode_spell_tweaks(&m.body).unwrap_or_default()
                {
                    page.ranges.insert(t.spell, u32::from(t.range_index));
                    page.attributes.insert(t.spell, t.attributes);
                }
            }
            Some(&OP_SMOKE) => {
                if let Some(s) = decode_smoke(&m.body) {
                    page.smokes.push(s);
                }
            }
            _ => {}
        }
    }
    page.spotted.retain(|_, until| *until > t);
}

fn spawn_hud(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(FONT);
    // The class buffs: a row of stock-sized buff icons over the action bar, each with its count
    // in the corner and a tooltip (the resources that are left show as buffs, not bars).
    commands
        .spawn((
            Name::new("class buffs"),
            BuffRow,
            Node {
                position_type: PositionType::Absolute,
                bottom: px(96),
                width: percent(100),
                justify_content: JustifyContent::Center,
                column_gap: px(4),
                ..default()
            },
            Pickable::IGNORE,
        ))
        .with_children(|row| {
            for i in 0..BUFF_SLOTS {
                row.spawn((
                    Name::new("class buff"),
                    BuffSlot(i),
                    Node {
                        width: px(BUFF_PX),
                        height: px(BUFF_PX),
                        border: UiRect::all(px(1)),
                        justify_content: JustifyContent::FlexEnd,
                        align_items: AlignItems::FlexEnd,
                        display: Display::None,
                        ..default()
                    },
                    BorderColor::all(Color::srgba(0.0, 0.0, 0.0, 0.9)),
                    ImageNode::default(),
                    Interaction::default(),
                    crate::wow_ui::WowTip::default(),
                ))
                .with_children(|b| {
                    b.spawn((
                        BuffCount(i),
                        Text::new(""),
                        crate::hud::FontUnits(12.0),
                        TextFont::from_font_size(13.0).with_font(font.clone()),
                        TextColor(Color::WHITE),
                        TextShadow::default(),
                        Node {
                            margin: UiRect::new(px(0), px(1), px(0), px(-1)),
                            ..default()
                        },
                        Pickable::IGNORE,
                    ));
                });
            }
        });
    for i in 0..MARKERS {
        commands.spawn((
            Name::new("spotted marker"),
            SpotMarker(i),
            Node {
                position_type: PositionType::Absolute,
                ..default()
            },
            Text::new("v"),
            TextFont::from_font_size(22.0).with_font(font.clone()),
            TextColor(Color::srgb(1.0, 0.15, 0.1)),
            TextShadow::default(),
            Visibility::Hidden,
            Pickable::IGNORE,
        ));
    }
}

/// The class buffs as the meters stand (only in the game view, as the old line was).
fn draw_buffs(
    page: Res<ClassPage>,
    state: Res<FpsState>,
    assets: Res<AssetServer>,
    mut slots: Query<(
        &BuffSlot,
        &mut Node,
        &mut ImageNode,
        &mut crate::wow_ui::WowTip,
    )>,
    mut counts: Query<(&BuffCount, &mut Text)>,
) {
    if !page.is_changed() && !state.is_changed() {
        return;
    }
    let buffs = if state.active {
        class_buffs(&page.meters)
    } else {
        Vec::new()
    };
    for (slot, mut node, mut image, mut tip) in &mut slots {
        let Some(b) = buffs.get(slot.0) else {
            if node.display != Display::None {
                node.display = Display::None;
            }
            continue;
        };
        if node.display != Display::Flex {
            node.display = Display::Flex;
        }
        let h: Handle<Image> = assets.load(crate::cac_ui::spell_icon_path(b.icon));
        if image.image != h {
            image.image = h;
        }
        if tip.0 != b.tip {
            tip.0 = b.tip.clone();
        }
    }
    for (c, mut text) in &mut counts {
        let want = buffs
            .get(c.0)
            .map_or_else(String::new, |b| b.count.to_string());
        if text.0 != want {
            text.0 = want;
        }
    }
}

/// The row over the action bar's stack, its icons at the seam's size.
fn place_buffs(
    stack: Option<Res<crate::hud::BarStack>>,
    scale: Option<Res<crate::hud::HudScale>>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut row: Query<&mut Node, (With<BuffRow>, Without<BuffSlot>)>,
    mut slots: Query<&mut Node, (With<BuffSlot>, Without<BuffRow>)>,
) {
    let Ok(w) = window.single() else {
        return;
    };
    let k = scale.map_or(1.0, |s| s.0);
    let bottom = px(buff_row_bottom(stack.and_then(|s| s.top), w.height(), k));
    for mut n in &mut row {
        if n.bottom != bottom {
            n.bottom = bottom;
        }
    }
    let side = px(BUFF_PX * k);
    for mut n in &mut slots {
        if n.width != side || n.height != side {
            n.width = side;
            n.height = side;
        }
    }
}

/// A red marker over each spotted unit's head, through walls (the party's spotting).
fn draw_spotted(
    page: Res<ClassPage>,
    objects: Objects,
    units: Query<&GlobalTransform>,
    cameras: Query<(&Camera, &GlobalTransform), With<benilla_world::view::WorldCamera>>,
    mut markers: Query<(&SpotMarker, &mut Node, &mut Visibility)>,
) {
    let Ok((camera, cam)) = cameras.single() else {
        return;
    };
    let mut points = Vec::new();
    for guid in page.spotted.keys() {
        let Some(tf) = objects.entity(*guid).and_then(|e| units.get(e).ok()) else {
            continue;
        };
        let head = tf.translation() + Vec3::Y * 2.6;
        if let Ok(p) = camera.world_to_viewport(cam, head) {
            points.push(p);
        }
    }
    for (m, mut node, mut vis) in &mut markers {
        let want = match points.get(m.0) {
            Some(p) => {
                node.left = px(p.x - 6.0);
                node.top = px(p.y - 22.0);
                Visibility::Inherited
            }
            None => Visibility::Hidden,
        };
        if *vis != want {
            *vis = want;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_buff_row_sits_over_the_bar_at_the_seam() {
        // The bar's stack 80 units up a 768-unit screen drawn 1080 tall.
        let k = 1080.0 / 768.0;
        let bottom = buff_row_bottom(Some(80.0 / 768.0), 1080.0, k);
        assert!((bottom - (80.0 + BUFF_GAP_UNITS) * k).abs() < 0.01);
        assert!((buff_row_bottom(None, 1080.0, 2.0) - BUFF_FALLBACK_UNITS * 2.0).abs() < 0.01);
    }

    fn page_bytes() -> Vec<u8> {
        // Two entries as Classes.cpp EncodePage writes them.
        let mut b = vec![OP_PAGE, 1];
        for a in ["Berserker", "Vanguard", "Warlord"] {
            b.push(a.len() as u8);
            b.extend_from_slice(a.as_bytes());
        }
        b.push(2);
        for (id, tier, arch, kind, cap, spell, name) in [
            (101u16, 0u8, 255u8, 0u8, 0u8, 11578u32, "Charge"),
            (141, 4, 0, 1, 0, 12970, "Blood Frenzy"),
        ] {
            b.extend_from_slice(&id.to_le_bytes());
            b.extend_from_slice(&[tier, arch, kind, cap]);
            b.extend_from_slice(&spell.to_le_bytes());
            b.push(name.len() as u8);
            b.extend_from_slice(name.as_bytes());
        }
        b
    }

    #[test]
    fn the_page_decodes_as_the_server_writes_it() {
        let p = decode_page(&page_bytes()).expect("page");
        assert_eq!(p.class, 1);
        assert_eq!(p.archetypes[1], "Vanguard");
        assert_eq!(p.entries.len(), 2);
        assert_eq!(p.entries[0].name, "Charge");
        assert_eq!(p.entries[1].kind, Kind::Passive);
        assert_eq!(p.entries[1].spell, 12970);
        assert_eq!(p.option(4, 0).map(|e| e.id), Some(141));
        let mut short = page_bytes();
        short.pop();
        assert!(decode_page(&short).is_none());
    }

    #[test]
    fn state_meters_spots_and_smoke_decode() {
        let mut b = vec![OP_STATE, 8, 42, 2, STATE_CAN_EDIT];
        for p in [811u16, 822, 0, 841, 0, 0] {
            b.extend_from_slice(&p.to_le_bytes());
        }
        b.push(2);
        b.extend_from_slice(&811u16.to_le_bytes());
        b.extend_from_slice(&822u16.to_le_bytes());
        let s = decode_state(&b).expect("state");
        assert_eq!((s.class, s.level, s.kit), (8, 42, 2));
        assert_eq!(s.picks[3], 841);
        assert_eq!(s.learned, vec![811, 822]);

        let mut m = vec![OP_METERS, 1, 14];
        m.extend_from_slice(&120u16.to_le_bytes());
        m.extend_from_slice(&150u16.to_le_bytes());
        let meters = decode_meters(&m).expect("meters");
        assert_eq!(meters[0].resource, RESOURCE_LIFE_FORCE);
        assert_eq!((meters[0].value, meters[0].max), (120, 150));

        let mut sp = vec![OP_SPOTTED, 1];
        sp.extend_from_slice(&0x0102u64.to_le_bytes());
        sp.extend_from_slice(&500u16.to_le_bytes());
        assert_eq!(decode_spotted(&sp), Some(vec![(0x0102, 500)]));

        let mut sm = vec![OP_SMOKE];
        for f in [1.0f32, 2.0, 3.0, 8.0] {
            sm.extend_from_slice(&f.to_le_bytes());
        }
        sm.extend_from_slice(&10000u16.to_le_bytes());
        let smoke = decode_smoke(&sm).expect("smoke");
        assert_eq!(
            (smoke.at, smoke.radius, smoke.ms),
            ([1.0, 2.0, 3.0], 8.0, 10000)
        );
    }

    #[test]
    fn the_resources_left_show_as_stacking_buffs() {
        let meters = [
            Meter {
                resource: RESOURCE_HOLY_POWER,
                value: 2,
                max: 3,
            },
            // Empty: no buff, as an aura with no stacks.
            Meter {
                resource: RESOURCE_STATIC,
                value: 0,
                max: 5,
            },
            Meter {
                resource: RESOURCE_LIFE_FORCE,
                value: 40,
                max: 100,
            },
            // A resource an earlier build had (Fury): nothing to show.
            Meter {
                resource: 1,
                value: 3,
                max: 5,
            },
        ];
        let buffs = class_buffs(&meters);
        let names: Vec<&str> = buffs.iter().map(|b| b.name).collect();
        assert_eq!(names, ["Holy Power", "Life Force"]);
        assert_eq!((buffs[0].count, buffs[0].max), (2, 3));
        assert!(buffs[0].icon.starts_with("Interface\\Icons\\"));
        // The tooltip: its name, the count of the most, what it does.
        assert_eq!(buffs[1].tip[0].text, "Life Force");
        assert_eq!(buffs[1].tip[1].text, "40 of 100");
        assert!(buffs[1].tip[2].text.contains("raising the dead"));
        // Three are left and Momentum is new, each with a name, an icon and words.
        for r in [
            RESOURCE_HOLY_POWER,
            RESOURCE_STATIC,
            RESOURCE_LIFE_FORCE,
            RESOURCE_MOMENTUM,
        ] {
            assert!(!resource_name(r).is_empty() && !resource_text(r).is_empty());
            assert!(resource_icon(r).is_some());
        }
        for gone in [1u8, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 15, 17] {
            assert_eq!(resource_name(gone), "");
        }
    }

    #[test]
    fn a_pick_takes_its_tier_once_and_needs_the_option_bought() {
        let mut page = ClassPage {
            page: decode_page(&page_bytes()),
            ..Default::default()
        };
        let mut picks = [0u16; TIERS];
        // Not bought: refused.
        page.state = Some(ClassState {
            level: 60,
            ..Default::default()
        });
        assert!(!toggle_pick(&mut picks, &page, 141));
        // Bought but tier 4 needs level 40.
        page.state = Some(ClassState {
            level: 39,
            learned: vec![141],
            ..Default::default()
        });
        assert!(!toggle_pick(&mut picks, &page, 141));
        page.state.as_mut().unwrap().level = 40;
        assert!(toggle_pick(&mut picks, &page, 141));
        assert_eq!(picks[3], 141);
        // Again: off.
        assert!(toggle_pick(&mut picks, &page, 141));
        assert_eq!(picks[3], 0);
        // A core ability is no pick; a GM picks anything.
        assert!(!toggle_pick(&mut picks, &page, 101));
        page.state = Some(ClassState {
            level: 1,
            flags: STATE_ADMIN,
            ..Default::default()
        });
        assert!(toggle_pick(&mut picks, &page, 141));
    }
}
