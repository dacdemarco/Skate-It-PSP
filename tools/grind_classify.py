#!/usr/bin/env python3
"""Classify the grind-like clips of the pose library (animdb.adb groups 10-14) and say what the data can and cannot tell us.

usage (from the repo root):  python tools/grind_classify.py [--all]

What the data says (all numbers are computed here, nothing is hard-coded except the rule thresholds):
  * In ALL 376 clips the board origin is fixed at (0, 0, 0.0098): no clip lifts, offsets or drops the board, so the clip never says which truck
    is on a rail.  The board only YAWS (and pitches / rolls a few degrees); the contact with the rail is procedural in the original.
  * Clip frame: the riding clips (group 0) have the board nose along data +y (yaw 90) and a goofy skater (right foot at the nose).
    Every grind IN clip starts from a riding pose (yaw 90) and the straight OUT clip ends at yaw ~90 again, so the rail runs along data +y
    and "board yaw - 90" = how far the board is turned out of the rail line (+ = counter-clockwise seen from above).
  * adb fields: b high byte = 0 IN (ride -> pose), 1 HOLD (static, 2 frames), 2 OUT (pose -> ride; b low byte 3 / 4 = OUT with a 90 / 180
    degree turn of the board); a high/low bytes as described per group below.
Families found:
  group 12 (62-81)   board turned across the line (yaw 167 = +77 deg, or 33 = -57 deg): the SLIDE family, two turn senses x two heights
  group 13 (82-111)  board along the line, body shifted along the board: forward (b low 1) or backward (b low 0), lean a low 0 / 1 / 2
  group 14 (112-139) crouch / torso-twist poses with the board along the line; 131-135 = lying flat on the board (not a grind)
The BS / FS labels are derived from the turn sense for a GOOFY skater (frontside = chest leads = clockwise = yaw decreasing); that is an
inference from the rotation direction, not something the clip states.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, pose_sheet as ps

ARC = ps.ARC_S
ADB = open(ARC + "/animdb.adb", "rb").read()
TRE = open(ARC + "/animdb.tre", "rb").read()
REC = [struct.unpack_from("<IIHHIHHI", ADB, 8 + 28 * i) for i in range(376)]
SK = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
OFF = [tuple(c / 4096 for c in o) for o in mbf.load_bof(ARC + "/animdb.BOF")]
S = 8.6


def wrap(a): return (a + 180) % 360 - 180
def yaw(v): return np.degrees(np.arctan2(v[1], v[0]))


def board(n):
    out = []
    for q, p in ps.load_board_abf("%s/%d.abf" % (ps.ARC_B, n)):
        R = ps.inv(q)
        L, N = R[:, 2], R[:, 1]
        out.append(dict(R=R, yaw=yaw(L), pitch=np.degrees(np.arcsin(np.clip(L[2], -1, 1))), tilt=np.degrees(np.arccos(np.clip(N[2], -1, 1))), p=np.array(p)))
    return out


def skater(n, f):
    a = mbf.load_abf("%s/%d.abf" % (ARC, n)); f = min(f, a["nframes"] - 1)
    rp, qs, _ = a["frames"][f]
    P = mbf.pose(SK["nodes"], OFF, qs, tuple(c / 4096 for c in rp))
    return np.array([P[i][1] for i in range(22)]) * S, a["nframes"]


def tre_tokens(n):
    offs = [r[4] for r in REC] + [len(TRE)]
    s = TRE[offs[n]:offs[n + 1]]
    return " ".join("%d:%02x" % (i, c) for i, c in enumerate(s[:-1]) if c)


def frame_of_role(n, nb, nf_skater):
    bh = REC[n][3] >> 8
    return (nf_skater - 1 if bh == 0 else 0), bh      # IN: the pose is reached at the end; HOLD / OUT: it is the first frame


def metrics(n):
    bd = board(n)
    nfs = mbf.load_abf("%s/%d.abf" % (ARC, n))["nframes"]
    f, bh = frame_of_role(n, len(bd), nfs)
    fb = min(f, len(bd) - 1) if bh != 0 else len(bd) - 1       # the board file can have more frames than the skater file
    pts, _ = skater(n, f)
    b = bd[fb]
    bl, W, N, L = b["p"] * S, b["R"][:, 0], b["R"][:, 1], b["R"][:, 2]
    loc = lambda p: np.array([(p - bl) @ W, (p - bl) @ N, (p - bl) @ L])
    sh = pts[19] - pts[13]; face = np.cross([0, 0, 1], sh)
    hp = pts[5] - pts[1]; hface = np.cross([0, 0, 1], hp)
    return dict(role=("IN" if bh == 0 else "HOLD" if bh == 1 else "OUT"), byaw=b["yaw"], roll=b["tilt"], pitch=b["pitch"],
                turn=wrap(b["yaw"] - 90.0), face=wrap(yaw(face) - b["yaw"]), hip=wrap(yaw(hface) - b["yaw"]),
                rz=loc(pts[7])[2], lz=loc(pts[3])[2], pz=loc(pts[0])[2], py=loc(pts[0])[1], hx=loc(pts[18])[0],
                start_yaw=bd[0]["yaw"], end_yaw=bd[-1]["yaw"], nb=len(bd), nfs=nfs)


def classify(n, m):
    """-> (family, side, confidence, note)"""
    a, b = REC[n][2], REC[n][3]
    g = REC[n][6]
    if g == 12:
        hold_turn = m["turn"]
        side = "BS-sense (CCW)" if hold_turn > 0 else "FS-sense (CW)"
        return "SLIDE (board turned across the rail line)", side, "medium (family) / low (which slide)", "board %+.0f deg from the rail line" % hold_turn
    if g == 13:
        fwd = (b & 0xFF) == 1
        lean = {0: "neutral", 1: "lean 1", 2: "lean 2"}[a & 0xFF]
        if fwd:
            return "FRONT-TRUCK family (nosegrind-like: weight shifted to the nose)", "n/a (lean: %s)" % lean, "medium (family) / low (which grind)", "front foot z %.2f (front truck 0.24), pelvis z %.2f" % (m["rz"], m["pz"])
        return "BACK-TRUCK family (5-0-like: weight shifted to the tail)", "n/a (lean: %s)" % lean, "medium (family) / low (which grind)", "back foot z %.2f (back truck -0.24), pelvis z %.2f" % (m["lz"], m["pz"])
    if g == 14:
        if m["rz"] > 0.5:
            return "NOT A GRIND (lying flat on the board)", "-", "high", "both feet at the nose end, z %.2f" % m["rz"]
        return "unclassified crouch / torso-twist pose (board along the line)", "-", "low", "pelvis y %.2f, chest %+.0f deg from the board nose, head x %.2f" % (m["py"], m["face"], m["hx"])
    if g in (10, 11):
        if g == 11:
            return "NOT A GRIND (180 deg pivot of the board on the ground: revert-like, %s)" % ("CCW" if (a >> 8) < 2 else "CW"), "-", "medium", "board yaw %.0f -> %.0f" % (m["start_yaw"], m["end_yaw"])
        return "NOT A GRIND (ride pose, crouch <-> upright)", "-", "medium", ""
    return "other", "-", "-", ""


def other_group_summary():
    """Everything outside 56-139: is the board ever tilted / lifted like a grind? (it never is lifted)"""
    rows = []
    hmax = 0.0; offmax = 0.0
    for n in range(376):
        for q, p in ps.load_board_abf("%s/%d.abf" % (ps.ARC_B, n)):
            hmax = max(hmax, abs(p[2] - 0.009765625)); offmax = max(offmax, np.hypot(p[0], p[1]))
    return hmax, offmax


def main():
    allrows = "--all" in sys.argv
    hmax, offmax = other_group_summary()
    print("board origin over all 376 clips: max |height - 0.0098| = %.5f, max horizontal offset = %.5f model units  (board never moves)" % (hmax, offmax))
    print()
    print("%3s %4s %4s %2s %-4s %-5s | %7s %6s %6s %6s | %6s %6s %6s %5s | %s | %s | %s" % ("clip", "a", "b", "g", "role", "tre", "boardyaw", "turn", "roll", "pitch", "Rfoot", "Lfoot", "pelvZ", "pelvY", "family", "side", "confidence"))
    for n in range(56, 140):
        m = metrics(n)
        fam, side, conf, note = classify(n, m)
        print("%3d %04x %04x %2d %-4s %-5s | %7.1f %6.1f %6.1f %6.1f | %6.2f %6.2f %6.2f %5.2f | %s | %s | %s | %s" % (
            n, REC[n][2], REC[n][3], REC[n][6], m["role"], tre_tokens(n).replace(" ", ",")[:14], m["byaw"], m["turn"], m["roll"], m["pitch"], m["rz"], m["lz"], m["pz"], m["py"], fam, side, conf, note))
    print()
    print("Families (holds): 62,72 / 67,77 = slide, 2 turn senses x tall/low; 94,97,100 (front) / 103,106,109 (back) = shifted-weight truck grinds;")
    print("126-130, 136-139 = crouch/twist poses (unidentified); 131-135 = lying flat (not grinds); 56-61 = ride / ground pivots (not grinds).")
    if allrows:
        print()
        print("Other groups (boards pitched / turned = manual-like, flips, grabs, bails) are not grind poses; see clip_survey.py / flip_survey.py.")


if __name__ == "__main__":
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    main()
