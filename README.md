# Another World for Game & Watch Retro-Go SD

Eric Chahi's **Another World** (1991, *Out of This World* in America), PC DOS
version, running on the Nintendo Game & Watch with the
[Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd) firmware,
as a **GWHB homebrew**: a single `.bin` dropped in `/homebrews/`, no firmware
rebuild needed.

The game engine is **raw**, Gregory Montoir's reimplementation of the game's
virtual machine, in its cleaned up NEO-RAW version by Fabien Sanglard
([Another-World-Bytecode-Interpreter](https://github.com/fabiensanglard/Another-World-Bytecode-Interpreter)).
The project is built from
[retro-go-sd-templates](https://github.com/sylverb/retro-go-sd-templates): it
talks to the firmware only through the firmware ABI.

**The game data is not included.** You need your own copy of the PC DOS
version: `MEMLIST.BIN` and `BANK01` to `BANK0D`.

## Installation

Requires Retro-Go SD **v2.0.0-rc1 or later** (firmware ABI v2).

1. Build the game data pack (Python 3 only):
   ```
   python3 tools/aw_pack.py AnotherWorld.aw /path/to/ANOTHER
   ```
2. Copy both files to the SD card, side by side, with the same name:
   ```
   /homebrews/AnotherWorld.bin
   /homebrews/AnotherWorld.aw
   ```
3. Start it from the **Homebrew** tab.

The pack (1.7 MB) is cached in the external flash the first time; the game
uses its data in place from there. `aw_pack.py` unpacks the resources of the
banks once on the PC, so the console never decompresses anything.

The copy protection screen (symbols of the code wheel) is skipped: the game
starts with the intro. Press A to skip the intro.

## Controls

| Game & Watch | Another World |
|---|---|
| D-pad | Move, crouch (down), jump (up) |
| A | Action: run (with a direction), kick, shoot, hold to charge the gun |
| B | Jump (same as up) |
| GAME (START on Zelda units) | Pause |
| PAUSE/SET | Retro-Go menu |

The Retro-Go menu adds savestates (they replace the game's access codes,
which need a keyboard) and, in *Options*, "Swap A/B" (A = jump, B = action;
saved for the next starts). Display "Scaling": *Off* shows the original 320x200
picture with black borders, the other modes stretch it to 320x240, the 4:3
aspect ratio of the original game ("Filtering" smooths the stretched lines).

## Building

Needs `arm-none-eabi-gcc` (hard-float, v10+), GNU Make, Python 3 + Pillow:

```
make            # → AnotherWorld.bin
make docker     # same, inside the Retro-Go SD builder image
```

### Testing without hardware

`harness/` builds the same engine and port code for Linux, headless, with a
virtual clock, scripted buttons, PPM screenshots and WAV output:

```
make harness
harness/build/aw_host AnotherWorld.aw -t 30000 -e 1000 -o /tmp/out -w /tmp/out/a.wav
harness/build/aw_host AnotherWorld.aw -s "5000:A:1,5200:A:0,9000:RIGHT:1,12000:RIGHT:0" -o /tmp/out
```

`-q file -Q ms` writes a savestate at that time (like the pause menu would),
`-j file -J ms` loads one, `-L file` restores one at start, `-F ms` makes
every frame that much slower (a slow device), `make -C harness SAN=1` builds
with AddressSanitizer. The pack is mapped read-only, like the flash.

## Layout

| Path | Role |
|------|------|
| `src/raw/` | NEO-RAW (fabiensanglard/Another-World-Bytecode-Interpreter @ 170b7d4). Every Game & Watch change is under `#ifdef GNW`. |
| `src/port/aw_core.cpp` | The engine's `System` (display, sound, timers, input), main loop, savestates. |
| `src/port/aw_alloc.c` | Heap on the RAM left after the binary (~710 KB, peak use ~270 KB). |
| `src/port/aw_pack.c` | `.aw` pack reader. |
| `src/port/aw_main.c` | Retro-Go side: entry point, display, audio, buttons, pause menu. |
| `tools/aw_pack.py` | Builds the `.aw` pack (unpacks the banks). |
| `harness/` | Headless Linux test harness. |
| `sdk/` | Retro-Go SD core / homebrew SDK (from retro-go-sd-templates). |

See `CLAUDE.md` for the internals (memory, savestates, sound).

## License

GPLv2 (see `LICENSE`), like raw. The SDK under `sdk/` comes from
retro-go-sd-templates (MIT, `sdk/LICENSE.MIT`; vendored firmware headers
keep their own licenses). Another World is a game by Eric Chahi.
