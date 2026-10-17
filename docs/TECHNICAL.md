# Technical overview

This document describes how the port is built. It contains no game data; addresses mentioned are locations in the original DS ARM9 / overlay code,
given so that the tools can be followed with a disassembler of your own copy. Exact byte layouts of the converted files are in the header comment of the
script that writes them (`tools/convert_*.py`) and of the C file that reads them.

## Pipeline

```
Skate It (USA).nds
   |  tools/extract_nds.py              header, ARM9 / ARM7, 18 overlays, banner, the 2175 files of the file system  -> extracted/
   |  tools/unpack_layers.py            inflate *.ZL, unzip the level archives                                       -> extracted/unpacked/
   |  tools/blz.py                      backward-LZ decompression of ARM9 + overlays                                 -> extracted/decomp/
   |  tools/campaign_dump.py            career tables from the ARM9 (challenges, pools, unlock sets, tricks, skaters) -> tools/campaign_*.json|tsv
   |  tools/campaign_text.py            the text database (all languages; the port uses English)                    -> tools/campaign_strings.tsv
   |  tools/convert_career.py           pack all of that, + the opponents' recorded runs, + the story lines          -> data/career.dat
   |  tools/convert_level.py (x21)      geometry, textures, collision, grind rails, markers                          -> data/<level>.lvl
   |  tools/convert_skater.py (x19)     model, texture, bone offsets, 376 animation clips                            -> data/skater_<name>.skt
   |  tools/convert_board.py            the generic skateboard                                                       -> data/board.brd
   |  tools/convert_custom.py           parts of create-a-skater, deck / wheel art, the gear item numbers                -> data/custom.pak
   |  tools/convert_scenes.py           the story scenes: conversation clips + the timing of the 57 NIS scripts          -> data/scenes.pak
   |  tools/convert_ai.py               the recorded runs of the original's skaters (457 tracks + events)              -> data/ai.pak
   |  tools/convert_ui.py               menu art, HUD sprites, XFN fonts                                             -> data/ui.pak
   |  tools/convert_sounds.py / _music  SDAT effects (PCM16) / streamed tracks (IMA-ADPCM)                           -> data/sounds.snd, music*.mus
   |  (copy)                            the two Actimagine "VX" movies                                               -> data/ealogo.vx, tempintro.vx
   |  tools/vx_mkref.py                 checksums of every decoded movie frame (host test reference)                 -> tests/vx_ref_*.txt
   v
tools/build.sh  (psp-cmake)             EBOOT.PBP                                                                    -> dist/SkateItPSP/
```

`tools/make_data.py` runs the whole chain; the other scripts in `tools/` are the reverse-engineering and survey helpers the formats were worked out with
(readers for each format, viewers that render a level or a pose sheet to a PNG, collision / rail / ledge / flag surveys, the ARM interpreter checks of the video codec).

## The ROM's formats (what the readers in `tools/` implement)

| Format | Where | Notes |
|---|---|---|
| NDS file system, BLZ | `extract_nds.py`, `blz.py` | standard; ARM9 and overlays are backward-LZ compressed |
| `.ZL` / `.zip` | `unpack_layers.py` | zlib streams and PK archives (each level is a zip: `OCT2`, `CMN1`, `PVS1`, `DST1` members) |
| `DST1` | `dst.py` | EA texture: 2 / 4 / 8 bpp indexed, RGB555 + opaque-bit palette (same bit layout as the PSP's 5551), optional colour key in entry 0 |
| GX command streams | `gx.py` | the DS geometry engine's display lists (vertex, matrix, texcoord, polygon-attribute commands) replayed into triangles |
| `DSM1` | `dsm.py` | static meshes (sky domes, props): a GX stream + texture name |
| `OCT2` + `CMN1` | `oct.py` | a level is a serialized octree: leaves hold collision polygons (indices into the shared `CMN1` vertex pool, 1.19.12 fixed) and a GX display list with a texture-batch trailer. World position = root centre + local * scale. The file prefix is a *detail level* (`1_` playable, `2_`, `4_` coarser, `7_` far background); the converter keeps the most detailed layer per chunk |
| `PVS1` (`track1.pvs`) | `pvs_tex.py`, `campaign_pvs.py` | texture id -> file table; the named object table (start points, challenge beams, start grids, zones, gates, bounds, teleports) and every grind rail as a spline (`col<L>_grind_<NNN>`) |
| `MBF` | `mbf.py` | skinned model: GX triangle strips with one rigid bone per vertex (`MTX_RESTORE`), a first-child / next-sibling bone tree |
| `ABF`, `ANIMDB.BOF`, `anims.arc` | `mbf.py`, `arc.py` | animation clips: per frame, per bone a unit quaternion (4096 = 1.0) plus the root position; bone rest offsets in the `.BOF`; 376 gameplay clips shared by all riders |
| `SDAT` | `sdat.py`, `sdat_dump.py` | Nintendo sound archive: sequences / banks / wave archives (the effects), STRM streams (the music) |
| `XFN` | `campaign_xfn.py` | the menu fonts: height, 256 widths, 8-bit glyph bitmaps |
| `NCGR / NCLR / NCER / NSCR` | `campaign_nitro.py` | standard Nintendo tile / palette / cell / screen formats (HUD sprites) |
| `AIRT` | `campaign_air.py`, `convert_ai.py` | recorded skater runs (opponents, ambient skaters): a position every 1/4 s (deltas + absolute fixes, 1.19.12) and 8-byte events (frame at 60 / s, kind, 4 argument bytes); the 7 pose bytes of each sample are not used |
| `VXDS` | `vx_video.py`, `vx_audio.py` | Actimagine VX movies; see `docs/VX_FORMAT.md` |
| ARM9 tables | `campaign_dump.py` | the career is data driven: see `docs/CAMPAIGN_FORMAT.md` |

### Skeleton maths

Every quaternion of a frame is a bone's full local rotation (no bind pose): `G_i = G_parent * T(offset_i) * R(q_i)^-1`, the root translation is the clip's
root position. The data frame is z-up; riders face +x in some clips and -y in others (the converter normalises that); the viewer maps (x, y, z) -> (y, z, x).
`tools/skel_verify.py` checks the model against evidence that does not depend on how it was found, `tests/test_skel.c` + `tools/test_skel_ref.py` compare the C
implementation with an independent Python one.

### Level files (`.lvl`)

Textures as swizzled T4 / T8 with a 5551 CLUT (pixels and palette copied as they are), one float vertex array `{u, v, abgr, x, y, z}` grouped per texture, a batch
table, the collision pool + polygons (with synthesised invisible edge walls where the visible geometry has none: `tools/collfix.py`), the grind rails and ledges
(`tools/rails.py`, `tools/ledges.py`), surface materials, and the markers of `PVS1` in world space. A uniform grid (`src/coll.c`) makes ground queries cheap; the
near-plane clipper (`src/nearclip.c`) re-triangulates geometry the camera is inside of.

## The engine (`src/`)

| File | |
|---|---|
| `main.c` | PSP start-up and memory partition, GU rendering of level / rider / board / HUD, input, the front end -> session -> front end loop, challenge start / result, intro movies |
| `skate.c`, `coll.c`, `rails.c` | rider physics: surface following on the collision grid, ollies, air, landing, rails / ledges / wallrides, manuals, bails |
| `control.c`, `trick.c`, `score.c` | the gesture logic (nub flicks -> tricks), the trick table (names, clips, points, balance), combos, multiplier, banked-line logs |
| `skel.c`, `rider.c` | skeleton evaluation and pose selection for the whole trick book (mirrored for regular stance) |
| `ghost.c`, `stage.c` | a recorded run played back as a rider (heading and speed from the motion, air / ground from the level, flips / grinds / grabs / pushes / bails from the events; the same pose code as the player), and who plays which recording when: race opponents (`race.c`), contest opponents, ambient skaters |
| `scene.c` | the story scene player: who stands where in a scene, which conversation clip he plays, the camera (host-tested; the drawing is in `main.c`) |
| `levelobj.c`, `career.c`, `goals.c`, `race.c`, `contest.c`, `progress.c`, `story.c`, `stats.c`, `tutorial.c` | level markers, the career data file, the rules of every challenge mode, opponents' recorded results, the pools / unlocks / sponsor rules and the profile save, the story pages, the Log Book statistics, tutorial tips in the player's buttons |
| `frontend.c`, `options.c`, `logbook.c`, `storyui.c`, `ui.c`, `hud.c`, `settings.c`, `custom.c` | all screens drawn through a backend (GU on the PSP, a software one in `tests/ui_sw.h`), the settings file, the run-time assembly of a custom skater |
| `mixer.c`, `sfx.c`, `sparks.c`, `audio.c` | PCM mixer, effect selection by surface / trick, grind sparks, the PSP audio thread |
| `vx.c` | VX movie decoder (a port of the game's ARM9 decoder; verified bit-exactly) |
| `nearclip.c` | near-plane clipping |

Only `main.c` and `audio.c` touch the PSP SDK; the rest is plain C tested on the host.

## Recorded runs

`data/ai.pak` holds, per recording of the ROM (`<level>/<challenge>_<mode>_<a>_<b>`; mode 0 = the free-skate routes), the track (a point every 1/4 s in the level's
frame), a flag where the track jumps (a respawn after a bail: every mid-recording position fix is 9 m or more) and the events. Worked out against the real
collision of the levels (`tests/ghost_probe.c`): the track is the ground under the rider (air time is the rest); event 27 = a flip (argument 0 = the original's
trick id), logged at take-off; 6 = push; 56 / 57 = a bail begins, 54 = the respawn that ends it; 49 = a line banked (the points); 51 = a grind starts (argument 0 = the grind's trick id, ended by 38 / 39 / 35); 43 / 44 = a grab starts / ends (argument 0 = the grab id). The heading comes from the motion.
Races play the recording of each opponent at a rate that keeps his pace along the course under what the port's physics can match; Jam plays it in step with the
contest clock, Best Trick restarts it every round, S.K.A.T.E. plays the tail of the recording of the trick he sets. The ambient skaters play the free-skate routes
one after the other, never starting near the player. `tests/test_stage.c` plays all 457 recordings, poses them, and runs the 24 contests and the ambient riders
of all 21 levels.

## Customize and the career's gear

The ARM9 holds pointer tables to the file names of the gear (shoes, hat designs, decks, wheels ...) in the order of the unlock numbers (`tools/convert_custom.py`,
`rom_orders`): the unlock sets of the sponsors hold exactly their own items under those numbers, which is how the order was confirmed. Unlock kinds 5 (shoes), 7
(wheel art), 8 (deck art), 2 (hat designs) are therefore gated in the editor; the numbers travel in `custom.pak`. The board's deck and wheel pictures are painted
into the board texture at run time (`custom_board_texture`).

## Story scenes

The original's 57 story scripts (`docs/CAMPAIGN_FORMAT.md` section 13) are one template: a single speaker, the guest, filmed in close-up while he talks. `data/scenes.pak` holds the 22
conversation clips of the original (with the board clips, the bone offsets they were made for and the direction each one faces) and the timing of each script; `src/scene.c` picks the guest
(the script's spawn record), his place (the start grid of the scene's event, else of the level), a clip of the kind the talk span asks for (drawn at random, blended over 0.3 s) and the camera
(look-at one metre above the guest, the eye one metre higher and the script's distance towards +z). The front end keeps the dialogue pages; while one is shown `main.c` draws the level and the
guest behind it. Which way the guest faces the camera is not settled by the data (SQUARE turns him round).

## Verification

* `tests/run_tests.sh` builds and runs about thirty host programs under UBSan + ASan: physics, controls, every trick class, rails, scoring, goals on real levels,
  the career progression (300 headless campaigns), stories, statistics, settings, the custom skater (hundreds of choices against the loader's rules), every
  front-end screen, sound, the VX decoder (every frame against the Python reference), level fuzzing around known trouble spots.
* `tests/level_survey.c` checks, for all 21 levels, that every career place (spawn, beam, start grid) has ground under it and nobody is boxed in.
* `tests/HARDWARE.md` is the checklist for a pass on a real PSP; hardware passes found bugs that the host cannot (see the comments that mention a "hardware test").
* The ARM-interpreter checks (`tools/vx_armcheck*.py`) run the game's own video / audio / colour code under `capstone` + a tiny CPU model and diff it against the
  port. They are development tools, not part of the game.
