//! The druid's shapeshift forms in first person (`design/CLASS_CATALOG.md` section 9; the
//! server's `fusion/DruidForms.h`). A form is read off the player's own form byte
//! (`UNIT_FIELD_BYTES_1` byte 2); while one holds:
//!
//! - **no gun, no human hands**: the viewmodel (gun and arms) is down, the gun page and ready mode
//!   are off, the gun HUD is hidden and the gun keys are blanked; the number keys are WoW's action
//!   bar again, which shows the form's own page (its bonus bar). The left button swipes: it
//!   attacks the unit under the crosshair, as a click does. The server refuses a gun's shot in a
//!   form too.
//! - **the form's own front limbs** (crate::form_limbs): paws, hooves, talons, a spider's front
//!   legs, the creature model's own forelimb triangles, shown while the model plays an attack or
//!   a cast (a swipe, a pounce, a spell); the worgen keeps its clawed hands up at rest too.
//! - **no humanoid body** (crate::fp_body): the form's body or nothing.
//!
//! [`FORMS`] is the server's table as the client needs it; a test reads the server's rows back
//! (`server/src/fusion/src/DruidForms.cpp`) so the two cannot drift.

use benilla_app::Objects;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::input::{Control, Mw2Input};
use crate::mode::FpsState;

/// What the first person shows of a form's front limbs (`Fusion::DruidForms::Limbs`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Limbs {
    Paws,
    Hooves,
    Talons,
    /// A spider's front legs.
    Legs,
    Flippers,
    /// A moonkin's clawed wings.
    Claws,
    /// The worgen keeps hands, clawed.
    ClawedHands,
    /// A treant's arms.
    Branches,
}

/// The ways a form is left (`Fusion::DruidForms::Leave`).
pub mod leave {
    pub const RECAST: u8 = 0x01;
    pub const BUTTON: u8 = 0x02;
    pub const BUFF: u8 = 0x04;
    pub const OTHER_FORM: u8 = 0x08;
    pub const NON_FORM_SPELL: u8 = 0x10;
    pub const DEATH: u8 = 0x20;
    pub const TIMED: u8 = 0x40;
    /// Every druid form's: recast, its button, its buff, another form, death.
    pub const STOCK: u8 = RECAST | BUTTON | BUFF | OTHER_FORM | DEATH;
}

/// One form's first-person rules.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FormRules {
    /// The form byte.
    pub form: u8,
    /// The spell that enters it.
    pub spell: u32,
    pub name: &'static str,
    /// The display it wears (the server's; Alliance's for a stock form).
    pub display: u32,
    /// Its action bar page (`GetBonusBarOffset`), 0 for none.
    pub bar_page: u8,
    /// The front limbs shown, `None` for none.
    pub limbs: Option<Limbs>,
    /// Shown at rest too, not only in action.
    pub limbs_always: bool,
    pub guns: bool,
    /// A weapon or shield held in a hand.
    pub held_weapon: bool,
    /// The humanoid first-person body (crate::fp_body).
    pub humanoid_body: bool,
    pub leave: u8,
}

const fn animal(form: u8, spell: u32, name: &'static str, display: u32, page: u8, limbs: Limbs) -> FormRules {
    FormRules {
        form,
        spell,
        name,
        display,
        bar_page: page,
        limbs: Some(limbs),
        limbs_always: false,
        guns: false,
        held_weapon: false,
        humanoid_body: false,
        leave: leave::STOCK,
    }
}

/// Every form the first person knows: the druid's ten (stock and new, the server's
/// `Fusion::DruidForms::Forms`) and the shaman's Ghost Wolf, an animal too. Tree of Life is no
/// form: the druid keeps casting in it, in a treant's look, hands and all.
pub const FORMS: [FormRules; 11] = [
    animal(1, 768, "Cat Form", 892, 1, Limbs::Paws),
    animal(5, 5487, "Bear Form", 2281, 3, Limbs::Paws),
    animal(8, 9634, "Dire Bear Form", 2281, 3, Limbs::Paws),
    animal(3, 783, "Travel Form", 632, 5, Limbs::Paws),
    animal(4, 1066, "Aquatic Form", 2428, 6, Limbs::Flippers),
    animal(31, 24858, "Moonkin Form", 15374, 4, Limbs::Claws),
    animal(9, 21936, "Stag Form", 8013, 7, Limbs::Hooves),
    animal(10, 8153, "Crow Form", 4566, 8, Limbs::Talons),
    animal(11, 24084, "Spider Form", 15226, 9, Limbs::Legs),
    FormRules {
        limbs_always: true,
        ..animal(12, 7621, "Worgen Form", 202, 10, Limbs::ClawedHands)
    },
    // The shaman's Ghost Wolf: an animal, no gun; its bar is the shaman's own (none in 1.12).
    animal(16, 2645, "Ghost Wolf", 4613, 0, Limbs::Paws),
];

/// The druid's forms (every row but the shaman's Ghost Wolf).
pub fn druid_forms() -> impl Iterator<Item = &'static FormRules> {
    FORMS.iter().filter(|f| f.form != 16)
}

/// The rules of a form byte; `None` for no form and for the stances (warrior stances, Stealth,
/// Shadowform), which keep the gun and the hands.
pub fn rules(form: u8) -> Option<&'static FormRules> {
    FORMS.iter().find(|f| f.form == form && form != 0)
}

/// The player's form this frame.
#[derive(Resource, Clone, Copy, Debug, Default)]
pub struct PlayerForm {
    pub form: u8,
    pub rules: Option<&'static FormRules>,
    /// The left button went down this frame in a form: a swipe at the crosshair's unit.
    pub swipe: bool,
}

impl PlayerForm {
    /// In a form that holds no gun: the gun, its HUD and its keys are off.
    pub fn gunless(&self) -> bool {
        self.rules.is_some_and(|r| !r.guns)
    }

    /// In a form whose hands hold nothing (the gun's arms, the melee weapon, the spell glows).
    pub fn handless(&self) -> bool {
        self.rules.is_some_and(|r| !r.held_weapon)
    }

    /// The humanoid first-person body is not this form's.
    pub fn hides_humanoid_body(&self) -> bool {
        self.rules.is_some_and(|r| !r.humanoid_body)
    }
}

/// The form read and the gun controls blanked: whatever writes the controls this frame (a probe)
/// goes before.
#[derive(SystemSet, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct FormsSet;

pub struct FormsPlugin;

impl Plugin for FormsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PlayerForm>()
            .add_systems(
                Update,
                (read_form, blank_gun_controls)
                    .chain()
                    .in_set(Mw2Set::Mode)
                    .in_set(FormsSet)
                    .before(crate::cac::CacSet),
            )
            .add_systems(Update, swipe.in_set(Mw2Set::Weapon))
            .add_systems(Update, hide_gun_hud.in_set(Mw2Set::Hud));
    }
}

/// The gun's HUD (its rounds, its name) is off in a gunless form.
fn hide_gun_hud(
    form: Res<PlayerForm>,
    mut column: Query<&mut Visibility, With<crate::hud::AmmoColumn>>,
) {
    let want = if form.gunless() {
        Visibility::Hidden
    } else {
        Visibility::Inherited
    };
    for mut v in &mut column {
        if *v != want {
            *v = want;
        }
    }
}

fn read_form(state: Res<FpsState>, objects: Objects, mut form: ResMut<PlayerForm>) {
    let byte = state
        .self_guid
        .and_then(|g| objects.object(g))
        .map_or(0, |f| f.unit_shapeshift_form());
    if form.form != byte {
        let r = rules(byte);
        info!(
            "benilla-mw2: form {byte} ({})",
            r.map_or("none or a stance", |r| r.name)
        );
        form.form = byte;
        form.rules = r;
    }
}

/// The gun's controls blanked in a gunless form (the left button kept as the swipe), before
/// anything reads them this frame.
pub fn blank_gun_controls(
    state: Res<FpsState>,
    mut form: ResMut<PlayerForm>,
    mut input: ResMut<Mw2Input>,
) {
    let gunless = state.active && form.gunless();
    form.swipe = gunless && input.fire.pressed;
    if !gunless {
        return;
    }
    let input = &mut *input;
    for c in [
        &mut input.fire,
        &mut input.ads,
        &mut input.reload,
        &mut input.lethal,
        &mut input.tactical,
        &mut input.killstreak,
    ] {
        *c = Control::default();
    }
    input.hand = None;
    input.wheel_swap = false;
}

/// The swipe: the unit under the crosshair is attacked (or, a friend, selected), as a click does.
fn swipe(
    form: Res<PlayerForm>,
    aim: Res<crate::aim::Aim>,
    mut interact: MessageWriter<benilla_app::Interact>,
) {
    if !form.swipe {
        return;
    }
    if let Some(t) = aim.target.as_ref().filter(|t| !t.service) {
        interact.write(benilla_app::Interact(t.entity));
    }
}

/// The front limbs of a creature model, right then left: the bones under each limb's root. A
/// limb runs from its hand's attachment bone (`HandRight` 1, `HandLeft` 2, which every creature
/// that casts or strikes carries on its front feet) up to the last bone whose subtree holds
/// neither the other hand nor the head, so the chest and the head stay out. `parents` is the
/// skeleton's, `hands` the two attachment bones, `head` the head's (attachment 11's bone).
pub fn forelimb_bones(
    parents: &[i16],
    hands: [Option<usize>; 2],
    head: Option<usize>,
) -> [Vec<bool>; 2] {
    let parent = |b: usize| parents.get(b).and_then(|&p| usize::try_from(p).ok());
    let ancestor_or_self = |a: usize, b: usize| {
        let mut cur = Some(b);
        for _ in 0..=parents.len() {
            match cur {
                Some(c) if c == a => return true,
                Some(c) => cur = parent(c),
                None => return false,
            }
        }
        false
    };
    let limb = |hand: Option<usize>, other: Option<usize>| {
        let mut under = vec![false; parents.len()];
        let Some(hand) = hand.filter(|&h| h < parents.len()) else {
            return under;
        };
        // The same bone for both hands (one paw carries both points): no pair to split. A hand
        // point on the head (a bird's beak) is no limb.
        if other == Some(hand) || head.is_some_and(|h| ancestor_or_self(h, hand)) {
            return under;
        }
        let mut root = hand;
        while let Some(p) = parent(root) {
            let takes = |x: Option<usize>| x.is_some_and(|x| ancestor_or_self(p, x));
            if takes(other) || takes(head) || parent(p).is_none() {
                break;
            }
            root = p;
        }
        for (b, flag) in under.iter_mut().enumerate() {
            *flag = ancestor_or_self(root, b);
        }
        under
    };
    [limb(hands[0], hands[1]), limb(hands[1], hands[0])]
}

/// What a creature model's skeleton offers for its front limbs, as its M2 file has it.
pub struct LimbSkeleton<'a> {
    pub parents: &'a [i16],
    /// Bind pivots, WoW model space: x forward, y to the left, z up.
    pub pivots: &'a [[f32; 3]],
    /// Each bone's key-bone id, -1 for none (2 ShoulderL, 3 ShoulderR, 6 Head).
    pub key_bones: &'a [i16],
    /// The `HandRight` (1) and `HandLeft` (2) attachment bones.
    pub hands: [Option<usize>; 2],
    /// The head: the `Head` attachment's (11) bone, else key bone 6.
    pub head: Option<usize>,
}

/// Where a model's front limbs were found.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LimbSource {
    /// Its hand attachment points (moonkin, spider, worgen).
    HandPoints,
    /// Its shoulder key bones, each shoulder's whole subtree (a sea lion's flippers, a treant's
    /// arms).
    Shoulders,
    /// Its two foremost feet, the lowest leaves of the skeleton (cat, bear, cheetah, stag, wolf;
    /// an owl's talons).
    FrontFeet,
    /// Nothing to show.
    None,
}

/// A foot is at least this deep in the skeleton (root, spine, shoulder or hip, leg, foot).
pub const MIN_FOOT_DEPTH: usize = 3;

/// The front limbs of a form's creature model, right then left, and how they were found: the hand
/// points first, else the shoulder key bones, else the two foremost feet (measured on the forms'
/// models, `examples/form_limbs.rs`: the quadrupeds carry no hand points and no arm key bones).
pub fn find_forelimbs(s: &LimbSkeleton) -> ([Vec<bool>; 2], LimbSource) {
    let n = s.parents.len();
    let key = |k: i16| s.key_bones.iter().position(|&b| b == k);
    let head = s.head.or_else(|| key(6));
    let any = |l: &[Vec<bool>; 2]| l.iter().flatten().any(|&b| b);
    if s.hands.iter().all(Option::is_some) {
        let limbs = forelimb_bones(s.parents, s.hands, head);
        if any(&limbs) {
            return (limbs, LimbSource::HandPoints);
        }
    }
    let parent = |b: usize| s.parents.get(b).and_then(|&p| usize::try_from(p).ok());
    let under = |root: usize, b: usize| {
        let mut cur = Some(b);
        for _ in 0..=n {
            match cur {
                Some(c) if c == root => return true,
                Some(c) => cur = parent(c),
                None => return false,
            }
        }
        false
    };
    if let (Some(r), Some(l)) = (key(3), key(2)) {
        let subtree = |root: usize| -> Vec<bool> {
            if head.is_some_and(|h| under(root, h)) {
                return vec![false; n];
            }
            (0..n).map(|b| under(root, b)).collect()
        };
        let limbs = [subtree(r), subtree(l)];
        if any(&limbs) {
            return (limbs, LimbSource::Shoulders);
        }
    }
    // The feet: leaves in the lowest quarter of the skeleton's height, outside the head, at the
    // end of a chain (the creature models carry loose root-level markers on the ground, a
    // footprint's, which are no feet).
    let z = |b: usize| s.pivots.get(b).map_or(0.0, |p| p[2]);
    let (lo, hi) = (0..n).fold((f32::MAX, f32::MIN), |(lo, hi), b| (lo.min(z(b)), hi.max(z(b))));
    let leaf = |b: usize| !s.parents.iter().any(|&p| usize::try_from(p).ok() == Some(b));
    let depth = |b: usize| {
        let mut d = 0;
        let mut cur = parent(b);
        while let Some(c) = cur.filter(|_| d <= n) {
            d += 1;
            cur = parent(c);
        }
        d
    };
    let feet: Vec<usize> = (0..n)
        .filter(|&b| leaf(b) && depth(b) >= MIN_FOOT_DEPTH && z(b) <= lo + 0.25 * (hi - lo))
        .filter(|&b| !head.is_some_and(|h| under(h, b)))
        .collect();
    let foremost = |right: bool| {
        feet.iter()
            .copied()
            .filter(|&b| {
                let y = s.pivots[b][1];
                if right { y < 0.0 } else { y > 0.0 }
            })
            .max_by(|&a, &b| s.pivots[a][0].total_cmp(&s.pivots[b][0]))
    };
    let ends = [foremost(true), foremost(false)];
    if ends.iter().all(Option::is_some) {
        let limbs = forelimb_bones(s.parents, ends, head);
        if any(&limbs) {
            return (limbs, LimbSource::FrontFeet);
        }
    }
    ([vec![false; n], vec![false; n]], LimbSource::None)
}

/// A corner leaning at least this much on the limb's bones keeps its triangle.
pub const LIMB_LEAN: f32 = 0.5;

/// Whether a triangle belongs to the limb: every corner mostly on its bones.
pub fn keeps_limb_triangle(corners: [([u16; 4], [f32; 4]); 3], limb: &dyn Fn(usize) -> bool) -> bool {
    corners.iter().all(|(j, w)| {
        j.iter()
            .zip(w)
            .filter(|(j, _)| limb(usize::from(**j)))
            .map(|(_, w)| *w)
            .sum::<f32>()
            >= LIMB_LEAN
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_druid_form_has_its_page_its_limbs_and_no_gun() {
        let mut pages = std::collections::HashSet::new();
        for f in druid_forms() {
            // No gun, no weapon in a hand, no humanoid body: the form's body or nothing.
            assert!(!f.guns && !f.held_weapon && !f.humanoid_body, "{}", f.name);
            assert!(f.limbs.is_some(), "{}", f.name);
            // Only the worgen keeps (clawed) hands, and shows them at rest.
            assert_eq!(f.limbs == Some(Limbs::ClawedHands), f.form == 12, "{}", f.name);
            assert_eq!(f.limbs_always, f.form == 12, "{}", f.name);
            // Its own page: bear and dire bear are one family (1.12's page 9 for both).
            assert!((1..=12).contains(&f.bar_page), "{}", f.name);
            if f.form != 8 {
                assert!(pages.insert(f.bar_page), "{} shares a page", f.name);
            }
            // Left every stock way; a caster spell is refused in a form, never leaves it.
            assert_eq!(f.leave & leave::STOCK, leave::STOCK, "{}", f.name);
            assert_eq!(f.leave & leave::NON_FORM_SPELL, 0, "{}", f.name);
            assert_eq!(f.leave & leave::TIMED, 0, "{}", f.name);
            // The page's slots fit the action slot array.
            let last = 72 + 12 * u32::from(f.bar_page);
            assert!(last <= benilla_ui::script::ACTION_SLOTS, "{}", f.name);
        }
        assert_eq!(druid_forms().count(), 10);
        assert!(rules(2).is_none(), "Tree of Life is a look, not a form");
        assert_eq!(rules(1).unwrap().bar_page, 1, "cat keeps 1.12's page 7");
        assert_eq!(rules(5).unwrap().bar_page, 3, "bear keeps 1.12's page 9");
    }

    #[test]
    fn the_stances_keep_the_gun_and_the_hands() {
        for stance in [0u8, 17, 18, 19, 28, 30] {
            let p = PlayerForm {
                form: stance,
                rules: rules(stance),
                swipe: false,
            };
            assert!(!p.gunless() && !p.handless() && !p.hides_humanoid_body(), "{stance}");
        }
        let cat = PlayerForm {
            form: 1,
            rules: rules(1),
            swipe: false,
        };
        assert!(cat.gunless() && cat.handless() && cat.hides_humanoid_body());
        assert!(PlayerForm { form: 16, rules: rules(16), swipe: false }.gunless(), "Ghost Wolf");
    }

    /// The server's table (`DruidForms.cpp`), read back row by row: the same forms, spells,
    /// names, pages and limbs. Skips where the server's source is not beside the client.
    #[test]
    fn the_table_is_the_servers() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../server/src/fusion/src/DruidForms.cpp");
        let Ok(src) = std::fs::read_to_string(&path) else {
            eprintln!("no server source at {}: skipped", path.display());
            return;
        };
        let header = std::fs::read_to_string(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../server/src/fusion/include/fusion/DruidForms.h"),
        )
        .expect("the header beside its source");
        // `constexpr uint8_t kCat = 1;`
        let constant = |name: &str| -> u8 {
            let at = header
                .find(&format!("constexpr uint8_t {name} = "))
                .unwrap_or_else(|| panic!("{name}"));
            let rest = &header[at..];
            let value = rest.split('=').nth(1).unwrap().split(';').next().unwrap();
            value.trim().parse().unwrap()
        };
        let mut rows = 0;
        for line in src.lines() {
            // `rows.push_back(Stock(F(kCat, 768, "Cat Form", 1, Limbs::Paws), 892, 2, ...`
            let Some(at) = line.find("F(k") else {
                continue;
            };
            let inner = &line[at + 2..];
            let inner = &inner[..inner.find(')').unwrap()];
            let parts: Vec<&str> = inner.split(", ").collect();
            assert_eq!(parts.len(), 5, "{line}");
            let form = constant(parts[0]);
            let spell: u32 = parts[1].parse().unwrap();
            let name = parts[2].trim_matches('"');
            let page: u8 = parts[3].parse().unwrap();
            let limbs = parts[4].trim_start_matches("Limbs::");
            let r = rules(form).unwrap_or_else(|| panic!("form {form} ({name}) is the client's too"));
            assert_eq!(r.spell, spell, "{name}");
            assert_eq!(r.name, name);
            assert_eq!(r.bar_page, page, "{name}");
            assert_eq!(format!("{:?}", r.limbs.unwrap()), limbs, "{name}");
            // The display the row wears follows its modifiers on the same line.
            let tail = &line[at..];
            assert!(tail.contains(&format!(", {}, ", r.display)), "{name}'s display {}", r.display);
            rows += 1;
        }
        assert_eq!(rows, druid_forms().count(), "every druid form, both sides");
    }

    #[test]
    fn a_forelimb_runs_from_its_paw_to_below_the_chest() {
        // 0 root, 1 spine, 2 chest, 3 neck, 4 head, 5 r shoulder, 6 r leg, 7 r paw, 8 l shoulder,
        // 9 l leg, 10 l paw, 11 hips, 12 r hind leg.
        let parents = [-1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0, 11];
        let [right, left] = forelimb_bones(&parents, [Some(7), Some(10)], Some(4));
        let set = |v: &[bool]| v.iter().enumerate().filter(|(_, k)| **k).map(|(b, _)| b).collect::<Vec<_>>();
        assert_eq!(set(&right), vec![5, 6, 7]);
        assert_eq!(set(&left), vec![8, 9, 10]);
        // No hand points: no limbs (the view stays clear).
        let [r, l] = forelimb_bones(&parents, [None, None], Some(4));
        assert!(r.iter().chain(&l).all(|k| !k));
        // A hand point on the head (a bird's beak) is no limb; the other stays.
        let [r, l] = forelimb_bones(&parents, [Some(4), Some(10)], Some(4));
        assert!(set(&r).is_empty());
        assert_eq!(set(&l), vec![8, 9, 10]);
    }

    #[test]
    fn a_quadrupeds_front_feet_are_found_without_hand_points() {
        // 0 root, 1 spine, 2 chest, 3 neck, 4 head, 5 r shoulder, 6 r foot, 7 l shoulder, 8 l foot,
        // 9 hips, 10 r hind foot, 11 l hind foot, 12 tail tip.
        let parents = [-1, 0, 1, 2, 3, 2, 5, 2, 7, 0, 9, 9, 9];
        let pivots = [
            [0.0, 0.0, 0.8],
            [0.2, 0.0, 0.8],
            [0.5, 0.0, 0.8],
            [0.7, 0.0, 1.0],
            [0.9, 0.0, 1.1],
            [0.5, -0.2, 0.6],
            [0.5, -0.2, 0.0],
            [0.5, 0.2, 0.6],
            [0.5, 0.2, 0.0],
            [-0.4, 0.0, 0.8],
            [-0.4, -0.2, 0.0],
            [-0.4, 0.2, 0.0],
            [-0.9, 0.0, 0.7],
        ];
        let mut key_bones = [-1i16; 13];
        key_bones[4] = 6;
        let skel = LimbSkeleton {
            parents: &parents,
            pivots: &pivots,
            key_bones: &key_bones,
            hands: [None, None],
            head: None,
        };
        let ([r, l], source) = find_forelimbs(&skel);
        let set = |v: &[bool]| v.iter().enumerate().filter(|(_, k)| **k).map(|(b, _)| b).collect::<Vec<_>>();
        assert_eq!(source, LimbSource::FrontFeet);
        assert_eq!(set(&r), vec![5, 6], "the right foreleg, not the hind leg");
        assert_eq!(set(&l), vec![7, 8]);
        // Shoulder key bones win over the feet: their whole subtrees (a sea lion's flippers).
        let mut shouldered = key_bones;
        shouldered[5] = 3;
        shouldered[7] = 2;
        let skel = LimbSkeleton { key_bones: &shouldered, ..skel };
        let ([r, l], source) = find_forelimbs(&skel);
        assert_eq!(source, LimbSource::Shoulders);
        assert_eq!((set(&r), set(&l)), (vec![5, 6], vec![7, 8]));
        // Hand points win over both.
        let skel = LimbSkeleton { hands: [Some(6), Some(8)], ..skel };
        assert_eq!(find_forelimbs(&skel).1, LimbSource::HandPoints);
    }

    /// The live test's "You have no target" on Worgen Form: the client's cast arm builds its
    /// targeting word from `implicit_target_a1`, a field of its own, so a donor aimed at an enemy
    /// (Arugal's Curse: implicit target 6, a 30 yd range) stays aimed unless op 8 and op 7 both
    /// rewrite that field, not only the per-effect array.
    #[test]
    fn a_rewritten_form_needs_no_target_on_the_client() {
        use benilla_app::fusion_spells::{FormRow, SpellTweak, TWEAK_ON_SELF, apply_form, apply_tweak};
        use benilla_formats::{SpellCatalog, SpellDisplay};
        use std::collections::HashMap;
        let arugal = || SpellDisplay {
            effects: [6, 6, 0],
            effect_apply_aura: [6, 56, 0],
            implicit_target_a1: 6,
            effect_implicit_target_a: [6, 6, 0],
            range_index: 4,
            ..Default::default()
        };
        let worgen = FormRow {
            spell: 7621,
            form: 12,
            bonus_bar: 10,
            stance_bar_order: 8,
            form_flags: 0x50,
            creature_type: 7,
            name: "Worgen Form".into(),
        };
        // Op 8 alone.
        let mut catalog = SpellCatalog::from_displays(HashMap::from([(7621, arugal())]));
        let mut forms = HashMap::new();
        apply_form(&mut catalog, &mut forms, &worgen, None);
        let d = catalog.get(7621).unwrap();
        assert_eq!(d.implicit_target_a1, 1, "the cast arm's word: oneself");
        assert_eq!(d.effect_implicit_target_a, [1, 0, 0]);
        assert_eq!(d.name, "Worgen Form");
        // Op 7's ON_SELF alone (it may land first, or alone for another class's self refit).
        let mut d = arugal();
        apply_tweak(
            &mut d,
            &SpellTweak {
                spell: 7621,
                range_index: 1,
                flags: TWEAK_ON_SELF,
                ..Default::default()
            },
        );
        assert_eq!(d.implicit_target_a1, 1, "ON_SELF must turn the cast arm's word too");
    }

    #[test]
    fn a_limb_triangle_leans_on_the_limb() {
        let limb = |b: usize| b == 6;
        let on = ([6, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]);
        let half = ([6, 2, 0, 0], [0.5, 0.5, 0.0, 0.0]);
        let chest = ([2, 6, 0, 0], [0.8, 0.2, 0.0, 0.0]);
        assert!(keeps_limb_triangle([on, on, half], &limb));
        assert!(!keeps_limb_triangle([on, on, chest], &limb));
    }
}
