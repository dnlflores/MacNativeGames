# Breath of the Wild: iPad display shape

Graphics → **Match display** is enabled by default. The game fills the display;
the resolution preset supplies the canvas width and taller screens add rows.
Turning it off explicitly selects original 16:9. Preserve this default while
fixing menu composition: the owner rejected the temporary build 10 letterbox
rollback. Native resolution, enhancements and mod preparation remain unchanged.

Pause-menu backgrounds are full-view quads that draw a screen capture back; they
retain their screen mapping (also when a scissor crops them) rather than receive
the ordinary UI fit a second time. That includes the page switch (L/R), which slides
the old page's capture out and the new page's backdrop in as view-sized quads
offset sideways; fitted, they shrank to the 16:9 band for the slide. Smaller panes that sample a capture are frosted
UI elements (quick-select slot frames and backgrounds, the inventory item card, the
item-name box over pickups): they follow the layout fit with their frames and text,
otherwise they sit off their frames. Static artwork and character previews remain
separate.

The frontend configures `TOTK_BOTW_RES`, `TOTK_OPERATION_MODE=docked`, and
`TOTK_ASPECT` before the loader sizes memory. `TOTK_IOS_MATCH_DISPLAY=0` keeps
the original canvas (an explicit user choice). Explicit `TOTK_ASPECT` takes precedence.

Three verified BotW 1.6.0 camera instructions load the aspect override from the
loader's data page (+56). The Mac export embeds the stubs into the iOS executable;
iOS verifies every patched word and retains text digest validation. A zero aspect
keeps the original instruction's result. The override applies only to a camera's original 16:9 ratio; cameras with another
aspect (such as menu previews) retain it. The experimental Metal path fits the ten observed
BotW layout shader families, including instanced text, into one centered 16:9
region over the full world. Full-size and downsampled screen targets attempt to use the same
rule; it does not depend on the preceding frame's last canvas texture. Fixed 16:9
menu artwork is fitted with its proportions and contents preserved. It may leave
bars in menus; 3D gameplay still fills the display.

GPU-produced screen captures keep screen mapping on full-view quads; solid fades
only on unscissored full-view quads; full-view artwork only when its quad overhangs
the view by more than 10% (the quick-select vignette reaches 1.78x the view height:
fitted, its edges showed as bars). See `botw_ui_screen_fill`. The renderer fetches vertices/indices and
uniforms from their pending-update snapshots and projects them to check coverage.
Shared quad dimensions alone do not establish an element's bounds. This avoids
mistaking menu panels, HUD icons or logos for backgrounds and enlarging them.
Screen captures retain their screen mapping, so a captured HUD is not fitted twice.
Quick-select's GPU-produced 1280×720 and 640×360 capture/blur textures qualify even
when their aspect differs from the native iPad canvas. The smallest qualifying size (1/8) is measured
against the 16:9 region of the target, so the shop sell screen's 320×180 backdrop capture
keeps screen mapping too (against the 1764-row canvas it was fitted, a squeezed ghost of
the scene behind the menu). Shop panes also blur a capture of the canvas region behind
them (region-capture program 17dccd27 into a small target such as the text box's 1224×315).
That capture reads the pane's layout rectangle, so it now reads the fitted rectangle (its
c3[6].xy scale times the fit); unfitted, the button hints under the text box showed through
it. Full-screen captures and strips of the view's whole height or width (the backdrop
behind the item list, 960×1764) keep screen mapping both when captured and drawn back. The map screen
draws its tiles into a layer the game sizes from the screen (3000×1764 on a 2560×1764 canvas)
and draws it back with an overhanging quad; that layer is layout and takes the fit like the
icons over it (with screen mapping it was 22% taller than its icons, which drifted while the
map moved). The map then sits in the 16:9 band like its frame. Static artwork and atlases
do not qualify. Geometry coverage uses projected coordinates after dividing by
homogeneous W; the UI can use a perspective camera rather than an orthographic one.
`TOTK_BOTW_UI_TRACE=1` enables bounded records of the chosen treatment.

The aspect is selected at game start; changing the window's shape requires restarting
the game. This does not provide live render-target reallocation during window resizing.
Menus, cinematics, and markers tied to world objects require on-device visual checks
after changes to this path; compilation and geometry tests alone cannot establish visual correctness. This setting is currently offered for BotW only.

Validation: both platforms compile; `tests/test_botw_aspect.py` executes all three
production ARM64 stubs on synthetic memory at original, iPad, 16:9 and ultrawide
ratios, checking the result, preservation of non-16:9 camera ratios, scratch registers,
condition flags and stack. `tests/test_botw_ui_aspect.py` checks layout and artwork
proportions and ensures screen captures are not fitted twice.
No game data is required by the test.

## Copied pause captures

The UI screen-content classification now follows whole base-level 2D texture
copies from render targets, including chains of copies. Previously only textures
rendered into directly qualified: a copied capture could be fitted as artwork
and displaced relative to other menu layers. This provenance is separate from
GPU ownership; it never changes uploads, writeback, texture pixels, camera
projection or resolution. Partial copies and overwrites cannot inherit an old
whole-screen tag. Static artwork copies do not acquire it. Tags are removed on
texture retirement, explicit CPU writes, buffer uploads and clears.

This is a candidate correction for pause-menu composition. Synthetic tests cover
copy chains, static content, partial/offset copies and overwritten/reused records;
only an on-device visual check can confirm that it resolves the reported menu.
