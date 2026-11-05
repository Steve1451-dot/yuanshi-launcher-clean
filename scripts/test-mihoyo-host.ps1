param([string]$CompilerRoot = 'C:/Users/y/scoop/apps/mingw/current/bin', [string[]]$TestNames)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path (Split-Path (Split-Path $repo -Parent) -Parent) '.opennow-msys-tmp'
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$env:TMPDIR=$testOutput; $env:TEMP=$testOutput; $env:TMP=$testOutput
Push-Location $repo
try {
    $cryptoInclude='extern/libpeer/third_party/mbedtls/include'
    foreach ($source in @('sha256','platform_util','sha1','base64','aes','aesni')) {
        & "$CompilerRoot/gcc.exe" -std=c11 -pipe "-I$cryptoInclude" -c "extern/libpeer/third_party/mbedtls/library/$source.c" -o "$testOutput/mihoyo_$source.o"
        if ($LASTEXITCODE -ne 0) {throw "Crypto test dependency compile failed: $source"}
    }
    $auth=@('app/src/mihoyo/auth_header.cpp','app/src/mihoyo/gamer_login.cpp',"$testOutput/mihoyo_sha256.o","$testOutput/mihoyo_platform_util.o")
    $tests=@{
        app_storage_policy=@('app/src/app_paths.cpp')
        home_background=@('-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        decode_queue_policy=@()
        decode_buffer_pool=@()
        media_pipeline_queue_model=@('-Itests/stream_stubs','-Iextern/libpeer/src','-isystem','extern/ffmpeg/include',
            'tests/ffmpeg_reference_model.cpp','app/src/mihoyo/media_pipeline.cpp','app/src/stream/ffmpeg/AVFrameHolder.cpp','app/src/webrtc/shared.cpp')
        decode_recovery_state=@()
        keyframe_request_policy=@()
        frame_recovery_timings=@()
        hardware_decode_policy=@('-isystem','extern/ffmpeg/include')
        mihoyo_decoder_probe_policy=@()
        mihoyo_stream_health=@()
        stage_timings=@()
        native_presentation_clock=@()
        native_decoder_selection=@()
        available_slot=@()
        presentation_diagnostics=@('-isystem','extern/ffmpeg/include','-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        video_frame_timing=@()
        video_renderer_identity=@('-isystem','extern/ffmpeg/include','-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        av_frame_queue_model=@('-Itests/stream_stubs','-isystem','extern/ffmpeg/include','tests/ffmpeg_reference_model.cpp','app/src/stream/ffmpeg/AVFrameHolder.cpp')
        av_frame_queue_lease=@('-Itests/stream_stubs','-isystem','extern/ffmpeg/include','tests/ffmpeg_reference_model.cpp','app/src/stream/ffmpeg/AVFrameHolder.cpp')
        gpu_frame_queue_model=@('-isystem','extern/ffmpeg/include','tests/ffmpeg_reference_model.cpp')
        audio_latency_policy=@()
        audio_playback_timeline=@()
        audio_rtp_utils=@()
        audio_pipeline=@('-Itests/stream_stubs','-Iextern/libpeer/src','app/src/stream/audio/AudioPipeline.cpp')
        http_verification_header_probe=@()
        http_transfer_diagnostics=@()
        http_cookie_session=@('-DCURL_STATICLIB','-isystem','extern/ffmpeg/include','app/src/http_client.cpp')
        mihoyo_qr_login=@('app/src/mihoyo/qr_login.cpp')
        mihoyo_passport_login=$auth+@('app/src/mihoyo/passport_qr_login.cpp',
            'app/src/mihoyo/passport_login_flow.cpp','app/src/mihoyo/combo_exchange.cpp','app/src/mihoyo/qr_login.cpp')
        mihoyo_passport_signin_policy=@()
        mihoyo_passport_token_resume=$auth+@('app/src/mihoyo/passport_token_resume.cpp','app/src/mihoyo/passport_qr_login.cpp',
            'app/src/mihoyo/combo_exchange.cpp','app/src/mihoyo/qr_login.cpp','-isystem','extern/ffmpeg/include',
            "$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")
        mihoyo_combo_exchange=@('app/src/mihoyo/combo_exchange.cpp','app/src/mihoyo/qr_login.cpp',
            'app/src/mihoyo/auth_header.cpp',"$testOutput/mihoyo_sha256.o","$testOutput/mihoyo_platform_util.o")
        mihoyo_game_control=@('app/src/mihoyo/game_control.cpp')
        mihoyo_auth_header=@('app/src/mihoyo/auth_header.cpp',"$testOutput/mihoyo_sha256.o","$testOutput/mihoyo_platform_util.o")
        mihoyo_gamer_login=$auth
        mihoyo_gamer_login_http=$auth+@('app/src/mihoyo/gamer_login_http.cpp','-isystem','extern/ffmpeg/include')
        mihoyo_game_profile=@()
        mihoyo_initial_dispatch=$auth+@('app/src/mihoyo/initial_dispatch.cpp')
        mihoyo_queue_ticket=$auth+@('app/src/mihoyo/queue_ticket.cpp')
        mihoyo_dispatch_prepare=$auth+@('app/src/mihoyo/queue_ticket.cpp','app/src/mihoyo/dispatch_prepare.cpp')
        mihoyo_dispatch_prepare_http=$auth+@('app/src/mihoyo/queue_ticket.cpp','app/src/mihoyo/dispatch_prepare.cpp',
            'app/src/mihoyo/dispatch_prepare_http.cpp','-isystem','extern/ffmpeg/include')
        mihoyo_dispatch_response=@('app/src/mihoyo/dispatch_response.cpp','app/src/mihoyo/sdk_start_game_params.cpp')
        mihoyo_dispatch_http_error=@('-isystem','extern/ffmpeg/include')
        mihoyo_sdk_start_game_params=@('app/src/mihoyo/sdk_start_game_params.cpp')
        mihoyo_ping_probe=@('app/src/mihoyo/ping_probe.cpp','app/src/mihoyo/sdk_start_game_params.cpp')
        mihoyo_input_packet=@('app/src/mihoyo/input_packet.cpp')
        mihoyo_motion_input=@('app/src/mihoyo/input_packet.cpp','app/src/mihoyo/game_control.cpp')
        mihoyo_motion_orientation=@()
        mihoyo_touch_forward=@('app/src/mihoyo/input_packet.cpp')
        mihoyo_rtc_signal=@('app/src/mihoyo/rtc_signal.cpp')
        mihoyo_signaling_frame=@()
        mihoyo_stream_diagnostics_policy=@()
        mihoyo_stream_startup_policy=@()
        mihoyo_media_startup_policy=@()
        media_ingress_diagnostics=@('-Iextern/libpeer/src')
        mihoyo_stream_settings_store=@()
        mihoyo_control_channel_policy=@()
        mihoyo_stream_input_policy=@()
        mihoyo_launcher_input_policy=@()
        mihoyo_gameplay_button_policy=@('app/src/mihoyo/input_packet.cpp')
        controller_mapping=@('app/src/mihoyo/input_packet.cpp')
        controller_system_buttons=@('app/src/mihoyo/input_packet.cpp')
        controller_mapping_editor=@()
        controller_mapping_draw=@('-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        controller_mapping_diagram=@()
        genshin_input_experiment=@('app/src/mihoyo/input_packet.cpp','app/src/mihoyo/game_control.cpp')
        gyro_stick_assist=@()
        mihoyo_launcher_status=@()
        mihoyo_stream_overlay_policy=@()
        network_latency=@()
        network_latency_draw=@('-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        compact_stream_menu=@()
        compact_stream_menu_draw=@('-isystem','extern/borealis/library/include/borealis/extern/nanovg')
        endfield_account_policy=@()
        endfield_launcher_status=@()
        endfield_stream_settings=@('app/src/endfield/wl_proto.cpp')
        endfield_xinput_state=@('app/src/endfield/wl_input.cpp','app/src/endfield/wl_proto.cpp','app/src/mihoyo/input_packet.cpp')
        endfield_wl_input=@('app/src/endfield/wl_input.cpp','app/src/endfield/wl_proto.cpp')
        endfield_startup_write_queue=@()
        endfield_startup_auth_policy=@()
        endfield_exit_drain=@()
        endfield_post_frame_policy=@()
        endfield_entry_probe_policy=@()
        endfield_wl_proto=@('app/src/endfield/wl_proto.cpp')
        endfield_vlink_proto=@('app/src/endfield/vlink_proto.cpp')
        endfield_game_bridge=@('app/src/endfield/game_bridge.cpp','app/src/endfield/wl_proto.cpp')
        endfield_saved_game_authorization=@('app/src/endfield/saved_game_authorization.cpp')
        endfield_peer_poll_policy=@()
        endfield_adaptive_bitrate=@()
        endfield_audio_offer=@()
        endfield_video_discontinuity=@()
        endfield_diagnostic_event_policy=@()
        endfield_store=@('app/src/endfield/endfield_store.cpp',
            "$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")
        endfield_account_http=@('app/src/endfield/hg_api_http.cpp','app/src/endfield/hg_api.cpp',
            'app/src/endfield/account_identity.cpp','app/src/endfield/endfield_sign.cpp','app/src/endfield/jwe.cpp',
            'app/src/endfield/hg_login.cpp','app/src/endfield/hg_login_http.cpp',
            'app/src/mihoyo/auth_header.cpp',
            '-isystem','extern/ffmpeg/include',"$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")
        mihoyo_safe_event_log=@()
        mihoyo_safe_log_writer=@()
        mihoyo_safe_log_archive=@()
        mihoyo_launch_flow=$auth+@('app/src/mihoyo/launch_flow.cpp','app/src/mihoyo/dispatch_prepare.cpp',
            'app/src/mihoyo/combo_exchange.cpp','app/src/mihoyo/qr_login.cpp',
            'app/src/mihoyo/ping_probe.cpp','app/src/mihoyo/sdk_start_game_params.cpp',
            'app/src/mihoyo/initial_dispatch.cpp','app/src/mihoyo/queue_ticket.cpp',"$testOutput/mihoyo_base64.o",'-DCLOUDGENSHIN_TEST_BASE64_SHIM')
        websocket_client=@('-DFMT_HEADER_ONLY','-DCURL_STATICLIB','-DCLOUDGENSHIN_TEST_HTTP_TLS_SHIM',
            '-isystem','extern/borealis/library/lib/extern/fmt/include','-isystem','extern/ffmpeg/include',
            'app/src/WebSocketClient.cpp','app/src/signaling_client.cpp','app/src/mihoyo/signaling_client.cpp',
            'app/src/mihoyo/rtc_signal.cpp',"$testOutput/mihoyo_sha1.o","$testOutput/mihoyo_base64.o","$testOutput/mihoyo_platform_util.o")
        websocket_connect_policy=@()
        mihoyo_signaling_endpoint=@()
        mihoyo_ice_candidate_policy=@()
        mihoyo_rtc_route=@()
        mihoyo_sdp_answer_policy=@()
        mihoyo_sdp_answer=@('app/src/mihoyo/sdp_answer.cpp')
        mihoyo_ui_language=@()
        mihoyo_autotest_policy=@()
        mihoyo_autotest_pad_policy=@()
        mihoyo_controller_source_policy=@()
        mihoyo_login_store=@('app/src/mihoyo/login_store.cpp',
            "$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")
        mihoyo_sdk_bridge=@('app/src/mihoyo/sdk_bridge.cpp',
            "$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")
        mihoyo_sdk_report_diagnostics=@()
        mihoyo_sdk_device_fingerprint=@('-isystem','extern/ffmpeg/include','app/src/mihoyo/sdk_device_fingerprint.cpp')
        mihoyo_device_info_consent=@()
        mihoyo_sdk_json=@()
    }
    $tests['mihoyo_rtc_session']=$tests['websocket_client']+@('-Iextern/libpeer/src',
        'app/src/mihoyo/rtc_session.cpp','app/src/mihoyo/sdk_bridge.cpp','app/src/mihoyo/web_control_channel.cpp','app/src/mihoyo/sdp_answer.cpp','app/src/mihoyo/game_control.cpp','app/src/mihoyo/input_packet.cpp',"$testOutput/mihoyo_aes.o","$testOutput/mihoyo_aesni.o")
    $tests['mihoyo_web_control_channel']=$tests['websocket_client']+@('app/src/mihoyo/web_control_channel.cpp','app/src/mihoyo/game_control.cpp')
    $tests['mihoyo_ping_network']=$tests['websocket_client']+@('app/src/mihoyo/ping_probe.cpp',
        'app/src/mihoyo/ping_probe_network.cpp','app/src/mihoyo/sdk_start_game_params.cpp')
    $tests['endfield_cloud_http']=$tests['endfield_account_http']
    $selectedTests=if ($TestNames) {$TestNames | Sort-Object -Unique} else {$tests.Keys | Sort-Object}
    foreach ($name in $selectedTests) {
        if (!$tests.ContainsKey($name)) {throw "Unknown test: $name"}
        if ($name -in @('mihoyo_login_store','mihoyo_sdk_bridge','endfield_store') -and !(Test-Path "$testOutput/dtls-host-mingw/mbedtls/library/libmbedcrypto.a")) {
            throw 'Build the real host Mbed TLS crypto archive with tests/dtls_host first (see docs/cloudgenshin-integration-status.md).'
        }
        $arguments=@('-std=c++20','-Wall','-Wextra','-Werror','-pipe','-Iapp/src','-isystem','extern/borealis/library/include',"-I$cryptoInclude","tests/$name`_test.cpp")+$tests[$name]+@('-o',"$testOutput/$name.exe")
        & "$CompilerRoot/g++.exe" @arguments
        if ($LASTEXITCODE -ne 0) {throw "Compile failed: $name"}
        & "$testOutput/$name.exe"
        if ($LASTEXITCODE -ne 0) {throw "Test failed: $name"}
        Write-Output "PASS $name"
    }
} finally {Pop-Location}
