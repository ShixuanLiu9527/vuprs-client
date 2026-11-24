#!/usr/bin/env bash
# Build script for the VUPRS client.
# Run from Git Bash or MSYS2 Bash on Windows: ./build.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

QT_ROOT="${QT_ROOT:-D:/QT6.11/6.11.0/mingw_64}"
MINGW_BIN="${MINGW_BIN:-D:/QT6.11/Tools/mingw1310_64/bin}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

CLEAN=0
RUN_TESTS=1

usage() {
    cat <<EOF
Usage: ./build.sh [options]

Options:
  --clean       Remove the build directory before configuring
  --debug       Build with Debug instead of Release
  --no-tests    Do not run tests after the build
  -h, --help    Show this help

Environment variables:
  QT_ROOT       Qt mingw_64 directory (default: $QT_ROOT)
  MINGW_BIN     MinGW bin directory (default: $MINGW_BIN)
  BUILD_DIR     Build directory (default: $BUILD_DIR)
  BUILD_TYPE    Build type, Debug or Release (default: $BUILD_TYPE)
  JOBS          Parallel build jobs (default: $JOBS)
EOF
}

for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --debug) BUILD_TYPE=Debug ;;
        --no-tests) RUN_TESTS=0 ;;
        -h|--help) usage; exit 0 ;;
        *)
            printf 'Unknown option: %s\n\n' "$arg" >&2
            usage >&2
            exit 2
            ;;
    esac
done

command -v cmake >/dev/null 2>&1 || {
    echo "cmake not found. Install CMake >= 3.16 and add it to PATH." >&2
    exit 1
}

if [[ ! -f "$QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "Qt not found: $QT_ROOT" >&2
    echo "Set QT_ROOT to the Qt mingw_64 directory." >&2
    exit 1
fi

if [[ ! -x "$MINGW_BIN/g++.exe" || ! -x "$MINGW_BIN/mingw32-make.exe" ]]; then
    echo "MinGW not found: $MINGW_BIN" >&2
    echo "Set MINGW_BIN to the Qt Tools/mingw1310_64/bin directory." >&2
    exit 1
fi

# Git Bash needs POSIX paths for PATH; CMake arguments use Windows-style paths.
to_posix() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$1"
    else
        printf '%s' "$1"
    fi
}

to_native() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s' "$1"
    fi
}

export PATH="$(to_posix "$QT_ROOT/bin"):$(to_posix "$MINGW_BIN"):$PATH"

QT_ROOT_CMAKE="$(to_native "$QT_ROOT")"
MINGW_BIN_CMAKE="$(to_native "$MINGW_BIN")"
ROOT_DIR_CMAKE="$(to_native "$ROOT_DIR")"
BUILD_DIR_CMAKE="$(to_native "$BUILD_DIR")"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT_DIR_CMAKE" -B "$BUILD_DIR_CMAKE" -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_CMAKE" \
    -DCMAKE_MAKE_PROGRAM="$MINGW_BIN_CMAKE/mingw32-make.exe" \
    -DCMAKE_CXX_COMPILER="$MINGW_BIN_CMAKE/g++.exe" \
    -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR_CMAKE" --parallel "$JOBS"

if [[ $RUN_TESTS -eq 1 ]]; then
    ctest --test-dir "$BUILD_DIR_CMAKE" --output-on-failure
fi

printf '\nBuild complete: %s/vuprs_client.exe\n' "$BUILD_DIR"
printf 'Before launching directly, add Qt bin to PATH or run windeployqt.\n'
