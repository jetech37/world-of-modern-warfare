//! World of Modern Warfare's area-loot window on the stock LootFrame (the layer's
//! LootFrameAdapters.xml): a window with sections lays out by corpse, and one without is the stock
//! window. Needs client data.

use benilla_ui::script::{LootLine, LootRow, LootSections, LootState, ScriptValue, UiScript};

use super::test_ui::load_ui as load_xml;

fn item_row(i: usize) -> Option<LootRow> {
    Some(LootRow {
        item_id: 0,
        name: Some(format!("Item {i}")),
        texture: Some(format!("Interface\\Icons\\Item_{i}")),
        quantity: 1,
        quality: Some(1),
        is_coin: false,
        link: None,
        random_property_id: 0,
    })
}

fn loot_of(rows: Vec<Option<LootRow>>) -> LootState {
    LootState {
        source_unit: false,
        fishing: false,
        master_candidates: Vec::new(),
        rows,
    }
}

/// The stock loot window and benilla's layer, LootFrameAdapters.xml among it.
fn loot_window_with_layer() -> UiScript {
    let mut s = UiScript::new().unwrap();
    s.set_screen_size(1024.0, 768.0);
    for f in super::test_ui::production_order(&[
        super::test_ui::LOOT_UI,
        &[
            "Interface\\FrameXML\\LootFrame.xml",
            "LootFrameAdapters.xml",
        ],
    ]) {
        load_xml(&s, f);
    }
    s
}

fn visible(s: &UiScript, names: &[&str]) -> Vec<bool> {
    names
        .iter()
        .map(|n| {
            s.eval::<bool>(&format!(
                "return {n} ~= nil and {n}:IsVisible() and true or false"
            ))
            .unwrap()
        })
        .collect()
}

/// A stock window (no source map) runs the stock code under the layer: the 256-tall window, three
/// rows and the pager for five items, no section header and no "+N".
#[test]
fn a_stock_loot_window_is_unchanged_by_the_area_loot_layer() {
    benilla_formats::wow_data_or_skip!();
    let mut s = loot_window_with_layer();
    s.set_loot(Some(loot_of((0..5).map(item_row).collect())));
    s.fire_event("LOOT_OPENED", vec![]);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    assert_eq!(
        visible(
            &s,
            &[
                "LootButton1",
                "LootButton2",
                "LootButton3",
                "LootButton4",
                "LootFrameDownButton"
            ]
        ),
        vec![true, true, true, false, true]
    );
    assert_eq!(
        s.eval::<f64>("return LootFrame:GetHeight()").unwrap(),
        256.0
    );
    assert_eq!(
        visible(
            &s,
            &[
                "BenillaLootButton1",
                "BenillaLootHeader1",
                "BenillaLootFrameOthers"
            ]
        ),
        vec![false, false, false]
    );
}

/// An area window: sections under their corpses' names, the coin on top, a taller window with
/// eight rows a page, "+N" on the portrait, each row with its own slot; then a stock window again
/// is the stock window.
#[test]
fn an_area_loot_window_lays_out_its_sections() {
    benilla_formats::wow_data_or_skip!();
    let mut s = loot_window_with_layer();
    // A coin row and nine items: the opened corpse's two, seven from another.
    let mut rows = vec![Some(LootRow {
        item_id: 0,
        name: Some("3 Silver".into()),
        texture: Some("Interface\\Icons\\INV_Misc_Coin_03".into()),
        quantity: 0,
        quality: Some(0),
        is_coin: true,
        link: None,
        random_property_id: 0,
    })];
    rows.extend((0..9).map(item_row));
    let mut page1 = vec![
        LootLine::Row(1),
        LootLine::Header("Kobold Vermin".into()),
        LootLine::Row(2),
        LootLine::Row(3),
        LootLine::Header("Kobold Worker".into()),
    ];
    page1.extend((4..=8).map(LootLine::Row));
    let page2 = vec![
        LootLine::Header("Kobold Worker".into()),
        LootLine::Row(9),
        LootLine::Row(10),
    ];
    s.set_loot(Some(loot_of(rows.clone())));
    s.set_loot_sections(Some(LootSections {
        others: 2,
        pages: vec![page1, page2],
        row_sources: Vec::new(),
    }));
    s.fire_event("LOOT_OPENED", vec![]);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());

    // The stock rows stay hidden; eight of ours show, the coin first.
    assert_eq!(
        visible(
            &s,
            &["LootButton1", "LootButton2", "LootButton3", "LootButton4"]
        ),
        vec![false; 4]
    );
    let ours: Vec<String> = (1..=9).map(|i| format!("BenillaLootButton{i}")).collect();
    let ours: Vec<&str> = ours.iter().map(String::as_str).collect();
    assert_eq!(
        visible(&s, &ours),
        vec![true, true, true, true, true, true, true, true, false]
    );
    assert_eq!(
        s.eval::<String>("return BenillaLootButton1Text:GetText()")
            .unwrap(),
        "3 Silver"
    );
    assert_eq!(
        s.eval::<(String, String)>(
            "return BenillaLootHeader1:GetText(), BenillaLootHeader2:GetText()"
        )
        .unwrap(),
        ("Kobold Vermin".into(), "Kobold Worker".into())
    );
    assert_eq!(
        s.eval::<i64>("return BenillaLootButton4.slot").unwrap(),
        4,
        "the button carries its own row"
    );
    assert_eq!(
        s.eval::<String>("return BenillaLootFrameOthers:GetText()")
            .unwrap(),
        "+2"
    );
    // Taller than the stock 256; the first row at the stock 80 below the top, the last clear of
    // the pager.
    let height = s.eval::<f64>("return LootFrame:GetHeight()").unwrap();
    assert!(height > 256.0, "an area window grows: {height}");
    s.resolve();
    let (top, first, last, bottom): (f64, f64, f64, f64) = s
        .eval(
            "local k = BenillaLootButton1:GetEffectiveScale() / LootFrame:GetEffectiveScale() \
             return LootFrame:GetTop(), BenillaLootButton1:GetTop() * k, \
                    BenillaLootButton8:GetBottom() * k, LootFrame:GetBottom()",
        )
        .unwrap();
    assert!(
        (top - first - 80.0).abs() < 1.0,
        "the first row 80 below the top: {top} {first}"
    );
    assert!(
        last > bottom + 40.0,
        "the last row clears the pager: {last} {bottom}"
    );
    assert_eq!(
        visible(&s, &["LootFrameUpButton", "LootFrameDownButton"]),
        vec![false, true]
    );

    // Page two repeats the carried section's header.
    s.run("LootFrame_PageDown()").unwrap();
    assert_eq!(
        visible(
            &s,
            &[
                "BenillaLootButton1",
                "BenillaLootButton2",
                "BenillaLootButton3"
            ]
        ),
        vec![true, true, false]
    );
    assert_eq!(
        s.eval::<String>("return BenillaLootHeader1:GetText()")
            .unwrap(),
        "Kobold Worker"
    );
    assert_eq!(
        visible(
            &s,
            &[
                "BenillaLootHeader2",
                "LootFrameUpButton",
                "LootFrameDownButton"
            ]
        ),
        vec![false, true, false]
    );

    // Both rows of page two go: a gap in place, then the window turns back to page one.
    let mut taken = rows.clone();
    taken[8] = None;
    s.set_loot(Some(loot_of(taken.clone())));
    s.fire_event("LOOT_SLOT_CLEARED", vec![ScriptValue::Int(9)]);
    assert_eq!(s.eval::<i64>("return LootFrame.page").unwrap(), 2);
    assert_eq!(
        visible(&s, &["BenillaLootButton1", "BenillaLootButton2"]),
        vec![false, true]
    );
    taken[9] = None;
    s.set_loot(Some(loot_of(taken)));
    s.fire_event("LOOT_SLOT_CLEARED", vec![ScriptValue::Int(10)]);
    assert_eq!(
        s.eval::<i64>("return LootFrame.page").unwrap(),
        1,
        "an emptied page turns to the one with loot left"
    );
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());

    // Closed, then a stock window: the stock shape and rows again.
    s.set_loot(None);
    s.set_loot_sections(None);
    s.fire_event("LOOT_CLOSED", vec![]);
    s.set_loot(Some(loot_of((0..2).map(item_row).collect())));
    s.fire_event("LOOT_OPENED", vec![]);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    assert_eq!(
        s.eval::<f64>("return LootFrame:GetHeight()").unwrap(),
        256.0
    );
    assert_eq!(
        visible(
            &s,
            &[
                "LootButton1",
                "LootButton2",
                "LootButton3",
                "BenillaLootButton1",
                "BenillaLootHeader1",
                "BenillaLootFrameOthers"
            ]
        ),
        vec![true, true, false, false, false, false]
    );
}
