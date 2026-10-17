#!/usr/bin/env python3
"""Reader for Nintendo DS SDAT sound archives (DATA/AUDIO/sound_data.sdat).
Layout: header (SYMB/INFO/FAT/FILE block offsets), SYMB = names, INFO = records per kind, FAT = (offset, size) per file id.
Kinds, in table order: SEQ (sseq sequences), SEQARC, BANK (sbnk instrument banks), WAVEARC (swar wave archives), PLAYER, GROUP, STRMPLAYER, STRM (streamed audio).
usage: sdat.py <sound_data.sdat>   lists every named record with its file id and size"""
import sys, struct

KINDS = ["SEQ", "SEQARC", "BANK", "WAVEARC", "PLAYER", "GROUP", "STRMPLAYER", "STRM"]


class Sdat:
    def __init__(self, path):
        self.b = b = open(path, "rb").read()
        assert b[:4] == b"SDAT"
        sym, _, info, _, fat, _, fil, _ = struct.unpack_from("<8I", b, 16)
        self.sym, self.info, self.fat, self.fil = sym, info, fat, fil
        n = struct.unpack_from("<I", b, fat + 8)[0]
        self.files = [struct.unpack_from("<II", b, fat + 12 + 16 * i) for i in range(n)]       # (offset from file start, size)
        self.names = {}
        self.records = {}
        so = struct.unpack_from("<8I", b, sym + 8)
        io = struct.unpack_from("<8I", b, info + 8)
        for k, kind in enumerate(KINDS):
            if not so[k] or not io[k]:
                continue
            cnt = struct.unpack_from("<I", b, sym + so[k])[0]
            names = []
            for i in range(cnt):
                o = struct.unpack_from("<I", b, sym + so[k] + 4 + 4 * i)[0]
                if kind == "SEQARC":
                    names.append("")          # nested table, unused here
                    continue
                e = b.index(b"\0", sym + o)
                names.append(b[sym + o:e].decode("ascii", "replace") if o else "")
            cnt2 = struct.unpack_from("<I", b, info + io[k])[0]
            recs = []
            for i in range(cnt2):
                o = struct.unpack_from("<I", b, info + io[k] + 4 + 4 * i)[0]
                recs.append(o and b[info + o:info + o + 32])
            self.names[kind] = names
            self.records[kind] = recs

    def file(self, fid):
        o, s = self.files[fid]
        return self.b[o:o + s]

    def file_id(self, kind, idx):
        r = self.records[kind][idx]
        return struct.unpack_from("<H", r, 0)[0] if r else None


if __name__ == "__main__":
    s = Sdat(sys.argv[1])
    print("files in FAT:", len(s.files))
    for kind in KINDS:
        if kind not in s.records:
            continue
        print("==", kind, len(s.records[kind]))
        for i, r in enumerate(s.records[kind]):
            if not r:
                continue
            fid = struct.unpack_from("<H", r, 0)[0]
            nm = s.names[kind][i] if i < len(s.names[kind]) else ""
            sz = s.files[fid][1] if kind != "PLAYER" and kind != "GROUP" and fid < len(s.files) else 0
            extra = ""
            if kind == "SEQ":
                extra = "bank %d vol %d" % (struct.unpack_from("<H", r, 4)[0], r[6])
            elif kind == "BANK":
                extra = "waves " + str(struct.unpack_from("<4H", r, 4))
            print("  %3d fid %3d size %8d  %s %s" % (i, fid, sz, nm, extra))
