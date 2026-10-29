//! The craft's cast, as a stock trade's: Create (or Create All) on one of the layer's trade
//! windows fills the stock casting bar with the recipe's name for [`MAKE_SECS`], and only when it
//! completes does the client ask the server for one item (the server checks and pays for it as
//! before). Create All repeats it, the count box counting down (`GetTradeskillRepeatCount`), and
//! stops "Failed" when the bags can't make another. Walking off interrupts it, as a stock cast.

use bevy::math::Vec3;

use crate::cast_bar::Edge;

use super::Book;

/// One item's cast (vanilla's common trade cast time).
pub const MAKE_SECS: f32 = 3.0;
/// How far the player may drift (yards, on the ground) before the cast is interrupted.
pub const MOVE_YARDS: f32 = 0.6;

/// A make under way.
#[derive(Clone, Debug, PartialEq)]
pub struct Making {
    pub book: Book,
    pub recipe: u32,
    pub label: String,
    /// Items still to make, this one included.
    pub left: u32,
    pub started: f32,
    pub at: Vec3,
}

/// What a frame of making does.
#[derive(Clone, Debug, PartialEq)]
pub enum Act {
    /// Something for the stock casting bar.
    Bar(Edge),
    /// Ask the server for one item.
    Make { book: Book, recipe: u32 },
}

fn start_edge(label: &str) -> Edge {
    Edge::Start {
        label: label.to_owned(),
        total_ms: (MAKE_SECS * 1000.0) as u32,
        elapsed_ms: 0,
        color: None,
    }
}

/// The recipe's name on the bar: Construction's "[3]" kits-held suffix dropped.
pub fn bar_label(name: &str) -> String {
    match name.rfind(" [") {
        Some(i) if name.ends_with(']') => name[..i].to_owned(),
        _ => name.to_owned(),
    }
}

impl Making {
    /// Begin `count` items at `t`, standing at `at`; its bar's first edge.
    pub fn begin(book: Book, recipe: u32, name: &str, count: u32, t: f32, at: Vec3) -> (Self, Act) {
        let label = bar_label(name);
        let edge = start_edge(&label);
        (
            Making { book, recipe, label, left: count.max(1), started: t, at },
            Act::Bar(edge),
        )
    }
}

/// One frame: `at` where the player stands, `can` whether the bags still make one.
pub fn step(m: &mut Option<Making>, t: f32, at: Vec3, can: bool) -> Vec<Act> {
    let Some(k) = m.as_mut() else { return Vec::new() };
    let drift = Vec3::new(at.x - k.at.x, 0.0, at.z - k.at.z).length();
    // Bevy is y-up; the ground plane is x/z.
    if drift > MOVE_YARDS {
        *m = None;
        return vec![Act::Bar(Edge::Interrupted)];
    }
    if t - k.started < MAKE_SECS {
        return Vec::new();
    }
    if !can {
        *m = None;
        return vec![Act::Bar(Edge::Failed)];
    }
    let mut out = vec![Act::Make { book: k.book, recipe: k.recipe }, Act::Bar(Edge::Stop)];
    k.left -= 1;
    if k.left > 0 {
        k.started = t;
        out.push(Act::Bar(start_edge(&k.label)));
    } else {
        *m = None;
    }
    out
}

/// Stop it (leaving the world): the bar interrupted if one was filling.
pub fn cancel(m: &mut Option<Making>) -> Vec<Act> {
    if m.take().is_some() { vec![Act::Bar(Edge::Interrupted)] } else { Vec::new() }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn makes(acts: &[Act]) -> usize {
        acts.iter().filter(|a| matches!(a, Act::Make { .. })).count()
    }

    #[test]
    fn create_all_casts_one_item_at_a_time() {
        let (k, first) = Making::begin(Book::Construction, 7, "Wood Wall [3]", 2, 10.0, Vec3::ZERO);
        assert_eq!(first, Act::Bar(start_edge("Wood Wall")), "the bar names the recipe, not the kits held");
        let mut m = Some(k);
        assert!(step(&mut m, 12.0, Vec3::ZERO, true).is_empty(), "still casting: nothing sent");
        let a = step(&mut m, 13.0, Vec3::ZERO, true);
        assert_eq!(a[0], Act::Make { book: Book::Construction, recipe: 7 });
        assert_eq!(a[1], Act::Bar(Edge::Stop));
        assert!(matches!(a[2], Act::Bar(Edge::Start { .. })), "the next item's cast");
        assert_eq!(m.as_ref().map(|k| k.left), Some(1));
        let a = step(&mut m, 16.0, Vec3::ZERO, true);
        assert_eq!((makes(&a), a.len()), (1, 2));
        assert!(m.is_none(), "done");
    }

    #[test]
    fn walking_off_interrupts_and_running_dry_fails() {
        let (k, _) = Making::begin(Book::Minstrelsy, 2, "Lute", 5, 0.0, Vec3::ZERO);
        let mut m = Some(k.clone());
        // Turning on the spot (a little head sway) is fine; a step away is not.
        assert!(step(&mut m, 1.0, Vec3::new(0.2, 1.0, 0.1), true).is_empty());
        assert_eq!(step(&mut m, 1.5, Vec3::new(2.0, 0.0, 0.0), true), vec![Act::Bar(Edge::Interrupted)]);
        assert!(m.is_none());
        let mut m = Some(k);
        assert_eq!(step(&mut m, 3.0, Vec3::ZERO, false), vec![Act::Bar(Edge::Failed)], "no reagents: nothing sent");
        assert!(m.is_none());
        assert!(cancel(&mut m).is_empty());
    }

    #[test]
    fn the_label_drops_the_kits_held_count() {
        assert_eq!(bar_label("Lute"), "Lute");
        assert_eq!(bar_label("Wall · Stone [12]"), "Wall · Stone");
    }
}
