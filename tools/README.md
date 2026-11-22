# tools/ : build, packaging and art scripts

| Script | What it does |
|---|---|
| `gen_port.py` | Generates the port's data layout and function map from `symbols/` (run by CMake) |
| `embed.py`, `embed_previews.py` | Build fonts, the logo, the fan missions' patches and the options screen's preview pictures into `tnfree` (run by CMake) |
| `mission-patch.py` | Writes what a fan mission's author changed in the game's resource files, and nothing else, as the patch file an extra mission is built from (`port/res/stone-henge.ffmp`) |
| `package.sh` | Puts the release folder together |
| `fetch-generaluser.sh`, `fetch-freefall-files.sh` | Download the files the release ships but git does not keep (the SoundFont, the Freefall Music pack, the Freefall Mod Pack), each checked against its SHA-256 |
| `md2txt.py` | Turns `docs/BUGS.md` into the plain text file of the release |
| `build-shaders.sh` | Compiles the screen filter shaders |
| `menu-wide.py` | Makes the wide main menu's own art parts |
| `visor-damage.py` | Makes the eighteen Visor damage pictures of `port/res/visor` from their nine source pictures |
| `cockpit-kit.py` | For mod makers: makes a 16:9 cockpit starter kit from your own game files |
| `mkimage.py`, `le_image.py` | Read `__FF.EXE` and write the program images the tests use (`le_image.py` is from TN_SFC_improved, MIT) |
| `tn.py`, `funcs.py` | Helpers for studying the original code: disassembly with labels, function starts in a range |
