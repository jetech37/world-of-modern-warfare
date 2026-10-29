use std::cell::RefCell;

use super::*;

const LABEL: &str = "Fireball";

fn charging(started: f32, secs: f32) -> View {
    View::Charging {
        label: LABEL.into(),
        started,
        secs,
        until: None,
    }
}

const CASTING: Seen = Seen {
    casting: true,
    fading: false,
};

#[test]
fn a_charge_fills_then_empowers_then_holds_full() {
    let mut bar = CastBar::default();
    let e = bar.step(charging(1.0, 1.0), None, Seen::default(), 1.0);
    assert_eq!(
        e,
        vec![Edge::Start {
            label: LABEL.into(),
            total_ms: 1000,
            elapsed_ms: 0,
            color: None
        }]
    );
    assert_eq!(bar.stage(), Some(Stage::Charging));
    // Filling: nothing more to say while the stock bar runs.
    assert!(bar.step(charging(1.0, 1.0), None, CASTING, 1.5).is_empty());
    // Charged: the second stage refills in violet over the bonus time.
    let e = bar.step(charging(1.0, 1.0), None, CASTING, 2.0);
    assert_eq!(
        e,
        vec![Edge::Start {
            label: "Fireball - Empowering".into(),
            total_ms: 1750,
            elapsed_ms: 0,
            color: Some(EMPOWER_COLOR)
        }]
    );
    assert_eq!(bar.stage(), Some(Stage::Empowering));
    // Full charge at FULL_CHARGE_FACTOR times the charge time: held full, flashed.
    assert!(bar.step(charging(1.0, 1.0), None, CASTING, 3.0).is_empty());
    let e = bar.step(charging(1.0, 1.0), None, CASTING, 3.75);
    assert_eq!(e, vec![Edge::Full("Fireball - Full Charge".into())]);
    assert_eq!(bar.stage(), Some(Stage::Full));
    assert!(
        bar.step(charging(1.0, 1.0), None, Seen::default(), 9.0)
            .is_empty()
    );
}

#[test]
fn let_go_completes_early_fails_and_put_away_interrupts() {
    let mut bar = CastBar::default();
    bar.step(charging(0.0, 1.0), None, Seen::default(), 0.0);
    assert_eq!(
        bar.step(View::Idle, Some(Outcome::Released), CASTING, 1.2),
        vec![Edge::Stop]
    );
    assert_eq!(bar.stage(), None);

    bar.step(charging(2.0, 1.0), None, Seen::default(), 2.0);
    assert_eq!(
        bar.step(View::Idle, Some(Outcome::Fizzled), CASTING, 2.4),
        vec![Edge::Failed]
    );

    bar.step(charging(3.0, 1.0), None, Seen::default(), 3.0);
    assert_eq!(
        bar.step(View::Idle, None, CASTING, 3.4),
        vec![Edge::Interrupted]
    );
    // Idle stays quiet; a refusal after the flash reads Failed.
    assert!(bar.step(View::Idle, None, Seen::default(), 4.0).is_empty());
    assert_eq!(
        bar.step(View::Idle, Some(Outcome::Fizzled), Seen::default(), 4.1),
        vec![Edge::Failed]
    );
}

#[test]
fn a_stray_stop_is_put_back_and_a_waiting_release_holds_its_stage() {
    let mut bar = CastBar::default();
    bar.step(charging(0.0, 1.0), None, Seen::default(), 0.0);
    // The server's own cast ended the bar mid-charge: wound back on to where we are.
    let e = bar.step(charging(0.0, 1.0), None, Seen::default(), 0.5);
    assert_eq!(
        e,
        vec![Edge::Start {
            label: LABEL.into(),
            total_ms: 1000,
            elapsed_ms: 500,
            color: None
        }]
    );
    // Let go charged at 1.5 and waiting for its pair: time stops, no Full Charge.
    let wait = View::Charging {
        label: LABEL.into(),
        started: 0.0,
        secs: 1.0,
        until: Some(1.5),
    };
    let e = bar.step(wait.clone(), None, CASTING, 1.6);
    assert!(matches!(&e[..], [Edge::Start { color: Some(_), .. }]));
    assert!(bar.step(wait, None, CASTING, 2.5).is_empty());
    assert_eq!(bar.stage(), Some(Stage::Empowering));
}

#[test]
fn a_spray_is_a_draining_channel() {
    let mut bar = CastBar::default();
    let pour = |mana| View::Pouring {
        label: "Scorch".into(),
        mana,
    };
    assert_eq!(
        bar.step(pour(1.0), None, Seen::default(), 0.0),
        vec![
            Edge::ChannelStart {
                ms: SPRAY_BAR_MS,
                label: "Scorch".into()
            },
            Edge::ChannelLeft(SPRAY_BAR_MS)
        ]
    );
    assert!(bar.channeling());
    assert_eq!(
        bar.step(pour(0.5), None, Seen::default(), 0.1),
        vec![Edge::ChannelLeft(SPRAY_BAR_MS / 2)]
    );
    assert_eq!(
        bar.step(View::Idle, None, Seen::default(), 0.2),
        vec![Edge::ChannelStop]
    );
    // Refused by the server (out of mana, silenced): stopped and Interrupted.
    bar.step(pour(0.2), None, Seen::default(), 1.0);
    assert_eq!(
        bar.step(View::Idle, Some(Outcome::Fizzled), Seen::default(), 1.1),
        vec![Edge::ChannelStop, Edge::Interrupted]
    );
}

#[test]
fn both_hands_read_dual_or_both_names() {
    assert_eq!(
        label_for(&["Frostbolt".into(), "Frostbolt".into()]),
        "Frostbolt (Dual)"
    );
    assert_eq!(
        label_for(&["Fireball".into(), "Frostbolt".into()]),
        "Fireball + Frostbolt"
    );
    let mut hands = Hands::default();
    hands.right = Hand::Spell {
        action: 1,
        spell_id: 10,
    };
    hands.left = Hand::Spell {
        action: 2,
        spell_id: 10,
    };
    hands.charging = [Some(2.0), Some(1.5)];
    hands.charge_secs = [1.0, 1.0];
    let v = view_of(&hands, |_| "Frostbolt".into(), 1.0);
    // The older charge leads.
    assert_eq!(
        v,
        View::Charging {
            label: "Frostbolt (Dual)".into(),
            started: 1.5,
            secs: 1.0,
            until: None
        }
    );
    // The second hand joining mid-charge relabels the bar without restarting it.
    let mut bar = CastBar::default();
    bar.step(
        View::Charging {
            label: "Frostbolt".into(),
            started: 1.5,
            secs: 1.0,
            until: None,
        },
        None,
        Seen::default(),
        1.5,
    );
    assert_eq!(
        bar.step(v, None, CASTING, 2.0),
        vec![Edge::Text("Frostbolt (Dual)".into())]
    );
    // A hand let go charged, waiting for its pair: still shown, its time stopped.
    hands.charging = [None, Some(1.5)];
    hands.pending = [Some(2.6), None];
    hands.charge_from = [2.0, 0.0];
    assert!(matches!(
        view_of(&hands, |_| "Frostbolt".into(), 1.0),
        View::Charging { started, .. } if started == 1.5
    ));
    hands.charging = [None; 2];
    assert!(matches!(
        view_of(&hands, |_| "Frostbolt".into(), 1.0),
        View::Charging { started, until: Some(u), .. } if started == 2.0 && u == 2.6
    ));
    // Nothing held: idle; a spray pouring: its channel.
    hands.pending = [None; 2];
    assert_eq!(view_of(&hands, |_| String::new(), 1.0), View::Idle);
    hands.spraying[0] = Some(3.0);
    hands.spray_spell[0] = 10;
    assert!(matches!(
        view_of(&hands, |_| "Scorch".into(), 0.4),
        View::Pouring { mana, .. } if (mana - 0.4).abs() < 1e-6
    ));
}

/// The stock casting bar off the install's chain, as the game loads it.
fn stock_vm(data: &std::path::Path) -> UiScript {
    let chain = RefCell::new(benilla_formats::open_chain(data).expect("the patch chain"));
    let read = |p: &str| chain.borrow_mut().read_file(&p.replace('/', "\\")).ok();
    let mut s = UiScript::new().expect("a VM");
    s.set_screen_size(1024.0, 768.0);
    for file in [
        r"Interface\FrameXML\GlobalStrings.lua",
        r"Interface\FrameXML\Fonts.xml",
        r"Interface\FrameXML\CastingBarFrame.xml",
    ] {
        let path = file.replace('\\', "/");
        let bytes = read(&path).unwrap_or_else(|| panic!("{file}: not in the chain"));
        if path.ends_with(".lua") {
            s.run_chunk_named(&bytes, &format!("@{file}")).unwrap();
            continue;
        }
        let doc = benilla_ui::framexml::parse(&benilla_ui::source::decode(&bytes)).unwrap();
        let report = benilla_ui::loader::load_in(&s, &doc, &path, &read);
        assert!(report.errors.is_empty(), "{file}: {:?}", report.errors);
    }
    s
}

fn q<T: mlua::FromLuaMulti>(s: &UiScript, chunk: &str) -> T {
    s.eval::<T>(chunk).unwrap()
}

fn color(s: &UiScript) -> (f32, f32, f32) {
    q(
        s,
        "local r, g, b = CastingBarFrame:GetStatusBarColor() return r, g, b",
    )
}

fn seen(s: &UiScript) -> Seen {
    let (casting, fading) = q::<(bool, bool)>(s, SEEN_LUA);
    Seen { casting, fading }
}

const FILLED: &str = "local a, b = CastingBarFrame:GetMinMaxValues() \
     return (CastingBarFrame:GetValue() - a) / (b - a)";

/// One frame's step of the bar, done to the VM.
fn step(s: &mut UiScript, bar: &mut CastBar, view: View, outcome: Option<Outcome>, t: f32) {
    let seen = seen(s);
    apply(s, bar.step(view, outcome, seen, t));
}

/// Step the bar and the VM together through a held charge, `from` to `to`.
fn hold(s: &mut UiScript, bar: &mut CastBar, view: &View, from: f32, to: f32) {
    let mut t = from;
    while t < to {
        step(s, bar, view.clone(), None, t);
        s.tick(0.05);
        t += 0.05;
    }
}

#[test]
fn the_stock_bar_shows_the_charge_stages_and_their_ends() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    let mut bar = CastBar::default();
    let view = charging(0.0, 1.0);
    hold(&mut s, &mut bar, &view, 0.0, 0.5);
    assert!(q::<bool>(&s, "return CastingBarFrame:IsShown()"));
    assert_eq!(q::<String>(&s, "return CastingBarText:GetText()"), LABEL);
    let (r, g, b) = color(&s);
    assert!(
        (r - 1.0).abs() < 1e-3 && (g - 0.7).abs() < 1e-3 && b.abs() < 1e-3,
        "stock gold ({r} {g} {b})"
    );
    let half: f64 = q(&s, FILLED);
    assert!(
        (half - 0.5).abs() < 0.1,
        "half filled at half the charge ({half})"
    );

    hold(&mut s, &mut bar, &view, 0.5, 1.9);
    assert_eq!(bar.stage(), Some(Stage::Empowering));
    assert_eq!(
        q::<String>(&s, "return CastingBarText:GetText()"),
        "Fireball - Empowering"
    );
    let (r, g, b) = color(&s);
    assert!(
        (r - EMPOWER_COLOR.0).abs() < 1e-3 && (b - EMPOWER_COLOR.2).abs() < 1e-3 && g < 0.5,
        "violet ({r} {g} {b})"
    );
    let refill: f64 = q(&s, FILLED);
    // Refilled from empty at the charge: about half way through the bonus time now (0.9 of 1.75 s).
    assert!(
        (refill - 0.45).abs() < 0.15,
        "the second stage refills ({refill})"
    );

    hold(&mut s, &mut bar, &view, 1.9, 2.85);
    assert_eq!(bar.stage(), Some(Stage::Full));
    assert_eq!(
        q::<String>(&s, "return CastingBarText:GetText()"),
        "Fireball - Full Charge"
    );
    hold(&mut s, &mut bar, &view, 2.85, 3.85);
    assert!(q::<bool>(
        &s,
        "return CastingBarFrame:IsShown() and CastingBarFlash:IsShown() and true or false"
    ));
    assert!(
        q::<f32>(&s, "return CastingBarFlash:GetAlpha()") > 0.9,
        "the flash lit and held"
    );
    assert!(q::<f64>(&s, FILLED) > 0.99, "held full");

    // Let go: the stock green completion, then gone.
    step(&mut s, &mut bar, View::Idle, Some(Outcome::Released), 3.85);
    assert_eq!(color(&s), (0.0, 1.0, 0.0));
    for _ in 0..40 {
        s.tick(0.1);
    }
    assert!(!q::<bool>(&s, "return CastingBarFrame:IsShown()"));

    // Let go early: red Failed; put away: red Interrupted.
    hold(&mut s, &mut bar, &charging(10.0, 1.0), 10.0, 10.3);
    step(&mut s, &mut bar, View::Idle, Some(Outcome::Fizzled), 10.3);
    assert_eq!(color(&s), (1.0, 0.0, 0.0));
    let failed: String = q(&s, "return FAILED");
    assert_eq!(q::<String>(&s, "return CastingBarText:GetText()"), failed);
    hold(&mut s, &mut bar, &charging(20.0, 1.0), 20.0, 20.3);
    step(&mut s, &mut bar, View::Idle, None, 20.3);
    assert_eq!(color(&s), (1.0, 0.0, 0.0));
    let interrupted: String = q(&s, "return INTERRUPTED");
    assert_eq!(
        q::<String>(&s, "return CastingBarText:GetText()"),
        interrupted
    );
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn a_stray_stop_on_the_stock_bar_is_wound_back_on() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    let mut bar = CastBar::default();
    let view = charging(0.0, 1.0);
    hold(&mut s, &mut bar, &view, 0.0, 0.6);
    // The server's instant cast completing ends the stock bar under our charge.
    s.fire_event("SPELLCAST_STOP", vec![]);
    hold(&mut s, &mut bar, &view, 0.6, 0.7);
    assert!(seen(&s).casting, "casting again");
    let at: f64 = q(&s, FILLED);
    assert!((at - 0.65).abs() < 0.1, "where the charge is ({at})");
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn the_stock_bar_drains_a_spray_as_a_channel() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    let mut bar = CastBar::default();
    let pour = |mana| View::Pouring {
        label: "Scorch".into(),
        mana,
    };
    step(&mut s, &mut bar, pour(1.0), None, 0.0);
    s.tick(0.05);
    assert!(q::<bool>(
        &s,
        "return CastingBarFrame:IsShown() and CastingBarFrame.channeling and true or false"
    ));
    assert_eq!(q::<String>(&s, "return CastingBarText:GetText()"), "Scorch");
    let share: f64 = q(&s, FILLED);
    assert!(share > 0.95, "full at full mana ({share})");
    step(&mut s, &mut bar, pour(0.3), None, 0.1);
    s.tick(0.05);
    let share: f64 = q(&s, FILLED);
    assert!(
        (share - 0.3).abs() < 0.05,
        "drained with the mana ({share})"
    );
    step(&mut s, &mut bar, View::Idle, None, 0.2);
    assert!(!q::<bool>(
        &s,
        "return CastingBarFrame.channeling and true or false"
    ));
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn each_hand_charges_on_its_own_bar() {
    let mut hands = Hands::default();
    hands.right = Hand::Spell {
        action: 1,
        spell_id: 10,
    };
    hands.left = Hand::Spell {
        action: 2,
        spell_id: 20,
    };
    let name = |id: u32| if id == 10 { "Fireball".to_string() } else { "Frostbolt".to_string() };
    // One hand: the stock bar alone, named for its own spell.
    hands.charging = [Some(1.0), None];
    hands.charge_secs = [1.0, 0.5];
    assert!(matches!(view_of(&hands, name, 1.0), View::Charging { label, .. } if label == "Fireball"));
    assert_eq!(second_charge(&hands, name), None);
    // Both: the older on the stock bar, the other on its own with its own time and spell.
    hands.charging = [Some(1.0), Some(1.4)];
    assert!(matches!(view_of(&hands, name, 1.0), View::Charging { label, started, .. }
        if label == "Fireball" && started == 1.0));
    assert_eq!(
        second_charge(&hands, name),
        Some(("Frostbolt".to_string(), 1.4, 0.5, None))
    );
    // The second bar fills by its own hand's stage.
    let c = second_charge(&hands, name).unwrap();
    let lua = second_bar_lua(Some(&c), 1.65);
    assert!(lua.contains("SetValue(0.5)") && lua.contains("Frostbolt"), "{lua}");
    let lua = second_bar_lua(Some(&c), 1.4 + 0.5 * FULL_CHARGE_FACTOR);
    assert!(lua.contains("Full Charge"), "{lua}");
    assert!(second_bar_lua(None, 1.0).contains("b:Hide()"));
}

#[test]
fn the_other_hands_bar_shows_on_the_stock_ui() {
    let data = benilla_formats::wow_data_or_skip!();
    let s = stock_vm(&data);
    let c = ("Frostbolt".to_string(), 0.0, 1.0, None);
    crate::lua::run(&s, &second_bar_lua(Some(&c), 0.5));
    assert!(q::<bool>(&s, "return WomwHandCastBar:IsShown() and true or false"));
    assert_eq!(q::<String>(&s, "return WomwHandCastBar.text:GetText()"), "Frostbolt");
    assert!((q::<f64>(&s, "return WomwHandCastBar:GetValue()") - 0.5).abs() < 1e-3);
    crate::lua::run(&s, &second_bar_lua(Some(&c), 1.0 * FULL_CHARGE_FACTOR + 0.1));
    assert_eq!(
        q::<String>(&s, "return WomwHandCastBar.text:GetText()"),
        "Frostbolt - Full Charge"
    );
    crate::lua::run(&s, &second_bar_lua(None, 9.0));
    assert!(!q::<bool>(&s, "return WomwHandCastBar:IsShown() and true or false"));
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}
