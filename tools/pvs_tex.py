#!/usr/bin/env python3
"""Texture-id table of a level, stored in track1.pvs: N entries of 64 bytes = [u8 flag][name, NUL padded].
(ARM loader: ov13 0x210fb20 reads [0x027E0148+0x54] entries of 0x40 bytes; the name is buf[1:];
an entry whose byte 1 is 0 becomes a NULL slot = no texture for that id.)
A texture id (as used by OCT2 batch entries) is the index into this table. The same file can appear
under several ids (the flag byte differs: '~' is by far the most common; meaning still unknown).
Names refer to *.dst files in the level zip. Empty slots are returned as (flag, "")."""
import sys, os, re, glob

NUL = bytes([0])


def read_table(pvs_path, zipnames=None):
    """-> list of (flag, filename or ""), index = texture id."""
    b = open(pvs_path, "rb").read()
    starts = [m.start() for m in re.finditer(rb"[ -~]{3,}\.dst" + NUL, b)]
    if not starts:
        return []

    def name_at(e, allow_empty):
        if e < 0 or e + 64 > len(b):
            return None
        nm = b[e + 1:e + 64].split(NUL)[0]
        if not nm:
            return "" if allow_empty else None
        if nm.lower().endswith(b".dst") and all(32 <= c < 127 for c in nm):
            return nm.decode("latin-1")
        return None

    best = None
    for shift in (0, -1):               # flag byte printable (inside the regex match) or not
        e = min(starts) + shift
        if name_at(e, False) is None:
            continue
        lo = e
        while name_at(lo - 64, False) is not None:
            lo -= 64
        ents, p = [], lo
        while name_at(p, True) is not None:
            ents.append((b[p], name_at(p, True)))
            p += 64
        while ents and ents[-1][1] == "":       # drop trailing empties (they are just padding)
            ents.pop()
        hit = sum(1 for _, n in ents if n and (zipnames is None or n.lower() in zipnames))
        if best is None or (hit, len(ents)) > best[0]:
            best = ((hit, len(ents)), ents)
    return best[1] if best else []


def level_table(level_dir):
    zn = {os.path.basename(p).lower() for p in glob.glob(level_dir + "*.dst")}
    return read_table(level_dir + "track1.pvs", zn), zn


# Sky texture per level, from the ARM9 level table at 0x02070D20 (36-byte rows, fields skewed by one row:
# row i holds level name + sky .dsm at +0x1C/+0x20 and ITS sky .dst at +0 of row i+1). Loaded from
# data/locations/skies/. A batch whose texture id == len(table) is the sky dome and uses this texture.
# Levels not listed have their sky inside their own zip table.
LEVEL_SKY = {
    "Spillway": "sanvan_skybox.dst", "CommCentre": "sanvan_skybox.dst", "Wilshire": "sanvan_skybox.dst",
    "EMB": "sanvan_skybox.dst", "Hellavator": "sanvan_skybox.dst", "DIYPark": "sanvan_skybox.dst",
    "Amphitheatre": "sanvan_skybox.dst", "BigAssWall": "sanvan_skybox.dst", "MegaPark": "sanvan_skybox.dst",
    "XGames": "sanvan_skybox.dst", "ElementarySchool": "sanvan_skybox.dst", "OldTown": "sanvan_skybox.dst",
    "Paris": "paris_skya.dst", "SanFran": "sanfran_256.dst", "SouthBank": "london_skyline.dst",
    "Barcelona": "barca_sky_large.dst", "Rio": "rio_sky_large.dst", "Shanghai": "shanghai_sky_large.dst",
}


def resolve_textures(level, root="extracted/unpacked/DATA/LOCATIONS"):
    """-> list indexed by texture id of file paths (None = empty slot); the sky texture is appended at
    index len(table)."""
    d = "%s/%s/%s/" % (root, level, level)
    tbl, _ = level_table(d)
    out = [(d + n) if n else None for _, n in tbl]
    sky = LEVEL_SKY.get(level)
    skies = {os.path.basename(p).lower(): p for p in glob.glob(root + "/SKIES/*.dst")}
    out.append(skies.get(sky.lower()) if sky else None)
    return out


if __name__ == "__main__":
    t, _ = level_table(sys.argv[1] if sys.argv[1].endswith("/") else sys.argv[1] + "/")
    for i, (f, n) in enumerate(t):
        print("%3d  flag=%02x %-3r  %s" % (i, f, chr(f) if 32 <= f < 127 else "", n))
