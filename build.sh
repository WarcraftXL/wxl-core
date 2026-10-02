#!/usr/bin/env bash
# Build + deploy WarcraftXL from Linux: WarcraftXL.dll, d3d9.dll, wxl-patcher.exe and the extensions,
# cross-compiled with the clang mingw-w64 toolchain in cmake/toolchain/. The Linux counterpart of
# build.ps1: same options, same deploy, Windows PE output.
#
#   ./build.sh [--client <dir>] [--config Release|Debug] [--clean] [--autopatch] [--target <name>]
#
# --client is needed once; it is then read back from the CMake cache. --autopatch runs wxl-patcher.exe
# on the client's Wow.exe through wine when wine is installed.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$root/build/mingw-x86"

config="Release"
client=""
clean=0
autopatch=0
target=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --client)    client="$2"; shift 2 ;;
        --config)    config="$2"; shift 2 ;;
        --clean)     clean=1; shift ;;
        --autopatch) autopatch=1; shift ;;
        --target)    target="$2"; shift 2 ;;
        -h|--help)   sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cached_client() {
    local cache="$1/CMakeCache.txt"
    [[ -f "$cache" ]] && sed -n 's/^CLIENT_PATH:PATH=//p' "$cache" | head -1 || true
}

reconfigure=0
if [[ -n "$client" ]]; then
    client="$(cd "$client" && pwd)"
    reconfigure=1
else
    client="$(cached_client "$build_dir")"
fi
if [[ -z "$client" ]]; then
    echo "No client path is known. Run once with --client <client folder> (it is stored in the CMake cache)." >&2
    exit 1
fi
[[ -d "$client" ]] || { echo "Client path not found: $client" >&2; exit 1; }

echo "Project : $root"
echo "Client  : $client"
echo "Config  : $config"
echo

if [[ $clean -eq 1 && -d "$build_dir" ]]; then
    echo "Clean $build_dir"
    rm -rf "$build_dir"
fi
if [[ $clean -eq 1 || $reconfigure -eq 1 || ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE="$config" \
        -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain/mingw-i686.cmake" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCLIENT_PATH="$client"
fi

start=$SECONDS
echo "=== WarcraftXL.dll, d3d9.dll, wxl-patcher.exe, extensions (32-bit) ==="
if [[ -n "$target" ]]; then
    cmake --build "$build_dir" --target "$target" --parallel
else
    cmake --build "$build_dir" --parallel
fi
echo

if [[ $autopatch -eq 1 ]]; then
    patcher="$build_dir/wxl-patcher.exe"
    [[ -f "$patcher" ]] || patcher="$client/wxl-patcher.exe"
    [[ -f "$patcher" ]] || { echo "wxl-patcher.exe not found. Build first." >&2; exit 1; }
    [[ -f "$client/Wow.exe" ]] || { echo "Wow.exe not found: $client/Wow.exe" >&2; exit 1; }
    command -v wine >/dev/null || { echo "wine is needed to run wxl-patcher.exe from Linux." >&2; exit 1; }
    echo "=== AutoPatch ==="
    wine "$patcher" "$client/Wow.exe"
    echo
fi

echo "OK - build + deploy in $((SECONDS - start))s -> $client"
