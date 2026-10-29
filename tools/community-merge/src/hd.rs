//! The optional HD pack: the community client's HD archives (`patch-A`..`patch-Y` in its `Data`,
//! the higher-resolution models, textures and sounds of stock files) filtered into our own
//! archives [`archive_name`] in `private-assets/womw-hd/`, listed by [`MANIFEST`].
//!
//! The rules ([`decide`]): our layers always win (a path our WotLK layer `patch-9` or community
//! layer `patch-A` holds is left out), the server's world stays the server's (no tables, terrain
//! tiles or WMO geometry, and a world doodad only when its collision hull is the stock one), the
//! stock interface code stays stock (benilla loads FrameXML byte for byte), and every file must
//! decode through benilla's own readers. A file stock lacks comes in only when a kept model names
//! it. The client mounts the pack above the stock archives and below our layers
//! (`benilla_app::hd_pack`), from its own folder, so the server's extractors never see it.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::io::Cursor;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use benilla_mpq::Archive;

use crate::source::Layer;

/// The community client's HD archives, lowest priority first (its own mount order).
pub const HD_ARCHIVES: [&str; 12] = [
    "patch-A.MPQ",
    "patch-B.MPQ",
    "patch-C.MPQ",
    "patch-D.MPQ",
    "patch-E.MPQ",
    "patch-G.MPQ",
    "patch-I.MPQ",
    "patch-L.MPQ",
    "patch-M.MPQ",
    "patch-T.MPQ",
    "patch-U.MPQ",
    "patch-Y.MPQ",
];

/// The pack's manifest beside its archives: one archive file name per line, then `#` lines.
pub const MANIFEST: &str = "womw-hd.txt";

/// One archive of the pack. Not a `patch-?.MPQ` name: dropped into a `Data` folder by mistake it
/// mounts nowhere, and the server's extractors skip it.
pub fn archive_name(i: usize) -> String {
    format!("womw-hd-{}.MPQ", i + 1)
}

/// The largest archive the build writes (a download unit; MPQ V1 stops at 4 GiB).
pub const SPLIT_BYTES: u64 = 1536 << 20;

/// What a path is, for the survey table.
pub fn class_of(name: &str) -> &'static str {
    let n = name.to_ascii_lowercase().replace('/', "\\");
    let ext = n.rsplit('.').next().unwrap_or("");
    let top = n.split('\\').next().unwrap_or("");
    match (top, ext) {
        (_, "dbc") => "tables",
        ("interface", "lua" | "xml" | "toc") => "UI code",
        ("interface", "blp") => "UI textures",
        ("interface", _) => "UI other",
        ("world", "adt" | "wdt" | "wdl") => "terrain tiles",
        (_, "wmo") => "WMOs",
        ("tileset", "blp") => "terrain textures",
        ("character", "m2") => "character models",
        ("character", "blp") => "character textures",
        ("creature", "m2") => "creature models",
        ("creature", "blp") => "creature textures",
        ("item", "m2") => "item models",
        ("item", "blp") => "item textures",
        ("spells", "m2") => "spell models",
        ("spells", "blp") => "spell textures",
        ("world", "m2") => "world doodads",
        ("world", "blp") => "world textures",
        (_, "wav" | "mp3" | "ogg") => "sound",
        (_, "m2") => "other models",
        (_, "blp") => "other textures",
        _ => "other",
    }
}

/// Where a path stands on our side.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Ours {
    /// Our WotLK layer (`patch-9`) holds it.
    Wotlk,
    /// Our community layer (`patch-A`) holds it.
    Community,
    /// Only the stock 1.12 archives hold it.
    Stock,
    /// Nobody on our side holds it.
    New,
}

impl Ours {
    pub fn label(self) -> &'static str {
        match self {
            Ours::Wotlk => "patch-9 (WotLK)",
            Ours::Community => "patch-A (community)",
            Ours::Stock => "stock",
            Ours::New => "new",
        }
    }
}

/// Our side, three stacks: stock (the `Data` folder without our layers), patch-9, patch-A.
pub struct OurSide {
    pub stock: Layer,
    pub wotlk: Layer,
    pub community: Layer,
    /// The branding rules: our community layer stores branded names under their aliases.
    pub brand: crate::brand::Rules,
}

impl OurSide {
    pub fn where_is(&self, name: &str) -> Ours {
        // Our community layer stores their branded names under neutral aliases (`brand`).
        if self.community.contains(name) || self.community.contains(&self.brand.scrub_path(name)) {
            Ours::Community
        } else if self.wotlk.contains(name) {
            Ours::Wotlk
        } else if self.stock.contains(name) {
            Ours::Stock
        } else {
            Ours::New
        }
    }
}

/// Why a file is left out.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Out {
    /// Our WotLK or community layer holds the path: ours wins.
    OurLayer,
    /// A client table: ours (and the server's copies) stay authoritative.
    Table,
    /// Interface Lua/XML/TOC: the stock UI is loaded byte for byte.
    UiCode,
    /// Terrain tiles: the server's maps and our merged tiles are built from ours.
    Terrain,
    /// WMO geometry: the server's collision (vmaps) is built from ours.
    Wmo,
    /// A world doodad whose collision hull differs from the stock one (the server's vmaps).
    DoodadCollision,
    /// A texture the character-skin composite blits at its stock size (1.12 composites into a
    /// fixed 256² atlas, tile by tile, without scaling), and the HD copy is another size.
    CompositeSize,
    /// A file stock lacks that no kept model names.
    Unreferenced,
    /// A kept model names a texture our layers override, or one nobody has.
    TextureMismatch(String),
    /// benilla's readers reject it (or it cannot be read out of their archive).
    Decode(String),
    /// Nothing in the game reads this kind of file.
    Other,
}

impl Out {
    pub fn label(&self) -> String {
        match self {
            Out::OurLayer => "our layer wins".into(),
            Out::Table => "client table".into(),
            Out::UiCode => "UI code".into(),
            Out::Terrain => "terrain tile".into(),
            Out::Wmo => "WMO geometry".into(),
            Out::DoodadCollision => "doodad collision differs".into(),
            Out::CompositeSize => "skin composite size".into(),
            Out::Unreferenced => "new and unreferenced".into(),
            Out::TextureMismatch(_) => "texture overridden or missing".into(),
            Out::Decode(_) => "does not decode".into(),
            Out::Other => "unused kind".into(),
        }
    }
}

/// The rule by path and side alone (no bytes): `Some` leaves the file out.
pub fn by_path(name: &str, ours: Ours) -> Option<Out> {
    if matches!(ours, Ours::Wotlk | Ours::Community) {
        return Some(Out::OurLayer);
    }
    match class_of(name) {
        "tables" => Some(Out::Table),
        "UI code" => Some(Out::UiCode),
        "terrain tiles" => Some(Out::Terrain),
        "WMOs" => Some(Out::Wmo),
        "other" | "UI other" => Some(Out::Other),
        _ => None,
    }
}

/// Whether benilla reads `bytes` as the kind `name` says: `Ok` with a short note (texture size).
pub fn decodes(name: &str, bytes: &[u8]) -> Result<String> {
    let lower = name.to_ascii_lowercase();
    if lower.ends_with(".blp") {
        let d = benilla_blp::decode(bytes).map_err(|e| anyhow::anyhow!("{e}"))?;
        if d.mips.is_empty() {
            bail!("no mip levels");
        }
        return Ok(format!("{}x{}", d.width, d.height));
    }
    if lower.ends_with(".m2") {
        if bytes.get(..4) != Some(b"MD20") {
            bail!("not an MD20 model");
        }
        let version = u32::from_le_bytes(bytes[4..8].try_into()?);
        if !(256..=257).contains(&version) {
            bail!("M2 version {version}, not 1.12's 256");
        }
        let subs = benilla_formats::parse_m2_render_submeshes(bytes, "", &[])?;
        benilla_formats::parse_m2_skeleton(bytes)?;
        let verts: usize = subs.iter().map(|s| s.positions.len()).sum();
        return Ok(format!("{} submeshes, {verts} vertices", subs.len()));
    }
    if lower.ends_with(".wav") {
        if bytes.get(..4) != Some(b"RIFF") {
            bail!("not a RIFF wave");
        }
        return Ok(String::new());
    }
    if lower.ends_with(".mp3") {
        let id3 = bytes.get(..3) == Some(b"ID3");
        let sync = bytes.len() > 1 && bytes[0] == 0xFF && bytes[1] & 0xE0 == 0xE0;
        if !(id3 || sync) {
            bail!("not an MP3 stream");
        }
        return Ok(String::new());
    }
    if lower.ends_with(".wmo") {
        benilla_wmo::parse_wmo(&mut Cursor::new(bytes)).map_err(|e| anyhow::anyhow!("{e}"))?;
        return Ok(String::new());
    }
    Ok(String::new())
}

/// Whether the character-skin composite blits this texture into its atlas (CharSections skins,
/// faces, facial hair, scalps and underwear under `Character\`, and every equipment region
/// texture under `Item\TextureComponents\`), rather than a model drawing it whole (hair).
pub fn composited(name: &str) -> bool {
    let n = name.to_ascii_lowercase().replace('/', "\\");
    if !n.ends_with(".blp") {
        return false;
    }
    if n.starts_with("item\\texturecomponents\\") {
        return true;
    }
    if !n.starts_with("character\\") {
        return false;
    }
    let file = n.rsplit('\\').next().unwrap_or("");
    !(file.contains("hair") && !file.contains("facial") && !file.contains("scalp"))
}

/// A BLP2's width and height from its header.
pub fn blp_dims(b: &[u8]) -> Option<(u32, u32)> {
    if b.get(..4) != Some(b"BLP2") {
        return None;
    }
    let w = u32::from_le_bytes(b.get(12..16)?.try_into().ok()?);
    let h = u32::from_le_bytes(b.get(16..20)?.try_into().ok()?);
    Some((w, h))
}

/// Whether two models' collision hulls are the same triangles (the server's vmaps hold stock's).
pub fn same_collision(a: &[u8], b: &[u8]) -> bool {
    match (
        benilla_formats::parse_m2_collision_hull(a),
        benilla_formats::parse_m2_collision_hull(b),
    ) {
        (Ok(x), Ok(y)) => x.indices == y.indices && x.positions == y.positions,
        _ => false,
    }
}

/// One file of the HD archives and the verdict on it.
pub struct Entry {
    pub name: String,
    /// Index into [`HD_ARCHIVES`] of the archive it is read from (the highest holding it).
    pub archive: usize,
    pub size: u64,
    pub ours: Ours,
    pub out: Option<Out>,
    pub note: String,
}

/// The HD side: their archives that exist, in order, with the archive index of each.
pub struct HdSide {
    pub archives: Vec<(usize, Archive)>,
}

impl HdSide {
    pub fn open(data: &Path) -> Result<Self> {
        let names: Vec<String> = std::fs::read_dir(data)
            .with_context(|| format!("listing {}", data.display()))?
            .filter_map(|e| e.ok()?.file_name().into_string().ok())
            .collect();
        let mut archives = Vec::new();
        for (i, want) in HD_ARCHIVES.iter().enumerate() {
            if let Some(n) = names.iter().find(|n| n.eq_ignore_ascii_case(want)) {
                let p = data.join(n);
                archives.push((
                    i,
                    Archive::open(&p).with_context(|| format!("opening {}", p.display()))?,
                ));
            }
        }
        if archives.is_empty() {
            bail!("no HD archives (patch-A..patch-Y) in {}", data.display());
        }
        Ok(Self { archives })
    }

    /// Every listed path, each from the highest archive holding it, with whatever lower archives
    /// also list it (for the survey's per-archive count).
    pub fn listing(&self) -> Vec<(String, usize, Vec<usize>)> {
        let mut by_key: BTreeMap<String, (String, Vec<usize>)> = BTreeMap::new();
        for (i, a) in &self.archives {
            let Ok(list) = a.read_file("(listfile)") else {
                continue;
            };
            for raw in String::from_utf8_lossy(&list).split([';', '\r', '\n']) {
                let n = raw.trim();
                if n.is_empty() || n.starts_with('(') || !a.contains(n) || a.is_delete_marker(n) {
                    continue;
                }
                let key = n.replace('/', "\\").to_ascii_lowercase();
                let e = by_key
                    .entry(key)
                    .or_insert_with(|| (n.replace('/', "\\"), Vec::new()));
                e.1.push(*i);
            }
        }
        by_key
            .into_values()
            .map(|(n, mut all)| {
                all.sort();
                all.dedup();
                let top = *all.last().unwrap();
                (n, top, all)
            })
            .collect()
    }

    pub fn archive(&self, i: usize) -> &Archive {
        &self.archives.iter().find(|(j, _)| *j == i).unwrap().1
    }
}

/// Judge every file: path rules, decode, doodad collision, then the texture closure.
pub fn judge(hd: &HdSide, ours: &OurSide, mut progress: impl FnMut(usize, usize)) -> Vec<Entry> {
    let listing = hd.listing();
    let total = listing.len();
    let mut entries: Vec<Entry> = Vec::with_capacity(total);
    // Lower-case path -> texture names a kept model asks for.
    let mut wants: HashMap<usize, Vec<String>> = HashMap::new();
    for (k, (name, top, _)) in listing.into_iter().enumerate() {
        if k % 2000 == 0 {
            progress(k, total);
        }
        let a = hd.archive(top);
        let size = a.file_size(&name).unwrap_or(0) as u64;
        let side = ours.where_is(&name);
        let mut e = Entry {
            name,
            archive: top,
            size,
            ours: side,
            out: None,
            note: String::new(),
        };
        e.out = by_path(&e.name, side);
        if e.out.is_none() {
            match a.read_file(&e.name) {
                Err(err) => e.out = Some(Out::Decode(format!("archive read: {err}"))),
                Ok(bytes) => match decodes(&e.name, &bytes) {
                    Err(err) => e.out = Some(Out::Decode(format!("{err:#}"))),
                    Ok(note) => {
                        e.note = note;
                        let lower = e.name.to_ascii_lowercase();
                        if side == Ours::Stock && composited(&e.name) {
                            let stock = ours.stock.read(&e.name).ok();
                            if stock.as_deref().and_then(blp_dims) != blp_dims(&bytes) {
                                e.out = Some(Out::CompositeSize);
                            }
                        }
                        if lower.ends_with(".m2") {
                            if side == Ours::Stock && class_of(&e.name) == "world doodads" {
                                let same = ours
                                    .stock
                                    .read(&e.name)
                                    .is_ok_and(|s| same_collision(&s, &bytes));
                                if !same {
                                    e.out = Some(Out::DoodadCollision);
                                }
                            }
                            if e.out.is_none() {
                                let refs: Vec<String> = crate::refs::m2_texture_refs(&bytes)
                                    .iter()
                                    .map(|r| crate::refs::normalize(r))
                                    .collect();
                                wants.insert(entries.len(), refs);
                            }
                        }
                    }
                },
            }
        }
        entries.push(e);
    }
    progress(total, total);
    close_textures(&mut entries, &wants, ours);
    entries
}

/// The texture closure: a kept model whose texture our layers override (ours would draw on the
/// HD mesh) or nobody has is left out; a new file comes in only when a kept model names it.
fn close_textures(entries: &mut [Entry], wants: &HashMap<usize, Vec<String>>, ours: &OurSide) {
    let index: HashMap<String, usize> = entries
        .iter()
        .enumerate()
        .map(|(i, e)| (e.name.to_ascii_lowercase(), i))
        .collect();
    for (&m, refs) in wants {
        let mut bad = None;
        for r in refs {
            let key = r.to_ascii_lowercase();
            let hd_kept = index.get(&key).filter(|&&i| entries[i].out.is_none());
            match (hd_kept, ours.where_is(r)) {
                (_, Ours::Wotlk | Ours::Community) => bad = Some(format!("{r} is ours")),
                (Some(_), _) | (None, Ours::Stock) => {}
                (None, Ours::New) => bad = Some(format!("{r} missing")),
            }
        }
        if let Some(b) = bad {
            entries[m].out = Some(Out::TextureMismatch(b));
        }
    }
    // Re-collect what kept models name after the mismatches dropped some.
    let mut named_kept: BTreeSet<usize> = BTreeSet::new();
    for (&m, refs) in wants {
        if entries[m].out.is_some() {
            continue;
        }
        for r in refs {
            if let Some(&i) = index.get(&r.to_ascii_lowercase()) {
                named_kept.insert(i);
            }
        }
    }
    for (i, e) in entries.iter_mut().enumerate() {
        if e.out.is_none() && e.ours == Ours::New && !named_kept.contains(&i) {
            // New models stay out too: no table of ours names them.
            e.out = Some(Out::Unreferenced);
        }
    }
}

/// Split the kept files into archives of at most [`SPLIT_BYTES`], grouped by class in path order.
pub fn split(kept: &[&Entry]) -> Vec<Vec<usize>> {
    let mut order: Vec<usize> = (0..kept.len()).collect();
    order.sort_by(|&a, &b| {
        (class_of(&kept[a].name), kept[a].name.to_ascii_lowercase())
            .cmp(&(class_of(&kept[b].name), kept[b].name.to_ascii_lowercase()))
    });
    let mut out: Vec<Vec<usize>> = vec![Vec::new()];
    let mut size = 0u64;
    for i in order {
        let s = kept[i].size;
        if size + s > SPLIT_BYTES && !out.last().unwrap().is_empty() {
            out.push(Vec::new());
            size = 0;
        }
        out.last_mut().unwrap().push(i);
        size += s;
    }
    out
}

/// `hd --hd <their Data> --ours <our Data> --wotlk <patch-9.MPQ> --community <patch-A.MPQ>
/// --out <dir> [--survey-only]`.
pub fn run(rest: &[String]) -> Result<()> {
    let arg = |k: &str| {
        rest.iter()
            .position(|r| r == k)
            .and_then(|i| rest.get(i + 1))
            .map(PathBuf::from)
    };
    let hd_dir = arg("--hd").context("--hd <the community client's Data>")?;
    let ours_dir = arg("--ours").context("--ours <our Data>")?;
    let wotlk = arg("--wotlk").context("--wotlk <patch-9.MPQ>")?;
    let community = arg("--community-layer").context("--community-layer <patch-A.MPQ>")?;
    let out_dir = arg("--out").context("--out <dir>")?;
    let survey_only = rest.iter().any(|r| r == "--survey-only");
    let brand = crate::brand::Rules::find(rest)?;

    let names: Vec<String> = std::fs::read_dir(&ours_dir)?
        .filter_map(|e| {
            let e = e.ok()?;
            e.path()
                .is_file()
                .then(|| e.file_name().into_string().ok())?
        })
        .collect();
    let stock_names: Vec<String> = crate::source::mount_order(&names, crate::ARCHIVE_NAME)
        .into_iter()
        .filter(|n| !is_built_layer(n))
        .collect();
    let stock_paths: Vec<PathBuf> = stock_names.iter().map(|n| ours_dir.join(n)).collect();
    let side = OurSide {
        stock: Layer::open_files(&stock_paths)?,
        wotlk: Layer::open_files(&[wotlk])?,
        community: Layer::open_files(&[community])?,
        brand: brand.clone(),
    };
    let hd = HdSide::open(&hd_dir)?;
    let entries = judge(&hd, &side, |k, n| eprintln!("judged {k}/{n}"));
    std::fs::create_dir_all(&out_dir)?;
    std::fs::write(out_dir.join("survey.tsv"), brand.scrub_text(&survey_tsv(&entries)))?;
    std::fs::write(out_dir.join("report.txt"), brand.scrub_text(&report(&entries, &hd)))?;
    println!("{}", brand.scrub_text(&report(&entries, &hd)));
    if survey_only {
        return Ok(());
    }
    // The branding scrub (`brand`): no text file of theirs ships in the pack (a rewrite could change
    // its length), every file is stored under its scrubbed name (the same rule as patch-A, so an
    // alias there and here agree) and binaries are rewritten as packed (same length).
    let kept: Vec<&Entry> = entries
        .iter()
        .filter(|e| e.out.is_none())
        .filter(|e| !crate::brand::is_text_name(&e.name) && !brand.is_branding_file(&e.name))
        .collect();
    let parts = split(&kept);
    // Old parts first: a rebuild with fewer parts must not leave a stale one mounted.
    for old in std::fs::read_dir(&out_dir)?.filter_map(|e| e.ok()) {
        let n = old.file_name().to_string_lossy().to_ascii_lowercase();
        if n.starts_with("womw-hd-") && n.ends_with(".mpq") {
            std::fs::remove_file(old.path())?;
        }
    }
    let mut manifest = String::new();
    let mut total = 0u64;
    for (p, files) in parts.iter().enumerate() {
        let name = archive_name(p);
        let sized: Vec<(String, u64)> = files
            .iter()
            .map(|&i| (brand.scrub_path(&kept[i].name), kept[i].size))
            .collect();
        let by_name: HashMap<String, usize> = files
            .iter()
            .map(|&i| (brand.scrub_path(&kept[i].name).to_ascii_lowercase(), i))
            .collect();
        let path = out_dir.join(&name);
        let tmp = out_dir.join(format!("{name}.tmp"));
        let mut w = std::io::BufWriter::with_capacity(1 << 20, std::fs::File::create(&tmp)?);
        let size = crate::mpq_write::write_mpq_with(&sized, &mut w, |n| {
            let e = kept[by_name[&n.to_ascii_lowercase()]];
            hd.archive(e.archive)
                .read_file(&e.name)
                .map(|b| brand.scrub_file(&e.name, b))
                .map_err(|err| std::io::Error::other(format!("{}: {err}", e.name)))
        })?;
        drop(w);
        std::fs::rename(&tmp, &path)?;
        eprintln!(
            "wrote {} ({} files, {:.2} GB)",
            path.display(),
            files.len(),
            size as f64 / 1e9
        );
        manifest.push_str(&format!("{name}\t{size}\n"));
        total += size;
    }
    manifest.push_str(&format!("# {} files, {total} bytes\n", kept.len()));
    std::fs::write(out_dir.join(MANIFEST), manifest)?;
    println!(
        "HD pack: {} archives, {} files, {:.2} GB",
        parts.len(),
        kept.len(),
        total as f64 / 1e9
    );
    // The branding gate over everything the pack folder holds (archives, survey, report).
    let hits = brand.scan_paths(&[out_dir.clone()])?;
    for h in &hits {
        println!("BRANDING LEFT: {h}");
    }
    if !hits.is_empty() {
        bail!("{} branded files or strings left in the HD pack", hits.len());
    }
    println!("brand gate: clean");
    Ok(())
}

/// Our layers past the stock chain (`patch-?` after `patch-3`), as the client's
/// `wotlk::is_built_layer`.
pub fn is_built_layer(archive: &str) -> bool {
    let n = archive.to_ascii_lowercase();
    n.strip_prefix("patch-")
        .and_then(|r| r.strip_suffix(".mpq"))
        .is_some_and(|c| c.len() == 1 && c > "3")
}

pub fn survey_tsv(entries: &[Entry]) -> String {
    let mut s = String::from("path\tarchive\tbytes\tclass\tours\tverdict\tdetail\n");
    for e in entries {
        let (verdict, detail) = match &e.out {
            None => ("kept".to_string(), e.note.clone()),
            Some(o) => {
                let d = match o {
                    Out::Decode(m) | Out::TextureMismatch(m) => m.clone(),
                    _ => String::new(),
                };
                (o.label(), d)
            }
        };
        s.push_str(&format!(
            "{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
            e.name,
            HD_ARCHIVES[e.archive],
            e.size,
            class_of(&e.name),
            e.ours.label(),
            verdict,
            detail.replace(['\t', '\n'], " ")
        ));
    }
    s
}

/// The survey's summary tables: per archive by class, per class by verdict, and the decode
/// failures by reason.
pub fn report(entries: &[Entry], hd: &HdSide) -> String {
    let gb = |b: u64| format!("{:.2}", b as f64 / 1e9);
    let mut s = String::new();
    s.push_str("## Per archive (files where the archive is the top copy)\n\n| archive | on disk GB | files | top classes |\n|---|---:|---:|---|\n");
    for (i, a) in &hd.archives {
        let mine: Vec<&Entry> = entries.iter().filter(|e| e.archive == *i).collect();
        let mut by: BTreeMap<&str, (usize, u64)> = BTreeMap::new();
        for e in &mine {
            let c = by.entry(class_of(&e.name)).or_default();
            c.0 += 1;
            c.1 += e.size;
        }
        let mut v: Vec<_> = by.into_iter().collect();
        v.sort_by(|a, b| b.1 .1.cmp(&a.1 .1));
        let top: Vec<String> = v
            .iter()
            .take(4)
            .map(|(c, (n, b))| format!("{c} {n} ({} GB)", gb(*b)))
            .collect();
        s.push_str(&format!(
            "| {} | {} | {} | {} |\n",
            HD_ARCHIVES[*i],
            gb(std::fs::metadata(a.path()).map(|m| m.len()).unwrap_or(0)),
            mine.len(),
            top.join(", ")
        ));
    }
    s.push_str("\n## Per class\n\n| class | files | GB | kept | kept GB | our layer wins | other reasons out |\n|---|---:|---:|---:|---:|---:|---|\n");
    let mut by: BTreeMap<&str, Vec<&Entry>> = BTreeMap::new();
    for e in entries {
        by.entry(class_of(&e.name)).or_default().push(e);
    }
    for (c, es) in &by {
        let kept: Vec<_> = es.iter().filter(|e| e.out.is_none()).collect();
        let ourl = es.iter().filter(|e| e.out == Some(Out::OurLayer)).count();
        let mut reasons: BTreeMap<String, usize> = BTreeMap::new();
        for e in es.iter() {
            if let Some(o) = &e.out {
                if *o != Out::OurLayer {
                    *reasons.entry(o.label()).or_default() += 1;
                }
            }
        }
        let r: Vec<String> = reasons.iter().map(|(k, v)| format!("{k} {v}")).collect();
        s.push_str(&format!(
            "| {c} | {} | {} | {} | {} | {ourl} | {} |\n",
            es.len(),
            gb(es.iter().map(|e| e.size).sum()),
            kept.len(),
            gb(kept.iter().map(|e| e.size).sum()),
            r.join(", ")
        ));
    }
    let mut ours: BTreeMap<&str, usize> = BTreeMap::new();
    for e in entries {
        *ours.entry(e.ours.label()).or_default() += 1;
    }
    s.push_str("\n## Against our side\n\n");
    for (k, v) in &ours {
        s.push_str(&format!("- {k}: {v}\n"));
    }
    let mut dec: BTreeMap<String, usize> = BTreeMap::new();
    for e in entries {
        if let Some(Out::Decode(m)) = &e.out {
            let k: String = m.chars().take(70).collect();
            *dec.entry(k).or_default() += 1;
        }
    }
    s.push_str("\n## Decode failures by reason\n\n");
    for (k, v) in &dec {
        s.push_str(&format!("- {v} x {k}\n"));
    }
    let mut sizes: BTreeMap<String, usize> = BTreeMap::new();
    for e in entries
        .iter()
        .filter(|e| e.out.is_none() && e.name.to_ascii_lowercase().ends_with(".blp"))
    {
        let w = e
            .note
            .split('x')
            .next()
            .and_then(|w| w.parse::<u32>().ok())
            .unwrap_or(0);
        let h = e
            .note
            .split('x')
            .nth(1)
            .and_then(|w| w.parse::<u32>().ok())
            .unwrap_or(0);
        *sizes.entry(format!("{:>5}", w.max(h))).or_default() += 1;
    }
    s.push_str("\n## Kept textures by largest side\n\n");
    for (k, v) in &sizes {
        s.push_str(&format!("- {}: {v}\n", k.trim()));
    }
    let kept: Vec<_> = entries.iter().filter(|e| e.out.is_none()).collect();
    s.push_str(&format!(
        "\nTotal: {} files listed ({} GB); kept {} files ({} GB)\n",
        entries.len(),
        gb(entries.iter().map(|e| e.size).sum()),
        kept.len(),
        gb(kept.iter().map(|e| e.size).sum())
    ));
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn path_rules_keep_art_and_leave_the_world_and_ours() {
        assert_eq!(by_path("Creature\\Bear\\Bear.m2", Ours::Stock), None);
        assert_eq!(
            by_path("Creature\\Bear\\Bear.m2", Ours::Community),
            Some(Out::OurLayer)
        );
        assert_eq!(
            by_path("Creature\\Bear\\Bear.m2", Ours::Wotlk),
            Some(Out::OurLayer)
        );
        assert_eq!(
            by_path("DBFilesClient\\Spell.dbc", Ours::Stock),
            Some(Out::Table)
        );
        assert_eq!(
            by_path("Interface\\FrameXML\\UIParent.lua", Ours::Stock),
            Some(Out::UiCode)
        );
        assert_eq!(
            by_path("Interface\\Buttons\\UI-Quickslot.blp", Ours::Stock),
            None
        );
        assert_eq!(
            by_path("World\\Maps\\Azeroth\\Azeroth_32_48.adt", Ours::Stock),
            Some(Out::Terrain)
        );
        assert_eq!(
            by_path("World\\wmo\\Azeroth\\Buildings\\Inn\\Inn.wmo", Ours::Stock),
            Some(Out::Wmo)
        );
        assert_eq!(
            by_path("TILESET\\Elwynn\\ElwynnGrass01.blp", Ours::Stock),
            None
        );
        assert_eq!(by_path("Sound\\Music\\x.mp3", Ours::Stock), None);
    }

    #[test]
    fn the_skin_composite_textures_are_told_from_model_textures() {
        assert!(composited(r"Character\Human\Male\HumanMaleSkin00_00.blp"));
        assert!(composited(
            r"Character\Human\Male\HumanMaleFaceLower00_00.blp"
        ));
        assert!(composited(
            r"Character\Human\Male\HumanMaleFacialLowerHair00_00.blp"
        ));
        assert!(composited(r"Character\Human\ScalpLowerHair00_00.blp"));
        assert!(composited(
            r"ITEM\TextureComponents\ArmUpperTexture\Plate_A_01_AU_U.blp"
        ));
        assert!(!composited(r"Character\Human\Hair00_00.blp"));
        assert!(!composited(r"Character\Human\Male\HumanMale.m2"));
        assert!(!composited(r"Creature\Bear\Bear.blp"));
    }

    #[test]
    fn archive_names_never_mount_from_a_data_folder() {
        for i in 0..20 {
            let n = archive_name(i);
            assert!(!is_built_layer(&n));
            assert!(!n.to_ascii_lowercase().starts_with("patch"));
        }
    }

    #[test]
    fn split_keeps_parts_under_the_cap() {
        let es: Vec<Entry> = (0..10)
            .map(|i| Entry {
                name: format!("Creature\\X\\{i}.blp"),
                archive: 0,
                size: SPLIT_BYTES / 3,
                ours: Ours::Stock,
                out: None,
                note: String::new(),
            })
            .collect();
        let kept: Vec<&Entry> = es.iter().collect();
        let parts = split(&kept);
        assert_eq!(parts.len(), 4);
        assert_eq!(parts.iter().map(Vec::len).sum::<usize>(), 10);
        for p in &parts {
            assert!(p.iter().map(|&i| kept[i].size).sum::<u64>() <= SPLIT_BYTES);
        }
    }
}
