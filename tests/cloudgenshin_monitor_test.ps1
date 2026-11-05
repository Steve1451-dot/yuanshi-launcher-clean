$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/cloudgenshin-monitor.ps1"
$queues='GDB_SCTP_QUEUES sent_bytes=12 recv_bytes=0 reasm=2048 ordered=3 unacked=2 retry=0 peer_window=65536 local_window=65536 partial=0 failed=0'
$stream='GDB_SCTP_STREAM sid=1 gap=0 first=1 last=0 end=0 partial=0 bytes=1092 idata=0'
if(!(Test-CloudGenshinDebuggerMarker $stream) -or (Test-CloudGenshinDebuggerMarker ($stream+' secret-text')) -or (Test-CloudGenshinDebuggerMarker $stream.Replace('sid=1','sid=999'))) {throw 'SCTP stream marker filtering failed'}
if(!(Test-CloudGenshinDebuggerMarker $queues)) {throw 'Numeric SCTP queue counters were rejected'}
foreach($line in @($queues+' private-text',$queues.Replace('sent_bytes=12','sent_bytes=-1'),$queues.Replace('failed=0','failed=2'),$queues.Replace('reasm=2048','reasm=12345678901'))) {
    if(Test-CloudGenshinDebuggerMarker $line) {throw 'Invalid SCTP queue marker escaped the allowlist'}
}
foreach($line in @('GDB_TARGET_CONNECTED','GDB_CONTROL_STATE connected=1 rx_dtls=118 rx_sctp=134 queued=0 failed=0','GDB_SCTP_FIRST_CHUNKS data=0 sack=20 heartbeat=2 other=0 invalid=0')) {
    if(!(Test-CloudGenshinDebuggerMarker $line)) {throw 'Safe debugger marker was rejected'}
}
foreach($line in @('GDB_SCTP_FIRST_CHUNKS data=0 sack=20 heartbeat=2 other=0 invalid=0 secret-text','GDB_SCTP_FIRST_CHUNKS data=-1 sack=20 heartbeat=2 other=0 invalid=0','GDB_SCTP_FIRST_CHUNKS data=1234567 sack=20 heartbeat=2 other=0 invalid=0','GDB_CONTROL_STATE connected=2 rx_dtls=1 rx_sctp=1 queued=0 failed=0','GDB_SECRET value=123','gdb_TARGET_CONNECTED')) {
    if(Test-CloudGenshinDebuggerMarker $line) {throw 'Unknown debugger data escaped the allowlist'}
}
$header="CloudGenshin-NX native alpha 0.2.10 / safe events only`nrun_started_unix_seconds=123`n"
$renamed=$header.Replace('CloudGenshin-NX','yuanshi launcher').Replace('0.2.10','0.2.110')
$newBrand=ConvertFrom-CloudGenshinSafeLog ($renamed+"1 t=0s NATIVE_CLOUD_ENTRY_READY code=0`n")
if($null -eq $newBrand -or $newBrand.Version -ne '0.2.110') {throw 'Rebranded safe logs must remain readable'}
$fp=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=8s SDK_DEVICE_FP_SERVICE_READY code=1 http=200`n2 t=67s GAME_SDK_DEVICE_FINGERPRINT_PRESENT code=1`n")
$fpEvidence=Get-CloudGenshinDiagnosticEvidence $fp $null
if($fp.Failed -or $fpEvidence.Counters.SDK_DEVICE_FP_SERVICE_READY -ne 1 -or
   $fpEvidence.Counters.GAME_SDK_DEVICE_FINGERPRINT_PRESENT -ne 1 -or
   (Get-CloudGenshinTestMilestones $fp $null).GameEntrySignalObserved) {throw 'FP presence is numeric evidence, not gameplay proof'}
$legacyWeb=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s STAR_RAIL_WEB_LOGIN_UPDATE_REQUIRED code=0`n2 t=1s AUTOTEST_LAUNCH_FINISHED code=0`n")
if((Get-CloudGenshinCompletionOutcome $legacyWeb) -ne 'NeedsOfficialWebSignIn') {throw 'Legacy login update must be a required user action, not a client crash or gameplay success'}
$resume=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GAME_WEB_RESUME_STAGE code=1`n2 t=2s GAME_WEB_RESUME_API_REJECTED code=-100 http=200`n")
$resumeEvidence=Get-CloudGenshinDiagnosticEvidence $resume $null
if(!$resume.Failed -or $resumeEvidence.Counters.GAME_WEB_RESUME_STAGE -ne 1 -or
   $resumeEvidence.Counters.GAME_WEB_RESUME_API_REJECTED -ne -100) {throw 'Official web-resume rejection lost or treated as gameplay success'}
$starRailError=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=60s GAME_SDK_REPORT_NET_1001_2 code=1`n2 t=61s VIDEO_DECODED_FRAMES code=3000`n3 t=62s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if($starRailError.Failed -or $starRailError.Metrics.GAME_SDK_REPORT_NET_1001_2 -ne 1) {
    throw 'Star Rail game error must survive filtering without masquerading as a transport failure'
}
$starRailEvidence=Get-CloudGenshinDiagnosticEvidence $starRailError $null
$refresh=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s SAVED_LOGIN_REFRESHING code=0`n2 t=2s SAVED_LOGIN_REFRESHED code=0 http=200`n3 t=2s LOGIN_RESTORED code=0 http=200`n4 t=3s LOGIN_SAVED code=0`n5 t=4s AUTOTEST_STAR_RAIL_POINTER_ENTRY code=0`n")
$refreshEvidence=Get-CloudGenshinDiagnosticEvidence $refresh $null
$h5=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=6s GAME_SDK_LOGIN_H5_ADDITIONAL_ROOT_FIELDS code=2`n2 t=72s GAME_SDK_REPORT_NETWORK_HINT_MASK code=3`n")
$h5Evidence=Get-CloudGenshinDiagnosticEvidence $h5 $null
if($h5.Failed -or $h5Evidence.Counters.GAME_SDK_LOGIN_H5_ADDITIONAL_ROOT_FIELDS -ne 2 -or
   $h5Evidence.Counters.GAME_SDK_REPORT_NETWORK_HINT_MASK -ne 3) {
    throw 'H5 field counts and textual hints must be retained as nonfatal evidence, without field/message values'
}
$prepareFailure=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=31s DISPATCH_PREPARE_NETWORK_OPERATION code=1`n2 t=31s DISPATCH_PREPARE_NETWORK_ERROR code=28`n3 t=31s SERVICE_CHECK_FAILED code=0`n")
$prepareEvidence=Get-CloudGenshinDiagnosticEvidence $prepareFailure $null
$timings=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=31s HTTP_FAILURE_PHASE code=4`n2 t=31s HTTP_FAILURE_TOTAL_MS code=30000`n3 t=31s HTTP_FAILURE_UPLOADED_BYTES code=128`n")
$timingEvidence=Get-CloudGenshinDiagnosticEvidence $timings $null
if($timings.Failed -or $timingEvidence.Counters.HTTP_FAILURE_PHASE -ne 4 -or
   $timingEvidence.Counters.HTTP_FAILURE_TOTAL_MS -ne 30000 -or
   $timingEvidence.Counters.HTTP_FAILURE_UPLOADED_BYTES -ne 128) {throw 'Numeric HTTP phases lost from safe evidence'}
$pingTiming=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=31s PING_FIRST_FAILURE_PHASE code=3`n2 t=31s PING_FIRST_DNS_MS code=1`n3 t=31s PING_FIRST_TOTAL_MS code=2500`n4 t=31s PING_FIRST_PORT code=8443`n5 t=31s PING_FIRST_OS_ERROR code=110`n")
$pingEvidence=Get-CloudGenshinDiagnosticEvidence $pingTiming $null
if($pingTiming.Failed -or $pingEvidence.Counters.PING_FIRST_FAILURE_PHASE -ne 3 -or
   $pingEvidence.Counters.PING_FIRST_DNS_MS -ne 1 -or $pingEvidence.Counters.PING_FIRST_TOTAL_MS -ne 2500 -or
   $pingEvidence.Counters.PING_FIRST_PORT -ne 8443 -or $pingEvidence.Counters.PING_FIRST_OS_ERROR -ne 110) {
    throw 'Numeric ping failure phases lost from safe evidence'
}
if(!$prepareFailure.Failed -or $prepareFailure.FailureEvent -ne 'SERVICE_CHECK_FAILED' -or
   $prepareEvidence.Counters.DISPATCH_PREPARE_NETWORK_ERROR -ne 28 -or
   $prepareEvidence.Counters.DISPATCH_PREPARE_NETWORK_OPERATION -ne 1) {
    throw 'Service-check transport cause must not be lost behind retcode zero'
}
$sdkError=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=72s GAME_SDK_REPORT_FIRST_NONZERO_RETCODE code=1001`n")
$sdkEvidence=Get-CloudGenshinDiagnosticEvidence $sdkError $null
$sdkEnd=ConvertFrom-CloudGenshinSafeLog ($header+"2000 t=179s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if($sdkError.Failed -or $sdkEvidence.Counters.GAME_SDK_REPORT_FIRST_NONZERO_RETCODE -ne 1001 -or
   (Get-CloudGenshinCompletionOutcome $sdkEnd $sdkEvidence) -ne 'GameReportedNonzeroSdkRetcode') {
    throw 'Game-reported numeric 1001 was lost, called a transport failure, or silently treated as success'
}
$sdkEvidence=Get-CloudGenshinDiagnosticEvidence $sdkEnd $sdkEvidence
if($sdkEvidence.Counters.GAME_SDK_REPORT_FIRST_NONZERO_RETCODE -ne 1001) {throw 'SDK error evicted by the event ring'}
foreach($event in @('GAME_SDK_LOGIN_REFRESHED','GAME_SDK_LOGIN_REFRESH_FAILED',
    'GAME_SDK_LOGIN_REFRESH_UNAVAILABLE','GAME_SDK_LOGIN_REFRESH_ACCOUNT_MISMATCH',
    'GAME_SDK_LOGIN_USING_EXISTING_GRANT')) {
    $grant=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=5s $event code=1`n")
    $grantEvidence=Get-CloudGenshinDiagnosticEvidence $grant $null
    if($grant.Failed -or $grantEvidence.Counters[$event] -ne 1) {
        throw 'Game authorization diagnostics must be retained without asserting transport failure or entry'
    }
}
if($refresh.Failed -or !$refreshEvidence.Counters.Contains('SAVED_LOGIN_REFRESHED') -or
   !$refreshEvidence.Counters.Contains('LOGIN_RESTORED') -or !$refreshEvidence.Counters.Contains('AUTOTEST_STAR_RAIL_POINTER_ENTRY')) {
    throw 'Presence-only login and probe evidence was lost'
}
if((Get-CloudGenshinCompletionOutcome $starRailError) -ne 'GameReportedNetworkError1001_2') {
    throw 'A completed media test with a game error must not look successful'
}
$starRailRolled=ConvertFrom-CloudGenshinSafeLog ($header+"5000 t=171s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
$starRailEvidence=Get-CloudGenshinDiagnosticEvidence $starRailRolled $starRailEvidence
if($starRailEvidence.Counters.GAME_SDK_REPORT_NET_1001_2 -ne 1 -or
   (Get-CloudGenshinCompletionOutcome $starRailRolled $starRailEvidence) -ne 'GameReportedNetworkError1001_2') {
    throw 'Bounded log rollover must not erase the observed game error'
}
$unrelatedRun=ConvertFrom-CloudGenshinSafeLog ($header.Replace('seconds=123','seconds=124')+"1 t=2s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if((Get-CloudGenshinCompletionOutcome $unrelatedRun $starRailEvidence) -ne 'TestEndedNotGameplayProof') {
    throw 'An old game error must not leak into another run'
}
$rank=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=2s QUEUE_TOTAL_LENGTH code=480`n2 t=2s QUEUE_BRANCH_LENGTH code=439`n3 t=2s QUEUED_POSITION code=2`n")
if($rank.Stage -ne 'Queue' -or $rank.Metrics.QUEUED_POSITION -ne 2 -or
   $rank.Metrics.QUEUE_TOTAL_LENGTH -ne 480 -or $rank.Metrics.QUEUE_BRANCH_LENGTH -ne 439) {
    throw 'Queue sizes must not be confused with personal rank'
}
foreach($value in @('0.2.98','0.2.7','....','99999999999.9.9','')) {
    $rank.Version=$value
    $queue=Get-CloudGenshinQueueCounters $rank
    if($queue.Contains('QueuePosition') -or $queue.UnverifiedQueueCounter -ne 2) {
        throw 'Legacy or malformed versions must not claim known rank'
    }
}
foreach($value in @('0.2.99','0.2.100','0.3.0','1.0.0')) {
    $rank.Version=$value
    $queue=Get-CloudGenshinQueueCounters $rank
    if($queue.QueuePosition -ne 2 -or $queue.QueueLength -ne 480 -or
       $queue.QueueBranchLength -ne 439 -or $queue.Contains('UnverifiedQueueCounter')) {
        throw 'Queue counters must preserve their separate meanings'
    }
}
$startup=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=0s STREAM_TERMINAL_TYPE code=3`n2 t=1s SDK_DEVICE_INFO_BEFORE_FIRST_FRAME code=0`n3 t=20s MEDIA_STARTUP_NO_AV_20S code=0`n4 t=21s SAVED_LOGIN_NETWORK_ERROR code=28`n")
if($startup.Metrics.STREAM_TERMINAL_TYPE -ne 3 -or !$startup.Metrics.Contains('MEDIA_STARTUP_NO_AV_20S') -or $startup.Metrics.SAVED_LOGIN_NETWORK_ERROR -ne 28) {throw 'PS5 startup / network evidence was lost'}
$ingress=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=20s RTC_RTP_COMPLETED code=12`n2 t=20s RTC_RTP_DECRYPT_FAILURES code=12`n3 t=20s RTC_RTP_LAST_DECRYPT_ERROR code=7`n4 t=20s MEDIA_STARTUP_SRTP_REJECTED_20S code=0`n5 t=20s RTC_RTP_SSRC_REJECTED code=2`n6 t=20s RTC_RTP_VIDEO_ROUTED code=5`n")
if($ingress.Metrics.RTC_RTP_COMPLETED -ne 12 -or $ingress.Metrics.RTC_RTP_LAST_DECRYPT_ERROR -ne 7 -or
   $ingress.Metrics.RTC_RTP_SSRC_REJECTED -ne 2 -or $ingress.Metrics.RTC_RTP_VIDEO_ROUTED -ne 5 -or
   !$ingress.Metrics.Contains('MEDIA_STARTUP_SRTP_REJECTED_20S') -or $ingress.Failed) {throw 'Media ingress evidence missing or warning ended the session'}
$boundedRecord=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s LOG_HISTORY_LIMIT_MIB code=8`n2 t=5s VIDEO_BUFFER_REUSES code=199`n3 t=5s DEBUG_DECODE_OVER_33MS code=4`n")
if($boundedRecord.Metrics.LOG_HISTORY_LIMIT_MIB -ne 8 -or $boundedRecord.Metrics.VIDEO_BUFFER_REUSES -ne 199 -or $boundedRecord.Metrics.DEBUG_DECODE_OVER_33MS -ne 4) {throw 'Archive and tail metrics were not preserved'}
$debugWindow=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=5s DEBUG_WINDOW_UI_MILLIFPS code=17000`n2 t=5s DEBUG_WINDOW_HW_FRAMES code=59`n3 t=5s VIDEO_HW_PREFERRED_EXPERIMENTAL code=0`n4 t=5s DEBUG_UI_SESSION_MAX_US code=366400`n")
if($debugWindow.Metrics.DEBUG_WINDOW_UI_MILLIFPS -ne 17000 -or $debugWindow.Metrics.DEBUG_WINDOW_HW_FRAMES -ne 59 -or $debugWindow.Metrics.DEBUG_UI_SESSION_MAX_US -ne 366400) {throw 'Scalar presentation diagnostics were lost'}
$sample=$header+"1 t=0s NATIVE_CLOUD_ENTRY_READY code=0`n2 t=1s LOGIN_RESTORED code=0 http=200`n3 t=2s AUTOTEST_NORMAL_QUEUE_START code=0`n4 t=4s QUEUED_POSITION code=2 http=200`n"
$result=ConvertFrom-CloudGenshinSafeLog $sample
if($result.Stage -ne 'Queue' -or $result.Code -ne 2 -or $result.Failed) {throw 'Queue snapshot failed'}
$currentPs5=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE code=0`n2 t=1s STREAM_TERMINAL_TYPE code=6`n3 t=1s GAMEPAD_IDENTITY code=1356 http=3302`n4 t=1s GAMEPAD_VENDOR_ID code=1356`n5 t=1s GAMEPAD_PRODUCT_ID code=3302`n6 t=21s MOTION_CURRENT_PS5_NO_ACTIVATION_20S code=0`n7 t=22s MOTION_CURRENT_PS5_FIRST_LOCAL_SEND_NOT_ACK code=0`n")
if($currentPs5.Failed -or !$currentPs5.Metrics.Contains('GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE') -or
   !$currentPs5.Metrics.Contains('MOTION_CURRENT_PS5_FIRST_LOCAL_SEND_NOT_ACK') -or
   !$currentPs5.Metrics.Contains('MOTION_CURRENT_PS5_NO_ACTIVATION_20S')) {throw 'Current-client PS5 evidence was lost or mistaken for failure'}
$currentEvidence=Get-CloudGenshinDiagnosticEvidence $currentPs5
if($currentEvidence.Counters.STREAM_TERMINAL_TYPE -ne 6 -or
   !$currentEvidence.Counters.Contains('GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE') -or
   $currentEvidence.Counters.GAMEPAD_VENDOR_ID -ne 1356 -or
   $currentEvidence.Counters.GAMEPAD_PRODUCT_ID -ne 3302 -or
   $currentPs5.Metrics.Contains('GAMEPAD_IDENTITY')) {throw 'Current-client PS5 identity was not retained or invalid HTTP was accepted'}
$localAim=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s XBOX_INPUT_MODE_ACTIVE code=0`n2 t=1s GENSHIN_LOCAL_GYRO_ZL_DEFAULT_READY code=0`n3 t=6s GYRO_LOCAL_ZL_TICKS code=120`n4 t=6s GYRO_LOCAL_COMPOSE_TICKS code=60`n5 t=6s GYRO_LOCAL_SEND_FAILURES code=1`n6 t=6s GYRO_LOCAL_STICK_X code=-9000`n7 t=6s GYRO_LOCAL_STICK_Y code=8000`n8 t=6s GYRO_STICK_ASSIST_FRAMES_SENT code=59`n")
$localEvidence=Get-CloudGenshinDiagnosticEvidence $localAim
if($localAim.Failed -or $localAim.Metrics.GYRO_LOCAL_STICK_X -ne -9000 -or
   $localEvidence.Counters.GYRO_LOCAL_ZL_TICKS -ne 120 -or
   $localEvidence.Counters.GYRO_LOCAL_COMPOSE_TICKS -ne 60 -or
   $localEvidence.Counters.GYRO_LOCAL_SEND_FAILURES -ne 1 -or
   $localEvidence.Counters.GYRO_STICK_ASSIST_FRAMES_SENT -ne 59) {throw 'Local gyro diagnostics were lost or mistaken for cloud activation'}
$queueRetry=ConvertFrom-CloudGenshinSafeLog ($sample+"5 t=35s QUEUE_POLL_NETWORK_RETRY code=28`n")
if($queueRetry.Failed -or $queueRetry.Stage -ne 'Queue' -or $queueRetry.Metrics.QUEUE_POLL_NETWORK_RETRY -ne 28) {throw 'Read-only queue retry lost queue state or was treated as fatal'}
$attempt=$header+"1 t=1s SIGNALING_TCP_TLS_FAILED code=6`n"
if((ConvertFrom-CloudGenshinSafeLog $attempt).Failed) {throw 'Endpoint attempt stopped fallback prematurely'}
$recovered=ConvertFrom-CloudGenshinSafeLog ($attempt+"2 t=2s SIGNALING_UPGRADE_OK code=0 http=101`n3 t=3s GAME_AUTH_OK code=0`n4 t=4s STREAM_WORKER_FINISHED code=0`n")
if($recovered.Failed) {throw 'Recovered endpoint retained a stale failure'}
$exhausted=ConvertFrom-CloudGenshinSafeLog ($attempt+"2 t=2s STREAM_WORKER_FINISHED code=0`n")
if(!$exhausted.Failed -or $exhausted.FailureCode -ne 6) {throw 'Exhausted signaling endpoints were not a failure'}
$lost=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=71s SIGNALING_DISCONNECTED code=0`n2 t=71s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if(!$lost.Failed -or $lost.FailureEvent -ne 'SIGNALING_DISCONNECTED') {throw 'A disconnected session must not be a successful test'}
$ended=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=71s GAME_SDK_SESSION_ENDED code=0`n2 t=71s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if(!$ended.Failed -or $ended.FailureEvent -ne 'GAME_SDK_SESSION_ENDED') {throw 'A server-ended session must not be a successful test'}
$bitrate=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=2s DISPATCH_BIT_RATE code=4000000`n")
if($bitrate.Metrics.DISPATCH_BIT_RATE -ne 4000000) {throw 'Dispatch bitrate metric did not persist'}
$crowded=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=2s QUEUED_POSITION code=284`n2 t=2s QUEUE_ABANDONED_TOO_LONG code=284`n")
if($crowded.Failed) {throw 'A deliberate queue abandonment must not be reported as a failure'}
if($crowded.Metrics.QUEUE_ABANDONED_TOO_LONG -ne 284) {throw 'Queue abandonment metric missing'}
$crowdedEnd=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=2s QUEUED_POSITION code=284`n2 t=2s QUEUE_ABANDONED_TOO_LONG code=284`n3 t=3s AUTOTEST_LAUNCH_FINISHED code=0`n")
if((Get-CloudGenshinCompletionOutcome $crowdedEnd) -ne 'QueueStoppedByPolicy' -or
   $crowdedEnd.Metrics.QUEUED_POSITION -ne 284) {throw 'A skipped queue must not look like a completed cloud test'}
if($null -ne (Get-CloudGenshinCompletionOutcome $crowded)) {throw 'In-progress snapshots must not be completed'}
$failedEnd=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s SAVED_LOGIN_RETRY_LATER code=0`n2 t=2s AUTOTEST_LAUNCH_FINISHED code=0`n")
if((Get-CloudGenshinCompletionOutcome $failedEnd) -ne 'NroTestFailed') {throw 'Failure precedence lost'}
foreach($tail in @('',"4 t=14s STREAM_ENTRY code=0`n5 t=16s RTC_CONNECTED_WAITING_VIDEO code=0`n")) {
    $handoff=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=13s DISPATCH_READY_FOR_GAME_AUTH code=0 http=200`n2 t=13s AUTOTEST_LAUNCH_FINISHED code=0`n"+$tail)
    if($handoff.Finished -or $null -ne (Get-CloudGenshinCompletionOutcome $handoff)) {
        throw 'A successful dispatch handoff must not terminate the emulator before video'
    }
}
$handoffEnd=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=13s DISPATCH_READY_FOR_GAME_AUTH code=0 http=200`n2 t=13s AUTOTEST_LAUNCH_FINISHED code=0`n3 t=180s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if(!$handoffEnd.Finished) {throw 'An actual stream-end marker must still finish the test'}
$ice=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s RTC_ICE_LOCAL_CANDIDATES code=0`n2 t=1s RTC_UDP_LAST_ERROR code=101`n")
if($ice.Metrics.RTC_ICE_LOCAL_CANDIDATES -ne 0 -or $ice.Metrics.RTC_UDP_LAST_ERROR -ne 101) {throw 'ICE numeric diagnostics failed'}
$input=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GAME_CONFIG_XBOX code=0`n2 t=1s INPUT_WAKE_CLICK_SENT code=0`n3 t=2s VIDEO_RESUMED_AFTER_INPUT code=0`n4 t=2s STREAM_QUALITY_SENT code=1`n")
if($input.Metrics.GAME_CONFIG_XBOX -ne 0 -or $input.Metrics.INPUT_WAKE_CLICK_SENT -ne 0 -or $input.Metrics.VIDEO_RESUMED_AFTER_INPUT -ne 0 -or $input.Metrics.STREAM_QUALITY_SENT -ne 1) {throw 'Input/quality metric passthrough failed'}
$nores=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s INPUT_WAKE_NO_VIDEO_RESPONSE code=0`n")
if(!$nores.Failed -or $nores.FailureEvent -ne 'INPUT_WAKE_NO_VIDEO_RESPONSE') {throw 'Input wake failure marking failed'}
$control=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s RTC_CONTROL_WAITING_CHANNEL code=0`n")
if($control.Stage -ne 'WaitingControlChannel' -or $control.Failed) {throw 'Control-stage parsing failed'}
$control=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s RTC_DATA_CHANNEL_TIMEOUT code=0`n2 t=2s VIDEO_DECODED_FRAMES code=20`n")
if(!$control.Failed -or $control.FailureEvent -ne 'RTC_DATA_CHANNEL_TIMEOUT') {throw 'Working video hid a failed control channel'}
$sample+="5 t=10s RTC_PEER_STATE code=5`n6 t=10s VIDEO_UNITS_RECEIVED code=0`n7 t=10s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n"
$result=ConvertFrom-CloudGenshinSafeLog $sample
if(!$result.Failed -or !$result.Finished -or $result.Metrics.VIDEO_UNITS_RECEIVED -ne 0 -or $result.FailureEvent -ne 'RTC_PEER_STATE') {throw 'Failure snapshot failed'}
$untrusted=$sample+"8 t=10s SECRET_TOKEN code=123`n9 t=10s LOGIN_OK code=9999999999`n10 t=10s LOGIN_OK code=0 http=200 secret-text`n"
$json=ConvertFrom-CloudGenshinSafeLog $untrusted | ConvertTo-Json -Depth 5
if($json -match 'SECRET_TOKEN|secret-text' -or (ConvertFrom-CloudGenshinSafeLog $untrusted).LastEvent -ne 'AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF') {throw 'Unknown input was not suppressed'}
if($null -ne (ConvertFrom-CloudGenshinSafeLog 'no valid header')) {throw 'Header validation failed'}
$sdk=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GAME_SDK_LOGIN code=0`n2 t=1s GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT code=0`n3 t=1s GAME_SDK_LOGIN_METADATA_PRESENT code=1`n")
if($sdk.Stage -ne 'GameSdkLogin' -or $sdk.Metrics.GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT -ne 0 -or $sdk.Metrics.GAME_SDK_LOGIN_METADATA_PRESENT -ne 1) {throw 'SDK presence-only diagnostics failed'}
$compat=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED code=1`n2 t=2s GAME_SDK_WILL_ENTER_GAME code=0`n")
if($compat.Stage -ne 'GameEntering' -or $compat.Metrics.GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED -ne 1 -or $compat.Failed) {throw 'Legacy repair / real game-enter transition parsing failed'}
$sdk=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s GAME_SDK_LOGIN_LOCAL_DATA_INVALID code=0`n")
if(!$sdk.Failed -or $sdk.FailureEvent -ne 'GAME_SDK_LOGIN_LOCAL_DATA_INVALID') {throw 'Invalid SDK login was not reported as failure'}
$passport=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s PASSPORT_LOGIN_START code=0`n2 t=2s QR_WAITING code=0 http=200`n")
if($passport.Stage -ne 'NeedsPhoneConfirmation' -or $passport.Failed) {throw 'Passport confirmation was misclassified'}
$passport=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s PASSPORT_WEB_VERIFY code=0`n2 t=2s PASSPORT_VERIFICATION_REQUIRED code=0 http=200`n")
if($passport.Stage -ne 'NeedsOfficialVerification' -or $passport.Failed) {throw 'Official verification needs human action, not a fake success/failure'}
$passport=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s PASSPORT_ACCOUNT_MISMATCH code=0`n")
if(!$passport.Failed -or $passport.FailureEvent -ne 'PASSPORT_ACCOUNT_MISMATCH') {throw 'Account mismatch was not rejected'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_TOKEN_READY code=0`n2 t=2s CLOUD_ACCOUNT_AUTH code=0`n")
if($endfield.Stage -ne 'CloudAccountAuth' -or $endfield.Failed) {throw 'Endfield saved-account exchange stage missing'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_DISPATCH_REQUESTED code=0`n2 t=2s ENDFIELD_GAME_AUTH_REFRESHING code=0`n")
if($endfield.Stage -ne 'GameAuthorization' -or $endfield.Failed) {throw 'Post-dispatch authorization must not look like an idle saved login'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_CHARACTER_ENTERED code=0`n2 t=2s ENDFIELD_BITRATE_REQUEST_SENT code=4000`n3 t=3s ENDFIELD_FPS_REQUEST_FAILED code=60`n")
if($endfield.Stage -ne 'GameCharacterEntered' -or $endfield.Failed -or $endfield.Metrics.ENDFIELD_BITRATE_REQUEST_SENT -ne 4000) {throw 'Optional tuning metrics must not masquerade as actual bandwidth or fatal disconnect'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_AUTH_REFRESHING code=0`n2 t=2s ENDFIELD_GAME_AUTH_FAILED code=11204 http=200`n3 t=3s ENDFIELD_QUEUE_NOT_ACTIVE code=23001 http=200`n")
if(!$endfield.Failed -or $endfield.Stage -ne 'Failed' -or $endfield.FailureCode -ne 11204 -or $endfield.FailureEvent -ne 'ENDFIELD_GAME_AUTH_FAILED') {throw 'Allocation cleanup must not hide the game-grant refusal'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_TOKEN_READY code=0`n2 t=2s ENDFIELD_STREAM_LOGIN_FAILED code=101 http=200`n")
if(!$endfield.Failed -or $endfield.FailureEvent -ne 'ENDFIELD_STREAM_LOGIN_FAILED' -or $endfield.FailureCode -ne 101 -or $endfield.Http -ne 200) {throw 'Endfield API failure was hidden'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_STREAM_STARTING code=0`n2 t=2s SERVER_READY_FOR_GAME code=0`n3 t=3s ENDFIELD_TITLE_CLICK_SENT code=1 http=1`n")
if($endfield.Stage -ne 'WaitingGame' -or $endfield.Metrics.ENDFIELD_TITLE_CLICK_SENT -ne 1 -or $endfield.Failed) {throw 'Endfield stream progress missing'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_TOKEN_READY code=0`n2 t=2s ENDFIELD_SECRET_TOKEN code=123`n")
if($endfield.LastEvent -ne 'ENDFIELD_TOKEN_READY') {throw 'Unknown Endfield event was not suppressed'}
$agreement=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_AGREEMENT_REQUIRED code=-23 http=200`n2 t=2s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if($agreement.Stage -ne 'NeedsOfficialAgreement' -or $agreement.Failed) {throw 'Official consent gate was lost'}
$authFailure=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_AUTH_FAILED code=11204 http=200`n2 t=2s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if(!$authFailure.Failed -or $authFailure.FailureCode -ne 11204) {throw 'Post-queue game authorization failure was hidden'}
$podFailure=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_POD_AUTH_TIMEOUT code=0`n2 t=2s STREAM_WORKER_FINISHED code=0`n")
if(!$podFailure.Failed -or $podFailure.FailureEvent -ne 'ENDFIELD_POD_AUTH_TIMEOUT') {throw 'Unconfirmed pod authorization was hidden'}
$exit=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_EXIT_QUEUED_LOCAL code=1`n2 t=2s ENDFIELD_EXIT_DRAIN_MS code=500`n3 t=3s ENDFIELD_QUEUE_NOT_ACTIVE code=23001 http=200`n")
if($exit.Failed -or $exit.Metrics.ENDFIELD_QUEUE_NOT_ACTIVE -ne 23001 -or $exit.Metrics.ENDFIELD_EXIT_DRAIN_MS -ne 500 -or $exit.Metrics.Contains('ENDFIELD_ALLOCATION_RELEASED')) {throw 'Queue-only cancellation was misclassified as a game-stop confirmation'}
$controlLost=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_CONTROL_CHANNEL_STATE code=1`n2 t=2s ENDFIELD_CONTROL_CHANNEL_STATE code=0`n3 t=3s ENDFIELD_CONTROL_CHANNEL_LOST code=0`n4 t=4s LIVE_VIDEO_DECODED code=0`n")
if(!$controlLost.Failed -or $controlLost.FailureEvent -ne 'ENDFIELD_CONTROL_CHANNEL_LOST' -or $controlLost.Metrics.ENDFIELD_CONTROL_CHANNEL_STATE -ne 0) {throw 'Continuing RTP concealed a lost control connection'}
$appStart=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_DATA_TYPE code=7`n")
if($appStart.Metrics.ENDFIELD_GAME_DATA_TYPE -ne 7 -or $appStart.Stage -eq 'GameCharacterEntered') {throw 'Generic SDK app startup was promoted to character entry'}
$controller=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_INPUT_READY code=1`n2 t=1s ENDFIELD_GAMEPAD_WRITES_ACCEPTED code=4`n3 t=1s ENDFIELD_GAMEPAD_WRITE_FAILURES code=0`n4 t=1s ENDFIELD_GAMEPAD_NEUTRAL_PENDING code=0`n5 t=1s ENDFIELD_ACTIVE_INPUT_VERSION code=3`n")
if($controller.Metrics.ENDFIELD_GAMEPAD_WRITES_ACCEPTED -ne 4 -or $controller.Metrics.ENDFIELD_ACTIVE_INPUT_VERSION -ne 3 -or $controller.Stage -eq 'GameCharacterEntered') {throw 'Controller counters were lost or misclassified as game entry'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s SESSION_KICKED_OFFLINE code=0`n2 t=2s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if(!$endfield.Failed -or !$endfield.Finished -or $endfield.FailureEvent -ne 'SESSION_KICKED_OFFLINE') {throw 'A server kick must remain failed after teardown'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED code=109 http=200`n2 t=5s LIVE_VIDEO_DECODED code=0`n3 t=30s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if($endfield.Stage -ne 'NeedsOfficialDeviceVerification' -or $endfield.Failed) {throw 'Official device verification must remain distinct from a bad saved account'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_SERVER_GAME_STARTED code=0`n")
if($endfield.Stage -ne 'GameProcessStarted') {throw 'A cloud process start is not proof of account or character login'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED code=109`n2 t=2s ENDFIELD_GAME_AUTH_READY code=0`n3 t=3s ENDFIELD_GAME_ACCOUNT_LOGGED_IN code=0`n4 t=4s ENDFIELD_GAME_CHARACTER_ENTERED code=0`n5 t=5s LIVE_VIDEO_DECODED code=0`n6 t=6s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
if($endfield.Stage -ne 'GameCharacterEntered' -or $endfield.Failed) {throw 'Confirmed character entry must survive video and teardown progress'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_CHARACTER_ENTERED code=0`n2 t=2s ENDFIELD_GAME_CHARACTER_LEFT code=0`n")
if($endfield.Stage -ne 'GameCharacterLeft') {throw 'A character logout must clear the entered state'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s ENDFIELD_GAME_CHARACTER_ENTERED code=0`n2 t=2s ENDFIELD_GAME_ACCOUNT_LOGGED_OUT code=0`n3 t=3s SESSION_KICKED_OFFLINE code=0`n")
if(!$endfield.Failed -or $endfield.Stage -ne 'Failed') {throw 'A character entry must not hide a later session failure'}
$endfield=ConvertFrom-CloudGenshinSafeLog ($header+"2000 t=250s ENDFIELD_GAME_LIFECYCLE_STATE code=10`n2001 t=251s LIVE_VIDEO_DECODED code=0`n")
if($endfield.Stage -ne 'GameCharacterEntered') {throw 'A bounded long-run log must retain the source-confirmed game state'}
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('cg-safe-monitor-'+[guid]::NewGuid().ToString('N')+'.log')
try {
    if($null -ne (Read-CloudGenshinSafeLog $fixture)) {throw 'Missing-file handling failed'}
    [IO.File]::WriteAllText($fixture,$sample)
    if((Read-CloudGenshinSafeLog $fixture).FailureEvent -ne 'RTC_PEER_STATE') {throw 'File snapshot handling failed'}
    [IO.File]::WriteAllText($fixture,('X'*131073))
    if($null -ne (Read-CloudGenshinSafeLog $fixture)) {throw 'File bound handling failed'}
} finally {if(Test-Path -LiteralPath $fixture){Remove-Item -LiteralPath $fixture}}
$audio=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=5s AUDIO_PACKETS_DECODED code=100`n2 t=5s AUDIO_SAMPLES_PLAYED code=0`n3 t=5s AUDIO_OUTPUT_LAST_ERROR code=4660`n4 t=5s AUDIO_PAYLOAD_BYTES_RECEIVED code=2000`n5 t=5s ENDFIELD_AUDIO_REQUESTED_BITRATE code=16000`n")
if($audio.Metrics.AUDIO_SAMPLES_PLAYED -ne 0 -or $audio.Metrics.AUDIO_OUTPUT_LAST_ERROR -ne 4660 -or
   $audio.Metrics.AUDIO_PAYLOAD_BYTES_RECEIVED -ne 2000 -or $audio.Metrics.ENDFIELD_AUDIO_REQUESTED_BITRATE -ne 16000) {
    throw 'Decoded audio must remain distinct from actual device playback and requested bitrate'
}
Write-Output 'PASS cloudgenshin_safe_monitor'
$hardware=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s VIDEO_HW_SETUP_STAGE code=4`n2 t=1s VIDEO_HW_SETUP_ERROR code=-38`n3 t=2s VIDEO_HARDWARE_FRAMES code=0`n4 t=2s VIDEO_DECODE_MEAN_US code=3000`n5 t=2s VIDEO_WAITING_IDR_DROPS code=500`n6 t=2s RTC_POLL_GAP_MAX_US code=20000`n")
if($hardware.Failed -or $hardware.Metrics.VIDEO_HW_SETUP_ERROR -ne -38 -or
    $hardware.Metrics.VIDEO_DECODE_MEAN_US -ne 3000 -or $hardware.Metrics.VIDEO_WAITING_IDR_DROPS -ne 500 -or
    $hardware.Metrics.RTC_POLL_GAP_MAX_US -ne 20000) {throw 'Performance diagnostics or nonfatal software fallback was misclassified'}
Write-Output 'PASS cloudgenshin_media_performance_monitor'
$cert=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s RTC_DTLS_REMOTE_PIN_PRESENT code=0`n2 t=2s RTC_DTLS_CERTIFICATE_VERIFY_FAILED code=-9984`n")
if(!$cert.Failed -or $cert.FailureEvent -ne 'RTC_DTLS_CERTIFICATE_VERIFY_FAILED' -or
   $cert.Metrics.RTC_DTLS_REMOTE_PIN_PRESENT -ne 0) {throw 'Certificate failures or pin-presence diagnostics were hidden'}
$milestoneSnapshot=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=90s GAME_SDK_WILL_ENTER_GAME code=0`n2 t=140s AUTOTEST_UI_PROBE_PHASE code=12`n3 t=141s VIDEO_RENDER_SUBMISSIONS code=1000`n")
$milestones=Get-CloudGenshinTestMilestones $milestoneSnapshot $null
if(!$milestones.GameEntrySignalObserved -or !$milestones.RenderSubmissionObserved -or
   $milestones.CharacterEntrySignalObserved -or $milestones.UiProbePhase -ne 12) {throw 'Entry signals must not be promoted to confirmed character entry'}
$rolled=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=1s RTC_PEER_STATE code=2`n5000 t=600s VIDEO_RENDER_SUBMISSIONS code=5000`n")
$milestones=Get-CloudGenshinTestMilestones $rolled $milestones
if(!$milestones.GameEntrySignalObserved -or $milestones.UiProbePhase -ne 12 -or $rolled.Stage -ne 'Rtc') {throw 'Bounded log eviction lost historical evidence or overwrote current stage'}
$failedLater=ConvertFrom-CloudGenshinSafeLog ($header+"5001 t=601s RTC_PEER_STATE code=5`n")
$milestones=Get-CloudGenshinTestMilestones $failedLater $milestones
if(!$failedLater.Failed -or !$milestones.GameEntrySignalObserved) {throw 'Historical entry hid a later connection failure'}
$newRun=ConvertFrom-CloudGenshinSafeLog ($header.Replace('seconds=123','seconds=124')+"1 t=0s NATIVE_CLOUD_ENTRY_READY code=0`n")
$milestones=Get-CloudGenshinTestMilestones $newRun $milestones
if($milestones.GameEntrySignalObserved -or $milestones.RenderSubmissionObserved -or $milestones.UiProbePhase -ne 0) {throw 'Milestones leaked between runs'}
Write-Output 'PASS cloudgenshin_bounded_history_milestones'
$initial=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=10s RTC_RTP_COMPLETED code=125`n2 t=10s RTC_RTP_DECRYPT_FAILURES code=125`n3 t=10s RTC_RTP_LAST_DECRYPT_ERROR code=7`n")
$evidence=Get-CloudGenshinDiagnosticEvidence $initial $null
$evidence.Counters['UNKNOWN_SECRET']='must-not-survive'
$later=ConvertFrom-CloudGenshinSafeLog ($header+"4000 t=124s FIRST_VIDEO_TIMEOUT code=0`n4001 t=124s AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF code=0`n")
$evidence=Get-CloudGenshinDiagnosticEvidence $later $evidence
if(!$later.Failed -or $evidence.Counters.RTC_RTP_DECRYPT_FAILURES -ne 125 -or
   $evidence.Counters.RTC_RTP_LAST_DECRYPT_ERROR -ne 7 -or
   $evidence.ObservedAtSeconds.RTC_RTP_LAST_DECRYPT_ERROR -ne 10 -or
   $evidence.Counters.Contains('UNKNOWN_SECRET')) {throw 'Teardown lost safe historical ingress evidence or hid a failure'}
$newer=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=125s RTC_RTP_LAST_DECRYPT_ERROR code=0`n")
$evidence=Get-CloudGenshinDiagnosticEvidence $newer $evidence
if($evidence.Counters.RTC_RTP_LAST_DECRYPT_ERROR -ne 0 -or
   $evidence.ObservedAtSeconds.RTC_RTP_LAST_DECRYPT_ERROR -ne 125) {throw 'New samples must replace historical values, including zero'}
$evidence=Get-CloudGenshinDiagnosticEvidence $initial $evidence
if($evidence.Counters.RTC_RTP_LAST_DECRYPT_ERROR -ne 0 -or
   $evidence.ObservedAtSeconds.RTC_RTP_LAST_DECRYPT_ERROR -ne 125) {throw 'Out-of-order reads must not replace newer evidence'}
foreach($bad in @('secret', $true, [double]::NaN, [long]2147483648, [decimal]1.5)) {
    $evidence.Counters['VIDEO_FRAME_SAMPLE_COUNT']=$bad
    $evidence.ObservedAtSeconds['VIDEO_FRAME_SAMPLE_COUNT']=1
    $evidence=Get-CloudGenshinDiagnosticEvidence $later $evidence
    if($evidence.Counters.Contains('VIDEO_FRAME_SAMPLE_COUNT')) {throw 'Non-integer or out-of-range historical evidence escaped'}
}
$evidence=Get-CloudGenshinDiagnosticEvidence $newRun $evidence
if($evidence.Counters.Count -ne 0) {throw 'Historical diagnostics leaked into a new run'}
Write-Output 'PASS bounded per-run diagnostic evidence retained after teardown'
$pollBudget=ConvertFrom-CloudGenshinSafeLog ($header+"1 t=5s RTC_SIGNAL_POLL_MAX_US code=100`n2 t=5s RTC_PEER_DRAIN_MAX_US code=2200`n3 t=5s RTC_SESSION_TASKS_MAX_US code=300`n4 t=5s RTC_RECEIVE_BUDGET_HITS code=12`n")
if($pollBudget.Failed -or $pollBudget.Metrics.RTC_PEER_DRAIN_MAX_US -ne 2200 -or
   $pollBudget.Metrics.RTC_RECEIVE_BUDGET_HITS -ne 12) {throw 'Receive stage timing scalars were lost or treated as fatal'}
