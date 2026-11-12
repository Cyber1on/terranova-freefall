# Objects packs

Terra Nova: Freefall can draw the trees, bushes and rocks with pictures from a mod: the `Objects` folder of a mod zip. It is off unless you choose a pack. Only the picture changes: the game plays the same.

## Using a pack

1. Next to `tnfree.exe`, there is a folder called `Mods` (make it if it is missing; with `--user`, it goes in that folder instead).
2. Put the mod's zip into it as it is, for example `Mods\Plants.zip`, holding `Objects\1160\01.png` and so on (the zip's layout: `dist/Mods/README.txt`, `Mods\README.txt` in the release). While you make a pack it can stay a folder: `Mods\Plants\Objects\1160\01.png`.
3. Press **Alt+O** (or click **Freefall Options** on the main menu), open **Mods** and change **Objects pack** (or **Mod set**, for all of the mod's parts) to the mod's name. Or write `screen_objpack=Plants` in `tnfree.ini`. **Off** shows the game's own trees and rocks.

The pack is used from the next picture on, also in the middle of a mission. It is drawn in the sharp 3D view, so choosing a pack turns that view on even at 3D resolution Original (at the game's size there). Packs made at 2 or 3 times the game's size look best at 3D resolution 4x or 6x.

## How it works

- The trees, bushes and rocks are the frames of four game resources, 1160 to 1163. Each frame is a different object (a pine, a round bush, a pile of rocks ...), not a step of an animation, so a pack replaces them one by one: frames the pack has no picture for stay the game's own.
- The game colours these objects for each planet: most of their colours are palette entries that the planet's palette changes (the same pine is green on one planet and cyan on another). So a pack's pictures keep the game's palette numbers, not colours: each picture is an **8-bit indexed PNG** whose colour numbers are the game's palette entries. The PNG's own palette is only for looking at the picture in an image editor; the game uses the numbers.
- Colour number 0 is see-through. There are no half see-through pixels.
- A picture takes the same room in the world as the game's frame: the game sizes an object by its width in the world and keeps the picture's proportions. So make the pictures exactly 2 or 3 times the size of the game's frames (the table below). Other proportions make the object taller or flatter.
- The pictures are shaded, fogged and drawn in the infrared view (I key) exactly as the game's own: through the same colour tables.

## Making a pack

- A pack folder holds one folder per resource. Its name starts with the resource number: `1160` or `1160-trees`. Everything after the number is ignored.
- In it, one PNG per object, named by the frame's number plus one: `01.png` is frame 0, `03.png` frame 2, `14.png` frame 13. Use two digits.
- Each picture is an 8-bit indexed PNG (colour type 3, not interlaced), up to 1024 x 1024. (1, 2 and 4 bits a pixel are read too, but they reach only the palette's first 2, 4 or 16 entries.) A picture that is not one is skipped, and `tnfree.log` names it.
- Use the palette numbers the game's own frame uses (most are between 146 and 247, and 17 to 20), so the object changes with the planet as the original does. A simple way: draw the picture in planet 0's colours, then map every pixel to the nearest of the colours the game's frame uses, and save the numbers.

The frames and their sizes in the game (width x height, in pixels):

| Resource | Frames |
|---|---|
| `1160` | 0: pine 141 x 160, 1: 98 x 98, 2: round bush 98 x 84, 3: 50 x 45, 4: 50 x 50, 5: 72 x 70, 6: rock pile 86 x 66, 7: 83 x 91, 8: 45 x 39, 9: 44 x 34, 10: 36 x 26, 11: 68 x 36, 12: 97 x 88, 13: 97 x 31 |
| `1161` | 0: 94 x 92, 1: 93 x 92, 2: 96 x 81, 3: 65 x 64, 4: 93 x 53, 5: 198 x 58, 6: 207 x 69, 7: 179 x 65, 8: 94 x 50, 9: 93 x 36, 10: 76 x 47, 11: 83 x 51, 12: 72 x 32 |
| `1162` | 0: 87 x 93, 1: 125 x 124, 2: 97 x 52, 3: 103 x 55 |
| `1163` | 0: 77 x 54, 1: 101 x 82, 2: 87 x 71, 3: 69 x 53 |

For example, a pack with a new pine and round bush at twice the size:

```
Objects\1160\01.png    282 x 320
Objects\1160\03.png    196 x 168
```

## Tree size

**Tree size** on the Detail page (under Water effects; `screen_trees=realistic` in `tnfree.ini`) is **Original** (every object the game's size) or **Realistic**: every tree, bush and rock gets a size of its own, from 0.6 to 2 times the game's, taller and wider alike, standing on the same spot. The size comes from the object's place on the map, so the same tree is always the same size. It works with the game's own pictures and with any pack. Only the picture changes: where shots hit, what you bump into and what blocks the view are the game's, so a big tree can hide an enemy who can still shoot through its branches. It is drawn in the sharp 3D view, so it turns that view on even at 3D resolution Original.

## Limits

- The pictures have the game's 256 colours, and only the ones the planet gives them: a pack can't add colours of its own.
- The fireball when a tree is destroyed is an effect, not one of these pictures; an effects pack can give trees and bushes (`1183`) and rocks (`1192`) a death of their own (see [EFFECT-PACKS.md](EFFECT-PACKS.md)).
