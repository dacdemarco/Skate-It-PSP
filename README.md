# Skate It PSP

A native port of **Skate It** (Electronic Arts, 2008, Nintendo DS, USA release) to the Sony PSP.

It is **not an emulator**: the game engine is written from scratch in C for the PSP (skating physics, grinds, the whole trick book, the career with its
goals and opponents, front end, HUD, FMV player, sound), and the game's own data - levels, skaters, animations, menu art, sound - is converted from
**your own copy of the cartridge**. The result plays on a PSP from the Memory Stick (PPSSPP should work too but has not been tried).

> ## Read this first - what this repository is, and is not
>
> * **This repository contains no part of the game.** No ROM, no graphics, models, levels, animations, sound, music, video, text or program
>   code of Skate It are included, and none may ever be added (see `.gitignore`).
> * **You need your own copy of Skate It for the Nintendo DS (USA)**, as a cartridge you own, dumped by yourself to an `.nds` file. The tools in
>   `tools/` read that file and write the converted data to your own computer.
> * The converted data and the finished game folder (`EBOOT.PBP` + `data/`) **are derived from Electronic Arts' copyrighted work. Do not upload, share or
>   redistribute them.** Only the source code of this repository is free to use, and it is released into the public domain (see `LICENSE`: the Unlicense).
> * This is an unofficial, non-commercial fan project. It is not affiliated with, authorised or endorsed by Electronic Arts, Nintendo or Sony.
>   *Skate It*, *EA* and all related names, skaters, brands and artwork are the property of their respective owners.
> * Nothing here is legal advice. Check what is allowed where you live.

## What you need

| | |
|---|---|
| **The game** | Skate It (USA) for the Nintendo DS as an `.nds` dump of your own cartridge. The converters check the game code `CSVE`; other releases (Europe etc.) are not supported. |
| **A PSP** with custom firmware (PRO, ARK-4 ...) that runs homebrew, or **PPSSPP** on PC / Android (not tried by the author) | built for the extra RAM of the PSP-2000 / 3000 / Go (`MEMSIZE 1`); the PSP-1000 has less memory and has **not** been tried |
| **A Memory Stick** | about 60 MB free |
| **Linux, or WSL2 (Ubuntu) on Windows** | to run the build |
| **Python 3.9+** | `pip install -r requirements.txt` (numpy, pillow; capstone only for the optional reverse-engineering tools) |
| **gcc** | only for the host tests |
| **The PSP toolchain** | [pspdev](https://pspdev.github.io/) (`psp-cmake`, `psp-gcc`, `pack-pbp` ...) |

## Step by step

### 1. Dump your cartridge

Make an `.nds` file of your own Skate It cartridge with any DS dumping tool. Expect a file of about 32 MB, header game code `CSVE`
(name like `Skate It (USA) (En,Fr,De,Es,It,Nl).nds`).

### 2. Install the tools

```bash
# Python packages
pip install -r requirements.txt

# PSP toolchain: follow https://pspdev.github.io/installation.html, then
export PSPDEV=$HOME/pspdev            # wherever you installed it
export PATH=$PATH:$PSPDEV/bin
```

**Windows (the route this was developed and tested on):** install Python 3.9+ for Windows and the packages above, install WSL2 with Ubuntu and the PSP
toolchain inside it. Run `python tools/make_data.py ...` from a normal Windows shell and `bash tools/build.sh` inside WSL (your drives appear as `/mnt/c/...`;
from PowerShell: `wsl -d Ubuntu -- bash /mnt/c/path/to/skate-it-psp-release/tools/build.sh`).
Do the WSL call from PowerShell or `cmd`, not from Git Bash (it rewrites `/mnt/...` paths).

**Linux:** the same commands run natively. This route has not been tested by the author yet, please report problems.

### 3. Convert the game data

```bash
python tools/make_data.py "path/to/Skate It (USA) (En,Fr,De,Es,It,Nl).nds"
```

This runs, in order: ROM extraction (files, ARM code, overlays, unzipping the level archives) -> the career tables and the text database
decoded from the ARM code -> `career.dat` -> the 21 locations (`*.lvl`: geometry, textures, collision grid, grind rails) -> the 19 riders (`skater_*.skt`:
model, texture, the whole animation set) -> board, create-a-skater parts (with the deck / wheel art and the numbers of the gear the career unlocks), the original's
recorded skater runs (`ai.pak`), menu art and fonts, sound effects, streamed music, the two FMV files ->
checksums for the host tests. `--list` shows the steps, `--only NAME` runs one, `--skip NAME` leaves one out, `--jobs N` sets how many levels are converted
at once. The level conversion is the slow part (minutes per big level).

Everything lands in `extracted/`, `dist/SkateItPSP/data/`, `tools/campaign_*.json|tsv` and `tests/vx_ref_*.txt`. All of it is git-ignored.

### 4. Build

```bash
bash tools/build.sh
```

This compiles `EBOOT.PBP` and stages the game folder `dist/SkateItPSP/` (`EBOOT.PBP`, `README.txt`, `data/`).

Optional: put your own `ICON0.png` (144x80), `PIC1.png` (480x272), `PIC0.png`, `SND0.AT3` into `pbp/` before building for the XMB icon and background.
No artwork is included here, and the game's own art must not be committed.

### 5. Put it on the PSP

1. Copy the whole folder `dist/SkateItPSP` to the Memory Stick as `ms0:/PSP/GAME/SkateItPSP` (so that `EBOOT.PBP` and `data/` are in that folder).
2. Start **Skate It PSP** from *Game -> Memory Stick* in the XMB.
3. Profiles, settings and the trick table are written into its `data/` folder (`profile0.sav` ..., `settings.sav`, `tricks.cfg`, `vx_log.txt`).

**PPSSPP:** copy the same folder to `<PPSSPP memstick>/PSP/GAME/SkateItPSP`, or open `EBOOT.PBP` with *File -> Load* (untested by the author: the game was developed against a real PSP).

## Controls

The scheme is the original's FlickIt idea (tricks are gestures on the board) moved onto the analog nub. These are the defaults; **Options > Controls** has
four presets and a custom mapping, and **Options > Trick guide** lists every trick with the buttons of the mapping in use.

| Button | |
|---|---|
| Analog nub | steer; a quick flick while popping sets up a flip trick (up-left kickflip, up-right heelflip, left / right pop shuvits, down diagonals 360 pop shuvits) |
| CROSS | hold to crouch, release to ollie (basic pop) |
| TRIANGLE | the advanced pop (varial flips, hardflip, inward heel, 360 flip, laser flip); both pops together: body flips |
| SQUARE | push: tap = quick, hold = strong and slower; in the air, held with the nub: spins |
| CIRCLE | brake; with a flick left / right: revert; with the nub held hard over at speed: powerslide |
| L / R | in the air: back-hand / front-hand grab (the nub and a flick pick the variant); both: double grab |
| Nub forward while crouching | nollie stance |
| Nub down / up and held | manual / nose manual (the nub then balances it) |
| Rails, ledges, walls | no button: the board locks on by itself; the nub at that moment picks the grind, a flick while grinding switches it |
| D-pad | camera: left / right orbit, up / down distance |
| START | pause menu (resume, respawn, start / restart / leave a challenge, options, quit to the menu) |
| SELECT + L | HUD detail lines on / off |

Menus: D-pad / nub up and down, CROSS confirm, CIRCLE back.

## What is in the game

* **Start Career**: the original's career from the first tutorial to the Skater Of The Year ending - skater choice, the career map (area -> place -> events),
  the story (dialogue pages with the speaker on screen), sponsor offers, unlocks of skaters and events, profile saves (3 slots). 93 of the 98 career events run: Photo, Film, Own The Spot,
  Tutorial, Checkpoint, Deathrace, Jam, Best Trick and S.K.A.T.E. (the other five have no objectives in the data). The opponents are on the screen and skate
  the runs the original's skaters were recorded doing (the racers their real paths; Jam / Best Trick / S.K.A.T.E. opponents their recorded scores and tricks).
* **Freeskate**: any of the 19 riders on any of the 20 places, with a few other skaters roaming the place on the original's routes (not on Spillway, which has none).
  **Tutorial**: the 10 tutorials, with tips in your own button names.
* **My Skate**: profile slots, Log Book (statistics, personal bests, the trick book with how-to texts, unlockables), Game Settings (stance, units, sound
  volumes), Credits, **Customize** (create-a-skater: gender, build, skin, face, hair, beard, hat, top, logo, bottom, shoes, the board's deck and wheel art, with a
  live 3D viewer; shoes, hat designs, decks and wheels are the ones the career has unlocked).
* **The whole trick book** (all 101 tricks): flips, nollies, late flips, body flips, spins, reverts, powerslides, wallrides, 29 grabs, 16 grinds, manuals.
* **Options**: controls, trick guide, gravity / ollie power / push speed, HUD information lines.
* The two intro movies (decoded on the PSP by a port of the game's own video / audio codec), the front-end art and fonts, all 72 sound effects.

Not built: **Multiplayer** and **My Spot** (the editor) - both stay greyed in the main menu. The streamed **music** is converted but switched off in the
build (`MUSIC_ENABLED` in `src/main.c`: it keeps about 5 MB of tracks in RAM).

## How it works, in short

* **Converters** (`tools/`, Python) take the ROM apart (`extract_nds.py`, `blz.py`, `unpack_layers.py`), read the game's formats (EA's `OCT2` / `DSM1` /
  `DST1` / `MBF` / `ABF` / `VXDS`, Nintendo's SDAT / NCGR / NCLR / ...) and write compact, PSP-friendly files: `.lvl` (levels), `.skt` (skater + animations),
  `.brd`, `career.dat`, `ui.pak`, `custom.pak`, `ai.pak`, `sounds.snd`, `music*.mus`, `.vx`.
* **Engine** (`src/`, C for the PSP): `skate.c` physics (surface following on the level's collision grid, `coll.c`), `rails.c` grinds, `control.c` the gesture
  logic, `trick.c` / `score.c` the trick book and scoring, `rider.c` / `skel.c` skeleton animation, `goals.c` / `race.c` / `contest.c` the challenge rules,
  `ghost.c` / `stage.c` the recorded runs played back as riders (race opponents, contest opponents, ambient skaters), `progress.c` / `story.c` / `stats.c` the career and profile, `scene.c` the story scene player, `frontend.c` / `options.c` / `logbook.c` / `storyui.c` / `hud.c` the screens, `vx.c` the
  FMV player, `mixer.c` / `sfx.c` / `audio.c` sound, `main.c` the PSP side (GU rendering, input, game session loop).
  Everything except `main.c` and `audio.c` has no PSP dependency and is tested on the host.
* **Verification**: ground truth is the ROM's own data. The VX decoder was checked bit-exactly against the game's ARM9 code run in a small interpreter
  (`tools/vx_armcheck*.py`), the skeleton maths against an independent Python implementation, the career rules against the ROM's tables.

See `docs/TECHNICAL.md` for more, `docs/CAMPAIGN_FORMAT.md` and `docs/VX_FORMAT.md` for the career tables and the movie format.

## Testing (optional)

```bash
bash tests/run_tests.sh            # inside WSL / Linux: 28 host test groups (needs gcc and the converted data), a few minutes
```

The tests build each module for the host with `-fsanitize=undefined,address`, check the career data against the converted ROM tables, render every front-end
screen with a software backend (`tests/fe_preview.c` writes the pictures to `/tmp/fe_out`), survey all 21 levels for holes, and run the VX decoder
against `tests/vx_ref_*.txt` (made by `make_data.py` from your ROM). `tests/HARDWARE.md` is the checklist for a pass on a real PSP.

## Known differences from DS version

* The recorded riders (race opponents, Jam / Best Trick / S.K.A.T.E. opponents, the ambient skaters) show riding, pushing, flips, grinds, grabs and bails
  (not manuals, spins or wallrides). They never collide with you, and the S.K.A.T.E. opponent's turns are staged with guessed timing. The recorded racers are slowed
  to a pace the port's physics can match.
* Scoring uses the port's own point scale, not the original's table; the goal thresholds were kept as they are in the data.
* Customize: shoes, hat designs, decks and wheels follow the career's unlocks (the item numbers come from the original's own tables); tops, logos and the rest
  are all open (their unlock numbers are not understood). Which hat design goes with which hat style is a guess.

## Contributing

Pull requests are welcome for the source code. **Never commit anything derived from the game** (data, screenshots of whole screens are fine in issues but
not in the repository, dumps, disassembly listings, extracted text, models, sound). `.gitignore` already excludes the usual places; please check
`git status` before every commit.

## License

The source code in this repository is released into the public domain under the [Unlicense](https://unlicense.org): use it, change it, sell it, no
attribution needed. That covers this code only, never the game or anything made from your copy of it.

## Credits

Skate It is a game by Electronic Arts. This port is a fan project that needs, and respects, the original.
Built with the [pspdev](https://github.com/pspdev) toolchain; the analysis tools use [capstone](https://www.capstone-engine.org/) and
[Pillow](https://python-pillow.org/) / [NumPy](https://numpy.org/). It was tested on a real PSP.
