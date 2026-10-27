//! Mac test driver for the BCML / UKMM bridge (src/lib.rs):
//!   ukmm-bridge convert <mod.bnp> <romfs dir> <aoc dir|-> <work dir> <out.zip>
//!   ukmm-bridge merge <mods.json> <romfs dir> <aoc dir|-> <out dir>
//!   ukmm-bridge pack <mod romfs dir> <name> <romfs dir> <aoc dir|-> <work dir> <out.zip>
use std::path::Path;

fn main() -> anyhow::Result<()> {
    let a: Vec<String> = std::env::args().collect();
    let aoc = |s: &str| (s != "-").then(|| Path::new(s).to_path_buf());
    match a.get(1).map(String::as_str) {
        Some("convert") if a.len() == 7 => {
            println!("{}", totk_ukmm::convert(a[2].as_ref(), a[3].as_ref(), aoc(&a[4]).as_deref(), a[5].as_ref(), "USen", a[6].as_ref())?);
        }
        Some("merge") if a.len() == 6 => {
            totk_ukmm::merge(&std::fs::read_to_string(&a[2])?, a[3].as_ref(), aoc(&a[4]).as_deref(), "USen", a[5].as_ref())?;
            println!("merged into {}", a[5]);
        }
        Some("pack") if a.len() == 8 => {
            println!("{}", totk_ukmm::pack(a[2].as_ref(), &a[3], a[4].as_ref(), aoc(&a[5]).as_deref(), a[6].as_ref(), a[7].as_ref())?);
        }
        _ => anyhow::bail!("usage: ukmm-bridge convert <mod.bnp> <romfs> <aoc|-> <work> <out.zip> | merge <mods.json> <romfs> <aoc|-> <out> | pack <mod romfs> <name> <romfs> <aoc|-> <work> <out.zip>"),
    }
    Ok(())
}
