//! Crash reports for players: when the game dies, it leaves a report the launcher can send.
//!
//! Two kinds, both written to the crash folder (`$AW_CRASH_DIR`, which the launcher sets to
//! `<install>\crashes`; else `crashes\` beside the exe) as `crash-<unix>.txt`:
//!
//! - **panic**: a hook chained after the others (Rust's printer and benilla's own report in
//!   `benilla-config/Diagnostics`) writes the message, the place, the thread, a backtrace (raw
//!   `module+offset` frames, which the build's PDB turns into functions, and std's symbolised one),
//!   the build, game version, Windows version, graphics adapter and the last log lines.
//! - **native** (Windows): an access violation, stack overflow or any other exception nothing
//!   handled. An unhandled-exception filter hands the crash to a watcher thread started at launch
//!   (the crashed thread may have no stack or a broken heap), which writes the report and a
//!   minidump (`crash-<unix>.dmp`, `MiniDumpWriteDump`), then lets the exception go on to the
//!   filter that was there before (the C runtime's, then Windows' own).
//!
//! An abort (`__fastfail`, a panic while panicking) reaches neither: the launcher still sees the
//! exit code. `AW_TEST_CRASH=panic|access` makes a test crash 5 s after launch, to try this out.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use bevy::prelude::*;

/// Where reports go; the launcher sets it.
pub const CRASH_DIR_VAR: &str = "AW_CRASH_DIR";
/// The game version the launcher installed (the manifest's), for the report.
pub const VERSION_VAR: &str = "AW_GAME_VERSION";
/// `panic` or `access`: a deliberate crash a few seconds in, to test the reporting.
pub const TEST_CRASH_VAR: &str = "AW_TEST_CRASH";
/// Seconds after launch the test crash happens.
const TEST_CRASH_AT: f32 = 5.0;

/// What every report says about this run, fixed at launch.
struct Context {
    build: String,
    game_version: String,
    os: String,
    dir: PathBuf,
    started: Instant,
}

static CONTEXT: OnceLock<Context> = OnceLock::new();
/// The graphics adapter, once the renderer has picked one.
static GPU: OnceLock<String> = OnceLock::new();
/// Only the first crash of a run is reported (and a crash inside the reporter isn't).
static REPORTING: AtomicBool = AtomicBool::new(false);

/// Arms the panic hook and (on Windows) the native crash handler. Called once, first thing in
/// `main`; `build` is the build line (version, commit, profile).
pub fn install(build: String) {
    let dir = crash_dir();
    let context = Context {
        build,
        game_version: std::env::var(VERSION_VAR)
            .ok()
            .filter(|v| !v.trim().is_empty())
            .unwrap_or_else(|| "unknown (not started by the launcher)".into()),
        os: os_version(),
        dir: dir.clone(),
        started: Instant::now(),
    };
    if CONTEXT.set(context).is_err() {
        return;
    }
    let _ = std::fs::create_dir_all(&dir);
    let previous = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        previous(info);
        on_panic(info);
    }));
    #[cfg(windows)]
    native::install();
}

/// `$AW_CRASH_DIR`, else `crashes\` beside the exe.
pub fn crash_dir() -> PathBuf {
    if let Some(dir) = std::env::var_os(CRASH_DIR_VAR).filter(|d| !d.is_empty()) {
        return PathBuf::from(dir);
    }
    std::env::current_exe()
        .ok()
        .and_then(|e| e.parent().map(Path::to_path_buf))
        .unwrap_or_default()
        .join("crashes")
}

/// Records the graphics adapter for reports, and arms the test crash when asked for.
pub struct CrashReportPlugin;

impl Plugin for CrashReportPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, record_gpu.run_if(|| GPU.get().is_none()));
        if let Some(mode) = TestCrash::from_env() {
            app.insert_resource(mode);
            app.add_systems(Update, test_crash);
        }
    }
}

fn record_gpu(info: Option<Res<bevy::render::renderer::RenderAdapterInfo>>) {
    if let Some(info) = info {
        let a = &***info;
        let _ = GPU.set(gpu_line(
            &a.name,
            &format!("{:?}", a.backend),
            &format!("{:?}", a.device_type),
            &a.driver,
            &a.driver_info,
        ));
    }
}

/// The adapter as one line: `NVIDIA GeForce RTX 3070 (Dx12, DiscreteGpu, driver NVIDIA 560.94)`.
fn gpu_line(name: &str, backend: &str, kind: &str, driver: &str, driver_info: &str) -> String {
    let driver = [driver.trim(), driver_info.trim()]
        .into_iter()
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" ");
    if driver.is_empty() {
        format!("{name} ({backend}, {kind})")
    } else {
        format!("{name} ({backend}, {kind}, driver {driver})")
    }
}

/// The hidden test crash (`AW_TEST_CRASH`).
#[derive(Resource, Debug, Clone, Copy, PartialEq, Eq)]
pub enum TestCrash {
    Panic,
    Access,
}

impl TestCrash {
    fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "panic" => Some(Self::Panic),
            "access" => Some(Self::Access),
            _ => None,
        }
    }

    fn from_env() -> Option<Self> {
        Self::parse(&std::env::var(TEST_CRASH_VAR).ok()?)
    }
}

fn test_crash(mode: Res<TestCrash>, time: Res<Time<bevy::time::Real>>) {
    if time.elapsed_secs() < TEST_CRASH_AT {
        return;
    }
    warn!("{TEST_CRASH_VAR}: a deliberate test crash ({:?})", *mode);
    match *mode {
        TestCrash::Panic => panic!("{TEST_CRASH_VAR}=panic: a deliberate test crash"),
        TestCrash::Access => {
            #[cfg(windows)]
            native::raise_access_violation();
            #[cfg(not(windows))]
            std::process::abort();
        }
    }
}

/// Everything a report says, gathered so rendering is pure.
struct Report<'a> {
    kind: &'a str,
    unix: u64,
    uptime: f64,
    game_version: &'a str,
    build: &'a str,
    os: &'a str,
    gpu: &'a str,
    exe: &'a str,
    thread: &'a str,
    /// The kind's own lines, `(label, value)`: `at` and `panic`, or `exception` and `address`.
    details: Vec<(&'a str, String)>,
    frames: &'a [String],
    backtrace: &'a str,
    log: &'a [String],
}

/// The report's text. The first lines are `label: value` (the gate reads `kind`, `at`, `panic`,
/// `exception` and `address` from them); a multi-line value goes on indented.
fn render(r: &Report<'_>) -> String {
    let mut out = String::new();
    let _ = writeln!(out, "World of Modern Warfare crash report");
    let mut line = |label: &str, value: &str| {
        let mut lines = value.lines();
        let _ = writeln!(
            out,
            "{:<10} {}",
            format!("{label}:"),
            lines.next().unwrap_or("")
        );
        for more in lines {
            let _ = writeln!(out, "           {more}");
        }
    };
    line("kind", r.kind);
    line(
        "time",
        &format!("{} unix, {:.1} s after launch", r.unix, r.uptime),
    );
    line("game", r.game_version);
    line("build", r.build);
    line("os", r.os);
    line("gpu", r.gpu);
    line("exe", r.exe);
    line("thread", r.thread);
    for (label, value) in &r.details {
        line(label, value);
    }
    if !r.frames.is_empty() {
        let _ = writeln!(
            out,
            "\nframes (module+offset; symbolise with the build's PDB):"
        );
        for (i, f) in r.frames.iter().enumerate() {
            let _ = writeln!(out, "  {i:3}  {f}");
        }
    }
    if !r.backtrace.trim().is_empty() {
        let _ = writeln!(out, "\nbacktrace:\n{}", r.backtrace.trim_end());
    }
    if !r.log.is_empty() {
        let _ = writeln!(out, "\n{}", log_heading(r.log.len()));
        for l in r.log {
            let _ = writeln!(out, "{l}");
        }
    }
    out
}

fn log_heading(n: usize) -> String {
    format!("last {n} log lines (oldest first):")
}

fn unix_now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn report_path(dir: &Path, unix: u64, ext: &str) -> PathBuf {
    dir.join(format!("crash-{unix}.{ext}"))
}

fn on_panic(info: &std::panic::PanicHookInfo<'_>) {
    if REPORTING.swap(true, Ordering::SeqCst) {
        return;
    }
    let Some(ctx) = CONTEXT.get() else {
        return;
    };
    let payload = if let Some(s) = info.payload().downcast_ref::<&str>() {
        (*s).to_owned()
    } else if let Some(s) = info.payload().downcast_ref::<String>() {
        s.clone()
    } else {
        "<non-string panic payload>".to_owned()
    };
    let location = info
        .location()
        .map(|l| format!("{}:{}:{}", l.file(), l.line(), l.column()))
        .unwrap_or_else(|| "<unknown>".to_owned());
    let thread = std::thread::current();
    let frames = stack_frames();
    let unix = unix_now();
    let text = render(&Report {
        kind: "panic",
        unix,
        uptime: ctx.started.elapsed().as_secs_f64(),
        game_version: &ctx.game_version,
        build: &ctx.build,
        os: &ctx.os,
        gpu: GPU.get().map_or("unknown", String::as_str),
        exe: &exe_line(),
        thread: thread.name().unwrap_or("<unnamed>"),
        details: vec![("at", location), ("panic", payload)],
        frames: &frames,
        backtrace: &std::backtrace::Backtrace::force_capture().to_string(),
        log: &benilla_world::log_ring::recent(),
    });
    let path = report_path(&ctx.dir, unix, "txt");
    let _ = std::fs::create_dir_all(&ctx.dir);
    if std::fs::write(&path, text).is_ok() {
        eprintln!("crash report written to {}", path.display());
    }
}

/// The exe's file name and where it is loaded (frames are offsets from there).
fn exe_line() -> String {
    let name = std::env::current_exe()
        .ok()
        .and_then(|e| e.file_name().map(|n| n.to_string_lossy().into_owned()))
        .unwrap_or_else(|| "?".into());
    #[cfg(windows)]
    {
        format!("{name}, loaded at 0x{:x}", native::exe_base())
    }
    #[cfg(not(windows))]
    {
        name
    }
}

/// This thread's return addresses as `module+0xoffset` (raw: a player's PC has no PDB).
fn stack_frames() -> Vec<String> {
    #[cfg(windows)]
    {
        native::stack()
            .into_iter()
            .map(native::module_offset)
            .collect()
    }
    #[cfg(not(windows))]
    {
        Vec::new()
    }
}

fn os_version() -> String {
    #[cfg(windows)]
    {
        native::os_version()
    }
    #[cfg(not(windows))]
    {
        format!("{} {}", std::env::consts::OS, std::env::consts::ARCH)
    }
}

/// Windows-only parts, on the system's own exports (kernel32, ntdll, dbghelp): no new crates.
#[cfg(windows)]
mod native {
    use super::*;
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;
    use std::ptr::{null, null_mut};
    use std::sync::atomic::{AtomicPtr, AtomicU32, AtomicUsize};

    type Handle = *mut c_void;
    type Filter = unsafe extern "system" fn(*mut ExceptionPointers) -> i32;

    #[repr(C)]
    pub struct ExceptionRecord {
        code: u32,
        flags: u32,
        record: *mut ExceptionRecord,
        address: *mut c_void,
        parameters: u32,
        information: [usize; 15],
    }

    #[repr(C)]
    pub struct ExceptionPointers {
        record: *mut ExceptionRecord,
        context: *mut c_void,
    }

    /// dbghelp.h's MINIDUMP_EXCEPTION_INFORMATION, which is packed to 4.
    #[repr(C, packed(4))]
    struct MinidumpExceptionInformation {
        thread_id: u32,
        pointers: *mut ExceptionPointers,
        client_pointers: i32,
    }

    #[repr(C)]
    struct OsVersionInfo {
        size: u32,
        major: u32,
        minor: u32,
        build: u32,
        platform: u32,
        service_pack: [u16; 128],
    }

    #[link(name = "kernel32", kind = "raw-dylib")]
    unsafe extern "system" {
        fn SetUnhandledExceptionFilter(filter: Option<Filter>) -> Option<Filter>;
        fn CreateEventW(
            attributes: *const c_void,
            manual: i32,
            initial: i32,
            name: *const u16,
        ) -> Handle;
        fn SetEvent(event: Handle) -> i32;
        fn WaitForSingleObject(handle: Handle, ms: u32) -> u32;
        fn GetCurrentProcess() -> Handle;
        fn GetCurrentProcessId() -> u32;
        fn GetCurrentThreadId() -> u32;
        fn CreateFileW(
            name: *const u16,
            access: u32,
            share: u32,
            security: *const c_void,
            disposition: u32,
            flags: u32,
            template: Handle,
        ) -> Handle;
        fn CloseHandle(handle: Handle) -> i32;
        fn RaiseException(code: u32, flags: u32, count: u32, arguments: *const usize);
        fn RtlCaptureStackBackTrace(
            skip: u32,
            count: u32,
            frames: *mut *mut c_void,
            hash: *mut u32,
        ) -> u16;
        fn GetModuleHandleExW(flags: u32, name: *const u16, module: *mut Handle) -> i32;
        fn GetModuleHandleW(name: *const u16) -> Handle;
        fn GetModuleFileNameW(module: Handle, name: *mut u16, size: u32) -> u32;
    }

    #[link(name = "ntdll", kind = "raw-dylib")]
    unsafe extern "system" {
        fn RtlGetVersion(info: *mut OsVersionInfo) -> i32;
    }

    #[link(name = "dbghelp", kind = "raw-dylib")]
    unsafe extern "system" {
        fn MiniDumpWriteDump(
            process: Handle,
            pid: u32,
            file: Handle,
            kind: u32,
            exception: *const MinidumpExceptionInformation,
            user_streams: *const c_void,
            callback: *const c_void,
        ) -> i32;
    }

    const INFINITE: u32 = u32::MAX;
    const EXCEPTION_CONTINUE_SEARCH: i32 = 0;
    const EXCEPTION_ACCESS_VIOLATION: u32 = 0xC000_0005;
    const EXCEPTION_NONCONTINUABLE: u32 = 1;
    const GENERIC_WRITE: u32 = 0x4000_0000;
    const CREATE_ALWAYS: u32 = 2;
    const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
    const FROM_ADDRESS: u32 = 0x4;
    const UNCHANGED_REFCOUNT: u32 = 0x2;
    /// Normal (every thread's registers and stack, the module list) + unloaded modules + thread
    /// info: enough to walk every thread's stack, about a MB. No heap: a player's dump should hold
    /// as little of what the game had in memory as still makes it useful.
    const DUMP_KIND: u32 = 0x20 | 0x1000;
    /// How long the crashed thread waits for the report and dump.
    const WAIT_MS: u32 = 30_000;

    static CRASHED: AtomicPtr<ExceptionPointers> = AtomicPtr::new(null_mut());
    static THREAD: AtomicU32 = AtomicU32::new(0);
    static CRASH_EVENT: AtomicUsize = AtomicUsize::new(0);
    static DONE_EVENT: AtomicUsize = AtomicUsize::new(0);
    static PREVIOUS: AtomicUsize = AtomicUsize::new(0);
    static ENTERED: AtomicBool = AtomicBool::new(false);

    /// Starts the watcher and sets the filter; without a watcher no filter is set.
    pub fn install() {
        // SAFETY: plain Win32 calls with valid (null) arguments.
        let (crash, done) = unsafe {
            (
                CreateEventW(null(), 1, 0, null()),
                CreateEventW(null(), 1, 0, null()),
            )
        };
        if crash.is_null() || done.is_null() {
            return;
        }
        CRASH_EVENT.store(crash as usize, Ordering::SeqCst);
        DONE_EVENT.store(done as usize, Ordering::SeqCst);
        let watcher = std::thread::Builder::new()
            .name("aw-crash-watch".into())
            .spawn(|| {
                // SAFETY: the event lives for the process.
                unsafe {
                    WaitForSingleObject(CRASH_EVENT.load(Ordering::SeqCst) as Handle, INFINITE)
                };
                write_native();
                // SAFETY: as above.
                unsafe { SetEvent(DONE_EVENT.load(Ordering::SeqCst) as Handle) };
            });
        if watcher.is_err() {
            return;
        }
        // SAFETY: `filter` has the signature Windows calls; the old one is kept to chain to.
        let previous = unsafe { SetUnhandledExceptionFilter(Some(filter)) };
        PREVIOUS.store(previous.map_or(0, |f| f as usize), Ordering::SeqCst);
    }

    /// On the crashed thread: no allocation, no locks; hand over and wait.
    unsafe extern "system" fn filter(pointers: *mut ExceptionPointers) -> i32 {
        if !ENTERED.swap(true, Ordering::SeqCst) {
            CRASHED.store(pointers, Ordering::SeqCst);
            // SAFETY: plain Win32 calls on events made at install.
            unsafe {
                THREAD.store(GetCurrentThreadId(), Ordering::SeqCst);
                SetEvent(CRASH_EVENT.load(Ordering::SeqCst) as Handle);
                WaitForSingleObject(DONE_EVENT.load(Ordering::SeqCst) as Handle, WAIT_MS);
            }
        }
        match PREVIOUS.load(Ordering::SeqCst) {
            0 => EXCEPTION_CONTINUE_SEARCH,
            // SAFETY: the value came from SetUnhandledExceptionFilter as a `Filter`.
            previous => unsafe { std::mem::transmute::<usize, Filter>(previous)(pointers) },
        }
    }

    /// On the watcher: the report first (small, most likely to make it), then the dump, then the
    /// log tail (its lock may be held by the crashed thread; the filter's wait bounds that).
    fn write_native() {
        let Some(ctx) = CONTEXT.get() else {
            return;
        };
        if REPORTING.swap(true, Ordering::SeqCst) {
            return; // a panic report is already being written
        }
        let pointers = CRASHED.load(Ordering::SeqCst);
        let thread = THREAD.load(Ordering::SeqCst);
        // SAFETY: Windows passed these to the filter, which is waiting: they stay valid.
        let record = unsafe { pointers.as_ref().and_then(|p| p.record.as_ref()) };
        let (code, address) = record.map_or((0, 0), |r| (r.code, r.address as usize));
        let mut details = vec![
            (
                "exception",
                format!("0x{code:08X} {}", exception_name(code)),
            ),
            ("address", module_offset(address)),
        ];
        if let Some(r) =
            record.filter(|r| r.code == EXCEPTION_ACCESS_VIOLATION && r.parameters >= 2)
        {
            let op = match r.information[0] {
                0 => "read of",
                1 => "write to",
                8 => "execute at",
                _ => "access to",
            };
            details.push(("access", format!("{op} 0x{:x}", r.information[1])));
        }
        let unix = unix_now();
        let text = render(&Report {
            kind: "native",
            unix,
            uptime: ctx.started.elapsed().as_secs_f64(),
            game_version: &ctx.game_version,
            build: &ctx.build,
            os: &ctx.os,
            gpu: GPU.get().map_or("unknown", String::as_str),
            exe: &exe_line(),
            thread: &format!("id {thread}"),
            details,
            frames: &[],
            backtrace: "",
            log: &[],
        });
        let _ = std::fs::create_dir_all(&ctx.dir);
        let txt = report_path(&ctx.dir, unix, "txt");
        let _ = std::fs::write(&txt, &text);
        let dumped = write_dump(&report_path(&ctx.dir, unix, "dmp"), pointers, thread);
        let mut tail = format!(
            "\nminidump: {}\n",
            if dumped { "written" } else { "failed" }
        );
        let log = benilla_world::log_ring::recent();
        let _ = writeln!(tail, "\n{}", log_heading(log.len()));
        for l in &log {
            let _ = writeln!(tail, "{l}");
        }
        if let Ok(mut f) = std::fs::OpenOptions::new().append(true).open(&txt) {
            use std::io::Write as _;
            let _ = f.write_all(tail.as_bytes());
        }
    }

    fn write_dump(path: &Path, pointers: *mut ExceptionPointers, thread: u32) -> bool {
        let wide: Vec<u16> = path.as_os_str().encode_wide().chain([0]).collect();
        // SAFETY: a null-terminated path; the handle is closed below.
        let file = unsafe {
            CreateFileW(
                wide.as_ptr(),
                GENERIC_WRITE,
                0,
                null(),
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                null_mut(),
            )
        };
        if file.is_null() || file as isize == -1 {
            return false;
        }
        let info = MinidumpExceptionInformation {
            thread_id: thread,
            pointers,
            client_pointers: 0,
        };
        let exception = if pointers.is_null() {
            null()
        } else {
            &info as *const _
        };
        // SAFETY: valid process and file handles; the exception pointers stay valid while the
        // crashed thread waits in the filter.
        let ok = unsafe {
            let ok = MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                file,
                DUMP_KIND,
                exception,
                null(),
                null(),
            );
            CloseHandle(file);
            ok != 0
        };
        ok
    }

    /// The test crash: an access violation, raised the way the hardware raises one.
    pub fn raise_access_violation() {
        // SAFETY: raises an SEH exception; nothing handles it, so the filter does.
        unsafe {
            RaiseException(
                EXCEPTION_ACCESS_VIOLATION,
                EXCEPTION_NONCONTINUABLE,
                0,
                null(),
            )
        };
    }

    pub fn exe_base() -> usize {
        // SAFETY: null asks for the exe's own module.
        unsafe { GetModuleHandleW(null()) as usize }
    }

    pub fn stack() -> Vec<usize> {
        let mut frames = [null_mut::<c_void>(); 62];
        // SAFETY: the buffer holds `count` entries.
        let n = unsafe { RtlCaptureStackBackTrace(1, 62, frames.as_mut_ptr(), null_mut()) };
        frames[..n as usize].iter().map(|&f| f as usize).collect()
    }

    /// `benilla-mw2.exe+0x1a2b3c` for an address inside a loaded module, else the bare address.
    pub fn module_offset(address: usize) -> String {
        if address == 0 {
            return "0x0".into();
        }
        let mut module: Handle = null_mut();
        // SAFETY: FROM_ADDRESS reads `name` as an address; no reference count is taken.
        let found = unsafe {
            GetModuleHandleExW(
                FROM_ADDRESS | UNCHANGED_REFCOUNT,
                address as *const u16,
                &mut module,
            )
        };
        if found == 0 || module.is_null() {
            return format!("0x{address:x}");
        }
        let mut name = [0u16; 520];
        // SAFETY: the buffer's size is passed.
        let len = unsafe { GetModuleFileNameW(module, name.as_mut_ptr(), name.len() as u32) };
        let full = String::from_utf16_lossy(&name[..len as usize]);
        let base = full.rsplit(['\\', '/']).next().unwrap_or("?").to_string();
        format!("{base}+0x{:x}", address - module as usize)
    }

    pub fn os_version() -> String {
        let mut info = OsVersionInfo {
            size: std::mem::size_of::<OsVersionInfo>() as u32,
            major: 0,
            minor: 0,
            build: 0,
            platform: 0,
            service_pack: [0; 128],
        };
        // SAFETY: the struct's size is set as the call asks.
        if unsafe { RtlGetVersion(&mut info) } != 0 {
            return format!("Windows ({})", std::env::consts::ARCH);
        }
        super::windows_name(info.major, info.minor, info.build)
    }
}

/// `Windows 11 (10.0.26200, x86_64)`: 11 reports itself as 10.0 with builds from 22000.
fn windows_name(major: u32, minor: u32, build: u32) -> String {
    let name = match (major, minor) {
        (10, 0) if build >= 22000 => "Windows 11",
        (10, 0) => "Windows 10",
        (6, 3) => "Windows 8.1",
        (6, 2) => "Windows 8",
        (6, 1) => "Windows 7",
        _ => "Windows",
    };
    format!(
        "{name} ({major}.{minor}.{build}, {})",
        std::env::consts::ARCH
    )
}

/// What a Windows exception code means, for people reading the report.
pub fn exception_name(code: u32) -> &'static str {
    match code {
        0xC000_0005 => "access violation",
        0xC000_00FD => "stack overflow",
        0xC000_0409 => "fail fast (abort)",
        0xC000_001D => "illegal instruction",
        0xC000_0094 => "integer divide by zero",
        0xC000_0374 => "heap corruption",
        0xC000_0006 => "in-page error (a file read failed)",
        0xC000_0017 => "out of memory",
        0xE06D_7363 => "C++ exception",
        0x8000_0003 => "breakpoint",
        _ => "exception",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn report<'a>(
        details: Vec<(&'a str, String)>,
        frames: &'a [String],
        log: &'a [String],
    ) -> Report<'a> {
        Report {
            kind: "panic",
            unix: 1_800_000_000,
            uptime: 12.345,
            game_version: "2026-10-18 12:00",
            build: "0.1.0 · 0123456 · 2026-10-18 · release (sha 0123)",
            os: "Windows 11 (10.0.26200, x86_64)",
            gpu: "NVIDIA GeForce RTX 3070 (Dx12, DiscreteGpu, driver NVIDIA 560.94)",
            exe: "benilla-mw2.exe, loaded at 0x7ff600000000",
            thread: "main",
            details,
            frames,
            backtrace: "   0: benilla_mw2::weapon::fire\n   1: <unknown>",
            log,
        }
    }

    #[test]
    fn a_panic_report_says_what_where_on_what() {
        let frames = vec!["benilla-mw2.exe+0x1a2b3c".to_string()];
        let log = vec!["+   1.000s  INFO benilla_app::net: entered world".to_string()];
        let text = render(&report(
            vec![
                ("at", "src/weapon.rs:10:5".into()),
                ("panic", "index out of bounds\nsecond line".into()),
            ],
            &frames,
            &log,
        ));
        for needle in [
            "World of Modern Warfare crash report\n",
            "kind:      panic\n",
            "time:      1800000000 unix, 12.3 s after launch\n",
            "game:      2026-10-18 12:00\n",
            "os:        Windows 11 (10.0.26200, x86_64)\n",
            "gpu:       NVIDIA GeForce RTX 3070",
            "exe:       benilla-mw2.exe, loaded at 0x7ff600000000\n",
            "thread:    main\n",
            "at:        src/weapon.rs:10:5\n",
            "panic:     index out of bounds\n           second line\n",
            "frames (module+offset",
            "    0  benilla-mw2.exe+0x1a2b3c\n",
            "backtrace:\n   0: benilla_mw2::weapon::fire",
            "last 1 log lines (oldest first):\n+   1.000s  INFO benilla_app::net: entered world\n",
        ] {
            assert!(text.contains(needle), "missing {needle:?} in:\n{text}");
        }
    }

    #[test]
    fn a_native_report_without_frames_or_log_leaves_them_out() {
        let mut r = report(
            vec![
                ("exception", "0xC0000005 access violation".into()),
                ("address", "benilla-mw2.exe+0x10".into()),
            ],
            &[],
            &[],
        );
        r.kind = "native";
        r.backtrace = "";
        let text = render(&r);
        assert!(text.contains("kind:      native\n"));
        assert!(text.contains("exception: 0xC0000005 access violation\n"));
        assert!(text.contains("address:   benilla-mw2.exe+0x10\n"));
        assert!(!text.contains("frames"));
        assert!(!text.contains("backtrace"));
        assert!(!text.contains("log lines"));
    }

    #[test]
    fn names_for_people() {
        assert_eq!(
            windows_name(10, 0, 26200),
            format!("Windows 11 (10.0.26200, {})", std::env::consts::ARCH)
        );
        assert!(windows_name(10, 0, 19045).starts_with("Windows 10 "));
        assert_eq!(exception_name(0xC000_0005), "access violation");
        assert_eq!(exception_name(0xC000_00FD), "stack overflow");
        assert_eq!(exception_name(0x1234), "exception");
        assert_eq!(
            gpu_line("Radeon", "Vulkan", "DiscreteGpu", "AMD", " 24.9 "),
            "Radeon (Vulkan, DiscreteGpu, driver AMD 24.9)"
        );
        assert_eq!(
            gpu_line("Basic", "Dx12", "Cpu", "", ""),
            "Basic (Dx12, Cpu)"
        );
    }

    #[test]
    fn the_test_crash_switch() {
        assert_eq!(TestCrash::parse("panic"), Some(TestCrash::Panic));
        assert_eq!(TestCrash::parse(" ACCESS "), Some(TestCrash::Access));
        assert_eq!(TestCrash::parse(""), None);
        assert_eq!(TestCrash::parse("1"), None);
    }

    #[test]
    fn reports_are_named_by_time() {
        assert_eq!(
            report_path(Path::new("x"), 42, "dmp"),
            Path::new("x").join("crash-42.dmp")
        );
    }

    /// Runs this test binary again with only `test`, as a child that crashes the way
    /// `AW_TEST_CRASH` makes the game crash; its exit code and crash folder.
    #[cfg(windows)]
    fn crash_child(test: &str, mode: &str) -> (Option<i32>, PathBuf) {
        let dir = std::env::temp_dir().join(format!("aw-crash-{mode}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let status = std::process::Command::new(std::env::current_exe().unwrap())
            .args([test, "--exact", "--nocapture", "--test-threads=1"])
            .env("AW_CRASH_CHILD", mode)
            .env(CRASH_DIR_VAR, &dir)
            .env(VERSION_VAR, "test-version")
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .unwrap();
        (status.code(), dir)
    }

    /// In the child: arm the reporter and crash.
    fn child_crashes() -> bool {
        let Ok(mode) = std::env::var("AW_CRASH_CHILD") else {
            return false;
        };
        install("test build".into());
        match mode.as_str() {
            #[cfg(windows)]
            "access" => native::raise_access_violation(),
            _ => panic!("deliberate test panic"),
        }
        true
    }

    fn only_report(dir: &Path, ext: &str) -> PathBuf {
        let found: Vec<_> = std::fs::read_dir(dir)
            .unwrap()
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|e| e == ext))
            .collect();
        assert_eq!(found.len(), 1, "{found:?}");
        found[0].clone()
    }

    #[cfg(windows)]
    #[test]
    fn an_access_violation_leaves_a_report_and_a_minidump() {
        if child_crashes() {
            return;
        }
        let (code, dir) = crash_child(
            "crash_report::tests::an_access_violation_leaves_a_report_and_a_minidump",
            "access",
        );
        assert_eq!(code.map(|c| c as u32), Some(0xC000_0005));
        let text = std::fs::read_to_string(only_report(&dir, "txt")).unwrap();
        for needle in [
            "kind:      native\n",
            "game:      test-version\n",
            "exception: 0xC0000005 access violation\n",
            "minidump: written",
        ] {
            assert!(text.contains(needle), "missing {needle:?} in:\n{text}");
        }
        let dump = std::fs::read(only_report(&dir, "dmp")).unwrap();
        assert_eq!(&dump[..4], b"MDMP");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[cfg(windows)]
    #[test]
    fn a_panic_leaves_a_report() {
        if child_crashes() {
            return;
        }
        let (code, dir) = crash_child("crash_report::tests::a_panic_leaves_a_report", "panic");
        assert_ne!(code, Some(0));
        let text = std::fs::read_to_string(only_report(&dir, "txt")).unwrap();
        for needle in [
            "kind:      panic\n",
            "build:     test build\n",
            "panic:     deliberate test panic\n",
            "crash_report.rs:",
            "frames (module+offset",
        ] {
            assert!(text.contains(needle), "missing {needle:?} in:\n{text}");
        }
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[cfg(windows)]
    #[test]
    fn frames_name_this_module() {
        let frames = stack_frames();
        assert!(!frames.is_empty());
        let exe = std::env::current_exe().unwrap();
        let name = exe.file_name().unwrap().to_string_lossy().to_string();
        assert!(
            frames.iter().any(|f| f.starts_with(&format!("{name}+0x"))),
            "{frames:?}"
        );
        assert!(os_version().starts_with("Windows"), "{}", os_version());
        assert!(exe_line().contains("loaded at 0x"));
    }
}
