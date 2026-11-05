#!/usr/bin/env bash
# Compile the libpeer host checks with devkitA64.
#
# There is no native host C compiler on this machine (the MSYS2 mingw64
# toolchain is not installed), so the checks that only need libpeer sources are
# compiled as Switch objects with devkitA64. They catch type errors and
# regressions in the parsing paths even though the binaries cannot run here.
#
# Usage: bash scripts/build-libpeer-checks-devkita64.sh [test-name ...]
#        (defaults to peer_dtls_setup)
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="/mingw64/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"

cd "$(dirname "$0")/.."
# Windows-style absolute paths: the toolchain does not see the D: drive at /d.
repo="$(pwd -W 2>/dev/null || pwd)"
out="${OPENNOW_CHECK_DIR:-.opennow-msys-tmp/libpeer-checks}"
mkdir -p "$out"

names=("$@")
if [[ ${#names[@]} -eq 0 ]]; then
    names=(peer_dtls_setup)
fi

compat_flags=("-isystem" "$DEVKITPRO/libnx/include")
# libsrtp headers are generated into the build tree by the main build.
if [[ -d "$repo/build/switch/dist/include" ]]; then
    compat_flags+=("-I" "$repo/build/switch/dist/include")
fi

for name in "${names[@]}"; do
    unity="$out/unity_${name}.c"
    printf '#include "%s/tests/%s_test.c"\n#include "%s/extern/libpeer/src/rtcp.c"\n#include "%s/extern/libpeer/src/sdp.c"\n' \
        "$repo" "$name" "$repo" "$repo" > "$unity"
    echo "compiling ${name}"
    aarch64-none-elf-gcc \
        -std=gnu11 -O2 -fwhole-program -ffunction-sections -fdata-sections \
        -Werror=implicit-function-declaration -Werror=return-type \
        -DCONFIG_USE_USRSCTP=0 -D__SWITCH__ \
        -I"$repo/extern/libpeer/src" \
        -I"$repo/extern/libpeer/third_party/mbedtls/include" \
        "${compat_flags[@]}" \
        -c "$unity" -o "$out/${name}.o" || exit 1
done

echo "PASS libpeer checks compiled (object only; no host runner on this machine)"
