# Only this source-defined subset can enter the monitor report. Never copy raw
# emulator output, credentials, QR contents, addresses, SDP or unknown events.
function Test-CloudGenshinDebuggerMarker {
    param([string]$Line)
    if($Line.Length -gt 256) {return $false}
    if($Line -cmatch '^GDB_SCTP_STREAM sid=[01] gap=\d{1,10} first=[01] last=[01] end=[01] partial=[01] bytes=\d{1,10} idata=[01]$') {return $true}
    if($Line -cmatch '^GDB_SCTP_QUEUES sent_bytes=\d{1,10} recv_bytes=\d{1,10} reasm=\d{1,10} ordered=\d{1,10} unacked=\d{1,10} retry=\d{1,10} peer_window=\d{1,10} local_window=\d{1,10} partial=\d{1,10} failed=[01]$') {return $true}
    return $Line -cmatch '^GDB_(?:TARGET_CONNECTED|MAIN_REACHED|LOGIN_VALIDATION_REACHED|QUEUE_FLOW_REACHED|SVC_BREAK reason=\d+|HTTP_CURL_CODE code=\d{1,3}|EXCEPTION_CALLER pc=0x[0-9a-fA-F]{1,16}|CONTROL_CLOSE receive_failed=[01] connected=[01] message_bytes=\d{1,7}|CONTROL_STATE connected=[01] rx_dtls=\d{1,10} rx_sctp=\d{1,10} queued=\d{1,3} failed=[01]|SCTP_FIRST_CHUNKS data=\d{1,6} sack=\d{1,6} heartbeat=\d{1,6} other=\d{1,6} invalid=\d{1,6}|SCTP_ASSOC state=\d{1,3} error=\d{1,5}|CONTROL_DIAG_UNAVAILABLE code=1)$'
}
function ConvertFrom-CloudGenshinSafeLog {
    param([string]$Text)
    if($Text.Length -gt 131072 -or $Text -notmatch '(?m)^(?:yuanshi launcher|CloudGenshin-NX) native alpha (?<Version>[0-9.]{1,20}) / safe events only\r?$') {return $null}
    $version=$Matches.Version
    if($Text -notmatch '(?m)^run_started_unix_seconds=(?<Run>\d{1,12})\r?$') {return $null}
    $run=[long]$Matches.Run
    $stages=@{
        NATIVE_CLOUD_ENTRY_READY='NativeEntry'; SAVED_LOGIN_CHECKING='ValidatingLogin'
        LOGIN_OK='Authenticated'; LOGIN_RESTORED='Authenticated'; LOGIN_SAVED='Authenticated'
        SAVED_LOGIN_REFRESHING='RefreshingLogin'; SAVED_LOGIN_REFRESHED='Authenticated'
        QR_WAITING='NeedsPhoneConfirmation'; QR_SCANNED_CONFIRM_ON_PHONE='NeedsPhoneConfirmation'
        PASSPORT_LOGIN_START='PassportLogin'; PASSPORT_QR_CONFIRMED='PassportVerify'
        PASSPORT_WEB_VERIFY='PassportVerify'; PASSPORT_COMBO_WEB_LOGIN='PassportCombo'
        PASSPORT_VERIFICATION_REQUIRED='NeedsOfficialVerification'
        AUTOTEST_NORMAL_QUEUE_START='PreparingQueue'; CHECK_SERVICE='PreparingQueue'
        GET_PING_SERVERS='PreparingQueue'; GET_NODES_WITH_MEASURED_RTT='PreparingQueue'
        DISPATCH_NORMAL_QUEUE='Queue'; QUEUED_POSITION='Queue'
        DISPATCH_READY_FOR_GAME_AUTH='Dispatched'; STREAM_ENTRY='Stream'
        GAME_SDK_LOGIN_REFRESHING='GameAuthorization'
        SIGNALING_CONNECTING='Signaling'; SIGNALING_UPGRADE_OK='Signaling'
        GAME_AUTH_OK='Rtc'; GAME_AUTH_OK_GAMEPAD_DISABLED='Rtc'
        RTC_CONNECTED_WAITING_VIDEO='WaitingVideo'; RTC_PEER_STATE='Rtc'
        RTC_CONTROL_WAITING_ASSOCIATION='WaitingControlAssociation'
        RTC_CONTROL_WAITING_CHANNEL='WaitingControlChannel'; RTC_CONTROL_READY='ControlReady'
        FIRST_VIDEO_UNIT_RECEIVED='ReceivingVideo'; LIVE_VIDEO_DECODED='DecodedVideo'
        FIRST_FRAME_RENDER_SUBMITTED_NOT_DISPLAY_PROOF='RenderSubmittedNotDisplayProof'
        GAME_SDK_LOGIN='GameSdkLogin'; GAME_SDK_WILL_ENTER_GAME='GameEntering'
        GAME_SDK_ENTER_GAME='GameEntering'
        ENDFIELD_TOKEN_READY='Authenticated'; ENDFIELD_LOGIN_OK='Authenticated'
        CLOUD_ACCOUNT_AUTH='CloudAccountAuth'; ENDFIELD_CLOUD_TICKETS_READY='CloudAccountAuth'
        ENDFIELD_QR_FETCH='Qr'; ENDFIELD_QR_FETCHING='Qr'
        ENDFIELD_QR_WAITING='NeedsPhoneConfirmation'; ENDFIELD_QR_SCANNED='NeedsPhoneConfirmation'
        ENDFIELD_QR_EXCHANGING='CloudAccountAuth'
        ENDFIELD_STREAM_STARTING='Dispatched'
        ENDFIELD_DISPATCH_REQUESTED='PreparingQueue'
        ENDFIELD_GAME_AUTH_REFRESHING='GameAuthorization'
        SERVER_READY_FOR_GAME='WaitingGame'; SERVER_HANDLING_SAVE='HandlingSave'
        ENDFIELD_SERVER_GAME_STARTED='GameProcessStarted'
        ENDFIELD_GAME_ACCOUNT_LOGGED_IN='GameAccountLoggedIn'
        ENDFIELD_GAME_CHARACTER_ENTERED='GameCharacterEntered'
        ENDFIELD_GAME_CHARACTER_LEFT='GameCharacterLeft'
        ENDFIELD_GAME_ACCOUNT_LOGGED_OUT='GameLogin'
        ENDFIELD_GAME_BROWSER_REQUIRED='NeedsGameBrowser'
        ENDFIELD_PREVIOUS_SAVE_PENDING='WaitingPreviousSave'
        ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED='NeedsOfficialDeviceVerification'
        ENDFIELD_GAME_AGREEMENT_REQUIRED='NeedsOfficialAgreement'
    }
    $failures=@('SAVED_LOGIN_REJECTED','SAVED_LOGIN_RETRY_LATER','SAVED_LOGIN_UNREADABLE',
        'QR_FETCH_FAILED','QR_QUERY_API_REJECTED','QR_OFFICIAL_RISK_REJECTED','QR_OFFICIAL_VERIFICATION_REQUIRED',
        'CLOUD_AUTH_FAILED','SERVICE_CHECK_FAILED','PING_ALL_FAILED_NO_NODE_REQUEST','GET_NODES_FAILED',
        'NODE_VERIFY_FAILED','DISPATCH_REJECTED','DISPATCH_NETWORK_ERROR','QUEUE_ACK_FAILED',
        'GAME_AUTH_REJECTED','RTC_CONNECTION_LOST','RTC_DTLS_CERTIFICATE_VERIFY_FAILED','SESSION_KICKED_OFFLINE',
        'RTC_CONNECT_TIMEOUT','FIRST_VIDEO_TIMEOUT','MEDIA_INITIALIZATION_FAILED','STREAM_RUNTIME_FAILED',
        'VIDEO_RENDER_NOT_SUBMITTED','NATIVE_OPERATION_FAILED','AUTOTEST_TIMEOUT')
    $failures+=@('RTC_SDP_UNSUPPORTED','RTC_LOCAL_CANDIDATE_UNAVAILABLE','INPUT_WAKE_NO_VIDEO_RESPONSE')
    $failures+=@('RTC_SCTP_INIT_FAILED','RTC_SCTP_ASSOCIATION_TIMEOUT','RTC_DATA_CHANNEL_TIMEOUT','RTC_CONTROL_CHANNEL_LOST')
    $failures+=@('GAME_SDK_LOGIN_LOCAL_DATA_INVALID','GAME_SDK_DECRYPT_FAILED','GAME_SDK_PARSE_FAILED')
    $failures+=@('GAME_WEB_RESUME_UNAVAILABLE','GAME_WEB_RESUME_FAILED','GAME_WEB_RESUME_API_REJECTED',
        'GAME_WEB_RESUME_VERIFICATION_REQUIRED','GAME_WEB_RESUME_ACCOUNT_MISMATCH',
        'GAME_WEB_RESUME_HTTP_FAILED','GAME_WEB_RESUME_NETWORK_FAILED')
    $failures+=@('SIGNALING_DISCONNECTED','RTC_TRANSPORT_OR_RUNTIME_FAILED','GAME_SDK_SESSION_ENDED')
    $failures+=@('PASSPORT_RESPONSE_INVALID','PASSPORT_NETWORK_FAILED','PASSPORT_API_REJECTED',
        'PASSPORT_ACCOUNT_MISMATCH','PASSPORT_PROFILE_UNSUPPORTED','PASSPORT_SESSION_CONTEXT_MISSING',
        'STAR_RAIL_WEB_LOGIN_UPDATE_REQUIRED')
    $failures+=@('ENDFIELD_TOKEN_UNREADABLE','ENDFIELD_STREAM_LOGIN_FAILED','ENDFIELD_STREAM_NODES_FAILED',
        'ENDFIELD_CLOUD_GRANT_FAILED','ENDFIELD_GAME_TICKET_FAILED',
        'ENDFIELD_STREAM_DISPATCH_FAILED','ENDFIELD_STREAM_QUEUE_FAILED','ENDFIELD_STREAM_SDK_INVALID',
        'ENDFIELD_STREAM_CONNECT_FAILED','ENDFIELD_STREAM_NEED_RESCAN','ENDFIELD_STARTUP_SEND_FAILED',
        'ENDFIELD_QR_NETWORK_FAILED','ENDFIELD_QR_CREATE_FAILED','ENDFIELD_QR_EXPIRED',
        'ENDFIELD_QR_STATUS_REJECTED','ENDFIELD_QR_RESPONSE_INVALID',
        'ENDFIELD_LOGIN_EXCHANGE_FAILED','ENDFIELD_LOGIN_SAVE_FAILED','ENDFIELD_GAME_AUTH_FAILED',
        'ENDFIELD_POD_AUTH_REJECTED','ENDFIELD_POD_AUTH_TIMEOUT','ENDFIELD_CONTROL_CHANNEL_LOST')
    $metrics=@('VIDEO_UNITS_RECEIVED','VIDEO_IDR_UNITS_RECEIVED','VIDEO_DECODED_FRAMES','VIDEO_DROPPED_UNITS',
        'VIDEO_DECODE_ERRORS','VIDEO_LAST_DECODE_ERROR','AUDIO_PACKETS_RECEIVED','VIDEO_RENDER_SUBMISSIONS',
        'VIDEO_FRAME_SAMPLE_LUMA_MEAN','VIDEO_FRAME_SAMPLE_LUMA_MIN','VIDEO_FRAME_SAMPLE_LUMA_MAX',
        'VIDEO_FRAME_SAMPLE_CONTRAST','VIDEO_FRAME_SAMPLE_NONBLACK','VIDEO_FRAME_SAMPLE_COUNT',
        'RTC_DTLS_LOCAL_ROLE','RTC_DTLS_LAST_RESULT','RTC_DTLS_DATAGRAMS_SENT','RTC_DTLS_DATAGRAMS_RECEIVED',
        'RTC_ICE_LOCAL_CANDIDATES','RTC_ICE_REMOTE_CANDIDATES','RTC_ICE_CANDIDATE_PAIRS',
        'RTC_ICE_REQUESTS_SENT','RTC_ICE_RESPONSES_RECEIVED','RTC_ICE_RESPONSES_REJECTED',
        'RTC_SDP_ANSWER_MISMATCH_MASK','RTC_SDP_NEGOTIATED_ANSWER_MISMATCH_MASK','RTC_REMOTE_ICE_LITE',
        'RTC_SDP_UNSUPPORTED_REASON',
        'RTC_UDP_DATAGRAMS_RECEIVED','RTC_UDP_SEND_ERRORS','RTC_UDP_RECEIVE_ERRORS','RTC_UDP_SELECT_ERRORS','RTC_UDP_LAST_ERROR',
        'GAME_CONFIG_XBOX','GAME_CONFIG_PS','GAME_CONFIG_GR0006','GAME_CONFIG_WEBCTRL',
        'GAMEPAD_LIST_ANNOUNCED','GAMEPAD_ONLY_MODE_ACTIVE','GAMEPAD_SERVER_CONFIG_OFF_FORCED',
        'GAMEPAD_INPUT_BLOCKED_BY_SERVER_CONFIG','KEYBOARD_MOUSE_INPUT_ACTIVE',
        'INPUT_WAKE_GAMEPAD_SENT','INPUT_WAKE_CLICK_SENT','INPUT_WAKE_CLICK_RESENT','VIDEO_RESUMED_AFTER_INPUT',
        'INPUT_LOCAL_PRESS_SAMPLED','STREAM_SETTINGS_SAVED','STREAM_SETTINGS_SAVE_FAILED',
        'RTC_SCTP_ASSOCIATION_UP','RTC_REMOTE_DATA_CHANNEL_OPEN','CONTROL_HEARTBEAT_STARTED',
        'INPUT_CONTINUE_REQUESTED_WAITING_READY','INPUT_CONTINUE_SEND_FAILED',
        'STREAM_QUALITY_DEFAULT_TRAFFIC_SAVING','STREAM_QUALITY_SENT','STREAM_QUALITY_SEND_FAILED',
        'STREAM_FPS_SENT','STREAM_FPS_SEND_FAILED')
    $metrics+=@('GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT','GAME_SDK_LOGIN_METADATA_PRESENT',
        'GAME_SDK_LOGIN_ACCOUNT_TYPE_PRESENT','GAME_SDK_LOGIN_COMBO_ID_PRESENT',
        'AUTOTEST_ENTRY_TIMED_ARM','AUTOTEST_ENTRY_SCENE_CHANGED','AUTOTEST_ENTRY_PAD_SENT')
    $metrics+=@('PASSPORT_STOKEN_VERIFIED','LOGIN_CHANNEL_TOKEN_PRESENT',
        'DISPATCH_PREPARE_NETWORK_OPERATION','DISPATCH_PREPARE_NETWORK_ERROR')
    $metrics+=@('HTTP_FAILURE_PHASE','HTTP_FAILURE_DNS_MS','HTTP_FAILURE_CONNECT_MS','HTTP_FAILURE_TLS_MS',
        'HTTP_FAILURE_READY_MS','HTTP_FAILURE_FIRST_BYTE_MS','HTTP_FAILURE_TOTAL_MS','HTTP_FAILURE_UPLOADED_BYTES')
    $metrics+=@('GAME_WEB_RESUME_SOURCE','GAME_WEB_RESUME_REJECTION_HINT_MASK','GAME_WEB_RESUME_STAGE','GAME_WEB_RESUME_OK','GAME_WEB_RESUME_FAILED','GAME_WEB_RESUME_API_REJECTED',
        'GAME_WEB_RESUME_VERIFICATION_REQUIRED','GAME_WEB_RESUME_ACCOUNT_MISMATCH',
        'GAME_WEB_RESUME_HTTP_FAILED','GAME_WEB_RESUME_NETWORK_FAILED')
    $metrics+=@('GAME_SDK_REPORT_BODY_KIND','GAME_SDK_REPORT_RETCODE','GAME_SDK_REPORT_ERROR_CODE',
        'GAME_SDK_REPORT_HTTP_CODE','GAME_SDK_REPORT_LOGIN_35_4301')
    $metrics+=@('GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED')
    $metrics+=@('GAME_SDK_REPORT_NET_1001_2','GAME_SDK_REPORT_FIRST_NONZERO_RETCODE','GAME_SDK_REPORT_NETWORK_HINT_MASK')
    $metrics+=@('SAVED_LOGIN_REFRESHED','LOGIN_RESTORED','LOGIN_SAVED','AUTOTEST_STAR_RAIL_POINTER_ENTRY',
        'GAME_SDK_LOGIN_H5_ADDITIONAL_ROOT_FIELDS','GAME_SDK_LOGIN_REFRESHED','GAME_SDK_LOGIN_REFRESH_FAILED','GAME_SDK_LOGIN_REFRESH_UNAVAILABLE',
        'GAME_SDK_LOGIN_REFRESH_ACCOUNT_MISMATCH','GAME_SDK_LOGIN_USING_EXISTING_GRANT')
    $metrics+=@('GENSHIN_INPUT_MODE_SAVED','GENSHIN_ANDROID_PS5_EXPERIMENT_ACTIVE',
        'XBOX_INPUT_MODE_ACTIVE','GENSHIN_LOCAL_GYRO_ZL_DEFAULT_READY',
        'GYRO_LOCAL_ZL_TICKS','GYRO_LOCAL_COMPOSE_TICKS','GYRO_LOCAL_SEND_FAILURES',
        'GYRO_LOCAL_STICK_X','GYRO_LOCAL_STICK_Y','GYRO_STICK_ASSIST_FRAMES_SENT',
        'GYRO_LOCAL_ENABLED','GYRO_LOCAL_AIM_ONLY','GYRO_LOCAL_SENSITIVITY',
        'GENSHIN_WEB_PS5_EXPERIMENT_ACTIVE','GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE',
        'GENSHIN_STANDARD_INPUT_ACTIVE','STREAM_TERMINAL_TYPE',
        'GAMEPAD_IDENTITY','GAMEPAD_VENDOR_ID','GAMEPAD_PRODUCT_ID','SDK_DEVICE_INFO_SENT','SDK_DEVICE_INFO_BEFORE_FIRST_FRAME',
        'RTC_NEGOTIATED_VIDEO_PAYLOAD','RTC_NEGOTIATED_AUDIO_PAYLOAD',
        'MEDIA_STARTUP_NO_AV_20S','MEDIA_STARTUP_AUDIO_ONLY_20S','MEDIA_STARTUP_VIDEO_RTP_NO_UNIT_20S',
        'MEDIA_STARTUP_VIDEO_UNITS_NOT_DECODED_20S','MOTION_SERVER_REQUESTED','MOTION_ACTIVATION_RECEIVED',
        'MOTION_SAMPLE_FRESH','MOTION_ORIENTATION_READY','MOTION_REPORTS_SENT','MOTION_ORIENTATION_REPORTS_SENT',
        'MOTION_ANDROID_PS5_NO_ACTIVATION_20S','MOTION_WEB_PS5_NO_ACTIVATION_20S',
        'MOTION_CURRENT_PS5_NO_ACTIVATION_20S','MOTION_CURRENT_PS5_FIRST_LOCAL_SEND_NOT_ACK',
        'SAVED_LOGIN_NETWORK_ERROR','CLOUD_AUTH_NETWORK_ERROR','AUTOTEST_LONG_QUEUE_WAIT_ENABLED','QUEUED_POSITION',
        'QUEUE_TOTAL_LENGTH','QUEUE_BRANCH_LENGTH')
    $metrics+=@('RTC_RTP_COMPLETED','RTC_RTP_DECRYPT_FAILURES','RTC_RTP_LAST_DECRYPT_ERROR',
        'RTC_RTCP_COMPLETED','RTC_RTCP_DECRYPT_FAILURES','RTC_RTCP_LAST_DECRYPT_ERROR',
        'RTC_RTP_LAST_PAYLOAD_TYPE','RTC_RTP_PAYLOAD_REJECTED','RTC_RTP_SSRC_REJECTED',
        'RTC_RTP_AUDIO_ROUTED','RTC_RTP_VIDEO_ROUTED','MEDIA_STARTUP_SRTP_REJECTED_20S',
        'MEDIA_STARTUP_PAYLOAD_REJECTED_20S','MEDIA_STARTUP_SSRC_REJECTED_20S','MEDIA_STARTUP_RTP_NO_OUTPUT_20S')
    $metrics+=@('DISPATCH_DEFAULT_GAMEPAD_REQUESTED','DISPATCH_BIT_RATE','STREAM_SETTINGS_RESTORED','LAUNCHER_POINTER_MOUSE','LAUNCHER_POINTER_TOUCH',
        'AUTOTEST_PAD_PROBE_PHASE','SERVER_GAME_CONTROL_MODE','GAMEPAD_SERVER_MODE_NOT_REPORTED_20S')
    $metrics+=@('CONTROLLER_SOURCE_HANDHELD_JOYCON','CONTROLLER_SOURCE_PAIRED_OR_PRO',
        'CONTROLLER_SOURCE_DISCONNECTED','AUTO_ENTRY_GAME_ENTERED_STOPPED','AUTO_ENTRY_TAP_SENT',
        'AUTO_ENTRY_PAD_TAP_SENT','GAME_SDK_DEVICE_FINGERPRINT_PRESENT','INPUT_SOURCE_DECLARED',
        'QUEUE_ABANDONED_TOO_LONG')
    $metrics+=@('PING_TARGETS_TESTED','PING_TARGETS_SUCCEEDED','PING_INVALID_ENDPOINT_COUNT',
        'PING_CONNECT_FAILED_COUNT','PING_CONNECT_TIMEOUT_COUNT','PING_ECHO_FAILED_COUNT','PING_FIRST_CONNECTION_ERROR',
        'PING_FIRST_TRANSPORT_STAGE','PING_FIRST_ELAPSED_MS','PING_FIRST_PORT','PING_FIRST_OS_ERROR',
        'PING_FIRST_FAILURE_PHASE','PING_FIRST_DNS_MS','PING_FIRST_CONNECT_MS','PING_FIRST_TLS_MS','PING_FIRST_READY_MS','PING_FIRST_TOTAL_MS')
    $metrics+=@('SDK_DEVICE_FP_CONSENT_REQUIRED_NOT_SENT','SDK_DEVICE_FP_STAGE','SDK_DEVICE_FP_SERVICE_READY','SDK_DEVICE_FP_VERIFICATION_REQUIRED','SDK_DEVICE_FP_HTTP_FAILED',
        'SDK_DEVICE_FP_RESPONSE_REJECTED','SDK_DEVICE_FP_NETWORK_FAILED','SDK_DEVICE_FP_RESPONSE_INVALID',
        'SDK_DEVICE_FP_UNAVAILABLE_NO_LOCAL_SUBSTITUTE')
    $metrics+=@('ENDFIELD_NODE_BUSY','ENDFIELD_TITLE_CLICK_SENT','ENDFIELD_INTERCEPT_APP_SENT',
        'ENDFIELD_ALLOCATION_RELEASED','ENDFIELD_ALLOCATION_RELEASE_FAILED',
        'ENDFIELD_QUEUE_CANCELLED','ENDFIELD_QUEUE_NOT_ACTIVE','ENDFIELD_QUEUE_CANCEL_FAILED',
        'ENDFIELD_EXIT_QUEUED_LOCAL','ENDFIELD_EXIT_SEND_FAILED','ENDFIELD_EXIT_DRAIN_MS',
        'ENDFIELD_CONTROL_CHANNEL_STATE',
        'ENDFIELD_GAME_DATA_TYPE',
        'ENDFIELD_CONTROL_MESSAGES_SENT','ENDFIELD_CONTROL_SEND_FAILURES','ENDFIELD_WEILINK_MESSAGES',
        'RTC_SCTP_CREATE_ATTEMPTED','RTC_SCTP_CREATE_RESULT')
    $metrics+=@('VIDEO_DECODE_SUBMITTED','VIDEO_DECODE_ZERO_OUTPUT','VIDEO_DECODE_QUEUE_DEPTH',
        'VIDEO_LAST_UNIT_BYTES','VIDEO_SMALL_UNITS','VIDEO_MEDIUM_UNITS','VIDEO_LARGE_UNITS',
        'VIDEO_RTP_PACKETS','VIDEO_RTP_GAPS','VIDEO_RTP_AU_DROPPED','VIDEO_RTP_NACK_REQUESTS',
        'AUDIO_PACKETS_DECODED','AUDIO_DECODE_ERRORS','AUDIO_IN_PEAK','AUDIO_IN_RMS',
        'ENDFIELD_AUTH_RESULT','ENDFIELD_PIPE_STATE','ENDFIELD_BITRATE_CHANGE','ENDFIELD_GINPUT_LEVEL')
    $metrics+=@('ENDFIELD_GAME_BRIDGE_STARTED','ENDFIELD_GAME_BRIDGE_REQUEST',
        'ENDFIELD_GAME_BRIDGE_CONFIRMED','ENDFIELD_GAME_BRIDGE_EXPIRED','ENDFIELD_GAME_BRIDGE_UNHANDLED_HASH')
    $metrics+=@('ENDFIELD_GAME_AUTH_READY','ENDFIELD_GAME_AUTH_FAILED')
    $metrics+=@('ENDFIELD_ALTERNATE_NODE_SELECTED')
    $metrics+=@('ENDFIELD_GAME_LIFECYCLE_STATE')
    $metrics+=@('ENDFIELD_GAME_RESOLUTION_SENT')
    $metrics+=@('VIDEO_HW_SETUP_STAGE','VIDEO_HW_SETUP_ERROR','VIDEO_HARDWARE_FRAMES',
        'RTC_DTLS_REMOTE_PIN_PRESENT','VIDEO_UI_INTERVAL_PRESENT_SKIPS','VIDEO_GAMEPLAY_PRESENT_SKIPS',
        'VIDEO_HW_RUNTIME_ERROR','VIDEO_HW_FALLBACKS','VIDEO_HW_DIAGNOSTIC_TRANSFER_FAILED',
        'VIDEO_HW_PROBE_OPTED_IN','VIDEO_HW_DISABLED_PENDING_DEVICE_VALIDATION',
        'VIDEO_CONGESTION_EVENTS','VIDEO_CONGESTION_DROPS','VIDEO_WAITING_IDR_DROPS',
        'VIDEO_RESYNC_EVENTS','VIDEO_RESYNC_QUEUE_DROPS','VIDEO_STALE_GENERATION_DROPS',
        'VIDEO_DECODE_MEAN_US','VIDEO_DECODE_MAX_US','VIDEO_QUEUE_WAIT_MEAN_US','VIDEO_QUEUE_WAIT_MAX_US',
        'VIDEO_PRESENT_QUEUE_DROPS','VIDEO_PAYLOAD_KBYTES','RTC_POLL_GAP_MEAN_US','RTC_POLL_GAP_MAX_US',
        'VIDEO_RTP_REORDERED','VIDEO_RTP_LATE_DROPPED','VIDEO_RTP_FORCED_SKIPS','VIDEO_RTP_BUFFERED')
    $metrics+=@('ENDFIELD_AUDIO_REQUESTED_BITRATE','AUDIO_PAYLOAD_BYTES_RECEIVED',
        'AUDIO_OUTPUT_READY','AUDIO_OUTPUT_STARTED','AUDIO_OUTPUT_ERRORS','AUDIO_OUTPUT_LAST_ERROR',
        'AUDIO_SAMPLES_SUBMITTED','AUDIO_SAMPLES_PLAYED','AUDIO_OUTPUT_DROPS','AUDIO_OUTPUT_UNDERRUNS',
        'AUDIO_OUT_PEAK','AUDIO_OUT_RMS')
    $metrics+=@('ENDFIELD_GAMEPAD_CONNECTION_SENT','ENDFIELD_GAMEPAD_WRITES_ACCEPTED',
        'ENDFIELD_GAMEPAD_WRITE_FAILURES','ENDFIELD_INPUT_READY','ENDFIELD_GAMEPAD_NEUTRAL_PENDING',
        'ENDFIELD_ACTIVE_INPUT_VERSION','ENDFIELD_GAMEPAD_RELEASE_QUEUED','ENDFIELD_GAMEPAD_RELEASE_NOT_QUEUED')
    $metrics+=@('ENDFIELD_ENTRY_CONFIRM_SENT')
    # Optional tuning failure is not a fatal stream failure. Values only;
    # actual achieved throughput/FPS are separate media counters.
    $metrics+=@('ENDFIELD_BITRATE_REQUESTED','ENDFIELD_BITRATE_REQUEST_SENT',
        'ENDFIELD_BITRATE_REQUEST_FAILED','ENDFIELD_FPS_REQUEST_FAILED',
        'ENDFIELD_STREAM_SETTINGS_SAVED','ENDFIELD_STREAM_SETTINGS_SAVE_FAILED')
    $metrics+=@('ENDFIELD_INPUT_VERSION_NEGOTIATED')
    $metrics+=@('ENDFIELD_KEYFRAME_REQUESTS','VIDEO_DECODE_RESYNC_REQUESTED')
    $metrics+=@('VIDEO_SUBMITTED_UNITS','VIDEO_ZERO_OUTPUT','VIDEO_QUEUE_DEPTH','VIDEO_CORRUPT_FRAMES',
        'VIDEO_LAST_UNIT_IDR','VIDEO_LAST_NAL_MASK','VIDEO_LAST_NAL_SLICES','VIDEO_LAST_NAL_FIRST')
    $metrics+=@('QUEUE_POLL_NETWORK_RETRY','AUTOTEST_UI_PROBE_PHASE','AUTOTEST_UI_STOPPED_USER_INPUT')
    $metrics+=@('RTC_SIGNAL_POLL_MAX_US','RTC_PEER_DRAIN_MAX_US','RTC_SESSION_TASKS_MAX_US','RTC_RECEIVE_BUDGET_HITS')
    $metrics+=@('VIDEO_RENDER_DEFERRED_SUBMISSIONS','VIDEO_PRESENT_REFERENCE_FAILURES',
        'VIDEO_PRESENT_CLOCK_REBASES','VIDEO_PRESENT_TIMING_HOLDS')
    $metrics+=@('VIDEO_RENDER_LOW_COST_MODE','VIDEO_HW_PREFERRED_EXPERIMENTAL','VIDEO_HW_UNCLEAN_OR_STORAGE_GUARD_SOFTWARE',
        'VIDEO_SOFTWARE_SELECTED','VIDEO_DECODER_PREFERENCE_SAVED','VIDEO_DECODER_PREFERENCE_SAVE_FAILED',
        'DEBUG_WINDOW_SAMPLE_BEGIN','DEBUG_WINDOW_SAMPLE_END','DEBUG_WINDOW_SAMPLE','DEBUG_WINDOW_DURATION_MS',
        'DEBUG_WINDOW_UI_MILLIFPS','DEBUG_WINDOW_RECEIVE_MILLIFPS','DEBUG_WINDOW_DECODE_MILLIFPS',
        'DEBUG_WINDOW_SUBMIT_MILLIFPS','DEBUG_WINDOW_UI_MAX_US','DEBUG_WINDOW_POLL_MAX_US',
        'DEBUG_WINDOW_UI_OVER_50MS','DEBUG_WINDOW_COMMAND_BUSY','DEBUG_WINDOW_UPLOAD_BUSY',
        'DEBUG_WINDOW_HW_REF_FAILED','DEBUG_WINDOW_MAPPING_FAILED','DEBUG_WINDOW_HW_FRAMES','DEBUG_WINDOW_SNAPSHOT_BUSY')
    $metrics+=@('DEBUG_UI_SESSION_MAX_US','DEBUG_COMMAND_BUSY_TOTAL','DEBUG_UPLOAD_BUSY_TOTAL',
        'DEBUG_HW_REF_FAILED_TOTAL','DEBUG_MAPPING_FAILED_TOTAL')
    $metrics+=@('LOG_HISTORY_LIMIT_MIB','LOG_BACKGROUND_WRITER_UNAVAILABLE_MEMORY_ONLY',
        'VIDEO_BUFFER_ALLOCATIONS','VIDEO_BUFFER_REUSES','VIDEO_BUFFER_ALLOCATION_FAILURES','VIDEO_BUFFER_POOL_BYTES',
        'DEBUG_DECODE_OVER_16MS','DEBUG_DECODE_OVER_33MS','DEBUG_DECODE_RECENT_MAX_US',
        'DEBUG_UI_INPUT_MEAN_US','DEBUG_UI_VIDEO_MEAN_US','DEBUG_UI_TAIL_MEAN_US','DEBUG_UI_OUTSIDE_MAX_US')
    $metrics+=@('VIDEO_FRAME_PIXEL_FORMAT','VIDEO_FRAME_WIDTH','VIDEO_FRAME_HEIGHT',
        'VIDEO_FRAME_DUMP_WRITTEN','VIDEO_HW_DIAGNOSTIC_TRANSFER_FAILED')
    $terminal=@('AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF','AUTOTEST_LAUNCH_FINISHED')
    $launchFinished=$false;$streamHandoff=$false
    # These are per-endpoint diagnostics: the session can succeed on the next
    # official endpoint. Do not kill that recovery before it finishes.
    $attemptFailures=@('SIGNALING_TCP_TLS_FAILED','SIGNALING_UPGRADE_HTTP_REJECTED')
    $pendingSignalingFailure=$null
    $needsDeviceVerification=$false
    $needsAgreement=$false
    $endfieldGameStage=$null
    $endfieldLifecycleStages=@{9='GameAccountLoggedIn';10='GameCharacterEntered';11='GameCharacterLeft';12='GameLogin';15='NeedsGameBrowser'}
    $report=[ordered]@{Version=$version;Run=$run;Stage='NativeEntry';LastEvent='NATIVE_CLOUD_ENTRY_READY';
        Sequence=0;ElapsedSeconds=0;Code=0;Http=0;Failed=$false;FailureEvent='';FailureCode=0;Finished=$false;Metrics=[ordered]@{}}
    foreach($line in ($Text -split '\r?\n')) {
        if($line -notmatch '^(?<Seq>\d{1,8}) t=(?<Time>\d{1,8})s (?<Event>[A-Z0-9_]{1,96}) code=(?<Code>-?\d{1,10})(?: http=(?<Http>\d{1,3}))?$') {continue}
        $event=$Matches.Event; $numeric=[long]$Matches.Code
        if($event -eq 'ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED') {$needsDeviceVerification=$true}
        if($event -eq 'ENDFIELD_GAME_AGREEMENT_REQUIRED') {$needsAgreement=$true}
        if($event -eq 'ENDFIELD_GAME_AUTH_READY') {$needsDeviceVerification=$false;$needsAgreement=$false}
        if($numeric -lt [int]::MinValue -or $numeric -gt [int]::MaxValue) {continue}
        if(!$stages.ContainsKey($event) -and $event -notin $failures -and $event -notin $metrics -and $event -notin $terminal -and $event -notin $attemptFailures -and $event -ne 'STREAM_WORKER_FINISHED') {continue}
        if($event -in @('ENDFIELD_GAME_ACCOUNT_LOGGED_IN','ENDFIELD_GAME_CHARACTER_ENTERED',
                       'ENDFIELD_GAME_CHARACTER_LEFT','ENDFIELD_GAME_ACCOUNT_LOGGED_OUT',
                       'ENDFIELD_GAME_BROWSER_REQUIRED')) {
            $endfieldGameStage=$stages[$event]
        }
        if($event -eq 'ENDFIELD_GAME_LIFECYCLE_STATE' -and $endfieldLifecycleStages.ContainsKey([int]$numeric)) {
            $endfieldGameStage=$endfieldLifecycleStages[[int]$numeric]
        }
        if($event -in $attemptFailures) {$pendingSignalingFailure=@{Event=$event;Code=[int]$numeric}}
        if($event -eq 'SIGNALING_UPGRADE_OK' -or $event -eq 'GAME_AUTH_OK') {$pendingSignalingFailure=$null}
        if($event -eq 'STREAM_WORKER_FINISHED' -and $pendingSignalingFailure -and !$report.Failed) {
            $report.Failed=$true;$report.FailureEvent=$pendingSignalingFailure.Event
            $report.FailureCode=$pendingSignalingFailure.Code;$report.Stage='Failed'
        }
        $report.LastEvent=$event; $report.Sequence=[int]$Matches.Seq; $report.ElapsedSeconds=[int]$Matches.Time
        $report.Code=[int]$numeric; $report.Http=if($Matches.Http){[int]$Matches.Http}else{0}
        if($stages.ContainsKey($event)) {$report.Stage=$stages[$event]}
        if($event -in $metrics) {$report.Metrics[$event]=[int]$numeric}
        if($event -in $failures -or ($event -eq 'RTC_PEER_STATE' -and $numeric -eq 5)) {
            if(!$report.Failed) {$report.FailureEvent=$event;$report.FailureCode=[int]$numeric}
            $report.Failed=$true;$report.Stage='Failed'
        }
        if($event -eq 'DISPATCH_READY_FOR_GAME_AUTH' -or $event -eq 'STREAM_ENTRY') {$streamHandoff=$true}
        if($event -eq 'AUTOTEST_LAUNCH_FINISHED') {$launchFinished=$true}
        elseif($event -in $terminal) {$report.Finished=$true}
    }
    # Older NROs can publish launcher-finished between the worker finishing and
    # the next UI frame consuming its startup payload. That is not stream end.
    if($launchFinished -and !$streamHandoff) {$report.Finished=$true}
    if($report.Failed) {$report.Stage='Failed'}
    elseif($needsDeviceVerification) {$report.Stage='NeedsOfficialDeviceVerification'}
    elseif($needsAgreement) {$report.Stage='NeedsOfficialAgreement'}
    elseif($endfieldGameStage) {$report.Stage=$endfieldGameStage}
    return [pscustomobject]$report
}

# Historical evidence only, not a claim that the current frame is gameplay.
# Keep fixed booleans/scalars across the bounded event ring; failures still
# come from the current snapshot and always retain their normal precedence.
function Get-CloudGenshinTestMilestones {
    param($Snapshot,$Previous)
    $result=[ordered]@{Run=$Snapshot.Run;GameEntrySignalObserved=$false;
        CharacterEntrySignalObserved=$false;RenderSubmissionObserved=$false;UiProbePhase=0}
    if($Previous -and $Previous.Run -eq $Snapshot.Run) {
        $result.GameEntrySignalObserved=($Previous.GameEntrySignalObserved -eq $true)
        $result.CharacterEntrySignalObserved=($Previous.CharacterEntrySignalObserved -eq $true)
        $result.RenderSubmissionObserved=($Previous.RenderSubmissionObserved -eq $true)
        if($Previous.UiProbePhase -ge 0 -and $Previous.UiProbePhase -le 12) {
            $result.UiProbePhase=[int]$Previous.UiProbePhase
        }
    }
    if($Snapshot.Stage -eq 'GameEntering') {$result.GameEntrySignalObserved=$true}
    if($Snapshot.Stage -eq 'GameCharacterEntered') {$result.CharacterEntrySignalObserved=$true}
    if($Snapshot.Metrics.VIDEO_RENDER_SUBMISSIONS -gt 0) {$result.RenderSubmissionObserved=$true}
    $phase=$Snapshot.Metrics.AUTOTEST_UI_PROBE_PHASE
    if($null -ne $phase -and $phase -ge $result.UiProbePhase -and $phase -le 12) {
        $result.UiProbePhase=[int]$phase
    }
    return [pscustomobject]$result
}

function Get-CloudGenshinCompletionOutcome {
    param($Snapshot,$Evidence=$null)
    if(!$Snapshot -or !$Snapshot.Finished) {return $null}
    if($Snapshot.FailureEvent -eq 'STAR_RAIL_WEB_LOGIN_UPDATE_REQUIRED') {return 'NeedsOfficialWebSignIn'}
    if($Snapshot.Failed) {return 'NroTestFailed'}
    # A game-reported error is not an RTC/media transport failure, but it also
    # cannot be hidden behind a successful bounded media test. Retain the fixed
    # numeric marker across ring-log eviction, never a report body.
    if($Snapshot.Metrics.GAME_SDK_REPORT_NET_1001_2 -eq 1 -or
       ($Evidence -and $Evidence.Run -eq $Snapshot.Run -and
        $Evidence.Counters.GAME_SDK_REPORT_NET_1001_2 -eq 1)) {return 'GameReportedNetworkError1001_2'}
    if(($Snapshot.Metrics.Contains('GAME_SDK_REPORT_FIRST_NONZERO_RETCODE') -and
        $Snapshot.Metrics.GAME_SDK_REPORT_FIRST_NONZERO_RETCODE -ne 0) -or
       ($Evidence -and $Evidence.Run -eq $Snapshot.Run -and
        $Evidence.Counters.Contains('GAME_SDK_REPORT_FIRST_NONZERO_RETCODE') -and
        $Evidence.Counters.GAME_SDK_REPORT_FIRST_NONZERO_RETCODE -ne 0)) {
        return 'GameReportedNonzeroSdkRetcode'
    }
    if($Snapshot.Metrics.QUEUE_ABANDONED_TOO_LONG -gt 100) {return 'QueueStoppedByPolicy'}
    return 'TestEndedNotGameplayProof'
}

# Last observed numeric evidence, not the current live transport state. Keep a
# fixed subset across bounded log eviction/teardown, scoped to exactly one run.
# Never merge unknown fields, response text or arbitrary caller-supplied keys.
function Get-CloudGenshinDiagnosticEvidence {
    param($Snapshot,$Previous)
    $names=@('STREAM_TERMINAL_TYPE','GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE','GAMEPAD_VENDOR_ID','GAMEPAD_PRODUCT_ID',
        'XBOX_INPUT_MODE_ACTIVE','GYRO_LOCAL_ZL_TICKS','GYRO_LOCAL_COMPOSE_TICKS',
        'GYRO_LOCAL_SEND_FAILURES','GYRO_STICK_ASSIST_FRAMES_SENT',
        'RTC_NEGOTIATED_VIDEO_PAYLOAD','RTC_NEGOTIATED_AUDIO_PAYLOAD',
        'PING_TARGETS_TESTED','PING_TARGETS_SUCCEEDED','PING_FIRST_CONNECTION_ERROR',
        'PING_FIRST_TRANSPORT_STAGE','PING_FIRST_ELAPSED_MS','PING_FIRST_PORT','PING_FIRST_OS_ERROR',
        'PING_FIRST_FAILURE_PHASE','PING_FIRST_DNS_MS','PING_FIRST_CONNECT_MS','PING_FIRST_TLS_MS','PING_FIRST_READY_MS','PING_FIRST_TOTAL_MS',
        'RTC_RTP_COMPLETED','RTC_RTP_DECRYPT_FAILURES','RTC_RTP_LAST_DECRYPT_ERROR',
        'RTC_RTCP_COMPLETED','RTC_RTCP_DECRYPT_FAILURES','RTC_RTCP_LAST_DECRYPT_ERROR',
        'RTC_RTP_LAST_PAYLOAD_TYPE','RTC_RTP_PAYLOAD_REJECTED','RTC_RTP_SSRC_REJECTED',
        'RTC_RTP_AUDIO_ROUTED','RTC_RTP_VIDEO_ROUTED','VIDEO_UNITS_RECEIVED',
        'VIDEO_DECODED_FRAMES','VIDEO_HARDWARE_FRAMES','VIDEO_RENDER_SUBMISSIONS',
        'VIDEO_FRAME_SAMPLE_COUNT','VIDEO_FRAME_PIXEL_FORMAT','VIDEO_FRAME_WIDTH','VIDEO_FRAME_HEIGHT',
        'VIDEO_FRAME_DUMP_WRITTEN','VIDEO_HW_DIAGNOSTIC_TRANSFER_FAILED',
        'AUDIO_PACKETS_RECEIVED','AUDIO_SAMPLES_PLAYED',
        'SDK_DEVICE_INFO_SENT','SDK_DEVICE_INFO_BEFORE_FIRST_FRAME','GAME_CONFIG_PS',
        'SDK_DEVICE_FP_CONSENT_REQUIRED_NOT_SENT','SDK_DEVICE_FP_STAGE','SDK_DEVICE_FP_SERVICE_READY','SDK_DEVICE_FP_VERIFICATION_REQUIRED','SDK_DEVICE_FP_HTTP_FAILED',
        'SDK_DEVICE_FP_RESPONSE_REJECTED','SDK_DEVICE_FP_NETWORK_FAILED','SDK_DEVICE_FP_RESPONSE_INVALID',
        'SDK_DEVICE_FP_UNAVAILABLE_NO_LOCAL_SUBSTITUTE','GAME_SDK_DEVICE_FINGERPRINT_PRESENT',
        'GAME_CONFIG_XBOX','AUTOTEST_PAD_PROBE_PHASE','MOTION_ACTIVATION_RECEIVED',
        'MOTION_REPORTS_SENT','MOTION_ORIENTATION_REPORTS_SENT',
        'MEDIA_STARTUP_SRTP_REJECTED_20S','MEDIA_STARTUP_NO_AV_20S','GAME_SDK_REPORT_NET_1001_2',
        'GAME_SDK_REPORT_FIRST_NONZERO_RETCODE','GAME_SDK_REPORT_NETWORK_HINT_MASK','DISPATCH_PREPARE_NETWORK_OPERATION','DISPATCH_PREPARE_NETWORK_ERROR',
        'HTTP_FAILURE_PHASE','HTTP_FAILURE_DNS_MS','HTTP_FAILURE_CONNECT_MS','HTTP_FAILURE_TLS_MS',
        'HTTP_FAILURE_READY_MS','HTTP_FAILURE_FIRST_BYTE_MS','HTTP_FAILURE_TOTAL_MS','HTTP_FAILURE_UPLOADED_BYTES',
        'GAME_WEB_RESUME_SOURCE','GAME_WEB_RESUME_REJECTION_HINT_MASK','GAME_WEB_RESUME_STAGE','GAME_WEB_RESUME_OK','GAME_WEB_RESUME_FAILED','GAME_WEB_RESUME_API_REJECTED',
        'GAME_WEB_RESUME_VERIFICATION_REQUIRED','GAME_WEB_RESUME_ACCOUNT_MISMATCH',
        'GAME_WEB_RESUME_HTTP_FAILED','GAME_WEB_RESUME_NETWORK_FAILED',
        'SAVED_LOGIN_REFRESHED','LOGIN_RESTORED','LOGIN_SAVED','AUTOTEST_STAR_RAIL_POINTER_ENTRY',
        'GAME_SDK_LOGIN_H5_ADDITIONAL_ROOT_FIELDS','GAME_SDK_LOGIN_REFRESHED','GAME_SDK_LOGIN_REFRESH_FAILED','GAME_SDK_LOGIN_REFRESH_UNAVAILABLE',
        'GAME_SDK_LOGIN_REFRESH_ACCOUNT_MISMATCH','GAME_SDK_LOGIN_USING_EXISTING_GRANT')
    $result=[ordered]@{Run=$Snapshot.Run;Counters=[ordered]@{};ObservedAtSeconds=[ordered]@{}}
    foreach($name in $names) {
        foreach($source in @($Previous,$Snapshot)) {
            if(!$source -or $source.Run -ne $Snapshot.Run) {continue}
            $value=if($source -eq $Snapshot) {$source.Metrics[$name]} else {$source.Counters[$name]}
            if(($value -isnot [int] -and $value -isnot [long]) -or
               $value -lt [int]::MinValue -or $value -gt [int]::MaxValue) {continue}
            $at=if($source -eq $Snapshot) {$source.ElapsedSeconds} else {$source.ObservedAtSeconds[$name]}
            if(($at -isnot [int] -and $at -isnot [long]) -or $at -lt 0 -or $at -gt 86400) {continue}
            if($result.ObservedAtSeconds.Contains($name) -and $at -lt $result.ObservedAtSeconds[$name]) {continue}
            $result.Counters[$name]=[long]$value
            $result.ObservedAtSeconds[$name]=[long]$at
        }
    }
    return [pscustomobject]$result
}

function Get-CloudGenshinQueueCounters {
    param($Snapshot)
    $version=$null
    # Older Genshin versions used a queue SIZE here; other entries used rank.
    # Malformed or legacy versions must not claim a verified personal rank.
    if([version]::TryParse([string]$Snapshot.Version,[ref]$version) -and
       $version -ge [version]'0.2.99') {
        return [ordered]@{QueuePosition=$Snapshot.Metrics.QUEUED_POSITION;
            QueueLength=$Snapshot.Metrics.QUEUE_TOTAL_LENGTH;
            QueueBranchLength=$Snapshot.Metrics.QUEUE_BRANCH_LENGTH}
    }
    return [ordered]@{UnverifiedQueueCounter=$Snapshot.Metrics.QUEUED_POSITION}
}

function Read-CloudGenshinSafeLog {
    param([string]$Path)
    if(!(Test-Path -LiteralPath $Path)) {return $null}
    try {
        $file=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        try {
            if($file.Length -gt 131072) {return $null}
            $reader=[IO.StreamReader]::new($file)
            try {return ConvertFrom-CloudGenshinSafeLog $reader.ReadToEnd()} finally {$reader.Dispose()}
        } finally {$file.Dispose()}
    } catch {return $null} # Atomic replacement can race a read; retry next tick.
}
