#!/bin/sh
# Builds unstripped Release binaries into build/, then installs stripped copies into bin/.
#   scripts/build.sh
set -e
. "$(dirname "$0")/env.sh"

"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_MAKE_PROGRAM="$(cygpath -m "$NINJA")" \
	-DCMAKE_TOOLCHAIN_FILE="$(cygpath -m "$ROOT/cmake/mingw64.cmake")" \
	-DCMAKE_BUILD_TYPE=Release \
	-DFM24_BUILD_TESTS=ON
"$CMAKE" --build "$BUILD_DIR"

protocol=$(sed -n 's/^#define HARNESS_PROTOCOL L"\([0-9.]*\)"$/\1/p' "$ROOT/src/common/protocol.h")
dll="fm24-harness-$protocol.dll"

"$CMAKE" --install "$BUILD_DIR" --config Release --prefix "$(cygpath -m "$ROOT/bin")"
# CMake's --strip does not propagate strip failures.
strip=$(sed -n 's/^CMAKE_STRIP:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt")
"$strip" --strip-unneeded "$ROOT/bin/fm24ctl.exe" "$ROOT/bin/fm24_tests.exe"

# fm24ctl.exe carries the DLL, so a loose copy in bin/ would override it (see inject.c); older
# protocols' DLLs in build/ are never injected again. Deleting one still loaded in fm.exe fails; it
# goes on a later build once the game has restarted.
for old in "$BUILD_DIR"/fm24-harness-*.dll "$BUILD_DIR"/fm24_harness_p*.dll; do
	[ -e "$old" ] && [ "$(basename "$old")" != "$dll" ] && { rm -f "$old" 2>/dev/null || true; }
done
for old in "$ROOT"/bin/fm24-harness-*.dll "$ROOT"/bin/fm24_harness_p*.dll; do
	[ -e "$old" ] && { rm -f "$old" 2>/dev/null || true; }
done
ls "$ROOT/bin"
