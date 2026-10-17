#!/usr/bin/env python3
"""Basis-independent board motion of a pose clip: AZ = unwrapped heading of the board's nose in the horizontal plane (degrees; the shuvit part; undefined while the nose points up / down),
ROLL = unwrapped roll of the deck normal about the board's long axis relative to the world up (the flip part; + = the board's +x = TOE side edge goes up first),
tilt extremes = how steeply the nose points up / down. Frames are the clip's keyframes."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, pose_sheet as ps, clip_survey as cs


def unwrap(a):
    out = [a[0]]
    for v in a[1:]:
        d = (v - out[-1] + 180.0) % 360.0 - 180.0
        out.append(out[-1] + d)
    return np.array(out)


def measure(n):
    fr = cs.board_frames(n)
    az, roll, tilt = [], [], []
    U = np.array([0.0, 0.0, 1.0])
    for q, p in fr:
        R = ps.inv(q)                       # board local -> model space (z is up)
        x, y, z = R[:, 0], R[:, 1], R[:, 2]  # width, deck normal, long (nose)
        az.append(np.degrees(np.arctan2(z[1], z[0])))
        up_perp = U - np.dot(U, z) * z
        nrm = np.linalg.norm(up_perp)
        if nrm < 0.2:
            roll.append(roll[-1] if roll else 0.0)
        else:
            up_perp /= nrm
            roll.append(np.degrees(np.arctan2(np.dot(np.cross(up_perp, y), z), np.dot(up_perp, y))))
        tilt.append(np.degrees(np.arcsin(np.clip(z[2], -1, 1))))
    az = unwrap(az); roll = unwrap(roll)
    return az - az[0], roll - roll[0], np.array(tilt)


if __name__ == "__main__":
    for n in [int(a) for a in sys.argv[1:]]:
        az, roll, tilt = measure(n)
        print("%3d frames %2d  AZ end %+7.1f  ROLL end %+7.1f  tilt min %+5.0f max %+5.0f" % (n, len(az), az[-1], roll[-1], tilt.min(), tilt.max()))
