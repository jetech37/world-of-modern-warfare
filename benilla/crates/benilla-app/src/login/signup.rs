//! World of Modern Warfare: the login screen's Create Account dialog. Not in the reference: the
//! 1.12 client has no in-game sign-up, its accounts were made on a web page. Deviation: a realm
//! whose operator publishes a sign-up address (`$AW_SIGNUP_URL`, which the WOMW launcher sets
//! beside `$WOW_HOST`) gets a Create Account button in the absent Community Site button's slot;
//! without one the screen is the reference's.
//!
//! The dialog wears `GlueDialog`'s backdrop and buttons and the login boxes. It asks the realm's
//! gate whether an invite code is needed (`GET …/api/signup-info`) and shows that box only then,
//! checks the name and password as the gate does, and posts them (`POST …/api/signup`) over
//! HTTPS off the main thread. A made account fills the login boxes, so Login is one press away.
//!
//! The player must tick a box confirming the gate's legal-access statement ([`ATTESTATION_TEXT`],
//! or the text the gate sends with its version); the post carries the confirmation and the gate
//! records it with the account.
//!
//! The address is tied to the realm: it is used only while its host is the realmlist's host.

use bevy::input::keyboard::KeyboardInput;
use bevy::input::ButtonState;
use bevy::prelude::*;
use crossbeam_channel::{Receiver, TryRecvError};

use benilla_ui::widget::EditBoxState;

use crate::char_select::wow_font;
use crate::glue::art::{GlueArt, GOLD};
use crate::glue::backdrop::{backdrop_border, tiled_bg_node};
use crate::glue::dialog::GlueDialog;
use crate::glue::widgets::{
    glue_button, glue_edit_box, outlined_text, outlined_text_centered, overlay, paint_glue_field,
    GlueBtnKind, GlueDisabled, GlueFieldPart, GlueText,
};
use crate::glue_strings::GlueStrings;
use crate::sound::GlueSound;
use crate::textinput::{self, HostClipboard};

use super::{Field, LoginForm};

/// Where the realm's gate takes sign-ups, `https://<host>/api/signup`.
pub(crate) const SIGNUP_URL_ENV: &str = "AW_SIGNUP_URL";
/// The legal-access statement the box confirms, word for word, and its version: the gate's own
/// (`vps/gate.py`), shown until the gate sends its text.
const ATTESTATION_TEXT: &str =
    "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009).";
const ATTESTATION_VERSION: u32 = 1;
/// The gate's names and passwords: at most 16 characters, as `AccountLogin.xml`'s boxes.
const MAX_LETTERS: usize = 16;
/// Invite codes: 12 characters, typed with or without their two dashes.
const CODE_LETTERS: usize = 16;
/// Over the login screen (1100), under `GlueDialog` (1200), which raises the success note.
const SIGNUP_Z: i32 = 1150;
/// `DEFAULT_TOOLTIP_COLOR`, the login boxes' tint.
const BOX_BORDER: Color = Color::srgb(0.8, 0.8, 0.8);
const BOX_FILL: Color = Color::srgb(0.09, 0.09, 0.09);
/// A refusal's colour, `RED_FONT_COLOR`.
const BAD: Color = Color::srgb(1.0, 0.1, 0.1);
const NOTE: Color = Color::srgb(1.0, 1.0, 1.0);

/// The boxes, in Tab order: name, password, the password again, and the invite code when the
/// realm needs one.
const NAME: usize = 0;
const PASSWORD: usize = 1;
const CONFIRM: usize = 2;
const CODE: usize = 3;

/// The sign-up address for the realm at `realmlist`: `env` when it is an HTTPS address (plain
/// HTTP only to this machine) on the realmlist's own host.
pub(crate) fn signup_url(env: Option<&str>, realmlist: &str) -> Option<String> {
    let url = env?.trim();
    let rest = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))?;
    let host = rest.split(['/', '?', '#']).next()?;
    let host = strip_port(host);
    if host.is_empty() {
        return None;
    }
    let local = matches!(host, "localhost" | "127.0.0.1");
    if !url.starts_with("https://") && !local {
        return None;
    }
    host.eq_ignore_ascii_case(strip_port(realmlist.trim()))
        .then(|| url.to_string())
}

/// `host` of `host[:port]`.
fn strip_port(address: &str) -> &str {
    address.rsplit_once(':').map_or(address, |(h, _)| h)
}

/// This session's sign-up address, read from [`SIGNUP_URL_ENV`].
pub(crate) fn signup_url_for(realmlist: &str) -> Option<String> {
    signup_url(std::env::var(SIGNUP_URL_ENV).ok().as_deref(), realmlist)
}

/// The gate's question, beside its sign-up address: `…/api/signup` asks `…/api/signup-info`.
fn info_url(signup: &str) -> String {
    format!("{}-info", signup.trim_end_matches('/'))
}

/// Why the typed account won't do, as the gate would say (`vps/gate.py`), or `None`.
fn check(
    name: &str,
    password: &str,
    confirm: &str,
    code: Option<&str>,
    attested: bool,
) -> Option<&'static str> {
    if !(3..=16).contains(&name.len()) || !name.bytes().all(|b| b.is_ascii_alphanumeric()) {
        return Some("Account names are 3-16 letters or digits.");
    }
    if !(6..=16).contains(&password.len()) || !password.bytes().all(|b| (0x21..=0x7e).contains(&b))
    {
        return Some("Passwords are 6-16 letters, digits or symbols (no spaces).");
    }
    if password.eq_ignore_ascii_case(name) {
        return Some("The password can't be the account name.");
    }
    if password != confirm {
        return Some("The two passwords don't match.");
    }
    if code.is_some_and(|c| c.trim().is_empty()) {
        return Some("This realm needs an invite code to sign up.");
    }
    if !attested {
        return Some("Tick the box above to confirm the statement.");
    }
    None
}

#[derive(serde::Deserialize)]
struct Info {
    invite_required: Option<bool>,
    attestation: Option<Statement>,
}

/// The statement to confirm, as the gate sends it (`{"text": ..., "version": ...}`).
#[derive(serde::Deserialize, Clone, Debug, PartialEq)]
struct Statement {
    text: String,
    version: u32,
}

impl Default for Statement {
    fn default() -> Self {
        Statement {
            text: ATTESTATION_TEXT.to_string(),
            version: ATTESTATION_VERSION,
        }
    }
}

/// What the gate says before the form: whether a code is needed, and the statement.
#[derive(Clone, Debug, PartialEq)]
struct GateInfo {
    needs_code: bool,
    statement: Statement,
}

#[derive(serde::Serialize)]
struct Body<'a> {
    name: &'a str,
    password: &'a str,
    code: &'a str,
    attest: bool,
    attestation_version: u32,
}

#[derive(serde::Deserialize)]
struct Reply {
    message: Option<String>,
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(std::time::Duration::from_secs(10))
        .timeout_read(std::time::Duration::from_secs(20))
        .build()
}

/// Whether the realm needs an invite code, and its statement; a gate that can't say needs a code,
/// as gates did before they could be asked, and gets the built-in statement.
fn ask_info(url: &str) -> GateInfo {
    let info = agent()
        .get(&info_url(url))
        .call()
        .ok()
        .and_then(|r| r.into_json::<Info>().ok());
    GateInfo {
        needs_code: info
            .as_ref()
            .and_then(|i| i.invite_required)
            .unwrap_or(true),
        statement: info
            .and_then(|i| i.attestation)
            .filter(|s| !s.text.trim().is_empty())
            .unwrap_or_default(),
    }
}

/// Post the sign-up, with the box's confirmation of statement `attested`: the gate's message, as
/// `Ok` when the account was made.
fn post_signup(
    url: &str,
    name: &str,
    password: &str,
    code: &str,
    attested: u32,
) -> Result<String, String> {
    match agent().post(url).send_json(Body {
        name,
        password,
        code,
        attest: true,
        attestation_version: attested,
    }) {
        Ok(r) => Ok(r
            .into_json::<Reply>()
            .ok()
            .and_then(|r| r.message)
            .unwrap_or_else(|| format!("Account {} made.", name.to_ascii_uppercase()))),
        Err(ureq::Error::Status(status, r)) => Err(r
            .into_json::<Reply>()
            .ok()
            .and_then(|r| r.message)
            .unwrap_or_else(|| format!("The realm refused the sign-up (error {status})."))),
        Err(e) => Err(format!("Couldn't reach the realm's sign-up: {e}")),
    }
}

/// The dialog's state.
#[derive(Resource)]
pub(crate) struct Signup {
    pub(crate) open: bool,
    url: String,
    fields: [EditBoxState; 4],
    focus: usize,
    /// Whether the realm needs an invite code; `None` while it is being asked.
    needs_code: Option<bool>,
    /// The legal-access statement, and whether its box is ticked.
    statement: Statement,
    attested: bool,
    status: String,
    status_bad: bool,
    info: Option<Receiver<GateInfo>>,
    result: Option<Receiver<Result<String, String>>>,
    /// What the tree was built for: the code box, the statement, and the glue scale.
    built: Option<(bool, String, f32)>,
    root: Option<Entity>,
}

impl Default for Signup {
    fn default() -> Self {
        Signup {
            open: false,
            url: String::new(),
            fields: Self::empty_fields(),
            focus: NAME,
            needs_code: None,
            statement: Statement::default(),
            attested: false,
            status: String::new(),
            status_bad: false,
            info: None,
            result: None,
            built: None,
            root: None,
        }
    }
}

impl Signup {
    fn empty_fields() -> [EditBoxState; 4] {
        [
            textinput::field(MAX_LETTERS, false),
            textinput::field(MAX_LETTERS, true),
            textinput::field(MAX_LETTERS, true),
            textinput::field(CODE_LETTERS, false),
        ]
    }

    /// Open on a clean form for the realm's sign-up at `url`, and ask it about invite codes.
    pub(crate) fn start(&mut self, url: String) {
        *self = Signup {
            open: true,
            status: "Checking the realm...".into(),
            root: self.root.take(),
            ..default()
        };
        let (tx, rx) = crossbeam_channel::bounded(1);
        let ask = url.clone();
        std::thread::spawn(move || {
            let _ = tx.send(ask_info(&ask));
        });
        self.url = url;
        self.info = Some(rx);
        self.fields[NAME].reset_blink();
    }

    fn close(&mut self) {
        self.open = false;
        self.info = None;
        self.result = None;
        // The passwords don't outlive the dialog.
        self.fields = Self::empty_fields();
    }

    fn busy(&self) -> bool {
        self.result.is_some()
    }

    /// The boxes shown, in Tab order.
    fn shown(&self) -> usize {
        if self.needs_code == Some(true) {
            4
        } else {
            3
        }
    }

    fn focus(&mut self, i: usize) {
        self.fields[self.focus].highlight_text(0, 0);
        self.focus = i;
        self.fields[i].highlight_text(0, -1);
        self.fields[i].reset_blink();
    }

    fn say(&mut self, text: &str, bad: bool) {
        self.status = text.to_string();
        self.status_bad = bad;
    }
}

/// A control on the dialog.
#[derive(Component, Clone, Copy, PartialEq, Eq)]
pub(super) enum SignupAction {
    Focus(usize),
    /// The legal-access box.
    ToggleAttest,
    Create,
    Cancel,
}

#[derive(Component)]
pub(super) struct SignupRoot;
/// A box's row items, by box.
#[derive(Component, Clone)]
pub(super) struct SignupText(usize);
/// The line under the boxes: what is happening, or what was wrong.
#[derive(Component)]
pub(super) struct SignupStatus;
/// The legal-access box's check mark, shown while it is ticked.
#[derive(Component)]
pub(super) struct SignupCheck;

/// Typing, Tab, Enter (Create) and Escape (Cancel), the clicks, and the gate's answers. Inert
/// while `GlueDialog` is up over it.
#[allow(clippy::too_many_arguments)]
pub(super) fn signup_input(
    mut signup: ResMut<Signup>,
    mut form: ResMut<LoginForm>,
    mut dialog: ResMut<GlueDialog>,
    presses: Query<(Entity, &SignupAction, Ref<Interaction>)>,
    clicks: Res<crate::glue::GlueClicks>,
    mut keyboard: MessageReader<KeyboardInput>,
    keys: Res<ButtonInput<KeyCode>>,
    (mut clipboard, layout): (NonSendMut<HostClipboard>, Res<crate::bindings::LayoutNames>),
    raw_handle: Query<&bevy::window::RawHandleWrapper, With<bevy::window::PrimaryWindow>>,
    mut sounds: MessageWriter<GlueSound>,
    time: Res<Time>,
) {
    if !signup.open {
        keyboard.clear();
        return;
    }
    // The gate's answers.
    if let Some(rx) = &signup.info {
        match rx.try_recv() {
            Ok(info) => {
                let needs = info.needs_code;
                signup.needs_code = Some(needs);
                signup.statement = info.statement;
                signup.info = None;
                let note = if needs {
                    "This realm needs an invite code."
                } else {
                    "Pick an account name and a password."
                };
                signup.say(note, false);
            }
            Err(TryRecvError::Disconnected) => {
                signup.needs_code = Some(true);
                signup.info = None;
            }
            Err(TryRecvError::Empty) => {}
        }
    }
    if let Some(rx) = &signup.result {
        match rx.try_recv() {
            Ok(Ok(message)) => {
                let name = signup.fields[NAME].text.clone();
                let password = signup.fields[PASSWORD].text.clone();
                info!("login: sign-up made account {}", name.to_ascii_uppercase());
                form.account.set_text(&name);
                form.password.set_text(&password);
                form.focus(Field::Password);
                signup.close();
                dialog.open_error(&format!("{message}\nPress Login to enter the game."));
            }
            Ok(Err(why)) => {
                warn!("login: sign-up refused: {why}");
                signup.result = None;
                signup.say(&why, true);
            }
            Err(TryRecvError::Disconnected) => {
                signup.result = None;
                signup.say("The sign-up stopped; try again.", true);
            }
            Err(TryRecvError::Empty) => {}
        }
    }
    if dialog.is_open() || !signup.open {
        keyboard.clear();
        return;
    }
    let focus = signup.focus;
    textinput::tick_caret(&mut signup.fields[focus], true, time.delta_secs());

    let mut create = false;
    let mut cancel = false;
    for (entity, action, interaction) in &presses {
        if let SignupAction::Focus(i) = *action {
            if interaction.is_changed() && *interaction == Interaction::Pressed {
                signup.focus(i);
            }
            continue;
        }
        if *action == SignupAction::ToggleAttest {
            if interaction.is_changed() && *interaction == Interaction::Pressed && !signup.busy() {
                signup.attested = !signup.attested;
                sounds.write(GlueSound("gsTitleOptionOK"));
            }
            continue;
        }
        if clicks.hit(entity) {
            match action {
                SignupAction::Create => create = true,
                SignupAction::Cancel => cancel = true,
                SignupAction::Focus(_) | SignupAction::ToggleAttest => {}
            }
        }
    }
    let mods = textinput::mods_now(&keys);
    let wl = textinput::wayland_display(raw_handle.iter().next());
    for ev in keyboard.read() {
        let focus = signup.focus;
        if signup.busy() {
            continue;
        }
        if textinput::feed_key(
            &mut signup.fields[focus],
            ev,
            mods,
            &layout,
            &mut clipboard,
            wl,
            textinput::CharFilter::Any,
        ) == textinput::FieldKey::Consumed
        {
            continue;
        }
        if ev.state == ButtonState::Pressed && ev.key_code == KeyCode::Tab {
            let n = signup.shown();
            let next = if mods.shift {
                (focus + n - 1) % n
            } else {
                (focus + 1) % n
            };
            signup.focus(next);
        }
    }
    if keys.just_pressed(KeyCode::Enter) || keys.just_pressed(KeyCode::NumpadEnter) {
        create = true;
    }
    if keys.just_pressed(KeyCode::Escape) {
        cancel = true;
    }

    if cancel {
        sounds.write(GlueSound("gsTitleOptionOK"));
        signup.close();
        return;
    }
    if create && !signup.busy() && signup.needs_code.is_some() {
        sounds.write(GlueSound("gsTitleOptionOK"));
        let needs_code = signup.needs_code == Some(true);
        let [name, password, confirm, code] = signup.fields.clone().map(|f| f.text);
        if let Some(why) = check(
            &name,
            &password,
            &confirm,
            needs_code.then_some(code.as_str()),
            signup.attested,
        ) {
            signup.say(why, true);
            return;
        }
        let code = if needs_code { code } else { String::new() };
        let url = signup.url.clone();
        let version = signup.statement.version;
        let (tx, rx) = crossbeam_channel::bounded(1);
        std::thread::spawn(move || {
            let _ = tx.send(post_signup(&url, &name, &password, &code, version));
        });
        signup.result = Some(rx);
        signup.say("Making your account...", false);
    }
}

/// Keep the tree in step with the dialog: built when it opens, rebuilt when the code box comes
/// or goes, the gate's statement arrives or the window resizes, gone when it closes.
pub(super) fn build_signup(
    mut commands: Commands,
    mut signup: ResMut<Signup>,
    art: Res<GlueArt>,
    assets: Res<AssetServer>,
    strings: Option<Res<GlueStrings>>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
) {
    if !signup.open {
        if let Some(root) = signup.root.take() {
            commands.entity(root).despawn();
        }
        signup.built = None;
        return;
    }
    let s = crate::glue::screen_scale(window.single().ok());
    let want = (
        signup.needs_code == Some(true),
        signup.statement.text.clone(),
        s,
    );
    if signup.root.is_some() && signup.built.as_ref() == Some(&want) {
        return;
    }
    if let Some(root) = signup.root.take() {
        commands.entity(root).despawn();
    }
    let empty = GlueStrings::default();
    let strings = strings.as_deref().unwrap_or(&empty);
    signup.root = Some(spawn_signup(
        &mut commands,
        &art,
        &assets,
        strings,
        (want.0, &want.1, signup.attested),
        s,
    ));
    signup.built = Some(want);
}

/// The dialog: `GlueDialog`'s 512-wide backdrop, the title, a label over each 300-wide box, the
/// legal-access box with its statement, the status line and the 200×40 pair of buttons.
/// `form` is (the code box shown, the statement, the box ticked).
fn spawn_signup(
    commands: &mut Commands,
    art: &GlueArt,
    assets: &AssetServer,
    strings: &GlueStrings,
    form: (bool, &str, bool),
    s: f32,
) -> Entity {
    let (with_code, statement, ticked) = form;
    let px = |v: f32| Val::Px(v * s);
    let font = wow_font(assets);
    let edit_font: Handle<Font> = assets.load("mpq://Fonts/ARIALN.ttf");
    let mut labels = vec![
        (
            NAME,
            strings.text("ACCOUNT_NAME", "Account Name").to_string(),
        ),
        (PASSWORD, "Password (6-16 characters)".to_string()),
        (CONFIRM, "Password again".to_string()),
    ];
    if with_code {
        labels.push((CODE, "Invite code".to_string()));
    }
    commands
        .spawn((
            SignupRoot,
            GlobalZIndex(SIGNUP_Z),
            Node {
                width: Val::Percent(100.0),
                height: Val::Percent(100.0),
                justify_content: JustifyContent::Center,
                align_items: AlignItems::Center,
                ..default()
            },
        ))
        .with_children(|o| {
            o.spawn(Node {
                width: px(512.0),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                padding: UiRect::axes(Val::Px(0.0), px(20.0)),
                row_gap: px(6.0),
                ..default()
            })
            .with_children(|b| {
                if let (Some(bg), Some(border)) = (&art.dialog_bg, &art.dialog_border) {
                    b.spawn((
                        tiled_bg_node(bg.clone(), 32.0, s, Color::WHITE),
                        Node {
                            position_type: PositionType::Absolute,
                            left: px(11.0),
                            right: px(12.0),
                            top: px(12.0),
                            bottom: px(11.0),
                            ..default()
                        },
                    ));
                    backdrop_border(b, border, 32.0, Color::WHITE);
                } else {
                    b.spawn((
                        BackgroundColor(Color::srgba(0.05, 0.05, 0.08, 0.95)),
                        overlay(),
                    ));
                }
                outlined_text_centered(
                    b,
                    Node {
                        width: px(440.0),
                        justify_content: JustifyContent::Center,
                        margin: UiRect::bottom(px(4.0)),
                        ..default()
                    },
                    (),
                    (),
                    GlueText {
                        text: "Create Account",
                        size: 18.0,
                        color: GOLD,
                        wrap: false,
                    },
                    &font,
                    s,
                );
                for (i, label) in &labels {
                    outlined_text(
                        b,
                        Node::default(),
                        (),
                        (),
                        GlueText {
                            text: label,
                            size: 13.0,
                            color: GOLD,
                            wrap: false,
                        },
                        &font,
                        s,
                    );
                    glue_edit_box(
                        b,
                        art,
                        &edit_font,
                        (SignupAction::Focus(*i), Button),
                        SignupText(*i),
                        (300.0, 32.0),
                        (BOX_BORDER, BOX_FILL),
                        (15.0, 0.0, 0.0, 5.0),
                        s,
                    );
                }
                // The legal-access box: the whole row (box and statement) toggles it.
                b.spawn((
                    SignupAction::ToggleAttest,
                    Button,
                    Node {
                        width: px(440.0),
                        flex_direction: FlexDirection::Row,
                        align_items: AlignItems::Center,
                        column_gap: px(8.0),
                        margin: UiRect::top(px(8.0)),
                        ..default()
                    },
                ))
                .with_children(|row| {
                    let mut tick = row.spawn(Node {
                        width: px(20.0),
                        height: px(20.0),
                        flex_shrink: 0.0,
                        ..default()
                    });
                    let shown = if ticked {
                        Visibility::Inherited
                    } else {
                        Visibility::Hidden
                    };
                    match &art.checkbox {
                        Some(check) => {
                            tick.insert(ImageNode::new(check.up.clone()));
                            tick.with_children(|inner| {
                                inner.spawn((
                                    SignupCheck,
                                    shown,
                                    ImageNode::new(check.checked.clone()),
                                    overlay(),
                                ));
                            });
                        }
                        None => {
                            tick.insert(BackgroundColor(Color::srgba(1.0, 1.0, 1.0, 0.15)));
                            tick.with_children(|inner| {
                                inner.spawn((
                                    SignupCheck,
                                    shown,
                                    Text::new("x"),
                                    TextFont {
                                        font: font.clone(),
                                        font_size: 14.0 * s,
                                        ..default()
                                    },
                                    TextColor(GOLD),
                                ));
                            });
                        }
                    }
                    outlined_text(
                        row,
                        Node {
                            width: px(412.0),
                            ..default()
                        },
                        (),
                        (),
                        GlueText {
                            text: statement,
                            size: 13.0,
                            color: GOLD,
                            wrap: true,
                        },
                        &font,
                        s,
                    );
                });
                outlined_text_centered(
                    b,
                    Node {
                        width: px(440.0),
                        min_height: px(36.0),
                        justify_content: JustifyContent::Center,
                        margin: UiRect::top(px(4.0)),
                        ..default()
                    },
                    (),
                    SignupStatus,
                    GlueText {
                        text: "",
                        size: 13.0,
                        color: NOTE,
                        wrap: true,
                    },
                    &font,
                    s,
                );
                b.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    column_gap: px(13.0),
                    ..default()
                })
                .with_children(|row| {
                    glue_button(
                        row,
                        art,
                        &font,
                        SignupAction::Create,
                        "Create Account",
                        200.0,
                        40.0,
                        GlueBtnKind::Dialog,
                        s,
                    );
                    glue_button(
                        row,
                        art,
                        &font,
                        SignupAction::Cancel,
                        strings.text("CANCEL", "Cancel"),
                        200.0,
                        40.0,
                        GlueBtnKind::Dialog,
                        s,
                    );
                });
            });
        })
        .id()
}

/// Paint the boxes, the status line and the Create button's state.
#[allow(clippy::type_complexity)]
pub(super) fn paint_signup(
    signup: Res<Signup>,
    dialog: Res<GlueDialog>,
    mut boxes: Query<(
        &SignupText,
        &GlueFieldPart,
        Option<&mut Text>,
        &mut Visibility,
    )>,
    mut status: Query<(&mut Text, &mut TextColor), (With<SignupStatus>, Without<SignupText>)>,
    mut buttons: Query<(&SignupAction, &mut GlueDisabled)>,
    mut checks: Query<&mut Visibility, (With<SignupCheck>, Without<SignupText>)>,
) {
    if !signup.open {
        return;
    }
    let focused = !dialog.is_open();
    for i in 0..4 {
        paint_glue_field(
            &signup.fields[i],
            focused && signup.focus == i,
            boxes
                .iter_mut()
                .filter(|(t, ..)| t.0 == i)
                .map(|(_, part, text, vis)| (part, text, vis)),
        );
    }
    for (mut text, mut color) in &mut status {
        if text.0 != signup.status {
            text.0 = signup.status.clone();
        }
        let want = if signup.status_bad { BAD } else { NOTE };
        if color.0 != want {
            color.0 = want;
        }
    }
    let ticked = if signup.attested {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut vis in &mut checks {
        if *vis != ticked {
            *vis = ticked;
        }
    }
    let disabled = signup.busy() || signup.needs_code.is_none();
    for (action, mut d) in &mut buttons {
        if *action == SignupAction::Create && d.0 != disabled {
            d.0 = disabled;
        }
    }
}

/// Leaving the login screen closes the dialog.
pub(super) fn exit_signup(mut commands: Commands, mut signup: ResMut<Signup>) {
    signup.close();
    if let Some(root) = signup.root.take() {
        commands.entity(root).despawn();
    }
    signup.built = None;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_address_is_the_realms_own() {
        let url = "https://play.womw.net/api/signup";
        assert_eq!(
            signup_url(Some(url), "play.womw.net"),
            Some(url.to_string())
        );
        assert_eq!(
            signup_url(Some(url), "PLAY.womw.net:3724"),
            Some(url.to_string())
        );
        // Another realm, none set, or a blank one: no button.
        assert_eq!(signup_url(Some(url), "localhost"), None);
        assert_eq!(signup_url(None, "play.womw.net"), None);
        assert_eq!(signup_url(Some("  "), "play.womw.net"), None);
        // Plain HTTP only to this machine.
        assert_eq!(
            signup_url(Some("http://play.womw.net/api/signup"), "play.womw.net"),
            None
        );
        assert_eq!(
            signup_url(Some("http://127.0.0.1:8090/api/signup"), "127.0.0.1"),
            Some("http://127.0.0.1:8090/api/signup".to_string())
        );
        assert_eq!(
            info_url("https://play.womw.net/api/signup"),
            "https://play.womw.net/api/signup-info"
        );
    }

    #[test]
    fn the_form_is_checked_as_the_gate_checks_it() {
        assert_eq!(check("Newbie", "Secret12", "Secret12", None, true), None);
        assert!(check("ab", "Secret12", "Secret12", None, true).is_some());
        assert!(check("has space", "Secret12", "Secret12", None, true).is_some());
        assert!(check("Newbie", "short", "short", None, true).is_some());
        assert!(check("Newbie", "has space1", "has space1", None, true).is_some());
        assert!(check("Newbie", "newbie", "newbie", None, true).is_some());
        assert_eq!(
            check("Newbie", "Secret12", "Secret13", None, true),
            Some("The two passwords don't match.")
        );
        assert!(check("Newbie", "Secret12", "Secret12", Some(""), true).is_some());
        assert_eq!(
            check(
                "Newbie",
                "Secret12",
                "Secret12",
                Some("K7QM-X3PA-W9TR"),
                true
            ),
            None
        );
        // The legal-access box must be ticked.
        assert_eq!(
            check("Newbie", "Secret12", "Secret12", None, false),
            Some("Tick the box above to confirm the statement.")
        );
    }

    #[test]
    fn the_statement_is_word_for_word() {
        assert_eq!(
            ATTESTATION_TEXT,
            "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009)."
        );
        let s = Signup::default();
        assert!(!s.attested, "the box starts unticked");
        assert_eq!(s.statement, Statement::default());
        assert_eq!(s.statement.version, 1);
    }

    /// One HTTP request served from a thread: the request it got comes back.
    fn serve_once(status: &str, body: &str) -> (String, std::thread::JoinHandle<String>) {
        use std::io::{Read, Write};
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        let reply = format!(
            "HTTP/1.1 {status}\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\
             Connection: close\r\n\r\n{body}",
            body.len()
        );
        let handle = std::thread::spawn(move || {
            let (mut s, _) = listener.accept().unwrap();
            let mut got = Vec::new();
            let mut buf = [0u8; 4096];
            loop {
                let n = s.read(&mut buf).unwrap();
                got.extend_from_slice(&buf[..n]);
                let text = String::from_utf8_lossy(&got).to_string();
                if let Some(end) = text.find("\r\n\r\n") {
                    let len = text
                        .lines()
                        .find_map(|l| {
                            l.to_ascii_lowercase()
                                .strip_prefix("content-length: ")
                                .map(|v| v.trim().parse::<usize>().unwrap())
                        })
                        .unwrap_or(0);
                    if got.len() >= end + 4 + len {
                        break;
                    }
                }
                if n == 0 {
                    break;
                }
            }
            s.write_all(reply.as_bytes()).unwrap();
            String::from_utf8_lossy(&got).to_string()
        });
        (base, handle)
    }

    #[test]
    fn the_gate_is_asked_and_told() {
        let (base, server) = serve_once(
            "200 OK",
            r#"{"open": true, "invite_required": false, "attestation": {"text": "Another statement.", "version": 2}}"#,
        );
        let info = ask_info(&format!("{base}/api/signup"));
        assert!(!info.needs_code);
        assert_eq!(
            info.statement,
            Statement {
                text: "Another statement.".into(),
                version: 2
            }
        );
        assert!(server.join().unwrap().starts_with("GET /api/signup-info "));
        // A gate that can't say: a code, as before, and the built-in statement.
        let (base, server) = serve_once("404 Not Found", r#"{"message": "not found"}"#);
        assert_eq!(
            ask_info(&format!("{base}/api/signup")),
            GateInfo {
                needs_code: true,
                statement: Statement::default()
            }
        );
        server.join().unwrap();

        let (base, server) = serve_once("201 Created", r#"{"message": "Account NEWBIE made."}"#);
        assert_eq!(
            post_signup(&format!("{base}/api/signup"), "Newbie", "Secret12", "", 1),
            Ok("Account NEWBIE made.".to_string())
        );
        let request = server.join().unwrap();
        assert!(request.starts_with("POST /api/signup "));
        assert!(request.contains(r#""name":"Newbie""#));
        assert!(request.contains(r#""code":"""#));
        assert!(request.contains(r#""attest":true"#));
        assert!(request.contains(r#""attestation_version":1"#));

        let (base, server) = serve_once(
            "409 Conflict",
            r#"{"message": "That account name is taken."}"#,
        );
        assert_eq!(
            post_signup(&format!("{base}/api/signup"), "Newbie", "Secret12", "", 1),
            Err("That account name is taken.".to_string())
        );
        server.join().unwrap();
    }

    #[test]
    fn tab_walks_the_shown_boxes() {
        let mut s = Signup::default();
        assert_eq!(s.shown(), 3);
        s.needs_code = Some(true);
        assert_eq!(s.shown(), 4);
        s.focus(CODE);
        assert_eq!(s.focus, CODE);
        s.open = true;
        s.fields[PASSWORD].set_text("Secret12");
        s.close();
        assert!(!s.open);
        assert!(s.fields.iter().all(|f| f.text.is_empty()));
        // A fresh form starts with the box unticked.
        s.attested = true;
        s.start("http://127.0.0.1:9/api/signup".into());
        assert!(!s.attested);
    }
}
