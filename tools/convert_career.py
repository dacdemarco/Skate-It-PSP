#!/usr/bin/env python3
"""Pack the career data decoded from the ROM (tools/campaign_dump.py -> campaign_data.json, tools/campaign_strings.tsv) into data/career.dat for the PSP game (src/career.c).
usage: convert_career.py [out.dat]     (run campaign_dump.py first)

File layout (little endian):
  Header (128 bytes): char magic[4] = 'SKCR'; u32 version = 5; u32 nchal, off_chal; u32 nobj, off_obj; u32 ntricks, off_tricks; u32 nlevels, off_levels; u32 off_str, size_str;
      u32 nskaters, off_skaters; u32 nareas, off_areas; u32 npools, off_pools; u32 nsets, off_sets; u32 nsetentries, off_setentries; u32 off_ai (nchal * 4 u16); u32 off_aiev, size_aiev; u32 off_story; pad
  Challenge (32 bytes) * nchal, in the original's order (index = the original's challenge index):
      u16 idx; u8 mode (the original's game mode id: 0 FreeSkate 1 Tutorial 2 Tutorial adv 3 Jam 4 SKATE 5 BestTrick 6 OwnTheSpot 7 Photo 8 Film 9 Deathrace 10 Checkpoint); u8 level (level table index); i16 time (s, -1 none);
      u8 slot (the chalNNN object number in the level); u8 nobj; u32 first_obj; u32 name_off, desc_off, map_off (string offsets); u8 pool (0xFF none); u8 flags (bit0 career, bit1 LINE mode: the objectives must be done in one line,
      bit2 multiplayer only); u8 opponents[3] (skater index, 0xFE none); u8 skate_set; u8 reward (the unlock set applied when the challenge is
      completed, 0xFF none); 1 pad
  Objective (20 bytes) * nobj:  u8 type (0 SCORE 1 LINE_SCORE 2 TRICK 3 AIR_TIME 4 SPIN 5 LINE 7 SKATE_GAME 8 JAM 9 BEST_TRICK_ROUND 10 OWN_THE_SPOT 11 CHECKPOINT); u8 flags (bit0 chained to the previous one: its text starts
      with ".." / "and", bit1 line only: the points must come from a single line); u16 mod; u32 a0, a1; u16 rail0, rail1 (a grind line number or a zone number, 0 = none); u32 text_off
  Trick (16 bytes) * ntricks (the original's trick table, index = trick id): u16 id; u16 class (0 flip 1 nollie flip 2 late flip 3 grind 4 grab 5 manual); u32 points; u32 per_sec; u32 name_off
  Level (16 bytes) * nlevels:  u8 id; u8 area; u16 pad; u32 name_off (display name); u32 zip_off (the folder / file stem, e.g. "Amphitheatre"); u32 pad
  Skater (16 bytes) * nskaters (header words 12, 13 = count, offset): u8 index (the original's skater index); u8 stance flag [?]; u16 pad; u32 name_off (display name, matched through the model key against TXT_PRO_SKATER_*:
      the dump's own text column is shifted by one record); u32 key_off (file key, e.g. "aaron"); f32 scale (0.6 = the five kids)
  Areas (header words 14, 15 = count, offset): u32 name_off * nareas ("Urban", "Parks", "Indoors", "Small Spots", "Real World"); a Level's area byte indexes it
  Pool (32 bytes) * npools, in progression order: u32 name_off; u8 required (completions that finish the pool); i8 sponsor slot (-1 none, 0 board, 1 trucks, 2 wheels, 3 shoes); u8 unlock set (applied when the pool
      becomes the current one); u8 nchal; u8 chal[24] (challenge indices)
  Unlock sets (4 bytes) * nsets: u16 first entry, u16 count;  entries (2 bytes) * nsetentries: u16 = kind << 8 | index  (kinds: 0 skater, 1 deck, 2 ?, 3 My Spot part, 4 ?, 5 shoes, 6 trucks, 7 wheels, 8 ?, 9 ?, 10 challenge)
  AI recordings (header word 22 = offset; 8 bytes * nchal): u16 samples[3] of the opponents' recordings (DATA/AI/<LEVEL>/<chal>_<mode>_0_<k>.dat, 0 = none), u16 pad. The recording has one sample per 7.5 game frames
      (4 Hz at 30 fps), so the opponent's run lasts samples / 4 seconds: checked against the races' time limits (Elementary School: 180 samples = 45 s against 12 s + 5 gates x 7 s)
  AI score events (header words 23, 24 = offset, size in bytes): records {u8 chal, u8 round, u8 opponent, u8 kind; u16 count; count * {u16 a, u16 b}}. kind 0 = the lines an opponent banked in a Jam /
      Best Trick recording (a = frame at 60 per second, b = points); kind 1 (one per S.K.A.T.E. challenge, round / opponent 0) = the tricks he sets in turn (a = the recording's index 0..9, b = the original's trick id)
  Story (header word 25 = offset; 4-byte aligned; M7): u32 nscenes (57), nlines, ncredits, npools, sign_str, soty_str, end_str, pad  (the strings: "%s has been signed to", "%s has been named Thrasher Skater Of The Year!",
      the end-of-game text); u16 {first line, count} * nscenes (scene n = entry n - 1: the NIS dialogue, one string offset per subtitle line); u32 line string offset * nlines;
      12 bytes * nchal: u8 intro scene, u8 win scene (0 = none; the story scenes before / after a sponsor challenge), u8 sponsor slot (0xFF none), pad, u32 sponsor name, u32 icon key ("planb");
      2 bytes * npools (padded to 4): u8 start scene, u8 end scene (0 = none); u32 credits string offset * ncredits (the original's credits, TXT_CREDITS_1..255)
  Strings: NUL-terminated ASCII (accents dropped)
"""
import sys, os, json, struct, unicodedata, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign_air

HERE = os.path.dirname(os.path.abspath(__file__))
TRICK_CLASS = {"flip": 0, "nollie_flip": 1, "late_flip": 2, "grind": 3, "grab": 4, "manual": 5}
CLASS_BITS = [(0x1, "Flip Trick"), (0x4, "Advanced Flip"), (0x8, "Grind"), (0x10, "Manual"), (0x20, "Grab"), (0x80, "Front Truck Grind"), (0x100, "Back Truck Grind"),
              (0x200, "Advanced Grab"), (0x400, "Advanced Grind"), (0x800, "Any Trick")]


def ascii_(s):
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if ord(c) < 128 and (c >= " " or c in "\n")).replace("\n", " ").replace(" ", " ")


class Strings:
    def __init__(self):
        self.buf = bytearray(b"\0")           # offset 0 = the empty string
        self.idx = {"": 0}

    def add(self, s):
        s = ascii_(s or "")
        if s not in self.idx:
            self.idx[s] = len(self.buf)
            self.buf += s.encode("ascii") + b"\0"
        return self.idx[s]


def load_strings():
    out = {}
    for line in open(os.path.join(HERE, "campaign_strings.tsv"), encoding="utf-8", errors="replace"):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 3 and p[0].isdigit():
            out[int(p[0])] = p[2]
    return out


def objective_text(o, tricks, text):
    """what the HUD shows for an objective: the original's own text when it has one, else a plain description built from the record"""
    if o["text_id"] and o["text_id"] in text and o["text_id"] != 0:
        return text[o["text_id"]]
    t, a0, a1 = o["type"], o["a0"], o["a1"]
    if t == 0:
        return "Get %d points" % a0
    if t == 1:
        return "Skate a line worth %d points" % a0
    if t == 3:
        return "Get %d s of air" % a0
    if t == 4:
        return "Spin %d degrees" % a0
    if t == 5:
        return "Skate a line"
    if t == 10:
        return "Beat the score"
    if t == 2:
        if a0 & 0x40000000:
            m = a0 & 0x3FFFFFFF
            name = "+".join(n for b, n in CLASS_BITS if m & b) or "Trick"
        else:
            name = tricks[a0]["name"] if a0 < len(tricks) else "Trick %d" % a0
            if a0 == 101:
                name = "Push"
        spin = " with a %d spin" % a1 if o["w3_lo"] == 2 else ""
        cnt = " x%d" % a1 if (a1 > 1 and o["w3_lo"] != 2) else ""
        return name + cnt + spin
    return o.get("desc", "")


SPONSOR_ICON = {"Plan B": "planb", "Almost": "almost", "Cliche": "cliche", "Alien Workshop": "alienws", "Girl": "girl", "Baker": "baker", "Blind": "blind", "Independent": "independent",
                "Venture": "venture", "Royal": "royal", "Silver": "silver", "Fillmore": "fillmore", "Spitfire": "spitfire", "Ricta": "ricta", "Autobahn": "autobahn", "Momentum": "momentum",
                "DC": "dc", "Lakai": "lakai", "Ice Cream": "icecream", "Adidas": "adidas", "Globe": "globe"}
# the ROM's intro_nis byte is right for every sponsor challenge but two: Baker's win scene is 54 (the byte gives Blind's intro) and Spitfire has its own pair 55 / 33 (docs/CAMPAIGN_FORMAT.md section 8);
# the first trip to Paris has its own pair too (scenes 3 and 4 are not named by any challenge field)
SCENE_OVERRIDE = {26: (16, 54), 47: (55, 33), 10: (3, 4)}
# which scene plays when a pool becomes the current one / when it is finished: the ROM's trigger table was not found, this follows the text of the scenes [guess, in the order of the story]
POOL_SCENES = {0: (1, 0), 3: (2, 0), 6: (5, 19), 9: (20, 29), 12: (30, 40), 15: (57, 0), 16: (51, 52), 17: (56, 0), 18: (0, 53)}
CREDITS_FIRST, CREDITS_LAST = 330, 584                      # TXT_CREDITS_1 .. the last line of the main block (what follows are fragments of the licence texts)


def story_text(s):
    return " ".join(s.replace("\\n", " ").split())


def pack_story(d, S):
    """the story section: scenes (the text of the NIS dialogue, one string per line), per challenge the scenes before / after it and its sponsor, per pool its start / end scene, the credits"""
    scenes = {}
    for line in open(os.path.join(HERE, "campaign_strings.tsv"), encoding="utf-8", errors="replace"):
        p = line.rstrip("\n").split("\t")
        m = re.match(r"TXT_NIS_(\d+)_(\d+)$", p[1]) if len(p) >= 3 else None
        if m:
            scenes.setdefault(int(m.group(1)), []).append((int(m.group(2)), p[2]))
    nsc = max(scenes)
    assert nsc == 57 and sorted(scenes) == list(range(1, 58)), sorted(scenes)
    scene_tab, line_tab = bytearray(), bytearray()
    for n in range(1, nsc + 1):
        ls = [t for k, t in sorted(scenes[n])]
        scene_tab += struct.pack("<HH", len(line_tab) // 4, len(ls))
        for t in ls:
            line_tab += struct.pack("<I", S.add(story_text(t)))
    text = load_strings()
    chal_tab = bytearray()
    for c in d["challenges"]:
        intro, win = c.get("intro_nis") or 0, c.get("win_nis") or 0
        if c["idx"] in SCENE_OVERRIDE:
            intro, win = SCENE_OVERRIDE[c["idx"]]
        assert 0 <= intro <= nsc and 0 <= win <= nsc, (c["idx"], intro, win)
        sp = c.get("sponsor") or ""
        slot = 0xFF
        if sp:
            assert sp in SPONSOR_ICON, sp
            for pl in d["pools"]:
                if c["idx"] in pl["challenges"] and pl["sponsor_slot"] is not None:
                    slot = pl["sponsor_slot"]
            assert slot != 0xFF, c["idx"]
        chal_tab += struct.pack("<BBBBII", intro, win, slot, 0, S.add(sp) if sp else 0, S.add(SPONSOR_ICON.get(sp, "")) if sp else 0)
    pool_tab = bytearray()
    for i in range(len(d["pools"])):
        pool_tab += struct.pack("<BB", *POOL_SCENES.get(i, (0, 0)))
    if len(pool_tab) & 3:
        pool_tab += bytes(4 - (len(pool_tab) & 3))
    cred_tab = bytearray()
    for i in range(CREDITS_FIRST, CREDITS_LAST + 1):
        cred_tab += struct.pack("<I", S.add(story_text(text.get(i, ""))))
    misc = [S.add(story_text(text[35])), S.add(story_text(text[29])), S.add(story_text(text[759]))]
    head = struct.pack("<8I", nsc, len(line_tab) // 4, CREDITS_LAST - CREDITS_FIRST + 1, len(d["pools"]), misc[0], misc[1], misc[2], 0)
    return head + bytes(scene_tab) + bytes(line_tab) + bytes(chal_tab) + bytes(pool_tab) + bytes(cred_tab)


def main(out_path):
    d = json.load(open(os.path.join(HERE, "campaign_data.json"), encoding="utf-8"))
    text = load_strings()
    S = Strings()
    chal, objs = bytearray(), bytearray()
    for c in d["challenges"]:
        first = len(objs) // 20
        flags_line = c["mode"] == 7                          # Photo: one line
        chained = False
        line_only_text = "single line" in (c["map_desc"] or "").lower()
        for k, o in enumerate(c["objectives"]):
            txt = objective_text(o, d["tricks"], text)
            chain = txt.startswith("..") or txt.lower().startswith("and ")
            chained |= chain
            r = o.get("rails") or []
            r0 = r[0] if len(r) > 0 and r[0] not in (0, 65535) else 0
            r1 = r[1] if len(r) > 1 and r[1] not in (0, 65535) else 0
            fl = (1 if chain else 0) | (2 if (o["type"] == 0 and line_only_text) else 0)
            mod = o["w3_lo"] & 0xFFFF
            objs += struct.pack("<BBHIIHHI", o["type"], fl, mod, o["a0"] & 0xFFFFFFFF, o["a1"] & 0xFFFFFFFF, r0, r1, S.add(txt.lstrip(". ") if chain else txt))
        if c["mode"] == 8 and " to " in (c["map_desc"] or "").lower():
            chained = True
        line = flags_line or chained
        opp = (c["opponents"] + [0xFE, 0xFE, 0xFE])[:3] if c["opponents"] and isinstance(c["opponents"][0], int) else [0xFE, 0xFE, 0xFE]
        pool = c["pool"] if c.get("pool") is not None else 0xFF
        flags = (1 if c["career"] else 0) | (2 if line else 0) | (4 if (c["flags"] & 0x0F) == 0x0E else 0)
        chal += struct.pack("<HBBhBBIIIIBBBBBBBx", c["idx"], c["mode"] & 0xFF, c["location"] & 0xFF, c["time_limit"], c["slot"] & 0xFF, len(c["objectives"]), first, S.add(c["name"]), S.add(c["desc"]), S.add(c["map_desc"]),
                            pool, flags, opp[0], opp[1], opp[2], c["skate_set"] & 0xFF, c["reward_set"] if c.get("reward_set") is not None else 0xFF)
    tr = bytearray()
    for t in d["tricks"]:
        tr += struct.pack("<HHIII", t["id"], TRICK_CLASS.get(t["class"], 0), t["points"], t["per_sec"], S.add(t["name"]))
    lv = bytearray()
    for l in d["levels"]:
        lv += struct.pack("<BBHIII", l["id"], l["area"], 0, S.add(l["text"]), S.add(l["zip"]), 0)
    names = {}
    for line in open(os.path.join(HERE, "campaign_strings.tsv"), encoding="utf-8", errors="replace"):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 3 and p[1].startswith("TXT_PRO_SKATER_"):
            names[p[1][len("TXT_PRO_SKATER_"):].replace("_", "").lower()] = p[2]
    names["shingo"] = names.get("shintagro", "Shingo")
    sk = bytearray()
    for k in d["skaters"]:
        sk += struct.pack("<BBHIIf", k["index"], k.get("flag", 0) & 0xFF, 0, S.add(names.get(k["name"], k["name"])), S.add(k["name"]), k.get("scale", 1.0))
    pools = bytearray()
    for p in d["pools"]:
        assert len(p["challenges"]) <= 24 and all(x < 256 for x in p["challenges"])
        sl = p["sponsor_slot"] if p["sponsor_slot"] is not None else -1
        pools += struct.pack("<IBbBB24s", S.add(text.get(p["str"], p["name"])), p["required"], sl, p["unlock_set"], len(p["challenges"]), bytes(p["challenges"]))
    sets, entries = bytearray(), bytearray()
    assert [x["set"] for x in d["unlock_sets"]] == list(range(len(d["unlock_sets"])))
    for us in d["unlock_sets"]:
        sets += struct.pack("<HH", len(entries) // 2, len(us["items"]))
        for it in us["items"]:
            assert 0 <= it["kind"] < 16 and 0 <= it["index"] < 256, it
            entries += struct.pack("<H", it["kind"] << 8 | it["index"])
    ai = bytearray()
    for c in d["challenges"]:
        b = [0, 0, 0]
        for fn in c.get("ai_files") or []:
            m = re.search(r"_(\d+)_(\d+)_(\d+)\.dat$", fn)
            path = os.path.join(HERE, "..", "extracted", "files", "DATA", "AI", fn.replace("/", os.sep))
            if m and int(m.group(3)) < 3 and c["mode"] in (9, 10) and os.path.exists(path):
                b[int(m.group(3))] = campaign_air.read(path)["B"]
        ai += struct.pack("<4H", b[0], b[1], b[2], 0)
    aiev = bytearray()
    for c in d["challenges"]:
        if c["mode"] not in (3, 4, 5) or not c.get("ai_files"):
            continue
        skate_sets = {}
        for fn in c["ai_files"]:
            m = re.search(r"_(\d+)_(\d+)_(\d+)\.dat$", fn)
            path = os.path.join(HERE, "..", "extracted", "files", "DATA", "AI", fn.replace("/", os.sep))
            if not m or not os.path.exists(path):
                continue
            a, b = int(m.group(2)), int(m.group(3))
            ev = campaign_air.events(path)
            if c["mode"] in (3, 5):
                lines = [(fr, struct.unpack_from("<H", args, 2)[0]) for fr, kind, args in ev if kind == 49]
                aiev += struct.pack("<BBBBH", c["idx"], a, b, 0, len(lines)) + b"".join(struct.pack("<HH", *x) for x in lines)
            elif b == 0 and a < 10:                                   # a S.K.A.T.E. recording of a success: the trick he set
                tricks = [args[0] for fr, kind, args in ev if kind == 27]
                if tricks and any(kind == 49 for fr, kind, args in ev):
                    skate_sets[a] = tricks[0]
        if c["mode"] == 4 and skate_sets:
            aiev += struct.pack("<BBBBH", c["idx"], 0, 0, 1, len(skate_sets)) + b"".join(struct.pack("<HH", a, t) for a, t in sorted(skate_sets.items()))
    off = 128
    off_chal = off; off += len(chal)
    off_obj = off; off += len(objs)
    off_tr = off; off += len(tr)
    off_lv = off; off += len(lv)
    off_sk = off; off += len(sk)
    ar = b"".join(struct.pack("<I", S.add(x["text"])) for x in d["areas"])
    off_ar = off; off += len(ar)
    off_pl = off; off += len(pools)
    off_us = off; off += len(sets)
    off_ue = off; off += len(entries)
    off_ai = off; off += len(ai)
    off_aiev = off; off += len(aiev)
    story = pack_story(d, S)
    story_pad = (-off) & 3                                       # the story section is read as u32 tables
    off += story_pad
    off_story = off; off += len(story)
    off_str = off
    hdr = struct.pack("<4sIIIIIIIIIII", b"SKCR", 5, len(d["challenges"]), off_chal, len(objs) // 20, off_obj, len(d["tricks"]), off_tr, len(d["levels"]), off_lv, off_str, len(S.buf))
    hdr += struct.pack("<IIII", len(d["skaters"]), off_sk, len(d["areas"]), off_ar)
    hdr += struct.pack("<IIIIIIIII", len(d["pools"]), off_pl, len(d["unlock_sets"]), off_us, len(entries) // 2, off_ue, off_ai, off_aiev, len(aiev))
    hdr += struct.pack("<I", off_story)
    hdr += bytes(128 - len(hdr))
    open(out_path, "wb").write(hdr + chal + objs + tr + lv + sk + ar + pools + sets + entries + ai + bytes(aiev) + bytes(story_pad) + story + bytes(S.buf))
    print("%s: %d challenges, %d objectives, %d tricks, %d levels, %d pools, %d unlock sets (%d entries), %d bytes of strings, %d bytes" % (out_path, len(d["challenges"]), len(objs) // 20, len(d["tricks"]), len(d["levels"]), len(d["pools"]), len(d["unlock_sets"]), len(entries) // 2, len(S.buf), off_str + len(S.buf)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "dist", "SkateItPSP", "data", "career.dat"))
