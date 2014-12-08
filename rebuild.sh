#!/bin/sh

set -eux
BASE="$(dirname "$0")"
BASE="$(cd "$BASE" && pwd -P)"

TARGET=
TARGET="$TARGET native"
TARGET="$TARGET mingw"

for x in $TARGET; do
	CMAKE_OPT="-DCMAKE_BUILD_TYPE=Debug -DLWS_WITH_SSL=0"

	case "$x" in
	native)
		BUILDDIR="$BASE/build"
		CMAKE_OPT="$CMAKE_OPT \
-DLWS_WITH_LIBEV=1 \
"
		;;
	mingw)
		BUILDDIR="$BASE/build-win"
		CMAKE_OPT="$CMAKE_OPT \
-DCMAKE_TOOLCHAIN_FILE=$BASE/cross-ming.cmake \
"
		;;
	*)
		echo "$x: unknown target" >&2
		continue
		;;
	esac

	if [ ! -s "$BUILDDIR/Makefile" ]; then
		mkdir -p "$BUILDDIR"

		cd "$BUILDDIR"
		cmake "$BASE" $CMAKE_OPT
		cd - > /dev/null
	fi

	make -C "$BUILDDIR" "$@"
done
