#!/usr/bin/env bash
# Host (MinGW) build of the small header-only policy tests that need no Switch
# headers. Complements scripts/run-host-libpeer-tests.sh, which compiles the
# libpeer C sources through tests/dtls_host.
#
# Usage: bash scripts/run-host-policy-tests.sh
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

find_winget_mingw() {
    local win root found
    for win in "${LOCALAPPDATA:-}" "C:/Users/${USERNAME:-y}/AppData/Local"; do
        [[ -z "$win" ]] && continue
        root="$(printf '%s' "$win" | sed -e 's#\\#/#g' -e 's#^\([A-Za-z]\):#/\L\1#')"
        found="$(ls -d "$root"/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT*/ 2>/dev/null | head -n 1 || true)"
        if [[ -n "$found" && -x "${found}mingw64/bin/g++.exe" ]]; then
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
export PATH="$WINGET_MINGW:$PATH"

cd "$(dirname "$0")/.."
build=".opennow-msys-tmp/host-policy-tests"
mkdir -p "$build"

# name:source pairs for tests that only need app/src headers.
tests=(
  "mihoyo_stream_diagnostics_policy_test:tests/mihoyo_stream_diagnostics_policy_test.cpp"
)

status=0

# Endfield wire transforms: the PackedData payload is encodeURI(text) bytes
# (jspb base64-decodes the btoa(...) argument before writing the field).
if g++ -std=c++20 -O1 -Wall -Wextra -I app/src \
    tests/endfield_wl_proto_test.cpp app/src/endfield/wl_proto.cpp \
    -o "$build/endfield_wl_proto_test.exe"; then
    if "$build/endfield_wl_proto_test.exe"; then
        echo "PASS endfield_wl_proto_test"
    else
        echo "FAIL endfield_wl_proto_test (non-zero exit)" >&2
        status=1
    fi
else
    echo "FAIL endfield_wl_proto_test (compile)" >&2
    status=1
fi

# Endfield protocol parser: needs nlohmann (from borealis) and the tested
# translation unit, because it is not header-only.
if g++ -std=c++20 -O1 -Wall -Wextra -I app/src -I extern/borealis/library/include \
    tests/endfield_vlink_proto_test.cpp app/src/endfield/vlink_proto.cpp \
    -o "$build/endfield_vlink_proto_test.exe"; then
    if "$build/endfield_vlink_proto_test.exe"; then
        echo "PASS endfield_vlink_proto_test"
    else
        echo "FAIL endfield_vlink_proto_test (non-zero exit)" >&2
        status=1
    fi
else
    echo "FAIL endfield_vlink_proto_test (compile)" >&2
    status=1
fi

for entry in "${tests[@]}"; do
  name="${entry%%:*}"
  source="${entry#*:}"
  if g++ -std=c++20 -O1 -Wall -Wextra -I app/src "$source" -o "$build/$name.exe"; then
    if "$build/$name.exe"; then
      echo "PASS $name"
    else
      echo "FAIL $name (non-zero exit)" >&2
      status=1
    fi
  else
    echo "FAIL $name (compile)" >&2
    status=1
  fi
done
exit $status
