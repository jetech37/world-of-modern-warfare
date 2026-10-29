//! Battle companions (the server's `fusion/Companions.h`): a companion item summons a creature
//! that fights at the player's side, levels with its kills, evolves into larger forms (a mount
//! family's last form is ridden), and fights other players' companions in companion duels.
//!
//! The server owns all of it. The companion fights from the stock pet frame and pet bar (its
//! abilities, and a rideable form's mount spell, are on the bar); this module keeps what the
//! server says of it and aims the pet bar keys (Ctrl+1..Ctrl+0) at the crosshair. It never changes
//! the crosshair or the cursor (the playtest: no ring by the crosshair while a companion is out;
//! its health is the pet frame's). Everything else, the collection, the Bestiary, evolving,
//! ability choices, breeding and the duel prompt, is the Companions window
//! (crate::companion_ui). Summoning is a right click on the item in the bags (benilla turns it
//! into the channel's summon request).
//!
//! The wire is the C++ encoders' byte for byte, little-endian, first byte the op (see
//! [`wire`]); the tests use the C++ tests' bytes.

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::aim::Aim;

pub use wire::*;

/// The companion messages' wire (`Fusion::Companions` in `Companions.h` / `Companions.cpp`).
pub mod wire {
    /// The fusion channel of battle companions (`kChannelCompanion`).
    pub const CHANNEL_COMPANION: u8 = 4;
    // Server to client.
    pub const OP_BAR: u8 = 1;
    pub const OP_EVOLVED: u8 = 2;
    pub const OP_DUEL: u8 = 3;
    /// The companion out's moveset, ability choices and autocasts.
    pub const OP_MOVES: u8 = 4;
    /// A pet bar slot went on cooldown.
    pub const OP_COOLDOWN: u8 = 5;
    /// A bred companion item's lineage, by its seed.
    pub const OP_LINEAGE: u8 = 6;
    /// The mount cast's bar: `[7, phase u8, ms u32]` (`MountCastPhase`).
    pub const OP_MOUNT_CAST: u8 = 7;
    pub const MOUNT_CAST_START: u8 = 0;
    pub const MOUNT_CAST_DONE: u8 = 1;
    pub const MOUNT_CAST_BROKEN: u8 = 2;
    // Client to server.
    pub const OP_SUMMON: u8 = 10;
    pub const OP_DISMISS: u8 = 11;
    pub const OP_EVOLVE: u8 = 12;
    pub const OP_DUEL_CHALLENGE: u8 = 13;
    pub const OP_DUEL_ANSWER: u8 = 14;
    pub const OP_DUEL_FORFEIT: u8 = 15;
    pub const OP_RIDE: u8 = 16;
    pub const OP_PICK: u8 = 17;
    pub const OP_BAR_PRESS: u8 = 18;
    pub const OP_AUTOCAST: u8 = 19;
    /// A bar press's point is the ground under the crosshair.
    pub const PRESS_POINT: u8 = 0x01;
    /// A double tap (Command: follow).
    pub const PRESS_DOUBLE: u8 = 0x02;

    pub const BAR_SUMMONED: u8 = 0x01;
    pub const BAR_CAN_EVOLVE: u8 = 0x02;
    /// Held at the owner's level with a full bar.
    pub const BAR_CAPPED: u8 = 0x04;
    pub const BAR_IN_DUEL: u8 = 0x08;
    pub const BAR_FINAL_STAGE: u8 = 0x10;
    /// Its stage is a mount.
    pub const BAR_RIDEABLE: u8 = 0x20;
    /// ... and its owner may ride it (or get off) now.
    pub const BAR_CAN_RIDE: u8 = 0x40;
    /// Its owner is riding it.
    pub const BAR_RIDING: u8 = 0x80;

    const BAR_BYTES: usize = 27;
    const EVOLVED_BYTES: usize = 12;
    const DUEL_BYTES: usize = 12;

    /// Op 1: one companion's state; item 0 with flags 0 means none is out.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Bar {
        pub item: u64,
        pub species: u16,
        pub stage: u8,
        pub level: u8,
        pub effective_level: u8,
        pub xp: u32,
        pub xp_to_next: u32,
        pub flags: u8,
        /// Why it can't evolve (`EvolveBlock`), 0 when it can.
        pub block: u8,
        /// 0 at its final stage.
        pub next_evolve_level: u8,
        pub catalyst_count: u8,
        pub health_pct: u8,
    }

    impl Bar {
        pub fn out(&self) -> bool {
            self.item != 0 && self.flags & (BAR_SUMMONED | BAR_RIDING) != 0
        }
    }

    /// `DuelPhase`.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub enum DuelPhase {
        #[default]
        Idle,
        Challenged,
        Countdown,
        Fighting,
        Finished,
    }

    impl DuelPhase {
        fn from_u8(v: u8) -> Self {
            match v {
                1 => DuelPhase::Challenged,
                2 => DuelPhase::Countdown,
                3 => DuelPhase::Fighting,
                4 => DuelPhase::Finished,
                _ => DuelPhase::Idle,
            }
        }
    }

    /// Op 3: a companion duel's phase or result, from the reader's side.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct DuelUpdate {
        pub phase: DuelPhase,
        /// `DuelEnd`: 1 beaten, 2 forfeit, 3 fled, 4 interfered, 5 timeout, 6 declined,
        /// 7 expired, 8 cancelled.
        pub end: u8,
        /// 0 you, 1 the opponent, -1 nobody.
        pub winner: i8,
        pub opponent: u64,
    }

    fn u16_at(b: &[u8], at: usize) -> u16 {
        u16::from_le_bytes([b[at], b[at + 1]])
    }
    fn u32_at(b: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
    }
    fn u64_at(b: &[u8], at: usize) -> u64 {
        u64::from_le_bytes(b[at..at + 8].try_into().unwrap())
    }

    pub fn encode_bar(b: &Bar) -> Vec<u8> {
        let mut o = Vec::with_capacity(BAR_BYTES);
        o.push(OP_BAR);
        o.extend_from_slice(&b.item.to_le_bytes());
        o.extend_from_slice(&b.species.to_le_bytes());
        o.extend_from_slice(&[b.stage, b.level, b.effective_level]);
        o.extend_from_slice(&b.xp.to_le_bytes());
        o.extend_from_slice(&b.xp_to_next.to_le_bytes());
        o.extend_from_slice(&[
            b.flags,
            b.block,
            b.next_evolve_level,
            b.catalyst_count,
            b.health_pct,
        ]);
        o
    }

    pub fn decode_bar(body: &[u8]) -> Option<Bar> {
        if body.len() != BAR_BYTES || body[0] != OP_BAR {
            return None;
        }
        Some(Bar {
            item: u64_at(body, 1),
            species: u16_at(body, 9),
            stage: body[11],
            level: body[12],
            effective_level: body[13],
            xp: u32_at(body, 14),
            xp_to_next: u32_at(body, 18),
            flags: body[22],
            block: body[23],
            next_evolve_level: body[24],
            catalyst_count: body[25],
            health_pct: body[26],
        })
    }

    /// Op 2: `(item guid, species, stage)`.
    pub fn decode_evolved(body: &[u8]) -> Option<(u64, u16, u8)> {
        if body.len() != EVOLVED_BYTES || body[0] != OP_EVOLVED {
            return None;
        }
        Some((u64_at(body, 1), u16_at(body, 9), body[11]))
    }

    pub fn encode_evolved(item: u64, species: u16, stage: u8) -> Vec<u8> {
        let mut o = vec![OP_EVOLVED];
        o.extend_from_slice(&item.to_le_bytes());
        o.extend_from_slice(&species.to_le_bytes());
        o.push(stage);
        o
    }

    pub fn encode_duel(d: &DuelUpdate) -> Vec<u8> {
        let mut o = vec![OP_DUEL, d.phase as u8, d.end, d.winner as u8];
        o.extend_from_slice(&d.opponent.to_le_bytes());
        o
    }

    pub fn decode_duel(body: &[u8]) -> Option<DuelUpdate> {
        if body.len() != DUEL_BYTES || body[0] != OP_DUEL {
            return None;
        }
        Some(DuelUpdate {
            phase: DuelPhase::from_u8(body[1]),
            end: body[2],
            winner: body[3] as i8,
            opponent: u64_at(body, 4),
        })
    }

    /// Op 4 (`Moves`): the companion out's moveset; item 0 when none is out.
    #[derive(Clone, Debug, Default, PartialEq)]
    pub struct Moves {
        pub item: u64,
        pub species: u16,
        /// The stage (0-based) whose ability choice is owed, -1 none.
        pub pending: i8,
        pub twin_stage: u8,
        pub traits: u32,
        /// Autocast on, a bit per pet bar slot.
        pub autocast: u16,
        /// The choice per stage: 0 not yet, 1 or 2.
        pub picks: [u8; 3],
        pub spells: [[u32; 2]; 3],
        pub names: [[String; 2]; 3],
        pub mounted_spell: u32,
        pub mounted_name: String,
    }

    pub fn encode_moves(m: &Moves) -> Vec<u8> {
        let mut o = vec![OP_MOVES];
        o.extend_from_slice(&m.item.to_le_bytes());
        o.extend_from_slice(&m.species.to_le_bytes());
        o.push(m.pending as u8);
        o.push(m.twin_stage);
        o.extend_from_slice(&m.traits.to_le_bytes());
        o.extend_from_slice(&m.autocast.to_le_bytes());
        o.extend_from_slice(&m.picks);
        let s = |o: &mut Vec<u8>, t: &str| {
            let b = &t.as_bytes()[..t.len().min(255)];
            o.push(b.len() as u8);
            o.extend_from_slice(b);
        };
        for st in 0..3 {
            for i in 0..2 {
                o.extend_from_slice(&m.spells[st][i].to_le_bytes());
                s(&mut o, &m.names[st][i]);
            }
        }
        o.extend_from_slice(&m.mounted_spell.to_le_bytes());
        s(&mut o, &m.mounted_name);
        o
    }

    pub fn decode_moves(body: &[u8]) -> Option<Moves> {
        if body.len() < 22 || body[0] != OP_MOVES {
            return None;
        }
        let mut m = Moves {
            item: u64_at(body, 1),
            species: u16_at(body, 9),
            pending: body[11] as i8,
            twin_stage: body[12],
            traits: u32_at(body, 13),
            autocast: u16_at(body, 17),
            picks: [body[19], body[20], body[21]],
            ..Moves::default()
        };
        let mut at = 22;
        let text = |at: &mut usize| -> Option<String> {
            let n = *body.get(*at)? as usize;
            let t = body.get(*at + 1..*at + 1 + n)?;
            *at += 1 + n;
            Some(String::from_utf8_lossy(t).into_owned())
        };
        for st in 0..3 {
            for i in 0..2 {
                body.get(at..at + 4)?;
                m.spells[st][i] = u32_at(body, at);
                at += 4;
                m.names[st][i] = text(&mut at)?;
            }
        }
        body.get(at..at + 4)?;
        m.mounted_spell = u32_at(body, at);
        at += 4;
        m.mounted_name = text(&mut at)?;
        (at == body.len()).then_some(m)
    }

    /// Op 5: `(slot, cooldown ms)`.
    pub fn decode_cooldown(body: &[u8]) -> Option<(u8, u32)> {
        (body.len() == 6 && body[0] == OP_COOLDOWN).then(|| (body[1], u32_at(body, 2)))
    }

    /// Op 6: a bred companion's lineage.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Lineage {
        pub generation: u8,
        pub parent_a: u16,
        pub parent_b: u16,
        pub bred: u8,
        pub hybrid: u16,
    }

    /// Op 7: `(phase, cast ms)`.
    pub fn decode_mount_cast(body: &[u8]) -> Option<(u8, u32)> {
        (body.len() == 6 && body[0] == OP_MOUNT_CAST && body[1] <= MOUNT_CAST_BROKEN)
            .then(|| (body[1], u32_at(body, 2)))
    }

    /// Op 6: `(item seed, lineage)`.
    pub fn decode_lineage(body: &[u8]) -> Option<(u32, Lineage)> {
        if body.len() != 13 || body[0] != OP_LINEAGE {
            return None;
        }
        Some((
            u32_at(body, 1),
            Lineage {
                generation: body[5],
                parent_a: u16_at(body, 6),
                parent_b: u16_at(body, 8),
                bred: body[10],
                hybrid: u16_at(body, 11),
            },
        ))
    }

    /// A request to the server (`EncodeRequest`): summon, evolve and challenge carry a guid, the
    /// duel answer a yes or no, a pick the item, stage and option, a bar press its slot, the
    /// crosshair's unit and ground point.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub enum Request {
        Summon(u64),
        Dismiss,
        Evolve(u64),
        DuelChallenge(u64),
        DuelAnswer(bool),
        DuelForfeit,
        Ride,
        Pick {
            item: u64,
            stage: u8,
            option: u8,
        },
        BarPress {
            slot: u8,
            target: u64,
            flags: u8,
            point: [f32; 3],
        },
        Autocast {
            slot: u8,
            on: bool,
        },
    }

    pub fn encode_request(r: Request) -> Vec<u8> {
        let guid = |op: u8, g: u64| {
            let mut o = vec![op];
            o.extend_from_slice(&g.to_le_bytes());
            o
        };
        match r {
            Request::Summon(g) => guid(OP_SUMMON, g),
            Request::Dismiss => vec![OP_DISMISS],
            Request::Evolve(g) => guid(OP_EVOLVE, g),
            Request::DuelChallenge(g) => guid(OP_DUEL_CHALLENGE, g),
            Request::DuelAnswer(yes) => vec![OP_DUEL_ANSWER, u8::from(yes)],
            Request::DuelForfeit => vec![OP_DUEL_FORFEIT],
            Request::Ride => vec![OP_RIDE],
            Request::Pick {
                item,
                stage,
                option,
            } => {
                let mut o = guid(OP_PICK, item);
                o.extend_from_slice(&[stage, option]);
                o
            }
            Request::BarPress {
                slot,
                target,
                flags,
                point,
            } => {
                let mut o = vec![OP_BAR_PRESS, slot];
                o.extend_from_slice(&target.to_le_bytes());
                o.push(flags);
                for v in point {
                    o.extend_from_slice(&v.to_le_bytes());
                }
                o
            }
            Request::Autocast { slot, on } => vec![OP_AUTOCAST, slot, u8::from(on)],
        }
    }

    /// The server's `DecodeRequest`, for the tests: strict about lengths and ops.
    pub fn decode_request(body: &[u8]) -> Option<Request> {
        let (&op, rest) = body.split_first()?;
        let guid = || (rest.len() == 8).then(|| u64::from_le_bytes(rest.try_into().unwrap()));
        match op {
            OP_SUMMON => guid().map(Request::Summon),
            OP_EVOLVE => guid().map(Request::Evolve),
            OP_DUEL_CHALLENGE => guid().map(Request::DuelChallenge),
            OP_DUEL_ANSWER if rest.len() == 1 => Some(Request::DuelAnswer(rest[0] != 0)),
            OP_DISMISS if rest.is_empty() => Some(Request::Dismiss),
            OP_DUEL_FORFEIT if rest.is_empty() => Some(Request::DuelForfeit),
            OP_RIDE if rest.is_empty() => Some(Request::Ride),
            OP_PICK if rest.len() == 10 && (1..=2).contains(&rest[9]) && rest[8] < 3 => {
                Some(Request::Pick {
                    item: u64::from_le_bytes(rest[..8].try_into().unwrap()),
                    stage: rest[8],
                    option: rest[9],
                })
            }
            OP_BAR_PRESS if rest.len() == 22 && rest[0] < 10 => {
                let f = |at: usize| f32::from_le_bytes(rest[at..at + 4].try_into().unwrap());
                Some(Request::BarPress {
                    slot: rest[0],
                    target: u64::from_le_bytes(rest[1..9].try_into().unwrap()),
                    flags: rest[9],
                    point: [f(10), f(14), f(18)],
                })
            }
            OP_AUTOCAST if rest.len() == 2 && rest[0] < 10 => Some(Request::Autocast {
                slot: rest[0],
                on: rest[1] != 0,
            }),
            _ => None,
        }
    }
}

/// The species' names and stage counts, as `02_companions.sql` has them (the bar carries the
/// id). An unknown id shows as "Companion".
const SPECIES: &[(u16, &str, u8)] = &[
    (1, "Black Whelpling", 3),
    (2, "Crimson Whelpling", 3),
    (3, "Emerald Whelpling", 3),
    (4, "Azure Whelpling", 3),
    (5, "Sprite Darter Hatchling", 3),
    (6, "Wolf Pup", 3),
    (7, "Nightsaber Cub", 3),
    (8, "Frostsaber Cub", 3),
    (9, "Panther Cub", 3),
    (10, "Bear Cub", 3),
    (11, "Young Web Spider", 3),
    (12, "Raptor Hatchling", 3),
    (13, "Young Crocolisk", 3),
    (14, "Infant Gorilla", 3),
    (15, "Owlet", 3),
    (16, "Duskbat Pup", 3),
    (17, "Plainstrider Chick", 3),
    (18, "Scorpid Hatchling", 3),
    (19, "Ember Spark", 3),
    (20, "Water Mote", 3),
    (21, "Pebble", 3),
    (22, "Zephyr", 3),
    (23, "Murloc Hatchling", 3),
    (24, "Fel Cinder", 3),
    (25, "Oozeling", 3),
    (26, "Snow Tiger Kitten", 3),
    (27, "Snowshoe Rabbit", 3),
    (28, "Spider Tank Bot", 3),
    (29, "Kodo Calf", 3),
    (30, "Foal", 3),
    (31, "Mechanostrider Runt", 3),
    (32, "Skeletal Foal", 3),
    (33, "Ram Kid", 3),
    // The catalog's new lines (design/COMPANION_CATALOG.md, 08_companion_lines.sql).
    (101, "Webwood Spiderling", 3),
    (102, "Darkmist Hatchling", 3),
    (103, "Cinderweb Spiderling", 3),
    (104, "Black Bear Cub", 3),
    (105, "Ironfur Cub", 3),
    (106, "Stranglethorn Kitten", 3),
    (107, "Mountain Lion Cub", 3),
    (108, "Savannah Cub", 3),
    (109, "Bloodsnout Pup", 3),
    (110, "Silvermane Pup", 3),
    (111, "Timber Wolf Pup", 3),
    (112, "Sunscale Hatchling", 3),
    (113, "Deviate Hatchling", 3),
    (114, "Bloodfen Hatchling", 3),
    (115, "Venomtail Scorpling", 3),
    (116, "Stonelash Scorpling", 3),
    (117, "Drywallow Hatchling", 3),
    (118, "Snapjaw Hatchling", 3),
    (119, "Surf Glider Hatchling", 3),
    (120, "Groddoc Infant", 3),
    (121, "Goretusk Piglet", 3),
    (122, "Pygmy Crawler", 3),
    (123, "Cloud Serpent Hatchling", 3),
    (124, "Swoop Fledgling", 3),
    (125, "Roc Chick", 3),
    (126, "Hecklefang Pup", 3),
    (127, "Strigid Owlet", 3),
    (128, "Kraul Batling", 3),
    (129, "Foreststrider Fledgling", 3),
    (130, "Aged Kodo Calf", 3),
    (131, "Gritjaw Hatchling", 3),
    (132, "Glasshide Hatchling", 3),
    (133, "Thresher Fry", 3),
    (134, "Mystlash Hydraling", 3),
    (135, "Timberling Sprout", 3),
    (136, "Toxic Mote", 3),
    (137, "Cracked Golemling", 3),
    (138, "Ectoplasm Blob", 3),
    (139, "Creeping Sludgeling", 3),
    (140, "Black Drake Hatchling", 3),
    (141, "Imp Familiar", 3),
    (142, "Felstalker Pup", 3),
    (143, "Doomguard Spawn", 3),
    (144, "Darkhound Whelp", 3),
    (145, "Rattlecage Bones", 3),
    (146, "Ghoul Thrall", 3),
    (147, "Wailing Shade", 3),
    (148, "Crypt Scuttler", 3),
    (149, "Servo", 3),
    (150, "Compact Reaper", 3),
    (151, "Mechano-Tankling", 3),
    (152, "Riverpaw Runt", 3),
    (153, "Kobold Vermin", 3),
    (154, "Rockjaw Trogg", 3),
    (155, "Ogre Runt", 3),
    (156, "Kolkar Drudge", 3),
    (157, "Bristleback Piglet", 3),
    (158, "Gnarlpine Cub", 3),
    (159, "Windfury Fledgling", 3),
    (160, "Troll Whelp", 3),
    (161, "Stormscale Wave Rider", 3),
    (162, "Makrura Clacker", 3),
    (163, "Young Moonkin", 3),
    (164, "Moonrage Whelp", 3),
    // The vanilla pets (31_vanilla_companions.sql).
    (165, "Shadowmaw Kitten", 3),
    (166, "Ridge Stalker Kitten", 3),
    (167, "Jungle Tiger Kitten", 3),
    (168, "Winterspring Kitten", 3),
    (169, "Ice Tiger Kitten", 3),
    (170, "Nightstalker Kitten", 3),
    (171, "Corrupted Kitten", 3),
    (172, "Green Wing Macaw", 3),
    (173, "Hyacinth Macaw", 3),
    (174, "Senegal Parrot", 3),
    (175, "Cockatiel", 3),
    (176, "Snowy Owlet", 3),
    (177, "Olive Serpent Hatchling", 3),
    (178, "Brown Snake", 3),
    (179, "Crimson Snake", 3),
    (180, "Cockroach", 3),
    (181, "Prairie Dog", 3),
    (182, "Mechanostrider Hatchling", 3),
    (183, "Ivory Tallstrider Chick", 3),
    (184, "Gray Tallstrider Chick", 3),
    (185, "Tree Frog", 3),
    (186, "Wood Frog", 3),
    (187, "Pet Bombling", 3),
    (188, "Lil' Smoky", 3),
    (189, "Lifelike Toad", 3),
    (190, "Tranquil Mechanical Yeti", 3),
    (191, "Smolderweb Hatchling", 3),
    (192, "Zergling", 3),
    (193, "Panda Cub", 3),
    (194, "Mini Diablo", 3),
    (195, "Albino Snapjaw", 3),
    (196, "Loggerhead Snapjaw", 3),
    (197, "Hawksbill Snapjaw", 3),
    (198, "Leatherback Snapjaw", 3),
    (199, "Olive Snapjaw", 3),
    (200, "Tiny Red Dragon", 3),
    (201, "Tiny Green Dragon", 3),
    (202, "Jubling", 3),
    (203, "Murki", 3),
    (204, "Gurky", 3),
    (205, "Terky", 3),
    (206, "Baby Shark", 3),
    (207, "Peddlefeet", 3),
    (208, "Poley", 3),
    (209, "Speedy", 3),
    (210, "Mr. Wiggles", 3),
    (211, "Whiskers the Rat", 3),
    (212, "Spirit of Summer", 3),
    (213, "White Tiger Cub", 3),
    (214, "Hippogryph Hatchling", 3),
    // The flying mounts (35_flying_companions.sql).
    (215, "Gryphon Hatchling", 3),
    (216, "Wind Rider Cub", 3),
    (217, "Frayfeather Hatchling", 3),
    (218, "Dragonhawk Hatchling", 3),
    (219, "Netherwhelp", 3),
    (220, "Proto-Drake Whelp", 3),
    (221, "Frostbrood Whelp", 3),
    (222, "Phoenix Hatchling", 3),
    (223, "Bronze Whelpling", 3),
];

pub fn species_name(id: u16) -> &'static str {
    SPECIES
        .iter()
        .find(|s| s.0 == id)
        .map_or("Companion", |s| s.1)
}

fn stage_count(id: u16) -> u8 {
    SPECIES.iter().find(|s| s.0 == id).map_or(1, |s| s.2)
}

/// How many species the client names (the look table's tests hold the server's to it).
#[cfg(test)]
pub(crate) fn species_count() -> usize {
    SPECIES.len()
}

/// A species' stage count, 1 for one the client does not know.
#[cfg(test)]
pub(crate) fn stages_of(id: u16) -> u8 {
    stage_count(id)
}

/// Whether an item entry is a companion's (the server's `IsCompanionItem`).
pub fn is_companion_item(item_id: u32) -> bool {
    (90_001..=90_099).contains(&item_id) || (91_100..=91_999).contains(&item_id)
}

/// The traits' names by id (`Companions::Trait`, 1-30); 27-30 are legendary.
const TRAITS: [&str; 30] = [
    "Ironhide",
    "Swift",
    "Ferocious",
    "Bloodthirsty",
    "Berserk",
    "Venomous",
    "Thick-skulled",
    "Fire-touched",
    "Frost-touched",
    "Shadow-touched",
    "Storm-touched",
    "Spellward",
    "Loyal",
    "Tracker",
    "Scavenger",
    "Forager",
    "Packmule",
    "Giant",
    "Runt",
    "Albino",
    "Shadowcoat",
    "Gilded",
    "Surefooted",
    "Skybound",
    "Tireless",
    "Gunner",
    "Phoenix-blooded",
    "Alpha",
    "Twin-souled",
    "Primal",
];

/// The named combinations (`Companions::Combos`): name and its two trait ids.
const COMBOS: [(&str, u8, u8); 14] = [
    ("Viper", 6, 2),
    ("Juggernaut", 1, 7),
    ("Bloodfrenzy", 4, 5),
    ("Wildfire", 8, 3),
    ("Glacier", 9, 1),
    ("Stormcaller", 11, 2),
    ("Nightstalker", 21, 10),
    ("Voidward", 12, 10),
    ("Colossus", 18, 1),
    ("Warhorse", 23, 25),
    ("Pathfinder", 14, 16),
    ("Golden Hoard", 22, 15),
    ("Quartermaster", 26, 17),
    ("Ashborn", 27, 8),
];

pub fn trait_name(id: u8) -> &'static str {
    TRAITS
        .get(usize::from(id).wrapping_sub(1))
        .copied()
        .unwrap_or("")
}

/// A companion item's suffix factor, read back (the server's `FromItemSeed`): level 6 bits,
/// stage 2, species 8, three trait ids of 5 bits, and the bred bit.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Seed {
    pub species: u16,
    pub stage: u8,
    pub level: u8,
    /// Trait ids, lowest first, 0 for none.
    pub traits: [u8; 3],
    pub bred: bool,
}

pub fn decode_seed(seed: u32) -> Option<Seed> {
    let level = (seed & 0x3F) as u8;
    let stage = (seed >> 6 & 3) as u8;
    let species = (seed >> 8 & 0xFF) as u16;
    if species == 0 || !(1..=60).contains(&level) || stage > 2 {
        return None;
    }
    let mut traits = [0u8; 3];
    for (i, t) in traits.iter_mut().enumerate() {
        *t = (seed >> (16 + 5 * i) & 0x1F) as u8;
        if *t > 30 {
            return None;
        }
    }
    Some(Seed {
        species,
        stage,
        level,
        traits,
        bred: seed >> 31 != 0,
    })
}

/// "Rare: Swift, Venomous" (the rarity from the traits: one Uncommon, two Rare, three Epic, a
/// legendary one Legendary); empty for none.
pub fn trait_line(traits: &[u8]) -> String {
    let ids: Vec<u8> = traits.iter().copied().filter(|&t| t != 0).collect();
    if ids.is_empty() {
        return String::new();
    }
    let rarity = if ids.iter().any(|&t| t >= 27) {
        "Legendary"
    } else {
        ["", "Uncommon", "Rare", "Epic"][ids.len().min(3)]
    };
    let names: Vec<&str> = ids.iter().map(|&t| trait_name(t)).collect();
    format!("{rarity}: {}", names.join(", "))
}

/// The combinations a set of traits makes: "Viper: Venomous + Swift".
pub fn combo_line(traits: &[u8]) -> String {
    COMBOS
        .iter()
        .filter(|(_, a, b)| traits.contains(a) && traits.contains(b))
        .map(|(n, a, b)| format!("{n}: {} + {}", trait_name(*a), trait_name(*b)))
        .collect::<Vec<_>>()
        .join("; ")
}

/// Bred companions' lineage, by their item seed (the server's op 6 for the items in the bags).
static LINEAGE: std::sync::LazyLock<std::sync::Mutex<std::collections::HashMap<u32, Lineage>>> =
    std::sync::LazyLock::new(Default::default);

fn lineage_of(seed: u32) -> Option<Lineage> {
    LINEAGE.lock().ok()?.get(&seed).copied()
}

/// The bags' fainted companions, by their item seed: when each comes round on its own (`None`:
/// not fainted). Filled from the Companions window's carried list (op 31); two companions that
/// share a seed but not a state are left out, so a tooltip never claims what it can't know.
static FAINTED: std::sync::LazyLock<
    std::sync::Mutex<std::collections::HashMap<u32, Option<std::time::Instant>>>,
> = std::sync::LazyLock::new(Default::default);

/// The carried list's faints: `(seed, Some(seconds until it comes round))` for a fainted one.
pub fn note_fainted(list: impl IntoIterator<Item = (u32, Option<u32>)>) {
    let map = faint_map(list, std::time::Instant::now());
    if let Ok(mut m) = FAINTED.lock() {
        *m = map;
    }
}

/// [`note_fainted`]'s map: by seed, when it comes round (`None`: not fainted); a seed shared by
/// a fainted and a whole companion is left out.
pub fn faint_map(
    list: impl IntoIterator<Item = (u32, Option<u32>)>,
    now: std::time::Instant,
) -> std::collections::HashMap<u32, Option<std::time::Instant>> {
    let mut seen: std::collections::HashMap<u32, Option<Option<std::time::Instant>>> =
        std::collections::HashMap::new();
    for (seed, secs) in list {
        let at = secs.map(|s| now + std::time::Duration::from_secs(u64::from(s)));
        seen.entry(seed)
            .and_modify(|v| {
                // The same seed twice, one fainted and one not: unknown.
                if v.is_some_and(|x| x.is_some() != at.is_some()) {
                    *v = None;
                }
            })
            .or_insert(Some(at));
    }
    seen.into_iter()
        .filter_map(|(k, v)| v.map(|v| (k, v)))
        .collect()
}

/// Seconds until this item's companion comes round, when it has fainted.
pub fn fainted_secs(seed: u32) -> Option<u32> {
    let at = (*FAINTED.lock().ok()?.get(&seed)?)?;
    let left = at
        .saturating_duration_since(std::time::Instant::now())
        .as_secs();
    (left > 0).then(|| u32::try_from(left).unwrap_or(u32::MAX))
}

/// "Gen 2, of Wolf Pup and Timber Wolf Pup, bred 1 of 3".
pub fn lineage_line(l: &Lineage) -> String {
    let mut s = format!(
        "Gen {}, of {} and {}, bred {} of 3",
        l.generation,
        species_name(l.parent_a),
        species_name(l.parent_b),
        l.bred
    );
    if l.hybrid != 0 {
        s.push_str(&format!(", {} coloring", species_name(l.hybrid)));
    }
    s
}

/// A companion item's tooltip lines, from its suffix factor (the server's `ItemSeed`), so a
/// buyer sees what it is in bags, trades, mail and the auction house: "Level 43 Wolf Pup, stage
/// 3 of 3", then its rarity and traits, their combinations and its lineage when it was bred.
/// `None` for any other item or seed.
pub fn item_note(item_id: u32, seed: u32) -> Option<String> {
    item_note_with(item_id, seed, fainted_secs(seed))
}

/// [`item_note`] with its faint given: seconds until it comes round, when it has fainted.
pub fn item_note_with(item_id: u32, seed: u32, fainted: Option<u32>) -> Option<String> {
    if !is_companion_item(item_id) {
        return None;
    }
    let s = decode_seed(seed)?;
    let mut lines = vec![format!(
        "Level {} {}, stage {} of {}",
        s.level,
        species_name(s.species),
        s.stage + 1,
        stage_count(s.species).max(s.stage + 1)
    )];
    for l in [trait_line(&s.traits), combo_line(&s.traits)] {
        if !l.is_empty() {
            lines.push(l);
        }
    }
    if s.bred {
        lines.push(lineage_of(seed).map_or_else(|| "Bred".to_string(), |l| lineage_line(&l)));
    }
    if let Some(left) = fainted {
        lines.push(format!(
            "Fainted: comes round in {}",
            crate::companion_ui::wait_text(left)
        ));
        lines.push("Revive it sooner at a stable master or with a Reviving Draught".to_string());
    }
    Some(lines.join("\n"))
}

/// Why it can't evolve (`EvolveBlock`), for the bar's status line.
pub fn block_text(b: &Bar) -> String {
    match b.block {
        0 => "Ready to evolve".into(),
        1 => "Fully grown".into(),
        2 => "Summon it to evolve".into(),
        3 => "In combat".into(),
        4 => "In a companion duel".into(),
        5 => format!("Evolves at level {}", b.next_evolve_level),
        6 => "Must be in the right place to evolve".into(),
        7 => "Evolves only at a certain hour".into(),
        8 => "Needs more kills to evolve".into(),
        9 => format!(
            "Needs {} catalyst{} to evolve",
            b.catalyst_count.max(1),
            if b.catalyst_count > 1 { "s" } else { "" }
        ),
        _ => String::new(),
    }
}

/// A finished duel's line, from the reader's side.
pub fn duel_result_text(d: &DuelUpdate) -> String {
    let why = match d.end {
        1 => "beaten",
        2 => "forfeited",
        3 => "fled",
        4 => "an owner stepped in",
        5 => "time ran out",
        6 => "declined",
        7 => "not answered",
        8 => "called off",
        _ => "",
    };
    match d.winner {
        0 => format!("Your companion won ({why})"),
        1 => format!("Your companion lost ({why})"),
        _ => format!("No winner ({why})"),
    }
}

/// How soon after "Duel Target" the server's "challenged" counts as our own challenge.
pub const OWN_CHALLENGE_SECS: f32 = 3.0;

/// Whether a "challenged" arriving at `now` answers our own press (else someone challenges us).
pub fn own_challenge(sent_at: Option<f32>, now: f32) -> bool {
    sent_at.is_some_and(|t| now >= t && now - t <= OWN_CHALLENGE_SECS)
}

/// What the server last said: the bar, the duel, and when an evolution or result came in.
#[derive(Resource, Debug, Default)]
pub struct CompanionState {
    pub bar: Bar,
    pub duel: DuelUpdate,
    /// When the duel update arrived (a result banner fades).
    pub duel_at: f32,
    /// When it last evolved.
    pub evolved_at: Option<f32>,
    /// We sent the challenge now pending (the server tells both sides "challenged"; only the
    /// challenged one accepts).
    pub challenger: bool,
    /// When we last pressed "Duel Target": a "challenged" arriving soon after is our own
    /// challenge; any other is someone challenging us (a refused challenge sends none).
    pub challenge_sent_at: Option<f32>,
    /// The companion out's moveset and choices (op 4).
    pub moves: Moves,
    /// When each pet bar slot is ready again (elapsed seconds), and its whole cooldown.
    pub ready_at: [f32; 10],
    pub cooldown: [f32; 10],
    /// When Command (slot 1) was last pressed: a second press soon after is "follow".
    pub last_command: Option<f32>,
    /// The mount cast's news (op 7) for the cast bar (crate::companion_bar), taken once.
    pub mount_cast: Option<(u8, u32)>,
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        benilla_ui::script::set_item_note(item_note);
        app.init_resource::<CompanionState>()
            .add_systems(
                Update,
                (forget_on_leaving, read_channel, pet_keys)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
        crate::companion_ui::plugin(app);
    }
}

/// Out of the world (logged out, or another character coming in): what the server said of the
/// last one's companion goes, so its pet keys don't outlive it.
fn forget_on_leaving(
    state: Res<crate::mode::FpsState>,
    mut was: Local<bool>,
    mut st: ResMut<CompanionState>,
) {
    if *was && !state.in_world {
        *st = CompanionState::default();
    }
    *was = state.in_world;
}

/// Seconds between two Command presses that make a double tap (follow).
const DOUBLE_TAP_SECS: f32 = 0.35;

/// Ctrl+1..Ctrl+0: the companion's pet bar, aimed by the crosshair. The unit under it and the
/// ground point go with every press; the server decides (Command: an enemy is attacked, the
/// ground is a spot to move to and hold, nothing or a double tap is follow; an ability goes at
/// the crosshair's enemy or point). Works with the gun up.
fn pet_keys(
    time: Res<Time>,
    input: Res<crate::input::Mw2Input>,
    aim: Res<Aim>,
    mut st: ResMut<CompanionState>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    // Ctrl+Space while riding a flying line: Launch, the mounted move's slot (the server takes
    // off from the ground, or climbs in the air).
    let surge = (input.sky_surge && crate::companion_bar::sky_surge_ready(&st))
        .then_some(crate::companion_bar::slot::MOUNTED);
    let Some(slot) = input.pet_slot.or(surge) else {
        return;
    };
    if !st.bar.out() {
        return;
    }
    let now = time.elapsed_secs();
    let mut flags = 0;
    let point = match aim.ground {
        Some(g) => {
            flags |= PRESS_POINT;
            crate::fusion::to_wow(g)
        }
        None => [0.0; 3],
    };
    if slot == 0 {
        if st.last_command.is_some_and(|t| now - t < DOUBLE_TAP_SECS) {
            flags |= PRESS_DOUBLE;
            st.last_command = None;
        } else {
            st.last_command = Some(now);
        }
    }
    let target = aim.target.as_ref().map_or(0, |t| t.guid);
    send(
        &mut out,
        Request::BarPress {
            slot,
            target,
            flags,
            point,
        },
    );
}

fn read_channel(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut st: ResMut<CompanionState>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_COMPANION {
            continue;
        }
        match m.body.first() {
            Some(&OP_BAR) => {
                if let Some(bar) = decode_bar(&m.body) {
                    st.bar = bar;
                }
            }
            Some(&OP_EVOLVED) => {
                if let Some((_, species, stage)) = decode_evolved(&m.body) {
                    info!(
                        "benilla-mw2: {} evolved to stage {}",
                        species_name(species),
                        stage + 1
                    );
                    st.evolved_at = Some(time.elapsed_secs());
                }
            }
            Some(&OP_DUEL) => {
                if let Some(d) = decode_duel(&m.body) {
                    let now = time.elapsed_secs();
                    if d.phase == DuelPhase::Challenged {
                        st.challenger = own_challenge(st.challenge_sent_at, now);
                        st.challenge_sent_at = None;
                    } else {
                        st.challenger = false;
                    }
                    st.duel = d;
                    st.duel_at = time.elapsed_secs();
                }
            }
            Some(&OP_MOVES) => {
                if let Some(moves) = decode_moves(&m.body) {
                    st.moves = moves;
                }
            }
            Some(&OP_COOLDOWN) => {
                if let Some((slot, ms)) = decode_cooldown(&m.body)
                    && usize::from(slot) < 10
                {
                    let secs = ms as f32 / 1000.0;
                    st.ready_at[usize::from(slot)] = time.elapsed_secs() + secs;
                    st.cooldown[usize::from(slot)] = secs;
                }
            }
            Some(&OP_MOUNT_CAST) => {
                if let Some(news) = decode_mount_cast(&m.body) {
                    st.mount_cast = Some(news);
                }
            }
            Some(&OP_LINEAGE) => {
                if let Some((seed, l)) = decode_lineage(&m.body)
                    && let Ok(mut map) = LINEAGE.lock()
                {
                    map.insert(seed, l);
                }
            }
            _ => {}
        }
    }
}

fn send(out: &mut MessageWriter<SendFusionChannel>, r: Request) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL_COMPANION,
        body: encode_request(r),
    }));
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_companion_items_tooltip_names_its_level_and_stage() {
        // The server's `ItemSeed`: level, stage << 6, species << 8, traits from bit 16, bred 31.
        let seed = 43 | 2 << 6 | 6 << 8;
        assert_eq!(
            item_note(90_006, seed).as_deref(),
            Some("Level 43 Wolf Pup, stage 3 of 3")
        );
        // A mount's item names its species from the seed, not the entry.
        assert_eq!(
            item_note(90_061, 40 | 2 << 6 | 29 << 8).as_deref(),
            Some("Level 40 Kodo Calf, stage 3 of 3")
        );
        // A new line's item, with traits and a combination (the C++ test's seed).
        let full: u32 = 43 | 2 << 6 | 164 << 8 | 2 << 16 | 6 << 21 | 30 << 26 | 1 << 31;
        assert_eq!(
            item_note(91_164, full).as_deref(),
            Some(
                "Level 43 Moonrage Whelp, stage 3 of 3\nLegendary: Swift, Venomous, Primal\n\
                 Viper: Venomous + Swift\nBred"
            )
        );
        LINEAGE.lock().unwrap().insert(
            full,
            Lineage {
                generation: 2,
                parent_a: 6,
                parent_b: 111,
                bred: 1,
                hybrid: 0,
            },
        );
        assert!(
            item_note(91_164, full)
                .unwrap()
                .ends_with("Gen 2, of Wolf Pup and Timber Wolf Pup, bred 1 of 3")
        );
        assert_eq!(trait_line(&[2, 6, 0]), "Rare: Swift, Venomous".to_string());
        // Not a companion item, or a seed that is no companion's.
        assert_eq!(item_note(12_345, seed), None);
        assert_eq!(item_note(90_006, 6 << 8), None);
        assert_eq!(item_note(90_006, 1 << 6 | 5), None);
        assert_eq!(item_note(90_006, 6 << 8 | 3 << 6 | 5), None);
        assert_eq!(item_note(90_006, 6 << 8 | 5 | 31 << 16), None);
        assert!(is_companion_item(91_101) && !is_companion_item(90_101));
    }

    /// `companion_pet_bar_messages_round_trip` in CompanionMoveTests.cpp.
    #[test]
    fn pet_bar_messages_match_the_server() {
        let mut m = Moves {
            item: 0x0102_0304_0506_0708,
            species: 6,
            pending: 1,
            twin_stage: 2,
            traits: 1 << 1,
            autocast: 0x3FF,
            picks: [2, 0, 0],
            mounted_spell: 23110,
            mounted_name: "Run Them Down".into(),
            ..Moves::default()
        };
        m.spells[0] = [24604, 3604];
        m.names[0] = ["Furious Howl".into(), "Tendon Rip".into()];
        let bytes = encode_moves(&m);
        assert_eq!(bytes[0], OP_MOVES);
        assert_eq!(decode_moves(&bytes), Some(m.clone()));
        assert_eq!(decode_moves(&bytes[..bytes.len() - 1]), None);
        assert_eq!(
            decode_cooldown(&[OP_COOLDOWN, 4, 0xE0, 0x2E, 0, 0]),
            Some((4, 12000))
        );
        // `mounting_is_a_cast_the_companion_runs_in_front_for` in CompanionTests.cpp.
        assert_eq!(
            decode_mount_cast(&[OP_MOUNT_CAST, MOUNT_CAST_START, 0xFC, 0x08, 0, 0]),
            Some((MOUNT_CAST_START, 2300))
        );
        assert_eq!(decode_mount_cast(&[OP_MOUNT_CAST, 9, 0, 0, 0, 0]), None);
        assert_eq!(decode_mount_cast(&[OP_MOUNT_CAST, MOUNT_CAST_DONE, 0, 0]), None);
        let press = Request::BarPress {
            slot: 4,
            target: 77,
            flags: PRESS_POINT,
            point: [1.5, -2.0, 30.25],
        };
        let b = encode_request(press);
        assert_eq!(b.len(), 23);
        assert_eq!(decode_request(&b), Some(press));
        let pick = Request::Pick {
            item: 9,
            stage: 1,
            option: 2,
        };
        assert_eq!(decode_request(&encode_request(pick)), Some(pick));
        assert_eq!(
            encode_request(Request::Autocast { slot: 3, on: true }),
            vec![OP_AUTOCAST, 3, 1]
        );
        let lineage = [OP_LINEAGE, 0xDD, 0xCC, 0xBB, 0xAA, 2, 6, 0, 111, 0, 1, 0, 0];
        assert_eq!(
            decode_lineage(&lineage),
            Some((
                0xAABB_CCDD,
                Lineage {
                    generation: 2,
                    parent_a: 6,
                    parent_b: 111,
                    bred: 1,
                    hybrid: 0
                }
            ))
        );
    }

    use super::*;

    /// `companion_bar_packet_round_trips` in CompanionTests.cpp: the same bar, the same bytes.
    #[test]
    fn bar_round_trips_as_the_server_encodes_it() {
        let b = Bar {
            item: 0x0102_0304_0506_0708,
            species: 1,
            stage: 0,
            level: 20,
            effective_level: 20,
            xp: 1234,
            xp_to_next: 7350,
            flags: BAR_SUMMONED | BAR_CAN_EVOLVE,
            block: 0,
            next_evolve_level: 20,
            catalyst_count: 0,
            health_pct: 87,
        };
        let mut bytes = encode_bar(&b);
        assert_eq!(bytes.len(), 27);
        assert_eq!(bytes[0], OP_BAR);
        assert!(bytes[1] == 0x08 && bytes[8] == 0x01, "little-endian guid");
        assert!(bytes[9] == 1 && bytes[10] == 0, "species");
        assert!(bytes[14] == (1234 & 0xFF) as u8 && bytes[15] == (1234 >> 8) as u8);
        assert_eq!(decode_bar(&bytes), Some(b));
        assert!(b.out());
        bytes.pop();
        assert_eq!(decode_bar(&bytes), None);
        assert!(!Bar::default().out(), "item 0, flags 0: nothing out");
    }

    #[test]
    fn evolved_and_duel_match_the_server() {
        assert_eq!(
            encode_evolved(7, 3, 2),
            vec![OP_EVOLVED, 7, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2]
        );
        assert_eq!(decode_evolved(&encode_evolved(7, 3, 2)), Some((7, 3, 2)));
        let mut d = DuelUpdate {
            phase: DuelPhase::Finished,
            end: 1,
            winner: 0,
            opponent: 42,
        };
        let bytes = encode_duel(&d);
        assert_eq!(bytes.len(), 12);
        assert_eq!(decode_duel(&bytes), Some(d));
        d.winner = -1;
        assert_eq!(decode_duel(&encode_duel(&d)).unwrap().winner, -1);
        assert_eq!(bytes[3], 0);
        assert_eq!(encode_duel(&d)[3], 0xFF);
        assert!(
            duel_result_text(&DuelUpdate {
                winner: 1,
                end: 3,
                ..d
            })
            .contains("lost (fled)")
        );
    }

    /// `companion_requests_decode_strictly` in CompanionTests.cpp.
    #[test]
    fn requests_encode_as_the_server_decodes() {
        let rt = |r: Request| decode_request(&encode_request(r));
        assert_eq!(rt(Request::Summon(0xABCD)), Some(Request::Summon(0xABCD)));
        assert_eq!(rt(Request::Evolve(5)), Some(Request::Evolve(5)));
        assert_eq!(
            rt(Request::DuelChallenge(77)),
            Some(Request::DuelChallenge(77))
        );
        assert_eq!(
            rt(Request::DuelAnswer(true)),
            Some(Request::DuelAnswer(true))
        );
        assert_eq!(rt(Request::Dismiss), Some(Request::Dismiss));
        assert_eq!(rt(Request::DuelForfeit), Some(Request::DuelForfeit));
        assert_eq!(rt(Request::Ride), Some(Request::Ride));
        assert_eq!(encode_request(Request::Dismiss).len(), 1);
        assert_eq!(
            encode_request(Request::Summon(0xABCD)),
            vec![OP_SUMMON, 0xCD, 0xAB, 0, 0, 0, 0, 0, 0]
        );
        assert_eq!(
            encode_request(Request::DuelAnswer(true)),
            vec![OP_DUEL_ANSWER, 1]
        );
        assert_eq!(encode_request(Request::Ride), vec![OP_RIDE]);
        assert_eq!(decode_request(&[]), None);
        assert_eq!(decode_request(&[OP_SUMMON, 1, 2]), None, "short");
        assert_eq!(decode_request(&[OP_DISMISS, 0]), None, "long");
        assert_eq!(decode_request(&[OP_BAR]), None, "a server op");
        assert_eq!(decode_request(&[200]), None);
        assert_eq!(decode_request(&[OP_RIDE, 1]), None);
    }

    /// The species table is the world SQL's rows, name and stage count, every one of them (the
    /// C++ `companion_sql_matches_the_catalog` holds the SQL to the rules).
    #[test]
    fn species_table_matches_the_world_sql() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../server/sql/custom/azeroth_warfare");
        let mut rows = 0;
        for file in [
            "02_companions.sql",
            "08_companion_lines.sql",
            "31_vanilla_companions.sql",
            "35_flying_companions.sql",
        ] {
            let sql = std::fs::read_to_string(dir.join(file)).expect(file);
            let mut in_species = false;
            for line in sql.lines() {
                if line.starts_with("INSERT INTO `fusion_companion_species`") {
                    in_species = true;
                    continue;
                }
                if !in_species || !line.starts_with('(') {
                    continue;
                }
                // (id, 'name', item, family, role, creature 1, scale 1, creature 2, ...
                let (id, rest) = line[1..].split_once(", '").expect("a species row");
                let (name, rest) = rest.split_once("', ").expect("a quoted name");
                let f: Vec<&str> = rest.split(", ").collect();
                let stages = [f[3], f[5], f[7]].iter().take_while(|c| **c != "0").count() as u8;
                let id: u16 = id.parse().expect("an id");
                assert_eq!(
                    species_name(id),
                    name.replace("''", "'"),
                    "species {id}'s name"
                );
                assert_eq!(stage_count(id), stages, "species {id}'s stages");
                rows += 1;
                if line.trim_end().ends_with(");") {
                    in_species = false;
                }
            }
        }
        assert_eq!(rows, SPECIES.len(), "a species the client does not name");
    }

    #[test]
    fn names_and_evolve_lines() {
        assert_eq!(species_name(6), "Wolf Pup");
        assert_eq!(species_name(999), "Companion");
        assert_eq!(
            block_text(&Bar {
                block: 5,
                next_evolve_level: 30,
                ..Bar::default()
            }),
            "Evolves at level 30"
        );
    }

    #[test]
    fn only_a_challenge_just_sent_is_our_own() {
        assert!(!own_challenge(None, 10.0));
        assert!(own_challenge(Some(9.0), 10.0));
        assert!(!own_challenge(Some(9.0), 9.0 + OWN_CHALLENGE_SECS + 0.5));
        // A refused challenge sent long ago no longer hides someone else's.
        assert!(!own_challenge(Some(1.0), 100.0));
    }
}
