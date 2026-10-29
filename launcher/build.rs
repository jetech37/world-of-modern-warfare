//! The WOMW emblem as the launcher exe's icon (Explorer, the taskbar and its desktop shortcut).
fn main() {
    println!("cargo:rerun-if-changed=icon.ico");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("icon.ico");
        res.compile().expect("embedding the launcher icon");
    }
}
