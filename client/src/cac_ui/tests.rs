use super::*;
use crate::achievements::{Unlock, UnlockKind, Unlocks};
use crate::catalog::perk_id;

fn catalog() -> Catalog {
    Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
}

fn locked_out() -> Unlocks {
    Unlocks {
        everything: false,
        held: Default::default(),
    }
}

fn grant(u: &mut Unlocks, kind: UnlockKind, id: u32) {
    u.held.insert(Unlock {
        kind,
        id,
        camo: 0,
        attachment: String::new(),
    });
}

fn part_index(part: Part) -> usize {
    PARTS.iter().position(|p| *p == part).unwrap()
}

#[test]
fn talent_icons_are_mpq_paths() {
    assert_eq!(
        spell_icon_path("Interface\\Icons\\Spell_Fire_Fireball02"),
        "mpq://Interface/Icons/Spell_Fire_Fireball02.blp"
    );
}

#[test]
fn a_kit_picks_its_equipment() {
    let c = catalog();
    let mut kit = ClassLoadout::default();
    let opts = parts::options(&c, &kit, Part::Lethal);
    parts::assign(&c, &mut kit, Part::Lethal, &opts[1], false);
    assert_eq!(equipment_name(&c, kit.lethal), "Thorium Grenade");
    let opts = parts::options(&c, &kit, Part::Tactical);
    parts::assign(&c, &mut kit, Part::Tactical, &opts[1], false);
    assert_eq!(equipment_name(&c, kit.tactical), "Arcane Flash Bomb");
    parts::assign(&c, &mut kit, Part::Lethal, &Choice::Gun(0), false);
    assert_eq!(kit.lethal, 0); // back to none
}

#[test]
fn the_arrows_walk_the_parts_and_the_sub_menu() {
    use crate::input::Nav;
    let c = catalog();
    assert_eq!(step_part(0, 1), 1);
    assert_eq!(step_part(0, -1), PARTS.len() - 1); // wraps
    assert_eq!(step_part(PARTS.len() - 1, 1), 0);
    let mut ui = CacUi {
        page: LOADOUT,
        ..default()
    };
    ui.focus_part(&c, 0);
    assert_eq!(nav_action(&ui, &c, Nav::Down), Some(KeyAction::Part(1)));
    assert_eq!(nav_action(&ui, &c, Nav::Left), Some(KeyAction::Cursor(-1)));
    // On the current pick, Enter saves and equips; moved off it, Enter assigns.
    assert_eq!(nav_action(&ui, &c, Nav::Enter), Some(KeyAction::Equip));
    ui.step_cursor(&c, 1);
    assert_eq!(ui.cursor, 1);
    assert_eq!(nav_action(&ui, &c, Nav::Enter), Some(KeyAction::Assign));
    // The highlight wraps through the options.
    let n = parts::options(&c, &ui.draft, Part::Primary).len();
    ui.step_cursor(&c, -2);
    assert_eq!(ui.cursor, n - 1);
    // Off the loadout page, only Enter, which saves.
    ui.page = TALENTS;
    assert_eq!(nav_action(&ui, &c, Nav::Left), None);
    assert_eq!(nav_action(&ui, &c, Nav::Enter), Some(KeyAction::Equip));
}

#[test]
fn assigning_puts_the_highlight_in_the_kit_unless_it_is_locked() {
    let c = catalog();
    let mut unlocks = locked_out();
    let mut ui = CacUi::default();
    ui.focus_part(&c, part_index(Part::Primary));
    let m4 = c.gun_named("m4_mp").unwrap().id;
    ui.cursor = parts::options(&c, &ui.draft, Part::Primary)
        .iter()
        .position(|o| *o == Choice::Gun(m4))
        .unwrap();
    assert!(!assign_highlighted(&mut ui, &c, &unlocks)); // locked
    assert_eq!(ui.draft.primary, 0);
    grant(&mut unlocks, UnlockKind::Gun, u32::from(m4));
    assert!(assign_highlighted(&mut ui, &c, &unlocks));
    assert_eq!(ui.draft.primary, m4);
    assert_eq!(ui.highlighted(&c), Choice::Gun(m4)); // the highlight stays on the pick
    // A new gun starts bare.
    ui.draft.primary_att1 = "acog".into();
    ui.draft.primary_camo = 1;
    ui.step_cursor(&c, 1);
    unlocks.everything = true;
    assert!(assign_highlighted(&mut ui, &c, &unlocks));
    assert!(ui.draft.primary_att1.is_empty() && ui.draft.primary_camo == 0);
}

#[test]
fn a_pro_is_chosen_only_once_earned() {
    let c = catalog();
    let mut unlocks = locked_out();
    let bling = perk_id::BLING;
    grant(&mut unlocks, UnlockKind::Perk, u32::from(bling));
    let mut ui = CacUi::default();
    ui.focus_part(&c, part_index(Part::Perk(1)));
    ui.cursor = parts::options(&c, &ui.draft, Part::Perk(1))
        .iter()
        .position(|o| *o == Choice::Perk(bling))
        .unwrap();
    ui.want_pro = true;
    let why = assign_refusal(
        &c,
        &unlocks,
        &ui.draft,
        Part::Perk(1),
        &Choice::Perk(bling),
        true,
    );
    assert!(why.unwrap().contains("Pro"));
    assert!(!assign_highlighted(&mut ui, &c, &unlocks));
    ui.want_pro = false;
    assert!(assign_highlighted(&mut ui, &c, &unlocks));
    assert_eq!(ui.draft.perks[0], bling);
    assert_eq!(ui.draft.perk_pro, 0);
    // Earned: the Pro goes in with it.
    grant(&mut unlocks, UnlockKind::PerkPro, u32::from(bling));
    ui.want_pro = true;
    assert!(assign_highlighted(&mut ui, &c, &unlocks));
    assert_eq!(ui.draft.perk_pro, 0b001);
    assert!(nav_action(&ui, &c, crate::input::Nav::Enter) == Some(KeyAction::Equip));
}

#[test]
fn the_ui_offers_a_perk_in_more_slots_only_where_it_stacks() {
    let c = catalog();
    let unlocks = Unlocks::default();
    let mut kit = ClassLoadout::default();
    kit.perks[0] = perk_id::STOPPING_POWER;
    let sp = Choice::Perk(perk_id::STOPPING_POWER);
    assert!(assign_refusal(&c, &unlocks, &kit, Part::Perk(2), &sp, false).is_some());
    kit.perks[0] = perk_id::BLING;
    kit.perks[1] = perk_id::BLING;
    let rails = Choice::Perk(perk_id::BLING);
    assert!(assign_refusal(&c, &unlocks, &kit, Part::Perk(3), &rails, false).is_none());
    // Rails x3: four primary attachments; the secondary's second is the Rails' Pro's.
    kit.perks[2] = perk_id::BLING;
    assert_eq!(crate::cac::attachment_room(&kit), (4, 1));
    kit.perk_pro = 0b001;
    assert_eq!(crate::cac::attachment_room(&kit), (4, 2));
    kit.perk_pro = 0;
    // The catalog's own choices agree: a stacking perk is offered again, one that doesn't isn't.
    let taken = [perk_id::BLING, perk_id::STOPPING_POWER];
    let choices = c.perk_choices(3, &taken);
    assert!(choices.contains(&perk_id::BLING));
    assert!(!choices.contains(&perk_id::STOPPING_POWER));
    assert!(
        !c.perk_choices(3, &[perk_id::BLING; 3])
            .contains(&perk_id::BLING)
    );
}

/// The window's systems, in an app laid out as the game's.
fn window_app() -> App {
    let mut app = crate::wow_ui::layout_app();
    app.add_plugins(crate::wow_ui::WowUiPlugin);
    app.init_asset::<Font>();
    app.insert_resource(FpsState {
        active: true,
        in_world: true,
        ..default()
    });
    app.init_resource::<Catalog>()
        .init_resource::<ClassPage>()
        .init_resource::<Mw2Input>()
        .init_resource::<Classes>()
        .init_resource::<crate::achievements::Unlocks>()
        .init_resource::<crate::achievements::Achievements>()
        .init_resource::<crate::mw2::Mw2Assets>()
        .init_resource::<crate::hands::SpellTable>()
        .init_resource::<crate::companion::CompanionState>()
        .insert_resource(crate::streaks::Streak::default())
        .init_resource::<ButtonInput<MouseButton>>()
        .insert_resource(crate::keybinds::Keymap::defaults())
        .add_message::<benilla_app::fusion::FusionChannelReceived>()
        .add_message::<SendFusionChannel>()
        .add_message::<SendFusionLoadout>()
        .add_message::<OpenPanel>()
        .add_message::<ShowKitPage>()
        .add_plugins(CacUiPlugin);
    app.world_mut().write_message(OpenPanel {
        panel: Panel::Kits,
        access: Access::Npc,
    });
    app.update();
    app.update();
    assert!(app.world().resource::<CacUi>().open);
    app
}

/// The window's laid-out height, logical pixels.
fn window_height(app: &mut App) -> f32 {
    let w = app.world_mut();
    let mut q = w.query_filtered::<&ComputedNode, With<CacRoot>>();
    let n = q.single(w).unwrap();
    n.size.y * n.inverse_scale_factor
}

/// Press a button for one frame, as the probe's `click:` does.
fn click(app: &mut App, want: Action) {
    #[derive(Resource)]
    struct Click(Option<Entity>);
    fn probe_click(mut click: ResMut<Click>, mut q: Query<&mut Interaction>) {
        if let Some(e) = click.0.take()
            && let Ok(mut i) = q.get_mut(e)
        {
            *i = Interaction::Pressed;
        }
    }
    let e = {
        let w = app.world_mut();
        let mut q = w.query::<(Entity, &Action)>();
        q.iter(w).find(|(_, a)| **a == want).map(|(e, _)| e)
    }
    .unwrap_or_else(|| panic!("no {want:?} button"));
    if !app.world().contains_resource::<Click>() {
        app.add_systems(PreUpdate, probe_click.after(bevy::ui::UiSystems::Focus));
    }
    app.insert_resource(Click(Some(e)));
    app.update();
    // The press lasts a frame.
    if let Some(mut i) = app.world_mut().get_mut::<Interaction>(e) {
        *i = Interaction::None;
    }
    app.update();
}

#[test]
fn the_window_keeps_one_height_whatever_is_chosen() {
    let mut app = window_app();
    app.update();
    let base = window_height(&mut app);
    assert!(base > 300.0, "{base}");
    let c = catalog();
    // Every primary, with the most attachments it can take, and every part's sub-menu.
    let primaries = parts::options(&c, &ClassLoadout::default(), Part::Primary);
    for (k, gun) in primaries.iter().enumerate().step_by(4) {
        {
            let mut ui = app.world_mut().resource_mut::<CacUi>();
            ui.draft = ClassLoadout::default();
            ui.draft.perks = [perk_id::BLING; 3];
            let Choice::Gun(id) = gun else { unreachable!() };
            ui.draft.primary = *id;
            let atts = c
                .gun(*id)
                .map(|g| g.attachments.clone())
                .unwrap_or_default();
            ui.draft.primary_att1 = atts.first().cloned().unwrap_or_default();
            let part = k % PARTS.len();
            ui.focus_part(&c, part);
            ui.step_cursor(&c, k as i32);
        }
        app.update();
        app.update();
        let h = window_height(&mut app);
        assert!((h - base).abs() < 0.5, "gun {gun:?}: {h} against {base}");
    }
    // The pages and the dropdown's list don't change it either.
    for page in [TALENTS, abilities::PAGE, KILLSTREAKS, LOADOUT] {
        app.world_mut().resource_mut::<CacUi>().page = page;
        app.update();
        app.update();
        let h = window_height(&mut app);
        assert!((h - base).abs() < 0.5, "page {page}: {h} against {base}");
    }
    app.world_mut().resource_mut::<CacUi>().dropdown = true;
    app.update();
    app.update();
    assert!((window_height(&mut app) - base).abs() < 0.5);
}

#[test]
fn the_sub_menu_lists_the_parts_options_and_follows_the_arrows() {
    let mut app = window_app();
    let c = catalog();
    // The Perk 1 row's ">" arrow: its sub-menu, the highlight one along.
    let perk1 = part_index(Part::Perk(1)) as u8;
    click(&mut app, Action::Step(perk1, 1));
    let ui = app.world().resource::<CacUi>();
    assert_eq!(ui.focused_part(), Part::Perk(1));
    assert_eq!(ui.cursor, 1);
    // A cell for every option, the highlighted one lit.
    let n = parts::options(&c, &ui.draft, Part::Perk(1)).len();
    let w = app.world_mut();
    let mut q = w.query::<(&Action, &WowButton)>();
    let cells: Vec<(u16, bool)> = q
        .iter(w)
        .filter_map(|(a, b)| match a {
            Action::PickOption(i) => Some((*i, b.selected)),
            _ => None,
        })
        .collect();
    assert_eq!(cells.len(), n);
    assert_eq!(
        cells
            .iter()
            .filter(|(_, s)| *s)
            .map(|(i, _)| *i)
            .collect::<Vec<_>>(),
        [1]
    );
    // The keyboard: right moves the highlight, Enter assigns it.
    app.world_mut().resource_mut::<Mw2Input>().nav = Some(crate::input::Nav::Right);
    app.update();
    app.world_mut().resource_mut::<Mw2Input>().nav = Some(crate::input::Nav::Enter);
    app.update();
    app.world_mut().resource_mut::<Mw2Input>().nav = None;
    app.update();
    let ui = app.world().resource::<CacUi>();
    assert_eq!(ui.cursor, 2);
    let want = parts::options(&c, &ui.draft, Part::Perk(1))[2].clone();
    assert_eq!(Choice::Perk(ui.draft.perks[0]), want);
    // A click on an option highlights it; Assign puts it in.
    click(&mut app, Action::PickOption(3));
    assert_eq!(app.world().resource::<CacUi>().cursor, 3);
    click(&mut app, Action::Assign);
    let ui = app.world().resource::<CacUi>();
    let want = parts::options(&c, &ui.draft, Part::Perk(1))[3].clone();
    assert_eq!(Choice::Perk(ui.draft.perks[0]), want);
    // The details say what it is.
    let w = app.world_mut();
    let mut boxes = w.query_filtered::<&Children, With<DetailsBox>>();
    assert!(boxes.single(w).unwrap().len() >= 3);
}

#[test]
fn the_kits_dropdown_shows_new_copies_and_deletes_kits() {
    let mut app = window_app();
    let lists_shown = |app: &mut App| {
        let w = app.world_mut();
        let mut q = w.query_filtered::<&Node, With<DropList>>();
        q.single(w).unwrap().display != Display::None
    };
    assert!(!lists_shown(&mut app));
    click(&mut app, Action::DropDown);
    assert!(app.world().resource::<CacUi>().dropdown);
    assert!(lists_shown(&mut app));
    // Another kit.
    click(&mut app, Action::KitSlot(2));
    {
        let ui = app.world().resource::<CacUi>();
        assert_eq!(ui.slot, 2);
        assert_eq!(ui.draft.name, "Kit 3");
        assert!(!ui.dropdown);
    }
    assert!(!lists_shown(&mut app));
    // Copy: saved in the first free slot, under the old name and " copy".
    app.world_mut().resource_mut::<CacUi>().draft.name = "Rifle".into();
    click(&mut app, Action::DropDown);
    click(&mut app, Action::CopyKit);
    let sent: Vec<_> = app
        .world_mut()
        .resource_mut::<Messages<SendFusionLoadout>>()
        .drain()
        .collect();
    let req = &sent.last().unwrap().0;
    assert_eq!(req.equip_slot, benilla_app::fusion::NO_CLASS); // saved, not equipped
    assert_eq!(req.classes[0].0, 0);
    assert_eq!(req.classes[0].1.name, "Rifle copy");
    assert_eq!(app.world().resource::<CacUi>().slot, 0);
    // Delete asks first, in a popup: No keeps the kit.
    let popups = |app: &mut App| {
        let w = app.world_mut();
        let mut q = w.query_filtered::<Entity, With<DeletePopup>>();
        q.iter(w).count()
    };
    click(&mut app, Action::DropDown);
    click(&mut app, Action::DeleteKit);
    assert!(app.world().resource::<CacUi>().confirm_delete);
    assert_eq!(popups(&mut app), 1, "the popup asks");
    assert!(
        app.world_mut()
            .resource_mut::<Messages<SendFusionLoadout>>()
            .drain()
            .next()
            .is_none(),
        "nothing is deleted yet"
    );
    click(&mut app, Action::CancelDelete);
    assert!(!app.world().resource::<CacUi>().confirm_delete);
    assert_eq!(popups(&mut app), 0);
    // Esc answers No too, before it closes the window.
    click(&mut app, Action::DropDown);
    click(&mut app, Action::DeleteKit);
    app.world_mut().resource_mut::<Mw2Input>().escape = true;
    app.update();
    app.world_mut().resource_mut::<Mw2Input>().escape = false;
    app.update();
    assert!(app.world().resource::<CacUi>().open, "the window stays");
    assert_eq!(popups(&mut app), 0);
    // Yes: the slot saved bare under its own name.
    click(&mut app, Action::DropDown);
    click(&mut app, Action::DeleteKit);
    click(&mut app, Action::ConfirmDelete);
    let sent: Vec<_> = app
        .world_mut()
        .resource_mut::<Messages<SendFusionLoadout>>()
        .drain()
        .collect();
    let req = &sent.last().unwrap().0;
    assert_eq!(
        req.classes[0].1,
        ClassLoadout {
            name: "Kit 1".into(),
            ..Default::default()
        }
    );
    // Rename types into the box.
    click(&mut app, Action::DropDown);
    click(&mut app, Action::Rename);
    assert!(app.world().resource::<CacUi>().renaming);
    let ui = app.world().resource::<CacUi>();
    assert!(drop_box_text(ui, false).ends_with('_'));
}

#[test]
fn the_window_builds_in_the_stock_look() {
    use bevy::ecs::system::RunSystemOnce;
    let mut world = crate::wow_ui::test_world();
    world.insert_resource(CacUi {
        open: true,
        ..default()
    });
    world.run_system_once(build_window).unwrap();
    let mut roots = world.query_filtered::<&crate::wow_ui::WowWindow, With<CacRoot>>();
    let root = roots.single(&world).unwrap();
    assert!(root.movable);
    let mut actions = world.query::<&Action>();
    let all: Vec<Action> = actions.iter(&world).copied().collect();
    let count = |f: &dyn Fn(&Action) -> bool| all.iter().filter(|a| f(a)).count();
    // Every part: a row and two arrows; the kits dropdown: five kits and four actions.
    assert_eq!(count(&|a| matches!(a, Action::FocusPart(_))), PARTS.len());
    assert_eq!(count(&|a| matches!(a, Action::Step(..))), 2 * PARTS.len());
    assert_eq!(count(&|a| matches!(a, Action::KitSlot(_))), 5);
    for a in [
        Action::DropDown,
        Action::Rename,
        Action::NewKit,
        Action::CopyKit,
        Action::DeleteKit,
        Action::Assign,
        Action::Pro(false),
        Action::Pro(true),
        Action::Equip,
        Action::Save,
        Action::FieldEdit,
    ] {
        assert_eq!(count(&|b| *b == a), 1, "{a:?}");
    }
    assert_eq!(count(&|a| matches!(a, Action::Page(_))), 4);
    assert_eq!(count(&|a| matches!(a, Action::Talent(..))), 3 * TIERS);
    // Every control that is not plain says what it does on hover.
    let mut tipped = world.query_filtered::<&Action, With<WowTip>>();
    let tips: Vec<Action> = tipped.iter(&world).copied().collect();
    for a in [
        Action::DropDown,
        Action::Assign,
        Action::Pro(true),
        Action::Equip,
        Action::Save,
    ] {
        assert!(tips.contains(&a), "{a:?}");
    }
    // One talent tooltip, hidden, under the window's root; the list closed.
    let mut tips = world.query_filtered::<&Visibility, With<TipBox>>();
    assert_eq!(tips.iter(&world).collect::<Vec<_>>(), [&Visibility::Hidden]);
    let mut lists = world.query_filtered::<&Node, With<DropList>>();
    assert_eq!(lists.single(&world).unwrap().display, Display::None);
    // Four pages, the Abilities page's list among them.
    let mut pages = world.query::<&PagePanel>();
    let mut ids: Vec<u8> = pages.iter(&world).map(|p| p.0).collect();
    ids.sort();
    assert_eq!(ids, [0, 1, 2, 3]);
    let mut lists = world.query_filtered::<(), With<abilities::AbilityList>>();
    assert_eq!(lists.iter(&world).count(), 1);
}

/// The open window turns its page for a tab pressed for one frame (a probe's click) and for
/// a page asked for from the HUD (N, P, the stock toggles).
#[test]
fn a_tab_or_a_page_request_turns_the_open_window() {
    let mut app = window_app();
    let shown = |app: &mut App| {
        let w = app.world_mut();
        let mut q = w.query::<(&Node, &ComputedNode, &PagePanel)>();
        q.iter(w)
            .filter(|(n, c, _)| n.display != Display::None && c.size.x > 0.0)
            .map(|(_, _, p)| p.0)
            .collect::<Vec<u8>>()
    };
    assert_eq!(shown(&mut app), [LOADOUT]);
    click(&mut app, Action::Page(abilities::PAGE));
    assert_eq!(app.world().resource::<CacUi>().page, abilities::PAGE);
    assert_eq!(shown(&mut app), [abilities::PAGE]);
    // N's page from the stock toggle.
    app.world_mut().write_message(ShowKitPage(KitPage::Talents));
    app.update();
    app.update();
    assert!(app.world().resource::<CacUi>().open);
    assert_eq!(app.world().resource::<CacUi>().page, TALENTS);
    assert_eq!(shown(&mut app), [TALENTS]);
}

#[test]
fn n_and_p_open_their_pages_and_close_from_them() {
    let (t, a) = (TALENTS, abilities::PAGE);
    assert_eq!(show_page(false, LOADOUT, t), (true, t)); // closed: open on it
    assert_eq!(show_page(true, LOADOUT, t), (true, t)); // on another page: turn to it
    assert_eq!(show_page(true, t, t), (false, t)); // on it: close
    assert_eq!(show_page(true, t, a), (true, a));
    assert_eq!(KILLSTREAKS, 3);
}

#[test]
fn the_window_systems_have_no_conflicting_queries() {
    // Bevy refuses a system whose queries overlap when it is initialised.
    let mut world = World::new();
    let mut schedule = Schedule::default();
    schedule.add_systems((
        tip::load_token_tables,
        server_messages,
        toggle,
        build_window,
        press,
        delete_popup,
        refresh,
        refresh_submenu,
        grey_controls,
        show_icons,
        refresh_talents,
        show_talent_icons,
        refresh_streaks,
        talent_tooltip,
        abilities::refresh,
        abilities::drag,
    ));
    schedule
        .initialize(&mut world)
        .expect("the systems initialise");
}

#[test]
fn typing_edits_the_name() {
    assert_eq!(edit_name("Class", " 1", false), "Class 1");
    assert_eq!(edit_name("Class 1", "", true), "Class ");
    assert_eq!(edit_name("ab", "\u{8}x", false), "abx"); // control characters ignored
    let long = "x".repeat(24);
    assert_eq!(edit_name(&long, "y", false), long); // at most 24
}

#[test]
fn options_name_their_icons() {
    let c = catalog();
    assert_eq!(
        parts::choice_icon(&c, None, &Choice::Attachment("silencer".into())).as_deref(),
        Some("weapon_attachment_suppressor")
    );
    assert_eq!(
        parts::choice_icon(&c, None, &Choice::Perk(perk_id::BLING)).as_deref(),
        Some("specialty_bling")
    );
    assert_eq!(
        parts::choice_icon(&c, None, &Choice::Attachment(String::new())),
        None
    );
    assert_eq!(parts::choice_icon(&c, None, &Choice::Gun(2)), None); // no install
}

#[test]
fn stat_bars_are_normalised_over_the_catalog() {
    let c = catalog();
    for g in &c.weapons {
        for v in stat_bars(&c, g) {
            assert!((0.0..=1.0).contains(&v), "{} {v}", g.name);
        }
    }
    let m4 = stat_bars(&c, c.gun_named("m4_mp").unwrap());
    let barrett = stat_bars(&c, c.gun_named("barrett_mp").unwrap());
    assert!(barrett[1] > m4[1]); // a sniper reaches farther
    assert!(m4[2] > barrett[2]); // and an M4 fires faster
    assert!((m4[0] - 0.3).abs() < 1e-6); // 30 of a kill's 100
    assert!(
        c.weapons
            .iter()
            .any(|g| (stat_bars(&c, g)[2] - 1.0).abs() < 1e-6)
    );
}

#[test]
fn the_attachment_grid_hides_incompatible_choices() {
    let c = catalog();
    let m4 = c.gun_named("m4_mp").unwrap();
    let alone = attachment_choices(&c, m4, "");
    assert_eq!(alone[0], "");
    assert!(alone.iter().any(|a| a == "acog") && alone.iter().any(|a| a == "reflex"));
    let with_acog = attachment_choices(&c, m4, "acog");
    assert!(!with_acog.iter().any(|a| a == "acog")); // not twice
    assert!(!with_acog.iter().any(|a| a == "reflex")); // two sights
    assert!(with_acog.iter().any(|a| a == "silencer"));
}

#[test]
fn who_opened_the_kit_window_says_whether_kits_can_change() {
    assert_eq!(
        KitAccess::of_open(&[OP_OPEN_CLASSES, 1]),
        (KitAccess::Station, 0)
    );
    assert_eq!(
        KitAccess::of_open(&[OP_OPEN_CLASSES, 2, 120, 0]),
        (KitAccess::Field, 120)
    );
    assert_eq!(
        KitAccess::of_open(&[OP_OPEN_CLASSES, 0]),
        (KitAccess::View, 0)
    );
    // An older server's bare op is a station's.
    assert_eq!(
        KitAccess::of_open(&[OP_OPEN_CLASSES]),
        (KitAccess::Station, 0)
    );
    assert_eq!(KitAccess::View.panel_access(), Access::Hud);
    assert_eq!(KitAccess::Station.panel_access(), Access::Npc);
    assert_eq!(KitAccess::Field.panel_access(), Access::Npc);
    assert_eq!(KitAccess::default(), KitAccess::View);
}

#[test]
fn the_hud_views_and_an_npc_edits() {
    let mut ui = CacUi::default();
    assert!(!ui.can_edit()); // the HUD's
    ui.access = KitAccess::Station;
    assert!(ui.can_edit());
    ui.access = KitAccess::Field;
    assert!(ui.can_edit());
    // A GM's status edits from the HUD too.
    ui.access = KitAccess::View;
    ui.status = Some(KitStatus {
        flags: STATUS_CAN_EDIT | STATUS_GM,
        ..default()
    });
    assert!(ui.can_edit());
    // Every edit is greyed while viewing; looking around the kits and their options is not.
    for a in [
        Action::Assign,
        Action::Pro(true),
        Action::Talent(1, 0),
        Action::Streak(0, 1),
        Action::Equip,
        Action::Save,
        Action::Rename,
        Action::NewKit,
        Action::CopyKit,
        Action::DeleteKit,
    ] {
        assert!(a.edits(), "{a:?}");
    }
    for a in [
        Action::KitSlot(1),
        Action::DropDown,
        Action::FocusPart(3),
        Action::Step(3, 1),
        Action::PickOption(2),
        Action::Page(2),
        Action::Close,
        Action::FieldEdit,
    ] {
        assert!(!a.edits(), "{a:?}");
    }
}

#[test]
fn the_view_says_to_visit_a_trainer_or_innkeeper() {
    let view = access_line(KitAccess::View, false, 0, 0);
    assert!(
        view.contains("Visit a trainer or innkeeper to change kits"),
        "{view}"
    );
    assert!(access_line(KitAccess::View, false, 1800, 0).contains("ready in 30 min"));
    assert!(access_line(KitAccess::Field, false, 0, 95).contains("1:35"));
    let station = access_line(KitAccess::Station, false, 0, 0);
    assert!(station.starts_with("Kits can change here.") && station.contains("rearm"));
}

#[test]
fn the_kit_status_decodes() {
    let s = decode_kit_status(&[OP_KIT_STATUS, 3, 0x10, 0x0E, 0, 0, 120, 0]).unwrap();
    assert_eq!(
        s,
        KitStatus {
            flags: STATUS_CAN_EDIT | STATUS_FIELD_OPEN,
            field_ready_in: 3600,
            field_open_for: 120,
        }
    );
    assert_eq!(decode_kit_status(&[OP_KIT_STATUS, 1]), None);
    let ui = CacUi {
        status: Some(s),
        status_at: 10.0,
        ..default()
    };
    assert_eq!(ui.field_ready_in(10.0), 3600);
    assert_eq!(ui.field_ready_in(70.0), 3540);
    assert_eq!(ui.field_ready_in(5000.0), 0);
}

#[test]
fn refusals_and_notes_show_in_the_window() {
    let (s, _) = status_text(
        None,
        Some(LoadoutError::NotAtKitStation),
        false,
        None,
        false,
    );
    assert!(s.contains("trainer or innkeeper"), "{s}");
    let (s, _) = status_text(None, None, false, Some("You left"), true);
    assert_eq!(s, "You left");
    let (s, _) = status_text(None, None, true, None, true);
    assert_eq!(s, "Equips when you leave combat");
    assert_eq!(status_text(None, None, false, None, true).0, "Equipped");
    for e in [
        LoadoutError::UnknownStreak,
        LoadoutError::PerkDoesNotStack,
        LoadoutError::ProLocked,
    ] {
        assert!(!error_text(e).is_empty());
    }
    assert!(error_text(LoadoutError::TwoAttachmentsNeedBling).contains("Tinker's Rails"));
}

#[test]
fn the_kit_window_has_a_fourth_attachment_slot_greyed_by_kind() {
    let c = catalog();
    let mut unlocks = Unlocks::default();
    unlocks.everything = true;
    let mut ui = CacUi::default();
    ui.draft.primary = c.gun_named("m4_mp").unwrap().id;
    ui.draft.perks = [perk_id::BLING, perk_id::BLING, 0];
    // The fourth slot's row, after the third, closed until a third Rails.
    let four = part_index(Part::PrimaryAtt(3));
    assert_eq!(four, part_index(Part::PrimaryAtt(2)) + 1);
    assert_eq!(Part::PrimaryAtt(3).label(), "Attachment 4");
    ui.focus_part(&c, four);
    let texts = |ui: &CacUi| {
        window_texts(
            ui,
            &Classes::default(),
            &c,
            &unlocks,
            &crate::achievements::Achievements::default(),
            None,
            0.0,
        )
    };
    let t = texts(&ui);
    assert_eq!(t[&TxtId::SubTitle].0, "Primary Attachment 4");
    assert!(t[&TxtId::SubHint].0.contains("all three perk slots"));
    assert_eq!(t[&TxtId::PartPick(four as u8)].1, GREY);
    ui.draft.perks[2] = perk_id::BLING;
    assert!(
        texts(&ui)[&TxtId::SubHint]
            .0
            .contains("Tinker's Rails make room")
    );
    // With a sight and a launcher on, the fourth refuses another of either kind, and says why.
    ui.draft.primary_att1 = "reflex".into();
    ui.draft.primary_att2 = "gl".into();
    let refusal = |a: &str| {
        assign_refusal(
            &c,
            &unlocks,
            &ui.draft,
            Part::PrimaryAtt(3),
            &Choice::Attachment(a.into()),
            false,
        )
    };
    assert_eq!(refusal("thermal").as_deref(), Some("Already has a sight."));
    assert_eq!(
        refusal("grip").as_deref(),
        Some("Already has an underbarrel attachment.")
    );
    assert_eq!(refusal("xmags"), None);
    // Assigned from the window, it goes in the fourth.
    ui.cursor = parts::options(&c, &ui.draft, Part::PrimaryAtt(3))
        .iter()
        .position(|o| *o == Choice::Attachment("xmags".into()))
        .unwrap();
    assert!(assign_highlighted(&mut ui, &c, &unlocks));
    assert_eq!(ui.draft.primary_att4, "xmags");
    assert_eq!(
        crate::cac::primary_attachments(&ui.draft),
        ["reflex", "gl", "xmags"]
    );
}

#[test]
fn the_window_texts_name_every_part_and_kit() {
    let c = catalog();
    let mut classes = Classes::default();
    classes.classes[1] = Some(ClassLoadout {
        name: "Sniper".into(),
        ..Default::default()
    });
    classes.equipped = Some(1);
    let mut ui = CacUi::default();
    ui.draft.perks = [perk_id::BLING, 0, 0];
    ui.draft.perk_pro = 1;
    ui.focus_part(&c, part_index(Part::PrimaryAtt(2)));
    let t = window_texts(
        &ui,
        &classes,
        &c,
        &Unlocks::default(),
        &crate::achievements::Achievements::default(),
        Some("L"),
        0.0,
    );
    assert_eq!(t[&TxtId::DropRow(1)].0, "2. Sniper  (equipped)");
    assert_eq!(t[&TxtId::DropRow(0)].0, "1. Kit 1");
    assert_eq!(
        t[&TxtId::PartPick(part_index(Part::Primary) as u8)].0,
        "None (class gun)"
    );
    assert!(
        t[&TxtId::PartPick(part_index(Part::Perk(1)) as u8)]
            .0
            .ends_with(" Pro")
    );
    assert_eq!(t[&TxtId::SubTitle].0, "Primary Attachment 3");
    assert!(
        t[&TxtId::SubHint].0.contains("primary first"),
        "{}",
        t[&TxtId::SubHint].0
    );
    assert_eq!(kit_line(0, "A", true), "1. A  (equipped)");
}

#[test]
fn a_kit_chooses_its_killstreaks_once_each() {
    let ladder = streak_ladder();
    assert_eq!(
        ladder.iter().map(|c| c.id).collect::<Vec<_>>(),
        [1, 4, 5, 7, 8, 11, 12, 15]
    );
    let mut kit = ClassLoadout::default();
    cycle_streak(&mut kit, 0, 1);
    assert_eq!(kit.streaks[0], 1); // the UAV
    cycle_streak(&mut kit, 1, 1);
    assert_eq!(kit.streaks[1], 4); // not the UAV again
    cycle_streak(&mut kit, 1, -1);
    assert_eq!(kit.streaks[1], 0); // back to none
    for _ in 0..10 {
        cycle_streak(&mut kit, 2, 1);
        assert_ne!(kit.streaks[2], 1);
    }
    // None chosen: every unlocked one; some chosen: those, in kill order.
    let none = ClassLoadout::default();
    assert_eq!(kit_streaks(&none, |id| id != 15).len(), 7);
    let chosen = ClassLoadout {
        streaks: [12, 1, 0],
        ..Default::default()
    };
    let mine: Vec<u16> = kit_streaks(&chosen, |_| true)
        .iter()
        .map(|c| c.id)
        .collect();
    assert_eq!(mine, [1, 12]);
}

#[test]
fn the_killstreak_page_shows_costs_and_the_streak() {
    let kit = ClassLoadout {
        streaks: [1, 7, 0],
        ..Default::default()
    };
    let lines = streak_lines(&kit, |id| id != 7, 34, false);
    let slot = &lines[&StreakTextId::Slot(0)].0;
    assert!(slot.contains("(3 kills)"), "{slot}");
    assert!(lines[&StreakTextId::Slot(1)].0.contains("(Locked)"));
    assert_eq!(lines[&StreakTextId::Slot(2)].0, "None");
    assert!(lines[&StreakTextId::Row(0)].0.contains("earned this life"));
    assert!(lines[&StreakTextId::Row(1)].0.contains("locked"));
    assert_eq!(lines[&StreakTextId::Row(2)].0, "");
    assert!(lines[&StreakTextId::Progress].0.contains("3.4 kills"));
    assert!(lines[&StreakTextId::Note].0.is_empty());
    // Hardline: a kill sooner.
    let hard = streak_lines(&kit, |_| true, 0, true);
    assert!(hard[&StreakTextId::Slot(0)].0.contains("(2 kills)"));
    assert!(hard[&StreakTextId::Row(0)].0.contains("2.0 to go"));
    // Nothing chosen: the note says the whole ladder plays.
    let none = streak_lines(&ClassLoadout::default(), |_| true, 0, false);
    assert!(none[&StreakTextId::Note].0.contains("every killstreak"));
    assert!(!none[&StreakTextId::Row(5)].0.is_empty());
}

#[test]
fn kits_are_named_kits_and_the_refusal_says_where() {
    assert_eq!(empty_kit_name(0), "Kit 1");
    assert!(error_text(LoadoutError::NotAtKitStation).contains("trainer or innkeeper"));
    assert!(!error_text(LoadoutError::UnknownTalent).is_empty());
}
