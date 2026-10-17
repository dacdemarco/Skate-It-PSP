#!/usr/bin/env python3
"""The story scenes for the PSP: the 22 conversation clips of DATA/SKATERS/ANIMEXTRAS (+ the board clips of ANIMEXTRAB) and the timing of the 57 NIS scripts -> data/scenes.pak.
usage: convert_scenes.py [out.pak]        default dist/SkateItPSP/data/scenes.pak
Needs the extract / campaign steps of make_data.py (extracted/, tools/campaign_*.json). What the scripts mean is in docs/CAMPAIGN_FORMAT.md section 13.

File (little endian):
  Header (64 B)  char magic[4]='SCN1'; u32 version=1, nclips=22, nscenes=57, off_clips, off_info, off_bones, off_scenes; pad
  Clip table     nclips * SkelAnim (16 B: u32 nframes, frame_off, board_nframes, board_off; offsets from the file start; board_nframes 0 = the clip has no board)
  Clip info      nclips * 8 B: u8 type, u8 sub (the animation database record: a talk span asks for a clip whose type = the span's link), u8 flags (bit 0 = has a board), u8 pad,
                 f32 face (degrees, the direction the knees bend in the data frame: atan2(y, x) of the first frame, i.e. the way the clip faces)
  Bones          22 * 3 f32 rest offsets in model units (ANIMEXTRAS/ANIMDB.BOF / 4096; the clips were made for these, they differ a little from the gameplay set)
  Scene table    nscenes * 12 B: s32 level (the level table id, -1 none), u32 offset, u32 size
  Scene          u32 distance (1.19.12 m), u32 length (frames: the 'talk pose off' record), u16 nspawn, ntalk, nsub, pad; spawn * {u16 frame, u8 skater, u8 pad};
                 talk * {u16 start, u16 end, u8 link, 3 pad}; sub * {u16 start, u16 end} (one per dialogue line, in order)
  Frames         per clip nframes * (3 f32 root + 22 * 4 f32 quaternion x y z w) = 364 B as in SKS2, then board_nframes * (3 f32 + 4 f32) = 28 B
"""
import struct, sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, convert_skater as cs, campaign_nis as nis

NCLIPS, NSCENES = 22, 57
HERE = os.path.dirname(os.path.abspath(__file__))
EXTRAS = "extracted/files/DATA/SKATERS/ANIMEXTRAS"
EXTRAB = "extracted/files/DATA/SKATERS/ANIMEXTRAB"


def face_degrees(m, off, clip):
    """the way the clip faces: the mean over its frames of the direction the knees bend in (tools/skel_verify.py)"""
    hs = []
    for rp, qs, _ in clip["frames"]:
        P = mbf.pose(m["nodes"], [tuple(o) for o in off], qs, tuple(c / 4096 for c in rp))
        v = np.zeros(2)
        for hip, knee, ank in ((1, 2, 3), (5, 6, 7)):
            k = np.array(P[knee][1]) - (np.array(P[hip][1]) + np.array(P[ank][1])) / 2
            v += k[:2]
        hs.append(math.atan2(v[1], v[0]))
    return math.degrees(np.angle(np.exp(1j * np.array(hs)).mean()))


def adb(path):
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 4)[0]
    return [struct.unpack_from("<7I", b, 8 + 28 * i) for i in range(n)]


def scene_blob(recs):
    spawns, talks, subs, dist, length = [], [], [], 0x2000, 0
    for t, tm, par, lk in recs:
        start, end = tm & 0xFFFF, tm >> 16
        if t == 22: spawns.append((start, lk))
        elif t in (19, 20): talks.append((start, end, lk))
        elif t == 0: subs.append((start, end))
        elif t == 32: dist = par
        elif t == 24: length = start
    ends = [e for _, e in subs if e != 0xFFFF] + [e for _, e, _ in talks if e != 0xFFFF]
    if not length: length = max(ends or [600])
    b = struct.pack("<IIHHHH", dist, length, len(spawns), len(talks), len(subs), 0)
    for f, sk in spawns: b += struct.pack("<HBB", f, sk, 0)
    for s, e, lk in talks: b += struct.pack("<HHBBBB", s, e, lk, 0, 0, 0)
    for s, e in subs: b += struct.pack("<HH", s, e)
    return b


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "dist/SkateItPSP/data/scenes.pak"
    m = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    boff = mbf.load_bof(EXTRAS + "/ANIMDB.BOF")
    rec = adb(EXTRAS + "/animdb.adb")
    assert len(rec) == NCLIPS and len(boff) == cs.NB
    clips = []
    for n in range(NCLIPS):
        a = mbf.load_abf("%s/%d.ABF" % (EXTRAS, n))
        bf = cs.load_board_abf("%s/%d.ABF" % (EXTRAB, n))
        clips.append((a, bf, face_degrees(m, [np.array(o, float) / 4096 for o in boff], a), rec[n]))
    off_clips = 64
    off_info = off_clips + NCLIPS * 16
    off_bones = off_info + NCLIPS * 8
    off_scenes = off_bones + cs.NB * 12
    scenes = nis.scenes()
    assert len(scenes) == NSCENES
    blobs = [scene_blob(r) for _, _, r in scenes]
    pos = off_scenes + NSCENES * 12
    stab = bytearray()
    for (k, x, _), b in zip(scenes, blobs):
        stab += struct.pack("<iII", -1 if x is None else x, pos, len(b))
        pos += len(b)
    pos = (pos + 15) & ~15
    table, info, data = bytearray(), bytearray(), bytearray()
    for a, bf, face, r in clips:
        sk_off = pos + len(data)
        for rp, qs, _ in a["frames"]:
            data += struct.pack("<3f", *(c / 4096 for c in rp))
            for q in qs: data += struct.pack("<4f", *q)
        bd_off = pos + len(data)
        for bq, bp in bf: data += struct.pack("<3f", *bp) + struct.pack("<4f", *bq)
        table += struct.pack("<4I", a["nframes"], sk_off, len(bf), bd_off)
        info += struct.pack("<BBBBf", r[2] & 0xFF, (r[2] >> 8) & 0xFF, 1 if bf else 0, 0, face)
    head = struct.pack("<4sIIIIIII", b"SCN1", 1, NCLIPS, NSCENES, off_clips, off_info, off_bones, off_scenes).ljust(64, b"\0")
    bones = b"".join(struct.pack("<3f", *(c / 4096 for c in o)) for o in boff)
    blob = head + bytes(table) + bytes(info) + bones + bytes(stab) + b"".join(blobs)
    blob = blob.ljust(pos, b"\0") + bytes(data)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(blob)
    print("%s: %d clips (%d with a board), %d scenes, %.2f MB" % (out, NCLIPS, sum(1 for c in clips if c[1]), NSCENES, len(blob) / 1048576.0))
    print("faces:", " ".join("%d:%.0f" % (n, c[2]) for n, c in enumerate(clips)))


if __name__ == "__main__":
    main()
