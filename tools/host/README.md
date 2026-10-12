# tools/host - the platform layer on a PC

The game code (`src/engine/`, `src/game/`) asks the machine for only what
`src/platform/*.h` declares. This folder implements that on a PC (Linux;
any POSIX system), and is the starting point for a port (PortMaster, a
desktop build, another microcontroller).

| File | Implements |
|---|---|
| `platform_host.cpp` | `platform.h` (clock, log, heap figures) and `file.h` (the card's files: `/GOLDBOX/...` mapped onto a host folder, the board's 3 open-file limit) |
| `memlog_host.cpp` | `memlog.h` (the memory log: nothing on a PC) |
| `host.h` | `pc::card_root()` - the folder that stands for `/GOLDBOX`; `g_now` - the clock, owned by the program (a test driver moves it on) |

Sound (`platform/audio.h`: `audio_play` and friends) is left to the
program: the playsim (`tools/playsim/sim.cpp`) renders each sound to a WAV.

## Used by

- `tools/playsim/sim.cpp` - the Play Test on a PC, canvas snapshots
- `tools/playsim/sweep.cpp` - the behaviour and crash sweeps
- `tools/host_tests/test_platform.cpp` - tests this folder (CI runs it)

Build (from the repo root):

    g++ -std=c++17 -O1 -Itools/host -Isrc -o playsim tools/playsim/sim.cpp src/engine/*.cpp tools/host/*.cpp

## A port

1. The platform layer: copy this folder, keep `file.h`'s rules (a File is
   shared by its copies; `close()` closes it for all), give `now_ms()` the
   real clock (SDL_GetTicks, clock_gettime) and `audio_*` a sound output
   (SDL audio: pull 8-bit unsigned samples at `kAudioHz` from the filler).
2. A front end: show the 320x200 canvas (`pic::Canvas`, 16 EGA colours,
   `pic::kEga`), turn the controls into the game's input (`src/game/play.h`:
   `play::tap`, `play::act`, `play::nav`, `play::input_key`) and call
   `play::tick` regularly. `src/ui/` is the CYD's front end to compare with.
3. The engine's own screens (Settings, the journal viewer, the library)
   are still drawn for the CYD (`src/ui/viewer.cpp`, LovyanGFX): a port
   makes its own (docs/TODO.md).

Nothing from the games is in this repository: the player's own game files
go in the folder that stands for `/GOLDBOX` (docs/SPEC.md, section 1).
