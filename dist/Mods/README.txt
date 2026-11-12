MODS
====

Mods change only pictures: the cockpit and goggles art, the suit's arms,
the explosions and effects, and the trees, bushes and rocks. The game
plays the same.

Each mod is one zip file, straight in this folder, as it is (no
unzipping). Freefall comes with one, the Freefall Mod Pack:

  Mods\Freefall.zip

Put other mods next to it and choose them on the options screen (O, Mods,
Mod set). The mod's name is the zip's name without .zip. The Freefall
preset chooses the mod named Freefall (Mods\Freefall.zip), or no mod when
there is none.

Music packs are not mods: they stay in the Music folder (see
Music\README.txt) and are chosen on the Music & Movies page.


WHAT A MOD ZIP HOLDS
--------------------
The game knows a zip as a mod when it holds at least one of these four
folders (one to four of them):

  Cockpits\    16:9 cockpit and goggles art         (COCKPITS below)
  Arms\        the suit's arms in the goggles view  (ARMS below)
  Effects\     explosions and effects               (EFFECTS below)
  Objects\     trees, bushes and rocks              (OBJECTS below)
  mod.txt      (optional) the mod's name, version and author (MOD.TXT)

For example:

  Freefall.zip
    mod.txt
    Cockpits\standard-400-cockpit.png
    Cockpits\standard-400-cockpit-2x.png
    Arms\standard\arms.txt
    Arms\standard\pose_3_2.png
    Effects\1171\01.png
    Effects\1171\02.png
    Objects\1160\01.png

The folders may be at the top of the zip, or inside one folder (when you
zip a folder: Freefall\Cockpits\...). Zips made by Windows (Send to,
Compressed (zipped) folder), 7-Zip or any other zip tool work, except
password-protected ones.

On the Mods page, Mod set chooses all of a mod's parts at once (a part the
mod does not have is Off). Each part also shows under its own line
(Cockpit pack, Arms pack, Effects pack, Objects pack) by the mod's name, so
you can take the cockpits from one mod and the trees from another; Mod set
then shows Custom. In tnfree.ini the mod's name is each part's:
screen_cockpit=Freefall, screen_arms=..., screen_effects=...,
screen_objpack=...

Making a mod: while you work on it, it can stay unzipped as a folder of
the same name (Mods\MyMod\Cockpits\...), read the same way. A folder comes
before a zip of the same name. Zip it when it is done.


COCKPITS
--------
16:9 art for the cockpit and the goggles view, used with Widescreen on
(Display page), in place of the evenly stretched screen:

  Cockpits\<suit>-<rows>-<part>.png       426 x rows
  Cockpits\<suit>-<rows>-<part>-2x.png    852 x 2 x rows

  suit   scout, standard, heavy, pirate
  rows   200 (the 320 X 200 screen) or 400 (the 320 X 400 screen)
  part   cockpit or goggles

For example:
  Cockpits\standard-400-cockpit.png       426 x 400
  Cockpits\standard-400-cockpit-2x.png    852 x 800
  Cockpits\standard-200-goggles.png       426 x 200

Each picture is an 8-bit indexed PNG in the game's mission palette: the
game's 320 columns in the middle (x 53 to 372), 53 more on each side.
Colour 0 is see-through: the 3D view shows there. The -2x picture is used
when the window shows the screen at twice its rows or more. A picture that
is missing keeps that screen stretched as before. A window narrower than
16:9 shows the whole picture with bars above and below.

The helmet (Dynamic helmet on the Mods page): beside a picture, its parts,
their hidden art, its border and one file per suit:

  Cockpits\<picture>-parts.png    indexed PNG, the same size: each
                                  pixel's part, 1 .. 8 back to front
                                  (0 the window, where the 3D shows)
  Cockpits\<picture>-hidden-N.png indexed PNG, the same size and colours:
                                  part N's art under the parts beside it,
                                  shown when they move apart (0 none)
  Cockpits\<picture>-border.png   indexed PNG, the picture's size and as
                                  many pixels more on each side: the art
                                  beyond the screen's edges
  Cockpits\<picture>-layers.png   (optional) indexed PNG, the same size:
                                  each pixel's part, 0 chin, 1 bezel,
                                  2 brow, 3 wall, 4 plates, 5 upper (a
                                  hit's static only on parts 0 to 2)
  Cockpits\<suit>-helmet.txt      how each part moves and its distance
                                  from the eye (m): lines
                                  "cockpit part 1 chin move 0.140"

Dynamic helmet needs the parts; without them the picture stays still.

How to make the art, and the starter kit (tools/cockpit-kit.py):
docs/modding/COCKPIT-PACKS.md in the Freefall source.


ARMS
----
Your suit's arm holding the selected weapon in the goggles view (G), aimed
at the pointer. Never in the cockpit, never in the drone's view. A folder
per suit:

  Arms\<suit>\arms.txt
  Arms\<suit>\<pictures>.png

  suit   scout, standard, heavy, pirate (a suit without a folder uses
         standard's)

The pictures are the RIGHT arm; the left arm is the same pictures mirrored.
Each one is a PNG with alpha (see-through below half), all the same size,
covering the whole 3D view. Any colours: each is shown as the game's nearest.

arms.txt lists them:

  size 608 342
  pose pose_3_2.png 0.0000 0.0000 524.8 242.8
  recoil pose_3_2_recoil.png 0.0000 0.0000 543.4 237.1

  size     the pictures' width and height
  view X Y W H
           (optional) when the pictures are bigger than the view: where
           the view is in them. Drawing the arm past the view's edges means
           its picture's edge never shows when the arm moves (a shot kicks
           it back). The muzzle's places are then in the whole picture.
  pose     a picture for the pointer at U V of the view (-1 to 1 from the
           middle to the edges, V up), then the weapon's muzzle at X Y of
           the picture. The poses make a grid of U and V values.
  recoil   (optional) the middle pose kicked back by a shot: only how far
           its muzzle moved is used
  sway K MAXU MAXV
           (optional) with mouselook the arm lags behind a turn: K of the
           view's half width for each radian a second of turning, at most
           MAXU sideways and MAXV up and down (0.06 0.3 0.2 without it)
  follow OMEGA ZETA
           (optional) how the arm follows the pointer: OMEGA its speed
           (lower lags longer), ZETA its damping (below 1 it swings past a
           little and back) (22 0.8 without it)
  walk BOB SWAY LEAN SPEED RATE
           (optional) walking on the ground: the arm dips BOB of the
           view's height RATE times a second and swings SWAY of its width
           to a side and back between two dips, and lags LEAN of its width
           behind a strafe (half of it up or down behind a walk forward or
           back), all at SPEED metres a second or faster; slower, less and
           a little slower (0.012 0.008 0.015 10 2.6 without it; the suits
           walk about 10 to 12, forward or back). walk 0 turns it off.

The grid need not be even: a mod can stop at a free-aim zone on one side
and go on to the view's edge on the other.

Between the poses the arm moves smoothly to the pointer; past the grid's
edge it stays at the edge, except below it: there the bottom row's pose
moves down with the pointer (until the muzzle reaches the bottom edge).
Up to 512 poses (64 values of U and of V). A shot kicks the arm back
(quick weapons such as the Laser in small soft pulses). Your shots leave
from the muzzle of the arm drawn (instead of the game's points beside your
body), to what is under the pointer when no target is locked. In the infrared view the arm shows in the infrared's colours.
Walking moves the arm by its pictures alone: no extra pictures are needed,
only the margins of "view" (a few percent of the view on each side).
The left or right arm shows by where the selected weapon sits; a weapon in
neither arm shows no arm. Switching to the other arm's weapon lowers one
arm and raises the other.


EFFECTS
-------
One folder for each animation the mod replaces, named after the game's
number for it, with the pictures as PNG files in order:

  Effects\1171\01.png
  Effects\1171\02.png
  ...

1171 is the fireball of every death (soldiers, vehicles, buildings, trees).
Animations the mod does not have stay the game's own.

Some deaths can have pictures of their own in place of the fireball:
  1183  a tree or bush (as tall as the plant)
  1192  a rock or grassy mound
  1184  a soldier (1.25 times as big)
  1167  a vehicle or a gun turret (1.5 times as big)
A death without its folder shows the fireball (1171) as before.

Tree explosion (Detail page: Realistic) draws a tree's or bush's
death on the plant itself and uses these folders:
  Effects\PlantBlast  the plant's blast (without it 1171, else the game's
                      fireball)
  Effects\Leftovers   what is left standing: Leftovers\1x\1160\01.png ...
                      for the game's own pictures, Leftovers\3x\1160\01.png
                      ... for a mod's objects at 3 times (NN.png = frame
                      NN-1, indexed PNGs in the game's palette as in
                      Objects; an empty picture: nothing left; a plant
                      without one: its trunk falls to ash)
  Effects\Pieces      wood.png, bark.png, leaf.png: flying chips, 24x24
                      cells, one piece per row, 8 turns of 45 degrees
                      across; grey = place on the plant's own colours,
                      alpha = shape
  Effects\1175        a dark smoke plume behind the plant
  Effects\1170        brown dust rolling along the ground
Pictures the mod does not have are left out.

The blast marks on scorched ground (res 1164, frames 1 to 4) can be the
mod's too, whatever Tree explosion is set to:
  Effects\BlastMarks\1x\02.png ... 05.png   64x64, the squares further away
  Effects\BlastMarks\4x\02.png ... 05.png   256x256, the near squares
Indexed PNGs in the game's palette; colour 0 keeps the ground. Use only
colours that are the same on every planet (24-28, 36-44, 84-90, 100-105,
113-120).

Pixels with alpha 240 to 255 are drawn solid, 16 to 239 see-through (blended
with what is behind, for soft smoke without dithering), under 16 not at all.
Pictures made at 2 or 3 times the game's size look best at 3D resolution 4x
or 6x.

The other animation numbers, the picture sizes and how to make them:
docs/modding/EFFECT-PACKS.md in the Freefall source.


OBJECTS
-------
One folder for each game resource the mod changes (1160 to 1163: the trees,
bushes and rocks), with one picture for each object, named by the object's
frame number plus one:

  Objects\1160\01.png     the pine (frame 0)
  Objects\1160\03.png     the round bush (frame 2)
  Objects\1160\07.png     the pile of rocks (frame 6)

Objects the mod does not have stay the game's own.

The pictures are 8-bit indexed PNG files whose colour numbers are the game's
palette entries (0 is see-through), so the game colours them for each planet
as it does its own trees. Pictures made at exactly 2 or 3 times the game's
size look best at 3D resolution 4x or 6x.

The frame numbers, their sizes and how to make them: docs/modding/OBJECT-PACKS.md
in the Freefall source.


MOD.TXT
-------
(optional) a line each for the mod's name, version and author:

  name Freefall HD
  version 1.0
  author Cyberion

shows as "Freefall HD 1.0 by Cyberion" under the Mod set line. Without it
the mod shows by its zip's name.
