# Breath of the Wild: iPad display shape

Normal launches use the game's original **16:9** canvas. The complete image fits
inside the display with its proportions preserved; taller iPads have bars above
and below it. Resolution presets, texture enhancements, mods, and the Magnesis
readback correction remain available. The old saved `matchDisplay` preference is
ignored, so upgrading an experimental fullscreen build restores consistent rendering.

## Experimental native aspect (disabled in normal launches)

The native-aspect experiment is **not visually correct**. On-device tests of builds
6–9 showed mismatched pause-menu backgrounds, blur captures, clipping, character
preview composition, and quick-select HUD duplication. Geometry tests and compiler
checks did not establish correct final composition. Do not re-enable this in the
launcher until all these screens have been checked on the device at both 16:9 and
native aspect, with enhancements enabled. Fixed menu bars are preferable to an
incorrect picture.

For investigation only, an explicit launch environment with
`TOTK_IOS_MATCH_DISPLAY=1` enables the experimental canvas. It derives the aspect
from the game view at startup, preserves the selected canvas width, and adds rows
on taller displays. This costs GPU time and memory. It does not support live
render-target reallocation when resizing a window.

The frontend configures `TOTK_BOTW_RES`, `TOTK_OPERATION_MODE=docked`, and
`TOTK_ASPECT` before the loader sizes memory. `TOTK_IOS_MATCH_DISPLAY=0` keeps
the original canvas (the normal launcher default). Explicit `TOTK_ASPECT` takes precedence.

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

A separate treatment is allowed only for actual full-view quad geometry copying a
screen capture or drawing a solid fade. The renderer fetches vertices/indices and
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
