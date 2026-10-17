#!/usr/bin/env python3
"""Classify the flip clips (315-362) by how the BOARD turns. Board local axes (from the board model): x = width, y = deck normal, z = long (nose-tail).
In the skater model space the vertical is z (the skater's model y is the board's long axis when riding), so:
  about_up   = cumulative rotation of the board about the skater's vertical (a shove-it spins the board flat: +-180 / +-360)
  about_long = cumulative rotation about the board's own long axis (a kickflip / heelflip turns the deck over: +-360)
Both are signed (degrees); the sign tells the two directions of a trick apart (kickflip / heelflip, BS / FS shuvit) - which is which depends on the stance, see docs/TECHNICAL.md.
usage: flip_survey.py [first last]"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, clip_survey as cs


def spins(n):
    fr = cs.board_frames(n)
    R = [np.array(mbf.qmat((-q[0], -q[1], -q[2], q[3]))) for q, p in fr]
    up = 0.0; lg = 0.0; wd = 0.0
    for i in range(1, len(R)):
        d = R[i] @ R[i - 1].T                                      # rotation between frames in the skater frame
        w = np.array([d[2, 1] - d[1, 2], d[0, 2] - d[2, 0], d[1, 0] - d[0, 1]]) / 2.0
        s = np.linalg.norm(w); c = (np.trace(d) - 1) / 2
        th = np.degrees(np.arctan2(s, c))
        axis = w / s if s > 1e-9 else np.zeros(3)
        up += th * axis[2]
        lg += th * float(np.dot(axis, R[i - 1][:, 2]))
        wd += th * float(np.dot(axis, R[i - 1][:, 0]))
    return len(fr), up, lg, wd


def label(up, lg):
    a, l = abs(up), abs(lg)
    def near(v, t): return abs(v - t) < 60
    if l < 60 and a < 60: return "small / pose"
    if near(l, 360) and a < 60: return "FLIP (deck over once)"
    if near(a, 180) and l < 60: return "SHUVIT 180"
    if near(a, 360) and l < 60: return "SHUVIT 360"
    if near(l, 360) and near(a, 180): return "VARIAL FLIP (flip + 180)"
    if near(l, 360) and near(a, 360): return "360 FLIP (flip + 360)"
    if l > 700 or a > 700: return "MULTI"
    return "combo (%.0f up, %.0f long)" % (up, lg)


if __name__ == "__main__":
    a0 = int(sys.argv[1]) if len(sys.argv) > 2 else 315
    a1 = int(sys.argv[2]) if len(sys.argv) > 2 else 362
    for n in range(a0, a1 + 1):
        nf, up, lg, wd = spins(n)
        print("%3d frames %2d  spin about the vertical %7.1f, about the long axis %7.1f, about the width %7.1f   %s" % (n, nf, up, lg, wd, label(up, lg)))
