param(
    [Parameter(Mandatory)][string]$EmulatorPath,
    [Parameter(Mandatory)][string]$NroPath,
    [Parameter(Mandatory)][string]$ConfigPath,
    [Parameter(Mandatory)][string]$SdRoot,
    [Parameter(Mandatory)][string]$ReportDirectory,
    [string]$GdbPath,
    [string]$ElfPath,
    [string]$GdbScriptPath = "$PSScriptRoot/cloudgenshin-autotest.gdb",
    [switch]$Visible,
    [ValidateRange(30,180)][int]$BootTimeoutSeconds=90,
    [ValidateRange(0,86400)][int]$RunTimeoutSeconds=560
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/cloudgenshin-monitor.ps1"
# Windows PowerShell 5.1 (.NET Framework) has no ProcessStartInfo.ArgumentList;
# it needs one pre-quoted Arguments string. PowerShell 7 keeps per-argument passing.
function Format-QuotedArguments([string[]]$values) {
    return (($values | ForEach-Object { '"'+($_ -replace '"','\"')+'"' }) -join ' ')
}
foreach($path in @($EmulatorPath,$NroPath,$ConfigPath)) {
    if(!(Test-Path -LiteralPath $path -PathType Leaf)) {throw 'Missing emulator, NRO or config file.'}
}
$emulatorResolved=(Resolve-Path -LiteralPath $EmulatorPath).Path
$configResolved=(Resolve-Path -LiteralPath $ConfigPath).Path
$portableGui=([IO.Path]::GetFileName($emulatorResolved) -ieq 'yuzu.exe')
if($portableGui) {
    # EA4176 GUI silently ignores -c as a global config override. The old
    # harness watched a different SD while the NRO ran in the portable SD.
    # Require the actual dedicated portable config instead of reporting that
    # as a boot/client failure. Do not copy credentials or mutate global config.
    $effectiveConfig=Join-Path (Split-Path $emulatorResolved -Parent) 'user/config/qt-config.ini'
    if(!(Test-Path -LiteralPath $effectiveConfig) -or
       ![StringComparer]::OrdinalIgnoreCase.Equals($configResolved,(Get-Item -LiteralPath $effectiveConfig).FullName)) {
        throw 'yuzu GUI requires its actual dedicated portable user/config/qt-config.ini; -c is not a global config override.'
    }
    $storageGroup='';$configuredSd='';$sdNonDefault=$false
    foreach($line in [IO.File]::ReadAllLines($configResolved)) {
        if($line -match '^\[(.*)\]$') {$storageGroup=$Matches[1];continue}
        if($storageGroup -cne 'Data%20Storage') {continue}
        if($line -match '^sdmc_directory=(.*)$') {$configuredSd=$Matches[1]}
        if($line -match '^sdmc_directory\\default=(.*)$') {$sdNonDefault=$Matches[1] -ceq 'false'}
    }
    if(!$sdNonDefault -or !$configuredSd -or
       ![StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($configuredSd),[IO.Path]::GetFullPath($SdRoot))) {
        throw 'The effective portable SD must match SdRoot with an explicit non-default path.'
    }
}
if($GdbPath -and (!(Test-Path -LiteralPath $GdbPath -PathType Leaf) -or !(Test-Path -LiteralPath $ElfPath -PathType Leaf) -or !(Test-Path -LiteralPath $GdbScriptPath -PathType Leaf))) {throw 'Matching ELF, GDB executable and script are required.'}
$appDirectory=Join-Path $SdRoot 'switch/yuanshi-launcher'
$safeLog=Join-Path $appDirectory 'cloudgenshin-safe.log'
$stopFile=Join-Path $appDirectory 'cloudgenshin-autotest.stop'
if(!(Test-Path -LiteralPath (Join-Path $appDirectory 'cloudgenshin-autotest.txt'))) {throw 'The explicit NRO autotest opt-in marker is missing.'}
if(Test-Path -LiteralPath $stopFile) {throw 'A previous stop marker exists. Resolve it before another run.'}
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
$previous=Read-CloudGenshinSafeLog $safeLog
$started=[DateTime]::UtcNow
$clock=[Diagnostics.Stopwatch]::StartNew()
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=(Resolve-Path -LiteralPath $EmulatorPath).Path
$start.WorkingDirectory=Split-Path $start.FileName -Parent
$start.UseShellExecute=$false; $start.CreateNoWindow=!$Visible
$start.WindowStyle=if($Visible){'Normal'}else{'Hidden'}
$start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$emulatorArguments=if($portableGui) {@('-g',(Resolve-Path -LiteralPath $NroPath).Path)}
    else {@('-c',$configResolved,'-g',(Resolve-Path -LiteralPath $NroPath).Path)}
if($null -ne $start.PSObject.Properties['ArgumentList']) {foreach($argument in $emulatorArguments){$start.ArgumentList.Add($argument)}}
else {$start.Arguments=Format-QuotedArguments $emulatorArguments}
$process=[Diagnostics.Process]::new(); $process.StartInfo=$start
$artifactHashes=[ordered]@{Nro=(Get-FileHash -LiteralPath $NroPath -Algorithm SHA256).Hash}
if($ElfPath) {$artifactHashes.Elf=(Get-FileHash -LiteralPath $ElfPath -Algorithm SHA256).Hash}
$status=[ordered]@{Outcome='Starting';ProcessId=0;NroBooted=$false;ArtifactHashes=$artifactHashes;DebuggerMarkers=@();Snapshot=$null;Milestones=$null;DiagnosticEvidence=$null}
$last='';$lastConsole='';$finished=$false;$stopAfterFailure=$false;$processStarted=$false
$debugger=$null;$debuggerStarted=$false;$debuggerLine=$null
try {
    if(!$process.Start()) {throw 'Emulator could not be started.'}
    $processStarted=$true
    $status.ProcessId=$process.Id
    # Drain and discard raw output with bounded buffers. Only allowlisted safe
    # events are written to reports; no full console/protocol capture.
    $outputDrain=$process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
    $errorDrain=$process.StandardError.BaseStream.CopyToAsync([IO.Stream]::Null)
    if($GdbPath) {
        $debugStart=[Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $GdbPath).Path)
        $debugStart.UseShellExecute=$false;$debugStart.CreateNoWindow=$true;$debugStart.WindowStyle='Hidden'
        $debugStart.RedirectStandardOutput=$true;$debugStart.RedirectStandardError=$true
        $symbolPath=(Resolve-Path -LiteralPath $ElfPath).Path.Replace('\','/')
        if($symbolPath.Contains('"')) {throw 'Invalid ELF path.'}
        $debuggerArguments=@('-nx','--batch','-ex',('add-symbol-file "'+$symbolPath+'" -o 0x80000000'),'-x',(Resolve-Path -LiteralPath $GdbScriptPath).Path)
        if($null -ne $debugStart.PSObject.Properties['ArgumentList']) {foreach($argument in $debuggerArguments){$debugStart.ArgumentList.Add($argument)}}
        else {$debugStart.Arguments=Format-QuotedArguments $debuggerArguments}
        $debugger=[Diagnostics.Process]::new();$debugger.StartInfo=$debugStart
        $debuggerStarted=$debugger.Start()
        if($debuggerStarted) {
            $debuggerErrorDrain=$debugger.StandardError.BaseStream.CopyToAsync([IO.Stream]::Null)
            $debuggerLine=$debugger.StandardOutput.ReadLineAsync()
        }
    }
    while(!$process.HasExited -and ($RunTimeoutSeconds -eq 0 -or $clock.Elapsed.TotalSeconds -lt $RunTimeoutSeconds)) {
        while($debuggerLine -and $debuggerLine.IsCompleted) {
            $line=$debuggerLine.GetAwaiter().GetResult()
            if($null -eq $line) {$debuggerLine=$null;break}
            if((Test-CloudGenshinDebuggerMarker $line) -and $status.DebuggerMarkers.Count -lt 32) {
                $status.DebuggerMarkers+= $line
                if($line.StartsWith('GDB_SVC_BREAK')) {$status.Outcome='NroSvcBreak';$finished=$true}
            }
            $debuggerLine=$debugger.StandardOutput.ReadLineAsync()
        }
        $snapshot=Read-CloudGenshinSafeLog $safeLog
        $fresh=$snapshot -and (!$previous -or $snapshot.Run -ne $previous.Run) -and
            (Get-Item -LiteralPath $safeLog -ErrorAction SilentlyContinue).LastWriteTimeUtc -ge $started.AddSeconds(-2)
        if($fresh -and !$finished) {
            $status.Milestones=Get-CloudGenshinTestMilestones $snapshot $status.Milestones
            $status.DiagnosticEvidence=Get-CloudGenshinDiagnosticEvidence $snapshot $status.DiagnosticEvidence
            $status.NroBooted=$true; $status.Snapshot=$snapshot; $status.Outcome='Running'
            if($snapshot.Finished) {$status.Outcome=Get-CloudGenshinCompletionOutcome $snapshot $status.DiagnosticEvidence;$finished=$true}
            elseif($snapshot.Failed) {$status.Outcome='NroTestFailed';$stopAfterFailure=$true}
        }
        if(!$status.NroBooted -and $clock.Elapsed.TotalSeconds -ge $BootTimeoutSeconds) {$status.Outcome='NoNroEntryBeforeBootDeadline';break}
        if(Test-Path -LiteralPath (Join-Path $ReportDirectory 'stop.request')) {$status.Outcome='StopRequested';break}
        $json=$status | ConvertTo-Json -Depth 5
        if($json -ne $last) {
            [IO.File]::WriteAllText((Join-Path $ReportDirectory 'status.json'),$json)
            $last=$json
            $compact=[ordered]@{Outcome=$status.Outcome;NroBooted=$status.NroBooted}
            if($status.Snapshot) {
                $compact.Stage=$status.Snapshot.Stage;$compact.FailureEvent=$status.Snapshot.FailureEvent
                if($status.Snapshot.Stage -eq 'Queue') {
                    $counters=Get-CloudGenshinQueueCounters $status.Snapshot
                    foreach($key in $counters.Keys) {$compact[$key]=$counters[$key]}
                }
                # Full counters remain in status.json. Print only stage
                # changes here, not a complete counter dump every five seconds.
            }
            $console=$compact | ConvertTo-Json -Depth 4 -Compress
            if($console -ne $lastConsole) {Write-Output $console;$lastConsole=$console}
        }
        if($finished -or $stopAfterFailure) {break}
        $null=$process.WaitForExit(1000)
    }
    if($process.HasExited -and !$finished) {$status.Outcome='EmulatorExited';$status.ExitCode=$process.ExitCode}
    elseif(!$finished -and $status.Outcome -eq 'Running') {$status.Outcome='HarnessDeadlineReached'}
} finally {
    if($processStarted -and !$process.HasExited) {
        if($status.NroBooted -and !$finished) {
            [IO.File]::WriteAllText($stopFile,"stop\n")
            # Give the NRO's cancellation and best-effort queue/session cleanup
            # time to run. Do not repeatedly dispatch after timeout/failure.
            $null=$process.WaitForExit(12000)
        }
        elseif($finished) {$null=$process.WaitForExit(2000)}
        if(!$process.HasExited) {$process.Kill();$process.WaitForExit()}
    }
    # A fast NRO failure can arrive before the next one-second debugger poll.
    # Drain already-buffered, allowlisted markers before stopping GDB; otherwise
    # the exception/curl location that explains that failure is silently lost.
    for($drained=0;$drained -lt 256 -and $debuggerLine -and $debuggerLine.IsCompleted;$drained++) {
        $line=$debuggerLine.GetAwaiter().GetResult()
        if($null -eq $line){$debuggerLine=$null;break}
        if((Test-CloudGenshinDebuggerMarker $line) -and $status.DebuggerMarkers.Count -lt 32) {
            $status.DebuggerMarkers+=$line
        }
        $debuggerLine=$debugger.StandardOutput.ReadLineAsync()
    }
    if($debuggerStarted -and $debugger.HasExited){$status.DebuggerExitCode=$debugger.ExitCode}
    if($debuggerStarted -and !$debugger.HasExited) {$debugger.Kill();$debugger.WaitForExit()}
    if($debugger) {$debugger.Dispose()}
    $finalSnapshot=Read-CloudGenshinSafeLog $safeLog
    if($status.NroBooted -and $finalSnapshot -and $finalSnapshot.Run -eq $status.Snapshot.Run) {
        $status.Snapshot=$finalSnapshot
        $status.Milestones=Get-CloudGenshinTestMilestones $finalSnapshot $status.Milestones
        $status.DiagnosticEvidence=Get-CloudGenshinDiagnosticEvidence $finalSnapshot $status.DiagnosticEvidence
        if($finished -and $finalSnapshot.Finished) {
            $status.Outcome=Get-CloudGenshinCompletionOutcome $finalSnapshot $status.DiagnosticEvidence
        }
    }
    [IO.File]::WriteAllText((Join-Path $ReportDirectory 'status.json'),($status | ConvertTo-Json -Depth 5))
    $process.Dispose()
}
$status | ConvertTo-Json -Depth 5
if($status.Outcome -ne 'TestEndedNotGameplayProof') {exit 2}
