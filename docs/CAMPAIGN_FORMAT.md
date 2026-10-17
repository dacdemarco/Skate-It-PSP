# Skate It (DS) - how the CAREER / CAMPAIGN is stored in the ROM (format notes)

> This document describes record layouts, rules and the way the data was found. It does **not** reproduce the game's data (challenge lists,
> point tables, story and menu text, the order of the career): `python tools/make_data.py ROM` decodes all of that from your own copy into
> `tools/campaign_data.json`, `campaign_levels.json`, `campaign_challenges.tsv` and `campaign_strings.tsv`, which are git-ignored.

All statements are from ROM data unless marked GUESS. Confidence tags: [V] verified (cross-checked by code, by exact size/count fits, or by independent data),
[L] likely (consistent evidence, no direct proof), [?] unknown / guess.
Addresses are RAM addresses of the decompressed arm9 (`extracted/decomp/arm9.bin`, base 0x02000000) unless an overlay is named.

Scripts (all new, `tools/campaign_*.py`, run from the project root, Windows python):
| script | what |
|---|---|
| campaign_dump.py | MAIN: writes `campaign_data.json` (areas, modes, levels, skaters, tricks, pools, 103 challenges with decoded objectives + placement, unlock sets), `campaign_challenges.tsv` (one line per challenge), `campaign_levels.json` (all level objects/rails/cameras/traffic) |
| campaign_text.py | textdb.arc -> id/key/string (all 6 languages); `campaign_strings.tsv` = all 1544 English strings with their keys |
| campaign_pvs.py | parser for the object sections of a level's track1.pvs (start points, challenge markers, gates, zones, rails, ...) |
| campaign_air.py | AIRT recording container reader (457 DATA/AI files) |
| campaign_xfn.py | XFN font reader/renderer (format fully verified) |
| campaign_nitro.py | NCLR/NCGR/NSCR/NCER readers + renderers (cell bank works) |
| campaign_rail_check.py | proves PVS grind splines == our inferred rails |
| campaign_dis.py / campaign_callers.py / campaign_refs.py / campaign_strs.py | RE helpers: annotated disasm that survives data words, raw-scan callers, constant->code refs, module strings |

## 0. Executive summary
* The whole career is **data-driven from arm9 tables**: 103 challenge records (0x0206E038), 19 progression "pools" (0x020814D0), 118 unlock sets (0x02077DCC), 21 levels, 12 game modes, 20 skaters, 101 tricks. Everything is decoded below and dumped to JSON.
* The in-level things (challenge start beams, start grids, race gates, zones, bounds, spawn points, **every grind rail as a spline**, traffic routes, camera paths) are stored in each level's `track1.pvs` as a **named object table** (found + decoded, all 21 levels, positions in the same world space as our converted levels). This also gives the exact rails our heuristic only approximated (all 20 inferred Amphitheatre rails coincide with PVS splines).
* Career structure: **19 sequential pools** (Tutorial -> A -> Paris -> B -> board sponsor (C) -> D -> truck sponsor (E) -> F -> wheel sponsor (G) -> H -> shoe sponsor (I) -> Frisco Freakout (J) -> Barcelona (K)). A pool is complete when `required` of its challenges are done; only the current pool's challenges are open. Total required completions: 49.
* The `AI/*.dat` files are **recorded opponent runs** (zlib payload: timeline of samples + trick events), NOT goal data. Goal data = the arm9 tables.

## 1. Front end / flow (strings: textdb.arc, 1544 English strings, keys in `campaign_strings.tsv`)
textdb.arc = Language.dat x6 (English, French, German, Italian, Spanish, Dutch), stringid.dat (key names), textdb.idx. Each .dat = `u32 offsets[1544]` + NUL-terminated Latin-1 strings; string i of every file <-> key i of stringid.dat [V]. (NOTES.md's "4523 strings" was a miscount: there are 1544.) **Every `str id` in the arm9 tables below is just this index** (e.g. 723 = TXT_GAMEMODE_FREESKATE) [V].
String groups (index ranges): 11-35 career UI; 36-125 challenge descriptions (89 used, 74-77/85/89 placeholders); 131-218 challenge map descriptions; 219-310 challenge names; 314-325 pool names; 330-637 credits (EA Black Box + Exient); 638-668 customisation; 695-699 EA Trax (5 tracks: Collarbone, Ghost Town, Death Or Glory, Possessed 2 Skate, OpShop); 710-729 game modes + descriptions; 730-734 areas; 736-750 main-menu help; 777-801 locations; 802-899 menus/stats/pause/MP; 923-1029 NIS dialogue (57 scenes); 1034-1073 objective text templates; 1087-1107 skater names; 1135-1169 sponsors; 1170-1250 My Spot / Spot Share; 1282-1292 challenge win/lose taunts (ch 3,6,7,10,20,33); 1303-1427 trick names + trick book; 1428-1514 tutorial text; 1517-1543 Wi-Fi.
Screens (from strings + overlay asset loads) [L]:
* Boot: EA logo FMV (ealogo.vx), intro FMV (tempintro.vx), language select (6 flag BMPs), Title ("Touch to start", (c) 2008 EA).
* Main menu items (help strings): **Career** (Start/Continue), **Freeskate**, **Multiplayer** (Host single-card / Host multi-card / Join / Nintendo WFC online), **My Skate** (Select Profile, Customize, Log Book = stats + trick book, Game Settings, Credits), **My Spot** (Spot Sculpt editor + Spot Share download/upload), **Tutorial**. Game Settings: Input Buttons/Stylus, Push control D-pad/Touch, Handedness, Alignment, Stance Regular/Goofy, Display units feet/m, Sound (Music/SFX/Dialogue), Language.
* Career: create skater (TXT_CUSTOMISAION_WELCOME) -> NIS_001 (Reda intro) -> **Career map** (Select Area -> Select Location -> challenge list "Go To Event"; areas = Urban/Parks/Indoors/Small Spots/Real World; hub-list UI, not a geographic map) -> challenge intro (ov14: mode icon `GameModes/<mode>_large.bmp` + description) -> play -> results/retry -> map. Pause menu: Resume / Restart Event / View Replay / Change Location / Leave Event / Quit. In-level: challenges are "green light beams", press the touch-screen icon to start; "backpack" = Challenges / Map / Music Player.
* Modes (table below): Freeskate, Tutorial, Jam, S.K.A.T.E. (game of skate), Best Trick, Own The Spot, Photo, Film, Deathrace, Checkpoint. Strings also mention Clock Stopper and "Obstacle" (unused/removed).
* Endings: after pool 18 -> TXT_INGAME_ENDOFGAME + TXT_CAREER_SOTY ("%s has been named Thrasher Skater Of The Year", NIS_053, thrasher magazine cover BMPs).

## 2. Game modes (arm9 0x02070B04, 12 x 0x28) [V ids vs AI file types and challenge table]
`{u32 id, u32 str id, char name[16], u8 flags[16]}` - flags unknown [?] (e.g. SKATE `00 00 00 04 00 00 01 00 00 00 00 01 00 01 00 00`).
id: 0 FreeSkate, 1 Tutorial, 2 Tutorial(advanced), 3 Jam, 4 SKATE, 5 BestTrick, 6 OwnTheSpot, 7 Photo, 8 Film, 9 Deathrace, 10 Checkpoint, 11 Checkpoint(variant). The AI file name field 2 (`NNN_<mode>_a_b.dat`) is this id.

## 3. Areas / levels (arm9 0x02070CE4, 0x02070D0C) [V]
Areas: 0 Urban, 1 Parks, 2 Indoors, 3 Small Spots, 4 Real World.
Level table 21 x 0x24: `{u32 id, u32 area, u32 str id, char* zip name, char* sky .dsm, char* sky .dst, s32 f (20.12; -100.0, -150.0 ...), u32 2000, u32 ?}` (last two [?]; the last column 100..2000 may be a scale/view value). No unlock data in it; **all 21 zips are real levels; no menu/other levels**. 21st = SpotSculptWarehouse (empty My Spot base).
| id | zip | in-game name | area | | id | zip | name | area |
|---|---|---|---|---|---|---|---|---|
| 0 | Warehouse | Plan B Warehouse | Indoors | | 11 | LakeSherwin | Lake Sherwin | Urban |
| 1 | Spillway | Spillway | Urban | | 12 | ElementarySchool | Elementary School | Parks |
| 2 | CommCentre | Community Center | Parks | | 13 | OldTown | Old Town Race | Urban |
| 3 | Wilshire | LA Rails | Small Spots | | 14 | Paris | Paris | Real World |
| 4 | EMB | The Gap | Small Spots | | 15 | SanFran | San Francisco | Real World |
| 5 | Hellavator | The Flood Gate | Small Spots | | 16 | SouthBank | London | Real World |
| 6 | DIYPark | DIY Park | Parks | | 17 | Barcelona | Barcelona | Real World |
| 7 | Amphitheatre | Library | Urban | | 18 | Rio | Rio | Real World |
| 8 | BigAssWall | Wipeout Wall | Small Spots | | 19 | Shanghai | Shanghai | Real World |
| 9 | MegaPark | Danny's Mega Park | Parks | | 20 | SpotSculptWarehouse | My Spot warehouse | - |
| 10 | XGames | Frisco Freakout | Indoors | | | | | |
(Our converted level "Amphitheatre" is the career's **Library**.) `DATA/LOCATIONS/<x>.bin` (2-17 KB) is only a zip file-index cache (path string + 0x60-byte records, one per zip member) - dead end for gameplay.
Level zips contain only OCT2/CMN1/PVS1/DST1 (no scripts); all placement data is in track1.pvs (section 8).
Free-skate location availability is NOT in the level table; location availability on the career map is derived: a location shows a challenge icon if the *current pool* has an unfinished unlocked challenge there (arm9 0x0201D764) [V]. Free-skate unlocking rule [?].

## 4. Skaters (arm9 0x02075950, 20 x 0x1C) [V]
`{char* "skater_<name>_lod%d.mbf", char* "<name>.dst", u32 id+1, u32 str id, u32 flag, s32 scale(20.12), s32 scale2}`; index = skater id used by challenges (`opponents`) and unlock kind 0.
| idx | name | text | goofy?(flag) | scale | | idx | name | text | flag | scale |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | dannyway | Danny Way | 0 | 1.0 | | 10 | sammy | Sammy | 0 | 1.0 |
| 1 | terrykennedy | Terry Kennedy | 1 | 1.0 | | 11 | mike | Mike (local kid) | 0 | 1.0 |
| 2 | robdyrdek | Rob Dyrdek | 1 | 1.0 | | 12 | aaron | Aaron | 0 | 1.0 |
| 3 | mikecarroll | Mike Carroll | 1 | 1.0 | | 13 | carl | Carl | 1 | 0.6 |
| 4 | chrishaslam | Chris Haslam | 0 | 1.0 | | 14 | emocrys | Emo Crys | 1 | 0.6 |
| 5 | lucaspuig | Lucas Puig | 1 | 1.0 | | 15 | jackknife | Jack Knife | 1 | 0.6 |
| 6 | erickoston | Eric Koston | 1 | 1.0 | | 16 | jayjay | Jay Jay | 1 | 0.6 |
| 7 | jakebrown | Jake Brown | 1 | 1.0 | | 17 | lilrob | Lil Rob | 1 | 0.6 |
| 8 | shingo | Shingo | 1 | 1.0 | | 18 | spitball | Spitball | 0 | 1.0 |
| 9 | slappy | Slappy | 1 | 1.0 | | 19 | reda | Reda (cameraman, not a rider) | - | - |
Only attributes in the table: model/texture names, scale (0.6 = the five kid skaters), one 0/1 flag [?] (probably stance), name string. **No per-skater stats** (speed/ollie/etc.) were found: all skaters share the same physics/animation set. Customisation (custom skater): body (gender M/F, build thin/medium/fat, skin), face (type 4 faces x hair 10 M + 10 F colours Black/Blonde/Brown, beard 9, hats), outfit (top/bottom/shoes/logo), board (deck 61 BMPs, trucks, wheels 53 BMPs, tuning), art (3 board + 3 logo paint slots). Inventory: DATA/SKATERS/CUSTOM (Models/Face 187 MBF, Top 73, Pants 72, Board 1; Textures: Board 114, Logos 70, Face 66, Top 39, Pants 36, Shoes 24) and DATA/FRONTEND/CUSTOMISATION (120 icons).

## 5. CHALLENGES (arm9 0x0206E038, 103 x 0x1C) [V] - the core table
Index code: `u32 idx; u16 name_str; u16 desc_str; s8 location; s8 mode; s8 slot; u8 time; u8 b12,b13; u8 opp1,opp2; u8 opp3,skate_set,intro_nis,b19; u16 flags; u16 nobj; u32* objectives`.
Verified by the accessors at 0x020168A8-0x02016A18 (loops 0..0x66, `ldrsb [+8]`=location, `[+9]`=mode, `[+0xA]`=slot).
| field | meaning |
|---|---|
| name_str / desc_str | text ids of TXT_CHALLENGE_NAME_nnn (219+idx) / TXT_CHALLENGE_DESC_nnn (36+idx); map description = TXT_CHALLENGE_MAP_DESC_(idx+1) (131+idx); 0 = none |
| location | level id (section 3) [V] |
| mode | game mode id (section 2) [V] |
| slot | list position inside the location AND the `chalNNN` object index in that level's PVS (chal000, chal001 ...) [V] (e.g. Spillway slots 0-3 = Tube Test, Deja Vu, Tube Test 2, Rats In A Maze; PVS has chal000-005) |
| time (+0xB) | seconds: Film/Checkpoint/Deathrace/Jam/BestTrick time (Film 30/45/60, Jam 120, Jam-Frisco 90, BestTrick 90 (or 20 per round for the 5-round Plan B/Frisco), SKATE 10); 0xFF = none (Photo, OwnTheSpot, Tutorial) [L for BestTrick/SKATE meaning, V for Film] |
| b12 (+0xC) | number of rounds for Best Trick (3 or 5) [L] |
| opp1,opp2,opp3 (+0xE,+0xF,+0x10) | opponent skater indices into section 4; 0xFE = none (code at ov12 0x020FE896 counts entries != -2) [V] |
| skate_set (+0x11) | 0-3 = difficulty/trick-set of the S.K.A.T.E. opponent (AI file `a` index) [L]; 0xFF none |
| intro_nis (+0x12) | sponsor challenges: NIS scene played BEFORE = this+1 (1-based key TXT_NIS_nnn), AFTER = this+2 [V text match for all 21 sponsor challenges; ch47 uses alternates 55/33] |
| flags (+0x14) | bit0 = career challenge; bits1-3 = available in 3 multiplayer variants (arm9 0x02016A18: value passed 0 -> bit2, 1 -> bit1, 2/3 -> bit3). 0x0F = career + MP, 0x01 = career only, 0x0E = MP only (ch 75, 76, 84) [V code, L meaning of which MP variant] |
| nobj (+0x16) | number of 24-byte objective records at the pointer [V] |
| objectives (+0x18) | pointer to the objective list (section 6) |
Challenges 73,74 are empty; 88-92 = My Spot challenge slots (no objectives; defined by the player); 93-102 = the tutorials; some are multiplayer-only; the Own-The-Spot bonus events are optional (listed in a pool only partially, otherwise unlocked by sets).
The decoded list (every row with location, mode, time, opponents, objectives, rails, AI files) is written by `tools/campaign_dump.py` to `tools/campaign_challenges.tsv` / `campaign_data.json`.

## 6. Objective lists (the goals) [V layout, V most semantics]
24-byte records `u32 type, u32 a0, u32 a1, u32 w3, u32 w4, u32 w5`; list at the challenge's pointer, `nobj` records.
| type | name | args |
|---|---|---|
| 0 | SCORE | a0 = points (750, 2000, 2250, 3000, 3500, 4000, 5000, 6000 ...) |
| 1 | LINE_SCORE | a0 = points in a single line (tutorial 400) |
| 2 | TRICK | a0 = trick id (section 7) or `0x40000000 \| class mask` (mask bits: 0x1 flip trick, 0x4 advanced flip, 0x8 grind, 0x10 manual, 0x20 grab, 0x80/0x100 advanced grind on front/back trucks, 0x200 advanced grab, 0x400 advanced grind, 0x800 any trick); a1 = count, **or spin degrees when w3.lo == 2** (El Taco kickflip 360, Spin Doctor grab 360, tutorial "any trick with 180 spin") |
| 3 | AIR_TIME | a0 = seconds (ch64: 11 s) |
| 4 | SPIN | a0 = total degrees (360, 540, 720, 1080) |
| 5 | LINE | a0=1: "do a line" (tutorial 10) [L] |
| 7 | SKATE_GAME | a0 = 0x40000800 (any trick) |
| 8 | JAM | none (score contest vs the listed opponents) |
| 9 | BEST_TRICK_ROUND | one per round; a0 = round/obstacle selector [?] (0,1,3,4,... per challenge), w4 = rails of that round |
| 10 | OWN_THE_SPOT | a0..a1 = score range (e.g. 125..350, 800..1200) = the "Beat %s points" value is chosen in this range [L], w4 = rail |
| 11 | CHECKPOINT | a0 = gate number (`checkpoint_NNN` in the level), a1 = bonus seconds when passing (0xFFFFFFFF on the LAST record = finish gate). Deathraces have ONE record = the finish gate (progress along `progress_spline`) |
w3 = `lo16: modifier (0xFFFF none; 2 = a1 is spin degrees; other bits e.g. 1,4,8,0x10,0x20,0x40,0x80 seen, meaning [?])`, `hi16: unknown value (0x38,0x49,0x4C...0x74) in the first record only [?]`.
w4 = `lo16, hi16`: **1-based number NNN of a `col<N>_grind_<NNN>` rail spline in the level's PVS** (0 / 0xFFFF = none) - up to two alternative rails [V: e.g. Ride The Rail rail 26 lies 0.9 m from chal000_chalstart, Baker's Dozen rail 18 1.1 m, Piece of France rail 53 3 m, AdVenture 28/44 4.6 m].
w5 = `lo16: explicit objective text id (TXT_OBJECTIVE_*, e.g. 0x411 = "Ollie over the picnic table"); 0 = generate from templates TXT_OBJ_*/TXT_OBJECTIVE_* (e.g. "%s Advanced Flip", "Get %i secs of air"); hi16: flags (0x0100/0x0101)` [L].
Example (ch 9 Pop Quiz, 30 s): TRICK flip x1 -> TRICK grind x1 ("..into a Grind") -> TRICK flip ("..and then flip trick out"). Example (ch 60 You're A Shoe In, 60 s): SCORE 5000, grinds x5, SPIN 1080.
Trick ids match the tutorial: 0 Ollie, 1 Pop Shuvit, 2 FS Pop Shuvit, 5 Varial Kickflip, 9 360 Flip, 13 Kickflip, 14 Heelflip, 15 Nollie, 22 Nollie Inward Heel, 73 Method, 0x65 (101) = push (tutorial "try a few pushes") [V].

## 7. Trick table (arm9 0x02076800, 101 x 0x1C) [V ids/names/points]
`{u32 str id, u32 base points, u32 hold points/s (grinds/grabs), u32 flag, u32 id+1, u32 ?, u32 ?}` - the full list is `campaign_data.json` `tricks` (from your ROM). id ranges: 0-14 ground flips, 15-29 nollie versions, 30-35 late flips, 36-69 grinds (17 types x BS/FS; hold points = base), 70-98 grabs, 99 Nose Manual, 100 Manual. Class boundaries verified by counts 15+15+6+34+29+2; the trailing two columns are shifted by one row relative to the id and difficulty was not resolved from them [?].

## 8. Career progression [V]
**Pool table** arm9 0x020814D0, 19 x 0x14: `{u32 name str id, u32 n, u32 required, u8* challenge list, s32 sponsor slot (-1 none, 0 board, 1 truck, 2 wheels, 3 shoes)}`; challenge byte lists at 0x0206DFE0-0x0206E030.
Current pool = first pool, in index order, whose completed-count < required, or (when complete) whose sponsor slot is still unchosen (function 0x0201D60C, loops 0..0x12; returns 19 when finished). Only challenges of the current pool are open (0x0201D6CC); save data: `done[idx]` byte array at save+0x10 (save pointer in 0x02097424), per-pool seen flags at +0x77, current pool at +0x8C, chosen sponsors at +0..+0xF (4 words).
Pool order, which challenges each pool holds, how many completions it needs and which sponsor slot it fills are in `campaign_data.json` `pools` (from your ROM). The NIS story scenes (`TXT_NIS_nnn`, 57 scenes) are subtitle lines with frame timings + camera / animation commands (script table at 0x020753C0, decoded in section 13: one speaker, an orbit camera, a gesture clip drawn from the animation database); a scene before / after every sponsor challenge comes from the challenge's `intro_nis` byte, the rest of the order is the port's own. The pool-advance routine is ov8 0x020ACBA8.
**Unlock sets** arm9 0x02077DCC: 118 sets `{u32* list, count}`, entries `kind<<16 \| index` (0xFFFF = whole kind). Applied by 0x0201C124. Rules [V]: sets 0-10 = new-profile defaults; set (11+pool) is applied when pool becomes current (unlocks that pool's challenges, and skaters); **set (30+challenge idx) is applied when that challenge is completed** (table 0x02075098, challenges 0-87 -> sets 30-117; 73/74 and 88+ none) and gives the next chained challenges + rewards. Kinds: 10 = challenge idx [V]; 0 = skater idx [L]; 3 = My Spot parts (222 entries; "New My Spot items") [L]; the gear kinds [V, 2026-10-16]: 5 = shoes (24), 7 = wheel art (52), 8 = deck art (60), 2 = hat items (item = position in the hat table + 1, 0 = no hat), 6 = trucks (23: 3 defaults + 4 sponsors x 5; no art exists for them), 4 = the 28 bottoms (all unlocked at the start); 1 (one per sponsor, 14 items) and 9 (108 items, probably tops and logos) are not understood. The item numbers are positions in the pointer tables to the file names at arm9 0x02083BA0..0x020840A4 (tops 35, logos 67, bottoms 28, shoes 24, hair 6, hats 20, beards 24, decks 58, wheels 52; the blank canvases board_white / board_whitedirt are items 58 / 59 of the defaults); every sponsor's unlock set holds exactly that sponsor's items under them (set 91 = DC's shoes 7-11, set 51 = Plan B's decks 44-48, wheels 37-40, caps 11-12 ...).
Total required completions = 49 (+ tutorials 5 counted).

## 9. In-level object table (track1.pvs) [V]
Layout (tools/campaign_pvs.py; end of the 3 sections == start of the 64-byte texture-name table in all 21 levels): after the octree-name table: `[u32 n] n x 76 B {char name[32], u32 junk[8], s32 x,y,z (20.12 /4096 -> world, Y up, same as convert_level)}`; `[u32 n] n x {char name[32], u32 npts, npts x (s32 x,y,z)}` splines; `[u32 n] n x {name[32], u32 npts, npts x 12 B pos + npts x 8 B aux}` camera paths (aux 2 x s32 [?]). (Name buffer tails are uninitialised memory = stack junk; ignore.)
Object names (counts over all levels) [V names, L semantics]:
* `start_pos_1..4` / `start_dir_1..4`: free-skate spawn points + a point ahead of each (dir).
* `chalNNN_chalstart`: the challenge trigger (green beam) of the challenge whose `slot` == NNN in that location.
* `chalNNN_start_pos_k` / `start_dir_k` (k = 1..4): start grid (player = 1, opponents 2-4; AI recordings start at these points: e.g. Amphitheatre 075_5_0_0 starts exactly at chal004_start_pos_2).
* `chalNNN_zone_k_a/_b`: AABB corners of the trick zone (BestTrick/Jam/Photo areas); `boundinner_a/b`, `boundouter_a/b` (global) and `chalNNN_boundinner/outer_a/b`: play-area boxes (inner/outer limit) [L].
* `checkpoint_NNN_a/_b`: the two end points of gate NNN (global per level; challenges reference gate numbers; Spillway checkpoint_001..008). `progress_spline`: race centre line (22 points in Spillway).
* `teleport###_a/_b_###`: teleporter/respawn pairs (BigAssWall, MegaPark, OldTown, SanFran, Shanghai, Spillway, XGames); `spotarea_NN` (2 points; EMB 6, Barcelona 2, SouthBank 2, SSWarehouse 2: My Spot build areas / real-world spot boxes); `trigger_a/b` (SanFran); `traffic_T_NN` splines (car/tram/boat routes, models in DATA/TRAFFIC), `group#` (OldTown) [?].
* **`col<L>_grind_<NNN>`**: grind rails/ledges as polylines (2-18 points); L = octree layer prefix (1,2,3,4,6,7,8) of the geometry the rail belongs to; NNN is 1-based and contiguous per level (Spillway 125, Elementary 331, Lake Sherwin 220 ...). Amphitheatre: 187 splines; all 20 rails inferred by tools/rails.py coincide with PVS splines (campaign_rail_check.py: 20/20 within 0.6 m). Use these instead of the collision-flag heuristic and for the `rails[]` goals.
* `cameraFOV_NNN` (name has an integer 45..135 [?], 100/60 samples): replay/spline camera paths.
Counts: all 21 levels parse (Spillway 102 objects/126 splines/11 cameras; Amphitheatre 65/187/6; CommCentre 173/211/6; SanFran 136/220/7; ...). campaign_levels.json has all of it; campaign_data.json challenges carry a `placement` block (chalstart, start grid, zones, gates for races).

## 10. AI recordings (DATA/AI, 457 files) [V container, ? payload]
Not goals: **recorded runs of opponent/pedestrian skaters**. File `<LEVEL>/<chal>_<mode>_<a>_<b>.dat`; `<chal>` = challenge idx (**0-based**, equals the challenge table idx), `<mode>` = mode id (3 Jam, 4 SKATE, 5 BestTrick, 9 Deathrace, 10 Checkpoint; 0 for the free-skate ones named `000_0_0_N`), a = round / SKATE trick-set index, b = opponent index (BestTrick: 3 rounds x 3 opponents = 9 files; SKATE: sets 0-3/10; races: 1-3 files = opponents). `000_0_0_N` (10-22 per level) = ambient AI skaters that roam each level in free skate.
Container (tools/campaign_air.py): `05 'AIRT'`, +0x0D u32 inflated size = 14B+8C+3D+7, +0x11 tag 4, +0x12 u32 B (samples; 368 for BestTrick/SKATE rounds, 31-150 for ambient routes), +0x16 u32 C (events), +0x1A u32 D, +0x1E 6 x s32 (start xyz, end xyz), +0x36 u32 R [?], +0x3B u32 origin `[01][mode][chal][a]`, zlib stream at +0x42 (CMF 0x18). Payload: first record `01/02` + 12 B, then alternating `07`+6 B samples (3 x s16 deltas?) and 7-byte pose snapshots, `02 <u16 frame> <trick id> <arg> ...` trick events (trick ids = section 7), terminator `03`. Samples/pose fields NOT decoded [?] (integrating the s16 triplets did not reproduce the stored end point). To rebuild opponents we can instead script AI (we know start grid, rail ids, trick ids, finish gates) or just use start/end + trick event timeline.

## 11. Collectibles
Letters S-K-A-T-E exist only as the S.K.A.T.E. game (HUD cells "S. K. A. T. E." in hudsprites_mainscreen; letters given as penalties) - **no collectible letters in levels**; no "gap" markers (gaps = challenge objectives with rails/zone). Level pickups: none found. Check points = gates; "beam" = DATA/IMAGES/CareerItems/GameModes/beam.dst; skate_arrow.dst = direction arrow.

## 12. UI / front-end art inventory
Decoded status: BMP = standard Windows BMP (8 bpp indexed mostly, 7 x 4 bpp, 1 x 16 bpp; magenta 255,0,255 = transparent); `.ZL` = zlib (already unpacked by unpack_layers.py to `extracted/unpacked`); `.dst` done earlier; **NCLR/NCGR/NSCR/NCER now readable (campaign_nitro.py)**, NANR (animation) header seen only, XFN fonts fully decoded (below), VXDS FMV not decoded.
Overlay roles (from which overlay loads what) [L]: arm9 = boot/title/language/front-end framework + customisation + skater data + career logic tables; ov2-6 Nintendo WFC/GameSpy/HTTP (DWC); ov8 = career map logic + AI/traffic loader + replay UI (`spr_career`, `MapLogos`, `LocStatusIcons`, subscreen_replay); ov9 = My Spot UI (47 Skt_SS_* BMPs); ov11 = My Spot 3D editor (designs, arrows, challenge icons); ov12 = in-game session (HUD sprites, sponsor icon .dst, effects, `Phase_*` challenge state machine: IntroNIS, ChallengeStart, Round*, ChallengeResults, PostChallengeRetry, ShowingReplay, WaitingToStartFilming ...); ov13 = level loader (names `start_pos_`, `checkpoint_`, `boundinner_`; track1.pvs/verts.cmn); ov14 = challenge intro + trick book (`SktF_challenge_elements_v3`, `GameModes/<mode>_large`); ov15 = profile/skater select/customise/paint/keyboard; ov16 = career map/menus/stats screens (`Skt_career_*`, ChalStatusIcons, sponsor BMPs, my_spot_N, trickbook top); ov0/1/7/10/17 tiny.
| asset group | what |
|---|---|
| IMAGES/BGS (BMP 256x192) | MainTitle (white "skate it" TV frame), SktF_BG_TOP/BOTTOM/BG2/DIMMED (the grimy city backgrounds of all menus, filmstrip border), creditscreen, paint bg, touch masks |
| IMAGES/CareerItems | career UI: Skt_career_TOP_box1/2/bars, BOTTOM_box (paper frames), careericons(+_32), ChalStatusIcons 16x90 (challenge status: locked/open/new/done), LocStatusIcons 32x180, board/truck/wheel/boot icons, sponsorbox, Thrasher magazine; MapLogos/<Level>.bmp (120x120 stamp per location) + Border_*, rip_*, Coffee_Stain, Aeroplane_Stencil (map-screen collage pieces); CareerLocs/<Level> thumbnails; GameModes/<Mode>.bmp + `_large.bmp` + `.dst` (mode icons small/large/3D) |
| FRONTEND/LOCATIONS | 210x106 per-level pictures (21 + my_spot_1..3) used in the location boxes |
| FRONTEND/BITS, COMP_* | buttons, hub lists (1Row/2Col/3Col/4Col +HL), scroll lists, msgbox, keyboard, title header, flags (6 languages), option boxes, playerselect, skater frame |
| FRONTEND/SPONSORICONS | 27 sponsors: `<name>.bmp` (menu) + `<name>.dst` (HUD) |
| FRONTEND/CUSTOMISATION, SPOTSCULPT, TRICKBOOK | customise icons (hair/beard/face/clothes/gender/stance/hand/build) ; 51 Spot Sculpt UI pieces; trick book backgrounds (Frontend_Top, Ingame, Board, shoulder buttons, Spin) |
| SPRITES (NCGR+NCLR+NCER+NANR) | HUD/hudsprites_mainscreen (29 cells: multiplier badges x1.5/x2/x3, countdown 1/2/3, S. K. A. T. E. letters, arrows, photo-frame corners, timer bars), hudsprites_subscreen(+_chal) (43/28 cells), hudsprites_FanC_0-5, spr_replay, FONTS/spr_font_{large,medium,tiny}(+tiny layouts 64/96/128), MENU/{Loading,spr_career,StandardMenu,WirelessStrength}, shared pal_sprites.NCLR |
| TILES | CareerMap (15x15 tile 8bpp dither pattern background), subscreen_frame_{left,center,right}, subscreen_replay (touch-screen frames) |
| FONTS (XFN) | see below |
NCER (campaign_nitro.read_ncer): KBEC block, per cell `{u16 noam, u16 attr, u32 oam offset}` + 3 x u16 OBJ attrs; tile index counts one 8x8 tile of the NCGR's bit depth, 1D mapping [V by rendering; sprites sheet pngs were checked].
**XFN** (campaign_xfn.py, [V: exact file size on all 9]): `u8 height` (8/14/19/22/27) at +0; `u8 1` at +1; `u8 width[256]` at +0x101 indexed by char code (Latin-1 + the game's special-char order); glyph pixels from +0x201: for each code with width>0 in code order `width*height` bytes (8 bit index, 0/1 for small fonts, up to 7 for fe_large etc., 2 fonts show up to 203/242), row-major; code 0x20 has no pixel data. Fonts: fe_extralarge/large/medium/small, fetiny, font_tiny_blitted (menus/HUD), pause_large/medium/small (subset 91-141 glyphs). Palette for the indices is set by the game (outline + fill).
Sprite fonts (`spr_font_*` NCGR/NCER) are the in-game 3D-screen text (224 cells for medium).

## 13. What is still unknown / suggested next steps
1. w3.hi16 / checkpoint first-record hi value, BestTrick a0 selector, w3.lo modifier bits (1,4,8,0x10,0x20,0x40): needs the ov12 objective evaluator (find the type switch; Thumb `__switch` style, not found by simple pattern).
2. NIS script table (0x02073CDC...: [-1 or id][frame range][text id][..] rows) and which NIS plays at which pool/challenge event.
3. AI recording sample/pose/event layout (section 10).
4. Mode flag bytes (section 2), level table f6/f7/f8 columns, challenge b13/b19 bytes, flags bits meaning per MP variant, kinds 1 and 9 of the unlock sets (the gear tables of kinds 2, 4, 5, 6, 7, 8 are explained in section 8).
5. For the PSP rebuild the usable ground truth is complete: pools + chains + challenge table + gate/zone/start/rail placement + trick base points + text. A faithful career can be driven straight from `campaign_data.json` / `campaign_levels.json` (positions are already in our level space).

## 13. NIS scene scripts [V]
* **The NIS script table, decoded (arm9 0x020753C0; scene scripts 0x02071070..0x02074F00)** [V: all 57 scenes parse, record counts add up to the next script's address]: a table of 12-byte entries {u32 n, u32 ptr, u32 x}, entry k-1 = scene k (k = 1 .. 57; the entries after 57 are other scripts), each script n records of 16 bytes {u32 type, u32 times = (end << 16) | start, u32 param, u32 link}; end 0xFFFF = open ended. The clock counts NIS frames (scene 1 is 688 long, five lines of about 137 each: the end of a subtitle is the start of the next). The entry's x is the level id of the scene (9 MegaPark, 10 XGames, 14 Paris, 15 San Francisco, 16 South Bank, 17 Barcelona, 18 Rio, 19 Shanghai), -1 for 12 scenes (1, 2, 5, 19, 20, 29, 30, 40, 51, 53, 55, 57); the before / after pair of a sponsor event has the same x. tools/campaign_nis.py prints them.
* **What a story scene is** (decoded 2026-10-16 from the code of overlay 12 and by running that code in an ARM emulator, tools/nis_emu.py) [V]: all 57 scripts are ONE template, a close-up of ONE speaker. Record types used: 23 actor count (2); 22 spawn (param = actor slot 1, link = the skater: a career skater index, 19 = Reda); 21 visible (param 0: the human player, actor 0, is hidden - he is never on screen and never speaks); 25 / 24 the guest's talking pose on at the start / off at the end (0x02107348, the game's state 0x36; its link would be a yaw in degrees, always -1 = none); 27 camera target (actor 1: mode 0, yaw 90 degrees, set by 0x021076E2); 3 camera on actor 1 (style = link 0); 32 camera distance (0x2000 = 2.0 m in 53 scenes, 0x1666 = 1.4 m scene 16, 0x1800 = 1.5 m scenes 43 / 44, 0x8000 = 8 m scene 57); 19 talk span of actor 1 (link 1 or 2 = which kind of gesture clip, below); 0 the subtitle lines (param = the string number of TXT_NIS_nnn_mmm); and once each 29 (a delay, scene 57) and 31 (a camera parameter, scene 16). The executor's jump table (0x021074AE, 37 types) also holds 2 (play a level camera path), 4 (camera yaw), 18 (play a named clip), 20, 26, 28 and others that NO scene uses. Scenes 3 / 4 / 23 / 27 change the guest at frame 500 (a second spawn record). The level cameras `camera<N>_<idx>` are therefore NOT used by the scenes (an earlier reading of type 19 as a shot with a camera path was wrong: 19 is the talk span).
* **Where the guest stands and what the camera does** [V by emulation for challenges 21, 22, 62, 63]: the spawn (Player_Init 0x02102368, kind 5) and the talking pose (0x02106B50) put the guest on the start grid of the scene's challenge with his actor slot (1) as the index: the `chalNNN_start_pos_k` block of the challenge (0xB0 bytes at 0x02095D28 + 0xB0 * slot, filled by the level loader of overlay 13), index 1, wrapped to 0 when it has only one start point; the level's global start grid (start_pos_k, same wrap) when the challenge has none (accessors 0x0201C7EC / 0x0201C824). Challenge 21 (four start points) put him on start_pos_2 (-70.164, 11.612, 38.348), 22 / 62 / 63 (one) on start_pos_1. The heading of a start point is atan2(dir - pos) measured from +z towards +x in units of 1/65536 turn (the game's own routine 0x0210F21C); 136 of 141 recorded runs (races, contests) start by riding along exactly that direction, so start_dir is the way a rider faces. The guest does NOT take it: the talking pose forces the quaternion (w 0, x 0, y 1, z 0), a turn of 180 degrees about the up axis, the same in every scene. Camera (orbit, mode 0, overlay 14 0x02111270): look-at = guest + (0, 1.0, 0); camera = look-at + (d cos yaw, 1.0, d sin yaw) in (x, y, z) with d the distance record and yaw 90 degrees, i.e. d metres towards +z and 1 m up; checked for d = 1 .. 8 m and other yaws; it does not depend on the guest's rotation (only on his position and up vector). So the camera stands on the +z side of the guest and looks towards -z.
* **The guest's clip** [V for the lookup, the rest [?]]: the script names no clip. The talk span (19, link L) runs 0x02107444 -> 0x02015C54 -> 0x02013084, which asks the animation database (DATA/SKATERS/ANIMEXTRAS/animdb.adb: header {2, 22, 0}, then 22 records of 28 bytes {u32 id = the file number N of N.ABF, 0xCDCDCDCD, bytes type, sub, 0, 0, u32 offset into animdb.tre, u32 0x1E00 (0x11E00 for ids 11 .. 21 = the clips that face -y), 0xFFFF, 0}; ANIMEXTRAB has the same table for the board) for a clip of TYPE L: the matcher 0x0201471C takes a query of four halfwords {L, -1, -1, -1} (0xFFFF = any), scores 2 per equal byte, the best score wins and ties are drawn at random (among the first five kept). Type 1 = ids 0, 1, 2, 14 .. 21; type 2 = ids 7 .. 13; type 3 = ids 3 .. 6 (never asked by a scene). Link 1 asks type 1, link 2 type 2 (29 scenes each: the two speakers of a conversation use different sets). The talking pose itself (record 25) starts the controller's clip 1 (0x020152FC) and record 24 ends it. Clip rate 30 / s (0x1E in the records). ANIMEXTRAB clips exist only for ids 0, 1, 2 and 11 .. 21 (the others are empty). The clips are relative to the actor (root at the origin). NOT settled: (a) whether the clips of a type face the camera after the 180 degree turn (by the heading convention the guest's front looks to -z while the camera stands at +z, so either the extra clips are authored backwards or the shot is over his shoulder; one look at the real thing decides, a viewer should have a switch), (b) how many NIS frames per second (at 30 / s scene 1 lasts 23 s, one subtitle 4.6 s; at 60 / s 11.5 s), (c) what the 12 scenes with x = -1 are filmed in (no level is named), (d) how a clip shorter than the span (63 .. 143 frames against 500 .. 1000) is repeated (looping is assumed).
