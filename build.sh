#!/usr/bin/env bash
# Builds NoAutosave.dll with mingw-w64, runs the signature test against the
# installed game exe (if found), and packages dist/NoAutosave-<ver>.zip.
set -euo pipefail
cd "$(dirname "$0")"
VER=1.0.1
OUT=build
mkdir -p "$OUT" dist

echo "== building DLL"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ \
    -Wall -Wextra -Wno-missing-field-initializers \
    -o "$OUT/NoAutosave.dll" src/NoAutosave.cpp \
    -Wl,--exclude-all-symbols -lkernel32
x86_64-w64-mingw32-strip "$OUT/NoAutosave.dll"
x86_64-w64-mingw32-objdump -p "$OUT/NoAutosave.dll" | sed -n '/Export Address Table/,/^$/p' | grep -E '\[ *[0-9]+\]' || true

echo "== building Linux signature test"
g++ -std=c++17 -O2 -o "$OUT/test_scan" test/test_scan.cpp
EXE="${OBR_EXE:-$HOME/.local/share/Steam/steamapps/common/Oblivion Remastered/OblivionRemastered/Binaries/Win64/OblivionRemastered-Win64-Shipping.exe}"
if [ -f "$EXE" ]; then
    "$OUT/test_scan" "$EXE" "${@:-}"
else
    echo "game exe not found at $EXE, skipping scan test (set OBR_EXE=...)"
fi

echo "== packaging"
PKG="$OUT/pkg"; rm -rf "$PKG"; mkdir -p "$PKG/OBSE/Plugins"
cp "$OUT/NoAutosave.dll" src/NoAutosave.ini "$PKG/OBSE/Plugins/"
cp README.md "$PKG/NoAutosave-README.md"
python3 - "$PKG" "dist/NoAutosave-OBSE64-$VER.zip" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(src):
        for f in files:
            full = os.path.join(root, f)
            z.write(full, os.path.relpath(full, src))
PY
ls -la "dist/NoAutosave-OBSE64-$VER.zip"
