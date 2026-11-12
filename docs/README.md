# Documentation

| Read this | For |
|---|---|
| [HISTORY.md](HISTORY.md) | How the project was made, step by step |
| [CHANGES.md](CHANGES.md) | Everything Freefall changes or adds, by version |
| [BUGS.md](BUGS.md) | Every bug fixed: in the original game (G) and in the port (P) |
| [PORTING.md](PORTING.md) | How the port works inside: loader, video, sound, mods, testing |
| [modding/](modding/) | Guides for mod makers: [cockpit](modding/COCKPIT-PACKS.md), [effect](modding/EFFECT-PACKS.md), [object](modding/OBJECT-PACKS.md) and [music](modding/MUSIC-PACKS.md) packs |
| [decompilation/](decompilation/) | The decompilation's own records (below) |

## The decompilation folder

These are the working records of the decompilation, kept because they hold what was learned about the game's code. They were written during the work, for the people and AI workers doing it, so they are terse and use the work's own terms.

| File | What it holds |
|---|---|
| [progress.md](decompilation/progress.md) | Every part that was decompiled: its files, function counts, test suite and findings; how to rebuild and verify |
| `2d-map.md`, `3d-map.md`, `edms-map.md`, `support-map.md`, `game-map.md` | Code maps: what lies at which address range of `__FF.EXE`, by library and subsystem |
| `*-functions.csv`, `2d-table-targets.csv` | Function lists by address, with the part each belongs to |
| [batch-notes.md](decompilation/batch-notes.md) | Structures, calling conventions and quirks of the original that later parts had to know |
| [test-harness.md](decompilation/test-harness.md) | How the whole-game test in `verify/` works |
| [interp-fault.md](decompilation/interp-fault.md) | A test-harness trap with the 3D interpreter's handler tables |
| [frame-comparison.md](decompilation/frame-comparison.md) | The port against the DOS original, frame by frame: results and fixes |
| [reports/](decompilation/reports/) | One report for each of the ten game parts, G1 to G10 |

**The part names.** The work was split into parts, and headers, symbol files and tests still carry their names: `fix`, `res`, `2d`, `3d`, `edms` and `sup` (support) are the engine libraries; `t1` and `t2` are Terra Nova's own support code; `g1` to `g10` are the game, from its foundation (g1) through objects and weapons (g2), units (g3), AI (g4), terrain and the 3D view (g5), object drawing and effects (g6), the HUD (g7, g8), missions and campaign (g9) to the menus (g10). A letter after the number (`g4_b.h`, `test_g4_b.py`, `symbols-g4-b.csv`) is one share of that part.
