#!/bin/sh
# Configures (first time) and builds into build/, then copies the DLL and fm24ctl.exe to bin/.
#   scripts/build.sh            Release
#   BUILD_TYPE=Debug scripts/build.sh
set -e
. "$(dirname "$0")/env.sh"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
	"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
		-DCMAKE_MAKE_PROGRAM="$(cygpath -m "$NINJA")" \
		-DCMAKE_TOOLCHAIN_FILE="$(cygpath -m "$ROOT/cmake/mingw64.cmake")" \
		-DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
fi
"$CMAKE" --build "$BUILD_DIR"

mkdir -p "$ROOT/bin"
cp "$BUILD_DIR"/fm24_harness_p*.dll "$BUILD_DIR/fm24ctl.exe" "$ROOT/bin/" 2>/dev/null || {
	echo "note: could not copy into bin/ (a DLL with this protocol is probably loaded in fm.exe;"
	echo "      bump HARNESS_PROTOCOL in src/common/protocol.h to build a new one)"
}
ls "$ROOT/bin"
