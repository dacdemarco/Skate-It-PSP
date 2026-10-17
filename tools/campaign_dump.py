#!/usr/bin/env python3
"""Dump the DS game's whole career/campaign definition to JSON/TSV (everything decoded from arm9 data tables, textdb.arc, AI/*.dat and track1.pvs).
Outputs (next to this script):
  campaign_data.json         areas, modes, levels, skaters, tricks, pools, challenges (+ decoded objectives), unlock sets, reward map
  campaign_challenges.tsv    one line per challenge, human readable
  campaign_levels.json       per level: the named object table + grind splines + cameras + traffic splines of track1.pvs (coordinates in the
                             SAME world space as the converted level: 1.19.12 fixed point /4096, Y up)
Table locations (all in decompressed arm9, RAM base 0x02000000; verified against the code that indexes them):
  mode table      0x02070B04  12 x 0x28   {u32 id, u32 str id, char name[16], u8 flags[16]}
  area table      0x02070CE4   5 x 8      {u32 id, u32 str id}
  level table     0x02070D0C  21 x 0x24   {u32 id, u32 area, u32 str id, char* zip name, char* sky dsm, char* sky dst, s32 y?, u32 2000, u32 ?}
  challenge table 0x0206E038 103 x 0x1C   see parse_challenges()
  objective lists 0x0206EB7C.. 24-byte records, pointed to by challenge+0x18, count = challenge.nobj
  pool table      0x020814D0  19 x 0x14   {u32 str id, u32 n, u32 required, u8* list, s32 sponsor slot}
  skater table    0x02075950  20 x 0x1C   {char* mbf fmt, char* dst, u32 id+1, u32 str id, u32 goofy?, s32 scale, s32 scale2}
  trick table     0x02076800 101 x 0x1C   {u32 str id, u32 points, u32 points/s, u32 ?, u32 id+1, u32 ?, u32 ?}
  unlock sets     0x02077DCC 118 x 8      {u32* list, u32 count}; list entries u32 = kind<<16 | index
  challenge->set  0x02075098 101 x 8      {u32 challenge, s32 set}
Run from the project root:  python tools/campaign_dump.py"""
import struct, sys, os, json, re, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from campaign_text import load
import campaign_pvs as P
import campaign_air as AIR

BASE = 0x02000000
b9 = open("extracted/decomp/arm9.bin", "rb").read()
KEYS, LANGS = load()
EN = LANGS["English"]
KD = dict(zip(KEYS, EN))


def u32(a): return struct.unpack_from("<I", b9, a - BASE)[0]
def s32(a): return struct.unpack_from("<i", b9, a - BASE)[0]
def cstr(a):
    o = a - BASE
    return b9[o:b9.index(b"\0", o)].decode("latin-1")
def txt(i): return EN[i] if 0 <= i < len(EN) else None


def parse_modes():
    out = []
    for i in range(12):
        a = 0x02070B04 + i * 0x28
        r = b9[a - BASE:a - BASE + 0x28]
        out.append(dict(id=u32(a), str=u32(a + 4), name=r[8:24].split(b"\0")[0].decode(), text=txt(u32(a + 4)), flags=list(r[24:40])))
    return out


def parse_areas():
    return [dict(id=u32(0x02070CE4 + i * 8), str=u32(0x02070CE4 + i * 8 + 4), text=txt(u32(0x02070CE4 + i * 8 + 4))) for i in range(5)]


def parse_levels():
    out = []
    for i in range(21):
        a = 0x02070D0C + i * 0x24
        w = struct.unpack_from("<9I", b9, a - BASE)
        ptr = lambda p: cstr(p) if 0x02000000 <= p < 0x02100000 else None
        out.append(dict(id=w[0], area=w[1], str=w[2], text=txt(w[2]), zip=ptr(w[3]), sky_dsm=ptr(w[4]), sky_dst=ptr(w[5]),
                        f6=struct.unpack("<i", struct.pack("<I", w[6]))[0] / 4096.0, f7=w[7], f8=w[8]))
    return out


def parse_skaters():
    out = []
    for i in range(20):
        a = 0x02075950 + i * 0x1C
        w = struct.unpack_from("<7I", b9, a - BASE)
        nm = cstr(w[0]).replace("skater_", "").replace("_lod%d.mbf", "")
        d = dict(index=i, name=nm, mbf=cstr(w[0]), tex=cstr(w[1]))
        if i < 19:
            d.update(id=w[2], str=w[3], text=txt(w[3]), flag=w[4], scale=struct.unpack("<i", struct.pack("<I", w[5]))[0] / 4096.0)
        else:
            d.update(text="Reda (cameraman)")
        out.append(d)
    return out


def parse_tricks():
    out = []
    for i in range(101):
        w = struct.unpack_from("<7I", b9, 0x02076800 + i * 28 - BASE)
        out.append(dict(id=i, str=w[0], name=txt(w[0]), points=w[1], per_sec=w[2], w3=w[3], w5=w[5], w6=w[6]))
    # trick class boundaries (verified by counts: 15 ground flips + 15 nollie + 6 late + 34 grinds + 29 grabs + 2 manuals ... )
    for t in out:
        i = t["id"]
        t["class"] = ("flip" if i <= 14 else "nollie_flip" if i <= 29 else "late_flip" if i <= 35 else "grind" if i <= 69 else "grab" if i <= 98 else "manual")
    return out


OBJ_TYPES = {0: "SCORE", 1: "LINE_SCORE", 2: "TRICK", 3: "AIR_TIME", 4: "SPIN", 5: "LINE", 7: "SKATE_GAME", 8: "JAM", 9: "BEST_TRICK_ROUND",
             10: "OWN_THE_SPOT", 11: "CHECKPOINT"}
CLASS_BITS = {0x1: "flip trick", 0x4: "advanced flip", 0x8: "grind", 0x10: "manual", 0x20: "grab", 0x80: "adv grind (front trucks)",
              0x100: "adv grind (back trucks)", 0x200: "advanced grab", 0x400: "advanced grind", 0x800: "any trick"}


def parse_objectives(ptr, n, tricks):
    out = []
    for k in range(n):
        w = struct.unpack_from("<6I", b9, ptr + 24 * k - BASE)
        t, a0, a1, w3, w4, w5 = w
        mod = w3 & 0xFFFF                      # 0xFFFF = no modifier; mod == 2 = a1 is a SPIN in degrees (instead of a count)
        d = dict(type=t, type_name=OBJ_TYPES.get(t, "?%d" % t), a0=a0, a1=a1, w3_lo=w3 & 0xFFFF, w3_hi=w3 >> 16, w4_lo=w4 & 0xFFFF, w4_hi=w4 >> 16,
                 text_id=w5 & 0xFFFF, text_flags=w5 >> 16, raw=["%08x" % x for x in w])
        s = d["type_name"]
        if t == 2:
            if a0 & 0x40000000:
                bits = [nm for bit, nm in CLASS_BITS.items() if (a0 & 0xFFFF) & bit]
                s += " class[%s]" % "+".join(bits) if bits else " class[0x%x]" % (a0 & 0xFFFF)
            else:
                s += " %s" % (tricks[a0]["name"] if a0 < len(tricks) else "trick#%d" % a0)
            s += (" with %d deg spin" % a1) if mod == 2 else (" x%d" % a1)
            if mod not in (0xFFFF, 2): s += " [mod 0x%x]" % mod
        elif t in (0, 1, 3, 4, 5):
            s += " %d" % a0
        elif t == 7:
            s += " (%s)" % ("any trick" if a0 == 0x40000800 else "0x%x" % a0)
        elif t == 9:
            s += " round-selector %d" % a0
        elif t == 10:
            s += " score %d..%d" % (a0, a1)
        elif t == 11:
            s += " gate %d bonus %s s" % (a0, "-" if a1 == 0xFFFFFFFF else a1)
        rails = [v for v in (w4 & 0xFFFF, w4 >> 16) if v not in (0, 0xFFFF) and t in (2, 9, 10)]   # 1-based number NNN of col<N>_grind_<NNN> in the level's PVS (0/0xFFFF = none)
        if rails:
            d["rails"] = rails
            s += " rails %s" % rails
        if d["text_id"]:
            s += " | \"%s\"" % txt(d["text_id"])
        d["desc"] = s
        out.append(d)
    return out


def parse_challenges(tricks, levels, skaters):
    pools = parse_pools()
    pool_of = {}
    for p in pools:
        for c in p["challenges"]:
            pool_of[c] = p["index"]
    sponsors = {}
    for names, rng in (("board", [21, 22, 23, 24, 25, 26, 27]), ("truck", [34, 35, 36, 37]), ("wheels", [46, 47, 48, 49, 50]), ("shoes", [61, 62, 63, 64, 65])):
        pass
    SP = {21: "Plan B", 22: "Almost", 23: "Cliche", 24: "Alien Workshop", 25: "Girl", 26: "Baker", 27: "Blind", 34: "Independent", 35: "Venture",
          36: "Royal", 37: "Silver", 46: "Fillmore", 47: "Spitfire", 48: "Ricta", 49: "Autobahn", 50: "Momentum", 61: "DC", 62: "Lakai",
          63: "Ice Cream", 64: "Adidas", 65: "Globe"}
    air = {}
    for f in glob.glob(AIR.ROOT + "/*/*.dat"):
        m = re.match(r"(\d+)_(\d+)_(\d+)_(\d+)\.dat", os.path.basename(f))
        if m and int(m[1]) > 0:
            air.setdefault(int(m[1]), []).append(os.path.relpath(f, AIR.ROOT).replace("\\", "/"))
    out = []
    for i in range(103):
        a = 0x0206E038 + i * 28
        idx, nm, ds, loc, mode, slot, tl, c0, c1, c2, c3, d0, d1, d2, d3, fa, fb, ptr = struct.unpack_from("<I2H4b4B4B2HI", b9, a - BASE)
        opp = [x for x in (c2, c3, d0) if x != 0xFE]
        mode_i = mode
        d = dict(idx=idx, name_str=nm, desc_str=ds, name=txt(nm) if nm else None, desc=txt(ds) if ds else None,
                 map_desc=KD.get("TXT_CHALLENGE_MAP_DESC_%03d" % (idx + 1)),
                 location=loc, location_name=(levels[loc]["zip"] if 0 <= loc < 21 else None), mode=mode_i, slot=slot,
                 time_limit=None if tl == 0xFF else tl, rounds_or_x=c0, c1=c1, opponents=opp, opponent_names=[skaters[o]["name"] for o in opp],
                 skate_set=None if d1 == 0xFF else d1, d_bytes=[d0, d1, d2, d3], intro_nis=None if d2 == 0xFF else d2 + 1, win_nis=None if d2 == 0xFF else d2 + 2,
                 career=bool(fa & 1), mp_bits=fa >> 1, flags=fa, nobj=fb, pool=pool_of.get(idx), sponsor=SP.get(idx),
                 reward_set=30 + idx if idx <= 87 and idx not in (73, 74) else None, ai_files=sorted(air.get(idx, [])), objectives=[])
        if ptr and fb:
            d["objectives"] = parse_objectives(ptr, fb, tricks)
        out.append(d)
    return out


def parse_pools():
    out = []
    for i in range(19):
        a = 0x020814D0 + i * 20
        sid, n, req, lp, sp = struct.unpack_from("<IIIII", b9, a - BASE)
        lst = list(b9[lp - BASE:lp - BASE + n])
        out.append(dict(index=i, str=sid, name=txt(sid), challenges=lst, required=req, sponsor_slot=None if sp == 0xFFFFFFFF else sp, unlock_set=i + 11))
    return out


def unlock_triggers(sets):
    """challenge idx -> how it gets unlocked: ('pool', n) = set 11+n applied when pool n becomes current; ('challenge', c) = set 30+c applied when c is completed"""
    out = {}
    for st in sets:
        for it in st["items"]:
            if it["kind"] == 10:
                if 11 <= st["set"] <= 29: out.setdefault(it["index"], []).append(("pool_start", st["set"] - 11))
                elif st["set"] >= 30: out.setdefault(it["index"], []).append(("after_challenge", st["set"] - 30))
                else: out.setdefault(it["index"], []).append(("initial_set", st["set"]))
    return out


def parse_unlock_sets():
    out = []
    for i in range(118):
        p, c = struct.unpack_from("<II", b9, 0x02077DCC + i * 8 - BASE)
        c &= 0xFF
        lst = struct.unpack_from("<%dI" % c, b9, p - BASE)
        out.append(dict(set=i, items=[dict(kind=x >> 16, index=(x & 0xFFFF) if (x & 0xFFFF) != 0xFFFF else None) for x in lst]))
    return out


# ---------------- level objects
def level_objects(level):
    d = P.parse(P.level_pvs(level))
    o = {}
    names = {}
    for nm, x, y, z in d["objs"]:
        o.setdefault(nm, []).append([round(x, 3), round(y, 3), round(z, 3)])
    return dict(objects=o, splines={n: [[round(v, 3) for v in p] for p in pts] for n, pts in d["splines"]},
                cameras={n: [[round(v, 3) for v in p] for p in pts] for n, pts in d["cams"]})


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    modes, areas, levels, skaters, tricks = parse_modes(), parse_areas(), parse_levels(), parse_skaters(), parse_tricks()
    chal = parse_challenges(tricks, levels, skaters)
    usets = parse_unlock_sets()
    trig = unlock_triggers(usets)
    lvobj = {}
    for l in levels:
        try:
            lvobj[l["zip"]] = level_objects(l["zip"])
        except Exception as e:
            print("no objects for", l["zip"], e)
    for c in chal:
        c["unlocked_by"] = trig.get(c["idx"], [])
        lo = lvobj.get(c["location_name"])
        if lo and c["slot"] >= 0:
            ob = lo["objects"]; pre = "chal%03d_" % c["slot"]
            c["placement"] = {k[len(pre):]: v for k, v in ob.items() if k.startswith(pre)}
            c["placement"]["gates"] = {int(re.search(r"checkpoint_(\d+)_", k).group(1)): ob[k] for k in ob if k.startswith("checkpoint_") and k.endswith("_a")} if c["mode"] in (9, 10, 11) else {}
    data = dict(areas=areas, modes=modes, levels=levels, skaters=skaters, tricks=tricks, pools=parse_pools(), challenges=chal,
                unlock_sets=usets,
                notes="levels[].zip = DATA/LOCATIONS/<zip>.zip folder; challenge.location indexes levels; challenge.mode indexes modes; "
                      "challenge.slot = the 'chalNNN' object index in that level's track1.pvs; opponents/skater indexes index skaters[]")
    json.dump(data, open(os.path.join(here, "campaign_data.json"), "w"), indent=1)
    with open(os.path.join(here, "campaign_challenges.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("idx\tpool\tlocation\tmode\tslot\tname\ttime\topponents\tskateset\tsponsor\tintro_nis\tobjectives\tai_files\n")
        for c in chal:
            f.write("%d\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\n" % (
                c["idx"], c["pool"], c["location_name"], modes[c["mode"]]["name"] if 0 <= c["mode"] < 12 else "?", c["slot"], c["name"], c["time_limit"],
                ",".join(c["opponent_names"]), c["skate_set"], c["sponsor"] or "", c["intro_nis"], " ; ".join(o["desc"] for o in c["objectives"]), len(c["ai_files"])))
    lv = lvobj
    json.dump(lv, open(os.path.join(here, "campaign_levels.json"), "w"), separators=(",", ":"))
    print("wrote campaign_data.json (%d challenges), campaign_challenges.tsv, campaign_levels.json (%d levels)" % (len(chal), len(lv)))


if __name__ == "__main__":
    main()
