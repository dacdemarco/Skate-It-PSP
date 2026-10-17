#!/usr/bin/env python3
"""Reference for tests/test_skel.c: compares the C skeleton output with the Python FK (tools/skate_check.py) on the real .skt.
usage: test_skel_ref.py <skater.skt> <c_output.txt>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, skate_check as sc

def mats(sk, root, q):
    G = sc.fk(sk["skel"], root, q); S = sk["scale"]
    out = []
    for R, t in G:
        M = np.zeros((3, 4)); M[:, :3] = S * (sc.A2 @ R); M[:, 3] = S * (sc.A2 @ t); out.append(M.flatten())
    return np.array(out)

def blend(a, b, t):
    root = a[0] + (b[0] - a[0]) * t; qs = []
    for qa, qb in zip(a[1], b[1]):
        s = -t if qa @ qb < 0 else t
        q = (1 - t) * qa + s * qb; qs.append(q / np.linalg.norm(q))
    return root, np.array(qs)

sk = sc.load_skt(sys.argv[1])
worst = 0.0
for line in open(sys.argv[2]):
    p = line.split()
    if p[0] == "pose":
        a, f = int(p[1]), int(p[2]); fr, bf = sk["anims"][a]
        ref = mats(sk, *fr[f]); got = np.array(p[3:], float).reshape(22, 12)
    elif p[0] == "board":
        a, f = int(p[1]), int(p[2]); fr, bf = sk["anims"][a]; bp, bq = bf[f if f < len(bf) else 0]
        R = sc.qmat_inv(bq); M = np.zeros((3, 4)); M[:, :3] = sk["scale"] * (sc.A2 @ R); M[:, 3] = sk["scale"] * (sc.A2 @ bp)
        ref = M.flatten(); got = np.array(p[3:], float)
    else:
        ref = mats(sk, *blend(sk["anims"][0][0][5], sk["anims"][3][0][5], 0.35)); got = np.array(p[4:], float).reshape(22, 12)
    err = np.abs(ref - got).max(); worst = max(worst, err)
    print("%-6s %s  max abs error %.2e" % (p[0], " ".join(p[1:3]), err))
print("WORST", worst, "OK" if worst < 1e-3 else "MISMATCH")
