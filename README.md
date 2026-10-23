# PS2 Web Recomp

PlayStation 2 games running in the browser, or natively, through static recompilation. The game's Emotion Engine code is translated ahead of time into C++ and compiled to WebAssembly (or a native binary); the rest of the console is emulated by a runtime that draws with WebGPU.

> Fork of [snuri00/ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) with macOS support for the native runner. The upstream README covers the full technical write-up and performance history.

<p align="center">
  <img src="docs/media/demo.webp" alt="Gran Turismo 4 running in a browser" width="720">
  <br>
  <a href="docs/media/demo.mp4">Watch the full recording (1:24)</a>
</p>

## Status

| Game | State |
|---|---|
| Gran Turismo 4 (PAL) | Boot, menus and races at full speed (50 fps), sound and memory card saves |
| God of War II | Menus, cutscenes and combat at 60 fps at native resolution |

Other games may work through the same scripts, but will most likely stop at something not implemented yet: the runner logs `[sys] unhandled syscall` or `[iop] missing <library> #<n>`, or the image shows a GS feature the renderer does not handle.

No game data is included. Bring a disc image of a game you own.

## How it works

- **Recompiler (`eerecomp`):** translates the R5900 code to C++ in 16 KiB units, keeping its quirks (128-bit registers, delay slots, the PS2's non-IEEE floats).
- **Kernel:** high-level emulation of EE threads, semaphores, interrupts and the SIF channel. The IOP runs the game's own IRX modules on an R3000 interpreter.
- **Vector units:** microprograms are compiled ahead of time to SIMD C++, or to WebAssembly on the fly in the browser.
- **Graphics:** GS video memory lives on the GPU and is drawn by WebGPU compute shaders, at 1x to 3x resolution. A software rasterizer remains as reference.
- **Sound and video:** SPU2 reimplemented (48 ADPCM voices), IPU MPEG-2 decoder for movies.
- **Threads:** emulation, vector unit 1 and the GS each run on their own thread.

## Quick start

Requirements: git, CMake, Ninja, a C++20 compiler with `<format>` (clang preferred), Python 3, SDL2 for the native window, 7-Zip for `.7z` images, and about 300 MB per game.

```bash
git clone https://github.com/RodriguesWall/ps2-web-recomp.git
cd ps2-web-recomp
scripts/port.sh mygame "My Game.iso"                                # browser + native
python3 scripts/serve.py build/web-mygame games/mygame/disc.iso     # http://localhost:8620/
```

`port.sh` reads the boot program from the disc's `SYSTEM.CNF`, translates it, runs the game headless a few times to find code static analysis missed, compiles the recorded vector unit programs and builds `build/web-mygame` and `build/native-mygame`. The first run installs the Emscripten SDK into `tools/emsdk` and downloads wgpu-native.

| Option | Effect |
|---|---|
| `--native-only` | Skip the browser build (and the Emscripten SDK) |
| `--press 900:cross` | Custom inputs for the exploring runs (default: start, then cross every 5 s) |
| `--frames N` / `--rounds N` | Length and number of exploring runs |
| `--pnach FILE` / `--pnach-section S` | Apply PCSX2 patch files, e.g. a widescreen section |

Frames from the last exploring run land in `games/mygame/shots`, the quickest way to see how far a new game gets. `scripts/build_web.sh mygame` rebuilds only the page.

## macOS

Tested on Apple silicon (M4, macOS 26): God of War II runs natively at 60 fps. The native runner draws through wgpu-native on Metal, into a `CAMetalLayer` created by SDL.

```bash
brew install bash cmake ninja sdl2 sevenzip
scripts/port.sh mygame "My Game.iso" --native-only
```

- **Homebrew:** use the native one in `/opt/homebrew`. If an Intel Homebrew in `/usr/local` comes first in `PATH`, add `eval "$(/opt/homebrew/bin/brew shellenv)"` to `~/.zprofile`.
- **bash:** the scripts re-run themselves under Homebrew's bash when started from the system's bash 3.2.
- **Compiler:** Apple clang from current Command Line Tools works. `cxx.sh` falls back to Homebrew's LLVM (`brew install llvm`) if needed.
- **Linker error `tapi error: malformed file ... unknown architecture arm64e.x1`:** the default SDK is newer than the linker. Point at the SDK of your macOS version:
  ```bash
  export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk
  ```
- **Browser:** Chrome on macOS has WebGPU on by default.

## Playing natively

```bash
build/native-mygame/ps2run games/mygame/disc/SLUS_123.45 --disc games/mygame/disc.iso --window --memcard mygame.ps2
```

`port.sh` prints the exact line at the end. Look for `[window] presenting through WebGPU` in the output.

| Option | Effect |
|---|---|
| `--fullscreen` / F11 | Fullscreen |
| `--widescreen` | 16:9 setting (most games also need a widescreen patch) |
| `--patches games/mygame/patches.txt` | Apply the patches `port.sh` collected |
| `PS2_SCALE=2` or `3` | Higher internal resolution |
| `PS2_ONE_THREAD=1` | Run everything on one thread |

## Browsers and controls

WebGPU is required for 3D. Chrome and Edge have it on Windows and macOS; on Linux enable `chrome://flags/#enable-unsafe-webgpu` and `#enable-vulkan`. The page must be served cross-origin isolated, which `serve.py` does (it binds to `127.0.0.1` by default).

| PS2 | Keyboard |
|---|---|
| Cross / Circle / Square / Triangle | Z / X / A / S |
| L1 / R1 / L2 / R2 | Q / W / 1 / 2 |
| Start / Select | Enter / Backspace |
| D-pad | Arrow keys |
| Left stick | I J K L (arrow keys in the native runner) |

Gamepads work in both (analog triggers act as pressure-sensitive L2/R2, with vibration). Phones get on-screen controls; F toggles fullscreen. In the native window F11 toggles fullscreen and F5 saves a state (to the `--save-state` file, or `quick.state`), which `--load-state` resumes.

## Saves

A memory card in slot 1 is kept per address in the browser's storage. **Export card** / **Import card** move it as a PCSX2-compatible `.ps2` file; `?nocard` starts with an empty slot. The native runner uses the same file format with `--memcard`.

## Legal

This repository contains no game code or data. The generated C++ and WebAssembly are translations of the game's executable: keep them on your machine and do not publish them. Use disc images of games you own. Gran Turismo, God of War and PlayStation are trademarks of Sony Interactive Entertainment; this project is not affiliated with Sony or any publisher.

## Credits and license

MIT License, see [LICENSE](LICENSE). The MPEG decoder's VLC tables are adapted from [Play!](https://github.com/jpd002/Play-) (BSD 2-clause, notice in `runtime/src/ipu/NOTICE`). PCSX2, Play! and the ps2sdk headers were references for the emulated hardware. wgpu-native and Dawn provide WebGPU, and Emscripten builds the WebAssembly. Original project by [snuri00](https://github.com/snuri00/ps2-web-recomp).
