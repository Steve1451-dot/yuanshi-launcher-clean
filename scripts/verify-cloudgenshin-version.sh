#!/usr/bin/env bash
# Refuse to label a build with a version that is not actually in the artifact.
# Usage: verify-cloudgenshin-version.sh <nro> <expected-version>
set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Usage: %s <nro> <expected-version>\n' "$0" >&2
    exit 2
fi

nro="$1"
expected="$2"

if [[ ! -f "$nro" ]]; then
    printf 'Missing build artifact: %s\n' "$nro" >&2
    exit 1
fi

if [[ ! "$expected" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Invalid expected version: %s\n' "$expected" >&2
    exit 2
fi

# The version constant is referenced by diagnostics, the tab label and the HTTP
# user agents, so it is present as a plain string in the NRO.
if ! grep -aqF -- "$expected" "$nro"; then
    printf 'NRO %s does not contain CloudGenshin version %s.\n' "$nro" "$expected" >&2
    exit 1
fi

printf '%s\n' "$expected"
