#!/bin/bash
# helper: print the GU header doc around given patterns
export PSPDEV="$HOME/pspdev"
H=$(find "$PSPDEV" -name pspgu.h | head -1)
sed -n 430,446p "$H"
echo ----
sed -n 1150,1166p "$H"
