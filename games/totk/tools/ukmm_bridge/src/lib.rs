//! BCML (.bnp) mods for the Breath of the Wild port: UKMM converts a .bnp (or packs a RomFS mod folder) into a UKMM
//! mod and merges the enabled mods into one RomFS layer, reading the vanilla files from the port's own RomFS images
//! (runtime-index.bin + romfs.bin, or romfs.bin.000, .001, ... on the iPhone / iPad) instead of an unpacked dump. Called from the iOS launcher
//! (launcher.m) through the C functions below; src/main.rs runs the same steps on the Mac for tests.
//!
//! GPL-3.0-or-later like UKMM (github.com/NiceneNerd/ukmm), which ios/tools/build_ukmm.py fetches at a pinned commit.

use std::{
    collections::HashMap,
    ffi::{CStr, CString, c_char},
    fs::File,
    os::unix::fs::FileExt,
    path::{Path, PathBuf},
    sync::{Arc, LazyLock},
};

use anyhow::{Context, Result, bail};
use dashmap::DashMap;
use fs_err as fs;
use parking_lot::Mutex;
use roead::yaz0::{compress, decompress};
use rstb::ResourceSizeTable;
use serde::{Deserialize, Serialize};
use uk_content::{constants::Language, platform_content, platform_prefixes, prelude::Endian};
use uk_manager::settings::{Platform, PlatformSettings, Settings, UpdatePreference};
use uk_mod::{Meta, ModOption, OptionGroup, unpack::{ModReader, ModUnpacker}};
use uk_reader::{ResourceLoader, ResourceReader};

/* ---------- the port's RomFS images ---------- */

/// One indexed image: sorted names and their (offset, size) in romfs.bin or its equal-sized parts.
struct Image {
    names: Vec<String>,
    spans: Vec<(u64, u64)>,
    parts: Vec<File>,
    part_size: u64,
}

impl Image {
    fn open(dir: &Path) -> Result<Self> {
        let index = fs::read(dir.join("runtime-index.bin"))?;
        if index.len() < 24 || &index[..8] != b"TOTKRFS1" {
            bail!("{}: not a RomFS index", dir.display());
        }
        let count = u64::from_le_bytes(index[8..16].try_into()?) as usize;
        let strings = u64::from_le_bytes(index[16..24].try_into()?) as usize;
        if strings != 24 + count * 24 || strings > index.len() {
            bail!("{}: RomFS index layout", dir.display());
        }
        let (mut names, mut spans) = (Vec::with_capacity(count), Vec::with_capacity(count));
        for i in 0..count {
            let e = &index[24 + i * 24..48 + i * 24];
            let offset = u64::from_le_bytes(e[0..8].try_into()?);
            let size = u64::from_le_bytes(e[8..16].try_into()?);
            let name = u32::from_le_bytes(e[16..20].try_into()?) as usize;
            let len = u32::from_le_bytes(e[20..24].try_into()?) as usize;
            let bytes = index.get(strings + name..strings + name + len.saturating_sub(1)).context("RomFS name bounds")?;
            names.push(String::from_utf8(bytes.to_vec())?);
            spans.push((offset, size));
        }
        let whole = dir.join("romfs.bin");
        let parts = if whole.exists() {
            vec![File::open(&whole)?]
        } else {
            (0..)
                .map(|k| dir.join(format!("romfs.bin.{k:03}")))
                .take_while(|p| p.exists())
                .map(File::open)
                .collect::<std::io::Result<Vec<_>>>()?
        };
        let part_size = parts.first().context("RomFS image missing")?.metadata()?.len();
        Ok(Self { names, spans, parts, part_size })
    }

    fn find(&self, name: &str) -> Option<usize> {
        self.names.binary_search_by(|n| n.as_str().cmp(name)).ok()
    }

    fn read(&self, name: &str) -> Option<Vec<u8>> {
        let (mut offset, size) = self.spans[self.find(name)?];
        let mut out = vec![0u8; size as usize];
        let mut done = 0usize;
        while done < out.len() {
            let part = (offset / self.part_size) as usize;
            let within = offset % self.part_size;
            let n = ((self.part_size - within) as usize).min(out.len() - done);
            self.parts.get(part)?.read_exact_at(&mut out[done..done + n], within).ok()?;
            done += n;
            offset += n as u64;
        }
        Some(out)
    }
}

static IMAGES: LazyLock<Mutex<HashMap<PathBuf, Arc<Image>>>> = LazyLock::new(Default::default);

fn image(dir: &Path) -> Result<Arc<Image>> {
    let mut images = IMAGES.lock();
    if let Some(i) = images.get(dir) {
        return Ok(i.clone());
    }
    let i = Arc::new(Image::open(dir)?);
    images.insert(dir.to_path_buf(), i.clone());
    Ok(i)
}

/// UKMM's view of the game: the port's RomFS image holds base and update merged; the add-on content image the DLC.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TotkImageLoader {
    base: PathBuf,
    aoc: Option<PathBuf>,
}

fn rel(name: &Path) -> String {
    name.to_string_lossy().replace('\\', "/").trim_start_matches('/').to_string()
}

impl TotkImageLoader {
    fn base_file(&self, name: &Path) -> uk_reader::Result<Vec<u8>> {
        image(&self.base)
            .ok()
            .and_then(|i| i.read(&rel(name)))
            .ok_or_else(|| uk_reader::ROMError::FileNotFound(rel(name).into(), self.base.clone()))
    }
}

#[typetag::serde]
impl ResourceLoader for TotkImageLoader {
    fn get_base_file_data(&self, name: &Path) -> uk_reader::Result<Vec<u8>> {
        self.base_file(name)
    }

    fn get_update_file_data(&self, name: &Path) -> uk_reader::Result<Vec<u8>> {
        self.base_file(name)
    }

    fn get_aoc_file_data(&self, name: &Path) -> uk_reader::Result<Vec<u8>> {
        let dir = self.aoc.as_ref().ok_or_else(|| uk_reader::ROMError::MissingDumpDir("DLC", self.base.clone()))?;
        image(dir)
            .ok()
            .and_then(|i| i.read(&rel(name)))
            .ok_or_else(|| uk_reader::ROMError::FileNotFound(rel(name).into(), dir.clone()))
    }

    fn file_exists(&self, name: &Path) -> bool {
        let n = rel(name);
        std::iter::once(&self.base)
            .chain(self.aoc.iter())
            .any(|d| image(d).map(|i| i.find(&n).is_some()).unwrap_or(false))
    }

    fn host_path(&self) -> &Path {
        &self.base
    }
}

/* ---------- conversion and merging ---------- */

fn dump(base: &Path, aoc: Option<&Path>) -> Result<Arc<ResourceReader>> {
    let source = serde_json::json!({
        "bin_type": "Nintendo",
        "source": {"type": "TotkImageLoader", "base": base, "aoc": aoc},
        "endian": "Switch",
    });
    Ok(Arc::new(serde_json::from_value(source).context("RomFS reader")?))
}

fn settings(work: &Path, dump: Arc<ResourceReader>, lang: Language) -> Settings {
    Settings {
        current_mode: Platform::Switch,
        system_7z: false,
        storage_dir: work.to_path_buf(),
        check_updates: UpdatePreference::None,
        show_changelog: false,
        last_version: None,
        wiiu_config: None,
        switch_config: Some(PlatformSettings { language: lang, profile: "Default".into(), dump, deploy_config: None }),
        lang: uk_localization::LocLang::English,
    }
}

fn language(code: &str) -> Language {
    serde_json::from_value(serde_json::Value::String(code.into())).unwrap_or(Language::USen)
}

/// Converts the .bnp at `bnp` into the UKMM mod `out_zip`; returns its metadata (name, version, options) as JSON.
pub fn convert(bnp: &Path, base: &Path, aoc: Option<&Path>, work: &Path, lang: &str, out_zip: &Path) -> Result<String> {
    fs::create_dir_all(work)?;
    let core = uk_manager::core::Manager::with_settings(settings(work, dump(base, aoc)?, language(lang)))?;
    let packed = uk_manager::bnp::convert_bnp(&core, bnp)?;
    if let Some(parent) = out_zip.parent() {
        fs::create_dir_all(parent)?;
    }
    if fs::rename(&packed, out_zip).is_err() {
        fs::copy(&packed, out_zip)?;
        fs::remove_file(&packed)?;
    }
    let meta: Meta = ModReader::open_peek(out_zip, vec![])?.meta;
    Ok(serde_json::to_string(&meta)?)
}

/// Packs a RomFS mod folder (`romfs` holds files under the game's own paths) into the UKMM mod `out_zip` called `name`, so
/// its edits to shared files (actor list, shop lists, game flags, texts) merge with other mods instead of replacing
/// those files whole; returns its metadata as JSON.
pub fn pack(romfs: &Path, name: &str, base: &Path, aoc: Option<&Path>, work: &Path, out_zip: &Path) -> Result<String> {
    fn stage(from: &Path, to: &Path, top: bool) -> Result<()> {
        for entry in fs::read_dir(from)? {
            let entry = entry?;
            let file = entry.file_name();
            let file = file.to_string_lossy();
            if file.starts_with('.') || (top && (file == "manifest.tsv" || file == "exefs")) {
                continue;
            }
            let (src, dst) = (entry.path(), to.join(&*file));
            if src.is_dir() {
                fs::create_dir_all(&dst)?;
                stage(&src, &dst, false)?;
            } else if src.is_file() && fs::hard_link(&src, &dst).is_err() {
                fs::copy(&src, &dst)?;
            }
        }
        Ok(())
    }
    let staged = work.join("stage");
    if staged.exists() {
        fs::remove_dir_all(&staged)?;
    }
    let content = staged.join(platform_prefixes(Endian::Little).0);
    fs::create_dir_all(&content)?;
    stage(romfs, &content, true)?;
    let meta: Meta = serde_json::from_value(serde_json::json!({
        "name": name, "version": "", "author": "", "category": "Other", "description": "", "url": null,
        "platform": {"Specific": "Switch"}, "option_groups": [], "masters": {},
    }))?;
    if let Some(parent) = out_zip.parent() {
        fs::create_dir_all(parent)?;
    }
    let packed = uk_mod::pack::ModPacker::new(&staged, out_zip, Some(meta), vec![dump(base, aoc)?])
        .and_then(|p| p.pack())
        .with_context(|| format!("Failed to package {name}"));
    fs::remove_dir_all(&staged)?;
    packed?;
    let meta: Meta = ModReader::open_peek(out_zip, vec![])?.meta;
    Ok(serde_json::to_string(&meta)?)
}

#[derive(Deserialize)]
struct MergeMod {
    path: PathBuf,
    #[serde(default)]
    options: Vec<PathBuf>,
}

fn chosen(meta: &Meta, paths: &[PathBuf]) -> Vec<ModOption> {
    meta.options
        .iter()
        .flat_map(|g| match g {
            OptionGroup::Exclusive(g) => g.options.iter(),
            OptionGroup::Multiple(g) => g.options.iter(),
        })
        .filter(|o| paths.contains(&o.path))
        .cloned()
        .collect()
}

/// Merges the UKMM mods listed in `mods_json` ([{"path": zip, "options": [option paths]}], lowest priority first) into
/// `out_dir` (01007EF00011E000/romfs and 01007EF00011F001/romfs), with the resource size table updated for them.
pub fn merge(mods_json: &str, base: &Path, aoc: Option<&Path>, lang: &str, out_dir: &Path) -> Result<()> {
    let list: Vec<MergeMod> = serde_json::from_str(mods_json).context("Mod list")?;
    let readers = list
        .iter()
        .map(|m| {
            let meta = ModReader::open_peek(&m.path, vec![])?.meta;
            ModReader::open(&m.path, chosen(&meta, &m.options)).with_context(|| format!("Failed to open {}", m.path.display()))
        })
        .collect::<Result<Vec<_>, _>>()?;
    if out_dir.exists() {
        fs::remove_dir_all(out_dir)?;
    }
    fs::create_dir_all(out_dir)?;
    let updates = ModUnpacker::new(dump(base, aoc)?, Endian::Little, language(lang), readers, out_dir.to_path_buf()).unpack()?;
    apply_rstb(out_dir, updates)
}

/// The resource size table of the merged files (uk-manager deploy::Manager::apply_rstb, without its pending log).
fn apply_rstb(merged: &Path, updates: DashMap<smartstring::alias::String, Option<u32>>) -> Result<()> {
    const RSTB_PATH: &str = "System/Resource/ResourceSizeTable.product.srsizetable";
    let table_path = merged.join(platform_content(Endian::Little)).join(RSTB_PATH);
    let mut table = if table_path.exists() {
        ResourceSizeTable::from_binary(decompress(fs::read(&table_path)?)?).context("Failed to parse merged RSTB")?
    } else {
        ResourceSizeTable::new_from_stock(Endian::Little.into())
    };
    for (canon, size) in updates {
        match size {
            Some(size) => {
                if table.get(canon.as_str()).map(|s| s < size).unwrap_or(true) {
                    table.set(canon.as_str(), size);
                }
            }
            None => {
                table.remove(canon.as_str());
            }
        }
    }
    fs::create_dir_all(table_path.parent().context("RSTB path")?)?;
    fs::write(table_path, compress(table.to_binary(Endian::Little.into())))?;
    Ok(())
}

/// The content and add-on content folder names inside a merged layer.
pub fn layer_dirs() -> (&'static str, &'static str) {
    platform_prefixes(Endian::Little)
}

/* ---------- C interface (launcher.m) ---------- */

fn c_str(p: *const c_char) -> Option<String> {
    (!p.is_null()).then(|| unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
}

fn c_out(s: String) -> *mut c_char {
    CString::new(s.replace('\0', " ")).map(CString::into_raw).unwrap_or(std::ptr::null_mut())
}

fn run(f: impl FnOnce() -> Result<String> + std::panic::UnwindSafe, out: *mut *mut c_char) -> *mut c_char {
    match std::panic::catch_unwind(f) {
        Ok(Ok(value)) => {
            if !out.is_null() {
                unsafe { *out = c_out(value) };
            }
            std::ptr::null_mut()
        }
        Ok(Err(e)) => c_out(format!("{e:#}")),
        Err(_) => c_out("The mod manager stopped unexpectedly".into()),
    }
}

/// Converts a .bnp (see `convert`). Returns NULL on success with the metadata JSON in *meta_json, or an error message.
/// `aoc` may be NULL (no DLC). Free returned strings with totk_ukmm_free.
#[unsafe(no_mangle)]
pub extern "C" fn totk_ukmm_convert(bnp: *const c_char, base: *const c_char, aoc: *const c_char, work: *const c_char,
                                    lang: *const c_char, out_zip: *const c_char, meta_json: *mut *mut c_char) -> *mut c_char {
    let (bnp, base, aoc, work, lang, out_zip) = (c_str(bnp), c_str(base), c_str(aoc), c_str(work), c_str(lang), c_str(out_zip));
    run(move || {
        let (Some(bnp), Some(base), Some(work), Some(out_zip)) = (bnp, base, work, out_zip) else { bail!("Missing path") };
        convert(bnp.as_ref(), base.as_ref(), aoc.as_deref().map(Path::new), work.as_ref(), lang.as_deref().unwrap_or("USen"), out_zip.as_ref())
    }, meta_json)
}

/// Packs a RomFS mod folder (see `pack`). Returns NULL on success with the metadata JSON in *meta_json (when not NULL),
/// or an error message.
#[unsafe(no_mangle)]
pub extern "C" fn totk_ukmm_pack(mod_romfs: *const c_char, name: *const c_char, base: *const c_char, aoc: *const c_char,
                                 work: *const c_char, out_zip: *const c_char, meta_json: *mut *mut c_char) -> *mut c_char {
    let (mod_romfs, name, base, aoc, work, out_zip) = (c_str(mod_romfs), c_str(name), c_str(base), c_str(aoc), c_str(work), c_str(out_zip));
    run(move || {
        let (Some(mod_romfs), Some(name), Some(base), Some(work), Some(out_zip)) = (mod_romfs, name, base, work, out_zip) else { bail!("Missing path") };
        pack(mod_romfs.as_ref(), &name, base.as_ref(), aoc.as_deref().map(Path::new), work.as_ref(), out_zip.as_ref())
    }, meta_json)
}

/// Merges UKMM mods (see `merge`). Returns NULL on success, or an error message.
#[unsafe(no_mangle)]
pub extern "C" fn totk_ukmm_merge(mods_json: *const c_char, base: *const c_char, aoc: *const c_char, lang: *const c_char,
                                  out_dir: *const c_char) -> *mut c_char {
    let (mods, base, aoc, lang, out) = (c_str(mods_json), c_str(base), c_str(aoc), c_str(lang), c_str(out_dir));
    run(move || {
        let (Some(mods), Some(base), Some(out)) = (mods, base, out) else { bail!("Missing argument") };
        merge(&mods, base.as_ref(), aoc.as_deref().map(Path::new), lang.as_deref().unwrap_or("USen"), out.as_ref())?;
        Ok(String::new())
    }, std::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "C" fn totk_ukmm_free(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}
