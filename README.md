# BlakeStone32X

Blake Stone: Aliens of Gold for the Sega 32X. Work in progress.

## B1 - speed test

Level 1 (Game1_Floor1) with walls, textured floors and ceilings, and Blake's distance shading.
Both SH2s render: the master does the left half, the slave does the right half.
Doors are open and there are no sprites yet. This build only measures speed.

| Button | Action |
|---|---|
| D-pad | walk / turn |
| A | floors and ceilings on / off |
| B | view size 320x160, 288x144, 256x128, 224x112, 192x96 |
| C | detail hi / lo (lo = double-wide pixels) |
| START | 2 CPUs / 1 CPU |

Each boot step is shown as a line on screen with its values. If a step fails, it shows `STOP` and the reason.

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
