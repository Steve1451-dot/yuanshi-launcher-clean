#pragma once

#include "mihoyo/input_packet.hpp"
#include "mihoyo/sdk_bridge.hpp"
#include "mihoyo/signaling_client.hpp"
#include "mihoyo/game_control.hpp"
#include "mihoyo/rtc_route.hpp"
#include "mihoyo/control_channel_policy.hpp"
#include "mihoyo/web_control_channel.hpp"
#include "webrtc/peer_runtime.hpp"
#include "stream/StageTimings.hpp"

#include <peer_connection.h>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cloudgenshin::rtc {

struct IceServerConfig {
    std::string url;
    std::string username;
    std::string credential;
};

// The owning network worker calls start(), poll(), send_gamepad(), and stop().
// Packet callback data is borrowed from libpeer and must be consumed before
// the callback returns. This class does not perform login or dispatch.
class Session {
public:
    using Clock=std::function<std::chrono::steady_clock::time_point()>;
    struct Callbacks {
        std::function<void(const PeerVideoPacket&)> video;
        // Fired after the RTP repair window failed and one access unit was
        // destroyed. The owner must gate dependent pictures and request IDR.
        std::function<void(const PeerVideoDamage&)> video_damage;
        std::function<void(const PeerAudioPacket&)> audio;
        std::function<void(const std::uint8_t*, std::size_t)> data;
        std::function<void(PeerConnectionState)> state;
        std::function<void(const control::StartResult&)> authenticated;
        std::function<void(int)> auth_rejected;
        SignalingClient::ConnectAttemptCallback connect_attempt;
        std::function<void(const char*,int)> candidate_event;
    };

    Session(std::string wss_url, control::StartGame authorization,
            std::vector<IceServerConfig> ice_servers,
            Callbacks callbacks = {},std::vector<std::string> signaling_alternatives={},
            std::vector<DispatchRoute> routes={},Clock clock=std::chrono::steady_clock::now,
            std::vector<WebControlRoute> web_control_routes={},
            sdk::BridgeContext sdk_bridge={});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool start(const std::atomic_bool* cancelled = nullptr);
    void poll();
    void stop();
    bool send_gamepad(const input::SwitchControllerState& state, std::int64_t monotonic_ms);
    bool send_motion(const input::MotionSample& sample,std::int64_t monotonic_ms);
    bool motion_requested() const {return motion_.enabled();}
    std::uint64_t motion_sent() const {return motion_.sent_count();}
    std::uint64_t motion_failures() const {return motion_.failure_count();}
    std::uint64_t orientation_sent() const {return orientation_sent_;}
    std::uint64_t motion_feedback_received() const {return motion_feedback_received_;}
    std::uint64_t motion_feedback_invalid() const {return motion_feedback_invalid_;}
    std::uint64_t motion_feedback_before_auth() const {return motion_feedback_before_auth_;}
    std::uint64_t motion_activation_received() const {return motion_activation_received_;}
    // Official TouchController stream (DataType 21, field 13): multi-finger
    // Down/Move/Up with pointX/pointY normalised by the video rectangle.
    bool send_touch(const std::vector<input::TouchEvent>& events);
    // Keyboard/mouse emulation and gamepad-device announcement use the same
    // official control transport (command 20010) as the gamepad frames.
    bool send_keyboard(std::uint32_t key_code, bool down);
    bool send_mouse_move(double delta_x, double delta_y);
    bool send_mouse_move(double delta_x, double delta_y, double cursor_x, double cursor_y);
    bool send_mouse_button(std::uint32_t button, bool down, double cursor_x, double cursor_y);
    // proto.msg.SdkDeviceInfo over the signaling proxy. The official web SDK
    // sends it when the RTC data channel reports connected, before any input.
    bool send_device_info(std::uint32_t screen_width, std::uint32_t screen_height);
    bool send_gamepad_list(std::uint32_t vendor_id, std::uint32_t product_id);
    // proto.msg control requests over the signaling proxy channel, matching the
    // official floating-ball quality/fps switches.
    bool set_graphics_mode(std::int32_t mode);
    // Application kbps, matching StreamBitrateRange (wire units are x1024).
    bool set_bitrate_range(int minimum,int maximum);
    bool set_fps(std::uint32_t fps);
    // Clear held input state on the pod (RtcDataChannel type 31). The official
    // app uses TOUCH_SCREEN when its UI takes over; REGULAR gamepad/touch frames
    // never carry input_control_type.
    bool reset_game_inputs(std::uint32_t input_types);
    bool gamepad_enabled() const { return xbox_enabled_ || ps_enabled_; }
    // Virtual-pad identity announced in RtcGamepadList and every type-17
    // frame. The protocol carries vendor/product, not a display-name string;
    // the pod derives the driver name from these IDs.
    void set_pad_identity(std::uint32_t vendor_id, std::uint32_t product_id) {
        if (vendor_id && product_id) {
            pad_vendor_id_ = vendor_id;
            pad_product_id_ = product_id;
        }
    }
    std::uint32_t pad_vendor_id() const { return pad_vendor_id_; }
    std::uint32_t pad_product_id() const { return pad_product_id_; }
    // Last control mode the pod reported through 20037; -1 until it reports.
    std::int32_t server_control_mode() const { return server_control_mode_; }
    std::int32_t web_control_channel_type() const { return web_control_channel_type_; }

    bool has_answered() const { return answer_sent_; }
    bool is_authenticated() const { return authenticated_; }
    bool transport_stats(PeerTransportStats* stats) {
        return peer_ && stats && peer_connection_get_transport_stats(peer_,stats)==0;
    }
    // Numeric receiver counters only; null before the peer exists.
    bool video_rtp_stats(PeerVideoRtpStats* stats) {
        return peer_ && stats && peer_connection_get_video_rtp_stats(peer_, stats) == 0;
    }
    bool is_running() const { return running_; }
    // Network-owner only. Existing transport estimate; no additional probes.
    int network_rtt_ms() const {
        return peer_ && running_ && peer_connection_get_state(peer_)==PEER_CONNECTION_COMPLETED
            ? peer_connection_get_rtt_ms(peer_) : -1;
    }
    // True when the pod sent StopGameRsp (20005) instead of a transport error.
    bool session_end_received() const { return session_end_received_; }
    bool request_keyframe();
    // Control-ready transport: the independent control channel while valid,
    // otherwise the RTC data channel (official sendRtcData fallback).
    bool has_remote_data_channel() const { return web_control_.ready() ||
        ((data_sid_ || extra_sid_) && control_.ready()); }
    bool input_ready() const {return can_send_input();}
    bool receive_backlog_hint() const {return receive_backlog_hint_;}
    std::uint64_t receive_budget_hits() const {return receive_budget_hits_;}
    unsigned receive_batch_maximum() const {return receive_batch_maximum_;}
    std::uint64_t signal_poll_peak_us() const {return signal_poll_timings_.maximum();}
    std::uint64_t peer_drain_peak_us() const {return peer_drain_timings_.maximum();}
    std::uint64_t session_tasks_peak_us() const {return session_tasks_timings_.maximum();}
    std::string last_error() const;
    // Fixed local diagnostic tag only: safe to persist, unlike raw transport
    // errors, endpoint-bearing server text, SDP or packet bodies.
    const char* last_error_event() const;

private:
    static void OnVideo(const PeerVideoPacket* packet, void* userdata);
    static void OnVideoDamage(const PeerVideoDamage* damage, void* userdata);
    static void OnAudio(const PeerAudioPacket* packet, void* userdata);
    static void OnData(char* bytes, std::size_t size, void* userdata, std::uint16_t sid);
    static void OnRemoteChannel(void* userdata, std::uint16_t sid, const char* label,
                                std::uint32_t ppid);
    static void OnPeerState(PeerConnectionState state, void* userdata);

    void handle_signal(const SignalMessage& message);
    void handle_frame(const signaling::Frame& frame);
    bool initialize_peer();
    void report_transport();
    bool add_remote_candidate(const std::string& candidate);
    bool send_channel_message(const std::vector<std::uint8_t>& channel_message, bool user_activity=true);
    // Sends one game-SDK bridge packet over the proxy channel, like the
    // official client's sendTcpMessage (signalingChannel.sendProxy).
    bool send_sdk_proxy(std::uint16_t command, const std::vector<std::uint8_t>& body);
    // Feeds data-channel bytes to the SDK bridge; true when consumed.
    bool consume_sdk_packet(const std::uint8_t* bytes, std::size_t size);
    bool consume_motion_feedback(const control::Packet& packet);
    // Autotest-only decoder for incoming data-channel packets: command kind,
    // and for heartbeats the echoed id/timestamp pair (never raw payloads).
    void trace_incoming_data(const std::uint8_t* bytes, std::size_t size);
    bool send_control_packet(std::vector<std::uint8_t>& packet);
    bool send_packet_on_sid(std::vector<std::uint8_t>& packet, std::uint16_t sid);
    void poll_web_control(std::chrono::steady_clock::time_point now);
    // The official client treats the independent GameControlChannel as
    // optional: input and heartbeats ride the RTC data channel whenever the
    // GCC is not valid. A failed control handshake therefore degrades control
    // instead of ending the session.
    void web_control_fail(const char* reason);
    bool control_via_wss() const { return UsesWebControl(web_control_channel_type_) && web_control_.ready(); }
    bool can_send_input() const;

    SignalingClient signaling_;
    std::vector<IceServerConfig> ice_servers_;
    Callbacks callbacks_;
    control::StartGame authorization_;
    std::unique_ptr<opennow::webrtc::PeerRuntime> runtime_;
    PeerConnection* peer_ = nullptr;
    std::optional<std::uint16_t> data_sid_;
    // The CA also opens a first channel with legacy DCEP PPID 0 on some pods.
    // It is kept apart from data_sid_ (the official sendRtcData channel) and
    // only used when cloudgenshin-input-channel.txt selects the diagnostic
    // "both"/"legacy" comparison.
    std::optional<std::uint16_t> extra_sid_;
    int input_channel_mode_ = 0;  // 0 primary, 1 both, 2 legacy
    std::vector<std::string> pending_candidates_;
    std::vector<std::string> seen_candidates_;
    std::vector<std::uint16_t> seen_proxy_commands_;
    std::vector<DispatchRoute> routes_;
    std::optional<DispatchRoute> selected_route_;
    bool answer_sent_ = false;
    // First negotiated answer and the remote transport identity captured from
    // the first offer. A later same-ufrag/fingerprint offer is a media update
    // that reuses this answer instead of rotating ICE/DTLS credentials.
    std::string local_answer_sdp_;
    std::string remote_ice_ufrag_;
    std::string remote_fingerprint_;
    // At most 32 timestamps, expiring after one minute. A lifetime cap would
    // eventually terminate a healthy long-running stream.
    std::vector<std::chrono::steady_clock::time_point> renegotiations_;
    bool running_ = false, authenticated_ = false, auth_sent_ = false;
    bool sctp_up_reported_ = false;
    bool receive_backlog_hint_=false;
    std::uint64_t receive_budget_hits_=0;
    unsigned receive_batch_maximum_=0;
    opennow::video::StageTimings signal_poll_timings_,peer_drain_timings_,session_tasks_timings_;
    ControlChannelPolicy control_;
    Clock clock_;
    WebControlChannel web_control_;
    std::vector<WebControlRoute> web_control_routes_;
    // In-game SDK message bridge (cmd 1900 reliable queue + game-directed
    // keep-alive); rebuilt per start() and torn down by stop().
    sdk::BridgeContext sdk_context_;
    std::optional<sdk::Bridge> sdk_bridge_;
    bool had_gamepad_activity_ = false;
    input::MotionSendPolicy motion_;
    std::uint64_t orientation_sent_=0;
    std::uint64_t motion_feedback_received_=0,motion_feedback_invalid_=0,motion_feedback_before_auth_=0;
    std::uint64_t motion_activation_received_=0; // Authenticated true feedback, even if later switched off.
    // Autotest-only protocol trace file (identifiers and decisions, never
    // parameter values); bounded and flushed per line so a crash keeps it.
    std::ofstream sdk_trace_log_;
    std::size_t sdk_trace_bytes_ = 0;
    std::chrono::steady_clock::time_point sdk_trace_start_{};
    std::vector<std::string> selected_control_urls_;
    std::optional<std::uint32_t> control_session_;
    bool control_handshake_received_=false,web_control_started_=false,web_control_reported_=false,web_control_failed_=false;
    bool reset_inputs_sent_=false;
    const std::atomic_bool* cancelled_=nullptr;
    std::chrono::steady_clock::time_point control_handshake_deadline_{};
    bool xbox_enabled_ = false, ps_enabled_ = false, gr0006_enabled_ = false;
    std::uint32_t pad_vendor_id_ = input::kXboxVendorId;
    std::uint32_t pad_product_id_ = input::kXboxProductId;
    // Last enumerate.GameControlMode reported by the pod (20037). The official
    // client forwards it to its UI; here it becomes SERVER_GAME_CONTROL_MODE.
    std::int32_t server_control_mode_ = -1;
    std::int32_t web_control_channel_type_ = 0;
    bool session_end_received_ = false;
    std::chrono::steady_clock::time_point deadline_{};
    std::chrono::steady_clock::time_point next_heartbeat_{};
    std::uint64_t heartbeat_id_ = 0;
    std::int64_t last_heartbeat_unix_ms_ = 0;
    int hb_trace_out_ = 0, hb_trace_in_ = 0, in_trace_count_ = 0;
    std::uint64_t hb_in_count_ = 0, hb_in_match_ = 0, hb_in_last_id_ = 0;
    std::int64_t hb_summary_next_ms_ = 0;
    int out_input_trace_ = 0;
    bool device_info_traced_ = false;
    std::string error_;
};

} // namespace cloudgenshin::rtc
