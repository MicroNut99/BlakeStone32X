# BlakeStone32X

A port of Blake Stone: Aliens of Gold (1993, JAM Productions / Apogee) to the Sega 32X.

## What this is

- The original game code, taken from BStone (Boris Bendovsky's source port of the released Blake Stone source),
  changed to run on the 32X.
- Built on Vic's (Victor Luchits) and Chilly Willy's (Joseph Fenton) 32X engine code: the cart header and SH2
  startup, the 32X hardware library, and the Genesis-side 68000 program from D32XR (Doom 32X: Resurrection).
  Everything here runs on that code.
- All 6 missions of the registered (full) version, with the original maps, graphics, sound effects and music.
- Source code and build tools only.

## What this is not

- Not a new game, a remake, or a rewrite. The game logic is the original code.
- Not an official release. It is not connected with Apogee or JAM Productions.
- Not a ROM download. This repo has no game data. You need your own copy of Blake Stone: Aliens of Gold
  (the `.BS6` files) to build a cart.
- Not Planet Strike. Only Aliens of Gold is supported.
- Not finished or tested on every setup. It is tested in the Fusion 3.64 and PicoDrive emulators.

## Why and how

The 32X has 256 KB of RAM, two SH2 CPUs at 23 MHz, and the Genesis 68000 next to it. The PC game expects
several MB of RAM, a VGA card, an AdLib or Sound Blaster, and a hard disk. Each part was moved to what the 32X has:

| PC game | 32X port | Why |
|---|---|---|
| Game files on disk | A read-only file table in cart ROM | No disk; ROM is 4 MB and readable at any time |
| Graphics unpacked into RAM | Graphics unpacked at build time and read from ROM | Not enough RAM to unpack them |
| VGA drawing | One 320x208 page in RAM, copied to the 32X frame buffer | Same drawing code; the 32X shows 8-bit color |
| 3D view by one CPU | Walls, floors, ceilings and sprites split between both SH2s, inner loops in SH2 assembly | One SH2 is too slow for the original renderer |
| Floors you left kept in RAM | Packed (LZSS) and kept in the unused part of the two frame buffers | About 37 KB per floor does not fit in RAM |
| Digitized sound on Sound Blaster | 4-voice ADPCM mixer on the second SH2, out through the 32X PWM | The 32X has PWM sound only |
| AdLib-only sound effects | Recorded at build time through BStone's decoder and the DOSBox OPL emulator, played like the digitized ones | No AdLib chip |
| AdLib music | Converted at build time to YM2612 (Genesis FM) and played by the 68000 program | The Genesis FM chip is the closest match |
| Config, high scores and saves on disk | 32 KB battery save RAM in the cart | No disk |
| Keyboard | Genesis 6-button pad | No keyboard |

The build runs a PC copy of the same game code (`host/`) to make some of the data the cart needs,
so it matches what the game itself makes.

## Controls

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

Buttons can be changed in GAME OPTIONS > CONTROLS.

## Differences from the PC game

- **Saves:** one saved mission. It keeps the current floor and the floors you left last, up to about 20 KB.
  A floor that did not fit starts over when you go back to it. Saving and loading take a few seconds,
  because the save RAM is written one byte at a time through the 68000.
- **Save names:** made from where you are (for example `MISSION 1 FLOOR 5`), since there is no keyboard.
- **High score names:** up/down picks a letter, right adds one, left or B removes one, A is done.
- **LEVEL SELECT** (main menu): pick the mission, floor 1-10 and lives, then the difficulty.
- **GAME OPTIONS > STARTING LIVES:** 1 to 10 lives for every new mission.
- **Removed:** LOGOFF (there is nothing to quit to), SOUND VOLUME, and keyboard/mouse/joystick setup.
- Menu fades go to black.
- **Turning:** the d-pad turns at a quarter of the original running speed, also when running
  (always run is on by default; the original speed was too fast on the pad).

## Build

You need:

- An SH2 toolchain (`sh-elf-gcc` 12.1.0)
- A PC C++ compiler (`g++`) and Python 3
- Your own Blake Stone: Aliens of Gold registered files (`.BS6`)

```
make -f Makefile.game GENDEV=/path/to/toolchain GAMEDIR=/path/to/BLAKE
```

Output: `build-game/BlakeStone32X_B14.32x`. Never commit the game data or the `.32x` file.

## Credits

- **MicroNut99** - the 32X port
- **JAM Productions** - Blake Stone: Aliens of Gold
- **Apogee Software** - publisher; released the Blake Stone source code (GPL-2.0)
- **Boris Bendovsky** - BStone, the source port this game code comes from (GPL-2.0)
- **Victor Luchits (Vic)** - D32XR: the 32X hardware library, the Genesis-side 68000 program
  (music player, save RAM access) and `romheaderfix` (MIT)
- **Chilly Willy (Joseph Fenton)** - 32X support code: cart header, SH2 startup and hardware library (MIT)
- **Steph's Mini DevKit** - the 8x8 font (from D32XR)
- **The DOSBox Team** - the OPL emulator used at build time to record AdLib sound effects (GPL-2.0)
- **Steve Snake** - the Fusion emulator, used for testing
- **notaz** - PicoDrive, used for automated testing
- Code written with help from Claude (Anthropic)

Blake Stone is a trademark of its owners. This project is not affiliated with them.
