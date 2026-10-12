#!/bin/sh
# Runs clang-tidy (MinGW-w64 build, with .clang-tidy) over every .c file, using build/'s compile
# database. Build first (scripts/build.sh) so build/compile_commands.json exists.
set -e
. "$(dirname "$0")/env.sh"

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
	echo "build/compile_commands.json missing: run scripts/build.sh first" >&2
	exit 1
fi
find "$ROOT/src" -name '*.c' | sort | xargs -d '\n' "$CLANG_TIDY" -p "$BUILD_DIR" --quiet "$@"
