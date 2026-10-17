#!/bin/bash
# Compile src/*.c with -Wall -Wextra (syntax/semantic warnings only, nothing is linked).
export PSPDEV="$HOME/pspdev"
export PATH="$PATH:$PSPDEV/bin"
cd "$(dirname "$0")/.."
SDK="$(psp-config --pspsdk-path)"
for f in src/*.c; do
  echo "== $f"
  psp-gcc -std=gnu11 -O2 -G0 -Wall -Wextra -Wshadow -Wformat=2 -I"$SDK/include" -c "$f" -o /tmp/warn_check.o 2>&1 | head -60
done
echo "warn-check finished"
