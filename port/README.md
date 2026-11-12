# port/ : the SDL3 port

Everything the decompiled game in `src/` needs to run on a modern system, and everything Freefall adds. How it works inside: [docs/PORTING.md](../docs/PORTING.md).

| Folder | What is in it |
|---|---|
| `core/` | Start-up (`main.c`), the loader that copies the game's code and data from the player's own `__FF.EXE` (`image.c`), the search for an installed Steam or GOG game (`findgame.c`), files and DOS paths, the log, the SDL platform layer (window, input, timer), DOS and Watcom compatibility, the divide-error handler, worker threads, joystick |
| `original/` | C for the routines the decompilation kept as machine code (`orig_*.c`) |
| `video/` | The screen and its filters, sharp 3D, widescreen, true colour, fog, infrared, the maps, the Smacker movie player, smooth videos, shaders |
| `audio/` | The Miles-compatible mixer, XMIDI, FM (OPL3), General MIDI, SoundFont music, the AWE32 bank converter, music packs |
| `ui/` | The Freefall options screen, the help screen, the Mission Selector, the wide main menu, key bindings |
| `mods/` | Mod zips; cockpit, arms, effects and object packs; Dynamic helmet; tree explosions and blast marks |
| `test/` | Lockstep mode (frame-by-frame comparison with the original), the benchmark, the smooth-video test |
| `include/` | The port's headers |
| `res/` | Logo, wide menu art, Windows icon and manifest, version template |
| `symbols-port.csv` | Original routines that only the standalone build needs to name |

Every `.c` file below `port/` is built (`CMakeLists.txt` finds them by itself).
