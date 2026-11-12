MUSIC PACKS
===========

Put a music pack here as a folder of its own, for example:
  Music\Remastered\track01.ogg
  Music\Remastered\track02.ogg
  ...
or as one zip file, as it is (no need to unzip it):
  Music\Remastered.zip
with the music files at the zip's top or in one folder in it. Zip them with
no compression ("Store" in 7-Zip): OGG and MP3 files hardly get smaller, and
the game starts each piece a little sooner. A folder and a zip of the same
name: the folder is used.
Files can be OGG or MP3, named track01 to track46, or after the composer's
soundtrack file names (Main, Sim2explor, ...).
Then choose the pack on the options screen (O, Music & Movies,
Music pack) or with music_pack=Remastered in tnfree.ini. Missing tracks fall
back to the game's own music. Music packs are never part of a mod (the zips
in the Mods folder) or a preset: they stay here.

The full list of tracks, their names and lengths is in MUSIC-PACKS.md in the
Freefall source (docs/modding/MUSIC-PACKS.md).

Freefall-MP3.zip here is the Freefall Music pack that comes with Freefall:
the game's music recorded as MP3 by Rich "Weeds" Nagel
(http://www.richnagel.net), shared with his permission. The Freefall preset
plays it; choose it or Off on the Music pack line. Keep it as it is.

SOUNDFONTS
==========
GeneralUser-GS.sf2 is the SoundFont Freefall comes with (GeneralUser GS
2.0.3 by S. Christian Collins, https://www.schristiancollins.com; its
licence is GeneralUser-GS-LICENSE.txt). It plays the SoundFont music
unless you choose another. Put another General MIDI SoundFont (.sf2) or
DLS file (.dls) here for the SoundFont music (options screen, Music &
Movies, Music: SoundFont (AWE32), then the SoundFont for AWE32 line; or
soundfont=<file name> in tnfree.ini). A dump of the AWE32's ROM (awe32.raw, 1 MB) goes here too:
then every sound of the game's own AWE32 bank plays. Get it from the 86Box
emulator's ROM collection:
  https://github.com/86Box/roms/tree/master/sound/creative
(click awe32.raw, then "Download raw file"). Freefall finds it by itself.
See DOWNLOADS and SOUNDFONT MUSIC in README.txt.
