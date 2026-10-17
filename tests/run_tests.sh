#!/bin/bash
# Host tests for the pure-C modules (no PSP SDK needed). Run from PowerShell:
#   wsl -d Ubuntu -- bash /mnt/<drive>/path/to/repo/tests/run_tests.sh
# Needs the data made from your own cartridge: python tools/make_data.py "<Skate It .nds>"  (dist/SkateItPSP/data and tests/vx_ref_*.txt).
# The skeleton cross-check at the end (tools/test_skel_ref.py) needs numpy.
set -e
cd "$(dirname "$0")/.."
D=dist/SkateItPSP/data
CF="-O1 -g -Wall -Wextra -Wshadow -fsanitize=undefined,address"
export ASAN_OPTIONS=detect_leaks=0

echo "== skate physics";   gcc $CF -o /tmp/t_skate tests/test_skate.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_skate
echo "== controls"; gcc $CF -o /tmp/t_control tests/test_control.c src/control.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_control
echo "== moves (spins, reverts, powerslides, body flips, wallrides)"; gcc $CF -o /tmp/t_moves tests/test_moves.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_moves
echo "== level markers (start grid, bounds, challenge beams, rail ids)"; gcc $CF -o /tmp/t_levelobj tests/test_levelobj.c src/levelobj.c -lm && /tmp/t_levelobj $D/amphitheatre.lvl
echo "== goals (career data, trick ids, challenge rules on real levels)"; gcc $CF -o /tmp/t_goals tests/test_goals.c src/goals.c src/race.c src/ghost.c src/contest.c src/career.c src/levelobj.c src/score.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_goals $D/career.dat $D/amphitheatre.lvl $D/elementaryschool.lvl $D/paris.lvl $D/commcentre.lvl
echo "== score contests (Jam / Best Trick / S.K.A.T.E. against the recorded opponents)"; gcc $CF -o /tmp/t_contest tests/test_contest.c src/contest.c src/goals.c src/race.c src/ghost.c src/career.c src/levelobj.c src/score.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_contest $D/career.dat $D
echo "== races (the 17 Checkpoint / Deathrace challenges on their real levels, the opponents' recorded times)"; gcc $CF -o /tmp/t_race tests/test_race.c src/race.c src/ghost.c src/trick.c src/career.c src/levelobj.c -lm && /tmp/t_race $D/career.dat $D
echo "== recorded runs played back (457 recordings, poses, the contest opponents, the ambient skaters of all 21 levels)"; gcc $CF -o /tmp/t_stage tests/test_stage.c src/stage.c src/ghost.c src/contest.c src/goals.c src/race.c src/career.c src/levelobj.c src/score.c src/skate.c src/coll.c src/rails.c src/trick.c src/rider.c src/skel.c -lm && { /tmp/t_stage $D/career.dat $D > /tmp/stage.txt; r=$?; grep -v "^[0-9]* " /tmp/stage.txt; [ $r -eq 0 ]; }
echo "== career progression and profile file (the ROM's pools and unlock sets, 300 headless campaigns)"; gcc $CF -o /tmp/t_prog tests/test_progress.c src/progress.c src/career.c -lm && /tmp/t_prog $D/career.dat
echo "== story (the NIS scenes, sponsors, the order the career shows them in, old profile files)"; gcc $CF -o /tmp/t_story tests/test_story.c src/story.c src/progress.c src/career.c -lm && /tmp/t_story $D/career.dat
echo "== story scene player (57 scripts, the guests' places, clips facing the camera, cross-fades)"; gcc $CF -o /tmp/t_scene tests/test_scene.c src/scene.c src/rider.c src/skel.c src/career.c src/levelobj.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_scene $D/scenes.pak $D/career.dat $D
echo "== custom skater (parts of create-a-skater assembled into a .skt: 540 choices checked against the loader's rules)"; gcc $CF -o /tmp/t_custom tests/test_custom.c src/custom.c -lm && /tmp/t_custom $D/custom.pak $D/skater_aaron.skt $D/board.brd
echo "== statistics and the trick book (play time, distances, bests, banked tricks, profile versions 1 - 3)"; gcc $CF -o /tmp/t_stats tests/test_stats.c src/stats.c src/progress.c src/career.c -lm && /tmp/t_stats $D/career.dat
echo "== tutorials (list, tips for every objective, button names filled in)"; gcc $CF -o /tmp/t_tut tests/test_tutorial.c src/tutorial.c src/career.c src/settings.c -lm && /tmp/t_tut $D/career.dat
echo "== settings (control presets, button swaps, file)"; gcc $CF -o /tmp/t_settings tests/test_settings.c src/settings.c && /tmp/t_settings
echo "== front end (menus, flow, every screen rendered by the software UI)"; mkdir -p /tmp/fe_out; gcc $CF -o /tmp/t_fe tests/fe_preview.c src/frontend.c src/storyui.c src/story.c src/logbook.c src/stats.c src/custom.c src/trick.c src/options.c src/settings.c src/hud.c src/ui.c src/career.c src/tutorial.c src/progress.c -lm && /tmp/t_fe $D/ui.pak $D/career.dat /tmp/fe_out $D/custom.pak
echo "== level survey (every career place on all 21 levels: ground under it, no NaN, nobody boxed in; falls and low surfaces are reported)"; gcc -O2 -Wall -Wextra -Wshadow -o /tmp/t_survey tests/level_survey.c src/coll.c src/skate.c src/rails.c src/trick.c src/levelobj.c src/career.c -lm && { /tmp/t_survey $D/career.dat $D/*.lvl > /tmp/survey.txt; r=$?; grep -v "^  " /tmp/survey.txt; [ $r -eq 0 ]; }
echo "== rails and grinding"; gcc $CF -o /tmp/t_rails tests/test_rails.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_rails $D/amphitheatre.lvl
echo "== scoring"; gcc $CF -o /tmp/t_score tests/test_score.c src/score.c src/trick.c -lm && /tmp/t_score
echo "== manuals"; gcc $CF -o /tmp/t_manual tests/test_manual.c src/skate.c src/coll.c src/rails.c src/trick.c src/score.c -lm && /tmp/t_manual
echo "== air tricks"; gcc $CF -o /tmp/t_air tests/test_air.c src/skate.c src/coll.c src/rails.c src/trick.c src/score.c -lm && /tmp/t_air
echo "== sound"; gcc $CF -o /tmp/t_sound tests/test_sound.c src/mixer.c src/sfx.c src/sparks.c src/trick.c -lm && /tmp/t_sound $D/sounds.snd $D/music0.mus
echo "== VX movies (all frames of ealogo+tempintro vs the Python reference, fuzz, speed)"; gcc $CF -o /tmp/t_vx tests/test_vx.c src/vx.c && VX_NOSPEED=1 /tmp/t_vx $D/ealogo.vx $D/tempintro.vx tests && gcc -O2 -o /tmp/t_vx_fast tests/test_vx.c src/vx.c && VX_FUZZ_N=3 /tmp/t_vx_fast $D/ealogo.vx $D/tempintro.vx tests | grep speed
echo "== rider poses";     gcc $CF -o /tmp/t_rider tests/test_rider.c src/rider.c src/skel.c src/skate.c src/coll.c src/rails.c src/trick.c -lm && /tmp/t_rider $D/skater_aaron.skt
echo "== collision grid";  gcc $CF -O2 -o /tmp/t_coll tests/test_coll.c src/coll.c -lm && /tmp/t_coll $D/amphitheatre.lvl
echo "== near-camera clipping"; gcc $CF -O2 -o /tmp/t_nc tests/test_nearclip.c src/nearclip.c -lm && /tmp/t_nc $D/amphitheatre.lvl
echo "== level simulation"; gcc $CF -O2 -o /tmp/t_sim tests/sim_level.c src/coll.c src/skate.c src/rails.c src/trick.c -lm && /tmp/t_sim $D/amphitheatre.lvl | tail -1
echo "== fuzz: ollies / grinds / flips around the bowl staircase (nobody may fall out of the level or end up inside a curb)"
gcc $CF -O2 -o /tmp/t_fuzz tests/fuzz_inside.c src/coll.c src/skate.c src/rails.c src/trick.c -lm
if STEP=1.5 /tmp/t_fuzz $D/amphitheatre.lvl -70 -40 28 62 -63 > /tmp/t_fuzz.txt; then head -1 /tmp/t_fuzz.txt; else head -12 /tmp/t_fuzz.txt; echo "FUZZ FAILED"; exit 1; fi
echo "== skeleton C output (compare on Windows: python tools/test_skel_ref.py $D/skater_aaron.skt tests/skel_out.txt)"
gcc $CF -o /tmp/t_skel tests/test_skel.c src/skel.c -lm 2>/dev/null && /tmp/t_skel $D/skater_aaron.skt > tests/skel_out.txt && echo written tests/skel_out.txt
echo "ALL HOST TESTS DONE"
