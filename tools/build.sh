#!/bin/bash
# Build EBOOT.PBP and stage the game folder in dist/SkateItPSP/ (EBOOT.PBP + README.txt, next to data/).
#
#   bash tools/build.sh
#
# Needs the PSP toolchain (https://pspdev.github.io, psp-cmake on the PATH or in $PSPDEV/bin; default $HOME/pspdev) and, for a
# playable folder, the data made from your own cartridge by
#   python tools/make_data.py "Skate It (USA) (En,Fr,De,Es,It,Nl).nds"
# On Windows run this inside WSL (Ubuntu); from PowerShell:  wsl -d Ubuntu -- bash /mnt/<drive>/path/to/repo/tools/build.sh
set -e
cd "$(dirname "$0")/.."

export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PATH:$PSPDEV/bin"
command -v psp-cmake >/dev/null || { echo "psp-cmake not found: install the PSP toolchain (pspdev) and set PSPDEV"; exit 1; }

mkdir -p build dist/SkateItPSP
cd build
psp-cmake .. > /dev/null          # every time (under a second): the XMB art in pbp/ (ICON0.png, PIC1.png ...) is found at configure time, so a file added later was missed by a make-only build
make -j"$(nproc)"
cp EBOOT.PBP ../dist/SkateItPSP/EBOOT.PBP
[ -f ../pbp/README.txt ] && cp ../pbp/README.txt ../dist/SkateItPSP/README.txt

if [ -d ../dist/SkateItPSP/data ] && [ -n "$(ls -A ../dist/SkateItPSP/data 2>/dev/null)" ]; then
    echo "dist/SkateItPSP: $(du -sh ../dist/SkateItPSP | cut -f1)"
else
    echo "warning: dist/SkateItPSP/data is empty - the EBOOT will not start without it: run  python tools/make_data.py \"<your Skate It .nds>\""
fi
ls -la EBOOT.PBP ../dist/SkateItPSP
