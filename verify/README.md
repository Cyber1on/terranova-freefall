# verify/ : checking the C against the original

The tests run the original `__FF.EXE` in an emulator (Unicorn) next to the decompiled C and compare what both do. They need your own copy of the game (English v1.09) and never write to it. Setting up and running: the main [README.md](../README.md), "Checking the C against the original".

| File | What it does |
|---|---|
| `build.sh` | Builds the C into `build/mine.bin`, a copy of the original program's layout. Its hash must not change when only the port changes |
| `emu.py` | The emulator: maps the original and the C side by side, calls either, compares |
| `frame.py`, `test_frame.py` | The whole-game test: runs missions frame by frame with the C swapped in for the original code |
| `test_<part>.py` | One suite for each part of the decompilation: see "The part names" in [docs/README.md](../docs/README.md) |
| `test_port_orig.py` | Tests the C that the port has in place of routines kept as machine code |
| `port_compare.py` | Runs the port (lockstep mode) and the original with the same input and compares every frame |
| `state_compare.py` | Compares two lockstep runs of the port, for options that must not change the game |
| `gen_thunks.py`, `mine.ld`, `hdrcheck.c`, `interp_hooks.py`, `tnpaths.py` | Build glue and helpers |
| `inputs/` | Recorded input for the mission runs |

More on the harness: [docs/decompilation/test-harness.md](../docs/decompilation/test-harness.md).
