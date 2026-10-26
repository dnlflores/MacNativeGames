# Player mods (RomFS)

## Tears of the Kingdom on iPhone / iPad

The TotK start screen has the same automatic ZIP/RAR installer described below, with links to
[GameBanana](https://gamebanana.com/games/7617) and
[Nexus Mods](https://www.nexusmods.com/games/legendofzeldatearsofthekingdom/mods).
Open **Mods**, browse either site, and tap a supported ZIP or RAR download to install and enable it.
The installed game version is shown in the instructions: choose a RomFS mod for that exact version
(the owner's prepared iPad build is 1.1.2). Version compatibility cannot be inferred from arbitrary
asset files, so the installer validates package structure rather than guaranteeing a mod will work.

TotK Atmosphere downloads may wrap `romfs` in `atmosphere/contents/0100F2C0115B6000`.
Archives with another game's title folder are rejected, as are mixed packages containing `exefs`,
cheats/code patches or multiple RomFS variants. Emulator patches and graphics mods do not work here.
The same archive limits, download/login flow and manual folder fallback below apply to both games.
This port reserves some UI/heap files (see Notes below); mods replacing those can still fail at Play.

## Breath of the Wild on iPhone / iPad

The BotW 1.6.0 iOS source includes a start screen: open **Mods** before pressing **Play**.
It supports base-game **Switch RomFS** replacements and additions. Compatibility must still be
checked for each mod; being listed on a mod site does not make a download compatible with this port.

The Mods screen links to [GameBanana's Switch section](https://gamebanana.com/games/6386) and
[Nexus Mods](https://www.nexusmods.com/games/legendofzeldabreathofthewild/mods).
These open an in-app WebKit browser. Tap the site's ZIP or RAR download to download, extract, validate,
install and enable the mod automatically, then return to the Mods list. Nexus login, file selection,
and any download steps required by the site still apply. External `nxm:` manager links are not
supported; choose the site's manual ZIP or RAR download. The app does not bypass download permissions.

Automatic installation accepts ZIP and RAR archives up to 512 MB compressed and 2 GB extracted, with no more than
20,000 entries and exactly one nonempty `romfs` folder (including inside Atmosphere wrappers).
Stored/deflated ZIPs and RAR/RAR5 archives are supported. RAR decoding uses pinned libarchive 3.8.9. Encrypted/split/ZIP64 archives, unsafe or duplicate paths,
links, and packages containing code patches, DLC replacements or Wii U content are rejected. RAR extraction also has a two-minute time limit.
Each successful download gets a new mod folder, preserving previous installs. Reinstalling a mod
creates a separate entry; disable or delete the old entry in Mods. Multiple installed mods still
follow the launcher's file replacement order, without content merging.

For 7z downloads, unsupported package layouts, or a site that blocks embedded browsing, use
the existing manual folder import:

1. Download a mod explicitly made for Switch BotW 1.6.0 and extract the archive in Files
   (use an archive app if Files cannot extract its format).
2. Return to **Mods → Import a mod folder…**. Select the folder directly containing `romfs`,
   or the `romfs` folder itself. For an Atmosphere package, select
   `atmosphere/contents/01007EF00011E000`, rather than the whole extracted archive.
3. Enable the imported mod, arrange its order, and press Play. A lower mod wins when two change
   the same file; the launcher does not merge the contents of that file. Back up saves first.

Limitations:

- Wii U/Cemu mods are not directly compatible with this Switch build.
- UKMM packages (`.zip` from UKMM) are not imported directly; BCML packages (`.bnp`) are (below).
- `exefs`, IPS/pchtxt code patches, cheats, and emulator graphics packs are not loaded by this screen.
- RomFS mod folders cannot replace DLC files (title `01007EF00011F001`); BCML packages can.
- Large overhauls may need merging, resource-size-table changes, or runtime features this port
  does not support. No individual downloaded mod has been tested by this change.

BCML packages (`.bnp`, BotW):

- Download a `.bnp` in the in-app browser, or use **Mods → Import a BCML package (.bnp)…** for a file in Files.
  UKMM ([U-King Mod Manager](https://github.com/NiceneNerd/ukmm), GPL-3.0-or-later, pinned by
  `games/totk/ios/tools/build_ukmm.py`) converts it against the app's own game data (`games/totk/tools/ukmm_bridge`).
  The package becomes an entry in Mods; tap it to choose its options (required groups start on their default
  or first option).
- Play merges every enabled package with UKMM's mergers (packs, game data and save flags, actor info, map units,
  texts, parameter deep merges, the resource size table), in the Mods order, into the lowest layer: RomFS mod folders
  load above it and replace whole files. The merge is redone only when the packages, their order or options, or the
  game language change. Text merges use the language the game runs in (US English, or the EU/JP/KR/CN text of the
  device language, as `runtime/services.c` reports it).
- DLC files of the merged packages are served by `runtime/aoc_romfs.c` (`TOTK_AOC_MODS`, a manifest like the base
  game's).
- Test launches: `TOTK_BNP_INSTALL=<file>` installs a package and `TOTK_MOD_REMOVE=<name>` deletes a mod before
  `TOTK_SKIP_LAUNCHER=1` starts the game (Play's preparation runs first).

These controls are part of the source build; an older installed app needs rebuilding and reinstalling
to receive them.

[English](#english) | [中文](#chinese)

<a id="english"></a>
## English

The Mac app loads **RomFS mods only**. `exefs/` files are not loaded (the runtime uses the rewritten
`main`, `sdk` and `subsdk0`, and IPS or pchtxt files are not picked up automatically), and the app does
not scan folders for mods. Whether a mod is used is decided by one file: `manifest.tsv`.

The launcher points `TOTK_ROMFS_MODS` at

```text
~/Library/Application Support/MacNativeGames/TOTK/mods/<version>
```

where `<version>` is the game version the app was built from: `v0` (1.0.0), `v112` (1.1.2), `v121`
(1.2.1) or `v143` (1.4.3).

### What `manifest.tsv` is

Only the files listed in `manifest.tsv` are served. Each line has four tab-separated fields:

```text
<RomFS path>	<size>	<SHA-256 of the mod file>	<SHA-256 of the original, or ->
```

At startup the loader checks each listed file's size and SHA-256, and checks the original's SHA-256
against the game image. `-` in the last field adds a file the game's RomFS does not have (used by mods
that add assets). `*` in the last field replaces the file if the RomFS has it and adds it otherwise,
without checking the original (the iPhone / iPad start screen writes this: it cannot hash files inside
the packed RomFS; the mod file itself is still checked). Paths are case-sensitive and keep the RomFS
spelling.

On iPhone and iPad, the start screen's **Mods** section manages this for you: put each mod in its own
folder under `Mods` (Files app, On My iPad, the game), switch it on, and press Play. Enabled mods are
linked into one folder with a generated manifest; a lower mod in the list wins a path both change.

A folder without `manifest.tsv` means "no mod". Files copied into the folder but not listed are not
used. In detail:

- A **new path** that is not listed is treated as absent: the game cannot see the file.
- A **replacement whose size or hash changed** without regenerating the manifest stops the start with
  `RomFS overlay file missing or wrong size` or `RomFS overlay integrity mismatch`.
- A path that exists in the game but is not listed is read from the game, so the copied file has no
  effect.

### Install a mod

1. Quit the game completely.
2. Copy the mod's RomFS paths **directly** into the version folder. If the mod ships a `romfs/` folder,
   copy its contents, not the folder itself:

   ```bash
   MODDIR="$HOME/Library/Application Support/MacNativeGames/TOTK/mods/v121"
   rsync -a --exclude='.DS_Store' --exclude='manifest.tsv' "/path/to/mod/romfs/" "$MODDIR/"
   ```

3. Generate the manifest with the app you built. Run this from the repository root:

   ```bash
   python3 games/totk/scripts/make_mod_manifest.py "$MODDIR" \
     --app "/path/to/Tears of the Kingdom.app" --version v121
   ```

   The script reads `Contents/Resources/totk/extracted/<version>/romfs` from the app, hashes each file
   and its original, leaves out files that are identical to the original, and writes `manifest.tsv`.
   It prints a line such as `...: 12 files (10 replaced, 2 added)`.

4. Start the game and check the log:

   ```bash
   grep "mods/v121 checked" "$HOME/Library/Logs/MacNativeGames/totk.log" | tail -1
   ```

   It should say `.../mods/v121 checked, N files`; additions also print `M files added to the RomFS`.

### Switching versions

The game reads the installed folder as it is; it does not follow the folder you downloaded or unpacked.
If the new version removes files, set the old version folder aside (or delete it after a backup) before
copying, or the removed files stay active. Generate the manifest again after every change.

### Notes

- Do not edit `manifest.tsv` by hand; regenerate it.
- Do not change files while the game runs. The loader records each file's identity at start and stops
  if a file changes mid-session.
- There is one overlay directory for player mods: to use several mods, merge their files into one
  version folder and generate one manifest. Do not nest mod folders. If two mods change the same path,
  choose one file.
- The build replaces a few RomFS files itself: `UI/LayoutArchive/Common.Product.<n>.Nin_NX_NVN.blarc.zs` (quick-menu
  timing; `<n>` is 100, 110 or 140 depending on the version), `Event/EventFlow/DmF_SY_GameOver.bfevfl.zs`, and with the
  2K output `System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs`. A mod that replaces one of them stops the start with
  "two overlays replace the same file"; leave that file out of the mod for now (UI translation mods hit this, #73).
- `exefs/` is not loaded; only RomFS content works. (`TOTK_PCHTXT` is a developer experiment for pchtxt
  patches, not a general mod loader.)
- For iPhone/iPad imports, see the Breath of the Wild section above and the start screen's Mods section.

<a id="chinese"></a>
## 中文

Mac App 只加载 **RomFS mod**。`exefs/` 里的文件不会被加载（运行时用的是改写后的 `main`、`sdk` 和
`subsdk0`，IPS 和 pchtxt 不会自动生效），App 也不会扫描文件夹找 mod。是否使用某个 mod，完全由
`manifest.tsv` 决定。

启动器把 `TOTK_ROMFS_MODS` 指向

```text
~/Library/Application Support/MacNativeGames/TOTK/mods/<version>
```

`<version>` 是构建这个 App 时使用的游戏版本：`v0`（1.0.0）、`v112`（1.1.2）、`v121`（1.2.1）、
`v143`（1.4.3）。

### manifest.tsv 是什么

只有列在 `manifest.tsv` 里的文件才会被提供给游戏。每一行是四个用制表符分开的字段：

```text
<RomFS 路径>	<大小>	<mod 文件的 SHA-256>	<原版文件的 SHA-256，或 ->
```

启动时加载器会逐个核对 mod 文件的大小和 SHA-256，并核对原版文件的 SHA-256。最后一个字段是 `-`
表示这是游戏 RomFS 里没有的新文件（用于新增资源）。路径区分大小写，必须和 RomFS 里的写法一致。

没有 `manifest.tsv` 就等于没有 mod。只把文件复制进目录但不写进清单，游戏不会使用：

- 清单里没有的**新路径**会被当成不存在，游戏完全看不到这个文件。
- 覆盖已有文件但大小或哈希变了、又没有重新生成清单，启动会直接报
  `RomFS overlay file missing or wrong size` 或 `RomFS overlay integrity mismatch`。
- 原版有这个路径、但清单里没列，游戏会读取原版文件，复制进去的文件看起来“没有效果”。

### 安装步骤

1. 先完全退出游戏。
2. 把 mod 的 RomFS 路径**直接**放进版本目录。如果 mod 带一层 `romfs/` 文件夹，复制的是它里面的
   内容，不是 `romfs` 这一层：

   ```bash
   MODDIR="$HOME/Library/Application Support/MacNativeGames/TOTK/mods/v121"
   rsync -a --exclude='.DS_Store' --exclude='manifest.tsv' "/path/to/mod/romfs/" "$MODDIR/"
   ```

3. 用你构建好的 App 生成清单。在仓库根目录执行：

   ```bash
   python3 games/totk/scripts/make_mod_manifest.py "$MODDIR" \
     --app "/path/to/Tears of the Kingdom.app" --version v121
   ```

   脚本会读取 App 里的 `Contents/Resources/totk/extracted/<version>/romfs`，计算每个文件和原版
   文件的哈希，跳过与原始完全相同的文件，然后写入 `manifest.tsv`。成功时会打印类似
   `...: 12 files (10 replaced, 2 added)` 的一行。

4. 启动游戏，确认日志：

   ```bash
   grep "mods/v121 checked" "$HOME/Library/Logs/MacNativeGames/totk.log" | tail -1
   ```

   应出现 `.../mods/v121 checked, N files`；有新增文件时还会打印 `M files added to the RomFS`。

### 换版本时

游戏读取的是安装目录本身，不会跟着你下载或解压的那个文件夹走。如果新版本删掉了旧版本的某些
文件，先把旧的版本目录整个移走（或备份后删除）再复制，否则被删掉的文件会继续生效。每次改动后
都要重新生成清单。

### 注意

- 不要手改 `manifest.tsv`，每次重新生成。
- 游戏运行中不要改 mod 文件；加载器会在启动时记录每个文件的身份，运行中变化会停止游戏。
- 玩家 mod 只有一个覆盖目录：多个 mod 就合并到同一个版本目录，最后生成一份清单；不要把 mod
  文件夹嵌套进去。两个 mod 改同一个路径时，只能选一个文件。
- 构建本身也会替换几个 RomFS 文件：`UI/LayoutArchive/Common.Product.<n>.Nin_NX_NVN.blarc.zs`（快捷菜单时机，`<n>`
  按版本是 100、110 或 140）、`Event/EventFlow/DmF_SY_GameOver.bfevfl.zs`，开 2K 输出时还有
  `System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs`。模组如果也替换其中一个，启动时会报“two overlays replace the
  same file”并停止，目前只能把那个文件从模组里去掉（界面翻译模组会碰到，见 #73）。
- `exefs/` 不会被加载，只有 RomFS 内容生效。（`TOTK_PCHTXT` 是开发者的 pchtxt 实验入口，不是
  通用的 mod 加载器。）
- 本文面向 Mac App；iPhone/iPad 的 App 容器内安装未在此说明。
