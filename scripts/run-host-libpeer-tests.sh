#!/usr/bin/env bash
# Native host checks for libpeer (Windows/MinGW build of the same sources).
#
# The repo's tests/dtls_host configures with WIN32 compat shims and needs a
# native C compiler. On this machine the MSYS2 mingw64 toolchain is empty; the
# WinLibs GCC installed through winget is used instead:
#
#   winget install BrechtSanders.WinLibs.POSIX.UCRT
#
# Usage: bash scripts/run-host-libpeer-tests.sh
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

# Locate the WinLibs mingw64 bin directory (the winget package path is per-user).
find_winget_mingw() {
    local win root found
    for win in "${LOCALAPPDATA:-}" "C:/Users/${USERNAME:-y}/AppData/Local"; do
        [[ -z "$win" ]] && continue
        # Normalise a Windows path (C:/...) into the msys2 form (/c/...).
        root="$(printf '%s' "$win" | sed -e 's#\\#/#g' -e 's#^\([A-Za-z]\):#/\L\1#')"
        found="$(ls -d "$root"/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT*/ 2>/dev/null | head -n 1 || true)"
        if [[ -n "$found" && -x "${found}mingw64/bin/gcc.exe" ]]; then
            printf '%s' "${found}mingw64/bin"
            return 0
        fi
    done
}

WINGET_MINGW="$(find_winget_mingw)"
if [[ -z "$WINGET_MINGW" ]]; then
    echo "WinLibs GCC not found. Install it with:" >&2
    echo "  winget install BrechtSanders.WinLibs.POSIX.UCRT" >&2
    exit 2
fi

export PATH="$WINGET_MINGW:/mingw64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"

cd "$(dirname "$0")/.."
# CMake wants a Windows path on this toolchain; .opennow-msys-tmp holds build
# artifacts only.
repo_dir="$(cygpath -m "$(pwd)" 2>/dev/null || pwd)"
build="$(cygpath -m "$(pwd)/.opennow-msys-tmp/host-tests")"

echo "gcc: $(gcc --version | head -n 1)"
rm -rf "$build"
cmake -G Ninja \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -S "$repo_dir/tests/dtls_host" -B "$build"
cmake --build "$build" --parallel 8
cd "$build"
ctest --output-on-failure
