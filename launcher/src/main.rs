//! World of Modern Warfare (WOMW), in one exe: run it once and it installs itself (with a desktop
//! shortcut), signs the player up (or logs in to their account), downloads the game; every run
//! after brings the game up to date (only what changed) and starts it.
//!
//! The server is built in (`AW_HOST`, https://host, at compile time). This exe is public; a new
//! player signs up (`POST /api/signup`), with a single-use invite code only when the server asks
//! for one (`GET /api/signup-info`). The game files need
//! the account: every download sends its name and password as HTTP Basic, which the server
//! checks against the account. The install keeps the name (account.txt) and the password
//! encrypted for the Windows user (password.dat, DPAPI), never in plain text. When the server
//! turns them down (a 401), the launcher forgets the password and asks again.
//!
//! The launcher stays open while the game runs: when the game crashes, it offers to send the crash
//! report the game left (`crash`, asked once).
//!
//! Before an account is made, and before anything downloads for an account that hasn't yet, the
//! player confirms the legal-access statement ([`ATTESTATION_TEXT`]) by typing Y. The server
//! records it with the account: sent with the sign-up, or `POST /api/attest` for an account made
//! before the statement (asked once, `GET /api/attest` says whether it was).

mod crash;

use std::collections::BTreeMap;
use std::fs;
use std::io::{self, BufRead, Read, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, UNIX_EPOCH};

use base64::Engine;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

const HOST: &str = env!("AW_HOST");
/// The optional HD pack's file list on the server, beside `manifest.json`.
const HD_MANIFEST: &str = "hd-manifest.json";
/// The pack's own manifest in the install: present only once every archive is down.
const HD_PACK_DONE: &str = "hd/womw-hd.txt";
/// Where an install remembers the player said no to the HD pack (`--hd` asks again).
const HD_DECLINED: &str = "hd-declined.txt";
/// The `Authorization` header every download sends: the account, set by [`login`].
static AUTHORIZATION: std::sync::RwLock<String> = std::sync::RwLock::new(String::new());

fn authorization() -> String {
    AUTHORIZATION.read().map(|c| c.clone()).unwrap_or_default()
}

fn set_account(name: &str, password: &str) {
    if let Ok(mut c) = AUTHORIZATION.write() {
        *c = basic(name, password);
    }
}

/// HTTP Basic credentials, as the header's value.
fn basic(name: &str, password: &str) -> String {
    let token = base64::engine::general_purpose::STANDARD.encode(format!("{name}:{password}"));
    format!("Basic {token}")
}

/// The error a request fails with when the server turns the account down (401).
const LOGIN_REJECTED: &str =
    "Wrong account name or password, or no account yet: make one when the launcher asks";
/// Too many wrong passwords from this address (429): the server waits a while.
const TOO_MANY_TRIES: &str =
    "too many wrong passwords from your address; wait 15 minutes and run the launcher again";
/// A 403: the server refuses outright (an old server mid-update).
const REFUSED: &str =
    "the server refused the download (it may be mid-update); try again in a few minutes";

/// A request's failure as the launcher says it: a 401 is the account's ([`LOGIN_REJECTED`]).
fn failure(e: ureq::Error) -> String {
    match e {
        ureq::Error::Status(401, _) => LOGIN_REJECTED.to_string(),
        ureq::Error::Status(429, _) => TOO_MANY_TRIES.to_string(),
        ureq::Error::Status(403, _) => REFUSED.to_string(),
        e => format!("can't reach the server: {e}"),
    }
}

/// A failure that retrying the same request won't fix.
fn final_failure(e: &str) -> bool {
    e == LOGIN_REJECTED || e == TOO_MANY_TRIES || e == REFUSED
}
/// The game server the client logs in to (its realm list).
const GAME_HOST: &str = env!("AW_GAME_HOST");

/// The legal-access statement, word for word, and its version: the server's
/// (`vps/gate.py` `ATTESTATION_TEXT`), shown when the server doesn't send its own.
const ATTESTATION_TEXT: &str =
    "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009).";
const ATTESTATION_VERSION: u32 = 1;
/// Where an install remembers the statement was confirmed, for a server too old to record it.
const ATTESTATION_FILE: &str = "attestation.txt";

/// The statement to confirm: its text and version.
#[derive(Debug, Clone, PartialEq)]
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

impl Statement {
    /// The server's statement (`{"text": ..., "version": ...}`), else the built-in one.
    fn from_json(v: &serde_json::Value) -> Statement {
        match (v["text"].as_str(), v["version"].as_u64()) {
            (Some(text), Some(version)) if !text.trim().is_empty() => Statement {
                text: text.to_string(),
                version: u32::try_from(version).unwrap_or(ATTESTATION_VERSION),
            },
            _ => Statement::default(),
        }
    }
}

/// What the server says about signing up (`GET /api/signup-info`).
#[derive(Debug, Clone, PartialEq)]
struct SignupInfo {
    invite_required: bool,
    statement: Statement,
}
const EXE: &str = "WorldOfModernWarfare.exe";
/// The published version the install is at (the manifest's `version`), which the game puts on
/// its bug reports (`WOMW_VERSION`).
const VERSION_FILE: &str = "version.txt";

/// What the server has: every file of the game, by its path under the install.
#[derive(Debug, Deserialize, Serialize, Clone, PartialEq)]
struct Manifest {
    version: String,
    files: Vec<Entry>,
}

#[derive(Debug, Deserialize, Serialize, Clone, PartialEq)]
struct Entry {
    path: String,
    size: u64,
    sha256: String,
}

/// What this install has: per file, its size and change time when it was last checked, and the
/// hash it had then (so a 17 GB install isn't hashed on every start).
#[derive(Debug, Default, Deserialize, Serialize)]
struct State {
    files: BTreeMap<String, Known>,
}

#[derive(Debug, Deserialize, Serialize, Clone, PartialEq)]
struct Known {
    size: u64,
    modified: u64,
    sha256: String,
}

fn main() {
    if let Err(e) = run() {
        eprintln!("\nSomething went wrong: {e}");
        pause();
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    println!("World of Modern Warfare\n");
    let install = install_dir()?;
    fs::create_dir_all(&install).map_err(|e| format!("can't make {}: {e}", install.display()))?;
    let _ = fs::remove_file(install.join(format!("{EXE}.old")));
    settle_in(&install)?;
    // Launchers before accounts kept an invite code; nothing takes one any more.
    let _ = fs::remove_file(install.join("invite.txt"));
    // A kept password the server turns down (on any request) is forgotten and asked for again.
    // The account's legal-access statement is settled before anything downloads.
    let version = loop {
        login(&install)?;
        match attest(&install)
            .and_then(|()| update(&install))
            .and_then(|v| update_hd(&install).map(|()| v))
        {
            Err(e) if e == LOGIN_REJECTED => forget_password(&install),
            result => break result?,
        }
    };
    // A test run stops here (AW_LAUNCHER_NO_PLAY).
    if std::env::var_os("AW_LAUNCHER_NO_PLAY").is_some() {
        return Ok(());
    }
    // A crash report a run left while this window was closed.
    crash::offer(&install, HOST, &authorization(), &version, None);
    play(&install, &version)
}

fn install_dir() -> Result<PathBuf, String> {
    let base = std::env::var_os("LOCALAPPDATA").ok_or("no LOCALAPPDATA")?;
    Ok(PathBuf::from(base).join("WorldOfModernWarfare"))
}

/// The first run, from wherever it was downloaded: copy into the install and put a shortcut on
/// the desktop.
fn settle_in(install: &Path) -> Result<(), String> {
    let me = std::env::current_exe().map_err(|e| e.to_string())?;
    let home = install.join(EXE);
    if same_file(&me, &home) {
        return Ok(());
    }
    println!("Installing to {}", install.display());
    fs::copy(&me, &home).map_err(|e| format!("can't copy myself: {e}"))?;
    let script = format!(
        "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\\World of Modern Warfare.lnk');\
         $s.TargetPath='{}';$s.WorkingDirectory='{}';$s.Save()",
        home.display(),
        install.display()
    );
    let _ = Command::new("powershell")
        .args(["-NoProfile", "-Command", &script])
        .status();
    println!("Added \"World of Modern Warfare\" to your desktop.\n");
    Ok(())
}

fn same_file(a: &Path, b: &Path) -> bool {
    match (fs::canonicalize(a), fs::canonicalize(b)) {
        (Ok(a), Ok(b)) => a == b,
        _ => false,
    }
}

/// The game account the downloads go with: the name this install kept (else asked for), the
/// password it kept encrypted (else asked for), tried against the server before anything else.
/// A new player (no name kept, or a login turned down) can sign up with an invite code instead.
fn login(install: &Path) -> Result<(), String> {
    let name_file = install.join("account.txt");
    let password_file = install.join("password.dat");
    let mut name = fs::read_to_string(&name_file)
        .map(|n| n.trim().to_string())
        .unwrap_or_default();
    let mut offer_sign_up = name.is_empty();
    let mut first = true;
    let mut info: Option<SignupInfo> = None;
    loop {
        let kept = fs::read(&password_file)
            .ok()
            .and_then(|sealed| secret::open(&sealed))
            .filter(|_| !name.is_empty());
        let typed = kept.is_none();
        let password = match kept {
            Some(p) => p,
            None => {
                // Whether a sign-up needs an invite code, and the statement: asked once, when a
                // sign-up is offered.
                if offer_sign_up && info.is_none() {
                    info = Some(signup_info(HOST));
                }
                let needs_code = info.as_ref().is_none_or(|i| i.invite_required);
                let statement = info
                    .as_ref()
                    .map(|i| i.statement.clone())
                    .unwrap_or_default();
                if first {
                    if !offer_sign_up {
                        println!("Log in with your game account.");
                    } else if needs_code {
                        println!(
                            "New player: type the invite code you were given.\n\
                             Already have an account: just press Enter and log in."
                        );
                    } else {
                        println!(
                            "New player: type N to make your account (it's free).\n\
                             Already have an account: just press Enter and log in."
                        );
                    }
                    first = false;
                }
                let mut made = None;
                while offer_sign_up && made.is_none() {
                    let code = if needs_code {
                        let code = ask("Invite code (Enter if you have an account): ")?;
                        if code.is_empty() {
                            break;
                        }
                        code
                    } else {
                        let answer = ask("New account? N for new, Enter to log in: ")?;
                        if !wants_new_account(&answer) {
                            break;
                        }
                        String::new()
                    };
                    made = sign_up(&code, &statement)?;
                }
                match made {
                    Some((n, p)) => {
                        name = n;
                        p
                    }
                    None => {
                        name = ask_name(&name)?;
                        rpassword::prompt_password("Password: ").map_err(|e| e.to_string())?
                    }
                }
            }
        };
        set_account(&name, &password);
        match files_request("HEAD", "manifest.json").call() {
            Ok(_) => {
                fs::write(&name_file, &name).map_err(|e| e.to_string())?;
                if typed {
                    match secret::seal(&password) {
                        Some(sealed) if fs::write(&password_file, &sealed).is_ok() => {
                            println!(
                                "Logged in. (The password is kept encrypted for your Windows account.)\n"
                            )
                        }
                        _ => println!("Logged in.\n"),
                    }
                }
                return Ok(());
            }
            Err(e) => {
                let e = failure(e);
                if e != LOGIN_REJECTED {
                    return Err(e);
                }
                println!("\n{LOGIN_REJECTED}.\n");
                let _ = fs::remove_file(&password_file);
                offer_sign_up = true;
            }
        }
    }
}

/// Sign up, with an invite code when the server needs one (`code` empty: none): the player
/// confirms the statement, then the server makes the account, records the confirmation and uses
/// any code up. The new name and password, or None when the player didn't confirm or the server
/// turned the code down (the player can try again or log in instead).
fn sign_up(code: &str, statement: &Statement) -> Result<Option<(String, String)>, String> {
    if !confirm_statement(statement)? {
        println!("No account was made: signing up needs that statement confirmed.\n");
        return Ok(None);
    }
    println!("\nPick your account (you'll log in with it in the game too).");
    loop {
        let name = ask_name("")?;
        let password = loop {
            let p = rpassword::prompt_password("Password (6-16 characters, no spaces): ")
                .map_err(|e| e.to_string())?;
            if !valid_new_password(&p) {
                println!("Passwords are 6-16 letters, digits or symbols, no spaces.\n");
                continue;
            }
            let again =
                rpassword::prompt_password("Password again: ").map_err(|e| e.to_string())?;
            if p != again {
                println!("The passwords don't match.\n");
                continue;
            }
            break p;
        };
        match register(HOST, code, &name, &password, statement.version) {
            Ok(()) => {
                println!("Account {name} made.\n");
                return Ok(Some((name, password)));
            }
            // The code: unknown, used, cancelled or missing.
            Err((403, message)) => {
                println!("{message}\n");
                return Ok(None);
            }
            // The name or password: the code (if any) is still good, pick again.
            Err((400 | 409, message)) => println!("{message}\n"),
            Err((_, message)) => return Err(message),
        }
    }
}

/// Whether the server at `host` needs an invite code to sign up, and its statement
/// (`GET /api/signup-info`). A server too old to say needs a code, as it always did.
fn signup_info(host: &str) -> SignupInfo {
    let v = ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(10))
        .timeout_read(Duration::from_secs(10))
        .build()
        .get(&format!("{host}/api/signup-info"))
        .call()
        .ok()
        .and_then(|r| r.into_json::<serde_json::Value>().ok())
        .unwrap_or_default();
    SignupInfo {
        invite_required: v["invite_required"].as_bool().unwrap_or(true),
        statement: Statement::from_json(&v["attestation"]),
    }
}

/// Show the statement and ask for Y: whether the player typed it.
fn confirm_statement(statement: &Statement) -> Result<bool, String> {
    println!(
        "\nTo play, you must confirm this statement:\n\n    {}\n",
        statement.text
    );
    Ok(confirms(&ask("Type Y to confirm: ")?))
}

/// A Y (any case), and nothing else.
fn confirms(answer: &str) -> bool {
    answer.trim().eq_ignore_ascii_case("y")
}

/// The account's legal-access statement, settled before anything downloads: an account that
/// hasn't confirmed the current statement (a sign-up already did) is asked once, and the server
/// records the Y. A server too old to record it (no `/api/attest`) gets the Y once per install.
fn attest(install: &Path) -> Result<(), String> {
    match attestation_status(HOST, &authorization())? {
        Some((true, _)) => Ok(()),
        Some((false, statement)) => {
            if !confirm_statement(&statement)? {
                return Err(STATEMENT_REFUSED.into());
            }
            post_attestation(HOST, &authorization(), statement.version)?;
            println!("Thanks: confirmed.\n");
            Ok(())
        }
        None => {
            let file = install.join(ATTESTATION_FILE);
            let kept = fs::read_to_string(&file).unwrap_or_default();
            if kept.trim() == ATTESTATION_VERSION.to_string() {
                return Ok(());
            }
            if !confirm_statement(&Statement::default())? {
                return Err(STATEMENT_REFUSED.into());
            }
            let _ = fs::write(&file, ATTESTATION_VERSION.to_string());
            println!();
            Ok(())
        }
    }
}

/// Why the launcher stops when the player doesn't type Y.
const STATEMENT_REFUSED: &str =
    "the game can't be downloaded or played without confirming that statement";

fn attest_agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(20))
        .timeout_read(Duration::from_secs(30))
        .build()
}

/// `GET /api/attest` with the account: whether it confirmed the server's statement, and that
/// statement; None from a server that doesn't record them (404).
fn attestation_status(
    host: &str,
    authorization: &str,
) -> Result<Option<(bool, Statement)>, String> {
    match attest_agent()
        .get(&format!("{host}/api/attest"))
        .set("Authorization", authorization)
        .call()
    {
        Ok(r) => {
            let v: serde_json::Value = r.into_json().map_err(|e| format!("bad answer: {e}"))?;
            Ok(Some((
                v["attested"].as_bool().unwrap_or(false),
                Statement::from_json(&v),
            )))
        }
        Err(ureq::Error::Status(404, _)) => Ok(None),
        Err(e) => Err(failure(e)),
    }
}

/// `POST /api/attest` with the account: it confirms statement `version`.
fn post_attestation(host: &str, authorization: &str, version: u32) -> Result<(), String> {
    attest_agent()
        .post(&format!("{host}/api/attest"))
        .set("Authorization", authorization)
        .send_json(serde_json::json!({ "attest": true, "version": version }))
        .map(|_| ())
        .map_err(|e| match e {
            ureq::Error::Status(400, r) => r
                .into_json::<serde_json::Value>()
                .ok()
                .and_then(|v| v["message"].as_str().map(str::to_string))
                .unwrap_or_else(|| "the server didn't take the confirmation".into()),
            e => failure(e),
        })
}

/// A yes to "New account?": N (new) or Y, any case.
fn wants_new_account(answer: &str) -> bool {
    matches!(
        answer.trim().to_ascii_lowercase().as_str(),
        "n" | "new" | "y" | "yes"
    )
}

/// `POST /api/signup` on the server at `host` (an older server's `/api/register` takes the same),
/// with the player's confirmation of statement `attested`: Ok, or the status (0: no answer) and
/// what the server said.
fn register(
    host: &str,
    code: &str,
    name: &str,
    password: &str,
    attested: u32,
) -> Result<(), (u16, String)> {
    let reply = ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(20))
        .timeout_read(Duration::from_secs(30))
        .build()
        .post(&format!("{host}/api/signup"))
        .send_json(serde_json::json!({
            "code": code,
            "name": name,
            "password": password,
            "attest": true,
            "attestation_version": attested,
        }));
    match reply {
        Ok(_) => Ok(()),
        Err(ureq::Error::Status(status, r)) => {
            let message = r
                .into_json::<serde_json::Value>()
                .ok()
                .and_then(|v| v["message"].as_str().map(str::to_string))
                .unwrap_or_else(|| format!("the server said no (error {status})"));
            Err((status, message))
        }
        Err(e) => Err((0, format!("can't reach the server: {e}"))),
    }
}

fn valid_new_password(p: &str) -> bool {
    (6..=16).contains(&p.len()) && p.bytes().all(|b| (0x21..=0x7e).contains(&b))
}

/// The account name, asked for; Enter keeps `current` when there is one.
fn ask_name(current: &str) -> Result<String, String> {
    loop {
        let prompt = if current.is_empty() {
            "Account name: ".to_string()
        } else {
            format!("Account name [{current}]: ")
        };
        let typed = ask(&prompt)?;
        let name = if typed.is_empty() {
            current.to_string()
        } else {
            typed
        };
        if valid_name(&name) {
            return Ok(name);
        }
        println!("Account names are 3-16 letters or digits.\n");
    }
}

fn valid_name(name: &str) -> bool {
    (3..=16).contains(&name.len()) && name.bytes().all(|b| b.is_ascii_alphanumeric())
}

/// The server turned the kept password down (a 401 mid-update: it was changed): drop it, so
/// `login` asks again.
fn forget_password(install: &Path) {
    let _ = fs::remove_file(install.join("password.dat"));
    set_account("", "");
    println!("\n{LOGIN_REJECTED}.\n");
}

/// The password kept between runs, sealed with Windows' DPAPI for this Windows user: the file is
/// no use on another PC or to another user, and never holds the password in plain text.
mod secret {
    #[cfg(windows)]
    mod dpapi {
        use std::ptr::{null, null_mut};
        use windows_sys::Win32::Foundation::LocalFree;
        use windows_sys::Win32::Security::Cryptography::{
            CRYPT_INTEGER_BLOB, CRYPTPROTECT_UI_FORBIDDEN, CryptProtectData, CryptUnprotectData,
        };

        fn run(data: &[u8], seal: bool) -> Option<Vec<u8>> {
            let input = CRYPT_INTEGER_BLOB {
                cbData: u32::try_from(data.len()).ok()?,
                pbData: data.as_ptr() as *mut u8,
            };
            let mut output = CRYPT_INTEGER_BLOB {
                cbData: 0,
                pbData: null_mut(),
            };
            // SAFETY: the input outlives the call; the output is copied out, then freed with
            // LocalFree as the API asks.
            unsafe {
                let ok = if seal {
                    CryptProtectData(
                        &input,
                        null(),
                        null(),
                        null(),
                        null(),
                        CRYPTPROTECT_UI_FORBIDDEN,
                        &mut output,
                    )
                } else {
                    CryptUnprotectData(
                        &input,
                        null_mut(),
                        null(),
                        null(),
                        null(),
                        CRYPTPROTECT_UI_FORBIDDEN,
                        &mut output,
                    )
                };
                if ok == 0 || output.pbData.is_null() {
                    return None;
                }
                let out =
                    std::slice::from_raw_parts(output.pbData, output.cbData as usize).to_vec();
                LocalFree(output.pbData as _);
                Some(out)
            }
        }

        pub fn seal(data: &[u8]) -> Option<Vec<u8>> {
            run(data, true)
        }

        pub fn open(data: &[u8]) -> Option<Vec<u8>> {
            run(data, false)
        }
    }

    /// The password sealed for this Windows user, or None (then it isn't kept).
    pub fn seal(password: &str) -> Option<Vec<u8>> {
        #[cfg(windows)]
        return dpapi::seal(password.as_bytes());
        #[cfg(not(windows))]
        {
            let _ = password;
            None
        }
    }

    /// A kept password, or None when there is none this user can open.
    pub fn open(sealed: &[u8]) -> Option<String> {
        #[cfg(windows)]
        return dpapi::open(sealed)
            .and_then(|p| String::from_utf8(p).ok())
            .filter(|p| !p.is_empty());
        #[cfg(not(windows))]
        {
            let _ = sealed;
            None
        }
    }
}

fn ask(prompt: &str) -> Result<String, String> {
    print!("{prompt}");
    io::stdout().flush().ok();
    let mut line = String::new();
    let read = io::stdin()
        .lock()
        .read_line(&mut line)
        .map_err(|e| e.to_string())?;
    if read == 0 {
        return Err("no input (the launcher needs a console to ask in)".into());
    }
    Ok(line.trim().to_string())
}

/// A file under the server's /files, with the account: a stalled connection gives up after a
/// minute (and the download resumes), however long the file takes.
fn get(path: &str) -> ureq::Request {
    files_request("GET", path)
}

fn files_request(method: &str, path: &str) -> ureq::Request {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(20))
        .timeout_read(Duration::from_secs(60))
        .build()
        .request(method, &format!("{HOST}/files/{}", url_path(path)))
        .set("Authorization", &authorization())
}

/// Bring the install up to the server's manifest: fetch what's missing or changed. The version
/// it is at.
fn update(install: &Path) -> Result<String, String> {
    println!("Checking for updates...");
    let manifest: Manifest = get("manifest.json")
        .call()
        .map_err(failure)?
        .into_json()
        .map_err(|e| format!("bad manifest: {e}"))?;
    // Kept for the game's bug reports; a version that can't be written is only left out of them.
    let _ = fs::write(install.join(VERSION_FILE), &manifest.version);
    let todo = stale(install, &manifest)?;
    if todo.is_empty() {
        println!("Up to date (version {}).\n", manifest.version);
        return Ok(manifest.version);
    }
    fetch(install, &todo)?;
    println!("\nUpdated to version {}.\n", manifest.version);
    Ok(manifest.version)
}

/// The server's optional HD pack (`hd-manifest.json`, files under `hd/`): offered once with its
/// size when it isn't installed, kept current when it is. Its own manifest (`hd/womw-hd.txt`)
/// downloads last, so the game never mounts half a pack. A server without one, or any failure
/// but the account's, leaves the game as it is: the pack is optional.
fn update_hd(install: &Path) -> Result<(), String> {
    let manifest: Manifest = match get(HD_MANIFEST).call() {
        Ok(r) => match r.into_json() {
            Ok(m) => m,
            Err(e) => {
                println!("(The HD pack's list is unreadable, skipped: {e}.)\n");
                return Ok(());
            }
        },
        Err(ureq::Error::Status(404, _)) => return Ok(()),
        Err(e) => {
            let e = failure(e);
            if e == LOGIN_REJECTED {
                return Err(e);
            }
            println!("(The HD pack could not be checked, skipped: {e}.)\n");
            return Ok(());
        }
    };
    let installed = install.join(HD_PACK_DONE).exists();
    let asked_again = std::env::args().any(|a| a == "--hd");
    if !installed {
        if install.join(HD_DECLINED).exists() && !asked_again {
            return Ok(());
        }
        let total: u64 = manifest.files.iter().map(|e| e.size).sum();
        println!(
            "Optional: the HD pack, higher-resolution textures, models and sounds ({}).\n\
             Switch it on and off in the game: Options > Graphics > HD Textures and Models.",
            human(total)
        );
        if !confirms(&ask("Download HD pack? [y/N] ")?) {
            let _ = fs::write(install.join(HD_DECLINED), "no\n");
            println!("Skipped. To get it later, run the launcher with --hd.\n");
            return Ok(());
        }
        let _ = fs::remove_file(install.join(HD_DECLINED));
    }
    let mut todo = stale(install, &manifest)?;
    // `fetch` takes the pack's manifest down before replacing an archive: it comes down again.
    if !todo.is_empty() && !todo.iter().any(|e| e.path.eq_ignore_ascii_case(HD_PACK_DONE)) {
        todo.extend(manifest.files.iter().filter(|e| e.path.eq_ignore_ascii_case(HD_PACK_DONE)).cloned());
    }
    let todo = hd_order(todo);
    if todo.is_empty() {
        return Ok(());
    }
    if let Err(e) = fetch(install, &todo) {
        if final_failure(&e) {
            return Err(e);
        }
        println!("\nThe HD pack download stopped ({e}); it resumes next time.\n");
        return Ok(());
    }
    println!("\nHD pack installed ({}).\n", manifest.version);
    Ok(())
}

/// The pack's own manifest last: what the game checks before it mounts anything. An archive
/// being replaced takes the manifest down first, so the game never mounts a half-written one.
fn hd_order(mut todo: Vec<Entry>) -> Vec<Entry> {
    todo.sort_by_key(|e| e.path.eq_ignore_ascii_case(HD_PACK_DONE));
    todo
}

/// The manifest's files this install lacks or holds stale.
fn stale(install: &Path, manifest: &Manifest) -> Result<Vec<Entry>, String> {
    let state = read_state(install);
    let mut todo = Vec::new();
    for entry in &manifest.files {
        // A path that would land outside the install (absolute, a drive, `..`) is refused: the
        // manifest names files in the install and nothing else.
        if !inside_install(&entry.path) {
            return Err(format!("bad manifest: the path {:?} isn't inside the install", entry.path));
        }
        let local = install.join(&entry.path);
        let seen = fs::metadata(&local).ok().map(|m| (m.len(), modified(&m)));
        if !current(entry, seen, state.files.get(&entry.path)) {
            todo.push(entry.clone());
        }
    }
    Ok(todo)
}

fn read_state(install: &Path) -> State {
    fs::read(install.join("state.json"))
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_default()
}

/// Download `todo`, recording each finished file in `state.json`.
fn fetch(install: &Path, todo: &[Entry]) -> Result<(), String> {
    let state_file = install.join("state.json");
    let mut state = read_state(install);
    // Replacing any part of the HD pack takes its manifest down first ([`hd_order`]).
    if todo.iter().any(|e| e.path.starts_with("hd/")) {
        let _ = fs::remove_file(install.join(HD_PACK_DONE));
    }
    let total: u64 = todo.iter().map(|e| e.size).sum();
    println!("Downloading {} file(s), {} ...", todo.len(), human(total));
    let mut progress = Progress::new(total);
    for entry in todo {
        let to = install.join(&entry.path);
        // The launcher replaces itself: the running exe moves aside, the new one takes its name.
        let self_update = entry.path == EXE;
        let target = if self_update {
            install.join(format!("{EXE}.new"))
        } else {
            to.clone()
        };
        download(entry, &target, &mut progress)?;
        if self_update {
            let _ = fs::rename(&to, install.join(format!("{EXE}.old")));
            fs::rename(&target, &to).map_err(|e| e.to_string())?;
        }
        let m = fs::metadata(&to).map_err(|e| e.to_string())?;
        state.files.insert(
            entry.path.clone(),
            Known {
                size: m.len(),
                modified: modified(&m),
                sha256: entry.sha256.clone(),
            },
        );
        // Kept after every file, so a stopped download keeps what it finished.
        let _ = fs::write(&state_file, serde_json::to_vec(&state).unwrap_or_default());
    }
    Ok(())
}

/// A file is current when it is there at the manifest's size, unchanged since it was checked,
/// and was then the manifest's hash.
fn current(entry: &Entry, seen: Option<(u64, u64)>, known: Option<&Known>) -> bool {
    match (seen, known) {
        (Some((size, modified)), Some(k)) => {
            size == entry.size
                && k.size == size
                && k.modified == modified
                && k.sha256 == entry.sha256
        }
        _ => false,
    }
}

fn modified(m: &fs::Metadata) -> u64 {
    m.modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |d| d.as_secs())
}

/// Fetch one file to `to` by way of `<to>.part`, resuming a part left by a stopped download,
/// and check its hash before it takes the name.
fn download(entry: &Entry, to: &Path, progress: &mut Progress) -> Result<(), String> {
    if let Some(dir) = to.parent() {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let part = PathBuf::from(format!("{}.part", to.display()));
    let mut have = fs::metadata(&part).map(|m| m.len()).unwrap_or(0);
    if have > entry.size {
        have = 0;
    }
    let mut hasher = Sha256::new();
    if have > 0 {
        let mut f = fs::File::open(&part).map_err(|e| e.to_string())?;
        io::copy(
            &mut Read::by_ref(&mut f).take(have),
            &mut HashWriter(&mut hasher),
        )
        .map_err(|e| e.to_string())?;
    }
    progress.add(have);
    let mut tries = 0;
    while have < entry.size {
        let request = get(&entry.path);
        let request = if have > 0 {
            request.set("Range", &format!("bytes={have}-"))
        } else {
            request
        };
        let result = request
            .call()
            .map_err(|e| match e {
                ureq::Error::Status(401 | 403 | 429, _) => failure(e),
                e => e.to_string(),
            })
            .and_then(|r| {
                // A server that ignores the range starts over.
                if have > 0 && r.status() != 206 {
                    return Err("restart".to_string());
                }
                let mut out = fs::OpenOptions::new()
                    .create(true)
                    .append(true)
                    .open(&part)
                    .map_err(|e| e.to_string())?;
                let mut body = r.into_reader();
                let mut buf = vec![0u8; 1 << 16];
                loop {
                    let n = body.read(&mut buf).map_err(|e| e.to_string())?;
                    if n == 0 {
                        return Ok(());
                    }
                    out.write_all(&buf[..n]).map_err(|e| e.to_string())?;
                    hasher.update(&buf[..n]);
                    have += n as u64;
                    progress.add(n as u64);
                }
            });
        match result {
            Ok(()) => {}
            // Not retried: the password is asked for again (or the player waits), and the part
            // kept for the resume.
            Err(e) if final_failure(&e) => return Err(e),
            Err(e) if e == "restart" => {
                progress.sub(have);
                have = 0;
                hasher = Sha256::new();
                let _ = fs::remove_file(&part);
            }
            Err(e) => {
                tries += 1;
                if tries > 5 {
                    return Err(format!("{}: {e}", entry.path));
                }
                std::thread::sleep(Duration::from_secs(2 * tries));
            }
        }
    }
    let got = hex(&hasher.finalize());
    if got != entry.sha256 {
        let _ = fs::remove_file(&part);
        return Err(format!(
            "{} came down damaged; run again to retry",
            entry.path
        ));
    }
    let _ = fs::remove_file(to);
    fs::rename(&part, to).map_err(|e| e.to_string())
}

struct HashWriter<'a>(&'a mut Sha256);

impl Write for HashWriter<'_> {
    fn write(&mut self, b: &[u8]) -> io::Result<usize> {
        self.0.update(b);
        Ok(b.len())
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

struct Progress {
    total: u64,
    done: u64,
    started: Instant,
    shown: Instant,
}

impl Progress {
    fn new(total: u64) -> Self {
        let now = Instant::now();
        Progress {
            total,
            done: 0,
            started: now,
            shown: now - Duration::from_secs(1),
        }
    }
    fn add(&mut self, n: u64) {
        self.done += n;
        if self.shown.elapsed() >= Duration::from_millis(500) || self.done == self.total {
            self.shown = Instant::now();
            let secs = self.started.elapsed().as_secs_f64().max(0.1);
            print!(
                "\r  {:5.1}%  {} of {}  {}/s     ",
                100.0 * self.done as f64 / self.total.max(1) as f64,
                human(self.done),
                human(self.total),
                human((self.done as f64 / secs) as u64)
            );
            io::stdout().flush().ok();
        }
    }
    fn sub(&mut self, n: u64) {
        self.done = self.done.saturating_sub(n);
    }
}

fn human(bytes: u64) -> String {
    let b = bytes as f64;
    if b >= 1e9 {
        format!("{:.2} GB", b / 1e9)
    } else if b >= 1e6 {
        format!("{:.1} MB", b / 1e6)
    } else {
        format!("{:.0} KB", b / 1e3)
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// A path under the install as a URL path: each part percent-encoded, `/` between.
/// Whether a manifest path stays inside the install: relative, no drive or root, no `..`, no
/// empty name, no `:` (an NTFS stream or a drive).
fn inside_install(path: &str) -> bool {
    use std::path::Component;
    !path.is_empty()
        && !path.contains(':')
        && !path.starts_with(['/', '\\'])
        && Path::new(path).components().all(|c| matches!(c, Component::Normal(_)))
        && path.split(['/', '\\']).all(|seg| !seg.is_empty() && seg != ".." && seg != ".")
}

fn url_path(path: &str) -> String {
    path.split(['/', '\\'])
        .map(|seg| {
            seg.bytes()
                .map(|b| match b {
                    b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => {
                        (b as char).to_string()
                    }
                    _ => format!("%{b:02X}"),
                })
                .collect::<String>()
        })
        .collect::<Vec<_>>()
        .join("/")
}

/// Start the game and wait for it: a crash is offered as a report ([`crash::offer`]).
fn play(install: &Path, version: &str) -> Result<(), String> {
    let client = install.join("client");
    let exe = client.join("benilla-mw2.exe");
    if !exe.exists() {
        return Err("the game isn't downloaded; run again".into());
    }
    println!("Starting the game...");
    let started = std::time::SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    // The version the game reports in bug reports: the one update last installed, else this run's.
    let shown = Some(installed_version(install))
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| version.to_string());
    let mut game = Command::new(&exe)
        .current_dir(&client)
        .env("WOMW_VERSION", &shown)
        .env("WOW_DATA", install.join("game").join("WoW").join("Data"))
        .env("MW2_PATH", install.join("game").join("MW2"))
        .env("WOW_HOST", GAME_HOST)
        // Where the game leaves a crash report, and the version it says it is.
        .env("AW_CRASH_DIR", install.join(crash::DIR))
        .env("AW_GAME_VERSION", version)
        // The game's Create Account button on the login screen signs up here.
        .env("AW_SIGNUP_URL", format!("{HOST}/api/signup"))
        // The player's settings (keybinds, options, addon state) beside the game, not in the
        // build machine's project folder a dev-featured build otherwise points at.
        .env("BENILLA_HOME", settings_home(&client))
        .spawn()
        .map_err(|e| format!("can't start the game: {e}"))?;
    println!("(This window shows the game's log; it closes when the game does.)\n");
    let code = game.wait().ok().and_then(|s| s.code());
    if crash::offer(
        install,
        HOST,
        &authorization(),
        version,
        Some((code, started)),
    ) {
        pause();
    }
    Ok(())
}

/// Where earlier versions of the game kept a player's settings, if a server's builds did: the
/// build machine's project folder, baked into a dev-featured build and created on the player's
/// PC. Set at build time (`AW_OLD_SETTINGS_DIR`, from `vps/deploy.env.local`); none by default.
const OLD_SETTINGS: Option<&str> = option_env!("AW_OLD_SETTINGS_DIR");

/// `<client>\benilla-config`, the folder the game is told to keep its settings in. The first time,
/// settings from [`OLD_SETTINGS`] are copied in so nobody loses their keybinds and options.
fn settings_home(client: &Path) -> PathBuf {
    let home = client.join("benilla-config");
    let Some(old) = OLD_SETTINGS.filter(|s| !s.is_empty()).map(Path::new) else {
        return home;
    };
    if !home.exists()
        && old.is_dir()
        && let Err(e) = copy_dir(old, &home)
    {
        eprintln!("(couldn't bring over your old settings: {e})");
    }
    home
}

fn copy_dir(from: &Path, to: &Path) -> io::Result<()> {
    fs::create_dir_all(to)?;
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let dest = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_dir(&entry.path(), &dest)?;
        } else {
            fs::copy(entry.path(), dest)?;
        }
    }
    Ok(())
}

fn pause() {
    print!("Press Enter to close.");
    io::stdout().flush().ok();
    let _ = io::stdin().lock().read_line(&mut String::new());
}

/// The version [`update`] last saw (one line, at most 64 printable characters), or "".
fn installed_version(install: &Path) -> String {
    fs::read_to_string(install.join(VERSION_FILE))
        .map(|v| clean_version(&v))
        .unwrap_or_default()
}

fn clean_version(v: &str) -> String {
    v.chars()
        .take_while(|c| *c != '\n' && *c != '\r')
        .filter(|c| c.is_ascii_graphic() || *c == ' ')
        .take(64)
        .collect::<String>()
        .trim()
        .to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn manifest_paths_stay_inside_the_install() {
        assert!(inside_install("WoW.exe"));
        assert!(inside_install("Data/patch-A.MPQ"));
        assert!(inside_install(r"Interface\AddOns\x.lua"));
        for bad in ["", "../evil.exe", "Data/../../evil.exe", r"Data\..\..\evil.exe", "/etc/x", r"\x",
                    r"C:\Windows\evil.exe", "C:evil.exe", r"\\server\share\x", "a//b", "./a", "WoW.exe:stream"] {
            assert!(!inside_install(bad), "{bad}");
        }
    }

    #[test]
    fn old_settings_are_copied_whole() {
        let tmp = std::env::temp_dir().join(format!("aw-launcher-settings-{}", std::process::id()));
        let _ = fs::remove_dir_all(&tmp);
        let old = tmp.join("old");
        fs::create_dir_all(old.join("bindings")).unwrap();
        fs::write(old.join("config.toml"), "[cvars]\n").unwrap();
        fs::write(old.join("bindings").join("account.txt"), "W MOVEFORWARD\n").unwrap();
        let new = tmp.join("client").join("benilla-config");
        copy_dir(&old, &new).unwrap();
        assert_eq!(fs::read_to_string(new.join("config.toml")).unwrap(), "[cvars]\n");
        assert_eq!(
            fs::read_to_string(new.join("bindings").join("account.txt")).unwrap(),
            "W MOVEFORWARD\n"
        );
        let _ = fs::remove_dir_all(&tmp);
    }

    #[test]
    fn a_401_is_the_account_turned_down() {
        let status = |code| ureq::Error::Status(code, ureq::Response::new(code, "x", "").unwrap());
        assert_eq!(failure(status(401)), LOGIN_REJECTED);
        assert!(LOGIN_REJECTED.starts_with("Wrong account name or password"));
        assert_eq!(failure(status(429)), TOO_MANY_TRIES);
        assert!(final_failure(&failure(status(401))));
        assert!(final_failure(&failure(status(403))));
        assert!(!final_failure(&failure(status(500))));
        assert_ne!(failure(status(500)), LOGIN_REJECTED);
    }

    #[test]
    fn downloads_send_http_basic() {
        // RFC 7617's example.
        assert_eq!(
            basic("Aladdin", "open sesame"),
            "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="
        );
        set_account("Tester", "secret12");
        assert_eq!(authorization(), basic("Tester", "secret12"));
        let request = get("manifest.json");
        assert_eq!(
            request.header("Authorization"),
            Some(basic("Tester", "secret12").as_str())
        );
        assert!(request.url().ends_with("/files/manifest.json"));
        set_account("", "");
    }

    #[test]
    fn account_names() {
        assert!(valid_name("Tester"));
        assert!(valid_name("ab0"));
        assert!(!valid_name("ab"));
        assert!(!valid_name("has space"));
        assert!(!valid_name("a:b"));
        assert!(!valid_name("seventeenletters1"));
        assert!(valid_new_password("Secret!2"));
        assert!(!valid_new_password("short"));
        assert!(!valid_new_password("has space1"));
        assert!(!valid_new_password("seventeen-chars-1"));
    }

    /// A one-request HTTP server: answers `status` with `body`, hands back the request it got.
    pub(crate) fn serve_once(
        status: &str,
        body: &str,
    ) -> (String, std::thread::JoinHandle<String>) {
        use std::net::TcpListener;
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let host = format!("http://{}", listener.local_addr().unwrap());
        let reply = format!(
            "HTTP/1.1 {status}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
            body.len()
        );
        let handle = std::thread::spawn(move || {
            let (mut s, _) = listener.accept().unwrap();
            let mut got = Vec::new();
            let mut buf = [0u8; 4096];
            // Headers, then as much body as Content-Length says.
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
        (host, handle)
    }

    #[test]
    fn sign_up_posts_the_code_name_and_password() {
        let (host, server) = serve_once("201 Created", r#"{"message": "Account NEWBIE made."}"#);
        assert_eq!(
            register(&host, "K7QM-X3PA-W9TR", "Newbie", "Secret12", 1),
            Ok(())
        );
        let request = server.join().unwrap();
        assert!(request.starts_with("POST /api/signup "));
        let body: serde_json::Value =
            serde_json::from_str(request.split("\r\n\r\n").nth(1).unwrap()).unwrap();
        assert_eq!(
            body,
            serde_json::json!({"code": "K7QM-X3PA-W9TR", "name": "Newbie", "password": "Secret12",
                               "attest": true, "attestation_version": 1})
        );
    }

    #[test]
    fn sign_up_says_what_the_server_said() {
        let (host, server) = serve_once(
            "403 Forbidden",
            r#"{"message": "That invite code has already been used."}"#,
        );
        assert_eq!(
            register(&host, "K7QMX3PAW9TR", "Newbie", "Secret12", 1),
            Err((403, "That invite code has already been used.".to_string()))
        );
        server.join().unwrap();
    }

    #[test]
    fn open_sign_up_sends_no_code() {
        let (host, server) = serve_once("201 Created", r#"{"message": "Account NEWBIE made."}"#);
        assert_eq!(register(&host, "", "Newbie", "Secret12", 1), Ok(()));
        let request = server.join().unwrap();
        let body: serde_json::Value =
            serde_json::from_str(request.split("\r\n\r\n").nth(1).unwrap()).unwrap();
        assert_eq!(body["code"], "");
    }

    #[test]
    fn the_server_says_whether_an_invite_is_needed() {
        let (host, server) = serve_once(
            "200 OK",
            r#"{"open": true, "invite_required": false, "name_rule": "x", "password_rule": "y",
                "attestation": {"text": "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009).", "version": 1}}"#,
        );
        let info = signup_info(&host);
        assert!(!info.invite_required);
        assert_eq!(info.statement, Statement::default());
        assert!(server.join().unwrap().starts_with("GET /api/signup-info "));
        let (host, server) = serve_once("200 OK", r#"{"open": true, "invite_required": true}"#);
        assert!(signup_info(&host).invite_required);
        server.join().unwrap();
        // An older server, which doesn't know the question: invites, as before, and the
        // built-in statement.
        let (host, server) = serve_once("404 Not Found", r#"{"message": "not found"}"#);
        let info = signup_info(&host);
        assert!(info.invite_required);
        assert_eq!(info.statement, Statement::default());
        server.join().unwrap();
        assert!(wants_new_account("N"));
        assert!(wants_new_account(" yes "));
        assert!(!wants_new_account(""));
    }

    #[test]
    fn the_statement_is_word_for_word() {
        assert_eq!(
            ATTESTATION_TEXT,
            "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009)."
        );
        assert_eq!(Statement::default().version, 1);
        // The server's own statement wins; a broken one is the built-in.
        let s = Statement::from_json(&serde_json::json!({"text": "Another.", "version": 2}));
        assert_eq!((s.text.as_str(), s.version), ("Another.", 2));
        assert_eq!(
            Statement::from_json(&serde_json::json!({"text": " ", "version": 2})),
            Statement::default()
        );
        assert_eq!(
            Statement::from_json(&serde_json::Value::Null),
            Statement::default()
        );
        assert!(confirms("Y"));
        assert!(confirms(" y "));
        assert!(!confirms(""));
        assert!(!confirms("yes"));
        assert!(!confirms("n"));
    }

    #[test]
    fn an_older_account_is_asked_and_its_yes_recorded() {
        let auth = basic("OldTimer", "Secret12");
        let (host, server) = serve_once(
            "200 OK",
            r#"{"attested": false, "version": 1, "text": "I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009)."}"#,
        );
        assert_eq!(
            attestation_status(&host, &auth),
            Ok(Some((false, Statement::default())))
        );
        let request = server.join().unwrap();
        assert!(request.starts_with("GET /api/attest "));
        assert!(request.contains(&format!("Authorization: {auth}")));

        let (host, server) = serve_once("200 OK", r#"{"attested": true, "version": 1}"#);
        assert_eq!(
            attestation_status(&host, &auth).map(|s| s.map(|s| s.0)),
            Ok(Some(true))
        );
        server.join().unwrap();

        let (host, server) = serve_once("200 OK", r#"{"message": "Recorded."}"#);
        assert_eq!(post_attestation(&host, &auth, 1), Ok(()));
        let request = server.join().unwrap();
        assert!(request.starts_with("POST /api/attest "));
        let body: serde_json::Value =
            serde_json::from_str(request.split("\r\n\r\n").nth(1).unwrap()).unwrap();
        assert_eq!(body, serde_json::json!({"attest": true, "version": 1}));
    }

    #[test]
    fn attestation_answers_from_the_server() {
        let auth = basic("OldTimer", "Secret12");
        // A server that doesn't record attestations: the install asks on its own.
        let (host, server) = serve_once("404 Not Found", r#"{"message": "not found"}"#);
        assert_eq!(attestation_status(&host, &auth), Ok(None));
        server.join().unwrap();
        // A turned-down login is the account's, as for downloads.
        let (host, server) = serve_once("401 Unauthorized", r#"{"message": "no"}"#);
        assert_eq!(
            attestation_status(&host, &auth),
            Err(LOGIN_REJECTED.to_string())
        );
        server.join().unwrap();
        let (host, server) = serve_once(
            "400 Bad Request",
            r#"{"message": "That isn't the current statement."}"#,
        );
        assert_eq!(
            post_attestation(&host, &auth, 7),
            Err("That isn't the current statement.".to_string())
        );
        server.join().unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn the_kept_password_is_sealed() {
        let sealed = secret::seal("p@ss:w0rd!").expect("DPAPI seals");
        assert!(!sealed.windows(10).any(|w| w == b"p@ss:w0rd!"));
        assert_eq!(secret::open(&sealed).as_deref(), Some("p@ss:w0rd!"));
        assert_eq!(secret::open(b"not sealed"), None);
    }

    fn entry(size: u64, sha: &str) -> Entry {
        Entry {
            path: "client/benilla-mw2.exe".into(),
            size,
            sha256: sha.into(),
        }
    }

    fn known(size: u64, modified: u64, sha: &str) -> Known {
        Known {
            size,
            modified,
            sha256: sha.into(),
        }
    }

    #[test]
    fn a_file_is_current_only_unchanged_at_the_manifests_hash() {
        let e = entry(10, "aa");
        assert!(current(&e, Some((10, 5)), Some(&known(10, 5, "aa"))));
        assert!(!current(&e, None, Some(&known(10, 5, "aa")))); // deleted
        assert!(!current(&e, Some((10, 5)), None)); // never checked
        assert!(!current(&e, Some((10, 6)), Some(&known(10, 5, "aa")))); // touched since
        assert!(!current(&e, Some((10, 5)), Some(&known(10, 5, "bb")))); // a new version
        assert!(!current(
            &entry(11, "aa"),
            Some((10, 5)),
            Some(&known(10, 5, "aa"))
        ));
    }

    #[test]
    fn the_hd_packs_manifest_downloads_last() {
        let todo = vec![
            entry(1, "a"),
            Entry { path: HD_PACK_DONE.into(), size: 2, sha256: "m".into() },
            Entry { path: "hd/womw-hd-2.MPQ".into(), size: 3, sha256: "b".into() },
        ];
        let order = hd_order(todo);
        assert_eq!(order.last().unwrap().path, HD_PACK_DONE);
        assert!(inside_install(HD_PACK_DONE) && inside_install("hd/womw-hd-1.MPQ"));
    }

    #[test]
    fn paths_become_url_paths() {
        assert_eq!(
            url_path("game\\WoW\\Data\\dbc.MPQ"),
            "game/WoW/Data/dbc.MPQ"
        );
        assert_eq!(
            url_path("game/MW2/zone/english/a b#.ff"),
            "game/MW2/zone/english/a%20b%23.ff"
        );
    }

    #[test]
    fn sizes_read_plainly() {
        assert_eq!(human(17_300_000_000), "17.30 GB");
        assert_eq!(human(181_000_000), "181.0 MB");
    }

    #[test]
    fn the_installed_version_reaches_the_game_clean() {
        assert_eq!(clean_version("2026-10-18 12:00"), "2026-10-18 12:00");
        assert_eq!(clean_version(" 2026-10-18 12:00\r\njunk"), "2026-10-18 12:00");
        assert_eq!(clean_version("v\x1b[2J1"), "v[2J1");
        assert_eq!(clean_version(&"9".repeat(100)).len(), 64);
        let dir = std::env::temp_dir().join(format!("womw-version-test-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        assert_eq!(installed_version(&dir), "", "none yet");
        fs::write(dir.join(VERSION_FILE), "2026-10-18 12:00\n").unwrap();
        assert_eq!(installed_version(&dir), "2026-10-18 12:00");
        let _ = fs::remove_dir_all(&dir);
    }
}
