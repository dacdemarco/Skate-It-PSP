#!/usr/bin/env python3
"""Contact sheets of the gameplay pose library by animdb group. usage: group_sheet.py <out_prefix> [yaw]
Writes <out_prefix>_groups.png (one pose per animdb group, labelled pose index) and, with --all <first> <count>, a sheet of consecutive poses."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pose_sheet as ps

b = open("extracted/unpacked/DATA/SKATERS/ARC_S/animdb.adb", "rb").read()
g = [struct.unpack_from("<14H", b, 8 + 28 * i)[9] for i in range(376)]
out = sys.argv[1]
yaw = 90
if len(sys.argv) > 3 and sys.argv[2] == "--all":
    first, count = int(sys.argv[3]), int(sys.argv[4])
    tiles = [(i, 2) for i in range(first, min(376, first + count))]
    ps.sheet(tiles, out + "_%d.png" % first, yaw=yaw, cols=8)
else:
    firsts = [i for i in range(376) if i == 0 or g[i] != g[i - 1]]
    ps.sheet([(i, 2) for i in firsts], out + "_groups.png", yaw=yaw, cols=8)
    print("group first poses:", firsts)
