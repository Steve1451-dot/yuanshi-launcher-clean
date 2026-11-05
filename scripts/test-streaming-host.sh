#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

cxx="${CXX:-g++}"
cc="${CC:-cc}"
cxxflags=(-std=c++20 -Wall -Wextra -Werror -Iapp/src -pthread)
cflags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-sign-compare -Iextern/libpeer/src)
sections=(-ffunction-sections -fdata-sections -Wl,--gc-sections)
if [[ -n "${OPENNOW_SANITIZERS:-}" ]]; then
    cxxflags+=(-g -O1 -fno-omit-frame-pointer "-fsanitize=$OPENNOW_SANITIZERS")
    cflags+=(-g -O1 -fno-omit-frame-pointer "-fsanitize=$OPENNOW_SANITIZERS")
fi

run_cpp() {
    local name="$1"
    shift
    "$cxx" "${cxxflags[@]}" "tests/${name}_test.cpp" "$@" -o "$out/$name"
    "$out/$name"
    printf 'PASS %s\n' "$name"
}

run_c() {
    local name="$1"
    shift
    "$cc" "${cflags[@]}" "tests/${name}_test.c" "$@" -o "$out/$name"
    "$out/$name"
    printf 'PASS %s\n' "$name"
}

for name in audio_latency_policy audio_playback_timeline audio_rtp_utils controller_assignment_policy \
    controller_delivery_policy controller_input_capture controller_layout decode_queue_policy decode_recovery_state \
    gpu_configuration_queue \
    keyboard_input_policy keyboard_shortcut_controls keyboard_touch_controls network_loop_policy \
    network_quality_policy sender_report_cache startup_timeout_policy stream_end_policy stream_overlay_policy \
    stream_settings_policy software_yuv_upload touch_mapping video_frame_timing \
    video_quality_policy websocket_write_queue; do
    run_cpp "$name"
done

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) net_libs=(-lws2_32);;
    *) net_libs=();;
esac
run_cpp network_monitor app/src/network_monitor.cpp app/src/network_utils.cpp "${net_libs[@]}"
run_cpp diagnostic_writer app/src/webrtc/diagnostic_writer.cpp
run_cpp mihoyo_signaling_frame
run_cpp hardware_decode_policy -isystem extern/ffmpeg/include
run_cpp stage_timings
run_cpp decode_buffer_pool
run_cpp media_pipeline_queue_model -Itests/stream_stubs -Iextern/libpeer/src -isystem extern/ffmpeg/include \
    tests/ffmpeg_reference_model.cpp app/src/mihoyo/media_pipeline.cpp app/src/stream/ffmpeg/AVFrameHolder.cpp app/src/webrtc/shared.cpp
run_cpp mihoyo_safe_log_archive
run_cpp native_presentation_clock
run_cpp native_decoder_selection
run_cpp available_slot
run_cpp presentation_diagnostics -isystem extern/ffmpeg/include -isystem extern/borealis/library/include/borealis/extern/nanovg
run_cpp video_renderer_identity -isystem extern/ffmpeg/include -isystem extern/borealis/library/include/borealis/extern/nanovg
run_cpp av_frame_queue_lease -Itests/stream_stubs tests/ffmpeg_reference_model.cpp app/src/stream/ffmpeg/AVFrameHolder.cpp -isystem extern/ffmpeg/include
run_cpp mihoyo_decoder_probe_policy
run_cpp mihoyo_stream_health
run_cpp endfield_adaptive_bitrate
run_cpp endfield_peer_poll_policy
run_cpp mihoyo_dispatch_http_error -isystem extern/ffmpeg/include
run_cpp mihoyo_sdp_answer_policy
run_cpp mihoyo_control_channel_policy
run_cpp mihoyo_stream_input_policy
run_cpp mihoyo_sdp_answer app/src/mihoyo/sdp_answer.cpp
run_cpp mihoyo_input_packet app/src/mihoyo/input_packet.cpp
run_cpp mihoyo_rtc_signal -isystem extern/borealis/library/include app/src/mihoyo/rtc_signal.cpp
crypto=()
for name in sha1 sha256 base64 platform_util constant_time; do
    "$cc" "${cflags[@]}" -ffunction-sections -fdata-sections \
        -Iextern/libpeer/third_party/mbedtls/include \
        -c "extern/libpeer/third_party/mbedtls/library/$name.c" -o "$out/$name.o"
    crypto+=("$out/$name.o")
done
run_cpp mihoyo_auth_header -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/mihoyo/auth_header.cpp "$out/sha256.o" "$out/platform_util.o"
run_cpp mihoyo_gamer_login -isystem extern/borealis/library/include \
    -isystem extern/ffmpeg/include -Iextern/libpeer/third_party/mbedtls/include \
    "${sections[@]}" app/src/mihoyo/gamer_login.cpp app/src/mihoyo/auth_header.cpp \
    "$out/sha256.o" "$out/platform_util.o"
run_cpp mihoyo_dispatch_response -isystem extern/borealis/library/include \
    app/src/mihoyo/dispatch_response.cpp app/src/mihoyo/sdk_start_game_params.cpp
run_cpp mihoyo_sdk_start_game_params app/src/mihoyo/sdk_start_game_params.cpp
run_cpp mihoyo_qr_login -isystem extern/borealis/library/include app/src/mihoyo/qr_login.cpp
run_cpp http_verification_header_probe
run_cpp mihoyo_ping_probe -isystem extern/borealis/library/include \
    app/src/mihoyo/ping_probe.cpp app/src/mihoyo/sdk_start_game_params.cpp
run_cpp mihoyo_combo_exchange -isystem extern/borealis/library/include \
    -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/mihoyo/combo_exchange.cpp app/src/mihoyo/qr_login.cpp app/src/mihoyo/auth_header.cpp \
    "$out/sha256.o" "$out/platform_util.o"
run_cpp mihoyo_game_control app/src/mihoyo/game_control.cpp
run_cpp mihoyo_dispatch_prepare -isystem extern/borealis/library/include \
    -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/mihoyo/dispatch_prepare.cpp app/src/mihoyo/queue_ticket.cpp \
    app/src/mihoyo/gamer_login.cpp app/src/mihoyo/auth_header.cpp "$out/sha256.o" "$out/platform_util.o"
run_cpp mihoyo_queue_ticket -isystem extern/borealis/library/include \
    -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/mihoyo/queue_ticket.cpp app/src/mihoyo/gamer_login.cpp \
    app/src/mihoyo/auth_header.cpp "$out/sha256.o" "$out/platform_util.o"
run_cpp mihoyo_initial_dispatch -isystem extern/borealis/library/include \
    -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/mihoyo/initial_dispatch.cpp app/src/mihoyo/gamer_login.cpp \
    app/src/mihoyo/auth_header.cpp "$out/sha256.o" "$out/platform_util.o"
run_cpp websocket_client -DFMT_HEADER_ONLY -DCURL_STATICLIB -isystem extern/ffmpeg/include \
    -isystem extern/borealis/library/include -isystem extern/borealis/library/lib/extern/fmt/include \
    -Iextern/libpeer/third_party/mbedtls/include "${sections[@]}" \
    app/src/WebSocketClient.cpp app/src/signaling_client.cpp app/src/mihoyo/signaling_client.cpp \
    app/src/mihoyo/rtc_signal.cpp \
    app/src/http_client.cpp "${crypto[@]}"
run_cpp websocket_handshake -Iextern/libpeer/third_party/mbedtls/include -Wl,--gc-sections "${crypto[@]}"
run_cpp signaling_diagnostics -ljansson
run_cpp av_frame_queue -Itests/stream_stubs app/src/stream/ffmpeg/AVFrameHolder.cpp -lavcodec -lavutil
run_cpp gpu_frame_queue -lavutil
python3 tests/run_deko_renderer_reconfiguration_test.py
printf 'PASS deko_renderer_reconfiguration\n'
python3 tests/run_deko_renderer_reconfiguration_test.py deko_renderer_color_range_test.cpp
printf 'PASS deko_renderer_color_range\n'
run_cpp audio_pipeline -Itests/stream_stubs -Iextern/libpeer/src app/src/stream/audio/AudioPipeline.cpp
for scenario in timeline ssrc output-failure; do
    "$out/audio_pipeline" "$scenario"
    printf 'PASS audio_pipeline/%s\n' "$scenario"
done
"$cxx" "${cxxflags[@]}" -Itests/stream_stubs tests/ffmpeg_video_decoder_test.cpp \
    app/src/stream/ffmpeg/FFmpegVideoDecoder.cpp app/src/stream/ffmpeg/AVFrameHolder.cpp \
    -Wl,--wrap=av_frame_alloc,--wrap=av_frame_free,--wrap=av_packet_alloc,--wrap=av_new_packet,--wrap=avcodec_send_packet,--wrap=avcodec_receive_frame \
    -lavcodec -lavutil -o "$out/ffmpeg_video_decoder"
for scenario in allocation{0..6} packet packet-data array receive receive-again send-again again eof; do
    "$out/ffmpeg_video_decoder" "$scenario"
    printf 'PASS ffmpeg_video_decoder/%s\n' "$scenario"
done
run_cpp nvst_sdp app/src/webrtc/nvst_sdp.cpp
run_cpp cloud_session_protocol "${sections[@]}" app/src/gfn/cloud_session_protocol.cpp app/src/gfn/shared.cpp -ljansson
run_cpp stream_bitrate_pipeline "${sections[@]}" app/src/stream_settings.cpp app/src/localization.cpp \
    app/src/gfn/cloud_session_protocol.cpp app/src/gfn/shared.cpp app/src/webrtc/nvst_sdp.cpp -ljansson
run_c rtp_h264_assembly extern/libpeer/src/rtp.c extern/libpeer/src/rtp_recovery.c
run_c rtp_reorder extern/libpeer/src/rtp.c extern/libpeer/src/rtp_recovery.c
run_c rtp_recovery extern/libpeer/src/rtp.c extern/libpeer/src/rtp_recovery.c
run_c rtp_recovery_alloc_fail extern/libpeer/src/rtp.c
run_c rtcp_nack extern/libpeer/src/rtcp.c
run_c rtcp_receiver_report extern/libpeer/src/rtcp.c
run_c sctp_socket_address -Iextern/libpeer/third_party/usrsctp/usrsctplib
run_c peer_rtcp_receive "${sections[@]}" -Wno-unused-but-set-variable \
    -Iextern/libpeer/third_party/mbedtls/include extern/libpeer/src/rtcp.c
run_c dtls_nonblocking_read "${sections[@]}" -Wno-unused-parameter -Wno-empty-body \
    -Iextern/libpeer/third_party/mbedtls/include extern/libpeer/src/dtls_srtp.c
run_c peer_dtls_receive "${sections[@]}" -Wno-unused-but-set-variable \
    -Iextern/libpeer/third_party/mbedtls/include
run_c peer_dtls_loop "${sections[@]}" -Wno-unused-but-set-variable \
    -Iextern/libpeer/third_party/mbedtls/include extern/libpeer/src/rtcp.c
run_c agent_socket_poll "${sections[@]}"
"$cc" "${cflags[@]}" -Iextern/libpeer/third_party/mbedtls/include \
    -c extern/libpeer/src/peer.c -o "$out/peer_runtime.o"
run_cpp peer_runtime -Iextern/libpeer/src "$out/peer_runtime.o" -lsrtp2
SCTP_TEST_SANITIZERS="${OPENNOW_SANITIZERS:-}" bash tests/run_sctp_reliability_test.sh
printf 'PASS sctp_reliability\n'
