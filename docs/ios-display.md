# Breath of the Wild: iPad display shape

Graphics → **Match display** is enabled by default. The game fills the display;
the resolution preset supplies the canvas width and taller screens add rows.
Turning it off explicitly selects original 16:9. Preserve this default while
fixing menu composition: the owner rejected the temporary build 10 letterbox
rollback. Native resolution, enhancements and mod preparation remain unchanged.

Pause-menu backgrounds can be composed from several partial capture panes, so
full-screen geometry is not a prerequisite for a screen-copy treatment. These
layers must retain their screen mapping rather than receive the ordinary UI fit
a second time. Static artwork and character previews must remain separate.
On-device verification is still required for the remaining pause composition.

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

GPU-produced screen captures keep screen mapping for full or partial panes.
Solid fades use that treatment only for actual full-view quad geometry. The renderer fetches vertices/indices and
uniforms from their pending-update snapshots and projects them to check coverage.
Shared quad dimensions alone do not establish an element's bounds. This avoids
mistaking menu panels, HUD icons or logos for backgrounds and enlarging them.
Screen captures retain their screen mapping, so a captured HUD is not fitted twice.
Quick-select's GPU-produced 1280×720 and 640×360 capture/blur textures qualify even
when their aspect differs from the native iPad canvas. Static artwork and atlases
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
