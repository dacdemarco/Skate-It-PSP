#!/usr/bin/env python3
"""Extract an NDS ROM: header, arm9/arm7 binaries, overlays, banner and the
whole file tree (FNT/FAT). No dependencies.

usage: extract_nds.py <rom.nds> <outdir>
"""
import os
import struct
import sys


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def main(rom_path, out):
    rom = open(rom_path, "rb").read()
    os.makedirs(out, exist_ok=True)

    def dump(rel, data):
        p = os.path.join(out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)

    dump("header.bin", rom[:0x4000])
    a9_off, a9_size = u32(rom, 0x20), u32(rom, 0x2C)
    a7_off, a7_size = u32(rom, 0x30), u32(rom, 0x3C)
    dump("arm9.bin", rom[a9_off:a9_off + a9_size])
    dump("arm7.bin", rom[a7_off:a7_off + a7_size])

    banner = u32(rom, 0x68)
    if banner:
        dump("banner.bin", rom[banner:banner + 0xA00])

    fnt_off = u32(rom, 0x40)
    fat_off, fat_size = u32(rom, 0x48), u32(rom, 0x4C)

    def fat(i):
        return u32(rom, fat_off + i * 8), u32(rom, fat_off + i * 8 + 4)

    # overlays (referenced by id into the FAT, usually not named in the FNT)
    for tag, tbl_off, tbl_size in (("ov9", u32(rom, 0x50), u32(rom, 0x54)),
                                   ("ov7", u32(rom, 0x58), u32(rom, 0x5C))):
        for i in range(tbl_size // 32):
            e = tbl_off + i * 32
            ov_id, ram = u32(rom, e), u32(rom, e + 4)
            file_id = u32(rom, e + 0x18)
            s, en = fat(file_id)
            dump("overlays/%s_%04d_ram%08X.bin" % (tag, ov_id, ram), rom[s:en])

    # file tree
    n_dirs = u16(rom, fnt_off + 6)
    listing = []

    def walk(dir_id, path):
        entry = fnt_off + (dir_id & 0xFFF) * 8
        sub_off, file_id = u32(rom, entry), u16(rom, entry + 4)
        p = fnt_off + sub_off
        while True:
            t = rom[p]
            p += 1
            if t == 0:
                break
            ln = t & 0x7F
            name = rom[p:p + ln].decode("latin-1")
            p += ln
            if t & 0x80:
                sub_id = u16(rom, p)
                p += 2
                walk(sub_id, path + name + "/")
            else:
                s, en = fat(file_id)
                rel = "files/" + path + name
                dump(rel, rom[s:en])
                listing.append((file_id, en - s, path + name))
                file_id += 1

    walk(0xF000, "")
    with open(os.path.join(out, "filelist.txt"), "w") as f:
        for fid, size, name in sorted(listing):
            f.write("%5d %9d %s\n" % (fid, size, name))
    print("dirs:", n_dirs, "files:", len(listing), "of", fat_size // 8, "FAT entries")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
