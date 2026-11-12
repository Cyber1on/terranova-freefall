# History

How Terra Nova: Freefall was made, in the order it happened. The full list of changes by version is in [CHANGES.md](CHANGES.md); every bug fixed on the way is in [BUGS.md](BUGS.md).

## 1. Decompilation

The game's program, `__FF.EXE` English v1.09, was turned into C function by function: the engine libraries first (fixed-point math, resources, 2D, 3D, the EDMS physics), then the game itself in ten parts (G1 to G10), from the foundation up to the menus.

- Every function was run side by side with the original in an emulator and compared: 58 test suites, 0 differences.
- Missions 1, 2, 10 and 25 ran frame by frame identical with the C swapped in for the original code.
- Third-party code (the Watcom C runtime, Miles, Smacker, UniVBE, the head-mounted display drivers, Spaceball) was left as it was; the port replaces it.

What was decompiled and how each part was checked: [decompilation/progress.md](decompilation/progress.md).

## 2. Standalone build

The C compiles and links into one 32-bit program with CMake. The game's tables are copied at start-up from the player's own `__FF.EXE`, so the repository holds no game data. From here on, a verification build has to stay a byte-identical copy of the original after every change.

## 3. SDL3 layer

Video, keyboard, mouse, joystick and timer on SDL3; a new Miles-compatible sound mixer; music as OPL3 FM or General MIDI; a new Smacker movie player; DOS paths mapped onto the game folder and the CD image. The game played from start to finish on Windows for the first time.

## 4. Frame-by-frame comparison with the DOS original

A lockstep mode runs the port and the original (in an emulator) with the same input and compares the screen, the palette and all game data at every frame. Missions 1, 2, 10 and 25 matched every recorded frame, from the main menu into play. What it found and fixed: [decompilation/frame-comparison.md](decompilation/frame-comparison.md).

## 5. Improvements, each one optional

With the copy proven exact, everything new went behind an option, so the Classic preset is still the DOS game:

- **Controls:** mouse look, a second controls style with its own key bindings, auto-aim, a new help screen.
- **Picture:** screen filters, sharp 3D up to 6x, widescreen, 60 frames a second, true colour sky and fog, water reflection and ripples, longer view distances, sharp maps, a science-fiction infrared view.
- **Mods:** one zip per mod, with 16:9 cockpits and goggles, a helmet that moves, the suit's arms, effects, trees and rocks.
- **Sound and movies:** music packs (OGG and MP3), SoundFont music, the game's own AWE32 bank, movies smoothed to 30 pictures a second.
- **Menus:** an options screen in the game's own style with three presets (Classic, Vanilla+, Freefall), a Mission Selector, a built-in benchmark.
- **Bugs:** 48 bugs of the original game found and fixed on the way.

## 6. Version 1.0

The first public release: the port, the Freefall Mod Pack, the Freefall Music pack and a SoundFont in one download.
