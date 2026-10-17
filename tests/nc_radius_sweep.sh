#!/bin/bash
# How small can the near-camera clip radius be before visible area is lost? (informational; see docs/TECHNICAL.md)
cd "$(dirname "$0")/.."
for R in 6 10 14 18 22 26; do
  gcc -O2 -DTEST_RADIUS=${R}.0f -o /tmp/t_ncr tests/test_nearclip.c src/nearclip.c -lm || exit 1
  OUT=$(/tmp/t_ncr dist/SkateItPSP/data/amphitheatre.lvl)
  echo "radius $R m: $(echo "$OUT" | tail -1)  |  $(echo "$OUT" | grep 'nc_gather')"
done
