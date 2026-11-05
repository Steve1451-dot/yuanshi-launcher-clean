#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    printf 'Usage: %s <version> [build-directory]\n' "$0" >&2
    exit 2
fi

version="$1"
build_directory="${2:-switch}"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
    printf 'Invalid release version: %s\n' "$version" >&2
    exit 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
nro="$repo/build/$build_directory/yuanshi-launcher.nro"

if [[ ! -f "$nro" ]]; then
    printf 'Missing build artifact: %s. Run the Switch build first.\n' "$nro" >&2
    exit 1
fi

dist="$repo/dist"
package_name="yuanshi-launcher-$version"
package_root="$dist/$package_name"
switch_directory="$package_root/switch/yuanshi-launcher"
versioned_nro="$dist/$package_name.nro"
zip_path="$dist/$package_name.zip"

for target in "$package_root" "$versioned_nro" "$zip_path"; do
    if [[ -e "$target" ]]; then
        printf 'Package already exists; use a new version instead of overwriting: %s\n' "$target" >&2
        exit 1
    fi
done
mkdir -p "$switch_directory"

cp "$nro" "$switch_directory/yuanshi-launcher.nro"
cp "$nro" "$versioned_nro"
cp "$repo/resources/icon/icon.jpg" "$switch_directory/icon.jpg"
cp "$repo/resources/icon/icon.png" "$switch_directory/icon.png"
cp "$repo/README.md" "$package_root/README.md"
cp "$repo/LICENSE" "$package_root/LICENSE"
if [[ -f "$repo/docs/yuanshi-launcher-$version.md" ]]; then
    mkdir -p "$package_root/docs"
    cp "$repo/docs/yuanshi-launcher-$version.md" "$package_root/docs/"
fi
if [[ -f "$repo/docs/yuanshi-launcher-$version-build.md" ]]; then
    cp "$repo/docs/yuanshi-launcher-$version-build.md" "$package_root/构建校验.md"
fi

install_template="$repo/resources/install/INSTALL.zh-CN.txt"
if [[ ! -f "$install_template" ]]; then
    printf 'Missing install template: %s\n' "$install_template" >&2
    exit 1
fi
sed "s/@VERSION@/$version/g" "$install_template" > "$package_root/INSTALL.txt"

(
    cd "$package_root"
    zip -qr "$zip_path" .
)

printf 'Packaged: %s\n' "$zip_path"
printf 'Versioned NRO: %s\n' "$versioned_nro"
