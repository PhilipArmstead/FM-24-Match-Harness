#!/bin/sh
# Builds the DLL, runner and test suite into build/ in Release mode, then copies them to bin/.
#   scripts/build.sh
set -e
. "$(dirname "$0")/env.sh"

"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_MAKE_PROGRAM="$(cygpath -m "$NINJA")" \
	-DCMAKE_TOOLCHAIN_FILE="$(cygpath -m "$ROOT/cmake/mingw64.cmake")" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
	-DFM24_BUILD_TESTS=ON
"$CMAKE" --build "$BUILD_DIR"

protocol=$(sed -n 's/^#define HARNESS_PROTOCOL L"\([0-9.]*\)"$/\1/p' "$ROOT/src/common/protocol.h")
dll="fm24-harness-$protocol.dll"

mkdir -p "$ROOT/bin"
cp "$BUILD_DIR/fm24ctl.exe" "$ROOT/bin/" || echo "note: could not copy fm24ctl.exe (is it running?)"
cp "$BUILD_DIR/fm24_tests.exe" "$ROOT/bin/"
cp "$BUILD_DIR/$dll" "$ROOT/bin/" 2>/dev/null || {
	echo "note: could not copy $dll into bin/ (probably loaded in fm.exe;"
	echo "      bump HARNESS_PROTOCOL in src/common/protocol.h to build a new one)"
}
# Older protocols' DLLs (and the old fm24_harness_p<N>.dll names) are never injected again.
# Deleting one still loaded in fm.exe fails; it goes on a later build once the game has restarted.
for old in "$BUILD_DIR"/fm24-harness-*.dll "$ROOT"/bin/fm24-harness-*.dll \
	"$BUILD_DIR"/fm24_harness_p*.dll "$ROOT"/bin/fm24_harness_p*.dll; do
	[ -e "$old" ] && [ "$(basename "$old")" != "$dll" ] && { rm -f "$old" 2>/dev/null || true; }
done
ls "$ROOT/bin"
