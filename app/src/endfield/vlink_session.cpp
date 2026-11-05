#include "endfield/vlink_session.hpp"

#include "WebSocketClient.hpp"

#include "endfield/endfield_profile.hpp"
#include "endfield/account_policy.hpp"
#include "endfield/wl_input.hpp"
#include "endfield/peer_poll_policy.hpp"
#include "endfield/exit_drain.hpp"
#include "endfield/stream_settings.hpp"
#include "endfield/audio_offer_policy.hpp"
#include "webrtc/peer_runtime.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cloudgenshin::endfield {
namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

constexpr int kSignalConnectTimeoutMs = 20000;
constexpr int kConnectedToGsTimeoutMs = 15000;
constexpr int kAnswerTimeoutMs = 30000;

constexpr auto kReadyInterval = std::chrono::seconds(1);
constexpr auto kWelinkHeartBeatInterval = std::chrono::seconds(2);
constexpr auto kSdkHeartBeatInterval = std::chrono::seconds(1);
constexpr auto kKeepAliveIdle = std::chrono::seconds(10);
constexpr int kResolutionAttemptLimit = 20;
constexpr auto kResolutionRetryInterval = std::chrono::milliseconds(500);

std::string JsonText(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned())
        return std::to_string(value.get<unsigned long long>());
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (number == static_cast<double>(static_cast<long long>(number)))
            return std::to_string(static_cast<long long>(number));
        return std::to_string(number);
    }
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    return {};
}

std::optional<long long> JsonInt(const Json& value) {
    if (value.is_number_integer()) return value.get<long long>();
    if (value.is_number_unsigned())
        return static_cast<long long>(value.get<unsigned long long>());
    if (value.is_number_float()) return static_cast<long long>(value.get<double>());
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace

VlinkSession::VlinkSession(VlinkSdkData sdk, Callbacks callbacks, std::string auth_code)
    : sdk_(std::move(sdk)), callbacks_(std::move(callbacks)),
      game_bridge_(std::move(auth_code),account::SessionDeviceId(),GameBridge::Profile::WebRtc) {}

VlinkSession::~VlinkSession() { stop(); }

bool VlinkSession::start(const std::atomic_bool* cancelled) {
    if (running_) return true;
    closing_=false;
    error_.clear();
    cancelled_=cancelled;
    if (!sdk_.ready()) {
        error_ = "Missing sdk_msg connection fields";
        return false;
    }

    try {
        runtime_ = std::make_unique<opennow::webrtc::PeerRuntime>();
    } catch (const std::exception&) {
        error_ = "Could not initialize streaming security runtime";
        return false;
    }
    // TLS/DTLS verification stays on; this path needs no extra runtime
    // switches.

    PeerConfiguration config{};
    config.video_codec = CODEC_H264;
    config.video_reorder_hold_ms=kVideoRecoveryHoldMs;
    config.video_reorder_initial_packets=kVideoRecoveryInitialPackets;
    config.video_reorder_max_packets=kVideoRecoveryMaximumPackets;
    config.audio_codec = CODEC_OPUS;
    // DATA_CHANNEL_STRING only selects the PPID of
    // peer_connection_datachannel_send_sid (used for the literal "READY"
    // texts); protobuf frames go through the explicit binary API. The SDP
    // m=application section is emitted for any non-NONE value.
    config.datachannel = DATA_CHANNEL_STRING;
    config.onvideopacket = &VlinkSession::OnVideo;
    config.onvideodamage = &VlinkSession::OnVideoDamage;
    config.onaudiopacket = &VlinkSession::OnAudio;
    config.user_data = this;
    peer_ = peer_connection_create(&config);
    if (!peer_) {
        error_ = "Could not initialize WebRTC peer";
        runtime_.reset();
        return false;
    }
    // Endfield's pod waits for the client's DTLS flight after ICE succeeds.
    // Configure the actual transport role together with the offer; changing
    // only SDP text would leave both transports waiting for ClientHello.
    if(peer_connection_set_offer_dtls_client(peer_,1)!=0) {
        error_="Could not configure cloud transport role";
        peer_connection_destroy(peer_);peer_=nullptr;runtime_.reset();return false;
    }
    peer_connection_ondatachannel(peer_, &VlinkSession::OnData,
                                  &VlinkSession::OnControlOpened,&VlinkSession::OnControlClosed);
    peer_connection_on_remote_datachannel(peer_, &VlinkSession::OnRemoteChannel);
    peer_connection_oniceconnectionstatechange(peer_, &VlinkSession::OnPeerState);

    const char* offer = peer_connection_create_offer(peer_);
    if (!offer || !*offer) {
        error_ = "Could not create the WebRTC offer";
        peer_connection_destroy(peer_);
        peer_ = nullptr;
        runtime_.reset();
        return false;
    }

    if (!SignalingHandshake(LowBandwidthAudioOffer(offer), cancelled)) {
        ws_.reset();
        peer_connection_destroy(peer_);
        peer_ = nullptr;
        runtime_.reset();
        return false;
    }

    running_ = true;
    return true;
}

bool VlinkSession::SignalingHandshake(const std::string& offer_sdp,
                                      const std::atomic_bool* cancelled) {
    const std::string url = std::string(kSignalDomain) + std::string(kSignalProxyPath);
    ws_ = std::make_unique<WebSocketClient>(url);
    ws_->set_on_message([this](const std::string& text) { HandleSignalingText(text); });
    if (!ws_->connect(cancelled, kSignalConnectTimeoutMs)) {
        error_ = "Could not connect cloud signaling";
        return false;
    }

    // 1. Attach the proxy to the game server websocket (the web client sends
    //    this right after the ws opens). ARM proxy pods append /webrtc-proxy
    //    (generateSocketUrl @2014270).
    signal_phase_ = SignalPhase::kWaitingConnected;
    {
        Json attach;
        std::string gs_url = "ws://" + sdk_.gs_ip + ":" + std::to_string(sdk_.web_rtc_port);
        if (sdk_.is_proxy_arm()) gs_url += "/webrtc-proxy";
        attach["gsUrl"] = std::move(gs_url);
        ws_->send_message(attach.dump());
    }
    if (!WaitFor(kConnectedToGsTimeoutMs, cancelled, [this] {
            return signal_phase_ != SignalPhase::kWaitingConnected;
        })) {
        if (error_.empty() && signal_phase_ == SignalPhase::kWaitingConnected)
            error_ = "Cloud signaling connect timeout";
        return false;
    }
    if (signal_phase_ == SignalPhase::kFailed) return false;

    // 2. Client offer (non-trickle: the offer already carries candidates).
    signal_phase_ = SignalPhase::kWaitingAnswer;
    {
        Json offer;
        offer["type"] = "offer";
        offer["sdp"] = offer_sdp;
        ws_->send_message(offer.dump());
    }
    if (!WaitFor(kAnswerTimeoutMs, cancelled, [this] {
            return signal_phase_ == SignalPhase::kAnswered ||
                   signal_phase_ == SignalPhase::kFailed;
        })) {
        if (error_.empty() && signal_phase_ == SignalPhase::kWaitingAnswer)
            error_ = "Cloud signaling answer timeout";
        return false;
    }
    if (signal_phase_ == SignalPhase::kFailed) return false;

    // 3. Rewrite the answer candidates to the relay endpoint and apply.
    //    x86 pods rewrite each candidate line (BH); ARM proxy pods replace the
    //    whole candidate section with one fixed srflx line (KH).
    const std::string rewritten = sdk_.is_proxy_arm()
        ? RewriteProxyArmCandidates(signal_answer_sdp_, sdk_.gs_ip, sdk_.web_rtc_port)
        : RewriteCandidateLines(signal_answer_sdp_, sdk_.gs_ip, sdk_.web_rtc_port);
    int video_payload = 0;
    int audio_payload = 0;
    if (!ParseAnswerPayloadTypes(rewritten, &video_payload, &audio_payload) ||
        peer_connection_set_receive_payload_types(peer_, video_payload, audio_payload)!=0) {
        error_="Unsupported cloud media answer";
        return false;
    }
    peer_connection_set_remote_description(peer_, rewritten.c_str(), SDP_TYPE_ANSWER);
    answer_applied_ = true;
    ready_next_ = Clock::now() + kReadyInterval;
    return true;
}

bool VlinkSession::WaitFor(int timeout_ms, const std::atomic_bool* cancelled,
                           const std::function<bool()>& done) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (cancelled && cancelled->load()) return false;
        if (ws_) ws_->poll();
        if (done()) return true;
        if (Clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void VlinkSession::HandleSignalingText(const std::string& text) {
    const Json message = Json::parse(text, nullptr, false);
    if (!message.is_object()) return;

    if (signal_phase_ == SignalPhase::kWaitingConnected) {
        const std::string body = JsonText(message.value("message", Json()));
        if (body.find("connected to gs") != std::string::npos) {
            signal_phase_ = SignalPhase::kConnected;
            return;
        }
        if (const auto code = JsonInt(message.value("code", Json())); code && *code != 0) {
            signal_phase_ = SignalPhase::kFailed;
            error_ = body.empty() ? "Cloud signaling connect failed"
                                  : "Cloud signaling: " + body;
            return;
        }
    }

    if (signal_phase_ == SignalPhase::kWaitingAnswer) {
        const auto answer = message.find("answer");
        if (answer != message.end() && answer->is_object()) {
            const auto sdp = answer->find("sdp");
            if (sdp != answer->end() && sdp->is_string()) {
                signal_answer_sdp_ = sdp->get<std::string>();
                signal_phase_ = SignalPhase::kAnswered;
                return;
            }
        }
        if (const auto code = JsonInt(message.value("code", Json())); code && *code != 0) {
            const std::string body = JsonText(message.value("message", Json()));
            signal_phase_ = SignalPhase::kFailed;
            error_ = body.empty() ? "Cloud signaling answer failed"
                                  : "Cloud signaling: " + body;
        }
    }
}

void VlinkSession::poll() {
    if (!running_) return;
    receive_backlog_hint_=false;
    const auto drain_media=[&] {
      if(!peer_) return;
      const auto drain_started=Clock::now();
      const auto drained=DrainPeerPacketsBudgeted([this]{
        return peer_connection_loop(peer_);
    },[] {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count();
      });
      receive_backlog_hint_=receive_backlog_hint_ || drained.exhausted;
      if(drained.exhausted) ++receive_budget_hits_;
      peer_drain_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-drain_started).count());
    };
    PollMediaAroundSignaling(drain_media,[&] {
        const auto signal_started=Clock::now();
        if(ws_) ws_->poll();
        signal_poll_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-signal_started).count());
    });
    const auto now=Clock::now();
    if(!error_.empty()) return;

    MaybeCreateDataChannel();
    const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if(game_bridge_.needs_login() && !game_authorization_.valid() && !ready_game_authorization_) {
        if(callbacks_.game_authorization) {
            game_authorization_=std::async(std::launch::async,callbacks_.game_authorization,cancelled_);
        } else game_bridge_.complete_login({},now_ms);
    }
    if(game_authorization_.valid() && game_authorization_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        ready_game_authorization_=game_authorization_.get();
    }
    if(ready_game_authorization_ && game_bridge_.complete_login(ready_game_authorization_->code,now_ms)) {
        if(callbacks_.gs_message) callbacks_.gs_message("WEILINK_GAME_AUTH_STATUS",std::to_string(ready_game_authorization_->status));
        ready_game_authorization_.reset();
    }
    if(proto_version_received_ && !proto_version_handled_ && error_.empty()) {
        const auto state=startup_writes_.poll(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count(),[&](const auto& packet) {
                ++startup_attempt_sends_;
                const bool ok=SendCanned(packet.data(),packet.size());
                if(!ok) ++startup_attempt_failures_;
                return ok;
            });
        if(state==StartupWriteQueue::State::Ready) {
            // "protoVersion" answering the pod: the official page reports this
            // to the UI as SERVER_REQUEST_ENABLE (protoVersion handler).
            if(!proto_version_handled_) {
                proto_version_handled_=true;
                startup_auth_.begin(now_ms);
                if (callbacks_.gs_message)
                    callbacks_.gs_message("WEILINK_STARTUP_SENT", std::to_string(startup_attempt_sends_));
            }
        } else if(state==StartupWriteQueue::State::Failed) {
            error_="WeLink startup messages timed out";
            if (callbacks_.gs_message)
                callbacks_.gs_message("WEILINK_STARTUP_FAILED", std::to_string(startup_attempt_failures_));
        }
    }

    if (dc_open_) {
        if(proto_version_handled_) {
            // Web gameManager sends PipelineConnected on 6038 (first decoded
            // video), not 6252 (PipeState). A transport ACK for a notification
            // sent before app interception is not proof the game saw it.
            // Keep retrying a locally full bridge queue without duplicating
            // an accepted notification. No UI or HTTP work occurs here.
            if(post_frame_.pipeline_ready() && !bridge_pipeline_sent_ && game_bridge_.pipeline_connected(now_ms)) {
                bridge_pipeline_sent_=true;
                if(callbacks_.gs_message)
                    callbacks_.gs_message("WEILINK_GAME_BRIDGE_STARTED","1");
            }
            game_bridge_.poll(std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count(),[this](const auto& bytes) {
                    return SendCanned(bytes.data(),bytes.size());
                });
            if(game_bridge_.expired()!=bridge_expired_) {
                bridge_expired_=game_bridge_.expired();
                if(callbacks_.gs_message)
                    callbacks_.gs_message("WEILINK_GAME_BRIDGE_EXPIRED",std::to_string(bridge_expired_));
            }
            if(game_bridge_.confirmed()!=bridge_confirmed_) {
                bridge_confirmed_=game_bridge_.confirmed();
                if(callbacks_.gs_message)
                    callbacks_.gs_message("WEILINK_GAME_BRIDGE_CONFIRMED",std::to_string(bridge_confirmed_));
            }
        }
        if (now >= welink_beat_next_) {
            welink_beat_next_ = now + kWelinkHeartBeatInterval;
            SendCanned(kWelinkHeartBeatCanned, sizeof(kWelinkHeartBeatCanned));
        }
        if (now >= keepalive_next_) {
            keepalive_next_ = now + kKeepAliveIdle;
            if (now - last_input_at_ >= kKeepAliveIdle) send_keep_alive();
        }
    }
    if (answer_applied_ && !proto_version_received_ && now >= ready_next_) {
        // The official page clears the 1 s "READY" interval when protoVersion
        // arrives (clearInterval(v.RF) in the protoVersion handler), so this
        // raw requestStartGame text must stop before the SDPInfo handshake.
        ready_next_ = now + kReadyInterval;
        if (dc_open_) SendText("READY");
    }
    if (sdk_beat_started_ && now >= sdk_beat_next_) {
        sdk_beat_next_ = now + kSdkHeartBeatInterval;
        SimpleCmd beat;
        beat.cmd = "SDKHeartBeat";
        const auto bytes = beat.Serialize();
        SendCanned(bytes.data(), bytes.size());
    }

    if (error_.empty() && ws_ && !ws_->is_connected()) {
        if (!answer_applied_) {
            error_ = "Cloud signaling disconnected";
        } else if (!signaling_close_ignored_) {
            // Official module 2014 ignores ws onclose once the answer has been
            // applied (its `b` flag): the ws-proxy only bridges signaling, and
            // media keeps flowing over the WebRTC transport after it closes.
            // Treating this as fatal cut sessions short while the game was
            // still booting.
            signaling_close_ignored_ = true;
            if (callbacks_.gs_message) {
                int code = 0;
                const std::string text = ws_->get_last_error();
                const auto at = text.find("code=");
                if (at != std::string::npos)
                    code = std::atoi(text.c_str() + at + 5);
                callbacks_.gs_message("WEILINK_SIGNALING_CLOSED_AFTER_ANSWER",
                                      std::to_string(code));
            }
        }
    }
    if (error_.empty() && peer_) {
        const auto state = peer_connection_get_state(peer_);
        if (state == PEER_CONNECTION_FAILED || state == PEER_CONNECTION_CLOSED)
            error_ = "WebRTC transport failed";
    }
    if(error_.empty()) {
        const auto auth=startup_auth_.poll(now_ms);
        if(auth==StartupAuthPolicy::State::Rejected) error_="Cloud pod authorization rejected";
        // A pod that already supplies decoded media must not be torn down
        // solely for omitting this optional diagnostic response. Explicit
        // rejection remains fatal; the no-media case gets a bounded failure.
        else if(auth==StartupAuthPolicy::State::TimedOut && !sdk_beat_started_)
            error_="Cloud pod authorization timed out";
    }
    session_tasks_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-now).count());
}

void VlinkSession::MaybeCreateDataChannel() {
    if (!peer_ || channel_created_ || !peer_connection_sctp_connected(peer_)) return;

    char label[] = "welink";
    char protocol[] = "";
    // Official browser channel: first client-created channel -> SCTP stream 0.
    // The pod may also open its own "welink" stream; writes stay on the
    // client-created sid, exactly like module 2014's f.send().
    constexpr std::uint16_t kLocalChannelSid = 0;
    if (peer_connection_create_datachannel_sid(peer_, DATA_CHANNEL_RELIABLE, 0, 0, label,
                                               protocol, kLocalChannelSid) < 0)
        return;  // retried on the next poll

    channel_sid_ = kLocalChannelSid;
    channel_created_ = true;
    dc_open_ = true;  // ordered stream: data after the DCEP OPEN is safe
    if (callbacks_.gs_message)
        callbacks_.gs_message("WEILINK_LOCAL_DC_CREATED", std::to_string(channel_sid_));
    // Proxy-ARM pods require ChannelType when the channel opens; every other
    // pod gets it from the protoVersion handler (onDataChannelOpen @1978322,
    // protoVersion handler @1991084).
    if (sdk_.is_proxy_arm() && !SendCanned(kChannelTypeCanned, sizeof(kChannelTypeCanned))) {
        error_="Could not send WeLink channel type";
        return;
    }
    const auto now = Clock::now();
    welink_beat_next_ = now + kWelinkHeartBeatInterval;
    keepalive_next_ = now + kKeepAliveIdle;
    last_input_at_ = now;
    if (callbacks_.datachannel_open) callbacks_.datachannel_open();
}

void VlinkSession::HandleWelinkMessage(const std::uint8_t* data, std::size_t size,
                                       std::uint16_t sid) {
    const auto message = SimpleCmd::Parse(data, size);
    if (!message) {
        // Distinguish "the pod sent nothing" from "it sent a frame this client
        // cannot decode". Length, first byte and stream id are enough to
        // recognise a JSON/text message channel without recording payloads.
        std::string info = std::to_string(size) + "," + std::to_string(size ? data[0] : 0) + "," +
                           std::to_string(sid);
        if (callbacks_.gs_message) callbacks_.gs_message("WEILINK_UNPARSED", info);
        return;
    }
    const std::string arg = message->arg.value_or(std::string());
    const auto bridge_request=game_bridge_.receive(*message,
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    if(bridge_request!=GameBridge::Request::Unknown && callbacks_.gs_message)
        callbacks_.gs_message("WEILINK_GAME_BRIDGE_REQUEST",std::to_string(static_cast<int>(bridge_request)));
    if(bridge_request==GameBridge::Request::Unsupported && callbacks_.gs_message)
        callbacks_.gs_message("WEILINK_GAME_BRIDGE_UNHANDLED_HASH",
                             std::to_string(game_bridge_.unsupported_command_hash()));
    if(callbacks_.gs_message && (bridge_request==GameBridge::Request::GameData ||
       bridge_request==GameBridge::Request::AccountLogin || bridge_request==GameBridge::Request::CharacterLogin ||
       bridge_request==GameBridge::Request::CharacterLogout || bridge_request==GameBridge::Request::AccountLogout))
        callbacks_.gs_message("WEILINK_GAME_DATA_TYPE",std::to_string(game_bridge_.game_data_type()));
    if (message->cmd == "protoVersion") {
        if (!proto_version_received_) {
            proto_version_received_ = true;
            HandleProtoVersion();
        }
    } else if (message->cmd == "启动进度") {
        startup_stage_ = arg;
        if (arg == "游戏开始" && !game_started_) {
            game_started_ = true;
        }
    } else if (message->cmd == "OnAuthResult") {
        startup_auth_.result(arg);
    } else if (message->cmd == "swap_gs_info") {
        if(const auto version=ParseServerInputVersion(arg)) {
            input_version_=*version;
            if(callbacks_.gs_message)
                callbacks_.gs_message("WEILINK_INPUT_VERSION",std::to_string(*version));
        }
    } else if (message->cmd == "BusinessData") {
        // Official messageDetail @1998726 acknowledges every game->client
        // business message by echoing its id: businessDataConfirm(t[1]). The
        // pod repeats the message until the client confirms it.
        SimpleCmd confirm;
        confirm.cmd = "BusinessDataConfirm";
        confirm.arg = arg;
        const auto bytes = confirm.Serialize();
        SendCanned(bytes.data(), bytes.size());
    } else if (message->cmd == "keepLive") {
        // Sent by the pod while the game is still booting; no reply is needed.
        startup_stage_ = "keepLive";
    } else if (message->cmd == "踢下线") {
        kicked_offline_ = true;
        error_ = "Session was closed by the server";
    }
    if (message->cmd == "BitrateChange" && message->arg1) {
        // Official messageDetail reads arg1 as the actual codec type and arg2
        // as the server's game resolution. A mismatch means the pod ignored
        // our requested codec, which the official client answers with a
        // downgrade reconnect instead of decoding garbage.
        if (callbacks_.gs_message)
            callbacks_.gs_message("WEILINK_BITRATE_META",
                                  *message->arg1 + "," + message->arg2.value_or(std::string()));
    }
    if (callbacks_.gs_message) callbacks_.gs_message(message->cmd, arg);
}

void VlinkSession::HandleProtoVersion() {
    // Official protoVersion sequence (findings 14.3): LocalIm -> canned
    // ChannelType (non proxy-arm) -> start message -> SetDPI on ARM pods.
    //
    // This is a fresh DISPATCH launch: Mu(packedData, SDPInfo). gsmToken is
    // also present on fresh launches; its presence does not mean reconnect.
    // authV2 + auth is the reconnect branch. Sending both starts the same pod
    // twice and is rejected (findings 14.11).
    std::vector<std::vector<std::uint8_t>> packets;
    std::size_t packed_wire_size = 0;
    {
        SimpleCmd local_im;
        local_im.cmd = "LocalIm";
        local_im.arg = "1";
        const auto bytes = local_im.Serialize();
        packets.push_back(bytes);
    }
    if (!sdk_.is_proxy_arm())
        packets.emplace_back(std::begin(kChannelTypeCanned),std::end(kChannelTypeCanned));
    if (sdk_.packed_data.empty()) {
        error_ = "Missing cloud launch data";
        return;
    }
    {
        SimpleCmd packed;
        packed.cmd = "PackedData";
        packed.arg = MakeSdpInfoJson();
        // Official Mu does btoa(encodeURI(packedData)) and passes that string
        // to a jspb bytes field. jspb stores bytes fields as base64 and its
        // writer decodes them, so the wire payload is encodeURI(packedData),
        // not the base64 text (the two transforms cancel).
        const std::string encoded = UriEncode(sdk_.packed_data);
        packed.data = std::vector<std::uint8_t>(encoded.begin(), encoded.end());
        const auto bytes = packed.Serialize();
        packed_wire_size = bytes.size();
        packets.push_back(bytes);
    }
    if (sdk_.is_arm_game_server()) {
        // tw && $e(configs.dpi || 480) — ARM pods only (protoVersion handler
        // @1991541; schemeType enum in module 2038).
        SimpleCmd dpi;
        dpi.cmd = "SetDPI";
        dpi.arg = std::to_string(kDefaultDpi);
        const auto bytes = dpi.Serialize();
        packets.push_back(bytes);
    }
    // Length-only evidence: the payload itself contains pod credentials.
    if (callbacks_.gs_message) {
        std::size_t total = 0;
        for (const auto& packet : packets) total += packet.size();
        callbacks_.gs_message("WEILINK_STARTUP_MODE", "0");
        callbacks_.gs_message("WEILINK_STARTUP_PACKETS",
                              std::to_string(packets.size()));
        callbacks_.gs_message("WEILINK_STARTUP_BYTES", std::to_string(total));
        callbacks_.gs_message("WEILINK_PACKED_DATA_BYTES",
                              std::to_string(packed_wire_size));
    }
    startup_writes_.begin(std::move(packets),std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}

std::string VlinkSession::MakeSdpInfoJson() const {
    nlohmann::ordered_json info;
    {
        nlohmann::ordered_json local;
        local["v"] = std::string(kWeLinkSdkVersion);
        local["os"] = std::string(kOsName);
        local["t"] = std::string(kClientChannel);
        local["dpi"] = 0;
        local["decode"] = sdk_.codec_type;
        local["sample_rate"] = kSampleRate;
        local["channel"] = kAudioChannels;
        local["audio_format"] = std::string(kAudioFormat);
        local["screen_width"] = kDispatchWidth;
        local["screen_height"] = kDispatchHeight;
        info["local"] = std::move(local);
    }
    {
        // getSDPInfo copies u/t/p as-is and writes r as a number (or "" when
        // playerInfo is absent / the role number is 0).
        const auto raw = [](const std::string& text) {
            auto value = nlohmann::json::parse(text, nullptr, false);
            return value.is_discarded() ? nlohmann::json("") : value;
        };
        nlohmann::ordered_json user;
        user["u"] = raw(sdk_.user_id_json);
        user["t"] = raw(sdk_.gsm_token_json);
        user["p"] = sdk_.player_info_valid ? raw(sdk_.player_id_json)
                                           : nlohmann::json("");
        if (sdk_.player_info_valid && sdk_.player_role_number != 0)
            user["r"] = sdk_.player_role_number;
        else
            user["r"] = "";
        info["user"] = std::move(user);
    }
    {
        nlohmann::ordered_json functions;
        functions["tr_file_pb"] = 0;
        functions["tr_json_pb"] = 0;
        functions["input_ver"] = nlohmann::ordered_json::array({1, 2, 3});
        functions["support_cmd_v2"] = kSupportCmdV2;
        info["function"] = std::move(functions);
    }
    {
        nlohmann::ordered_json net;
        net["t"] = "connected";
        info["net"] = std::move(net);
    }
    return info.dump();
}

void VlinkSession::note_video_frame() {
    if (sdk_beat_started_) return;
    post_frame_.frame_received();
    sdk_beat_started_ = true;
    sdk_beat_next_ = Clock::now() + kSdkHeartBeatInterval;
    if (callbacks_.gs_message) callbacks_.gs_message("WEILINK_SDK_BEAT_STARTED", "0");
}

void VlinkSession::note_gamepad(bool connected) {
    game_bridge_.gamepad_connected(connected,std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}

bool VlinkSession::SendCanned(const std::uint8_t* bytes, std::size_t size) {
    if (!peer_ || !dc_open_ || !bytes || size == 0) return false;
    const bool accepted=peer_connection_datachannel_send_binary_sid(
        peer_, reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes)), size, channel_sid_)>=0;
    if(accepted) ++control_messages_sent_;
    else ++control_send_failures_;
    return accepted;
}

bool VlinkSession::SendText(const std::string& text) {
    if (!peer_ || !dc_open_) return false;
    return peer_connection_datachannel_send_sid(peer_, const_cast<char*>(text.data()),
                                                text.size(), channel_sid_) >= 0;
}

bool VlinkSession::send_input(const std::vector<std::uint8_t>& welink_message) {
    if (!peer_ || !dc_open_ || !proto_version_handled_ || welink_message.empty()) return false;
    const int sent = peer_connection_datachannel_send_binary_sid(
        peer_, reinterpret_cast<char*>(const_cast<std::uint8_t*>(welink_message.data())),
        welink_message.size(), channel_sid_);
    if (sent < 0) return false;
    last_input_at_ = Clock::now();
    return true;
}

bool VlinkSession::request_keyframe() {
    if (!peer_ || peer_connection_get_state(peer_) != PEER_CONNECTION_COMPLETED) return false;
    return peer_connection_request_video_keyframe(peer_) > 0;
}

bool VlinkSession::send_motion(const cloudgenshin::input::MotionSample& sample,std::int64_t monotonic_ms) {
    motion_.enable(running_ && game_bridge_.gravity_requested());
    // No backlog and no synthetic user activity. The Android app sends only
    // changed gravity (epsilon .001), subject to its sensor FPS interval.
    if(!peer_ || !dc_open_ || !proto_version_handled_ ||
       !motion_.admit(sample,monotonic_ms,34,true)) return false;
    const auto frame=game_bridge_.gravity_frame(sample);
    const bool sent=!frame.empty() && SendCanned(frame.data(),frame.size());
    if(sent) motion_.sent(sample);else motion_.failed();
    return sent;
}

bool VlinkSession::send_keep_alive() {
    const auto frame = input::KeepAliveFrame();
    return send_input(frame);
}

bool VlinkSession::set_fps(int fps) {
    if (!dc_open_ || !proto_version_handled_ || !post_frame_.has_frame()) return false;
    // The owning worker gates on first video and bounds retries per selection.
    const auto bytes=StreamFpsFrame(fps);
    return !bytes.empty() && SendCanned(bytes.data(),bytes.size());
}

bool VlinkSession::set_bitrate_tier(int tier) {
    if(!dc_open_ || !proto_version_handled_ || !post_frame_.has_frame() || !ValidBitrateTier(tier)) return false;
    return SendBitrateTier(tier,bitrate_minimum_,[this](const auto& bytes) {
        return SendCanned(bytes.data(),bytes.size());
    });
}

bool VlinkSession::set_bitrate_range(int minimum,int maximum) {
    if(!dc_open_ || !proto_version_handled_ || !post_frame_.has_frame()) return false;
    return SendBitrateRange({minimum,maximum},bitrate_minimum_,[this](const auto& bytes) {
        return SendCanned(bytes.data(),bytes.size());
    });
}

bool VlinkSession::enable_app_intercept() {
    if (!dc_open_ || !proto_version_handled_ || app_intercept_sent_) return false;
    // Independent bounded retry budget: failed FPS writes must not consume
    // the interception attempts needed for the game's SDK messages.
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    if(!post_frame_.try_write(PostFramePolicy::Write::Intercept,now)) return false;
    SimpleCmd cmd;
    cmd.cmd = "intercept_app";
    cmd.arg = std::string(kInterceptAppArg);
    const auto bytes = cmd.Serialize();
    if(!SendCanned(bytes.data(), bytes.size())) return false;
    app_intercept_sent_ = true;
    post_frame_.intercept_accepted();
    return true;
}

bool VlinkSession::set_game_resolution(int width,int height) {
    if(game_resolution_sent_) return true;
    if(!dc_open_ || !proto_version_handled_) return false;
    const auto now=Clock::now();
    if(game_resolution_attempts_>=kResolutionAttemptLimit || now<game_resolution_retry_next_) return false;
    const auto bytes=GameResolutionFrame(width,height);
    if(bytes.empty()) return false;
    ++game_resolution_attempts_;
    game_resolution_retry_next_=now+kResolutionRetryInterval;
    if(!SendCanned(bytes.data(),bytes.size())) return false;
    game_resolution_sent_=true;
    return true;
}

std::optional<PeerTransportStats> VlinkSession::transport_stats() const {
    PeerTransportStats stats{};
    if(!peer_ || peer_connection_get_transport_stats(peer_,&stats)!=0) return std::nullopt;
    return stats;
}

std::optional<PeerVideoRtpStats> VlinkSession::video_rtp_stats() const {
    PeerVideoRtpStats stats{};
    if (!peer_ || peer_connection_get_video_rtp_stats(peer_, &stats) != 0)
        return std::nullopt;
    return stats;
}

int VlinkSession::network_rtt_ms() const {
    return peer_ && running_ && peer_connection_get_state(peer_)==PEER_CONNECTION_COMPLETED
        ? peer_connection_get_rtt_ms(peer_) : -1;
}

void VlinkSession::stop() {
    closing_=true;
    running_ = false;
    if(peer_ && dc_open_ && error_.empty()) {
        const auto result=DrainExitCommand(
            [this]{return peer_connection_sctp_connected(peer_)!=0;},
            [this]{return SendCanned(kExitGameCanned,sizeof(kExitGameCanned));},
            [this]{DrainPeerPackets([this]{return peer_connection_loop(peer_);},[] {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now().time_since_epoch()).count();
            });},
            []{return std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now().time_since_epoch()).count();},
            []{std::this_thread::sleep_for(std::chrono::milliseconds(2));});
        if(callbacks_.gs_message) {
            callbacks_.gs_message(result.accepted_locally ? "WEILINK_EXIT_QUEUED_LOCAL" :
                                                           "WEILINK_EXIT_SEND_FAILED",
                                  std::to_string(result.attempts));
            callbacks_.gs_message("WEILINK_EXIT_DRAIN_MS",std::to_string(result.elapsed_ms));
        }
    }
    answer_applied_ = false;
    signaling_close_ignored_ = false;
    channel_created_ = false;
    dc_open_ = false;
    game_started_ = false;
    sdk_beat_started_ = false;
    bridge_pipeline_sent_ = false;
    game_bridge_.stop_sensors();motion_={};
    proto_version_handled_ = false;
    proto_version_received_ = false;
    startup_writes_=StartupWriteQueue{};
    startup_auth_=StartupAuthPolicy{};
    startup_attempt_sends_ = 0;
    startup_attempt_failures_ = 0;
    welink_messages_received_ = 0;
    welink_messages_other_sid_ = 0;
    control_messages_sent_ = control_send_failures_ = 0;
    post_frame_ = PostFramePolicy{};
    app_intercept_sent_ = false;
    input_version_ = 1;
    game_resolution_sent_ = false;
    game_resolution_attempts_ = 0;
    game_resolution_retry_next_ = {};
    kicked_offline_ = false;
    channel_sid_ = 0;
    startup_stage_.clear();
    signal_phase_ = SignalPhase::kWaitingConnected;
    signal_answer_sdp_.clear();

    if (ws_) {
        ws_->disconnect();
        ws_.reset();
    }
    if (peer_) {
        peer_connection_destroy(peer_);
        peer_ = nullptr;
    }
    runtime_.reset();
    if(game_authorization_.valid()) game_authorization_.wait();
}

void VlinkSession::OnVideoDamage(const PeerVideoDamage* damage, void* userdata) {
    auto* self = static_cast<VlinkSession*>(userdata);
    if (self && !self->closing_ && damage && self->callbacks_.video_discontinuity)
        self->callbacks_.video_discontinuity();
}

void VlinkSession::OnVideo(const PeerVideoPacket* packet, void* userdata) {
    auto* self = static_cast<VlinkSession*>(userdata);
    if (self && !self->closing_ && self->callbacks_.video && packet) {
        self->callbacks_.video(*packet);
    }
}

void VlinkSession::OnAudio(const PeerAudioPacket* packet, void* userdata) {
    auto* self = static_cast<VlinkSession*>(userdata);
    if (self && !self->closing_ && self->callbacks_.audio && packet) self->callbacks_.audio(*packet);
}

void VlinkSession::OnData(char* data, std::size_t len, void* userdata, std::uint16_t sid) {
    auto* self = static_cast<VlinkSession*>(userdata);
    if (!self || self->closing_ || !data || !len) return;
    // Count every delivery, including ones from a different stream id: this is
    // what distinguishes "the pod sent nothing" from "it sent on another sid".
    ++self->welink_messages_received_;
    if (sid != self->channel_sid_) ++self->welink_messages_other_sid_;
    self->HandleWelinkMessage(reinterpret_cast<const std::uint8_t*>(data), len, sid);
}

void VlinkSession::OnControlOpened(void* userdata) {
    auto* self=static_cast<VlinkSession*>(userdata);
    if(!self || self->closing_) return;
    // This is SCTP association readiness, not the remote game's SDK/login.
    if(self->callbacks_.gs_message)
        self->callbacks_.gs_message("WEILINK_CONTROL_CHANNEL_STATE","1");
}

void VlinkSession::OnControlClosed(void* userdata) {
    auto* self=static_cast<VlinkSession*>(userdata);
    if(!self || self->closing_) return;
    self->dc_open_=false;
    if(self->error_.empty()) self->error_="Cloud control channel disconnected";
    // RTP can continue while SCTP is gone. Do not let animated title frames
    // disguise a dead SDK/input connection as a healthy running game.
    if(self->callbacks_.gs_message)
        self->callbacks_.gs_message("WEILINK_CONTROL_CHANNEL_STATE","0");
}

void VlinkSession::OnRemoteChannel(void* userdata, std::uint16_t sid, const char* label, std::uint32_t ppid) {
    (void)ppid;
    auto* self = static_cast<VlinkSession*>(userdata);
    // The official page always SENDS on the channel it created itself
    // (module 2014: f = d.createDataChannel("welink"), v() -> f.send), while
    // it additionally listens on a pod-opened channel via ondatachannel.
    // A remote OPEN can arrive before our poll creates SID 0. It must not
    // enable writes or timers on the receive-only stream during that race.
    if (!self || self->closing_ || !label) return;
    if (strcmp(label, "welink") != 0) return;
    if (self->callbacks_.gs_message)
        self->callbacks_.gs_message("WEILINK_REMOTE_DC", std::to_string(sid));
}

void VlinkSession::OnPeerState(PeerConnectionState state, void* userdata) {
    auto* self = static_cast<VlinkSession*>(userdata);
    if (self && !self->closing_ && self->callbacks_.transport_state)
        self->callbacks_.transport_state(state, peer_connection_state_to_string(state));
}

}  // namespace cloudgenshin::endfield
