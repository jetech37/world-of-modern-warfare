//! World of Modern Warfare's area loot: the server gathers several nearby corpses into one loot
//! window and sends, on the fusion world channel (op 36, [`LootSources`]), which corpse each row
//! came from. This lays such a window out in sections, one per corpse under its creature's name,
//! the opened corpse first and the rest in the server's (nearest-first) order, and pages it at
//! [`ROWS_PER_PAGE`] rows. A window the server sent no map for is the stock window.

use benilla_protocol::fusion::{LootSource, LootSources};
use benilla_ui::script::{LootLine, LootSections};

/// The rows an area window shows a page (the coin row included); the stock window shows four.
pub(crate) const ROWS_PER_PAGE: usize = 8;

/// The source map for one loot window, merged across the server's updates: a corpse that leaves
/// the window keeps its rows' places (they are taken or gone), and only the "+N" follows the
/// latest map.
#[derive(Clone, Debug, Default, PartialEq)]
pub(crate) struct AreaSources {
    pub(crate) loot_guid: u64,
    /// Every corpse a map has named, in the order first seen: the opened corpse first.
    order: Vec<LootSource>,
    /// The latest map's corpses beside the opened one.
    others: u32,
    /// Each wire loot slot's corpse guid.
    rows: Vec<(u8, u64)>,
}

impl AreaSources {
    pub(crate) fn new(m: &LootSources) -> Self {
        let mut a = Self {
            loot_guid: m.loot_guid,
            ..Self::default()
        };
        a.merge(m);
        a
    }

    /// Takes a later map for the same window.
    pub(crate) fn merge(&mut self, m: &LootSources) {
        for s in &m.sources {
            if !self.order.iter().any(|o| o.guid == s.guid) {
                self.order.push(*s);
            }
        }
        self.others = m.sources.iter().filter(|s| s.guid != m.loot_guid).count() as u32;
        for &(slot, index) in &m.rows {
            let Some(source) = m.sources.get(usize::from(index)) else {
                continue;
            };
            match self.rows.iter_mut().find(|(s, _)| *s == slot) {
                Some(row) => row.1 = source.guid,
                None => self.rows.push((slot, source.guid)),
            }
        }
    }

    /// The corpses whose names a layout reads: those with a row among `wire_slots`.
    pub(crate) fn sources_of<'a>(
        &'a self,
        wire_slots: &'a [u8],
    ) -> impl Iterator<Item = LootSource> + 'a {
        self.order.iter().copied().filter(move |s| {
            self.rows
                .iter()
                .any(|&(slot, guid)| guid == s.guid && wire_slots.contains(&slot))
        })
    }

    /// The corpse a wire loot slot came from.
    pub(crate) fn source_guid_of(&self, wire_slot: u8) -> Option<u64> {
        Some(self.rows.iter().find(|(s, _)| *s == wire_slot)?.1)
    }

    fn source_of(&self, wire_slot: u8) -> Option<LootSource> {
        let guid = self.rows.iter().find(|(s, _)| *s == wire_slot)?.1;
        self.order.iter().copied().find(|s| s.guid == guid)
    }
}

/// The window's sections: `wire_slots` are its item rows in display order, after the coin row when
/// `coin_row` (display row 1). Each corpse with a row gets a section, in [`AreaSources`]' order; a
/// row the map does not name goes in a last section with no name. A page holds
/// [`ROWS_PER_PAGE`] rows, the coin row first on page one with no header; a section carried onto
/// the next page repeats its header there. `name_of` names a corpse (`None`: not known yet).
pub(crate) fn layout(
    coin_row: bool,
    wire_slots: &[u8],
    area: &AreaSources,
    name_of: impl Fn(LootSource) -> Option<String>,
) -> LootSections {
    let first_item_row = 1 + u32::from(coin_row);
    // Sections in the map's corpse order, then the unnamed one.
    let mut groups: Vec<(Option<u64>, Vec<u32>)> = area
        .order
        .iter()
        .map(|s| (Some(s.guid), Vec::new()))
        .collect();
    groups.push((None, Vec::new()));
    let mut row_sources: Vec<Option<String>> = vec![None; wire_slots.len() + coin_row as usize];
    for (i, &slot) in wire_slots.iter().enumerate() {
        let row = first_item_row + i as u32;
        let source = area.source_of(slot);
        let key = source.map(|s| s.guid);
        if let Some(group) = groups.iter_mut().find(|(g, _)| *g == key) {
            group.1.push(row);
        }
        row_sources[row as usize - 1] = source.and_then(&name_of);
    }

    let mut pages: Vec<Vec<LootLine>> = vec![Vec::new()];
    let mut on_page = 0usize;
    if coin_row {
        pages[0].push(LootLine::Row(1));
        on_page = 1;
    }
    for (key, rows) in groups.iter().filter(|(_, rows)| !rows.is_empty()) {
        let header = key
            .and_then(|guid| area.order.iter().copied().find(|s| s.guid == guid))
            .and_then(&name_of)
            .unwrap_or_default();
        let mut headed = false;
        for &row in rows {
            if on_page == ROWS_PER_PAGE {
                pages.push(Vec::new());
                on_page = 0;
                headed = false;
            }
            let page = pages.last_mut().expect("a page");
            if !headed {
                page.push(LootLine::Header(header.clone()));
                headed = true;
            }
            page.push(LootLine::Row(row));
            on_page += 1;
        }
    }
    LootSections {
        others: area.others,
        pages,
        row_sources,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const OPENED: u64 = 0xF130_0000_0100_0001;
    const NEAR: u64 = 0xF130_0000_0200_0002;
    const FAR: u64 = 0xF130_0000_0300_0003;
    const COIN_ONLY: u64 = 0xF130_0000_0400_0004;

    fn source(guid: u64) -> LootSource {
        LootSource {
            guid,
            entry: (guid >> 24) as u32 & 0xff_ffff,
        }
    }

    fn name(s: LootSource) -> Option<String> {
        match s.guid {
            OPENED => Some("Kobold Vermin".into()),
            NEAR => Some("Kobold Worker".into()),
            FAR => Some("Kobold Laborer".into()),
            _ => None,
        }
    }

    /// The server's map: `rows` as (wire slot, corpse guid).
    fn map(sources: &[u64], rows: &[(u8, u64)]) -> LootSources {
        LootSources {
            loot_guid: OPENED,
            sources: sources.iter().map(|&g| source(g)).collect(),
            rows: rows
                .iter()
                .map(|&(slot, g)| (slot, sources.iter().position(|&s| s == g).unwrap() as u8))
                .collect(),
        }
    }

    fn texts(s: &LootSections) -> Vec<Vec<String>> {
        s.pages
            .iter()
            .map(|p| {
                p.iter()
                    .map(|l| match l {
                        LootLine::Header(n) => format!("[{n}]"),
                        LootLine::Row(r) => r.to_string(),
                    })
                    .collect()
            })
            .collect()
    }

    /// One section per corpse in the map's order, the opened corpse first, each row under its
    /// corpse even when the window interleaves them; the coin row first, under no header.
    #[test]
    fn sections_follow_the_corpse_order_with_the_coin_on_top() {
        let area = AreaSources::new(&map(
            &[OPENED, NEAR, FAR],
            &[(0, OPENED), (1, NEAR), (2, OPENED), (3, FAR)],
        ));
        let s = layout(true, &[0, 1, 2, 3], &area, name);
        assert_eq!(
            texts(&s),
            vec![vec![
                "1",
                "[Kobold Vermin]",
                "2",
                "4",
                "[Kobold Worker]",
                "3",
                "[Kobold Laborer]",
                "5"
            ]]
        );
        assert_eq!(s.others, 2);
        assert_eq!(
            s.row_sources,
            vec![
                None,
                Some("Kobold Vermin".into()),
                Some("Kobold Worker".into()),
                Some("Kobold Vermin".into()),
                Some("Kobold Laborer".into()),
            ]
        );
    }

    /// A corpse with no row in the window (it added only coin, or its rows did not fit) has no
    /// section, though it counts in "+N"; map entries for slots the window lacks are ignored.
    #[test]
    fn a_corpse_without_rows_gets_no_section() {
        let area = AreaSources::new(&map(
            &[OPENED, COIN_ONLY, NEAR],
            &[(0, OPENED), (1, NEAR), (9, NEAR)],
        ));
        let s = layout(false, &[0, 1], &area, name);
        assert_eq!(
            texts(&s),
            vec![vec!["[Kobold Vermin]", "1", "[Kobold Worker]", "2"]]
        );
        assert_eq!(s.others, 2);
        let named: Vec<u64> = area.sources_of(&[0, 1]).map(|s| s.guid).collect();
        assert_eq!(named, vec![OPENED, NEAR]);
    }

    /// Eight rows fit a page; the ninth starts page two, and a section that runs over repeats its
    /// header there.
    #[test]
    fn pages_hold_eight_rows_and_repeat_a_carried_header() {
        let mut rows: Vec<(u8, u64)> = (0..5).map(|s| (s, OPENED)).collect();
        rows.extend((5..9).map(|s| (s, NEAR)));
        let area = AreaSources::new(&map(&[OPENED, NEAR], &rows));
        let slots: Vec<u8> = (0..8).collect();
        let eight = layout(false, &slots, &area, name);
        assert_eq!(eight.pages.len(), 1, "eight rows: one page");

        let slots: Vec<u8> = (0..9).collect();
        let nine = layout(false, &slots, &area, name);
        assert_eq!(
            texts(&nine),
            vec![
                vec![
                    "[Kobold Vermin]",
                    "1",
                    "2",
                    "3",
                    "4",
                    "5",
                    "[Kobold Worker]",
                    "6",
                    "7",
                    "8"
                ],
                vec!["[Kobold Worker]", "9"],
            ]
        );

        // With the coin row, the eighth item row is the ninth row: page two.
        let slots: Vec<u8> = (0..8).collect();
        let coin = layout(true, &slots, &area, name);
        assert_eq!(coin.pages.len(), 2);
        assert_eq!(texts(&coin)[1], vec!["[Kobold Worker]", "9"]);

        // The server's 16 rows and the coin: three pages.
        let mut rows: Vec<(u8, u64)> = (0..8).map(|s| (s, OPENED)).collect();
        rows.extend((8..16).map(|s| (s, NEAR)));
        let area = AreaSources::new(&map(&[OPENED, NEAR], &rows));
        let slots: Vec<u8> = (0..16).collect();
        assert_eq!(layout(true, &slots, &area, name).pages.len(), 3);
    }

    /// A row the map does not name goes last, under an empty header; a corpse with no name yet
    /// reads empty too.
    #[test]
    fn unmapped_rows_and_unnamed_corpses_read_empty() {
        let area = AreaSources::new(&map(&[OPENED, COIN_ONLY], &[(1, COIN_ONLY)]));
        let s = layout(false, &[0, 1], &area, name);
        assert_eq!(texts(&s), vec![vec!["[]", "2", "[]", "1"]]);
        assert_eq!(s.row_sources, vec![None, None]);
    }

    /// A later map (a corpse left the window) changes "+N" only: the rows keep their sections.
    #[test]
    fn a_later_map_keeps_every_rows_section() {
        let mut area = AreaSources::new(&map(
            &[OPENED, NEAR, FAR],
            &[(0, OPENED), (1, NEAR), (2, FAR)],
        ));
        area.merge(&map(&[OPENED, FAR], &[(0, OPENED), (2, FAR)]));
        let s = layout(false, &[0, 1, 2], &area, name);
        assert_eq!(s.others, 1);
        assert_eq!(
            texts(&s),
            vec![vec![
                "[Kobold Vermin]",
                "1",
                "[Kobold Worker]",
                "2",
                "[Kobold Laborer]",
                "3"
            ]]
        );
    }
}
