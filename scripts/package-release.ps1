param(
    [string]$Version = "",
    [string]$BuildDirectory = "switch",
    [string]$Flavor = ""
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionHeader = Get-Content -LiteralPath (Join-Path $repo 'app/src/app_version.hpp') -Raw
    if ($versionHeader -notmatch '#define CLOUDGENSHIN_APP_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"') {
        throw 'Cannot read the native launcher version.'
    }
    $Version = $Matches[1]
}
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or $Flavor -notmatch '^[A-Za-z0-9-]*$' -or
    $BuildDirectory -notmatch '^[A-Za-z0-9_-]+$') {
    throw 'Invalid package version, flavor or build directory.'
}
$nro = Join-Path $repo "build\$BuildDirectory\yuanshi-launcher.nro"
if (-not (Test-Path -LiteralPath $nro)) {
    throw "Missing build artifact: $nro. Run .\build-switch.ps1 first."
}

$dist = Join-Path $repo "dist"
$flavorSuffix = if ([string]::IsNullOrWhiteSpace($Flavor)) { "" } else { "-$Flavor" }
$packageName = "yuanshi-launcher-$Version$flavorSuffix"
$packageRoot = Join-Path $dist $packageName
$switchDir = Join-Path $packageRoot "switch\yuanshi-launcher"
$versionedNro = Join-Path $dist "$packageName.nro"
$zipPath = Join-Path $dist "$packageName.zip"

foreach ($target in @($packageRoot, $zipPath, $versionedNro)) {
    if (Test-Path -LiteralPath $target) {
        throw "Package already exists; use a new version/flavor rather than overwrite: $target"
    }
}

New-Item -ItemType Directory -Force -Path $switchDir | Out-Null

Copy-Item -LiteralPath $nro -Destination (Join-Path $switchDir "yuanshi-launcher.nro") -Force
Copy-Item -LiteralPath $nro -Destination $versionedNro -Force
Copy-Item -LiteralPath (Join-Path $repo "resources\icon\icon.jpg") -Destination (Join-Path $switchDir "icon.jpg") -Force
Copy-Item -LiteralPath (Join-Path $repo "resources\icon\icon.png") -Destination (Join-Path $switchDir "icon.png") -Force
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination (Join-Path $packageRoot "README.md") -Force
Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination (Join-Path $packageRoot "LICENSE") -Force
$releaseNotes = Join-Path $repo "docs/yuanshi-launcher-$Version.md"
if (Test-Path -LiteralPath $releaseNotes) {
    $docsDir = Join-Path $packageRoot 'docs'
    New-Item -ItemType Directory -Path $docsDir | Out-Null
    Copy-Item -LiteralPath $releaseNotes -Destination $docsDir
}
$buildVerification = Join-Path $repo "docs/yuanshi-launcher-$Version-build.md"
if (Test-Path -LiteralPath $buildVerification) {
    Copy-Item -LiteralPath $buildVerification -Destination (Join-Path $packageRoot '构建校验.md')
}

$installTemplate = Join-Path $repo 'resources/install/INSTALL.zh-CN.txt'
if (-not (Test-Path -LiteralPath $installTemplate)) {
    throw "Missing install template: $installTemplate"
}
$manifest = (Get-Content -LiteralPath $installTemplate -Raw -Encoding UTF8).Replace('@VERSION@', $Version)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $packageRoot "INSTALL.txt"), $manifest, $utf8NoBom)

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $zipPath)

Write-Host "Packaged: $zipPath"
Write-Host "Versioned NRO: $versionedNro"
