# Effects packs

Terra Nova: Freefall can draw the game's effect animations (explosions, impacts, smoke) with pictures from a mod: the `Effects` folder of a mod zip. It is off unless you choose a pack. Only the picture changes: the game plays the same.

## Using a pack

1. Next to `tnfree.exe`, there is a folder called `Mods` (make it if it is missing; with `--user`, it goes in that folder instead).
2. Put the mod's zip into it as it is, for example `Mods\Fireball.zip`, holding `Effects\1171\01.png` and so on (the zip's layout: `dist/Mods/README.txt`, `Mods\README.txt` in the release). While you make a pack it can stay a folder: `Mods\Fireball\Effects\1171\01.png`.
3. Press **Alt+O** (or click **Freefall Options** on the main menu), open **Mods** and change **Effects pack** (or **Mod set**, for all of the mod's parts) to the mod's name. Or write `screen_effects=Fireball` in `tnfree.ini`. **Off** shows the game's own effects.

The pack is used from the next picture on, also in the middle of a mission. It is drawn in the sharp 3D view, so choosing a pack turns that view on even at 3D resolution Original (at the game's size there). Packs made at 2 or 3 times the game's size look best at 3D resolution 4x or 6x.

## How it works

- The game runs each animation as it always did: it decides when an effect starts and when it ends, and nothing in the game depends on the pictures. A pack only changes what is drawn.
- The pack's pictures are spread over the time the game shows the animation. The game's fireball has 15 frames of 71 ms (1.065 s); a pack with 30 pictures shows one between each two of the game's, a pack with 15 shows one for each, and any other number is spread over the same time.
- A picture takes the same room in the world as the game's frame: the game sizes an effect by its width in the world and keeps the picture's proportions. So make the pictures exactly 2 or 3 times the size of the game's frames (the fireball's are 68 x 88, so 136 x 176 or 204 x 264). Other proportions make the effect taller or flatter.
- The game's anchor point (the point placed on the ground) is used, scaled to the picture: for the fireball, pixel (32, 87) of 68 x 88, so (64, 174) at 2x and (96, 261) at 3x. Draw the bottom of the blast there.
- Each colour becomes the nearest colour of the game's palette in use (the game has 256 colours, and a planet changes some of them). The game's fire colours (palette entries 61 to 129) are the same on every planet, so art that keeps to them looks the same everywhere.
- See-through pixels: a pixel's alpha decides how it is drawn. Alpha 240 to 255: solid. Alpha 16 to 239: blended with what is behind it (alpha 128 is half and half), so soft smoke edges and thin smoke need no dithering; the mix becomes the nearest colour of the palette (by how the eye sees colours, CIE Lab), so very thin layers can still step a little. Alpha under 16: not drawn. In the infrared view (I key) a see-through pixel moves the colour behind it part of the way along the infrared's heat ramp.
- Effects drawn far away or small still use the pack's picture, scaled down.

## Deaths by what died

The game shows the same fireball (`1171`) for every death. A pack can give some kinds of death pictures of their own, in these folders:

| Folder | What died | Drawn |
|---|---|---|
| `1183` | a tree or bush | as tall as the plant was drawn (with **Tree size** too), the picture's proportions kept |
| `1192` | a rock or grassy mound | as wide as the fireball |
| `1184` | a soldier | 1.25 times as wide as the fireball |
| `1167` | a vehicle or a gun turret | 1.5 times as wide as the fireball |

- They take the fireball's place and time: the game still plays its fireball (the same start, end and sound), and the folder's pictures are spread over its 1.065 s like the fireball's own. Any number of pictures, any size and proportions.
- They stand like the fireball: the point 47% across the picture (32 of 68) on its bottom row is where the object stood.
- A death whose folder the pack doesn't have shows the pack's `1171` pictures (or the game's fireball), as before. Buildings and aircraft always do.
- Rocks (and the grassy mounds, which crumble rather than burn) are frames 5 to 8 and 10 of `1160` (`06.png` to `09.png` and `11.png` in an objects pack), frames 5 to 7 of `1161` (`06.png` to `08.png`) and all of `1162` and `1163` (see [OBJECT-PACKS.md](OBJECT-PACKS.md)); every other frame of `1160` and `1161` is a tree or bush.
- The game doesn't play these four animations itself (version 1.09), so the folders change nothing else.

## Tree explosion

With **Tree explosion** on (Alt+O, Detail page: **Realistic**; `screen_burn=realistic` in `tnfree.ini`; called Burning plants: Explosive in earlier test builds), a tree's or bush's death is drawn on the plant's own picture instead of a picture in its place: the plant lights up from the blast's centre outwards (0.18 s), then the crown bursts into spinning shards and needles of its own pixels, pieces of wood, bark and leaf, burnt leaves thrown high that flutter down and sparks streaking up, with the blast, dust rolling out and a smoke plume. What is left of the plant stays standing until the mission ends, its broken top glowing and shedding ash for a while; it is drawn behind the blast, so the blast hides it at first. The mark on the ground is the game's own blast mark (the pack's `BlastMarks` when it has them, below). The shards, needles, burnt leaves and sparks are worked out from the plant's pixels and need no pack. It uses these folders of the effects pack in use:

| Folder | Used for | Drawn |
|---|---|---|
| `PlantBlast` | the plant's blast: a fireball, wood splinters and ground dust, then smoke | one picture each 42 ms from 0.18 s after the death to 1.31 s, from the fourth picture (`04.png`: the light-up makes the start) to the last; `1.6 x sqrt(width x height)` of the plant wide, in the proportions 136 x 176, so a low plant gets a small pop. Pixel (64, 170) of 136 x 176 (scaled to the picture) goes a little below the plant's foot; half the plants show it mirrored (pixel (72, 170) then) |
| `Leftovers` | what is left of the plant | see below |
| `Pieces` | flying wood, bark and leaf | three sheets, `wood.png`, `bark.png` and `leaf.png`: cells of 24 x 24, one piece per row, 8 turns of it across (45 degrees each). The grey is a place on the plant's own colours (0 dark, 255 light: its trunk's for wood and bark, its crown's for leaf), the alpha is the shape. Drawn at the plant's picture height / 480 (one pixel each for a 3x objects pack's pine) |
| `1175` | smoke: a dark plume behind the plant | stretched to a tall plume about half as wide as the plant is tall and as tall as the plant, rising and drifting behind it, fading in and out (no drift when the blast is mirrored) |
| `1171` | the blast when the pack has no `PlantBlast` | 1.8 times the plant's width, centred at 60 % of its height, for about 0.7 s, a little darker; with neither, the game's fireball shows as the blast |
| `1170` | brown dust | two puffs stretched to a low, wide shape (from about 0.3 x 0.25 of the plant's height to 0.55 x 0.38), rolling out left and right along the ground, growing and fading, at 40 % |

What is left (`Leftovers`):

- `Leftovers\1x\1160\01.png` ... and `Leftovers\3x\1160\01.png` ...: one folder for each plant resource (`1160`, `1161`), and in it one picture for each frame: `NN.png` is frame NN - 1, as in an objects pack ([OBJECT-PACKS.md](OBJECT-PACKS.md)).
- Indexed PNGs whose colour numbers are the game's palette entries (0 is see-through), as an objects pack's pictures, so they take the planet's colours as the plant does: use the plant's own entries where the plant has a pixel.
- `1x` is for the game's own pictures and `3x` for a pack's at 3 times their size. The one the size of the plant's picture as drawn is used; otherwise the `3x` one (or the `1x` one when there is no `3x`), scaled to the plant's width and standing on its foot.
- A picture with no pixels: nothing is left. A plant without a picture keeps the first version's rule: a jagged trunk that chars and falls apart in scattered pixels and ash onto a small pile at its foot that fades.
- Up to 240 stay at a time (the oldest go first). They are the picture only: they don't block shots or movement, and they are not kept in a saved game.
- 13 kinds of tree and bush break before they die: in the game the first hit leaves a broken tree (a pine's sticks) that still stands in the way, and a second hit destroys it. With Tree explosion such a tree bursts whole at the first hit and is gone from the game at once, so nothing unseen stays where its leftover stands. With Tree explosion Original the game's rule holds.

- The blast keeps the picture's proportions; the smoke and the dust are stretched to the sizes above, so draw them about in those shapes. Any number of pictures: `1171` is spread over its time, the smoke and the dust loop. The blast, the smoke and the dust are best soft and see-through (alpha), since they are drawn over each other and over the land.
- The fewer pieces, the smaller the plant: 12 to 60 shards, 40 to 150 needles, 8 to 50 pieces from the sheets and 10 to 35 burnt leaves, by the crown's size.
- A folder the pack doesn't have leaves that part out. With Effects pack Off, it is the plant's pieces with the game's fireball and the first version's trunk.
- Rocks and grassy mounds don't burn: their deaths stay as above (`1192`, or the fireball).

## Blast marks

Where an explosion scorches the ground, the game paints one of four blast marks (res 1164, frames 1 to 4, by the square's texture) over the square's texture. An effects pack can bring its own, in the sharp 3D view, whatever Tree explosion is set to:

- `BlastMarks\1x\02.png` ... `05.png`: frames 1 to 4 at the game's size, 64 x 64, for the squares further away and the distant land.
- `BlastMarks\4x\02.png` ... `05.png`: the same at 256 x 256, for the near squares (one more than 96 pixels across on the screen). The ground under the mark is the texture's own pixels made 4 x 4.
- Indexed PNGs, as an objects pack's pictures: colour 0 keeps the ground, and the other colour numbers are the game's palette entries. Use only entries that are the same on every planet (24 to 28, 36 to 44, 84 to 90, 100 to 105, 113 to 120), so a mark looks the same on every planet's ground.
- A picture of another size is left out (`tnfree.log` says so), and so is a missing one: that frame is the game's. With only `1x`, the near squares use it too.
- A mark that reaches to the square's edges and fades out there with dithering joins up with its neighbours' into one patch.

## Making a pack

- A pack folder holds one folder per animation. Its name starts with the number of the game resource it replaces: `1171` or `1171-fireball`. Everything after the number is ignored. Tree explosion's own folders have names instead: `PlantBlast`, `Pieces` and `Leftovers`, and the blast marks `BlastMarks` (any case).
- In it, the pictures as PNG files, in the order of their names: `01.png`, `02.png`, ... (use leading zeros: `10.png` comes after `09.png`, but before `2.png`). Up to 240 pictures, each up to 1024 x 1024.
- Any PNG works (RGBA is best). An animation folder without PNG files is ignored, and so is a picture that can't be read (`tnfree.log` names it).

The effect animations are resources 1165 to 1193. The ones known so far:

| Number | Animation | Used for |
|---|---|---|
| `1171` | Fireball | every death: soldiers, vehicles, aircraft, gun turrets, trees and small buildings (some can have their own: see above) |
| `1193` | Big explosion | Mass driver (M-B) hits; aircraft and some buildings destroyed |
| `1191` | Explosion | missile hits, grenades, some buildings destroyed |
| `1190` | Explosion | heavy missiles, demolition charges |
| `1180` | Energy hit | laser and particle beam hits on units |
| `1179` | Blue sparks | hits in water |
| `1182` | Brown sparks | explosive hits in water |
| `1185`, `1186`, `1187` | Smoke puff, smoke column, glow | see-through effects in the game; a pack's pictures are see-through by their alpha |

Not played by the game (version 1.09): 1167, 1170, 1175, 1183, 1184, 1192 (four of them are the deaths' folders above; Tree explosion uses 1170 and 1175 too).

## Limits

- Fire, sparks, smoke puffs and debris that the game makes with code (particles, flying pieces) are not pictures and can't be changed by a pack.
- One effect animation is used for several things. The deaths of trees, rocks, soldiers, vehicles and gun turrets can have their own pictures (above); the other shared animations (the big explosions) can't be told apart yet.
- Trees, bushes and rocks are not effects: they have packs of their own ([OBJECT-PACKS.md](OBJECT-PACKS.md)).
