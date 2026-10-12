#!/bin/sh
# Formats every source under src/ with clang-format (22 or newer, which .clang-format requires).
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   only report files that are not formatted (exit 1 if any)
set -e
. "$(dirname "$0")/env.sh"

version=$("$CLANG_FORMAT" --version | sed -n 's/.*version \([0-9]*\).*/\1/p')
if [ -z "$version" ] || [ "$version" -lt 22 ]; then
	echo "clang-format 22 or newer is required (found: $("$CLANG_FORMAT" --version))" >&2
	exit 1
fi

if [ "$1" = "--check" ]; then
	sources | xargs -d '\n' "$CLANG_FORMAT" --dry-run --Werror
else
	sources | xargs -d '\n' "$CLANG_FORMAT" -i
fi
