//! One switch that keeps any of our layer's windows open in a headless capture, with no player
//! and no server (client/docs/CAPTURE.md):
//!
//! `MW2_CAPTURE_KEEP_WINDOWS=1 MW2_CAPTURE_WINDOW=bounty` (or a comma list), or the names straight
//! in `MW2_CAPTURE_KEEP_WINDOWS=bounty,kits`. `all` keeps every one. Each window asks
//! [`in_world`] where it would ask `FpsState::in_world` (so it builds and stays) and [`keeps`]
//! to open itself. The names are [`WINDOWS`].

use std::sync::OnceLock;

/// Every window the switch knows, by the name it is asked for.
pub const WINDOWS: &[&str] = &[
    "duel",
    "kits",
    "companions",
    "bounty",
    "minstrelsy",
    "augments",
    "professions",
    "trainer",
    "build",
    "achievements",
    "guide",
    "map",
];

fn parse(keep: Option<&str>, window: Option<&str>) -> Vec<String> {
    let list = match keep.map(str::trim) {
        None | Some("") | Some("0") => return Vec::new(),
        Some("1") => window.unwrap_or(""),
        Some(names) => names,
    };
    let names: Vec<String> = list
        .split(',')
        .map(|n| n.trim().to_ascii_lowercase())
        .filter(|n| !n.is_empty())
        .collect();
    if names.iter().any(|n| n == "all") {
        return WINDOWS.iter().map(|n| n.to_string()).collect();
    }
    names
}

fn names() -> &'static [String] {
    static NAMES: OnceLock<Vec<String>> = OnceLock::new();
    NAMES.get_or_init(|| {
        let keep = std::env::var("MW2_CAPTURE_KEEP_WINDOWS").ok();
        let window = std::env::var("MW2_CAPTURE_WINDOW").ok();
        let names = parse(keep.as_deref(), window.as_deref());
        for n in &names {
            if !WINDOWS.contains(&n.as_str()) {
                bevy::log::warn!("MW2_CAPTURE_KEEP_WINDOWS: no window named {n} (known: {WINDOWS:?})");
            }
        }
        names
    })
}

/// Whether the capture switch keeps this window open.
pub fn keeps(window: &str) -> bool {
    names().iter().any(|n| n == window)
}

/// `in_world`, or true for a window the capture switch keeps.
pub fn in_world(in_world: bool, window: &str) -> bool {
    in_world || keeps(window)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_switch_reads_one_name_a_list_or_all() {
        assert!(parse(None, Some("bounty")).is_empty());
        assert!(parse(Some("0"), Some("bounty")).is_empty());
        assert_eq!(parse(Some("1"), Some("bounty")), vec!["bounty"]);
        assert_eq!(parse(Some("1"), Some("Kits, duel")), vec!["kits", "duel"]);
        assert_eq!(parse(Some("guide"), None), vec!["guide"]);
        assert_eq!(parse(Some("all"), None).len(), WINDOWS.len());
    }
}
