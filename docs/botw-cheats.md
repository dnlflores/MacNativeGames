# Breath of the Wild: cheats and the in-game cheat menu

## Using it

Hold **L and R** and press **A** while playing. A menu opens on the left of the picture; while it is open the
controller drives the menu and the game sees no input (the game itself keeps running).

| Button | Main page | An item list |
|---|---|---|
| D-pad / left stick up and down | move | move (hold to scroll) |
| D-pad left and right | | page up and down |
| A | switch a cheat, open an item list | drop one in front of Link |
| Y | | drop five side by side |
| ZL / ZR | | previous / next item list |
| B | close | back to the main page |
| + or L + R + A | close | close |

On a Mac keyboard L, R and A are Q, E and J.

- **Invincibility**: damage never takes hearts away, long falls included, and the hearts stay full.
- **Unlimited stamina**: the stamina wheel stays full.
- **Moon jump**: hold L, then hold X. Link jumps and rises as long as both are held; let go and he falls (press X to
  open the paraglider). While moon jump is on, L (runes) acts when you let go of it, so that L + X never uses a rune.
- **Max rupees** (999,999) and **999 arrows** (the equipped arrows): a save made while they are on keeps them.
- **Drop in front of Link**: one-handed and two-handed weapons, spears, bows, shields, arrow bundles, food and ingredients,
  monster parts and gems, critters and rupees. They land two metres in front of Link and are picked up as usual
  (a few, such as the Bow of Light, go straight into the inventory). The list comes from the game's own files, mods
  included, in the game's language.

The same cheats can be switched on before starting (iOS start screen → **Cheats**). Every cheat is off again when the app
restarts; nothing is saved. The first L or R press reaches the game 90 ms late (the menu needs to tell an L + R press
from a single one); a press shorter than that still arrives as a tap.

Environment variables (a launch's own variables win over the start screen): `TOTK_CHEAT_HEALTH` (invincibility),
`TOTK_CHEAT_STAMINA`, `TOTK_CHEAT_MOONJUMP`, `TOTK_CHEAT_RUPEES`, `TOTK_CHEAT_ARROWS` = 1; `TOTK_CHEAT_LOG=1` logs the
values every 300 frames. The log shows `[cheats] menu opened`, `[cheats] item list: N items ...` and one
`[cheats] dropped <actor> xN` line per drop.

## How it works

Everything goes through the game's own data and functions in 1.6.0 (build `8e9978d50bdd20b4`); the addresses are in
`runtime/botw_cheats.c` and `runtime/game_patches_botw.c`.

- **Controller**: `runtime/input.c` hands every controller read to `botw_cheat_input`, which recognises the chord, runs the
  menu and returns the state the game sees. Drops and moon jump happen there, on the game thread that reads the
  controller.
- **Dropping**: `ksys::act::ActorCreator::requestCreateActor` with a position parameter, as the game creates a falling
  star (`ksys::world::ShootingStarMgr::spawnStar`). Link's position and facing come from `uking::act::PlayerInfo`'s
  player actor.
- **Item names**: `runtime/botw_item_list.c` reads `Actor/ActorInfo.product.sbyml` and the text pack the game opened
  (`Pack/Bootup_<language>.pack`) through the RomFS with its mod overlays; `tests/test_botw_item_list.py` checks the
  readers on files it builds itself.
- **Invincibility**: refilling the hearts every frame does not survive one big hit (a long fall ends in Game Over on
  landing). The player's damage step commits the new life with one store, found with a hardware store watchpoint on
  the life during a fatal fall. Two stubs after the code keep the old life while a flag on the loader's data page is set
  (`+0xa0`), so the iOS executable stays as exported and the cheat can be switched while playing.
- **Moon jump**: the player's gravity factor (1.0 as shipped) is -1 while L and X are held, the community "Moonjump"
  code's field. It acts only in the air, which is why X jumps first.
- **Drawing**: the menu is drawn with Core Text into a premultiplied BGRA texture when it changes and blended over the
  picture at present (`gfx/nvn/nvn_mtl.m`, both present paths), so it shows in window captures (`wshot`) as well.
