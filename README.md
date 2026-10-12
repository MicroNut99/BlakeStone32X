# BlakeStone32X

Blake Stone: Aliens of Gold for the Sega 32X. Work in progress.

## B12

The full game: all 6 missions, sound effects (digitized and AdLib), music, menus.

| Button | Playing | Menus |
|---|---|---|
| D-pad | walk / turn | move |
| A | fire | choose |
| B | use (doors, switches) | back |
| C | strafe | choose |
| X | run | |
| Y | next weapon | |
| Z / MODE | auto-map | back |
| START | menu | choose |

- **Save RAM (32 KB):** options, controls, high scores and one saved mission.
  A saved mission keeps the current floor and the floors you left last (up to about 20 KB);
  other floors start over when you go back to them.
- **LEVEL SELECT** (main menu): mission, floor 1-10, lives, then difficulty.
- **GAME OPTIONS > STARTING LIVES:** 1 to 10 lives for every new mission.
- High score names: up/down picks a letter, right adds one, left or B takes one off, A is done.

## Build

No game data is in this repo. You need your own Blake Stone AOG files (`VSWAP.BS6`, `MAPHEAD.BS6`, `MAPTEMP.BS6`)
and the BStone v0.9.0 source (`vgapal.cpp`, `colormap.cpp`).

    make GENDEV=/path/to/toolchain GAMEDIR=/path/to/BLAKE BSTONE=/path/to/bstone

Toolchain: sh-elf-gcc 12.1.0. Output: `build/BlakeStone32X_B1.32x`.

`host/hosttest.c` renders one frame on a PC with the same renderer, which is useful for checking pictures without an emulator.

## Credits

- Blake Stone (c) Apogee / JAM Productions, source released under GPL-2.0; BStone port by Boris Bendovsky
- 32X support code (`mars/`): Chilly Willy and Victor Luchits (D32XR), MIT
- 8x8 font: Steph's Mini DevKit (from D32XR)
