# Music packs

Terra Nova: Freefall can play OGG or MP3 files in place of the game's FM or General MIDI music. It is off unless you choose a pack.

## Using a pack

1. Next to `tnfree.exe`, make a folder called `Music` (with `--user`, it goes in that folder instead).
2. Put each pack in its own folder inside it, for example `Music\Remastered\`, or put the pack's zip file there as it is, for example `Music\Remastered.zip` (no need to unzip it).
3. In a mission press **Alt+O** and change **Music pack** to the pack's name (the folder's, or the zip's without `.zip`). Or write `music_pack=Remastered` in `tnfree.ini`. **Off** plays the game's own music.

A zipped pack has its music files at the zip's top or in one folder in it (a zipped folder: `Remastered/track01.ogg`); files deeper down are not used. Zip it with no compression (7-Zip's "Store", `zip -0`): OGG and MP3 files hardly get smaller, and each piece then starts without unpacking (deflated files work too; unpacking a 6 MB file takes about 40 ms when its piece starts). When a folder and a zip have the same name, the folder is used.

The new choice applies at once: the music playing switches to the pack's file, at the point the piece has reached. Choosing **Off** switches back to the game's own music the same way.

Music must be on: the options screen's Music line (Sound & movies) set to OPL3 FM, OPL2 FM or General MIDI, not Off. A pack replaces the music; it does not add music where the game has none.

## How it works

The game chooses its music as it always did. Each piece still runs as the original music in the background, with no sound, so the game changes pieces at exactly the same moments: when you are spotted, when a fight starts, and when it calms down. When the pack has a file for that piece, the file plays instead:

- it starts from the beginning when the piece starts;
- it follows the piece's volume, including the game's fades and the music volume setting;
- it pauses and resumes with the game;
- it stops when the piece ends. Most pieces play once and then the game moves on, so a file longer than the original piece is cut off, and a shorter one leaves silence until the piece ends. Make each file about as long as the original (the table below gives each length).
- it loops only where the original piece loops (tracks 02 and 18).

A piece with no file in the pack plays the original music, so a pack can replace just a few tracks.

## Making a pack

- Name each file after its track, `track01` to `track46`, or after the composer's MIDI soundtrack file (the **Soundtrack name** column, as in the Database Viewer's music page), ending in `.ogg` or `.mp3`. Upper and lower case, spaces, `-` and `_` do not matter, so `nh7vict.mp3` and `NH5 battle peril trans.ogg` work too.
- The NH set's soundtrack files are numbered differently from the other sets (`NH1explor`, `Nh2peril`); the regular spelling (`NH2explor`, `NH3peril`) works as well.
- `NH1night` is the Explore piece of the night missions (it has no track number). Without it, the pack's `track08` / `NH1explor` file plays there.
- Mono or stereo, any sample rate (44100 Hz is best).

The five mission sets go with the planet's terrain type: every mission on the same kind of terrain uses the same set. Mission 1 uses set 5.

Inside a set, the game moves between the pieces like this: the mission starts with **Drop**, which leads into **Explore**. When an enemy notices the squad the music moves to **Peril**, and fighting close by moves it to **Battle**. After 10 seconds without new fighting it goes back to Peril through **Battle to Peril**, and after 45 seconds without a new alert back to Explore through **Peril to Explore**. **Victory** and **Failure** are short pieces for the end of a mission.

| File | Soundtrack name | Where it plays | Original length | Original |
|---|---|---|---|---|
| `track01` | - | Sound start-up (nearly silent) | under 1 s | once |
| `track02` | `Main` | Main menu | 115 s | loops |
| `track03` | - | Shell, short piece | 5 s | once |
| `track04` | `Base` | Base | 131 s | once |
| `track05` | `Load1intro` | Loading: opening (leads into 06) | 12 s | once |
| `track06` | `Load2loop` | Loading | 99 s | once |
| `track07` | `Nh1drop` | Set 1 (NH): Drop: mission start (leads into Explore) | 9 s | once |
| `track08` | `NH1explor` | Set 1 (NH): Explore: no enemy has noticed the squad | 134 s | once |
| `track09` | `Nh2peril` | Set 1 (NH): Peril: an enemy has noticed the squad | 67 s | once |
| `track10` | `NH4battle` | Set 1 (NH): Battle: fighting close by | 101 s | once |
| `track11` | `NH6peril-explor trans` | Set 1 (NH): Peril to Explore: short transition | 7 s | once |
| `track12` | `NH5battle-peril trans` | Set 1 (NH): Battle to Peril: short transition | 7 s | once |
| `track13` | `Nh7vict` | Set 1 (NH): Victory: short piece | 3 s | once |
| `track14` | `Nh8fail` | Set 1 (NH): Failure: short piece | 5 s | once |
| `track15` | `Tha1drop` | Set 2 (Tha): Drop: mission start (leads into Explore) | 10 s | once |
| `track16` | `Tha2explor` | Set 2 (Tha): Explore: no enemy has noticed the squad | 128 s | once |
| `track17` | `Tha3peril` | Set 2 (Tha): Peril: an enemy has noticed the squad | 79 s | once |
| `track18` | `Tha4battle` | Set 2 (Tha): Battle: fighting close by | 103 s | loops |
| `track19` | `Tha6peril-explor trans` | Set 2 (Tha): Peril to Explore: short transition | 10 s (General MIDI: 11 s) | once |
| `track20` | `Tha5battle-peril trans` | Set 2 (Tha): Battle to Peril: short transition | 9 s | once |
| `track21` | `Tha7vict` | Set 2 (Tha): Victory: short piece | 3 s | once |
| `track22` | `Tha8fail` | Set 2 (Tha): Failure: short piece | 5 s (General MIDI: 4 s) | once |
| `track23` | `Sar1drop` | Set 3 (Sar): Drop: mission start (leads into Explore) | 9 s | once |
| `track24` | `Sar2explor` | Set 3 (Sar): Explore: no enemy has noticed the squad | 91 s | once |
| `track25` | `Sar3peril` | Set 3 (Sar): Peril: an enemy has noticed the squad | 54 s | once |
| `track26` | `Sar4battle` | Set 3 (Sar): Battle: fighting close by | 91 s | once |
| `track27` | `Sar6peril-explor trans` | Set 3 (Sar): Peril to Explore: short transition | 9 s | once |
| `track28` | `Sar5battle-peril trans` | Set 3 (Sar): Battle to Peril: short transition | 7 s (General MIDI: 9 s) | once |
| `track29` | `Sar7vict` | Set 3 (Sar): Victory: short piece | 3 s | once |
| `track30` | `Sar8fail` | Set 3 (Sar): Failure: short piece | 4 s | once |
| `track31` | `Kar1drop` | Set 4 (Kar): Drop: mission start (leads into Explore) | 8 s | once |
| `track32` | `Kar2explor` | Set 4 (Kar): Explore: no enemy has noticed the squad | 270 s | once |
| `track33` | `Kar3peril` | Set 4 (Kar): Peril: an enemy has noticed the squad | 47 s | once |
| `track34` | `Kar4battle` | Set 4 (Kar): Battle: fighting close by | 87 s | once |
| `track35` | `Kar6peril-explor trans` | Set 4 (Kar): Peril to Explore: short transition | 9 s (General MIDI: 10 s) | once |
| `track36` | `Kar5battle-peril trans` | Set 4 (Kar): Battle to Peril: short transition | 8 s | once |
| `track37` | `Kar7vict` | Set 4 (Kar): Victory: short piece | 3 s | once |
| `track38` | `Kar8fail` | Set 4 (Kar): Failure: short piece | 4 s | once |
| `track39` | `Sim1drop` | Set 5 (Sim, training and simulator): Drop: mission start (leads into Explore) | 5 s | once |
| `track40` | `Sim2explor` | Set 5 (Sim, training and simulator): Explore: no enemy has noticed the squad | 89 s | once |
| `track41` | `Sim3peril` | Set 5 (Sim, training and simulator): Peril: an enemy has noticed the squad | 53 s | once |
| `track42` | `Sim4battle` | Set 5 (Sim, training and simulator): Battle: fighting close by | 87 s | once |
| `track43` | `Sim6peril-explor trans` | Set 5 (Sim, training and simulator): Peril to Explore: short transition | 7 s | once |
| `track44` | `Sim5battle-peril trans` | Set 5 (Sim, training and simulator): Battle to Peril: short transition | 7 s | once |
| `track45` | `Sim7vict` | Set 5 (Sim, training and simulator): Victory: short piece | 3 s | once |
| `track46` | `Sim8fail` | Set 5 (Sim, training and simulator): Failure: short piece | 5 s | once |

The lengths are the FM versions (the General MIDI versions are the same except where noted). The soundtrack names were matched note for note against the composer's General MIDI soundtrack files. Track 01 and 03 have no soundtrack file, so they only take the `track` name.
