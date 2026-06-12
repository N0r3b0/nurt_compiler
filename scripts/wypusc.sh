#!/usr/bin/env bash
# wypusc.sh - the Nurt build-and-run pipeline:
#   .nrt --(nurtc)--> .asm --(nasm -f elf64)--> .o --(gcc)--> executable --> run
#
# Usage: scripts/wypusc.sh <file.nrt>
#
# Artifacts land in build/wyjscie/<name>.{asm,o} and build/wyjscie/<name>.
# The script exits with the compiled program's own exit code.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "uzycie: $(basename "$0") <plik.nrt>" >&2
    exit 2
fi

SRC="$1"
if [[ ! -f "$SRC" ]]; then
    echo "wypusc: blad: nie ma pliku '$SRC'" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NURTC="$ROOT/build/nurtc"

# Build the compiler itself if it is not there yet.
if [[ ! -x "$NURTC" ]]; then
    echo "== wypusc: buduje nurtc =="
    cmake -S "$ROOT" -B "$ROOT/build" -DNURT_BUILD_TESTS=OFF >/dev/null
    cmake --build "$ROOT/build" --target nurtc >/dev/null
fi

OUT_DIR="$ROOT/build/wyjscie"
mkdir -p "$OUT_DIR"

STEM="$(basename "${SRC%.nrt}")"
ASM="$OUT_DIR/$STEM.asm"
OBJ="$OUT_DIR/$STEM.o"
EXE="$OUT_DIR/$STEM"

echo "== wypusc: nurtc $SRC -> $ASM =="
"$NURTC" "$SRC" -o "$ASM"

echo "== wypusc: nasm -f elf64 -> $OBJ =="
nasm -f elf64 "$ASM" -o "$OBJ"

echo "== wypusc: gcc -no-pie -> $EXE =="
gcc -no-pie "$OBJ" -o "$EXE"

echo "== wypusc: uruchamiam $STEM =="
status=0
"$EXE" || status=$?
echo "== wypusc: kod wyjscia: $status =="
exit "$status"
