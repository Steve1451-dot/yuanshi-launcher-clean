$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('cg-yuzu-config-'+[Guid]::NewGuid().ToString('N'))
$emu=Join-Path $root 'emulator/yuzu.exe'
$effective=Join-Path $root 'emulator/user/config/qt-config.ini'
$nro=Join-Path $root 'dummy.nro'
$sd=Join-Path $root 'sdmc'
$wrong=Join-Path $root 'wrong.ini'
New-Item -ItemType Directory -Force -Path (Split-Path $effective),$sd | Out-Null
[IO.File]::WriteAllBytes($emu,[byte[]]@())
[IO.File]::WriteAllBytes($nro,[byte[]]@())
[IO.File]::WriteAllText($wrong,'')
function Expect-Guard([string]$config,[string]$message) {
    $caught=$false
    try {
        & "$PSScriptRoot/../scripts/start-cloudgenshin-autotest.ps1" -EmulatorPath $emu -NroPath $nro `
            -ConfigPath $config -SdRoot $sd -ReportDirectory (Join-Path $root 'reports')
    } catch {
        $caught=$true
        if(!$_.Exception.Message.Contains($message)) {throw}
    }
    if(!$caught) {throw 'Expected pre-launch portable-config rejection'}
}
try {
    [IO.File]::WriteAllText($effective,'')
    Expect-Guard $wrong 'actual dedicated portable'
    [IO.File]::WriteAllText($effective,"[Data Storage]`nsdmc_directory=$sd`nsdmc_directory\default=false`n")
    Expect-Guard $effective 'effective portable SD'
    [IO.File]::WriteAllText($effective,"[Data%20Storage]`nsdmc_directory=$sd`nsdmc_directory\default=true`n")
    Expect-Guard $effective 'effective portable SD'
    [IO.File]::WriteAllText($effective,"[Data%20Storage]`nsdmc_directory=$root`nsdmc_directory\default=false`n")
    Expect-Guard $effective 'effective portable SD'
    [IO.File]::WriteAllText($effective,"[Data%20Storage]`nsdmc_directory=$sd`nsdmc_directory\default=false`nsdmc_directory\default=true`n")
    Expect-Guard $effective 'effective portable SD'
    if(Test-Path -LiteralPath (Join-Path $root 'reports')) {throw 'Guard must fail before launching/report writes'}
    'PASS yuzu GUI effective-config/SD/default guards; no emulator or account used'
} finally {
    $resolved=(Get-Item -LiteralPath $root).FullName
    $temporary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$resolved.StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase) -or
       (Split-Path $resolved -Leaf) -notmatch '^cg-yuzu-config-[0-9a-f]{32}$') {throw 'Unsafe test cleanup target'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
