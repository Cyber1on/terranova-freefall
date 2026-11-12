# Cockpit packs

With **Widescreen** on, the cockpit and the goggles' frame are stretched evenly to 16:9. A cockpit pack shows art painted for 16:9 in their place: the game's screen stays 1:1 in the middle, and the pack adds the sides and may open the window wider (the 3D view is drawn as wide as the pack's window shows). It is off unless you choose a pack. Only the picture changes: the game plays the same.

## Using a pack

1. Next to `tnfree.exe`, there is a folder called `Mods` (make it if it is missing; with `--user`, it goes in that folder instead).
2. Put the mod's zip into it as it is, for example `Mods\Standard-16x9.zip`, holding `Cockpits\standard-400-cockpit.png` and so on (the zip's layout: `dist/Mods/README.txt`, `Mods\README.txt` in the release). While you make a pack it can stay a folder: `Mods\Standard-16x9\Cockpits\...`.
3. Turn **Widescreen** on (Alt+O, Display page), then change **Cockpit pack** (or **Mod set**, for all of the mod's parts; Mods page) to the mod's name. Or write `screen_cockpit=Standard-16x9` in `tnfree.ini`. **Off** shows the stretched cockpit as before.

The pack is used from the next picture on, also in the middle of a mission. A screen the pack has no picture for (another suit, the other detail level, the goggles) stays stretched as before, and `tnfree.log` names each picture it loads or cannot use.

- A window narrower than 16:9 (16:10, 4:3) shows the whole picture with black bars above and below it; a wider one has black bars at the sides, as Widescreen.
- The mouse: on the game's screen everything is where it was. Beside it, over the pack's window, you aim, fire and target in the wide view as with Widescreen; over the pack's art beside the screen the pointer moves, but the buttons do nothing there.
- The drone's view (no frame) stays stretched.

## The pictures

```
Cockpits\<suit>-<rows>-<part>.png        426 x rows
Cockpits\<suit>-<rows>-<part>-2x.png     852 x (2 x rows), the same at twice the pixels
```

| Part of the name | Values |
|---|---|
| suit | `scout`, `standard`, `heavy`, `pirate` (the game's `RESHELMS/N/H/P.RES` for 320 X 200 and `RESHLMHS/N/H/P.RES` for 320 X 400) |
| rows | `200` (the game's 320 X 200 screen, low detail) or `400` (320 X 400, high detail) |
| part | `cockpit` (picture 750, with the frame 752 the game draws over its view) or `goggles` (the goggles' frame, picture 751, G key) |

For example the standard suit's cockpit at both detail levels, the 400-row one also at twice the pixels:

```
Cockpits\standard-400-cockpit.png       426 x 400
Cockpits\standard-400-cockpit-2x.png    852 x 800
Cockpits\standard-200-cockpit.png       426 x 200
```

- The game's 320 columns are in the middle (x 53 to 372 of the 426), 53 more on each side: 320 at 4:3 is 426 at 16:9.
- The `-2x` picture is used when the game's screen is shown at least twice its rows tall in the window (a 400-row screen in a window 800 pixels tall or more, a 200-row one at 400 or more), the 1x one below that. When only one of them is there, it is used at every size.
- Each picture is an **8-bit indexed PNG** (colour type 3, not interlaced) whose colour numbers are the game's mission palette entries. The PNG's own palette is only for looking at the picture in an image editor; Freefall uses the numbers. A picture that is not one, or not the right size, is skipped.
- Colour 0 is see-through: the 3D view shows there. Use it only for the window. (Beside the main view's rows it shows black beside the screen.)
- Colours 1 to 16 and 21 to 145 look the same on every planet. 17 to 20 and 146 to 255 change with the planet (or blink), so use them only for pixels copied from the game's own art.
- Buttons, the screens (MFDs), the health and energy bars, the compass and the red number displays must stay where they are: the game still draws into the middle 320 columns and the mouse still clicks there. The metal around them, and the window's pillars, may change.
- The infrared, the screen filters and the planet's colours change the pack's pixels as they change the game's art.

## How the picture is put together

The pack's pixel shows wherever the game's screen still shows the game's own art at that place, and everywhere beside the screen. Along the window's edges the game's 3D view covers a pixel of its own art; the pack's pixel shows there too. Wherever the game draws over its art (the screens, the gauges, the text, the help boxes, the target's brackets), the game's pixels show, doubled with a `-2x` picture. So a pack can repaint any of the art in the middle too, but what the game draws there stays the game's.

## The helmet: motion and light

**Dynamic helmet** (Mods page) needs a picture that comes in parts. Each part moves by its own share of the helmet's move against the head (the lag behind a turn, the walk's bob, a landing, a shot's kick, a hit's shake): the nearer the eye, the more. The 3D view, the crosshair and the targets' boxes stay still under them. Beside the picture, in the same folder:

```
Cockpits\<picture>-parts.png       the same size as the picture
Cockpits\<picture>-hidden-N.png    the same size, one per part (optional)
Cockpits\<picture>-border.png      the picture's size and as many pixels more on each side (optional)
Cockpits\<suit>-helmet.txt         one per suit
```

- `-parts.png` is an **8-bit indexed PNG** whose colour numbers are each pixel's part, `1` to `8`, drawn back to front; `0` is the window, where the 3D shows. A pixel of art where the map says window belongs to part 1. The game's own drawing over the art (the screens, the gauges, the numbers) moves with the part it is on.
- `-hidden-N.png` (in the picture's colours, `0` none) is part N's art under the parts beside it, in front and behind: what shows in the gap when two parts move apart. Give it a few pixels (12 at 2x) under every neighbour. Where two parts' hidden art meet in a gap, the one behind wins, so what shows is the back part going on under the front one: paint it plain and a little darker, as in the front part's shade. A copy of the part's edge or texture repeats as the gap opens and is easy to see.
- `-border.png` is the art beyond the picture's edges (its middle, the picture's place, is not used), for the parts that move off the screen's edge: up to 64 pixels at each side, at least as many as the parts move. Each border pixel goes with the part whose edge it touches; where the picture's edge beside it is the window, with the nearest art along the edge, so a frame's curve can go on past the edge (carry it on down and out, as it curves; do not cut it off at the edge's last row).
- `<suit>-helmet.txt` gives each part's move and distance from the eye, one line each, `#` for a comment:

```
# standard suit: part N, its name, how it moves, its distance from the eye (m)
cockpit part 1 chin move 0.140
cockpit part 6 upper move 0.104
goggles part 1 brow move 0.132
```

  The move is `move` (every way), `side` (sideways only), `side-down` (sideways, and down but never up) or `still`. A part not named stays still. The parts move up to 7 of the game's columns sideways and 6 rows up and down (3 at 200 rows), the part at 0.125 m by the helmet's move, nearer ones more (by `0.125 / distance`). Parts side by side at very different distances open wide gaps.

A hit breaks the screens into static for a moment: the picture's big dark blue areas, the bright green box of the weapon's screen (as the game's own art has it), its big see-through holes beside the window, and the goggles' screens. A picture can say which parts carry the screens, so that dark blue elsewhere stays as it is:

```
Cockpits\<picture>-layers.png     the same size as the picture (optional)
```

- `-layers.png` is an **8-bit indexed PNG** whose colour numbers are each pixel's part: `0` chin (the console and its screens), `1` bezel (the hump, the screens' bezels), `2` brow (the console at the top), `3` wall (the window's rim and walls), `4` plates (the lower side plates), `5` upper (the upper corners and hoses). A number above 5 counts as 0. The static goes only on parts 0 to 2.

## The starter kit

`tools/cockpit-kit.py` makes a starter kit from your own game files (Python with Pillow and numpy):

```sh
python tools/cockpit-kit.py "D:\Games\Terra Nova\TNOVA" cockpit-kit
```

It writes drafts of all 16 pictures to `Mods/MyCockpits/Cockpits/` (a mod folder: copy `MyCockpits` into the game's `Mods` folder to try it, zip it to share it; the window's pillars moved out, the outer metal repeated outwards), the original pictures, a guide layer marking the 4:3 middle, and the palette as a `.gpl` file (Aseprite, LibreSprite, GIMP) and a numbered swatch sheet with the colours to avoid crossed out. The kit and any pack made from it hold the game's art, so they are never added to this repository.

## How it works

`helm_draw_frame` (`src/game/hud/helmet.c`, port only) hands the art it draws to `port/mods/cockpitpack.c`: the cockpit's picture 750 when the game draws it, and each frame the piece over the main view (the cockpit's frame 752, or the goggles' frame 751; none in the drone's view), with the suit's letter from the res file's name. The display (`present` and `ck_compose` in `port/core/platform.c`) then takes the pack's picture for that screen, lays the game's screen 1:1 in the middle instead of stretching it, the main view's wide picture behind the pack's see-through pixels, and the pack's picture over it all at its own pixels. The main view is drawn wide enough to reach the picture's edges (`port_ck_on`, `sharp3d.c`). With Dynamic helmet, `port/mods/helmetfx.c` works out the helmet's move each frame and `platform.c` (`hlp_draw`) draws the picture's parts moved by it over the still 3D. Details: [PORTING.md](../PORTING.md), Cockpit pack and Helmet motion.
