#!/usr/bin/env python3
"""Survey of the 376 pose-library clips for air tricks: frames, how far the BOARD turns over the clip (angle and axis in the board's own frame) and how far it moves
relative to the skater. Flips are clips where the board spins a lot; grabs are static poses with the board held away from its riding place.
usage: clip_survey.py [first last]      (prints one line per clip)"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf

ARC_S, ARC_B = "extracted/unpacked/DATA/SKATERS/ARC_S", "extracted/unpacked/DATA/SKATERS/ARC_B"


def board_frames(n):
    b = open("%s/%d.abf" % (ARC_B, n), "rb").read()
    w = struct.unpack_from("<%dh" % (len(b) // 2), b, 0)
    out = []
    for f in range(w[2]):
        q = [x / 4096 for x in w[4 + 8 * f:8 + 8 * f]]
        p = [x / 4096 for x in w[9 + 8 * f:12 + 8 * f]]
        out.append((q, p))
    return out


def qmul(a, b):
    x1, y1, z1, w1 = a; x2, y2, z2, w2 = b
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def qinv(q):
    n = sum(c * c for c in q) or 1
    return (-q[0] / n, -q[1] / n, -q[2] / n, q[3] / n)


def rel_angle_axis(q0, q1):
    d = qmul(qinv(q0), q1)
    n = np.linalg.norm(d)
    d = tuple(c / n for c in d)
    if d[3] < 0:
        d = tuple(-c for c in d)
    ang = 2 * np.arccos(min(1.0, d[3]))
    s = np.sqrt(max(1e-12, 1 - d[3] * d[3]))
    ax = np.array(d[:3]) / s
    return ang, ax


def survey(n):
    fr = board_frames(n)
    nf = len(fr)
    # cumulative rotation of the board between consecutive frames (sum of the absolute steps) and the net rotation first -> last
    tot = 0.0
    maxang = 0.0
    for i in range(1, nf):
        a, _ = rel_angle_axis(fr[i - 1][0], fr[i][0])
        tot += a
        a0, _ = rel_angle_axis(fr[0][0], fr[i][0])
        maxang = max(maxang, a0)
    net, ax = rel_angle_axis(fr[0][0], fr[-1][0])
    p0 = np.array(fr[0][1]); pr = max(np.linalg.norm(np.array(f[1]) - p0) for f in fr)
    return nf, tot, maxang, net, ax, pr


if __name__ == "__main__":
    a = int(sys.argv[1]) if len(sys.argv) > 2 else 0
    b = int(sys.argv[2]) if len(sys.argv) > 2 else 375
    sa = mbf.load_abf("%s/%d.abf" % (ARC_S, 0))
    for n in range(a, b + 1):
        nf, tot, maxang, net, ax, pr = survey(n)
        print("%3d frames %2d  board turn: total %6.1f deg, max from start %6.1f, net %6.1f about (%5.2f %5.2f %5.2f), moves up to %.2f" % (n, nf, np.degrees(tot), np.degrees(maxang), np.degrees(net), *ax, pr))
