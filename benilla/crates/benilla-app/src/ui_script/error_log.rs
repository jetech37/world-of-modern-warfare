//! `Logs\LuaErrors.log`: every Lua error the UI raises (a handler's runtime error, a file that
//! failed to load), stamped and appended a line per error, so an error the in-game dialog shows
//! can be read and sent after the session. The reference has no such file; its `ScriptErrors`
//! dialog is the only record. Under `benilla-config/Logs/`, beside `FrameXML.log`.

use std::io::Write as _;

use bevy::prelude::*;

/// The log's name.
pub(crate) const FILE: &str = "LuaErrors.log";

/// Append `errors` to the log, a stamped line each (a multi-line message, its stack, keeps its
/// lines). Nothing to write opens nothing.
pub(super) fn write(errors: &[String]) {
    if errors.is_empty() {
        return;
    }
    let Some(dir) = crate::local_state::logs_dir() else {
        return;
    };
    write_under(&dir, errors);
}

/// [`write`] into `dir`.
pub(super) fn write_under(dir: &std::path::Path, errors: &[String]) {
    let path = dir.join(FILE);
    let mut body = String::new();
    for e in errors {
        body.push_str(&crate::ui_chat::logging::stamp());
        body.push_str("  ");
        body.push_str(&e.replace('\n', "\r\n    "));
        body.push_str("\r\n");
    }
    let written = std::fs::create_dir_all(dir).and_then(|()| {
        std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)?
            .write_all(body.as_bytes())
    });
    if let Err(e) = written {
        warn!("ui_script: cannot write {}: {e}", path.display());
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn errors_are_appended_a_stamped_line_each() {
        let dir = std::env::temp_dir().join(format!("benilla-lua-errors-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        super::write_under(&dir, &["first: boom".into()]);
        super::write_under(&dir, &["second\nstack".into()]);
        let text = std::fs::read_to_string(dir.join(super::FILE)).unwrap();
        assert!(text.contains("first: boom\r\n"));
        assert!(text.contains("second\r\n    stack\r\n"));
        assert!(text.find("first").unwrap() < text.find("second").unwrap(), "appended");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
