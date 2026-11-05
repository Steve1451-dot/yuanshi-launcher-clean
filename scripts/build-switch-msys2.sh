#!/usr/bin/env bash
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="/mingw64/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

cd "$(dirname "$0")/.."

detect_build_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
    else
        printf '4\n'
    fi
}

opennow_build_jobs="${OPENNOW_BUILD_JOBS:-$(detect_build_jobs)}"
opennow_version="${OPENNOW_VERSION:-$(bash scripts/read-version.sh)}"

# One source of truth for the CloudGenshin label. read-version.sh validates the
# OpenNOW pair; the CloudGenshin pair is derived from the header here and passed
# to CMake, so a stale CMake cache value cannot silently win.
read_cloudgenshin_version() {
    sed -n 's/^#define CLOUDGENSHIN_APP_VERSION "\([^"]*\)"$/\1/p' app/src/app_version.hpp
}
cloudgenshin_version="${CLOUDGENSHIN_VERSION:-$(read_cloudgenshin_version)}"
opennow_build_dir="${OPENNOW_BUILD_DIR:-build/switch}"
if [[ ! "$cloudgenshin_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Invalid CLOUDGENSHIN_APP_VERSION "%s" in app/src/app_version.hpp.\n' \
        "$cloudgenshin_version" >&2
    exit 1
fi

cmake -B "$opennow_build_dir" -G Ninja -DOPENNOW_VERSION="$opennow_version" -DCLOUDGENSHIN_VERSION="$cloudgenshin_version" -DCLOUDGENSHIN_DEBUG_SYMBOLS="${CLOUDGENSHIN_DEBUG_SYMBOLS:-OFF}"
cmake --build "$opennow_build_dir" --target cjson mbedtls srtp2 usrsctp -j"$opennow_build_jobs"
cmake --build "$opennow_build_dir" --target yuanshi-launcher.nro -j"$opennow_build_jobs"

# Verify the artifact instead of trusting the build order. The header used to be
# edited while a build was already running, which produced a correctly linked
# NRO carrying the previous version label. Catch both that and a broken cache.
nro="$opennow_build_dir/yuanshi-launcher.nro"
if [[ ! -f "$nro" ]]; then
    printf 'Build did not produce %s.\n' "$nro" >&2
    exit 1
fi
if [[ -z "${CLOUDGENSHIN_VERSION:-}" ]]; then
    final_version="$(read_cloudgenshin_version)"
    if [[ "$final_version" != "$cloudgenshin_version" ]]; then
        printf 'Version changed during the build (started %s, header now %s). Rebuild required.\n' \
            "$cloudgenshin_version" "$final_version" >&2
        exit 1
    fi
fi
bash scripts/verify-cloudgenshin-version.sh "$nro" "$cloudgenshin_version"

printf '\nBuilt: %s (CloudGenshin %s)\n' "$nro" "$cloudgenshin_version"
