//! The `benilla-mw2` launcher: the benilla client with the MW2 layer on top, started through
//! [`benilla_app::run_with`], as a crate built on benilla starts it.

use benilla_app::BuildId;

fn main() -> benilla_app::AppExit {
    let build = BuildId {
        version: env!("CARGO_PKG_VERSION"),
        describe: env!("BENILLA_GIT_DESCRIBE"),
        sha: env!("BENILLA_GIT_SHA"),
        short: env!("BENILLA_GIT_SHORT"),
        date: env!("BENILLA_GIT_DATE"),
        profile: env!("BENILLA_PROFILE"),
        project_dir: env!("BENILLA_PROJECT_DIR"),
        ..Default::default()
    };
    // A crash leaves a report in the launcher's crash folder (`$AW_CRASH_DIR`), which it offers
    // to send; armed before anything else runs.
    benilla_mw2::crash_report::install(format!("{} (sha {})", build.summary(), build.sha));
    benilla_app::run_with(build, |app| {
        app.add_plugins(benilla_mw2::Mw2Plugin);
    })
}
