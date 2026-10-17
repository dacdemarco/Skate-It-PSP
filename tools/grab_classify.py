#!/usr/bin/env python3
"""Grab classifier for the pose library (report: grab_report.md in the session scratchpad).

For every clip/frame: skin the skater mesh, express it in the BOARD's local frame (x width, +x = toe side, y deck normal, z long, +z = nose; metres) and measure
  * where each hand (mesh cluster of node 15 = left, 21 = right; left hand = BACK hand, right hand = FRONT hand of the goofy skater 'aaron')
    touches the board: nearest board point, region along the board, edge (toe side +x / heel side -x / top / underside),
  * whether each foot (clusters of nodes 3,4 / 7,8) is on the deck.
Then it prints the table: clip, group, a, b, per-hand contact, and a name with confidence.
usage: grab_classify.py [first last]  (default: every clip with a hand within 0.12 m of the board)"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, clip_survey as cs

S = 8.6
SK = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
OFF = [tuple(c / 4096 for c in o) for o in mbf.load_bof(cs.ARC_S + "/animdb.BOF")]
BD = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
_bv = S * np.array([v[0] for sec in BD["sections"] for t in sec[1] for v in t], float)
BOX_LO, BOX_HI = _bv.min(axis=0), _bv.max(axis=0)
BOARD_PTS = np.unique(np.round(_bv, 4), axis=0)

# skater mesh vertices grouped by node
VN = np.array([v[3] for t in SK["tris"] for v in t])
VL = np.array([v[0] for t in SK["tris"] for v in t], float)
# node ids per limb cluster
HAND = {"L": 15, "R": 21}
FOOT = {"L": (3, 4), "R": (7, 8)}

ADB = open(cs.ARC_S + "/animdb.adb", "rb").read()
def adb(n):
    t = struct.unpack_from("<IIHHIHHI", ADB, 8 + 28 * n)
    return dict(a=t[2], b=t[3], group=t[6], tre=t[4])

_cache = {}
def abf(n):
    if n not in _cache:
        _cache[n] = (mbf.load_abf("%s/%d.abf" % (cs.ARC_S, n)), cs.board_frames(n))
    return _cache[n]

def skin(n, f):
    """-> (world positions of all mesh verts in board-local metres, nodes positions in board local, nframes)"""
    a, bf = abf(n)
    f = min(f, a["nframes"] - 1)
    rp, qs, _ = a["frames"][f]
    P = mbf.pose(SK["nodes"], OFF, qs, tuple(c / 4096 for c in rp))
    bq, bp = bf[min(f, len(bf) - 1)]
    R = np.array(mbf.qmat((-bq[0], -bq[1], -bq[2], bq[3])))
    W = np.empty_like(VL)
    for node in set(VN.tolist()):
        m = VN == node
        W[m] = VL[m] @ np.array(P[node][0]).T + np.array(P[node][1])
    loc = S * ((W - np.array(bp)) @ R)                   # row-vector form of R.T @ (w - bp)
    nodes = {i: S * (R.T @ (np.array(P[i][1]) - np.array(bp))) for i in range(22)}
    return loc, nodes, a["nframes"]

def box_dist_pts(pts):
    d = np.maximum(np.maximum(BOX_LO - pts, pts - BOX_HI), 0)
    return np.linalg.norm(d, axis=1)

def hand_info(loc, nodes, side):
    m = VN == HAND[side]
    pts = loc[m]
    el = nodes[14 if side == "L" else 20]
    tip = pts[np.argsort(-np.linalg.norm(pts - el, axis=1))[:25]].mean(axis=0)   # fingertip / fist end of the hand cluster
    d = box_dist_pts(pts)
    k = int(np.argmin(d))
    return dict(tip=tip, dist=float(d[k]), contact=pts[k])

def foot_info(loc, side):
    m = np.isin(VN, FOOT[side])
    d = box_dist_pts(loc[m])
    return float(d.min()), loc[m].mean(axis=0)



# ---------------------------------------------------------------------------------------------------------------------------
# classification
ROLE = {0x100: "enter", 0x000: "hold", 0x200: "exit", 0x101: "enter(flat)", 0x001: "hold(flat)", 0x201: "exit(flat)"}


def ext(nodes, h, k, a):
    """leg straightness: hip-ankle distance / (thigh + shin); 1.0 = fully straight"""
    return np.linalg.norm(nodes[h] - nodes[a]) / (np.linalg.norm(nodes[h] - nodes[k]) + np.linalg.norm(nodes[k] - nodes[a]))


def yaw_deg(v):
    return float(np.degrees(np.arctan2(v[0], v[2])))


def board_pitch_roll(n, f):
    a, bf = abf(n)
    bq, bp = bf[min(f, len(bf) - 1)]
    R = np.array(mbf.qmat((-bq[0], -bq[1], -bq[2], bq[3])))
    return float(np.degrees(np.arcsin(R[2, 2]))), float(np.degrees(np.arcsin(R[2, 0])))   # nose elevation, toe-side elevation (world)


def region(c, ankle_b, ankle_f):
    """where on the board a contact point is: edge (x: +toe side / -heel side) and position along the board (z: +nose)"""
    x, y, z = c
    edge = "toe edge" if x > 0.04 else ("heel edge" if x < -0.04 else "deck centre")
    if z > 0.40 or (z > 0.26 and z > ankle_f + 0.02): along = "nose"
    elif z < -0.36: along = "tail"
    elif ankle_b - 0.04 <= z <= ankle_f + 0.04: along = "between the feet"
    elif z < ankle_b: along = "behind the back foot"
    else: along = "in front of the front foot"
    return edge, along


def analyse(n):
    t = adb(n)
    nf = len(abf(n)[0]["frames"])
    best = None
    for f in range(nf):            # frame with the hand closest to the board (the last one on ties: the target pose)
        loc, nodes, _ = skin(n, f)
        hl, hr = hand_info(loc, nodes, "L"), hand_info(loc, nodes, "R")
        d = min(hl["dist"], hr["dist"])
        if best is None or d <= best[0] + 1e-9: best = (d, f, loc, nodes, hl, hr)
    d, f, loc, nodes, hl, hr = best
    r = dict(n=n, group=t["group"], a=t["a"], b=t["b"], frames=nf, f=f, hl=hl, hr=hr, nodes=nodes,
             backext=ext(nodes, 1, 2, 3), frontext=ext(nodes, 5, 6, 7), pelvis_y=float(nodes[0][1]),
             hipyaw=yaw_deg(nodes[5] - nodes[1]), ab=float(nodes[3][2]), af=float(nodes[7][2]))
    r["nose"], r["toe"] = board_pitch_roll(n, f)
    r["held"] = [(s, hh) for s, hh in (("back/L", hl), ("front/R", hr)) if hh["dist"] < 0.10]
    return r


def hold_desc(r):
    if not r["held"]: return "no hand on the board"
    out = []
    for s, hh in r["held"]:
        e, al = region(hh["contact"], r["ab"], r["af"])
        out.append("%s hand: %s, %s" % (s, e, al))
    return "; ".join(out)


def base_name(r):
    """name of the grab from which hand touches which part of the board"""
    h = {s: region(hh["contact"], r["ab"], r["af"]) for s, hh in r["held"]}
    if len(h) == 2:
        return "Double Grab", "high" if r["group"] == 21 else "medium"
    if not h: return None, "-"
    (s, (e, al)), = h.items()
    if s == "front/R":
        if al == "nose": return "Nose Grab", "high"
        if al == "between the feet" and e == "heel edge": return "Melon", "high"
        if al == "between the feet" and e == "toe edge": return "Mute", "high"
    else:
        if al == "tail": return "Tail Grab", "high"
        if al == "between the feet" and e == "toe edge": return "Indy-type (book: 'BS Grab' / 'FS Grab')", "medium"
        if al == "between the feet" and e == "heel edge": return "Stale", "high"
    return None, "-"


def classify(R):
    """R = {clip: analyse(clip)} -> {clip: (name, confidence, evidence, role)}"""
    out = {}
    plain = {}                      # group -> the a=0 b=0x100 clip of that group
    for n, r in R.items():
        if r["a"] == 0 and r["b"] == 0x100 and 15 <= r["group"] <= 21: plain[r["group"]] = r
    for n, r in R.items():
        g, a, b = r["group"], r["a"], r["b"]
        role = ROLE.get(b, hex(b))
        ev = ""
        name, conf = None, "-"
        if 15 <= g <= 21 and a == 5:
            if b in (0x100, 0x000):
                name, conf, ev = "carry the board in one hand (NOT a grab)", "high", "board hangs from one hand, both feet 1+ m away from it"
            else:
                name, conf, ev = "put the board down / step back on from the one-hand carry (NOT a grab)", "medium", "hand still on the board, feet back on it, board level"
        elif g == 14 and a >= 0x400 and (b & 0xff) == 1 and r["frontext"] > 0.95:
            name, conf = "Coffin", "high" if a == 0x400 else "medium"
            ev = "skater lies flat above the board (legs straight %.2f/%.2f, pelvis %.2f m), both hands on opposite edges" % (r["backext"], r["frontext"], r["pelvis_y"])
            if a != 0x400: ev += "; a=%x = hand/head variant of the coffin (no hold/exit clip)" % a
        elif g == 14 and a >= 0x400:
            name, conf = ("Double Grab, low (both hands)" if len(r["held"]) == 2 else "exit from a double grab / coffin"), "medium"
            ev = "both hands on the board, board level"
        elif g == 14:
            name = "basic low grab, %s hand (%s)" % (("front/R" if a < 0x100 else "back/L"), "Melon-like" if a < 0x100 else "Indy-like")
            conf = "low"
            ev = "board stays level under the feet (pitch %.0f, roll %.0f); sub-variant a=%x unnamed" % (r["nose"], r["toe"], a)
        elif 15 <= g <= 21:
            bn, bc = base_name(r)
            p = plain.get(g)
            if a == 0:
                name, conf = (bn, bc) if bn else ("unknown", "-")
                ev = "hand/board geometry only"
            elif a in (1, 2, 3) and g in (15, 16, 17, 18) and p:
                nm = {1: "Nosebone", 2: "Tailbone", 3: "Tuck Knee"}[a]
                if a == 1:
                    ok = r["frontext"] > p["frontext"] + 0.04 and r["nose"] < p["nose"] - 8
                    ev = "front leg straighter than the plain grab (%.2f vs %.2f), board nose pitched down (%.0f vs %.0f deg)" % (r["frontext"], p["frontext"], r["nose"], p["nose"])
                if a == 2:
                    ok = r["backext"] > p["backext"] + 0.04 and r["nose"] > p["nose"] + 8
                    ev = "back leg straighter than the plain grab (%.2f vs %.2f), board nose pitched up (%.0f vs %.0f deg)" % (r["backext"], p["backext"], r["nose"], p["nose"])
                if a == 3:
                    ok = r["frontext"] < p["frontext"] - 0.1
                    ev = "front knee tucked: front leg %.2f vs %.2f in the plain grab" % (r["frontext"], p["frontext"])
                pre = {15: "Melon-type ", 16: "", 17: "Stale ", 18: "Mute "}[g]
                name = pre + nm
                conf = "medium" if (ok and g != 15) else "low"
                if g == 16: name += " (book: unprefixed)"
                if g == 15: name += " (book has no 'Melon X')"
                ev += "; a=1/2/3 = book order Nosebone/Tailbone/Tuck Knee"
            elif a in (1, 2) and g in (19, 20) and p:
                kind = "Nose" if g == 19 else "Tail"
                name = "%s Shifty %s Grab" % ("BS" if a == 1 else "FS", kind)
                conf = "medium (pair) / low (which is BS)"
                ev = "hips twisted %+.0f deg vs the board (plain grab %+.0f); a=1 and a=2 twist opposite ways in both groups -> the BS/FS pair; BS=a1 / FS=a2 only from book order" % (r["hipyaw"], p["hipyaw"])
            elif g == 19 and a == 3:
                name, conf = "Rocket Air (or Stiffy)", "low"
                ev = "both hands on the nose, both feet at the tail, legs straight (%.2f/%.2f), body stretched along the board, nose %+.0f deg" % (r["backext"], r["frontext"], r["nose"])
            elif g in (15, 16) and a in (4, 0x104):
                name, conf = "unknown (one of Method / Christ Air / Cross Bone / No-foot Air / Stiffy / BS Tweak)", "-"
                ev = "board rolled %+.0f deg (toe-side elevation), pitch %+.0f, legs %.2f/%.2f, pelvis %.2f m above deck; %s" % (r["toe"], r["nose"], r["backext"], r["frontext"], r["pelvis_y"], "Melon-based" if g == 15 else "Indy-based")
            else:
                name, conf = "unknown", "-"
            if b in (0x000, 0x200) and name != "unknown":
                ev += "; this clip = %s pose of the same grab" % role
        out[n] = (name, conf, ev, role)
    return out


GROUPS = {14: "basic low grabs (board level) + coffin", 15: "front/R hand, heel edge, between feet = MELON family", 16: "back/L hand, toe edge, between feet = INDY family",
          17: "back/L hand, heel edge = STALE family", 18: "front/R hand, toe edge = MUTE family", 19: "front/R hand on the nose = NOSE GRAB family",
          20: "back/L hand on the tail = TAIL GRAB family", 21: "both hands = DOUBLE GRAB"}


def main():
    np.set_printoptions(precision=2, suppress=True)
    R = {n: analyse(n) for n in range(112, 200)}
    C = classify(R)
    print("clip grp a     b     role         board nose/toe  legs B/F   hands on board  ->  name [confidence]")
    last = None
    for n in sorted(R):
        r = R[n]; name, conf, ev, role = C[n]
        if r["group"] != last:
            last = r["group"]; print("\n== group %d: %s" % (last, GROUPS.get(last, "")))
        print("%3d g%-2d a=%03x b=%03x %-11s %4.0f/%4.0f  %.2f/%.2f  %s -> %s [%s]" % (n, r["group"], r["a"], r["b"], role, r["nose"], r["toe"], r["backext"], r["frontext"], hold_desc(r), name, conf))
        if ev: print("        evidence: " + ev)
    print("\n== other clips with a hand within 10 cm of the board (not grabs)")
    for n in (6, 7, 294, 370, 374, 375):
        r = analyse(n)
        print("%3d g%-2d a=%03x b=%03x  %s" % (n, r["group"], r["a"], r["b"], hold_desc(r)))
    print("\nskater: front hand = RIGHT hand (node 21), back hand = LEFT hand (node 15); board +x = toe side (the skater faces +x), -x = heel side, +z = nose")


if __name__ == "__main__":
    main()
