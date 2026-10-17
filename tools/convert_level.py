#!/usr/bin/env python3
"""Convert one Skate It DS level into a PSP-ready .lvl file.

usage: convert_level.py <LevelName> <out.lvl> [--no-collfix] [--names]
  --no-collfix  keep only the original collision polygons (default: also add solid walls for drawn-but-not-solid wall triangles, tools/collfix.py)
  --names       also write <out.lvl>.names (texture index -> file name) for the analysis tools

File layout (little endian, every section 16-byte aligned):
  Header  (116 bytes, version 4)
    char  magic[4] = 'SKLV';  u32 version = 1
    u32 ntex, nbatch, nverts, ncollverts, ncollpolys
    u32 off_tex, off_batch, off_verts, off_collv, off_collp          (file offsets)
    f32 bounds[6]  (minx, miny, minz, maxx, maxy, maxz of the visible geometry, layers 1/2/4 only)
    f32 start[3]   (suggested camera start)
    u32 nrails, nrailpts, off_rails   (v2)
    u32 off_polymat (v3)
    u32 off_railids, off_objs, off_paths, pad (v4)
  TexEntry (24 bytes) * ntex
    u16 w, h        stored size in pixels (small textures are tiled up to the GPU minimum width)
    u8  bpp (4 or 8), u8 swizzled, u8 has_alpha, u8 pad
    u32 clut_off, pix_off                (file offsets), u32 clut_entries (16 or 256), u32 pad
    clut = 5551 entries (same bit layout as the DS: R bits0-4, G 5-9, B 10-14, bit15 = opaque)
    pixels = T4/T8 data, GPU swizzled when swizzled == 1
  Batch (16 bytes) * nbatch   sorted by texture
    u16 tex (0xFFFF = untextured), u16 flags (bit0 = background layer 7_), u32 first_vertex, u32 vertex_count, u32 pad
  Vertex (24 bytes) * nverts, triangle list:  f32 u, v;  u32 abgr;  f32 x, y, z     (uv normalised by stored w/h)
  CollVert (12 bytes) * ncollverts: f32 x,y,z
  Material bytes (v3) at off_polymat: one per collision polygon (tools/materials.py: bits 0-1 concrete/metal/wood, bits 2-3 medium/rough/smooth roll)
  Rails (v2) at off_rails: nrails * {u32 first_point, u32 npoints | kind << 16 | material << 24} (kind 0 = rail, 1 = ledge: tools/ledges.py, the top surface on the left of the direction), then nrailpts * {f32 x,y,z} (the centre line over the TOP of each rail, ~0.25 m apart)
  Rail ids (v4) at off_railids: nrails * u16 = the original game's grind line number (col<L>_grind_<NNN> of track1.pvs, 1-based, what the challenge goals refer to); 0x8000 = a rail / ledge the heuristics found that no original line covers
  Objects (v4) at off_objs: u32 n, then n * {char name[32], f32 x, y, z}: every named object of track1.pvs (start grids, challenge beams, gates, zones, bounds, spawn points ...)
  Paths (v4) at off_paths: u32 n, then n * {char name[32], u32 npts, npts * f32 x,y,z}: the non-grind, non-traffic splines of track1.pvs (progress_spline ...)
  CollPoly (12 bytes) * ncollpolys: u16 v0..v3 (0xFFFF = none), u32 flags      (flag 0x8000 = wall added by tools/collfix.py from a render triangle, 0x4000 = invisible edge wall where the floor ends over nothing)
"""
import sys, os, struct, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm, dst, pvs_tex

ROOT = "extracted/unpacked/DATA/LOCATIONS"
HDR_FMT = "<4sI5I5I6f3f3II4I"       # 116 bytes; the first 84 are the v1 header, then nrails, nrailpts, off_rails (v2), off_polymat (v3), off_railids, off_objs, off_paths, pad (v4)
TEX_FMT = "<HHBBBBIIII"
BATCH_FMT = "<HHIII"
VERT_FMT = "<2fI3f"


def align16(n):
    return (n + 15) & ~15


def swizzle(data, width_bytes, height):
    """PSP texture swizzle: 16-byte x 8-row blocks, row-major blocks."""
    out = bytearray(len(data))
    rowblocks = width_bytes // 16
    for j in range(height):
        for i in range(width_bytes):
            bx, by, x, y = i // 16, j // 8, i % 16, j % 8
            out[(bx + by * rowblocks) * 128 + y * 16 + x] = data[i + j * width_bytes]
    return bytes(out)


def unswizzle(data, width_bytes, height):
    out = bytearray(len(data))
    rowblocks = width_bytes // 16
    for j in range(height):
        for i in range(width_bytes):
            bx, by, x, y = i // 16, j // 8, i % 16, j % 8
            out[i + j * width_bytes] = data[(bx + by * rowblocks) * 128 + y * 16 + x]
    return bytes(out)


def prep_texture(path):
    t = dst.decode(open(path, "rb").read())
    w, h, idx = t["w"], t["h"], t["idx"]
    bpp = 8 if t["fmt"] == 4 else 4          # 2-bit textures are widened to 4-bit (PSP has no T2)
    n = 256 if bpp == 8 else 16
    pal = list(t["pal"])
    if t["flag"] == 1 and pal:
        pal[0] = 0                            # colour key: palette entry 0 is transparent black (a leftover key colour fringes under linear filtering)
    pal = (pal + [0] * n)[:n]
    k = 1
    min_w = 32 if bpp == 4 else 16            # GPU wants >= 16 bytes per row
    while w * k < min_w:
        k *= 2
    if k > 1:                                 # tile horizontally; sampling stays identical under REPEAT
        idx = [idx[y * w + (x % w)] for y in range(h) for x in range(w * k)]
        w *= k
    if bpp == 4:
        pix = bytes((idx[i] & 15) | ((idx[i + 1] & 15) << 4) for i in range(0, len(idx), 2))
    else:
        pix = bytes(idx)
    wb = w * bpp // 8
    swz = 1 if (wb % 16 == 0 and h % 8 == 0) else 0
    if swz:
        pix = swizzle(pix, wb, h)
    assert w <= 512 and h <= 512 and (w & (w - 1)) == 0 and (h & (h - 1)) == 0, (path, w, h)
    used = set(idx)
    has_alpha = 1 if any(not (pal[i] & 0x8000) for i in used if i < len(pal)) else 0
    return dict(w=w, h=h, bpp=bpp, swz=swz, alpha=has_alpha, clut=struct.pack("<%dH" % n, *pal), pix=pix, n=n,
                src_idx=t["idx"], src_w=t["w"])


MAX_EDGE = 14.0      # world units (~metres); longer triangle edges are split


def _mid(a, b):
    return (tuple((a[0][k] + b[0][k]) / 2 for k in range(3)),
            ((a[1][0] + b[1][0]) / 2, (a[1][1] + b[1][1]) / 2),
            tuple((a[2][k] + b[2][k]) / 2 for k in range(3)))


def subdivide(tri, max_edge):
    """Split a triangle (3 x (pos, uv, colour)) along its longest edge until every edge <= max_edge."""
    import math
    out, stack = [], [tri]
    while stack:
        t = stack.pop()
        d = [math.dist(t[0][0], t[1][0]), math.dist(t[1][0], t[2][0]), math.dist(t[2][0], t[0][0])]
        k = d.index(max(d))
        if d[k] <= max_edge:
            out.append(t)
            continue
        a, b, c = t[k], t[(k + 1) % 3], t[(k + 2) % 3]      # split edge a-b at its midpoint m
        m = _mid(a, b)
        stack.append((a, m, c))
        stack.append((m, b, c))
    return out


def _tri_y(a, b, c, x, z):
    d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
    if abs(d) < 1e-9:
        return None
    l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
    l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
    l3 = 1 - l1 - l2
    if min(l1, l2, l3) < -1e-4:
        return None
    return l1 * a[1] + l2 * b[1] + l3 * c[1]


def pick_start(cverts, polys, bounds):
    """A good place to put the skater/camera: the centre of a large, flat, upward-facing collision polygon that has
    nothing above it (open plaza, not a rooftop or an underpass). Returns (x, y, z) = a point ON the ground."""
    import math
    cand = []
    for p in polys:
        idx = [i for i in p[:4] if i != 0xFFFF]
        pts = [cverts[i] for i in idx]
        if len(pts) < 3:
            continue
        n = [0.0, 0.0, 0.0]
        for k in range(len(pts)):                      # polygon normal (Newell)
            a, b = pts[k], pts[(k + 1) % len(pts)]
            n[0] += (a[1] - b[1]) * (a[2] + b[2]); n[1] += (a[2] - b[2]) * (a[0] + b[0]); n[2] += (a[0] - b[0]) * (a[1] + b[1])
        ln = math.sqrt(sum(c * c for c in n))
        if ln < 1e-6 or n[1] / ln < 0.97:
            continue
        cand.append((ln / 2, idx, pts))
    cand.sort(key=lambda c: -c[0])
    cx = (bounds[0][0] + bounds[1][0]) / 2; cz = (bounds[0][2] + bounds[1][2]) / 2
    best = None
    for area, idx, pts in cand[:150]:
        x = sum(p[0] for p in pts) / len(pts); z = sum(p[2] for p in pts) / len(pts)
        y = sum(p[1] for p in pts) / len(pts)
        if not (bounds[0][0] + 3 < x < bounds[1][0] - 3 and bounds[0][2] + 3 < z < bounds[1][2] - 3):
            continue                                   # collision-only ground outside what is drawn (ElementarySchool, LakeSherwin had their start there)
        covered = False
        for q in polys:                                # anything above the point within 5 units?
            qi = [i for i in q[:4] if i != 0xFFFF]
            tri_sets = [(qi[0], qi[1], qi[2])] + ([(qi[0], qi[2], qi[3])] if len(qi) == 4 else [])
            for a, b, c in tri_sets:
                yy = _tri_y(cverts[a], cverts[b], cverts[c], x, z)
                if yy is not None and y + 0.3 < yy < y + 5.0:
                    covered = True; break
            if covered:
                break
        if covered:
            continue
        score = area - 0.02 * math.hypot(x - cx, z - cz) * 10
        if best is None or score > best[0]:
            best = (score, (x, y, z))
    if best is None:
        return ((bounds[0][0] + bounds[1][0]) / 2, bounds[1][1] + 15.0, (bounds[0][2] + bounds[1][2]) / 2)
    return best[1]


def convert(level, out_path, collfix=True, names_sidecar=False):
    lvl = "%s/%s/%s/" % (ROOT, level, level)
    table = pvs_tex.resolve_textures(level, ROOT)             # id -> path (None = no texture)
    nverts_pool = len(octm.load_cmn(lvl + "verts.cmn"))
    # ---- textures actually used, deduplicated by file
    tex_index, textures = {}, []

    def tex_for(tid):
        if tid == 0xFFFF or tid >= len(table) or table[tid] is None:
            return 0xFFFF
        key = os.path.normcase(table[tid])
        if key not in tex_index:
            tex_index[key] = len(textures)
            textures.append(prep_texture(table[tid]))
        return tex_index[key]

    # ---- geometry
    per_batch = collections.defaultdict(list)                 # (tex, background) -> [vertex tuples]
    raw = []                                                  # (tex, background, triangle)
    collpolys = set()
    bmin, bmax = [1e9] * 3, [-1e9] * 3
    ntris = 0
    # Layers 1, 2, 4 are detail levels of the same chunks (4_X is a simplified copy of 1_X: ~1/3 of the triangles, flat proxies for steps,
    # bowls and rails). The DS draws one of them depending on distance; drawing all of them put flat proxy slabs over the real geometry.
    # Keep, per chunk name, only the most detailed layer present (1, then 2, then 4); other layers (7 = backdrop, ...) are kept as is.
    all_oct = sorted(glob.glob(lvl + "*.oct"))
    present = {}
    for f in all_oct:
        base = os.path.basename(f)
        if base[0] in "124" and base[1] == "_":
            present.setdefault(base[2:].lower(), set()).add(base[0])

    def keep_chunk(f):
        base = os.path.basename(f)
        if base[0] in "124" and base[1] == "_":
            return base[0] == min(present[base[2:].lower()], key="124".index)
        return True
    skipped = [os.path.basename(f) for f in all_oct if not keep_chunk(f)]
    print("  detail layers: skipping %d simplified chunks (%s ...)" % (len(skipped), ", ".join(skipped[:3])))
    for f in all_oct:
        if not keep_chunk(f):
            continue
        background = 1 if os.path.basename(f).startswith("7_") else 0
        b = open(f, "rb").read()
        S, O = octm.scale(b), octm.origin(b)
        for lf in octm.leaves(b, nverts_pool):
            for p in lf["polys"]:
                collpolys.add((p[0], p[1], p[2], p[3], p[4]))
            for tid, tris in octm.batches(b, lf):
                ti = tex_for(tid)
                for tri in tris:
                    ntris += 1
                    corners = []
                    for v in tri:
                        pos = tuple(O[a] + v[0][a] * S for a in range(3))
                        if not background:
                            for a in range(3):
                                bmin[a] = min(bmin[a], pos[a]); bmax[a] = max(bmax[a], pos[a])
                        corners.append((pos, v[1], v[2]))
                    # the PSP GPU drops/garbles big polygons that cross the near plane: keep triangles small
                    for small in subdivide(corners, MAX_EDGE if not background else 1e9):   # far backdrop: leave as is
                        raw.append((ti, background, small))
    for ti, bg_, t in raw:
        per_batch[(ti, bg_)].extend(t)
    # ---- vertex buffer, grouped by (texture, background)
    verts, batches = bytearray(), []
    nv = 0
    for (ti, bg) in sorted(per_batch, key=lambda k: (k[1], k[0])):
        vs = per_batch[(ti, bg)]
        w = textures[ti]["w"] if ti != 0xFFFF else 1
        h = textures[ti]["h"] if ti != 0xFFFF else 1
        # uv were in texels of the ORIGINAL texture; stored width may be tiled wider
        sw = textures[ti]["src_w"] if ti != 0xFFFF else 1
        for pos, uv, col in vs:
            r, g, bl = (int(round(min(max(c, 0.0), 1.0) * 255)) for c in col)
            u, v_ = (uv[0] / w, uv[1] / h) if ti != 0xFFFF else (0.0, 0.0)
            verts += struct.pack(VERT_FMT, u, v_, r | (g << 8) | (bl << 16) | (255 << 24), *pos)
        batches.append(struct.pack(BATCH_FMT, ti, bg, nv, len(vs), 0))
        nv += len(vs)
    # ---- collision
    cverts = list(octm.load_cmn(lvl + "verts.cmn"))
    if collfix:                                               # walls that are drawn but not solid in the original collision data
        import collfix as cf
        tname = {i: os.path.basename(k) for k, i in tex_index.items()}
        cand = [(np.array([c[0] for c in t], float), tname.get(ti, "")) for ti, bg_, t in raw if not bg_ and ti != 0xFFFF]
        nvx, npl, _ = cf.synthesize_walls(cverts, sorted(collpolys), cand)
        cverts += nvx
        collpolys.update(npl)
        nvx, npl = cf.synthesize_edge_walls(cverts, sorted(collpolys))
        cverts += nvx
        collpolys.update(npl)
    cv = b"".join(struct.pack("<3f", *v) for v in cverts)
    cp = b"".join(struct.pack("<4HI", *p) for p in sorted(collpolys))
    # ---- layout
    off = align16(struct.calcsize(HDR_FMT))
    off_tex = off
    off = align16(off + len(textures) * struct.calcsize(TEX_FMT))
    tex_blobs, tex_entries = [], []
    for t in textures:
        clut_off = off; off = align16(off + len(t["clut"]))
        pix_off = off; off = align16(off + len(t["pix"]))
        tex_entries.append(struct.pack(TEX_FMT, t["w"], t["h"], t["bpp"], t["swz"], t["alpha"], 0, clut_off, pix_off, t["n"], 0))
        tex_blobs.append((clut_off, t["clut"])); tex_blobs.append((pix_off, t["pix"]))
    off_batch = off; off = align16(off + len(batches) * struct.calcsize(BATCH_FMT))
    off_verts = off; off = align16(off + len(verts))
    off_collv = off; off = align16(off + len(cv))
    off_collp = off; off = align16(off + len(cp))
    # ---- rails (collision flag 256 objects, tools/rails.py): table of (first point, npoints) then the points, f32 x,y,z
    import rails as rl
    import ledges as ld
    rail_list = rl.extract_rails(cverts, sorted(collpolys))
    kinds = [0] * len(rail_list)
    ledge_list, _ = ld.extract_ledges(cverts, sorted(collpolys))     # kind 1: the top edges of blocks (top surface on the left of the direction of travel)
    rail_list += ledge_list
    kinds += [1] * len(ledge_list)
    # the original game's own grind lines (track1.pvs, tools/pvs_rails.py) are the truth: they replace the heuristic ones that lie on them; heuristic rails / ledges that
    # no original line covers stay as "extras" (id bit 0x8000) so nothing that could be ground before stops working
    import pvs_rails as pr, campaign_pvs as cpvs
    pvs = None
    try:
        pvs = cpvs.parse(cpvs.level_pvs(level))
    except Exception as e:                                    # no / unreadable object table: the heuristic rails alone, no ids
        print("  WARNING: no usable track1.pvs object table (%s): heuristic rails only" % e)
    rail_ids = [0x8000] * len(rail_list)
    if pvs:
        floor = pr.Floor(cverts, sorted(collpolys))
        orig = []
        for n, pts in pvs["splines"]:
            if "grind" not in n:
                continue
            k, q, _ = pr.classify_spline(pts, floor)
            orig.append((int(n.rsplit("_", 1)[1]), k, np.asarray(q, np.float32)))
        orig.sort(key=lambda o: o[0])
        import pvs_rail_match as pm
        os_ = [pm.resample(o[2]) for o in orig]
        keep = []
        for i, r in enumerate(rail_list):
            rs = pm.resample(np.asarray(r, float))
            cov = max([float((pm.dist_to_poly(rs, o) <= 0.6).mean()) for o in os_] or [0.0])
            if cov < 0.2:
                keep.append(i)
        print("  rails: %d original grind lines (%d rails, %d ledges) + %d heuristic extras" % (len(orig), sum(1 for o in orig if o[1] == 0), sum(1 for o in orig if o[1] == 1), len(keep)))
        rail_list = [o[2] for o in orig] + [rail_list[i] for i in keep]
        kinds = [o[1] for o in orig] + [kinds[i] for i in keep]
        rail_ids = [o[0] for o in orig] + [0x8000] * len(keep)
    # ---- surface materials (tools/materials.py): one byte per collision polygon, and one 2-bit code per rail / ledge in bits 24-25 of its npoints word
    import materials as mt
    names = [""] * len(textures)
    for k, i in tex_index.items():
        names[i] = os.path.basename(k)
    tri_array = np.array([[c[0] for c in t] for ti, bg_, t in raw if not bg_], float)
    tri_tex = [ti if ti != 0xFFFF else -1 for ti, bg_, t in raw if not bg_]
    polymat, rail_mat = mt.build(cverts, sorted(collpolys), tri_array, tri_tex, names, rail_list, kinds)
    off_polymat = off; off = align16(off + len(polymat))
    rail_tab = b"".join(struct.pack("<II", sum(len(r) for r in rail_list[:i]), len(r) | (kinds[i] << 16) | (rail_mat[i] << 24)) for i, r in enumerate(rail_list))
    rail_pts = b"".join(r.astype("<f4").tobytes() for r in rail_list)
    off_rails = off; off = align16(off + len(rail_tab) + len(rail_pts))
    rail_idb = b"".join(struct.pack("<H", v) for v in rail_ids)               # per rail: the original game's grind line number (1-based), 0x8000 = a heuristic extra
    off_railids = off; off = align16(off + len(rail_idb))
    objb = b""; pathb = b""
    if pvs:                                                   # named objects (start grids, challenge beams, gates, zones, bounds, spawns ...) and the non-grind paths (progress spline ...)
        objs = [(n, x, y, z) for n, x, y, z in pvs["objs"]]
        names_ = [o[0] for o in objs]
        if "start_dir_1" not in names_ and names_.count("start_dir_2") == 2:      # SouthBank's data names slot 1's direction point "start_dir_2" (the pairs line up: it lies 11 m ahead of start_pos_1)
            i = names_.index("start_dir_2")
            objs[i] = ("start_dir_1",) + objs[i][1:]
            print("  NOTE: the first start_dir_2 is slot 1's (duplicate name in the original data), renamed start_dir_1")
        objb = struct.pack("<I", len(objs)) + b"".join(struct.pack("<32s3f", n.encode("latin-1")[:31], x, y, z) for n, x, y, z in objs)
        paths = [(n, p) for n, p in pvs["splines"] if "grind" not in n and not n.startswith("traffic")]
        pathb = struct.pack("<I", len(paths)) + b"".join(struct.pack("<32sI", n.encode("latin-1")[:31], len(p)) + b"".join(struct.pack("<3f", *q) for q in p) for n, p in paths)
    else:
        objb = struct.pack("<I", 0); pathb = struct.pack("<I", 0)
    off_objs = off; off = align16(off + len(objb))
    off_paths = off; off = align16(off + len(pathb))
    start = pick_start(cverts, collpolys, (bmin, bmax))
    hdr = struct.pack(HDR_FMT, b"SKLV", 4, len(textures), len(batches), nv, len(cverts), len(collpolys),
                      off_tex, off_batch, off_verts, off_collv, off_collp, *bmin, *bmax, *start,
                      len(rail_list), sum(len(r) for r in rail_list), off_rails, off_polymat, off_railids, off_objs, off_paths, 0)
    buf = bytearray(off)
    buf[:len(hdr)] = hdr
    pos = off_tex
    for e in tex_entries:
        buf[pos:pos + len(e)] = e; pos += len(e)
    for o, d in tex_blobs:
        buf[o:o + len(d)] = d
    for i, bt in enumerate(batches):
        buf[off_batch + i * 16: off_batch + i * 16 + 16] = bt
    buf[off_verts:off_verts + len(verts)] = verts
    buf[off_collv:off_collv + len(cv)] = cv
    buf[off_collp:off_collp + len(cp)] = cp
    buf[off_polymat:off_polymat + len(polymat)] = polymat.tobytes()
    buf[off_rails:off_rails + len(rail_tab)] = rail_tab
    buf[off_rails + len(rail_tab):off_rails + len(rail_tab) + len(rail_pts)] = rail_pts
    buf[off_railids:off_railids + len(rail_idb)] = rail_idb
    buf[off_objs:off_objs + len(objb)] = objb
    buf[off_paths:off_paths + len(pathb)] = pathb
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, "wb").write(buf)
    if names_sidecar:                                         # for the analysis tools: <out>.names = texture index -> file name
        names = [""] * len(textures)
        for k, i in tex_index.items():
            names[i] = os.path.basename(k)
        open(out_path + ".names", "w", encoding="utf-8").write("\n".join(names) + "\n")
    print("%s: %d triangles, %d vertices, %d batches, %d textures (%d untextured tris)" % (
        level, ntris, nv, len(batches), len(textures),
        sum(per_batch[k].__len__() // 3 for k in per_batch if k[0] == 0xFFFF)))
    print("  collision: %d vertices, %d polygons" % (len(cverts), len(collpolys)))
    print("  file %d bytes (%.2f MB); bounds %s .. %s" % (len(buf), len(buf) / 1048576, [round(x) for x in bmin], [round(x) for x in bmax]))
    return textures


if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2], collfix="--no-collfix" not in sys.argv, names_sidecar="--names" in sys.argv)
