#pragma once

// Endfield cloud streaming session (official web path, rtcType=WEBRTC):
//
//   ws-proxy signaling ("gsUrl" -> "connected to gs" -> client offer ->
//   answer) -> rewrite answer candidates to the relay (gsIp:webRtcPort) ->
//   "welink" DataChannel -> protoVersion handshake (LocalIm / canned
//   ChannelType / PackedData) -> WelinkHeartBeat + READY timers.
//
// The class follows the owning-worker pattern of the mihoyo RTC session: one
// owner thread calls start() (blocking, bounded), poll() (drives the socket,
// the libpeer loop and all timers), send_input()/send_keep_alive() and stop().

#include "endfield/vlink_proto.hpp"
#include "endfield/wl_proto.hpp"
#include "endfield/startup_write_queue.hpp"
#include "endfield/startup_auth_policy.hpp"
#include "endfield/post_frame_policy.hpp"
#include "endfield/game_bridge.hpp"
#include "stream/StageTimings.hpp"
#include "endfield/hg_login.hpp"
#include <future>

#include <peer_connection.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opennow::webrtc {
class PeerRuntime;
}
class WebSocketClient;

namespace cloudgenshin::endfield {

class VlinkSession {
public:
    struct Callbacks {
        std::function<void(const PeerVideoPacket&)> video;
        std::function<void(const PeerAudioPacket&)> audio;
        std::function<void(PeerConnectionState, const std::string&)> transport_state;
        std::function<void()> datachannel_open;
        std::function<void(const std::string& cmd, const std::string& arg)> gs_message;
        std::function<login::GameAuthorization(const std::atomic_bool*)> game_authorization;
        // A damaged AU was discarded, not submitted to the decoder. Preserve
        // previously queued complete units while gating dependent pictures.
        std::function<void()> video_discontinuity;
    };

    VlinkSession(VlinkSdkData sdk, Callbacks callbacks = {}, std::string auth_code = {});
    ~VlinkSession();
    VlinkSession(const VlinkSession&) = delete;
    VlinkSession& operator=(const VlinkSession&) = delete;

    // Blocking signaling handshake with internal deadlines (ws connect, gs
    // attach, offer/answer, candidate rewrite, setRemoteDescription).
    bool start(const std::atomic_bool* cancelled = nullptr);
    // Drives the websocket, the peer connection and all protocol timers.
    void poll();
    // exitGame (clean stop) + transport teardown.
    void stop();

    // First-attempt sends. All of them return the transport result so callers
    // only mark work done once the bytes reached the channel.
    bool send_input(const std::vector<std::uint8_t>& welink_message);
    bool send_motion(const cloudgenshin::input::MotionSample& sample,std::int64_t monotonic_ms);
    bool motion_requested() const {return game_bridge_.gravity_requested();}
    std::uint64_t motion_sent() const {return motion_.sent_count();}
    std::uint64_t motion_failures() const {return motion_.failure_count();}
    bool send_keep_alive();
    bool set_fps(int fps);
    bool set_bitrate_tier(int tier);
    bool set_bitrate_range(int minimum,int maximum);
    bool set_game_resolution(int width,int height);
    // RTCP PLI: ask the pod for an IDR. The official browser client sends this
    // when its decoder loses the reference frame; without it a pod that lost
    // one packet can keep sending undecodable P-frames forever.
    bool request_keyframe();
    bool receive_backlog_hint() const {return receive_backlog_hint_;}
    std::uint64_t receive_budget_hits() const {return receive_budget_hits_;}
    std::uint64_t signal_poll_peak_us() const {return signal_poll_timings_.maximum();}
    std::uint64_t peer_drain_peak_us() const {return peer_drain_timings_.maximum();}
    std::uint64_t session_tasks_peak_us() const {return session_tasks_timings_.maximum();}
    // Official web client sends this once after the first decoded video frame
    // (player module @2006433, R4/W). Returns true only when the payload was
    // actually written, and stays idempotent after the first accepted send.
    bool enable_app_intercept();
    // Protocol counters for the safe diagnostics log.
    std::uint32_t startup_write_attempts() const {
        return startup_attempt_sends_;
    }
    std::uint32_t startup_write_failures() const { return startup_attempt_failures_; }
    bool startup_ready() const { return proto_version_handled_; }
    std::uint32_t welink_messages_received() const { return welink_messages_received_; }
    std::uint32_t welink_messages_other_sid() const { return welink_messages_other_sid_; }
    std::uint32_t control_messages_sent() const {return control_messages_sent_;}
    std::uint32_t control_send_failures() const {return control_send_failures_;}
    std::uint16_t channel_sid() const { return channel_sid_; }
    std::optional<PeerTransportStats> transport_stats() const;
    std::optional<PeerVideoRtpStats> video_rtp_stats() const;
    // Called at most once per second by the owning network worker, not UI.
    int network_rtt_ms() const;

    bool running() const { return running_; }
    bool datachannel_open() const { return dc_open_; }
    bool game_started() const { return game_started_; }
    // Official gamepad A() keeps CG.PROTO on ARM even with a newer GS version.
    int input_version() const { return sdk_.is_arm_game_server()?1:input_version_; }
    // Official handleStartGameInfo starts the 1 s SDKHeartBeat as soon as the
    // first video frame is decoded (not when the game reports its start), so
    // the pod's game proxy knows the client is attached while the game boots.
    void note_video_frame();
    void note_gamepad(bool connected);
    bool kicked_offline() const { return kicked_offline_; }
    const std::string& startup_stage() const { return startup_stage_; }
    const std::string& last_error() const { return error_; }

private:
    enum class SignalPhase {
        kWaitingConnected,
        kConnected,
        kWaitingAnswer,
        kAnswered,
        kFailed,
    };

    static void OnVideo(const PeerVideoPacket* packet, void* userdata);
    static void OnVideoDamage(const PeerVideoDamage* damage, void* userdata);
    static void OnAudio(const PeerAudioPacket* packet, void* userdata);
    static void OnData(char* data, std::size_t len, void* userdata, std::uint16_t sid);
    static void OnControlOpened(void* userdata);
    static void OnControlClosed(void* userdata);
    static void OnRemoteChannel(void* userdata, std::uint16_t sid, const char* label, std::uint32_t ppid);
    static void OnPeerState(PeerConnectionState state, void* userdata);

    bool SignalingHandshake(const std::string& offer_sdp, const std::atomic_bool* cancelled);
    bool WaitFor(int timeout_ms, const std::atomic_bool* cancelled,
                 const std::function<bool()>& done);
    void HandleSignalingText(const std::string& text);
    void HandleWelinkMessage(const std::uint8_t* data, std::size_t size, std::uint16_t sid);
    void HandleProtoVersion();
    void MaybeCreateDataChannel();
    bool SendCanned(const std::uint8_t* bytes, std::size_t size);
    bool SendText(const std::string& text);
    std::string MakeSdpInfoJson() const;

    VlinkSdkData sdk_;
    Callbacks callbacks_;

    std::unique_ptr<opennow::webrtc::PeerRuntime> runtime_;
    std::unique_ptr<WebSocketClient> ws_;
    PeerConnection* peer_ = nullptr;

    SignalPhase signal_phase_ = SignalPhase::kWaitingConnected;
    std::string signal_answer_sdp_;
    std::string error_;

    bool running_ = false;
    bool closing_ = false;
    bool answer_applied_ = false;
    bool signaling_close_ignored_ = false;
    bool channel_created_ = false;
    bool dc_open_ = false;
    bool game_started_ = false;
    bool sdk_beat_started_ = false;
    bool bridge_pipeline_sent_ = false;
    bool proto_version_handled_ = false;
    bool proto_version_received_ = false;
    StartupWriteQueue startup_writes_;
    StartupAuthPolicy startup_auth_;
    GameBridge game_bridge_;
    cloudgenshin::input::MotionSendPolicy motion_;
    unsigned bridge_expired_=0, bridge_confirmed_=0;
    const std::atomic_bool* cancelled_=nullptr;
    std::future<login::GameAuthorization> game_authorization_;
    std::optional<login::GameAuthorization> ready_game_authorization_;
    // Only diagnostics: how many startup packets were attempted/failed. The
    // queue itself retries until its own deadline.
    std::uint32_t startup_attempt_sends_ = 0;
    std::uint32_t startup_attempt_failures_ = 0;
    std::uint32_t welink_messages_received_ = 0;
    std::uint32_t welink_messages_other_sid_ = 0;
    std::uint32_t control_messages_sent_ = 0, control_send_failures_ = 0;
    bool app_intercept_sent_ = false;
    int bitrate_minimum_ = 0; // Last fully accepted range, not remote ACK.
    int input_version_ = 1;
    bool game_resolution_sent_ = false;
    int game_resolution_attempts_ = 0;
    std::chrono::steady_clock::time_point game_resolution_retry_next_{};
    PostFramePolicy post_frame_;
    bool receive_backlog_hint_=false;
    std::uint64_t receive_budget_hits_=0;
    opennow::video::StageTimings signal_poll_timings_,peer_drain_timings_,session_tasks_timings_;
    bool kicked_offline_ = false;
    std::uint16_t channel_sid_ = 0;
    std::string startup_stage_;

    std::chrono::steady_clock::time_point ready_next_{};
    std::chrono::steady_clock::time_point welink_beat_next_{};
    std::chrono::steady_clock::time_point sdk_beat_next_{};
    std::chrono::steady_clock::time_point keepalive_next_{};
    std::chrono::steady_clock::time_point last_input_at_{};
};

}  // namespace cloudgenshin::endfield
