#!/usr/bin/env python3
"""Convert YOUR OWN copy of Skate It (DS, USA) into the data folder of the PSP port.

    python tools/make_data.py "path/to/Skate It (USA) (En,Fr,De,Es,It,Nl).nds"
    python tools/make_data.py --list                 the steps
    python tools/make_data.py ROM --only levels      run one step (or several: --only levels --only skaters)
    python tools/make_data.py ROM --skip music       leave a step out
    python tools/make_data.py ROM --jobs 4           how many levels / skaters are converted at the same time (default: half the CPUs)

Run it from anywhere; it works in the repository root. Everything it writes is made from the ROM and is git-ignored:

    extracted/                      the ROM taken apart (header, ARM code, overlays, 2175 files, the unzipped levels)
    tools/campaign_*.json|tsv       the career tables decoded from the ARM code and the text database (read by convert_career.py)
    tests/vx_ref_*.txt              checksums of the decoded FMV frames (the VX decoder test compares against them)
    dist/SkateItPSP/data/           what the game loads: levels, skaters, board, career, front-end art, sounds, music, FMV

then `bash tools/build.sh` (inside WSL on Windows) builds EBOOT.PBP into dist/SkateItPSP/.

Python 3.9+, numpy, pillow, capstone (pip install -r requirements.txt). The ROM must be the USA release (game code CSVE);
the converters stop with an error if it is not.
"""
import argparse, hashlib, os, shutil, struct, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
DATA = os.path.join("dist", "SkateItPSP", "data")          # relative to ROOT (the converters use root-relative paths)
PY = sys.executable

# The ROM this port was made from: NDS header game code CSVE (USA), 32 MB cart image.
GAME_CODE = b"CSVE"
# Skate It's skater files: every rider with a model except Reda (the cameraman, not a rider).
NOT_RIDERS = {"reda"}
# The generic board: deck and wheel bitmaps (DATA/SKATERS/CUSTOM/Textures/Board) put on the board of every skater.
BOARD_DECK, BOARD_WHEELS = "board_generic_toon", "wheels_generic_wht"


def run(args, **kw):
    """Run a converter script from the repository root; stop the whole run when it fails."""
    cmd = [PY, "-X", "utf8"] + [str(a) for a in args]
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode != 0:
        sys.exit("FAILED (exit %d): %s" % (r.returncode, " ".join(cmd)))


def run_quiet(args):
    """Same, but the output is kept and shown only when it fails (for the parallel jobs)."""
    cmd = [PY, "-X", "utf8"] + [str(a) for a in args]
    r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    if r.returncode != 0:
        sys.exit("FAILED (exit %d): %s\n%s" % (r.returncode, " ".join(cmd), r.stdout))
    return r.stdout


def parallel(label, jobs, items, fn):
    done = 0
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for _ in ex.map(fn, items):
            done += 1
            print("  %s %d / %d" % (label, done, len(items)), flush=True)


def data_dir():
    os.makedirs(os.path.join(ROOT, DATA), exist_ok=True)
    return DATA


# ---- the steps ----------------------------------------------------------------------------------------------------------------

def step_extract(a):
    with open(a.rom, "rb") as f:
        head = f.read(0x20)
    if head[0x0C:0x10] != GAME_CODE:
        sys.exit("%s is not the USA release of Skate It (game code %r, expected %r)" % (a.rom, head[0x0C:0x10], GAME_CODE))
    run([os.path.join(TOOLS, "extract_nds.py"), os.path.abspath(a.rom), "extracted"])
    run([os.path.join(TOOLS, "unpack_layers.py"), "extracted"])
    run([os.path.join(TOOLS, "blz.py"), "extracted", os.path.join("extracted", "decomp")])


def step_arc(a):
    # the skaters' animation archives (anims.arc = gameplay clips, animb.arc = the board's) -> extracted/unpacked/DATA/SKATERS/ARC_S, ARC_B
    run([os.path.join(TOOLS, "arc.py")])


def step_campaign(a):
    run([os.path.join(TOOLS, "campaign_dump.py")])
    run([os.path.join(TOOLS, "campaign_text.py")])


def step_career(a):
    run([os.path.join(TOOLS, "convert_career.py"), os.path.join(data_dir(), "career.dat")])


def level_names():
    d = os.path.join(ROOT, "extracted", "unpacked", "DATA", "LOCATIONS")
    if not os.path.isdir(d):
        sys.exit("extracted/ is missing: run the 'extract' step first")
    return sorted(n for n in os.listdir(d) if os.path.isdir(os.path.join(d, n)) and n != "SKIES")


def step_levels(a):
    names = level_names()
    out = data_dir()
    print("  %d levels (the big ones take a few minutes each)" % len(names))
    parallel("level", a.jobs, names,
             lambda n: run_quiet([os.path.join(TOOLS, "convert_level.py"), n, os.path.join(out, n.lower() + ".lvl")]))


def rider_names():
    d = os.path.join(ROOT, "extracted", "files", "DATA", "SKATERS", "MODELS")
    names = sorted(f[len("skater_"):-len("_lod1.mbf.ZL")] for f in os.listdir(d) if f.startswith("skater_") and f.endswith("_lod1.mbf.ZL"))
    return [n for n in names if n not in NOT_RIDERS]


def step_skaters(a):
    names = rider_names()
    out = data_dir()
    parallel("skater", a.jobs, names,
             lambda n: run_quiet([os.path.join(TOOLS, "convert_skater.py"), n, "1", os.path.join(out, "skater_%s.skt" % n)]))
    for n in sorted(NOT_RIDERS):                      # Reda, the cameraman: not a rider, but the story scenes use him
        run_quiet([os.path.join(TOOLS, "convert_skater.py"), n, "1", os.path.join(out, "skater_%s.skt" % n)])


def step_scenes(a):
    run([os.path.join(TOOLS, "convert_scenes.py"), os.path.join(data_dir(), "scenes.pak")])


def step_board(a):
    run([os.path.join(TOOLS, "convert_board.py"), BOARD_DECK, BOARD_WHEELS, os.path.join(data_dir(), "board.brd")])


def step_ai(a):
    run([os.path.join(TOOLS, "convert_ai.py"), os.path.join(data_dir(), "ai.pak")])


def step_custom(a):
    run([os.path.join(TOOLS, "convert_custom.py"), os.path.join(data_dir(), "custom.pak")])


def step_ui(a):
    run([os.path.join(TOOLS, "convert_ui.py"), os.path.join(data_dir(), "ui.pak")])


def step_sound(a):
    sdat = os.path.join("extracted", "files", "DATA", "AUDIO", "sound_data.sdat")
    run([os.path.join(TOOLS, "convert_sounds.py"), sdat, os.path.join(data_dir(), "sounds.snd")])


def step_music(a):
    sdat = os.path.join("extracted", "files", "DATA", "AUDIO", "sound_data.sdat")
    run([os.path.join(TOOLS, "convert_music.py"), sdat, data_dir()])


def step_fmv(a):
    # the two movies are played by src/vx.c as they are on the cartridge: a copy is all they need
    for name in ("ealogo.vx", "tempintro.vx"):
        src = os.path.join(ROOT, "extracted", "files", "DATA", "FMV", name)
        if not os.path.isfile(src):
            sys.exit("%s is missing: run the 'extract' step first" % src)
        shutil.copyfile(src, os.path.join(ROOT, data_dir(), name))
        print("  " + name)


def step_testrefs(a):
    # checksums of every decoded movie frame, for tests/test_vx.c (about two minutes of pure-Python decoding)
    run([os.path.join(TOOLS, "vx_mkref.py"), "tests"])


STEPS = [
    ("extract",   step_extract,  "take the ROM apart: extracted/ (files, ARM code, overlays), unzip the level archives"),
    ("arc",       step_arc,      "unpack the skater animation archives (anims.arc / animb.arc)"),
    ("campaign",  step_campaign, "decode the career tables and the text database -> tools/campaign_*.json / .tsv"),
    ("career",    step_career,   "career.dat (challenges, pools, unlocks, story lines, opponents' recorded results)"),
    ("levels",    step_levels,   "the 21 locations -> *.lvl (slow: collision grid, grind rails, texture packing)"),
    ("skaters",   step_skaters,  "the 19 riders (+ Reda, the story's cameraman) -> skater_*.skt (model, texture, the whole animation set)"),
    ("board",     step_board,    "board.brd"),
    ("custom",    step_custom,   "custom.pak (parts of create-a-skater, deck / wheel art, the item numbers of the career's gear unlocks)"),
    ("ai",        step_ai,       "ai.pak (the original's recorded runs: opponents of races and contests, ambient skaters)"),
    ("scenes",    step_scenes,   "scenes.pak (the story scenes: the guests' conversation clips and the timing of the 57 NIS scripts)"),
    ("ui",        step_ui,       "ui.pak (menu art, HUD sprites, fonts)"),
    ("sound",     step_sound,    "sounds.snd (sound effects)"),
    ("music",     step_music,    "music0..3.mus (streamed tracks; the build ships with music switched off, see MUSIC_ENABLED in src/main.c)"),
    ("fmv",       step_fmv,      "ealogo.vx, tempintro.vx"),
    ("testrefs",  step_testrefs, "tests/vx_ref_*.txt (only the host tests need them)"),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", nargs="?", help="Skate It (USA) .nds")
    ap.add_argument("--list", action="store_true", help="list the steps and exit")
    ap.add_argument("--only", action="append", default=[], metavar="STEP", help="run only this step (repeatable)")
    ap.add_argument("--skip", action="append", default=[], metavar="STEP", help="leave this step out (repeatable)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2), help="parallel conversions (levels, skaters)")
    a = ap.parse_args()
    names = [s[0] for s in STEPS]
    if a.list:
        for n, _, d in STEPS:
            print("%-9s %s" % (n, d))
        return
    for n in a.only + a.skip:
        if n not in names:
            ap.error("unknown step %r (see --list)" % n)
    if not a.rom:
        ap.error("the path of your Skate It .nds is required")
    if not os.path.isfile(a.rom):
        ap.error("no such file: %s" % a.rom)
    todo = [s for s in STEPS if (not a.only or s[0] in a.only) and s[0] not in a.skip]
    t0 = time.time()
    for n, fn, d in todo:
        t = time.time()
        print("== %s: %s" % (n, d), flush=True)
        fn(a)
        print("   (%.0f s)" % (time.time() - t), flush=True)
    print("done in %.0f s. Next: bash tools/build.sh   (inside WSL on Windows)" % (time.time() - t0))


if __name__ == "__main__":
    main()
