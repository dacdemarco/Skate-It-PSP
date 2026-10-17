#!/usr/bin/env python3
"""DATA/AI/<LEVEL>/<chal>_<mode>_<a>_<b>.dat (the original's recorded opponent runs, tools/campaign_air.py) -> data/ai.pak, the tracks src/ghost.c plays back.

    convert_ai.py [out.pak]        default dist/SkateItPSP/data/ai.pak

File (little endian):
    u32 'AIP1', u32 count, then count entries of 44 bytes:
        char level[24]   lower case level stem ("amphitheatre": the folder name of the recording)
        u16 chal         the challenge number of the file name (0 for the free-skate ambient routes)
        u8 mode, a, b    game mode id, round / trick set, opponent (ambient: 0, 0, route number)
        u8 pad[3]
        u32 nsamples     points of the track (the first fix + one per 07 record: 4 per second)
        u32 nevents
        u32 offset       from the start of the file
    at the offset: nsamples x { f32 x, y, z } (metres, the level's frame), nsamples flag bytes (bit 0 = a jump: the recording was set to a new place there - a respawn after a bail),
    padded to 4, then nevents x 8 bytes { u16 frame (60 per second), u8 kind, u8 arg[4], u8 pad } (kinds: tools/campaign_air.py).
The position of point i is where the opponent was i / 4 seconds after the start of the recording."""
import glob, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign_air as A

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
AI_DIR = os.path.join(ROOT, "extracted", "files", "DATA", "AI")
JUMP = 1.5            # metres: a position fix this far from where the deltas had led is a jump (a respawn); closer ones just correct the drift
NAME = re.compile(r"^(\d+)_(\d+)_(\d+)_(\d+)\.dat$")


def track(path):
    """([(x, y, z)], [flags], [(frame, kind, args)]) of a recording"""
    r, p, recs = A.parse_records(path)
    import struct as st
    pos, pts, flags, pending_jump = None, [], [], False
    for t, i in recs:
        if t == 1:
            new = [v / 4096.0 for v in st.unpack_from("<3i", p, i + 1)]
            if pos is None:
                pos = new
                pts.append(tuple(pos)); flags.append(0)
            else:
                d = max(abs(new[k] - pos[k]) for k in range(3))
                if d > JUMP:
                    pending_jump = True
                pos = new
        elif t == 7 and pos is not None:
            d = [v / 4096.0 for v in st.unpack_from("<3h", p, i + 1)]
            pos = [pos[k] + d[k] for k in range(3)]
            pts.append(tuple(pos)); flags.append(1 if pending_jump else 0)
            pending_jump = False
    evs = [(fr, kind, a) for fr, kind, a in A.events(path)]
    return pts, flags, evs


def main(out):
    entries = []
    for f in sorted(glob.glob(os.path.join(AI_DIR, "*", "*.dat"))):
        m = NAME.match(os.path.basename(f))
        if not m:
            continue
        chal, mode, a, b = (int(v) for v in m.groups())
        level = os.path.basename(os.path.dirname(f)).lower()
        pts, flags, evs = track(f)
        if len(pts) < 2:
            continue
        entries.append((level, chal, mode, a, b, pts, flags, evs))
    head = struct.pack("<4sI", b"AIP1", len(entries))
    table_size = len(head) + 44 * len(entries)
    body, table = bytearray(), bytearray()
    for level, chal, mode, a, b, pts, flags, evs in entries:
        off = table_size + len(body)
        blob = b"".join(struct.pack("<3f", *p) for p in pts) + bytes(flags)
        blob += b"\0" * (-len(blob) % 4)
        for fr, kind, args in evs:
            blob += struct.pack("<HB4sB", fr, kind, bytes(args), 0)
        body += blob
        table += struct.pack("<24sHBBB3xIII", level.encode("ascii"), chal, mode, a, b, len(pts), len(evs), off)
    with open(out, "wb") as fh:
        fh.write(head + bytes(table) + bytes(body))
    jumps = sum(sum(e[6]) for e in entries)
    print("%s: %d recordings, %d points, %d events, %d jumps, %.0f KB" % (out, len(entries), sum(len(e[5]) for e in entries), sum(len(e[7]) for e in entries), jumps, (table_size + len(body)) / 1024.0))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "dist", "SkateItPSP", "data", "ai.pak"))
