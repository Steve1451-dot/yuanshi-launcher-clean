#pragma once
#include "mihoyo/native_decoder_selection.hpp"
#include "mihoyo/presentation_diagnostics.hpp"
#include "mihoyo/network_latency.hpp"
#include "stream/FrameSubmission.hpp"

// Endfield cloud streaming view: video/audio/input surface owning one
// VlinkSession (welink WebRTC path). Follows the CloudGenshinStreamView
// structure (MediaPipeline + Deko3D renderer + network worker), trimmed to
// the essentials: video, status line, diagnostics panel, stop-and-return.

#include "endfield/vlink_proto.hpp"
#include "endfield/hg_login.hpp"
#include "endfield/stream_settings.hpp"
#include "mihoyo/controller_source_policy.hpp"
#include "mihoyo/controller_sampler.hpp"
#include "mihoyo/controller_capture_policy.hpp"
#include "mihoyo/controller_mapping_editor.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/launch_flow.hpp"
#include "mihoyo/media_pipeline.hpp"
#include "mihoyo/stream_health.hpp"
#include "mihoyo/compact_stream_menu.hpp"
#include "mihoyo/stream_diagnostics_policy.hpp"
#include "mihoyo/stream_input_policy.hpp"
#include "mihoyo/ui_language.hpp"
#include "stream/IVideoRenderer.hpp"
#include <borealis.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <functional>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::endfield {

struct AllocatedStream {
    VlinkSdkData sdk;
    // Called on the network worker after every stream outcome.
    std::function<void()> release;
    std::string game_auth_code; // Ephemeral OAuth code, never persisted or logged.
    std::function<login::GameAuthorization(const std::atomic_bool*)> authorize_game;
    SavedStreamSettings settings;
};

class EndfieldStreamView final : public brls::Box {
public:
    EndfieldStreamView(AllocatedStream launch, cloudgenshin::Progress progress,
                       std::function<std::string()> log_snapshot,
                       std::function<bool()> export_log, int automated_seconds=0);
    ~EndfieldStreamView() override;
    void draw(NVGcontext*,float,float,float,float,brls::Style,brls::FrameContext*) override;
    void willDisappear(bool reset_state) override;
    brls::View* getDefaultFocus() override;
private:
    void network_loop(VlinkSdkData sdk,std::string auth_code,
        std::function<login::GameAuthorization(const std::atomic_bool*)> authorize_game);
    void set_status(const std::string& event);
    void show_diagnostics(bool visible);
    void arm_input_release();
    void show_quality_menu(bool visible);
    void activate_quality_row(int row);
    void update_overlay_input(const cloudgenshin::input::SwitchControllerState& state,
        cloudgenshin::overlay::Rect bounds,bool touch_down,int finger,cloudgenshin::overlay::Point point);
    void draw_overlay(NVGcontext* vg,cloudgenshin::overlay::Rect bounds);
    void persist_stream_settings(); // Network worker only, after accepted writes.
    void request_exit();
    void request_activity_pop();
    void snapshot_counters();
    void sample_frame_content(AVFrame* frame);
    // Annex-B NAL summary of recent access units. When the picture stops
    // updating this proves whether the pod still sends coded slices or only
    // filler/SEI, which no amount of decoder tuning can fix.
    struct VideoUnitTrace {
        unsigned size=0;
        unsigned nal_mask=0;
        unsigned slices=0;
        unsigned first_nal=0;
        unsigned start_code=0;
    };
    void record_video_unit(const PeerVideoPacket& packet);
    void dump_video_unit_trace();

    cloudgenshin::Progress progress_;
    std::function<std::string()> log_snapshot_;
    std::function<bool()> export_log_;
    int automated_seconds_=0; // 0 normal; -1 diagnostic run until explicit stop.
    unsigned frame_dump_slot_=0;
    std::chrono::steady_clock::time_point frame_dump_next_{};

    cloudgenshin::MediaPipeline media_;
    opennow::video::StageTimings peer_poll_gaps_;
    std::unique_ptr<IVideoRenderer> renderer_;
    std::thread worker_;
    std::atomic_bool cancelled_{false},finished_{false};
    std::atomic_bool input_channel_ready_{false},gamepad_neutral_pending_{true};
    std::atomic_int active_input_version_{0};
    std::atomic_uint gamepad_writes_{0},gamepad_write_failures_{0};
    std::atomic_int game_lifecycle_{0}; // Source-defined GameBridge request kind, no account data.
    // Loaded once before the network worker starts. Endfield uses the same
    // "Face buttons" choice as the Genshin/Star Rail stream.
    std::string controller_layout_="Xbox";
    cloudgenshin::input::ControllerMapping controller_mapping_; // Published under mutex_.
    cloudgenshin::input::ControllerMappingEditor mapping_editor_; // UI only.
    bool hardware_decoder_=false;

    std::mutex mutex_;
    std::string status_="STREAM_ENTRY";
    cloudgenshin::input::ControllerFrame input_;
    cloudgenshin::input::MotionSample motion_input_; // Latest only, protected by mutex_.
    cloudgenshin::input::ControllerCapture input_capture_;
    cloudgenshin::input::ControllerSampler controller_sampler_;

    std::atomic_uint video_units_{0},audio_packets_{0};
    std::atomic_uint64_t video_bytes_{0};
    std::atomic_uint frame_samples_{0};
    std::atomic_uint render_submissions_{0};
    std::atomic_uint deferred_render_submissions_{0};
    opennow::video::FrameSubmissionTracker frame_submissions_;
    cloudgenshin::NativeDecoderSelection decoder_selection_;
    cloudgenshin::overlay::PresentationDiagnostics presentation_debug_;
    std::atomic_uint new_render_submissions_{0},reused_render_submissions_{0};
    std::atomic_uint64_t ui_present_skips_{0},gameplay_present_skips_{0};
    opennow::video::StageTimings render_cpu_timings_;
    static constexpr unsigned kVideoUnitTraceSlots=8;
    std::mutex trace_mutex_;
    VideoUnitTrace video_unit_trace_[kVideoUnitTraceSlots];
    unsigned video_unit_trace_seq_=0;
    // Last decoded frame size: input coordinates are expressed in game
    // pixels, not in the 960x540 diagnostic bitmap.
    std::atomic_int frame_width_{0},frame_height_{0};

    bool frame_metadata_seen_=false;
    std::chrono::steady_clock::time_point created_=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_frame_sample_at_{};
    std::chrono::steady_clock::time_point first_decoded_at_{};
    bool first_render_=false;
    bool render_failure_reported_=false;
    bool finished_panel_shown_=false;
    bool pop_requested_=false;
    std::atomic_bool activity_pop_armed_{false};
    // UI-thread lifetime token for deferred pop callbacks.
    std::shared_ptr<bool> ui_lifetime_=std::make_shared<bool>(true);
    bool exit_requested_=false;
    bool diagnostics_visible_=false;
    bool quality_menu_open_=false,stats_visible_=false,confirm_exit_=false;
    bool quality_menu_up_=false,quality_menu_down_=false,quality_menu_a_=false,quality_menu_b_=false;
    bool ball_was_captured_=false;
    cloudgenshin::input::DiagnosticsChord diagnostics_chord_;
    bool diagnostics_chord_captured_=false;
    int quality_menu_index_=0;
    cloudgenshin::overlay::CompactStreamMenu stream_menu_;
    std::string menu_feedback_;
    cloudgenshin::overlay::FloatingBall floating_ball_;
    cloudgenshin::overlay::MenuTap menu_tap_;
    cloudgenshin::overlay::StreamRates stream_rates_;
    cloudgenshin::overlay::StreamHealth stream_health_;
    cloudgenshin::overlay::NetworkLatency network_latency_;
    cloudgenshin::overlay::SettingRequest bitrate_request_,fps_request_;
    std::atomic_bool settings_ready_{false},continue_requested_{false};
    std::atomic_int active_bitrate_limit_{0}; // Local accepted cap; not measured bitrate or server ACK.
    int initial_fps_=kDefaultStreamFps;
    int persisted_tier_=-1,persisted_fps_=0;
    std::optional<int> accepted_tier_,accepted_fps_;
    std::chrono::steady_clock::time_point diagnostics_refresh_{};
    std::chrono::steady_clock::time_point automatic_stop_check_{};
    bool automated_reported_=false;

    brls::Box* diagnostics_=nullptr;
    brls::Label* status_label_=nullptr;
    brls::Label* log_label_=nullptr;
    brls::Button* export_button_=nullptr;
    cloudgenshin::ui::Bindings texts_;
    cloudgenshin::ExitHold exit_hold_;
};

} // namespace cloudgenshin::endfield
