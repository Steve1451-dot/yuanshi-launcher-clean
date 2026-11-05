param([string]$NroPath=(Join-Path $PSScriptRoot '../build/switch/yuanshi-launcher.nro'))
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$source=Get-Content -LiteralPath (Join-Path $repo 'app/src/app_version.hpp') -Raw
if($source -notmatch '#define CLOUDGENSHIN_APP_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"') {throw 'Missing native source version'}
$version=$Matches[1]
if($source -notmatch 'kNativeAppName = "([^"]+)"') {throw 'Missing native source name'}
$name=$Matches[1]
$stream=[IO.File]::OpenRead((Resolve-Path -LiteralPath $NroPath).Path)
$reader=[IO.BinaryReader]::new($stream)
try {
    $stream.Position=0x10
    if([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'NRO0') {throw 'Missing NRO header'}
    $stream.Position=0x18;$assetOffset=$reader.ReadUInt32()
    if($assetOffset -lt 0x80 -or $assetOffset+0x38 -gt $stream.Length) {throw 'Invalid NRO size'}
    $stream.Position=$assetOffset
    if([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'ASET') {throw 'Missing NRO assets'}
    $stream.Position=$assetOffset+8
    $iconRelative=$reader.ReadUInt64();$iconSize=$reader.ReadUInt64()
    if($iconSize -gt 1024*1024 -or $assetOffset+$iconRelative+$iconSize -gt $stream.Length) {throw 'Invalid icon bounds'}
    $stream.Position=$assetOffset+$iconRelative
    $embeddedIcon=$reader.ReadBytes([int]$iconSize)
    $selectedIcon=[IO.File]::ReadAllBytes((Join-Path $repo 'resources/icon/icon.jpg'))
    if([Convert]::ToBase64String($embeddedIcon) -cne [Convert]::ToBase64String($selectedIcon)) {
        throw 'NRO icon is not the selected repository icon'
    }
    $stream.Position=$assetOffset+0x18
    $nacpRelative=$reader.ReadUInt64();$nacpSize=$reader.ReadUInt64()
    if($nacpSize -ne 0x4000 -or $nacpRelative -gt $stream.Length -or
       $assetOffset+$nacpRelative+$nacpSize -gt $stream.Length) {throw 'Invalid NACP bounds'}
    $nacp=$assetOffset+$nacpRelative
    for($language=0;$language -lt 16;$language++) {
        $stream.Position=$nacp+$language*0x300
        $title=[Text.Encoding]::UTF8.GetString($reader.ReadBytes(0x200)).TrimEnd([char]0)
        if($title -ne $name) {throw "NACP title mismatch for language $language"}
        $author=[Text.Encoding]::UTF8.GetString($reader.ReadBytes(0x100)).TrimEnd([char]0)
        if($author -ne 'Steve1451-dot') {throw "NACP author mismatch for language $language"}
    }
    $stream.Position=$nacp+0x3060
    $actual=[Text.Encoding]::UTF8.GetString($reader.ReadBytes(16)).TrimEnd([char]0)
    if($actual -ne $version) {throw 'NACP version does not match source'}
    Write-Output "PASS native NRO0/ASET/NACP: $name $version, Steve1451-dot, selected icon (16 languages)"
} finally {$reader.Dispose();$stream.Dispose()}
