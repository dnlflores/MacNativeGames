# Working on the Zelda iOS ports

Read `CONTRIBUTING.md` before changing this repository. BotW and TotK share
`games/totk`; select the game with `GAME_BOTW` / `GAME_BOTW160`. Leave other games'
independent runtime copies alone. The current device target for this workspace is
an iPad Pro (M4). Preserve existing work and obtain user authorization before
committing or pushing. Never force-push shared history.

## Content and build boundaries

- Never commit game binaries, RomFS, shaders translated from game content, keys,
  saves, extracted icons, provisioning profiles, private device logs, or IPAs.
  Keep owner data and prepared app builds outside this checkout.
- Native ARM64 execution and ahead-of-time rewriting are architectural requirements;
  do not add a CPU emulator, JIT, or runtime code generation.
- iOS cannot patch executable pages. Export game patches on the Mac and embed the
  resulting text. The iOS path verifies the embedded words and digest. Launch-time
  choices must reach game code through writable data, not modified instructions.
- Preserve original-instruction checks and input hashes. Do not patch an already
  rewritten executable by hand. Keep prepared objects and large app bundles when
  doing app-only builds; deleting them requires the owner's authorization.
- The launcher uses `setenv(..., 0)`: explicit diagnostic launch variables override
  saved preferences. Check the actual environment and installed build before
  attributing a screenshot to a candidate.

## Confirmed Magnesis readback finding

BotW 1.6.0 could show metal pink without ever selecting it yellow. Mods off,
30 fps, and handheld mode did not resolve it. The CPU picking buffer was stale:
small linear GPU readback targets shared a 16 KiB dirty-tracking page with other
CPU-written buffers. The page freshness heuristic suppressed GPU writeback.

`mtl_pool_invalidate` now treats a full invalidation of a linear texture as an
explicit discard of its CPU copy. `wb_write_list` honors that invalidation while
retaining freshness checks for tiled textures and partial ranges. Reset the flag
when recording another GPU write. Do not replace this with game selection hacks,
forced depth values, or a blanket overwrite of partially invalidated CPU memory.
The owner confirmed Magnesis working on-device after this correction.

`TOTK_READBACK_TRACE=1` enables bounded in-process tracing (64 calls, configurable
startup delay). It logs ranges/callers, never pool contents. Diagnostics stay off
normally. A debugger may intercept intentional runtime faults and alter behavior.

## Display and menu findings

Keep BotW fullscreen enabled by default (`matchDisplay`, default YES). The owner
explicitly requires the game to fill the display and has confirmed gameplay is
working. Fix pause composition without disabling fullscreen, changing gameplay
projection, reducing quality, or reverting unrelated work. The temporary build 10
16:9 rollback was rejected and has been reversed. Preserve native resolution,
enhancements, mods, and the Magnesis correction.

Pause-menu capture/blur layers and clipping still require on-device validation.
See `docs/ios-display.md`. Do not call a global letterbox fallback a menu fix.

Findings useful for further investigation:

- Derive view dimensions before the loader allocates heaps. Changing only the
  drawable does not change game cameras, UI layout, or capture targets.
- Camera aspect overrides must preserve originally non-16:9 cameras, including
  menu previews. Verify all three BotW 1.6.0 sites; do not override every ratio.
- Layout/text shader families differ between BotW and TotK. Include instanced text
  when evaluating transforms. Shared unit-quad dimensions do not identify bounds.
- Project actual vertices using pending-update snapshots and divide by homogeneous
  W. A UI camera can be perspective. Account for the final clip-space X offset.
- Quick-select can sample GPU-produced 1280x720 and 640x360 captures even when the
  main canvas has another shape. Do not fit a captured HUD twice. Static artwork,
  atlases, panels and partial quads must not be classified as full-screen captures.
- Uniformly fitting some draws while leaving other passes untouched is insufficient:
  captures, blur passes, scissors, character preview, and final composition must
  agree. The remaining pause artifacts need actual draw/uniform evidence before
  another native-aspect release.
- No live window-resize/reallocation support has been established.

Check title, Continue/save selection, gameplay HUD, every D-pad quick-select,
Inventory (all categories), Adventure Log, System, map, transitions and cinematics
at the chosen aspect on-device. Also check Magnesis and normal enhanced gameplay.
Do not claim these passed from a screenshot of a different screen.

## Mod installation

Both launchers link to their own GameBanana/Nexus pages and import supported ZIP,
RAR and RAR5 packages in-process. See `docs/mods.md`. Only version-compatible
Switch RomFS assets are supported. ExeFS, cheat patches, Wii U mods, BNP/UKMM
packages and arbitrary multiple variants are not automatically compatible.

Keep extraction staged and bounded. Reject traversal, absolute paths, links,
special files, encrypted/split archives, duplicate or case-conflicting paths,
wrong title folders, mixed executable patches, and ambiguous RomFS roots. Limits
include a 512 MB archive, 2 GB expansion, 20,000 entries and extraction deadlines.
Preserve manifests and existing installed mods when an import fails. Keep the
pinned libarchive checksum and license notices with the in-process RAR reader;
no runtime helper processes on iOS.

## Build, test, deploy and report

- Run `python -m unittest discover -s tests` using a Python environment containing
  the repository's dependencies (including `cryptography`). Single-file imports
  can fail because `tests` is not a package. Inspect the final test status.
- Compile shared changes for macOS and iOS, and both game configurations when
  applicable. Compile-only validation is not a play test. ARM64 aspect probes use
  synthetic memory, never game data.
- Full rebuilds require owner-provided keys. An app-only relink can reuse prepared
  game objects and data. Never silently drop digest checks or decrease resolution.
- Installation uses `games/totk/ios/tools/install_device.py`: bounded discovery,
  live connection/lock check, signing identity check, signing, signature verification,
  transfer, installed-build verification, and launch. An update preserves Documents;
  do not uninstall to fix a deployment problem.
- Query the installed bundle version after transfer; local bundle version or a
  launch succeeding alone does not prove the update arrived.
- CoreDevice may time out even when the iPad is unlocked. Keep separate stage
  deadlines and useful logs. Device identifiers and signing material belong in
  local wrappers, not investigation notes or committed artifacts.
- This tool session may see zero signing identities although the owner's Terminal
  can sign. Report the exact failure. Do not claim installation, fabricate success,
  bypass access controls, or repeatedly retry an unchanged signing failure.
- Launch normally for visual validation. `TOTK_SKIP_LAUNCHER` may bypass mod
  preparation; reduced-resolution diagnostics can look like lost enhancements.
  Pull logs before restarting (they are overwritten); use actual screenshots.
- Report Mac/iOS compilation, synthetic tests and on-device results separately.
  If network/authentication or signing blocks push/install, say what remains.
