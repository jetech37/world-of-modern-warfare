//! World of Modern Warfare's community-zone merge: the open-world tiles of a 1.12-format
//! community client (new zones on Azeroth and Kalimdor), the models and textures they need and
//! their table rows, packed into our own private patch archive [`ARCHIVE_NAME`].
//!
//! Nothing this writes is committed: the output goes to the gitignored `private-assets/`
//! (`private-assets.md`), and `package/build.ps1` ships it.

pub mod brand;
pub mod build;
pub mod dbc;
pub mod hd;
/// The city void-hole pass, shared with the client's patch-layer import (std only).
#[path = "../../../client/src/wotlk/holes.rs"]
pub mod holes;
pub mod mpq_write;
pub mod refs;
pub mod source;
pub mod tiles;

/// Our archive for the merged zones. benilla mounts `patch-?.MPQ` by case-folded name, so
/// `patch-A` sits above our WotLK layer `patch-9` (`'9' < 'a'`) and above every stock archive; the
/// server's extractors list it explicitly. It never reuses one of the community client's own
/// archive names for the same content: theirs are `patch-3`..`patch-9` (content) and
/// `patch-A`..`patch-Y` (an HD pack we do not ship), and nothing of theirs is shipped as is.
pub const ARCHIVE_NAME: &str = "patch-A.MPQ";
