#pragma once
#include "mihoyo/native_decoder_selection.hpp"
#include "mihoyo/presentation_diagnostics.hpp"
#include "mihoyo/network_latency.hpp"
#include "mihoyo/sdk_start_game_params.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/media_pipeline.hpp"
#include "mihoyo/stream_health.hpp"
#include "mihoyo/launch_flow.hpp"
#include "mihoyo/stream_diagnostics_policy.hpp"
#include "mihoyo/stream_input_policy.hpp"
#include "mihoyo/stream_overlay_policy.hpp"
#include "mihoyo/compact_stream_menu.hpp"
#include "mihoyo/stream_settings_store.hpp"
#include "mihoyo/touch_forward_policy.hpp"
#include "mihoyo/autotest_pad_policy.hpp"
#include "stream/FrameSubmission.hpp"
#include "mihoyo/controller_source_policy.hpp"
#include "mihoyo/controller_sampler.hpp"
#include "mihoyo/controller_capture_policy.hpp"
#include "mihoyo/controller_mapping_editor.hpp"
#include "stream/IVideoRenderer.hpp"
#include <borealis.hpp>
#include <memory>
#include <optional>
#include "mihoyo/ui_language.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::rtc { class Session; }

class CloudGenshinStreamView final : public brls::Box {
public:
    CloudGenshinStreamView(cloudgenshin::dispatch::SdkStartGameParams params,
                           cloudgenshin::Authorization authorization,
                           cloudgenshin::Progress progress,
                           std::function<std::string()> log_snapshot,
                           std::function<bool()> export_log,std::string sdk_version,
                           int automated_seconds=0,
                           std::function<void()> reconnect={});
    ~CloudGenshinStreamView() override;
    void draw(NVGcontext*,float,float,float,float,brls::Style,brls::FrameContext*) override;
    void willDisappear(bool reset_state) override;
    brls::View* getDefaultFocus() override;
private:
    void network_loop(cloudgenshin::dispatch::SdkStartGameParams params,
                      cloudgenshin::Authorization authorization);
    // Legacy web keyboard/mouse emulation, used only when CaGameConfig leaves
    // the virtual pad off (the official client's fallback).
    void send_keyboard_mouse(cloudgenshin::rtc::Session& session,
                             const cloudgenshin::input::SwitchControllerState& state);
    void process_continue_click(cloudgenshin::rtc::Session& session,std::int64_t now_ms);
    // The official page sends RtcMouse.cursorX/cursorY (absolute) together with
    // cursorRelativeX/cursorRelativeY (movement since the previous send). Keep
    // the same pair in sync for every synthetic pointer action.
    bool move_mouse_to(cloudgenshin::rtc::Session& session,double x,double y);
    void calibrate_mouse(cloudgenshin::rtc::Session& session);
    // Official gamePlayer maps clientX/clientY through the video rect before
    // calling the SDK, so RtcMouse.cursorX/cursorY are normalised to [0,1].
    // Internally we still track the pod cursor in video pixels.
    double normalized_x(double pixel_x) const;
    double normalized_y(double pixel_y) const;
    bool send_mouse_button_at(cloudgenshin::rtc::Session& session,bool down,
                              double pixel_x,double pixel_y);
    bool send_launcher_pointer(cloudgenshin::rtc::Session& session,bool down,double x,double y);
    bool launcher_touch_=false; // Network-worker owned; fixed per terminal.
    bool launcher_entered_=false; // SDK transition permanently disables title taps.
    double mouse_x_=0.0,mouse_y_=0.0;
    bool mouse_position_known_=false;
    bool mouse_calibrated_=false;
    void set_status(const std::string& event);
    void show_diagnostics(bool visible);
    void request_exit();
    void request_activity_pop();
    void snapshot_counters();
    void sample_frame_content(AVFrame* frame);
    void update_overlay_input(const cloudgenshin::input::SwitchControllerState& state,
        cloudgenshin::overlay::Rect bounds,bool touch_down,int finger,cloudgenshin::overlay::Point point);
    void draw_overlay(NVGcontext*,cloudgenshin::overlay::Rect bounds);
    void show_quality_menu(bool visible);
    void activate_quality_row(int row);
    // Arms the game-input release gate and stamps its timeout.
    void arm_input_release();
    // Worker only: remember the floating-ball choices after a successful send.
    void PersistStreamSettings();
    void toggle_language();
    void request_continue();
    std::string mapping_profile_,mapping_game_name_;
    cloudgenshin::input::ControllerMapping controller_mapping_; // Published under mutex_.
    bool native_navigation_=false; // UI writes / worker snapshots under mutex_; session only.
    cloudgenshin::input::ControllerMappingEditor mapping_editor_; // UI only.
    std::atomic_int motion_ui_status_{0};
    cloudgenshin::Progress progress_;
    std::function<std::string()> log_snapshot_;
    std::function<bool()> export_log_;
    // User-initiated fresh launch using the saved login and normal queue. The
    // view never invokes it automatically; it is wired to a diagnostics button.
    std::function<void()> reconnect_;
    cloudgenshin::MediaPipeline media_;
    std::unique_ptr<IVideoRenderer> renderer_;
    std::thread worker_;
    std::atomic_bool cancelled_{false}, finished_{false};
    // Set by the SDK bridge when the game logs out or switches role; the
    // network loop ends the cloud session like the official client.
    std::atomic_bool session_end_requested_{false};
    std::mutex mutex_;
    cloudgenshin::input::SwitchControllerState input_;
    cloudgenshin::input::MotionSample motion_input_; // Latest only, protected by mutex_.
    cloudgenshin::input::ControllerSampler controller_sampler_;
    cloudgenshin::input::ControllerSystemButtons controller_system_buttons_;
    unsigned controller_input_generation_=1; // Protected by mutex_, like input_.
    std::atomic_bool autotest_pad_enabled_{false};
    cloudgenshin::AutoTestPadProbe autotest_pad_probe_;
    std::atomic<std::int64_t> autotest_game_enter_ms_{0};
    int autotest_pad_phase_=0;
    int autotest_pad_dump_phase_=0;
    // Web keyboard/mouse fallback state for sessions without a virtual pad.
    struct RemoteKeys {
        cloudgenshin::input::DeliveredButton w,a,s,d,space,shift,e,q,f,m,escape;
        cloudgenshin::input::DeliveredButton key1,key2,key3,key4,mouse_left,mouse_right;
    };
    RemoteKeys remote_keys_;
    bool remote_input_reported_=false;
    bool gamepad_blocked_reported_=false;
    // True once a physical button was sampled, so a "no response" report can be
    // separated from a sampling failure.
    bool first_local_press_seen_=false;
    cloudgenshin::input::ManualContinueClick continue_click_;
    bool continue_release_pending_=false;
    std::int64_t continue_release_retry_at_=0;
    std::atomic_bool continue_requested_{false};
    std::atomic_int frame_width_{1280},frame_height_{720};
    std::atomic_bool frame_geometry_ready_{false};
    // Latest 5-second picture sample, used by the opt-in autotest entry probes
    // to stop as soon as the title page is left.
    std::atomic_int last_luma_mean_{0};
    std::atomic<std::int64_t> last_luma_sample_ms_{0};
    // UI-owned floating menu; settings requests cross to the network worker.
    bool quality_menu_open_=false;
    int quality_menu_index_=0;
    cloudgenshin::overlay::CompactStreamMenu stream_menu_;
    bool quality_menu_up_=false,quality_menu_down_=false,quality_menu_a_=false,quality_menu_b_=false;
    cloudgenshin::input::QualityMenuChord quality_chord_;
    cloudgenshin::input::DiagnosticsChord diagnostics_chord_;
    bool diagnostics_chord_captured_=false;
    bool quality_chord_captured_=false;
    bool input_release_pending_=true;    // Edge detector for the floating-ball gesture so a held ball only arms the
    // release gate once instead of re-arming (and extending) it every frame.
    bool ball_was_captured_=false;
    // Worker-side decision: never depends on touch contacts.
    cloudgenshin::input::InputReleaseGate release_gate_;
    cloudgenshin::overlay::SettingRequest graphics_request_,fps_request_;
    // Last values written to this game's stream settings, so a repeated
    // sent request does not rewrite the SD card every session.
    int persisted_graphics_=-1,persisted_fps_=0;
    // Worker owned: values actually accepted by a local send. UI request
    // snapshots may already contain a newer choice that has not been sent.
    std::optional<int> accepted_graphics_,accepted_fps_;
    cloudgenshin::overlay::FloatingBall floating_ball_;
    cloudgenshin::overlay::MenuTap menu_tap_;
    cloudgenshin::overlay::StreamRates stream_rates_;
    cloudgenshin::overlay::StreamHealth stream_health_;
    cloudgenshin::overlay::NetworkLatency network_latency_;
    opennow::video::StageTimings peer_poll_gaps_,render_cpu_timings_;
    std::atomic_uint64_t ui_present_skips_{0},gameplay_present_skips_{0};
    // Game touch forwarding: the pod's launcher/loading page only accepts
    // pointer input (CaGameConfig reports no virtual pad), so contacts the
    // overlay does not own go to the official RtcTouchEvent stream. The
    // tracker runs on the UI thread; queued events cross to the network
    // worker guarded by mutex_.
    cloudgenshin::touch::TouchTracker touch_tracker_;
    std::vector<cloudgenshin::input::TouchEvent> pending_touch_;
    bool stats_visible_=false,confirm_exit_=false;
    std::string menu_feedback_;
    std::atomic_bool settings_ready_{false},control_ready_{false};
    std::atomic_bool hardware_decoder_{false};
    std::atomic_uint64_t video_bytes_{0};
    int initial_fps_=30;
    std::string status_="GAME_AUTH_CONNECTING";
    cloudgenshin::ExitHold exit_hold_;
    bool diagnostics_visible_=true,exit_requested_=false,pop_requested_=false;
    bool activity_pop_armed_=false;
    // Queued UI callbacks retain a weak token, never this view's lifetime.
    std::shared_ptr<bool> ui_lifetime_=std::make_shared<bool>(true);
    bool reconnect_requested_=false;
    bool first_render_=false,finished_panel_shown_=false;
    bool frame_metadata_seen_=false;
    bool render_failure_reported_=false;
    brls::Box* diagnostics_=nullptr;
    brls::Button* export_button_=nullptr;
    brls::Button* reconnect_button_=nullptr;
    brls::Label* status_label_=nullptr;
    brls::Label* log_label_=nullptr;
    cloudgenshin::ui::Bindings texts_;
    std::chrono::steady_clock::time_point diagnostics_refresh_{};
    std::chrono::steady_clock::time_point first_decoded_at_{};
    std::chrono::steady_clock::time_point next_frame_sample_at_{};
    std::atomic_uint frame_samples_{0};
    std::atomic_uint video_units_{0},audio_packets_{0},render_submissions_{0};
    std::atomic_uint new_render_submissions_{0},reused_render_submissions_{0},keyframe_requests_{0};
    std::atomic_uint deferred_render_submissions_{0};
    opennow::video::FrameSubmissionTracker frame_submissions_;
    cloudgenshin::NativeDecoderSelection decoder_selection_;
    cloudgenshin::overlay::PresentationDiagnostics presentation_debug_;
    int automated_seconds_=0;
    bool autotest_ui_enabled_=false;
    int autotest_ui_phase_=0;
    std::string sdk_version_;
    bool automated_reported_=false;
    const std::chrono::steady_clock::time_point created_=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point automatic_stop_check_{};
};
