$ErrorActionPreference = "Stop"

$bashCandidates = @(
    "C:\devkitPro\msys2\usr\bin\bash.exe",
    "C:\msys64\usr\bin\bash.exe"
)
# Prefer an environment whose /usr/bin actually ships cmake+ninja.
$bash = $bashCandidates | Where-Object {
    (Test-Path $_) -and (Test-Path (Join-Path (Split-Path $_ -Parent) "cmake.exe"))
} | Select-Object -First 1
if (-not $bash) {
    $bash = $bashCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $bash) {
    throw "MSYS2 bash was not found. Tried: $($bashCandidates -join ', ')"
}

$repo = $PSScriptRoot -replace "\\", "/"
if ($repo -match "^([A-Za-z]):/(.*)$") {
    $repo = "/$($matches[1].ToLower())/$($matches[2])"
}

& $bash -lc "cd '$repo' && bash scripts/build-switch-msys2.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Switch build failed (exit code $LASTEXITCODE)."
}
