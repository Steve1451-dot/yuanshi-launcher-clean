param(
    [Parameter(Mandatory=$true)][string]$IsolatedRoot,
    [Parameter(Mandatory=$true)][string]$ProbeNro,
    [Parameter(Mandatory=$true)][string]$PythonExe,
    [string]$RunName = 'motion',
    [ValidateRange(30,180)][int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
if ($RunName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'RunName must be a safe file label.' }
$root = (Resolve-Path -LiteralPath $IsolatedRoot).Path
$nro = (Resolve-Path -LiteralPath $ProbeNro).Path
if ((Split-Path $nro -Leaf) -ne 'switch_motion_probe.nro') {
    throw 'This harness runs only the credential-free motion probe, not the cloud client.'
}
$emulator = Join-Path $root 'emulator/yuzu.exe'
$portableUser = Join-Path $root 'emulator/user'
$config = Join-Path $portableUser 'config/qt-config.ini'
$report = Join-Path $root 'reports'
$probeLog = Join-Path $portableUser 'sdmc/switch/yuanshi-launcher/motion-probe.log'
foreach ($required in @($emulator, $config, $PythonExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
# The portable user tree must be dedicated to this test, not the user's yuzu.
if ((Split-Path $root -Leaf) -notlike 'yuzu-motion-probe*') { throw 'Expected isolated yuzu-motion-probe directory.' }
if (Get-ChildItem -LiteralPath (Join-Path $portableUser 'sdmc/switch/yuanshi-launcher') -Filter '*.vault' -File -ErrorAction SilentlyContinue) {
    throw 'Account vault found in test SD. Use an empty, dedicated portable user tree.'
}
New-Item -ItemType Directory -Force -Path $report | Out-Null
if (Test-Path -LiteralPath $probeLog) {
    Move-Item -LiteralPath $probeLog -Destination (Join-Path $report "$RunName-previous.log") -Force
}
$serverProc = $null
$emulatorProc = $null
$result = 'NO_PROBE_RESULT'
try {
    $serverProc = Start-Process -FilePath $PythonExe -ArgumentList @(
        '-u', ('"' + (Join-Path $PSScriptRoot 'dsu-motion-probe.py') + '"'),
        '--duration', ($TimeoutSeconds + 5)) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $report "$RunName-dsu.out") `
        -RedirectStandardError (Join-Path $report "$RunName-dsu.err")
    $emulatorProc = Start-Process -FilePath $emulator -ArgumentList @('-c', ('"' + $config + '"'), '-g', ('"' + $nro + '"')) `
        -WorkingDirectory (Split-Path $emulator -Parent) -WindowStyle Hidden -PassThru
    "PROBE_LAUNCHED emulator_pid=$($emulatorProc.Id) server_pid=$($serverProc.Id) local_fixture_only=1"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 3
        $serverProc.Refresh()
        if ($serverProc.HasExited) { throw "DSU server exited early: $($serverProc.ExitCode)" }
        if (Test-Path -LiteralPath $probeLog) {
            $tail = Get-Content -LiteralPath $probeLog -Tail 1
            $tail
            if ($tail -match '^MOTION_PROBE_DONE') { $result = $tail; break }
        }
        $emulatorProc.Refresh()
        if ($emulatorProc.HasExited) { $result = "EMULATOR_EXIT code=$($emulatorProc.ExitCode)"; break }
    }
} finally {
    # Only terminate the exact children created by this run; never all yuzu processes.
    if ($emulatorProc -and -not $emulatorProc.HasExited) { Stop-Process -Id $emulatorProc.Id -Force }
    if ($serverProc -and -not $serverProc.HasExited) { Stop-Process -Id $serverProc.Id -Force }
}
if (Test-Path -LiteralPath $probeLog) { Copy-Item -LiteralPath $probeLog -Destination (Join-Path $report "$RunName-probe.log") }
Get-Content -LiteralPath (Join-Path $report "$RunName-dsu.out")
Get-Content -LiteralPath (Join-Path $report "$RunName-dsu.err")
$result
if ($result -notmatch '^MOTION_PROBE_DONE pass=1 ') { throw "Motion probe did not pass: $result" }
