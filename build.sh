#!/usr/bin/env bash
# VUPRS 上位机构建脚本，适用于 Windows Git Bash / MSYS2。
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
用法: ./build.sh [选项]

选项:
  --clean       删除构建目录后重新配置
  --debug       使用 Debug 构建（默认 Release）
  --no-tests    构建后不运行测试
  -h, --help    显示帮助

环境变量:
  QT_ROOT       Qt mingw_64 目录（默认: $QT_ROOT）
  MINGW_BIN     MinGW bin 目录（默认: $MINGW_BIN）
  BUILD_DIR     构建目录（默认: $BUILD_DIR）
  BUILD_TYPE    构建类型，Debug/Release（默认: $BUILD_TYPE）
  JOBS          并行编译任务数（默认: $JOBS）
EOF
}

for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --debug) BUILD_TYPE=Debug ;;
        --no-tests) RUN_TESTS=0 ;;
        -h|--help) usage; exit 0 ;;
        *)
            printf '未知参数: %s\n\n' "$arg" >&2
            usage >&2
            exit 2
            ;;
    esac
done

command -v cmake >/dev/null 2>&1 || {
    echo "未找到 cmake，请安装 CMake >= 3.16 并加入 PATH。" >&2
    exit 1
}

if [[ ! -f "$QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "未找到 Qt: $QT_ROOT" >&2
    echo "请设置 QT_ROOT 指向 Qt 的 mingw_64 目录。" >&2
    exit 1
fi

if [[ ! -x "$MINGW_BIN/g++.exe" || ! -x "$MINGW_BIN/mingw32-make.exe" ]]; then
    echo "未找到 MinGW: $MINGW_BIN" >&2
    echo "请设置 MINGW_BIN 指向 Qt Tools/mingw1310_64/bin 目录。" >&2
    exit 1
fi

# Git Bash 的 PATH 需要 POSIX 路径；CMake 参数使用 Windows 路径。
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
    echo "清理构建目录: $BUILD_DIR"
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

printf '\n构建完成: %s/vuprs_client.exe\n' "$BUILD_DIR"
printf '直接运行前请确保 Qt bin 在 PATH 中，或执行 windeployqt。\n'
