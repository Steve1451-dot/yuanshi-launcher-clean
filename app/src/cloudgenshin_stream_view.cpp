#include "cloudgenshin_stream_view.hpp"
#include "mihoyo/gyro_stick_assist.hpp"
#include "mihoyo/media_startup_policy.hpp"
#include "webrtc/media_ingress_diagnostics.hpp"
#include "stream/KeyframeRequestPolicy.hpp"
#include "stream/audio/AudioDiagnostics.hpp"
#include "mihoyo/media_diagnostics.hpp"
#include "mihoyo/decoder_probe_policy.hpp"
#include "mihoyo/stream_health_sample.hpp"
#include "mihoyo/network_latency_draw.hpp"
#include "stream/ffmpeg/HardwareDecodePolicy.hpp"
#include "mihoyo/rtc_session.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/sdk_device_fingerprint.hpp"
#include "mihoyo/device_info_consent.hpp"
#include "mihoyo/stream_startup_policy.hpp"
#include "mihoyo/setting_delivery_policy.hpp"
#include "mihoyo/adaptive_stream_policy.hpp"
#include "mihoyo/launcher_input_policy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "app_version.hpp"
#include "app_paths.hpp"
#include "mihoyo/autotest_policy.hpp"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef USE_DK_RENDERER
#include "stream/deko3d/DKVideoRenderer.hpp"
#elif defined(USE_GL_RENDERER)
#include "stream/opengl/GLVideoRenderer.hpp"
#endif

namespace ui=cloudgenshin::ui;
CloudGenshinStreamView::CloudGenshinStreamView(cloudgenshin::dispatch::SdkStartGameParams params,
    cloudgenshin::Authorization authorization,cloudgenshin::Progress progress,
    std::function<std::string()> log_snapshot,std::function<bool()> export_log,std::string sdk_version,
    int automated_seconds,
    std::function<void()> reconnect)
    : brls::Box(brls::Axis::COLUMN),progress_(std::move(progress)),
      log_snapshot_(std::move(log_snapshot)),export_log_(std::move(export_log)),
      reconnect_(std::move(reconnect)),
      automated_seconds_(std::clamp(automated_seconds,0,600)),
      sdk_version_(std::move(sdk_version)) {
    mapping_profile_=cloudgenshin::input::MappingProfileForApp(authorization.combo.app_id);
    mapping_game_name_=authorization.combo.app_id=="4"?"Genshin Cloud":authorization.combo.app_id=="8"?"Star Rail Cloud":"Cloud gaming";
    const auto mapping=cloudgenshin::input::LoadControllerMapping(opennow::AppHomePath(),mapping_profile_);
    controller_mapping_=mapping.value;
    if(mapping.upgraded) {
        opennow::PrepareAppStorage();
        progress_(cloudgenshin::input::SaveControllerMapping(opennow::AppHomePath(),mapping_profile_,mapping.value)
            ? "CONTROLLER_MAPPING_MIGRATED" : "CONTROLLER_MAPPING_MIGRATION_SAVE_FAILED",4,0);
    }
    motion_ui_status_=mapping_profile_=="hk4e"?(controller_mapping_.gyro_assist?2:5):0;
    progress_(mapping.restored?"CONTROLLER_MAPPING_RESTORED":"CONTROLLER_MAPPING_DEFAULT",0,0);
    autotest_pad_enabled_=automated_seconds_>0 && cloudgenshin::AutoTestPadEnabled(
        opennow::AppHomePath()+"/cloudgenshin-autotest.pad");
    autotest_ui_enabled_=automated_seconds_>0 && cloudgenshin::AutoTestUiProbeEnabled(
        opennow::AppHomePath()+"/cloudgenshin-autotest.ui");
    // Restore the floating-ball choices from the last session; fall back to the
    // official low startup preset when nothing valid was saved. These are still
    // requests, not proof the server applied them.
    const auto saved=cloudgenshin::overlay::LoadSavedSettings(
        cloudgenshin::overlay::StreamSettingsPath(opennow::AppHomePath(),mapping_profile_));
    const int startup_graphics=saved.valid ? saved.graphics : cloudgenshin::kStartupGraphicsMode;
    const int startup_fps=saved.valid ? saved.fps : cloudgenshin::kStartupStreamFps;
    if(saved.valid) {persisted_graphics_=saved.graphics;persisted_fps_=saved.fps;}
    params.target_fps=startup_fps;
    initial_fps_=params.target_fps;
    graphics_request_.request(startup_graphics);
    fps_request_.request(initial_fps_);
    setWidth(brls::Application::windowWidth);setHeight(brls::Application::windowHeight);
    setFocusable(true);setHideHighlight(true);setHideHighlightBackground(true);setHideHighlightBorder(true);
    diagnostics_=new brls::Box(brls::Axis::COLUMN);
    diagnostics_->setWidthPercentage(95);diagnostics_->setMargins(24,24,24,24);
    diagnostics_->setPadding(20);diagnostics_->setBackgroundColor(nvgRGBA(12,20,32,238));
    addView(diagnostics_);
    // Normal entry shows the stream connection status; diagnostics are opt-in
    // and are still opened automatically when the stream fails.
    diagnostics_visible_=false;
    diagnostics_->setVisibility(brls::Visibility::GONE);
    auto* heading=new brls::Box(brls::Axis::ROW);heading->setHeight(52);diagnostics_->addView(heading);
    auto* title=new brls::Label();title->setFontSize(24);title->setGrow(1);
    texts_.add(title,"Stream diagnostics",std::string(opennow::kNativeAppName)+" "+opennow::kCloudGenshinVersion+" / ");
    heading->addView(title);
    auto* language=new brls::Button();language->setText("中文 / English");language->setWidth(230);language->setHeight(44);
    language->registerClickAction([this](brls::View*) {
        toggle_language();
        return true;
    });heading->addView(language);
    status_label_=new brls::Label();status_label_->setFontSize(20);
    status_label_->setTextColor(nvgRGB(90,225,235));diagnostics_->addView(status_label_);
    auto* help=new brls::Label();help->setFontSize(17);help->setMarginTop(10);help->setMarginBottom(12);
    texts_.add(help,"Logs save automatically. Touch the ball for settings; hold L3+R3 for diagnostics.\nMinus opens the game map. Hold + and - to stop.");
    diagnostics_->addView(help);
    auto* buttons=new brls::Box(brls::Axis::ROW);buttons->setHeight(52);diagnostics_->addView(buttons);
    export_button_=new brls::Button();texts_.add(export_button_,"Export safe log");
    export_button_->setWidth(275);export_button_->setMarginRight(12);
    export_button_->registerClickAction([this](brls::View*) {
        snapshot_counters();
        export_button_->setText(ui::Text(export_log_() ? "Saved to SD" : "SD failed / photograph log"));
        return true;
    });buttons->addView(export_button_);
    auto* exit=new brls::Button();texts_.add(exit,"Stop + return");exit->setWidth(240);exit->setMarginRight(12);
    exit->registerClickAction([this](brls::View*) {request_exit();return true;});buttons->addView(exit);
    auto* close=new brls::Button();texts_.add(close,"Hide diagnostics");close->setWidth(240);
    close->registerClickAction([this](brls::View*) {show_diagnostics(false);return true;});buttons->addView(close);
    auto* settings=new brls::Button();texts_.add(settings,"Stream settings");settings->setWidth(220);settings->setMarginLeft(12);
    settings->registerClickAction([this](brls::View*) {show_quality_menu(true);return true;});buttons->addView(settings);
    log_label_=new brls::Label();log_label_->setFontSize(17);log_label_->setMarginTop(12);
    log_label_->setTextColor(nvgRGB(198,212,228));diagnostics_->addView(log_label_);
    if(reconnect_) {
        // Only a user press starts a new launch. It reuses the saved login and
        // the normal queue; login, QR, queue and logout are never auto-clicked.
        auto* reconnect_button=new brls::Button();
        reconnect_button_=reconnect_button;
        texts_.add(reconnect_button_,"Reconnect");
        reconnect_button_->setWidth(240);reconnect_button_->setMarginTop(12);
        reconnect_button_->registerClickAction([this](brls::View*) {
            progress_("SESSION_END_RECONNECT_REQUESTED",0,0);
            reconnect_requested_=true;
            request_exit();
            return true;
        });
        diagnostics_->addView(reconnect_button_);
    }
    progress_("STREAM_ENTRY",0,0);
    progress_("STREAM_TARGET_FPS",cloudgenshin::EffectiveStreamFps(params.target_fps),0);
    // Report the value actually requested, not the compile-time default: the
    // log must show whether a saved quality preference was restored.
    progress_("STREAM_STARTUP_QUALITY",startup_graphics,0);
    progress_("STREAM_SETTINGS_RESTORED",saved.valid ? 1 : 0,0);
    progress_("FIRST_VIDEO_WAIT_LIMIT_120S",1,0);
#ifdef USE_DK_RENDERER
    renderer_=std::make_unique<DKVideoRenderer>();
    renderer_->setLowCostPresentation(true);
    progress_("VIDEO_RENDER_LOW_COST_MODE",1,0);
#elif defined(USE_GL_RENDERER)
    renderer_=std::make_unique<GLVideoRenderer>();
#endif
    opennow::PrepareAppStorage();
    const bool hardware_probe=decoder_selection_.begin(opennow::AppHomePath());
    progress_(hardware_probe ? "VIDEO_HW_PREFERRED_EXPERIMENTAL" : decoder_selection_.blocked() ?
        "VIDEO_HW_UNCLEAN_OR_STORAGE_GUARD_SOFTWARE" : "VIDEO_SOFTWARE_SELECTED",0,0);
    if (!renderer_ || !media_.start(params.target_fps,40,hardware_probe)) {
        set_status("MEDIA_INITIALIZATION_FAILED");finished_=true;return;
    }
    progress_(media_.hardware_decoder() ? "VIDEO_DECODER_HARDWARE_INITIALIZED" : "VIDEO_DECODER_SOFTWARE_READY",0,0);
    hardware_decoder_=media_.hardware_decoder();
    if (!media_.sound_ready()) progress_("AUDIO_INITIALIZATION_FAILED",0,0);
    worker_=std::thread([this,params=std::move(params),authorization=std::move(authorization)]() mutable {
        try {network_loop(std::move(params),std::move(authorization));}
        catch (...) {set_status("STREAM_RUNTIME_FAILED");}
        settings_ready_=false;control_ready_=false;
        // AVFrameHolder lends a frame to draw(). Do not free it from this
        // network thread; the UI owner does final cleanup after joining us.
        media_.request_stop();
        snapshot_counters();
        // Announce completion last: the UI may pop this activity the moment it
        // sees the flag, and the activity's destructor joins this very thread.
        progress_("STREAM_WORKER_FINISHED",0,0);
        finished_=true;
    });
}
CloudGenshinStreamView::~CloudGenshinStreamView() {
    ui_lifetime_.reset();
    cancelled_=true;
    if (worker_.joinable()) worker_.join();
    // Release GPU-held frame references before decoder/frame-store cleanup.
    renderer_.reset();media_.stop();AVFrameHolder::instance().releaseViewReference();
    decoder_selection_.finish();
}
void CloudGenshinStreamView::willDisappear(bool reset_state) {
    cancelled_=true;brls::Box::willDisappear(reset_state);
}
brls::View* CloudGenshinStreamView::getDefaultFocus() {
    return diagnostics_visible_ ? static_cast<brls::View*>(export_button_) : this;
}
void CloudGenshinStreamView::show_diagnostics(bool visible) {
    if(visible) show_quality_menu(false);
    diagnostics_visible_=visible;
    diagnostics_->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    arm_input_release();
    if (visible) {std::lock_guard lock(mutex_);input_={};}
    diagnostics_refresh_={};
    brls::Application::giveFocus(visible ? static_cast<brls::View*>(export_button_) : this);
}
void CloudGenshinStreamView::request_exit() {
    if (exit_requested_) return;
    exit_requested_=true;cancelled_=true;
    set_status("STREAM_STOP_REQUESTED");
    snapshot_counters();show_diagnostics(true);
}
void CloudGenshinStreamView::request_activity_pop() {
    if(activity_pop_armed_) return;
    activity_pop_armed_=true;
    const std::weak_ptr<bool> lifetime=ui_lifetime_;
    // Borealis may synchronously destroy the activity during popActivity.
    // Defer until draw has returned, and discard tasks for a destroyed view.
    brls::sync([this,lifetime] {
        if(lifetime.expired()) return;
        activity_pop_armed_=false;
        if(pop_requested_) return;
        const auto stack=brls::Application::getActivitiesStack();
        if(stack.empty() || !stack.back() || stack.back()->getContentView()!=this) return;
        auto reconnect=reconnect_requested_ ? reconnect_ : std::function<void()>{};
        const auto progress=progress_;
        try {if(export_log_) export_log_();} catch(...) {}
        pop_requested_=true;
        progress("STREAM_POP_REQUESTED",0,0);
        const bool popped=brls::Application::popActivity(brls::TransitionAnimation::FADE,[reconnect] {
            const auto activities=brls::Application::getActivitiesStack();
            if(!activities.empty() && activities.back() && activities.back()->getContentView())
                brls::Application::giveFocus(activities.back()->getContentView());
            // A requested fresh launch waits for the outgoing activity's
            // destruction; starting it inside the pop callback races teardown.
            if(reconnect) brls::sync(reconnect);
        });
        // Use copied callbacks after pop: a successful pop may have deleted us.
        progress(popped ? "STREAM_POP_ACCEPTED" : "STREAM_POP_REFUSED",0,0);
        if(!popped && !lifetime.expired()) pop_requested_=false;
    });
}
namespace {
template<class Report>
void ReportGenshinVideoRtp(cloudgenshin::rtc::Session& session,Report&& report) {
    PeerVideoRtpStats stats{};
    if(!session.video_rtp_stats(&stats)) return;
    report("VIDEO_RTP_PACKETS",static_cast<int>(std::min<std::uint32_t>(stats.packets_received,INT_MAX)),0);
    report("VIDEO_RTP_GAPS",static_cast<int>(std::min<std::uint32_t>(stats.sequence_gaps,INT_MAX)),0);
    report("VIDEO_RTP_AU_DROPPED",static_cast<int>(std::min<std::uint32_t>(stats.access_units_dropped,INT_MAX)),0);
    report("VIDEO_RTP_REORDERED",static_cast<int>(std::min<std::uint32_t>(stats.reordered_packets,INT_MAX)),0);
    report("VIDEO_RTP_LATE_DROPPED",static_cast<int>(std::min<std::uint32_t>(stats.late_packets_dropped,INT_MAX)),0);
    report("VIDEO_RTP_FORCED_SKIPS",static_cast<int>(std::min<std::uint32_t>(stats.forced_sequence_skips,INT_MAX)),0);
    report("VIDEO_RTP_BUFFERED",static_cast<int>(std::min<std::uint32_t>(stats.reorder_buffered_packets,INT_MAX)),0);
    report("VIDEO_RTP_NACK_REQUESTS",static_cast<int>(std::min<std::uint32_t>(stats.nack_requests,INT_MAX)),0);
    report("VIDEO_RTP_NACK_PACKETS",static_cast<int>(std::min<std::uint32_t>(stats.nack_packets_requested,INT_MAX)),0);
    report("VIDEO_RTP_NACK_RETRIES",static_cast<int>(std::min<std::uint32_t>(stats.nack_retry_packets,INT_MAX)),0);
    report("VIDEO_RTP_NACK_RECOVERED",static_cast<int>(std::min<std::uint32_t>(stats.nack_recovered_packets,INT_MAX)),0);
    report("VIDEO_RTP_NACK_EXHAUSTED",static_cast<int>(std::min<std::uint32_t>(stats.nack_exhausted_packets,INT_MAX)),0);
    report("VIDEO_RTP_NACK_SEND_FAILURES",static_cast<int>(std::min<std::uint32_t>(stats.nack_send_failures,INT_MAX)),0);
    report("VIDEO_RTP_REPAIR_MAX_MS",static_cast<int>(std::min<std::uint32_t>(stats.nack_repair_max_ms,INT_MAX)),0);
    report("VIDEO_RTP_TIMEOUT_SKIPS",static_cast<int>(std::min<std::uint32_t>(stats.reorder_timeout_skips,INT_MAX)),0);
    report("VIDEO_RTP_CAPACITY_SKIPS",static_cast<int>(std::min<std::uint32_t>(stats.reorder_capacity_skips,INT_MAX)),0);
    report("VIDEO_RTP_WINDOW_CAPACITY",static_cast<int>(stats.reorder_capacity),0);
    report("VIDEO_RTP_WINDOW_PEAK",static_cast<int>(std::min<std::uint32_t>(stats.reorder_peak_packets,INT_MAX)),0);
    report("VIDEO_RTP_WINDOW_GROWTHS",static_cast<int>(std::min<std::uint32_t>(stats.reorder_growths,INT_MAX)),0);
    report("VIDEO_RTP_WINDOW_ALLOC_FAILURES",static_cast<int>(std::min<std::uint32_t>(stats.reorder_growth_failures,INT_MAX)),0);
}
} // namespace

void CloudGenshinStreamView::snapshot_counters() {
    const auto report=[this](const char* name,unsigned value) {
        progress_(name,static_cast<int>(std::min(value,static_cast<unsigned>(std::numeric_limits<int>::max()))),0);
    };
    report("VIDEO_UNITS_RECEIVED",video_units_);
    report("VIDEO_IDR_UNITS_RECEIVED",media_.idrs());
    report("VIDEO_DECODED_FRAMES",media_.decoded());
    report("VIDEO_DECODE_SUBMITTED_UNITS",media_.submitted());
    report("VIDEO_DECODE_ZERO_OUTPUT_UNITS",media_.zero_output());
    report("VIDEO_DECODE_QUEUE_DEPTH",media_.queue_depth());
    report("VIDEO_DROPPED_UNITS",media_.drops());
    report("VIDEO_DECODE_ERRORS",media_.errors());
    cloudgenshin::ReportMediaPerformance(media_.performance(),progress_);
    progress_("VIDEO_LAST_DECODE_ERROR",media_.last_decode_error(),0);
    report("AUDIO_PACKETS_RECEIVED",audio_packets_);
    opennow::audio::ReportOutputHealth(media_.audio_levels(),progress_);
    report("VIDEO_RENDER_SUBMISSIONS",render_submissions_);
    report("VIDEO_NEW_FRAME_SUBMISSIONS",new_render_submissions_);
    report("VIDEO_RENDER_DEFERRED_SUBMISSIONS",deferred_render_submissions_);
    presentation_debug_.report(progress_);
    report("VIDEO_PRESENT_REFERENCE_FAILURES",AVFrameHolder::instance().getReferenceFailureStat());
    report("VIDEO_PRESENT_CLOCK_REBASES",AVFrameHolder::instance().getClockRebaseStat());
    report("VIDEO_PRESENT_TIMING_HOLDS",AVFrameHolder::instance().getTimingHoldStat());
    report("VIDEO_REUSED_FRAME_SUBMISSIONS",reused_render_submissions_);
    report("VIDEO_UI_INTERVAL_PRESENT_SKIPS",static_cast<unsigned>(std::min<std::uint64_t>(ui_present_skips_.load(),UINT_MAX)));
    report("VIDEO_GAMEPLAY_PRESENT_SKIPS",static_cast<unsigned>(std::min<std::uint64_t>(gameplay_present_skips_.load(),UINT_MAX)));
    progress_("RTC_POLL_GAP_MEAN_US",static_cast<int>(std::min<std::uint64_t>(peer_poll_gaps_.mean(),INT_MAX)),0);
    progress_("RTC_POLL_GAP_MAX_US",static_cast<int>(std::min<std::uint64_t>(peer_poll_gaps_.maximum(),INT_MAX)),0);
    report("VIDEO_KEYFRAME_REQUESTS",keyframe_requests_);
    report("INPUT_CONTROL_READY",control_ready_ ? 1 : 0);
    report("VIDEO_FRAME_SAMPLE_COUNT",frame_samples_);
    progress_("STREAM_DIAGNOSTICS_SNAPSHOT",0,0);
}
namespace {
// Opt-in autotest image: writes the decoded luma plane as an 8-bit grayscale
// BMP (960x540) so the screen the pod is showing can be identified (title
// page, loading page, dialog, in-game). It only runs for the bounded autotest
// session and only reads the video decoder output; the desktop is never
// captured.
bool WriteLumaBmp(const std::string& path,const unsigned char* plane,
                  int width,int height,int stride) {
    constexpr int kWidth=960,kHeight=540;
    constexpr std::uint32_t kPalette=256*4;
    const auto put32=[&](unsigned char* header,int offset,std::uint32_t value) {
        header[offset]=static_cast<unsigned char>(value);
        header[offset+1]=static_cast<unsigned char>(value>>8);
        header[offset+2]=static_cast<unsigned char>(value>>16);
        header[offset+3]=static_cast<unsigned char>(value>>24);
    };
    const auto put16=[&](unsigned char* header,int offset,std::uint16_t value) {
        header[offset]=static_cast<unsigned char>(value);
        header[offset+1]=static_cast<unsigned char>(value>>8);
    };
    unsigned char header[54]{};
    header[0]='B';header[1]='M';
    put32(header,2,54+kPalette+static_cast<std::uint32_t>(kWidth*kHeight));
    put32(header,10,54+kPalette);
    put32(header,14,40);
    put32(header,18,kWidth);
    put32(header,22,kHeight);
    put16(header,26,1);put16(header,28,8);
    put32(header,34,static_cast<std::uint32_t>(kWidth*kHeight));
    put32(header,38,2835);put32(header,42,2835);
    put32(header,46,256);put32(header,50,256);
    FILE* file=fopen(path.c_str(),"wb");
    if(!file) return false;
    bool ok=fwrite(header,1,sizeof(header),file)==sizeof(header);
    if(ok) {
        unsigned char palette[kPalette];
        for(int i=0;i<256;++i) {
            palette[i*4]=static_cast<unsigned char>(i);
            palette[i*4+1]=static_cast<unsigned char>(i);
            palette[i*4+2]=static_cast<unsigned char>(i);
            palette[i*4+3]=0;
        }
        ok=fwrite(palette,1,kPalette,file)==kPalette;
    }
    std::vector<unsigned char> line(kWidth);
    for(int row=kHeight-1;ok && row>=0;--row) {
        const unsigned char* source=plane+
            static_cast<long long>(row*height/kHeight)*stride;
        for(int column=0;column<kWidth;++column)
            line[column]=source[column*width/kWidth];
        ok=fwrite(line.data(),1,line.size(),file)==line.size();
    }
    fclose(file);
    return ok;
}
} // namespace
// Numeric content check for the frame handed to the renderer: a sparse luma
// grid proves the decoded picture is real non-black imagery. Only summary
// numbers leave this function; no pixels are stored, exported or logged.
void CloudGenshinStreamView::sample_frame_content(AVFrame* frame) {
    if(!opennow::video::LinearLumaReadable(frame->format)) return;
    const unsigned char* plane=frame->data[0];
    const int width=frame->width,height=frame->height,stride=frame->linesize[0];
    if (!plane || width<16 || height<16 || stride<=0) return;
    ++frame_samples_;
    constexpr int kRows=36,kColumns=64;
    unsigned long long sum=0,gradient=0;
    unsigned luma_min=255,luma_max=0;
    int samples=0,gradient_samples=0,nonblack=0;
    for (int row=0;row<kRows;++row) {
        const unsigned char* line=plane+static_cast<long long>(row*height/kRows)*stride;
        int previous=-1;
        for (int column=0;column<kColumns;++column) {
            const int value=line[column*width/kColumns];
            sum+=static_cast<unsigned>(value);
            if (static_cast<unsigned>(value)<luma_min) luma_min=static_cast<unsigned>(value);
            if (static_cast<unsigned>(value)>luma_max) luma_max=static_cast<unsigned>(value);
            if (value>=24) ++nonblack;
            if (previous>=0) {
                const int delta=value>previous ? value-previous : previous-value;
                gradient+=static_cast<unsigned>(delta);++gradient_samples;
            }
            previous=value;++samples;
        }
    }
    if (samples==0) return;
    const int mean=static_cast<int>(sum/static_cast<unsigned>(samples));
    last_luma_mean_.store(mean);
    last_luma_sample_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MEAN",mean,0);
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MIN",static_cast<int>(luma_min),0);
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MAX",static_cast<int>(luma_max),0);
    progress_("VIDEO_FRAME_SAMPLE_CONTRAST",gradient_samples
        ? static_cast<int>(gradient/static_cast<unsigned>(gradient_samples)) : 0,0);
    progress_("VIDEO_FRAME_SAMPLE_NONBLACK",nonblack*100/samples,0);
    // Coarse 48x27 block signature. Two bounded numbers (motion, changed) and,
    // only for the opt-in autotest run, an ASCII brightness map so a human can
    // tell a live title page from a frozen or dialog-covered picture. Block
    // means are written to a log file; no pixels are stored or exported.
    constexpr int kThumbColumns=48,kThumbRows=27;
    static unsigned char thumbnail_blocks[kThumbColumns*kThumbRows];
    static bool thumbnail_ready=false;
    long long movement=0;
    int changed_blocks=0;
    std::string thumbnail;
    thumbnail.reserve(kThumbRows*(kThumbColumns+4)+64);
    for (int row=0;row<kThumbRows;++row) {
        thumbnail+=static_cast<char>('0'+row/10);
        thumbnail+=static_cast<char>('0'+row%10);
        thumbnail+=' ';
        const int y0=row*height/kThumbRows;
        const int y1=std::max(y0+1,(row+1)*height/kThumbRows);
        for (int column=0;column<kThumbColumns;++column) {
            const int x0=column*width/kThumbColumns;
            const int x1=std::max(x0+1,(column+1)*width/kThumbColumns);
            unsigned sum=0,count=0;
            for (int sy=0;sy<4;++sy) {
                const unsigned char* line=plane+static_cast<long long>(y0+(y1-y0)*sy/4)*stride;
                for (int sx=0;sx<4;++sx) {sum+=line[x0+(x1-x0)*sx/4];++count;}
            }
            const unsigned value=count ? sum/count : 0;
            const int index=row*kThumbColumns+column;
            if (thumbnail_ready) {
                const int delta=std::abs(static_cast<int>(value)-static_cast<int>(thumbnail_blocks[index]));
                movement+=delta;
                if (delta>=12) ++changed_blocks;
            }
            thumbnail_blocks[index]=static_cast<unsigned char>(value);
            thumbnail+="0123456789abcdef"[value>>4];
        }
        thumbnail+='\n';
    }
    const int motion=thumbnail_ready
        ? static_cast<int>(movement*10/(kThumbColumns*kThumbRows)) : 0;
    if (thumbnail_ready) progress_("VIDEO_FRAME_SAMPLE_MOTION_X10",motion,0);
    thumbnail_ready=true;
    const std::string thumbnail_path=opennow::AppHomePath()+"/frame_thumb.log";
    if (FILE* file=automated_seconds_!=0 ? fopen(thumbnail_path.c_str(),"a") : nullptr) {
        fprintf(file,"THUMB t=%lld w=%d h=%d mean=%d motion_x10=%d changed=%d\n",
            static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()),
            width,height,mean,motion,changed_blocks);
        fputs(thumbnail.c_str(),file);
        fclose(file);
    }
    // Bounded autotest screenshots of the stream itself (never the desktop).
    if (automated_seconds_>0) {
        if(autotest_pad_enabled_ && autotest_pad_phase_>0 && autotest_pad_phase_!=autotest_pad_dump_phase_) {
            if(WriteLumaBmp(opennow::AppHomePath()+"/pad_probe_"+
                    std::to_string(autotest_pad_phase_)+".bmp",plane,width,height,stride))
                autotest_pad_dump_phase_=autotest_pad_phase_;
        }
        static int dumped=0;
        static std::chrono::steady_clock::time_point next_dump{};
        const auto now=std::chrono::steady_clock::now();
        if (dumped<12 && (dumped==0 || now>=next_dump)) {
            const std::string path=opennow::AppHomePath()+"/frame_dump_"+
                std::to_string(dumped)+".bmp";
            if (WriteLumaBmp(path,plane,width,height,stride)) {
                progress_("VIDEO_FRAME_DUMP_WRITTEN",dumped+1,0);
                ++dumped;
                next_dump=now+std::chrono::seconds(8);
            }
        }
    }
}
void CloudGenshinStreamView::set_status(const std::string& event) {
    {std::lock_guard lock(mutex_);status_=event;}
    progress_(event,0,0);
}
void CloudGenshinStreamView::network_loop(cloudgenshin::dispatch::SdkStartGameParams params,
                                          cloudgenshin::Authorization authorization) {
    using namespace cloudgenshin;
    auto alternatives=params.additional_game_server_wss_urls;
    if (!params.game_server_wss_url_ipv6.empty()) alternatives.push_back(params.game_server_wss_url_ipv6);
    alternatives.insert(alternatives.end(),params.additional_game_server_wss_urls_ipv6.begin(),
        params.additional_game_server_wss_urls_ipv6.end());
    // The cloud terminal type drives both the StartGame request and the SDK
    // channel string the game reads through info_get_cps. Official values:
    // CTT_PC=1, CTT_IOS=2, CTT_ANDROID=3, CTT_MAC=4, CTT_WEB_PC=5,
    // CTT_WEB_IOS=6, CTT_WEB_ANDROID=7, CTT_WEB_MAC=8, CTT_WEB_TOUCH=9,
    // CTT_WEB_KEYBOARD=10. The web page composes "<prefix>_mihoyo" with
    // pc/mac/android/ios for those families.
    const unsigned terminal_type=cloudgenshin::LoadTerminalTypeOverride(
        opennow::AppHomePath()+"/cloudgenshin-terminal.txt",cloudgenshin::kWebKeyboardTerminal);
    progress_("XBOX_INPUT_MODE_ACTIVE",0,0);
    if(mapping_profile_=="hk4e") progress_("GENSHIN_LOCAL_GYRO_ZL_DEFAULT_READY",0,0);
    progress_("STREAM_TERMINAL_TYPE",static_cast<int>(terminal_type),0);
    if(mapping_profile_=="hk4e") progress_(terminal_type==3?
        "MOTION_NATIVE_ANDROID_ROUTE_SELECTED":"MOTION_NATIVE_ANDROID_ROUTE_NOT_SELECTED",0,0);
    launcher_touch_=cloudgenshin::input::UsesTouchLauncher(terminal_type);
    progress_(launcher_touch_?"LAUNCHER_POINTER_TOUCH":"LAUNCHER_POINTER_MOUSE",0,0);
    // In-game SDK bridge: the cloud game asks this client to run the official
    // web login (Init/login_*) over the cmd-1900 reliable queue, and the reply
    // carries the user-authorized login that already happened on this device.
    sdk::BridgeContext bridge;
    switch (terminal_type) {
    case 2: case 6: bridge.cps="ios_mihoyo"; break;
    case 3: case 7: case 9: bridge.cps="android_mihoyo"; break;
    case 4: case 8: bridge.cps="mac_mihoyo"; break;
    default: bridge.cps="pc_mihoyo"; break;
    }
    bridge.game_token=params.game_token;
    bridge.account_id=authorization.account_id;
    bridge.device_id=authorization.device.device_id;
    bridge.app_id=authorization.combo.app_id;
    bridge.channel_id=authorization.combo.channel_id;
    bridge.channel_token=authorization.channel_token;
    if(const auto* profile=FindGameProfile(mapping_profile_);profile && profile->id=="hkrpg") {
        if(OfficialDeviceInfoAllowed(opennow::AppHomePath()+"/cloudgenshin-device-info-consent.txt")) {
            auto fp=FetchSdkDeviceFingerprint(*profile,authorization.device,cancelled_,progress_);
            if(cancelled_) {set_status("STREAM_STOPPED_BY_USER");return;}
            if(fp) bridge.device_fingerprint=std::move(*fp);
            else progress_("SDK_DEVICE_FP_UNAVAILABLE_NO_LOCAL_SUBSTITUTE",0,0);
        } else progress_("SDK_DEVICE_FP_CONSENT_REQUIRED_NOT_SENT",0,0);
    }
    // Opt-in Star Rail re-grant experiment, not the normal startup path. Do
    // HTTP before peer startup, never in a media/SDK callback. Preserve the
    // launch authorization and encrypted store; failure is explicitly logged.
    auto game_combo=authorization.combo;
    if(automated_seconds_>0 && AutoTestStarRailFreshSdkLoginEnabled(
            opennow::AppHomePath(),authorization.combo.app_id)) {
        if(const auto* profile=FindGameProfile(mapping_profile_);profile && profile->id=="hkrpg") {
            if(auto refreshed=RefreshGameSdkLogin(*profile,authorization,cancelled_,progress_))
                game_combo=std::move(*refreshed);
            else if(!cancelled_) progress_("GAME_SDK_LOGIN_USING_EXISTING_GRANT",0,0);
        }
    }
    bridge.open_id=game_combo.open_id;
    bridge.combo_token=game_combo.combo_token;
    bridge.sdk_login_data=game_combo.sdk_login_data;
    // login_logout / login_switch_role end the official cloud session.
    bridge.on_session_end=[this]{session_end_requested_=true;cancelled_=true;};
    // Independent four-attempt entry probe, only with its explicit test
    // marker. The normal bounded auto-entry policy below remains separate.
    const bool entry_click_enabled=automated_seconds_>0 &&
        cloudgenshin::AutoTestEntryClickEnabled(opennow::AppHomePath()+"/cloudgenshin-autotest.click");
    // Default launcher auto-entry. It only starts after the first decoded
    // stream frame, so login/QR/queue screens are never clicked. The opt-out
    // marker keeps a fully manual path available.
    const bool auto_entry_enabled =
        !std::ifstream(opennow::AppHomePath()+"/cloudgenshin-auto-entry.off").good();
    constexpr int kEntryProbeCount=4;
    bool entry_probe_armed=false,entry_probe_stopped=false;
    int entry_action_index=0;
    int entry_tap_phase=0;
    double entry_tap_x=0.5,entry_tap_y=0.947;
    std::chrono::steady_clock::time_point entry_tap_due{};
    // The title page brightness differs per pod and per game build (a bright
    // launcher page is normal), so the sequence stops on a real scene change
    // from the brightness seen once the first few actions have run, not on an
    // absolute threshold. The baseline is re-armed during the logo->title
    // transition so that early fade-in cannot end the sequence.
    bool entry_luma_baseline_ready=false;
    int entry_luma_baseline=0;
    std::chrono::steady_clock::time_point entry_probe_due{},entry_mouse_due{};
    // Launcher-only pad shortcut: the streamed title page is a pointer page,
    // so one physical face-button press sends a single tap on its "点击进入"
    // bar. This is not a gameplay mapping: it is consumed once, only before
    // the first real touch, and every gameplay frame is still a pure virtual
    // Xbox pad frame.
    bool pad_entry_button_was_held=false,pad_entry_release_pending=false;
    bool pad_entry_release_warned=false;
    std::chrono::steady_clock::time_point pad_entry_release_due{};
    // Default launcher auto-entry. It arms only after the stream has decoded a
    // real frame, so the login/QR/queue screens are never clicked. Each tap is
    // a normal TouchController tap on the title page's "点击进入" bar. Before
    // the next tap it waits for a luma sample taken after the previous tap, so
    // a successful entry stops the sequence instead of clicking the game
    // behind it. Any real touch or pad press also takes over immediately.
    constexpr int kAutoEntryMaxTaps=10;
    bool auto_entry_started=false,auto_entry_stopped=false;
    bool auto_entry_tap_held=false,auto_entry_need_sample=false;
    bool auto_entry_pad_hold=false;
    bool auto_entry_baseline_ready=false,auto_entry_release_warned=false;
    int auto_entry_taps=0,auto_entry_baseline=0;
    std::int64_t auto_entry_last_tap_ms=0;
    std::chrono::steady_clock::time_point auto_entry_due{},auto_entry_release_due{};
    // Throttle only Move diagnostics; Down/Up are always logged so a live tap
    // can be verified from the safe log without flooding it.
    std::chrono::steady_clock::time_point touch_move_log_due{};
    // One held gamepad confirm tap (XInput A). The regular 16 ms sender keeps
    // the button down for 120 ms and then sends the neutral release.
    constexpr int kConfirmEntry=1,kConfirmContinue=2;
    std::chrono::steady_clock::time_point confirm_until{};
    int confirm_source=0;
    bool confirm_report_pending=false;
    rtc::Session session(params.game_server_wss_url,
        {params.game_token,authorization.account_id,sdk_version_,params.sid,params.pod_id,terminal_type},{},
        {.video=[this](const PeerVideoPacket& packet){
             ++video_units_;video_bytes_.fetch_add(packet.size,std::memory_order_relaxed);media_.video(packet);
         },
         .video_damage=[this](const PeerVideoDamage&){media_.notify_video_damage();},
         .audio=[this](const PeerAudioPacket& packet){++audio_packets_;media_.audio(packet);},
         .data={},
         .state=[this](PeerConnectionState state){
             progress_("RTC_PEER_STATE",static_cast<int>(state),0);
             if (state==PEER_CONNECTION_COMPLETED) set_status("RTC_CONNECTED_WAITING_VIDEO");
         },
         .authenticated=[this](const control::StartResult& result){
             // Server-provided flags (diagnostic only: the session forces the
             // Xbox pad on, so these values no longer decide the input route).
             progress_("GAME_CONFIG_XBOX",result.xbox_enabled ? 1 : 0,0);
             progress_("GAME_CONFIG_PS",result.ps_enabled ? 1 : 0,0);
             progress_("GAME_CONFIG_GR0006",result.gr0006_enabled ? 1 : 0,0);
             progress_("GAME_CONFIG_WEBCTRL",result.control_channel_type,0);
             if(!result.xbox_enabled && !result.ps_enabled)
                 progress_("GAMEPAD_SERVER_CONFIG_OFF_FORCED",0,0);
             set_status("GAME_AUTH_OK");
         },
         .auth_rejected=[this](int code){progress_("GAME_AUTH_REJECTED",code,0);},
         .connect_attempt=[this](unsigned attempt,const opennow::websocket::ConnectDiagnostics& diagnostic) {
             progress_("SIGNALING_ENDPOINT_ATTEMPT",static_cast<int>(attempt),0);
             progress_("SIGNALING_ENDPOINT_PORT",diagnostic.endpoint_port,0);
             progress_(diagnostic.event(),diagnostic.curl_code,diagnostic.http_status);
             progress_("SIGNALING_CONNECT_ELAPSED_MS",diagnostic.elapsed_ms,0);
         },
         .candidate_event=[this,&entry_probe_armed,&entry_probe_due,&entry_probe_stopped,entry_click_enabled](const char* event,int code){
             const std::string_view tag(event);
             if(tag=="GAME_SDK_WILL_ENTER_GAME" || tag=="GAME_SDK_ENTER_GAME") launcher_entered_=true;
             if(autotest_pad_enabled_ && (tag=="GAME_SDK_WILL_ENTER_GAME" || tag=="GAME_SDK_ENTER_GAME")) {
                 std::int64_t empty=0;
                 autotest_game_enter_ms_.compare_exchange_strong(empty,
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count());
             }
             // The game finishes the SDK login long before its title bar is
             // interactive (0.2.37: login at +29s, clickable bar at +64s), so
             // the bounded sequence is armed on the stream clock below and
             // re-arms its brightness baseline while the title scene fades in.
             if(entry_click_enabled) {
                 if(tag=="GAME_SDK_WILL_ENTER_GAME" || tag=="GAME_SDK_ENTER_GAME")
                     entry_probe_stopped=true;
             }
             if(tag=="RTC_CONTROL_WAITING_ASSOCIATION" || tag=="RTC_CONTROL_WAITING_CHANNEL" || tag=="RTC_CONTROL_READY" ||
                tag=="RTC_CONTROL_RECOVERING" ||
                tag=="CONTROL_WSS_CONNECTING" || tag=="CONTROL_WSS_WAITING_SYNC" || tag=="CONTROL_WSS_READY")
                 set_status(event);
             else progress_(event,code,0);
         }
        },std::move(alternatives),rtc::DispatchRoutes(params),std::chrono::steady_clock::now,rtc::WebControlRoutes(params),
        std::move(bridge));
    // The PS experiment is retired. Ignore old mode/pad files without deleting
    // them; every game uses the proven Xbox wire identity and original terminal.
    constexpr auto pad_vendor_id=input::kXboxVendorId,pad_product_id=input::kXboxProductId;
    session.set_pad_identity(pad_vendor_id,pad_product_id);
    progress_("GAMEPAD_IDENTITY",static_cast<int>(pad_vendor_id),static_cast<int>(pad_product_id));
    // Separate typed counters: product IDs are not HTTP statuses (e.g. 3302),
    // and strict safe-log monitors deliberately reject four-digit HTTP fields.
    progress_("GAMEPAD_VENDOR_ID",static_cast<int>(pad_vendor_id),0);
    progress_("GAMEPAD_PRODUCT_ID",static_cast<int>(pad_product_id),0);
    set_status("SIGNALING_CONNECTING");
    if (!session.start(&cancelled_)) {
        set_status(cancelled_ ? "STREAM_STOPPED_BY_USER" : session.last_error_event());return;
    }
    set_status("SIGNALING_CONNECTED_GAME_AUTH_REQUEST_SENT");
    const auto started=std::chrono::steady_clock::now();
    auto next_input=started, next_snapshot=started;
    const auto report_scheduling=[this,&session] {
        const auto emit=[this](const char* name,std::uint64_t value) {
            progress_(name,static_cast<int>(std::min<std::uint64_t>(value,INT_MAX)),0);
        };
        emit("RTC_SIGNAL_POLL_MAX_US",session.signal_poll_peak_us());
        emit("RTC_PEER_DRAIN_MAX_US",session.peer_drain_peak_us());
        emit("RTC_SESSION_TASKS_MAX_US",session.session_tasks_peak_us());
        emit("RTC_RECEIVE_BUDGET_HITS",session.receive_budget_hits());
        emit("RTC_PEER_BATCH_PACKETS_MAX",session.receive_batch_maximum());
    };
    opennow::video::KeyframeRequestPolicy keyframe_policy;
    bool first_frame=false, input_seen=false, touch_seen=false, answer_seen=false, channel_seen=false;
    bool video_seen=false,audio_seen=false;
    bool cursor_initialized=false,keyframe_after_settings=false,was_stalled=false;
    input::GyroStickAssist gyro_assist;
    DeviceInfoSync device_info;
    MediaStartupProbe media_startup;
    std::uint64_t gyro_assist_sent=0;
    std::uint64_t gyro_zl_ticks=0,gyro_composed=0,gyro_send_failures=0,wheel_input_ticks=0;
    int gyro_last_x=0,gyro_last_y=0;
    bool pad_mode_warned=false;
    std::chrono::steady_clock::time_point pad_mode_deadline{};
    bool gamepad_list_announced=false;
    bool input_ready_prev=false;
    bool input_source_declared_=false;
    std::chrono::steady_clock::time_point next_gamepad_list_attempt{};
    input::ControllerNeutralHandoff controller_handoff;
    auto settings_keyframe_at=started;
    overlay::SettingSendBudget graphics_budget,fps_budget;
    AdaptiveStream adaptive_stream;
    auto next_adaptive_sample=started;
    bool settings_were_ready=false;
    VideoProgressWatchdog video_progress;
    auto last_peer_poll=started;
    while (!cancelled_ && session.is_running()) {
        const auto poll_at=std::chrono::steady_clock::now();
        peer_poll_gaps_.observe(std::chrono::duration_cast<std::chrono::microseconds>(poll_at-last_peer_poll).count());
        last_peer_poll=poll_at;
        session.poll();
        // Official setGraphicsMode/setFps applies in Connected, not immediately
        // after game auth while transport/initial server config is still pending.
        const bool settings_ready=media_.decoded()>0 && session.input_ready();
        if(settings_ready && !settings_were_ready) {
            graphics_request_.retry();fps_request_.retry();
        }
        settings_were_ready=settings_ready;settings_ready_=settings_ready;
        control_ready_=session.input_ready();
        const auto now=std::chrono::steady_clock::now();
        // Effective input route: the forced virtual-pad transport. The
        // official CaGameConfig flags gate only the client's local pad reader,
        // so the pod is asked for a virtual pad regardless.
        const bool gamepad_transport=session.gamepad_enabled();
        const bool continue_via_gamepad=input::ContinueViaGamepad(gamepad_transport,launcher_entered_);
        if (!answer_seen && session.has_answered()) {answer_seen=true;set_status("RTC_ANSWER_SENT_WAITING_CONNECTION");}
        const bool input_ready_now=session.input_ready();
        if(input_ready_now && !input_ready_prev) {
            // First open and every later recovery need a fresh list: the pod's
            // virtual pad is announced per input channel lifetime. The input
            // source declaration is per-channel too, so it must be re-sent as
            // well; otherwise a recovered channel keeps the stale "declared"
            // flag and the pod never learns the source again.
            gamepad_list_announced=false;
            input_source_declared_=false;
            controller_handoff.reset();
            // The device/screen record and the initial cursor placement are also
            // per input-channel state on the pod. Without re-sending the record a
            // recovered session runs with stale geometry, and without re-centring
            // the first mouse move after recovery can jump from a stale origin.
            device_info.invalidate();
            cursor_initialized=false;
            // A synthetic entry press/tap that was in flight when the channel
            // died can never be released on the old channel. Clearing the hold
            // flags avoids a permanent "A held" frame after recovery; the
            // bounded entry sequence presses again on its own schedule.
            auto_entry_pad_hold=false;
            auto_entry_tap_held=false;
            pad_entry_release_pending=false;
            entry_tap_phase=0;
            next_gamepad_list_attempt=now;
        }
        input_ready_prev=input_ready_now;
        // GCC can finish syncing before DTLS/media. Announce the controller
        // only once the input send gate is actually open, not prematurely.
        if (!channel_seen && input_ready_now) {
            channel_seen=true;
            if(!rtc::UsesWebControl(session.web_control_channel_type())) progress_("RTC_REMOTE_DATA_CHANNEL_OPEN",0,0);
            if (gamepad_transport) progress_("GAMEPAD_ONLY_MODE_ACTIVE",0,0);
        }
        // Announce the Xbox 360 pad so the pod can create its virtual XInput
        // device. The official client gates this locally on CaGameConfig, but
        // no client-to-pod message carries those flags; whether the pod honours
        // the list is recorded by the GAMEPAD_* events below. Retry until the
        // send really succeeds: a one-shot flag on the first input_ready()
        // iteration used to drop the announcement forever if that SCTP write
        // failed.
        if (gamepad_transport && !gamepad_list_announced && input_ready_now &&
            now>=next_gamepad_list_attempt) {
            if(session.send_gamepad_list(session.pad_vendor_id(),session.pad_product_id())) {
                gamepad_list_announced=true;
                progress_("GAMEPAD_LIST_ANNOUNCED",
                    static_cast<int>(session.pad_vendor_id()),
                    static_cast<int>(session.pad_product_id()));
            } else {
                next_gamepad_list_attempt=now+std::chrono::milliseconds(500);
            }
        }
        // Once the input channel is open, declare which device owns input. The
        // official client sends the type-31 resetGameInputControl frame when its
        // UI takes over; without any such declaration the pod can keep routing
        // input to a device that never speaks, which matches the observed
        // "frames sent successfully but the title page never reacts".
        if (input_ready_now && !input_source_declared_) {
            input_source_declared_=true;
            const auto source=input::LauncherInputSource(gamepad_transport,launcher_touch_);
            if(session.reset_game_inputs(source))
                progress_("INPUT_SOURCE_DECLARED",static_cast<int>(source),0);
            else
                input_source_declared_=false; // retry next iteration
        }
        if (channel_seen && !pad_mode_warned && gamepad_transport && first_local_press_seen_ &&
            now>=pad_mode_deadline &&
            static_cast<std::uint32_t>(session.server_control_mode())!=cloudgenshin::input::kGameControlModeGamepad) {
            pad_mode_warned=true;
            progress_("GAMEPAD_SERVER_MODE_NOT_REPORTED_20S",0,0);
        }
        if (!video_seen && video_units_>0) {video_seen=true;set_status("FIRST_VIDEO_UNIT_RECEIVED");}
        if (!audio_seen && audio_packets_>0) {audio_seen=true;progress_("FIRST_AUDIO_PACKET_RECEIVED",0,0);}
        if (!first_frame && media_.decoded()>0) {first_frame=true;set_status("LIVE_VIDEO_DECODED");}
        if(!cursor_initialized && frame_geometry_ready_ && session.input_ready() && !gamepad_transport) {
            const double center_x=frame_width_.load()/2.0,center_y=frame_height_.load()/2.0;
            cursor_initialized=session.send_mouse_move(0,0,normalized_x(center_x),normalized_y(center_y));
            if(cursor_initialized) {
                mouse_x_=center_x;mouse_y_=center_y;mouse_position_known_=true;
                progress_("INPUT_CURSOR_CENTERED_NOT_CLICKED",0,0);
            }
        }
        // APK onAuthPassed sends 20017 before media connection. Waiting for
        // frame_geometry_ready_ withheld it forever in the Android no-AV run.
        // Start with the 1280x720 client display; update if decoded geometry
        // later differs. Signaling is sufficient, no input/video gate required.
        const auto startup_now=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        if(device_info.poll(startup_now,session.is_authenticated(),frame_width_.load(),frame_height_.load(),
            [&session](unsigned w,unsigned h){return session.send_device_info(w,h);})) {
            progress_("SDK_DEVICE_INFO_SENT",0,0);
            if(!frame_geometry_ready_) progress_("SDK_DEVICE_INFO_BEFORE_FIRST_FRAME",0,0);
        }
        if (now>=next_snapshot) {
            report_scheduling();
            input::MotionSample motion;
            input::ControllerMapping motion_mapping;
            bool native_navigation;
            {std::lock_guard lock(mutex_);motion=motion_input_;motion_mapping=controller_mapping_;
             native_navigation=native_navigation_;}
            const auto motion_now=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            progress_("MOTION_SERVER_REQUESTED",session.motion_requested()?1:0,0);
            progress_("MOTION_SAMPLE_FRESH",motion.valid && motion_now>=motion.sampled_ms && motion_now-motion.sampled_ms<=100 ? 1:0,0);
            progress_("MOTION_ORIENTATION_READY",motion.valid && motion.orientation && input::UnitQuaternion(*motion.orientation) &&
                motion_now>=motion.sampled_ms && motion_now-motion.sampled_ms<=100 ? 1:0,0);
            progress_("MOTION_ORIENTATION_REPORTS_SENT",static_cast<int>(std::min<std::uint64_t>(session.orientation_sent(),INT_MAX)),0);
            progress_("MOTION_REPORTS_SENT",static_cast<int>(std::min<std::uint64_t>(session.motion_sent(),INT_MAX)),0);
            progress_("MOTION_SEND_FAILURES",static_cast<int>(std::min<std::uint64_t>(session.motion_failures(),INT_MAX)),0);
            progress_("MOTION_FEEDBACK_RECEIVED",static_cast<int>(std::min<std::uint64_t>(session.motion_feedback_received(),INT_MAX)),0);
            progress_("MOTION_FEEDBACK_INVALID",static_cast<int>(std::min<std::uint64_t>(session.motion_feedback_invalid(),INT_MAX)),0);
            progress_("MOTION_FEEDBACK_BEFORE_AUTH",static_cast<int>(std::min<std::uint64_t>(session.motion_feedback_before_auth(),INT_MAX)),0);
            progress_("MOTION_ACTIVATION_RECEIVED",static_cast<int>(std::min<std::uint64_t>(session.motion_activation_received(),INT_MAX)),0);
            progress_("GYRO_STICK_ASSIST_FRAMES_SENT",static_cast<int>(std::min<std::uint64_t>(gyro_assist_sent,INT_MAX)),0);
            progress_("GYRO_LOCAL_ZL_TICKS",static_cast<int>(std::min<std::uint64_t>(gyro_zl_ticks,INT_MAX)),0);
            progress_("GYRO_LOCAL_COMPOSE_TICKS",static_cast<int>(std::min<std::uint64_t>(gyro_composed,INT_MAX)),0);
            progress_("GYRO_LOCAL_SEND_FAILURES",static_cast<int>(std::min<std::uint64_t>(gyro_send_failures,INT_MAX)),0);
            progress_("GYRO_LOCAL_STICK_X",gyro_last_x,0);
            progress_("GYRO_LOCAL_STICK_Y",gyro_last_y,0);
            progress_("GYRO_LOCAL_ENABLED",mapping_profile_=="hk4e" && motion_mapping.gyro_assist?1:0,0);
            progress_("GYRO_LOCAL_AIM_ONLY",motion_mapping.gyro_aim_only?1:0,0);
            progress_("GYRO_LOCAL_SENSITIVITY",static_cast<int>(motion_mapping.gyro_sensitivity),0);
            if(mapping_profile_=="hk4e") {
                progress_("GENSHIN_POSITION_PRESET_ACTIVE",input::HasGenshinPositionPreset(motion_mapping)?1:0,0);
                progress_("GENSHIN_NATIVE_NAVIGATION",native_navigation?1:0,0);
                progress_("GENSHIN_WHEEL_INPUT_TICKS",static_cast<int>(std::min<std::uint64_t>(wheel_input_ticks,INT_MAX)),0);
            }
            snapshot_counters();ReportGenshinVideoRtp(session,progress_);
            PeerVideoRtpStats rtp{};
            PeerTransportStats transport{};
            if(session.transport_stats(&transport)) {
                opennow::webrtc::ReportMediaIngress(transport,progress_);
                (void)session.video_rtp_stats(&rtp);
                if(const char* event=media_startup.update(motion_now,session.input_ready(),rtp.packets_received,
                    video_units_.load(),audio_packets_.load(),media_.decoded(),
                    {transport.rtp_completed,transport.rtp_decrypt_failures,
                     transport.rtp_payload_rejected,transport.rtp_ssrc_rejected})) set_status(event);
            }
            next_snapshot=now+std::chrono::seconds(5);
        }
        // Terminal 6 has produced its first decoded frame at 86 s on the same
        // service; 60 s aborted before the launcher was even visible. This is a
        // wait limit only, not a readiness claim.
        if (!first_frame && now-started>std::chrono::seconds(120)) {set_status("FIRST_VIDEO_TIMEOUT");break;}
        const auto picture_progress=video_progress.update(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
            video_units_,media_.decoded(),session.input_ready());
        const bool stalled=VideoProgressWatchdog::stalled(picture_progress.state);
        if(picture_progress.changed && stalled) {
            progress_(picture_progress.state==VideoProgressWatchdog::State::WaitingDecode
                ? "VIDEO_DECODE_STALLED_REQUESTING_KEYFRAME" : "VIDEO_PACKETS_STALLED_REQUESTING_KEYFRAME",0,0);
            was_stalled=true;
        } else if(picture_progress.changed && was_stalled && picture_progress.state==VideoProgressWatchdog::State::Healthy) {
            progress_("VIDEO_PROGRESS_RESUMED_NOT_GAME_LOADING_PROOF",0,0);was_stalled=false;
        }
        if(picture_progress.resync) {media_.request_resync();progress_("VIDEO_DECODE_RESYNC_REQUESTED",0,0);}
        if(session.has_answered()) {
            std::uint64_t recovery_generation=0;
            const bool recovery=media_.take_keyframe_request(&recovery_generation);
            const bool needed=!first_frame || stalled || recovery ||
                (keyframe_after_settings && now>=settings_keyframe_at);
            if(keyframe_policy.due(std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count(),needed,recovery_generation)) {
                if(session.request_keyframe()) {++keyframe_requests_;keyframe_after_settings=false;}
            }
        }
        if(settings_ready_) {
            const auto setting_now=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            if(const auto request=graphics_request_.pending();request && graphics_budget.try_write(*request,setting_now)) {
                const bool ok=session.set_graphics_mode(request->value());
                graphics_budget.sent(ok);
                if(graphics_budget.delivered() || graphics_budget.exhausted()) {
                    const bool delivered=graphics_budget.delivered();
                    graphics_request_.complete(*request,delivered);
                    progress_(delivered ? "STREAM_QUALITY_SENT" : "STREAM_QUALITY_SEND_FAILED",request->value(),0);
                }
                if(graphics_budget.delivered()) {
                    accepted_graphics_=request->value();
                    fps_request_.retry();
                    keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);
                    const auto range=StreamQualityBitrate(request->value());
                    adaptive_stream.configure(*range,fps_request_.snapshot().value(),setting_now);
                    smoothness_protection_=false;
                    progress_("STREAM_BITRATE_MIN_KBPS",range->minimum,0);
                    progress_("STREAM_BITRATE_MAX_KBPS",range->maximum,0);
                    PersistStreamSettings();
                }
            }
            if(const auto request=fps_request_.pending();request && fps_budget.try_write(*request,setting_now)) {
                const bool ok=session.set_fps(static_cast<std::uint32_t>(request->value()));
                fps_budget.sent(ok);
                if(ok) {media_.set_fps(request->value());active_fps_=request->value();}
                if(fps_budget.delivered() || fps_budget.exhausted()) {
                    const bool delivered=fps_budget.delivered();
                    fps_request_.complete(*request,delivered);
                    progress_(delivered ? "STREAM_FPS_SENT" : "STREAM_FPS_SEND_FAILED",request->value(),0);
                }
                if(fps_budget.delivered()) {
                    accepted_fps_=request->value();
                    adaptive_stream.manual_fps(request->value(),setting_now);
                    smoothness_protection_=adaptive_stream.protecting();
                    keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);
                    PersistStreamSettings();
                }
            }
        }
        const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        // Sample independently of opt-in diagnostics. Each setting revision is
        // completed first; manual choices and reconnects cancel stale auto work.
        if(settings_ready_ && graphics_request_.snapshot().state()==overlay::SettingRequest::State::Sent &&
           fps_request_.snapshot().state()==overlay::SettingRequest::State::Sent) {
            if(now>=next_adaptive_sample) {
                next_adaptive_sample=now+std::chrono::seconds(1);
                PeerVideoRtpStats stats{};
                if(session.video_rtp_stats(&stats)) adaptive_stream.observe(now_ms,
                    {stats.access_units_completed,stats.access_units_dropped,stats.reorder_timeout_skips,
                     stats.reorder_capacity_skips,media_.performance().congestion_events});
            }
            if(const auto change=adaptive_stream.write_due(now_ms)) {
                const bool fps=change->kind==AdaptiveStream::Kind::Fps;
                const bool accepted=fps ? session.set_fps(change->fps) :
                    session.set_bitrate_range(change->bitrate.minimum,change->bitrate.maximum);
                const bool completed=adaptive_stream.sent(now_ms,accepted);
                if(accepted) {
                    active_fps_=adaptive_stream.fps();media_.set_fps(active_fps_);
                    smoothness_protection_=adaptive_stream.protecting();
                }
                if(completed) {
                    progress_(fps ? "STREAM_AUTO_FPS_SENT" : "STREAM_AUTO_BITRATE_SENT",
                        fps ? change->fps : change->bitrate.maximum,0);
                    if(fps) {keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);}
                }
            }
        } else {adaptive_stream.suspend();next_adaptive_sample=now;}
        network_latency_.sample(now_ms,[&session]{return session.network_rtt_ms();});
        // A queued Continue (overlay row or automation) becomes one held
        // XInput A frame when the server enabled a virtual pad, and a normal
        // mouse click through the official fallback otherwise.
        // A press that no frame could carry must not wedge later requests.
        if(continue_via_gamepad) {
            if(confirm_report_pending && now>=confirm_until) {
                confirm_report_pending=false;
                if(confirm_source==kConfirmContinue) progress_("INPUT_CONTINUE_SEND_FAILED",0,0);
            }
            if(now>=confirm_until && continue_requested_.exchange(false)) {
                confirm_until=now+std::chrono::milliseconds(120);
                confirm_report_pending=true;
                confirm_source=kConfirmContinue;
            }
        }
        if (now>=next_input && session.input_ready()) {
            input::SwitchControllerState state;
            input::MotionSample motion;
            input::ControllerMapping mapping;
            unsigned generation;
            bool native_navigation;
            {std::lock_guard lock(mutex_);state=input_;motion=motion_input_;mapping=controller_mapping_;
             generation=controller_input_generation_;native_navigation=native_navigation_;}
            // One-shot evidence that physical buttons are actually sampled and
            // reaching the transport decision; without this a "buttons do
            // nothing" report cannot tell sampling from delivery.
            if(!first_local_press_seen_ && input::HasGamepadActivity(input::ToGamepadState(state,0))) {
                first_local_press_seen_=true;
                // The server-mode watchdog starts from the first real press,
                // not from channel open: a player may take a minute to clear
                // the launcher before touching the pad.
                pad_mode_deadline=now+std::chrono::seconds(20);
                // Re-announce the virtual pad when the player first uses it.
                // If the pod's input adapter finished initialising after the
                // channel-open announcement, this gives it the device list at
                // the moment the pad is actually needed. Sending the same list
                // twice is idempotent for a real device manager.
                if(gamepad_transport &&
                   session.send_gamepad_list(session.pad_vendor_id(),session.pad_product_id())) {
                    gamepad_list_announced=true;
                    progress_("GAMEPAD_LIST_REANNOUNCED_ON_FIRST_PRESS",
                        static_cast<int>(session.pad_vendor_id()),
                        static_cast<int>(session.pad_product_id()));
                }
                progress_("INPUT_LOCAL_PRESS_SAMPLED",gamepad_transport ? 1 : 0,0);
            }
            // One title-entry tap per physical face-button edge, only before
            // the SDK enter transition. After entry, native pad frames remain
            // and this shortcut must not inject gameplay pointer events.
            const bool entry_button_held=state.a||state.b;
            if(input::FaceButtonPointerShortcut(launcher_entered_) &&
               entry_button_held && !pad_entry_button_was_held &&
               !pad_entry_release_pending && !auto_entry_tap_held && media_.decoded()>0) {
                input::TouchEvent down;
                down.finger_id=0;down.action=input::kTouchActionDown;
                down.x=0.5;down.y=0.947;down.primary=true;
                if(send_launcher_pointer(session,true,down.x,down.y)) {
                    pad_entry_release_pending=true;
                    pad_entry_release_due=now+std::chrono::milliseconds(120);
                    progress_("PAD_A_ENTRY_TAP_SENT",0,0);
                }
            }
            pad_entry_button_was_held=entry_button_held;
            if(pad_entry_release_pending && now>=pad_entry_release_due) {
                input::TouchEvent up;
                up.finger_id=0;up.action=input::kTouchActionUp;
                up.x=0.5;up.y=0.947;up.primary=true;
                if(send_launcher_pointer(session,false,up.x,up.y)) {
                    pad_entry_release_pending=false;
                } else {
                    // Never drop an Up silently: a lost release leaves the
                    // pod holding a contact. Retry until the channel takes it.
                    pad_entry_release_due=now+std::chrono::milliseconds(100);
                    if(!pad_entry_release_warned) {
                        pad_entry_release_warned=true;
                        progress_("PAD_A_ENTRY_RELEASE_RETRY",0,0);
                    }
                }
            }
            const bool physical_zl=state.zl; // Physical ZL, independent of remapping to game LT.
            const auto mapped=input::ComposeGameplayMapping(mapping_profile_,mapping,state,native_navigation);
            state=mapped.state;
            if(mapped.wheel_navigation) ++wheel_input_ticks;
            if(gamepad_transport) {
                const bool confirm_held=now<confirm_until;
                const bool handoff=controller_handoff.pending(generation);
                // Synthetic A (Continue / auto entry) must survive the next
                // physical frame, so the hold flag is part of the composition
                // rather than a single one-off send.
                if(mapping_profile_=="hk4e" && physical_zl) ++gyro_zl_ticks;
                const bool synthetic=confirm_held || auto_entry_pad_hold || entry_tap_phase==2;
                state=gyro_assist.apply(mapping,state,motion,now_ms,
                    !handoff && !synthetic && mapped.gyro_allowed && media_.decoded()>0,physical_zl);
                if(gyro_assist.active()) {++gyro_composed;gyro_last_x=state.right_x;gyro_last_y=state.right_y;}
                else gyro_last_x=gyro_last_y=0;
                state=input::ComposeGamepadFrame(state,
                    confirm_held,auto_entry_pad_hold || entry_tap_phase==2);
                if(handoff) state={}; // Deliver neutral before the new source can press.
                const bool sent=session.send_gamepad(state,now_ms);
                if(sent && !handoff && gyro_assist.active()) ++gyro_assist_sent;
                if(!sent && !handoff && gyro_assist.active()) ++gyro_send_failures;
                if(handoff) controller_handoff.complete(generation,sent);
                if (sent && !input_seen) {
                    input_seen=true;progress_("GAMEPAD_CHANNEL_SEND_OK_NOT_DELIVERY_PROOF",0,0);
                }
                if (sent && !handoff && confirm_held && confirm_report_pending) {
                    confirm_report_pending=false;
                    progress_(confirm_source==kConfirmEntry ? "AUTOTEST_ENTRY_CLICK_SENT"
                                                            : "INPUT_WAKE_GAMEPAD_SENT",0,0);
                }
            } else {
                // Same policy as the official web client: without a virtual
                // pad the keyboard/mouse emulation is the only input path.
                if(!gamepad_blocked_reported_) {
                    gamepad_blocked_reported_=true;
                    progress_("GAMEPAD_INPUT_BLOCKED_BY_SERVER_CONFIG",0,0);
                }
                send_keyboard_mouse(session,state);
            }
            motion_ui_status_=!mapping.gyro_assist ? (mapping_profile_=="hk4e"?5:(session.motion_requested()?1:0)) :
                !mapped.gyro_allowed ? 6 :
                !gamepad_transport || !motion.valid || now_ms<motion.sampled_ms || now_ms-motion.sampled_ms>100 ? 4 :
                gyro_assist.active()?3:2;
            if(mapping_profile_!="hk4e" && !mapping.gyro_assist && (!gamepad_transport || !controller_handoff.pending(generation)))
                session.send_motion(motion,now_ms);
            // Touch always rides the official TouchController stream: the pod's
            // launcher/loading page only accepts pointer input, so this is what
            // advances past the title screen even though gameplay uses the pad.
            std::vector<input::TouchEvent> touch_events;
            {std::lock_guard lock(mutex_);touch_events.swap(pending_touch_);}
            if(!touch_events.empty()) {
                // The wire list is limited to 10 events; a retried batch plus
                // newer state can exceed that, so send ordered chunks instead
                // of failing the whole batch forever.
                std::size_t sent_events=0;
                while(sent_events<touch_events.size()) {
                    const std::size_t count=std::min<std::size_t>(
                        input::kTouchEventListLimit,touch_events.size()-sent_events);
                    const std::vector<input::TouchEvent> chunk(
                        touch_events.begin()+static_cast<std::ptrdiff_t>(sent_events),
                        touch_events.begin()+static_cast<std::ptrdiff_t>(sent_events+count));
                    if(!session.send_touch(chunk)) break;
                    sent_events+=count;
                }
                if(sent_events>0) {
                    if(!touch_seen) {
                        touch_seen=true;
                        progress_("TOUCH_CHANNEL_SEND_OK_NOT_DELIVERY_PROOF",0,0);
                    }
                    // Per-event evidence for the live touch test: the first
                    // event of each batch plus the scaled coordinates.
                    const auto& first=touch_events.front();
                    const int x_permille=static_cast<int>(first.x*1000.0);
                    const int y_permille=static_cast<int>(first.y*1000.0);
                    if(first.action==input::kTouchActionDown) {
                        progress_("TOUCH_SENT_DOWN",x_permille,y_permille);
                    } else if(first.action==input::kTouchActionUp) {
                        progress_("TOUCH_SENT_UP",x_permille,y_permille);
                    } else if(now>=touch_move_log_due) {
                        touch_move_log_due=now+std::chrono::milliseconds(500);
                        progress_("TOUCH_SENT_MOVE",x_permille,y_permille);
                    }
                }
                if(sent_events<touch_events.size()) {
                    // Put only the unsent tail back in front of newer state so
                    // an Up cannot be lost; the next 16 ms frame retries it.
                    std::lock_guard lock(mutex_);
                    pending_touch_.insert(pending_touch_.begin(),
                        touch_events.begin()+static_cast<std::ptrdiff_t>(sent_events),
                        touch_events.end());
                    if(pending_touch_.size()>64) pending_touch_.resize(64);
                }
            }
            next_input=now+std::chrono::milliseconds(16);
        }
        if(!continue_via_gamepad) process_continue_click(session,now_ms);
        // Match manual Continue: a virtual pad existing does not make the
        // title page accept XInput A. Both titles use pointer entry until
        // the game's SDK enter transition, without changing gameplay mapping.
        if(auto_entry_enabled && !entry_click_enabled &&
           (!auto_entry_stopped || auto_entry_pad_hold || auto_entry_tap_held)) {
            // A synthetic press that is still held must be released even when the
            // stop flag was set in the same iteration (the game can report its
            // enter transition while A is down). Gating the whole block on
            // !auto_entry_stopped used to skip the release and leave A pressed
            // for the rest of the session.
            if(launcher_entered_ && !auto_entry_tap_held && !auto_entry_pad_hold) {
                auto_entry_stopped=true;
                progress_("AUTO_ENTRY_GAME_ENTERED_STOPPED",0,0);
            }
            const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            if(!auto_entry_started) {
                if(!auto_entry_stopped && first_frame && now>=started+std::chrono::seconds(55)) {
                    auto_entry_started=true;
                    auto_entry_due=now;
                    progress_("AUTO_ENTRY_ARMED",0,0);
                }
            } else if(auto_entry_pad_hold) {
                if(now>=auto_entry_release_due) {
                    // Release A with a neutral gamepad frame.
                    if(gamepad_transport && session.send_gamepad({},now_ms)) {
                        auto_entry_pad_hold=false;
                        ++auto_entry_taps;
                        auto_entry_last_tap_ms=now_ms;
                        auto_entry_need_sample=true;
                        auto_entry_due=now+std::chrono::milliseconds(2500);
                        progress_("AUTO_ENTRY_PAD_TAP_SENT",auto_entry_taps,0);
                    } else {
                        auto_entry_release_due=now+std::chrono::milliseconds(100);
                    }
                }
            } else if(auto_entry_tap_held) {
                if(now>=auto_entry_release_due) {
                    input::TouchEvent up;
                    up.finger_id=0;up.action=input::kTouchActionUp;
                    up.x=0.5;up.y=0.947;up.primary=true;
                    if(send_launcher_pointer(session,false,up.x,up.y)) {
                        auto_entry_tap_held=false;
                        ++auto_entry_taps;
                        auto_entry_last_tap_ms=now_ms;
                        auto_entry_need_sample=true;
                        auto_entry_due=now+std::chrono::milliseconds(2500);
                        progress_("AUTO_ENTRY_TAP_SENT",auto_entry_taps,0);
                    } else {
                        auto_entry_release_due=now+std::chrono::milliseconds(100);
                        if(!auto_entry_release_warned) {
                            auto_entry_release_warned=true;
                            progress_("AUTO_ENTRY_RELEASE_RETRY",0,0);
                        }
                    }
                }
            } else if(touch_seen || first_local_press_seen_) {
                auto_entry_stopped=true;
                progress_("AUTO_ENTRY_STOPPED_USER_INPUT",0,0);
            } else if(auto_entry_taps>=kAutoEntryMaxTaps) {
                auto_entry_stopped=true;
                progress_("AUTO_ENTRY_ATTEMPTS_DONE",auto_entry_taps,0);
            } else if(!auto_entry_stopped && now>=auto_entry_due) {
                const auto sample_ms=last_luma_sample_ms_.load();
                const bool fresh_luma=sample_ms>0 && now_ms-sample_ms<=7000;
                if(auto_entry_need_sample) {
                    if(sample_ms>=auto_entry_last_tap_ms+1000) {
                        auto_entry_need_sample=false;
                        const int luma=last_luma_mean_.load();
                        if(std::abs(luma-auto_entry_baseline)>=15) {
                            auto_entry_stopped=true;
                            progress_("AUTO_ENTRY_SCENE_CHANGED",luma,0);
                        }
                    } else if(now_ms-auto_entry_last_tap_ms<6000) {
                        // Wait for a sample taken after the previous tap;
                        // otherwise a successful entry could get a second tap.
                        auto_entry_due=now+std::chrono::milliseconds(500);
                    } else {
                        // The luma sampler stopped producing; do not stall.
                        auto_entry_need_sample=false;
                    }
                }
                if(!auto_entry_stopped && !auto_entry_need_sample) {
                    if(!auto_entry_baseline_ready) {
                        if(!fresh_luma || last_luma_mean_.load()<16) {
                            // Wait for a non-black title/loading frame before
                            // recording the baseline.
                            auto_entry_due=now+std::chrono::milliseconds(500);
                        } else {
                            auto_entry_baseline=last_luma_mean_.load();
                            auto_entry_baseline_ready=true;
                            progress_("AUTO_ENTRY_BASELINE_LUMA",auto_entry_baseline,0);
                        }
                    }
                    if(!auto_entry_stopped && auto_entry_baseline_ready && media_.decoded()>0) {
                        if(continue_via_gamepad) {
                            // Same game-specific route as manual Continue.
                            input::SwitchControllerState press{};
                            press.b=true; // XInput A = physical B
                            if(session.send_gamepad(press,now_ms)) {
                                auto_entry_pad_hold=true;
                                auto_entry_release_due=now+std::chrono::milliseconds(120);
                                progress_("AUTO_ENTRY_PAD_PRESS",0,0);
                            } else {
                                auto_entry_due=now+std::chrono::milliseconds(500);
                            }
                        } else {
                            input::TouchEvent down;
                            down.finger_id=0;down.action=input::kTouchActionDown;
                            down.x=0.5;down.y=0.947;down.primary=true;
                            if(send_launcher_pointer(session,true,down.x,down.y)) {
                                auto_entry_tap_held=true;
                                auto_entry_release_due=now+std::chrono::milliseconds(120);
                            } else {
                                auto_entry_due=now+std::chrono::milliseconds(500);
                            }
                        }
                    }
                }
            }
        }
        // Opt-in autotest entry sequence: marker-gated, bounded to four
        // confirms. It uses the same transport policy as continuous input
        // (gamepad A when a virtual pad exists, a click on the title page's
        // "点击进入" bar otherwise), stops on the game's own enter transition,
        // a real scene change, or the fixed probe count.
        if(entry_click_enabled && !entry_probe_armed && now>=started+std::chrono::seconds(55)) {
            entry_probe_armed=true;
            entry_probe_due=now;
            progress_("AUTOTEST_ENTRY_TIMED_ARM",0,0);
        }
        if(entry_click_enabled && entry_probe_armed && (!entry_probe_stopped || entry_tap_phase!=0) && now>=entry_probe_due) {
            const auto sample_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            const bool fresh_luma=sample_ms-last_luma_sample_ms_.load()<=7000;
            if(fresh_luma) {
                const int luma=last_luma_mean_.load();
                if(!entry_luma_baseline_ready) {
                    progress_("AUTOTEST_ENTRY_BASELINE_LUMA",luma,0);
                    entry_luma_baseline_ready=true;
                    entry_luma_baseline=luma;
                } else if(std::abs(luma-entry_luma_baseline)>=15) {
                    entry_probe_stopped=true;
                    progress_("AUTOTEST_ENTRY_SCENE_CHANGED",0,0);
                }
            }
            if(entry_probe_stopped && entry_tap_phase==0) {
                // Scene already changed; nothing left to send.
            } else if(entry_action_index>=kEntryProbeCount) {
                entry_probe_stopped=true;
                progress_("AUTOTEST_ENTRY_ACTIONS_FINISHED",kEntryProbeCount,0);
            } else if(entry_tap_phase==1) {
                // Release the contact 120 ms later so the pod sees a tap.
                if(now>=entry_tap_due) {
                    input::TouchEvent up;
                    up.finger_id=0;up.action=input::kTouchActionUp;
                    up.x=entry_tap_x;up.y=entry_tap_y;up.primary=true;
                    if(send_launcher_pointer(session,false,up.x,up.y)) {
                        entry_tap_phase=0;
                        ++entry_action_index;
                        entry_probe_due=now+std::chrono::seconds(3);
                        progress_("AUTOTEST_ENTRY_CLICK_SENT",entry_action_index,0);
                    } else {
                        entry_tap_due=now+std::chrono::milliseconds(100);
                        entry_probe_due=entry_tap_due;
                    }
                } else {
                    entry_probe_due=entry_tap_due;
                }
            } else if(entry_tap_phase==2) {
                // Release the synthetic A after the same 120 ms hold used by the
                // default auto-entry path.
                if(now>=entry_tap_due) {
                    if(session.send_gamepad({},now_ms)) {
                        entry_tap_phase=0;
                        ++entry_action_index;
                        entry_probe_due=now+std::chrono::seconds(3);
                        progress_("AUTOTEST_ENTRY_PAD_SENT",entry_action_index,0);
                    } else {
                        entry_tap_due=now+std::chrono::milliseconds(100);
                        entry_probe_due=entry_tap_due;
                    }
                } else {
                    entry_probe_due=entry_tap_due;
                }
            } else if(media_.decoded()>0) {
                if(continue_via_gamepad) {
                    input::SwitchControllerState press{};
                    press.b=true; // XInput A = physical B.
                    if(session.send_gamepad(press,now_ms)) {
                        entry_tap_phase=2;
                        entry_tap_due=now+std::chrono::milliseconds(120);
                        entry_probe_due=entry_tap_due;
                    } else {
                        entry_probe_due=now+std::chrono::seconds(1);
                    }
                } else {
                    // Pointer-only launcher: the title page's "点击进入" bar sits
                    // at ~94.7% height.
                    input::TouchEvent down;
                    down.finger_id=0;down.action=input::kTouchActionDown;
                    down.x=0.5;down.y=0.947;down.primary=true;
                    if(send_launcher_pointer(session,true,down.x,down.y)) {
                        entry_tap_x=down.x;entry_tap_y=down.y;
                        entry_tap_phase=1;
                        entry_tap_due=now+std::chrono::milliseconds(120);
                        entry_probe_due=entry_tap_due;
                    } else {
                        entry_probe_due=now+std::chrono::seconds(1);
                    }
                }
            } else {
                entry_probe_due=now+std::chrono::milliseconds(500);
            }
        }
        if(session.receive_backlog_hint()) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    report_scheduling();
    settings_ready_=false;control_ready_=false;
    if (session_end_requested_ || session.session_end_received()) {
        // Server 20005 / SDK logout: not a transport fault.
        set_status("GAME_SDK_SESSION_ENDED");
    } else if (!session.last_error().empty()) {
        // Distinguish an explicit game end from a recoverable transport error.
        const auto err = session.last_error();
        if (err == "StopGameRsp received") {
            set_status("GAME_SDK_SESSION_ENDED");
        } else if (err.find("SIGNALING")!=std::string::npos || err.find("disconnect")!=std::string::npos) {
            // Signaling dropped or remote endpoint closed: tell the user the
            // session can be retried instead of showing a generic failure.
            set_status(err == "SIGNALING_DISCONNECTED" ? "RTC_SIGNALING_DISCONNECTED_RETRY" : "RTC_CONNECTION_LOST_RETRY");
        } else if (err == "CONTROL_CHANNEL_TIMEOUT" || err == "CONTROL_WSS_PONG_TIMEOUT") {
            // Control channel timeout while media may still be alive must not
            // read as a total disconnect.
            set_status("CONTROL_CHANNEL_TIMEOUT_RETRY");
        } else {
            set_status(session.last_error_event());
        }
        // draw() observes finished_ and opens diagnostics on the UI thread.
        // Never mutate Borealis visibility/focus from this network worker.
    } else if (cancelled_) {
        set_status("STREAM_STOPPED_BY_USER");
    }
    send_keyboard_mouse(session,{}); // Best-effort release, without claiming delivery.
    if(session.gamepad_enabled()) session.send_gamepad({},0);
    {std::vector<input::TouchEvent> touch_events;
     {std::lock_guard lock(mutex_);touch_events.swap(pending_touch_);}
     if(!touch_events.empty()) session.send_touch(touch_events);}
    PeerTransportStats final_transport{};
    if(session.transport_stats(&final_transport)) opennow::webrtc::ReportMediaIngress(final_transport,progress_);
    ReportGenshinVideoRtp(session,progress_);
    network_latency_.clear();
    session.stop();
}
void CloudGenshinStreamView::draw(NVGcontext* vg,float x,float y,float width,float height,
                                brls::Style style,brls::FrameContext* context) {
    presentation_debug_.beginDraw();
    const auto now=std::chrono::steady_clock::now();
    if(automated_seconds_ && !exit_requested_ && now>=automatic_stop_check_) {
        automatic_stop_check_=now+std::chrono::seconds(1);
        if(std::ifstream(opennow::AppHomePath()+"/cloudgenshin-autotest.stop").good()) {
            progress_("AUTOTEST_STOP_REQUESTED",0,0);request_exit();
        }
    }
    if(automated_seconds_ && !exit_requested_ && now-created_>=std::chrono::seconds(automated_seconds_)) {
        progress_("AUTOTEST_STREAM_LIMIT_REACHED",automated_seconds_,0);request_exit();
    }
    if(automated_seconds_ && finished_ && !automated_reported_) {
        automated_reported_=true;snapshot_counters();
        progress_("AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF",0,0);
        request_exit();
    }
    auto* input_manager=brls::Application::getPlatform()->getInputManager();
    const auto sampled=controller_sampler_.sample(*input_manager);
    if(sampled.changed) {
        mapping_editor_.suspend_input();
        arm_input_release();
        exit_hold_={};controller_system_buttons_.reset();
        diagnostics_chord_={};diagnostics_chord_captured_=false;
        progress_(sampled.source==0?"CONTROLLER_SOURCE_HANDHELD_JOYCON":
            sampled.source<0?"CONTROLLER_SOURCE_DISCONNECTED":"CONTROLLER_SOURCE_PAIRED_OR_PRO",
            sampled.source,0);
    }
    const bool plus=sampled.state.plus,minus=sampled.state.minus;
    const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto shortcut=diagnostics_chord_.update(sampled.state,now_ms,
        !exit_requested_ && !finished_ && !mapping_editor_.is_open());
    if(diagnostics_chord_captured_!=shortcut.capture) arm_input_release();
    diagnostics_chord_captured_=shortcut.capture;
    if(shortcut.open) show_diagnostics(!diagnostics_visible_);
    auto state=sampled.state;
    controller_system_buttons_.observe(state);
    if(autotest_pad_enabled_ && !cloudgenshin::input::ControlsReleased(state)) {
        autotest_pad_enabled_=false;
        progress_("AUTOTEST_PAD_STOPPED_USER_INPUT",0,0);
    }
    if(autotest_pad_enabled_) {
        autotest_pad_probe_.arm(autotest_game_enter_ms_.load());
        const auto probe=autotest_pad_probe_.sample(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
        state=probe.state;
        if(probe.phase!=autotest_pad_phase_) {
            autotest_pad_phase_=probe.phase;
            next_frame_sample_at_={};
            progress_("AUTOTEST_PAD_PROBE_PHASE",probe.phase,0);
        }
    }
    const auto raw_state=state;
    std::vector<brls::RawTouchState> touches;
    brls::Application::getPlatform()->getInputManager()->updateTouchStates(&touches);
    const auto finger=std::find_if(touches.begin(),touches.end(),[](const auto& t){return t.pressed;});
    const bool touch_down=finger!=touches.end();
    if(autotest_ui_enabled_) {
        if(touch_down || !cloudgenshin::input::ControlsReleased(raw_state)) {
            autotest_ui_enabled_=false;
            progress_("AUTOTEST_UI_STOPPED_USER_INPUT",0,0);
        } else if(first_render_ && !finished_ && !exit_requested_) {
            const auto phase=cloudgenshin::AutoTestUiProbePhase(
                std::chrono::duration_cast<std::chrono::milliseconds>(now-created_).count());
            if(phase!=autotest_ui_phase_) {
                autotest_ui_phase_=phase;stats_visible_=true;
                show_quality_menu((phase%2)!=0);
                progress_("AUTOTEST_UI_PROBE_PHASE",phase,0);
            }
        }
    }
    update_overlay_input(raw_state,{x,y,width,height},touch_down,
        touch_down ? finger->fingerId : 0,touch_down ? cloudgenshin::overlay::Point{finger->position.x,finger->position.y}
                                                    : cloudgenshin::overlay::Point{});
    // Game touches: the overlay consumes at most the first pressed finger for
    // the floating ball / menu, so every contact it does not own goes to the
    // official RtcTouchEvent stream. The pod's launcher and loading page only
    // accept pointer input (CaGameConfig reports no virtual pad), so this is
    // what gets the session past the title/continue screen; the gamepad frames
    // then drive gameplay. While a UI panel owns the screen all contacts are
    // released so the game is not left holding one.
    {
        const bool ui_owns_screen=diagnostics_visible_ || quality_menu_open_ ||
                                  exit_requested_ || quality_chord_captured_ || diagnostics_chord_captured_;
        std::vector<cloudgenshin::touch::Contact> contacts;
        if(!ui_owns_screen && width>0.0f && height>0.0f) {
            for(const auto& touch : touches) {
                if(!touch.pressed) continue;
                if(floating_ball_.captured() && touch_down &&
                   touch.fingerId==finger->fingerId) continue;
                contacts.push_back({static_cast<std::uint32_t>(touch.fingerId),
                    static_cast<double>(std::clamp((touch.position.x-x)/width,0.0f,1.0f)),
                    static_cast<double>(std::clamp((touch.position.y-y)/height,0.0f,1.0f))});
            }
        }
        const auto touch_events=ui_owns_screen ? touch_tracker_.release_all()
                                               : touch_tracker_.update(contacts);
        if(!touch_events.empty()) {
            std::lock_guard lock(mutex_);
            pending_touch_.insert(pending_touch_.end(),touch_events.begin(),touch_events.end());
            if(pending_touch_.size()>64)
                pending_touch_.erase(pending_touch_.begin(),pending_touch_.end()-64);
        }
    }
    if(diagnostics_visible_ || quality_menu_open_ || exit_requested_ || floating_ball_.captured() ||
       quality_chord_captured_ || diagnostics_chord_captured_) state={};
    // Release gate: withhold gameplay input until the physical controls are
    // neutral once. It deliberately ignores touch contacts; the old version
    // required "no touch down" and a stale contact muted every button press.
    const bool motion_release_suppressed=release_gate_.suppress(raw_state,
           std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch()).count());
    if(motion_release_suppressed) state={};
    input_release_pending_=release_gate_.pending();
    controller_system_buttons_.filter(state);
    auto motion=sampled.motion;
    if(motion_release_suppressed || diagnostics_visible_ || quality_menu_open_ || exit_requested_ ||
       finished_ || floating_ball_.captured() || quality_chord_captured_ || diagnostics_chord_captured_ || autotest_pad_enabled_) motion.valid=false;
    {std::lock_guard lock(mutex_);input_=state;motion_input_=motion;}
    if (exit_hold_.update(plus,minus,now_ms)) request_exit();
    if(exit_requested_ && finished_ && !pop_requested_) request_activity_pop();
    presentation_debug_.videoBegin();
    bool got=false;
    AVFrameHolder::instance().get_paced([&](AVFrame* frame,std::uint64_t generation,bool){
        frame_width_=frame->width;frame_height_=frame->height;
        frame_geometry_ready_=true;
        if (!frame_metadata_seen_) {
            frame_metadata_seen_=true;
            progress_("VIDEO_FRAME_WIDTH",frame->width,0);
            progress_("VIDEO_FRAME_HEIGHT",frame->height,0);
            progress_("VIDEO_FRAME_PIXEL_FORMAT",frame->format,0);
        }
        if (frame && now>=next_frame_sample_at_) {
            next_frame_sample_at_=now+std::chrono::seconds(5);
            sample_frame_content(frame);
        }
        if(renderer_) {
            const auto before=renderer_->video_render_stats()->rendered_frames;
            const auto render_started=std::chrono::steady_clock::now();
            renderer_->drawLatest(vg,static_cast<int>(width),static_cast<int>(height),frame,VIDEO_FORMAT_H264,generation);
            render_cpu_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-render_started).count());
            got=renderer_->video_render_stats()->rendered_frames!=before;
            if (got) {
                ++render_submissions_;
                const auto submission=frame_submissions_.observe(generation,renderer_->video_render_stats()->submitted_generation);
                if(submission.fresh) ++new_render_submissions_;else ++reused_render_submissions_;
                if(submission.deferred) ++deferred_render_submissions_;
            }
        }
    },std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    presentation_debug_.observe(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(),renderer_ ? renderer_->video_render_stats() : nullptr);
    std::string status;
    {std::lock_guard lock(mutex_);status=status_;}
    status=ui::Text(status);
    if (!got) {
        nvgBeginPath(vg);nvgRect(vg,x,y,width,height);nvgFillColor(vg,nvgRGB(8,12,18));nvgFill(vg);
    }
    if (got && !first_render_) {
        first_render_=true;progress_("FIRST_FRAME_RENDER_SUBMITTED_NOT_DISPLAY_PROOF",0,0);
        if (!exit_requested_) show_diagnostics(false);
    }
    if (media_.decoded()>0 && !first_render_) {
        if (first_decoded_at_==std::chrono::steady_clock::time_point{}) {
            first_decoded_at_=now;
            progress_("VIDEO_VIEW_WIDTH",static_cast<int>(width),0);
            progress_("VIDEO_VIEW_HEIGHT",static_cast<int>(height),0);
        }
        if (!render_failure_reported_ && now-first_decoded_at_>std::chrono::seconds(5)) {
            render_failure_reported_=true;set_status("VIDEO_RENDER_NOT_SUBMITTED");
            snapshot_counters();show_diagnostics(true);
        }
    }
    if (finished_ && !finished_panel_shown_) {finished_panel_shown_=true;show_diagnostics(true);}
    if (diagnostics_visible_ && now>=diagnostics_refresh_) {
        if (status_label_->getFullText()!=status) status_label_->setText(status);
        const auto log=ui::LogText(log_snapshot_());
        if (log_label_->getFullText()!=log) log_label_->setText(log);
        diagnostics_refresh_=now+std::chrono::milliseconds(250);
    }
    if (!diagnostics_visible_) {
        nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));
        nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
        nvgFontSize(vg,24);nvgFillColor(vg,nvgRGB(242,242,242));
        if (!got || finished_) nvgText(vg,x+32,y+80,status.c_str(),nullptr);
        if(!got || now-created_<std::chrono::seconds(8)) {
            nvgFontSize(vg,19);nvgText(vg,x+32,y+height-42,
                ui::Text("Touch ball: settings / Hold L3+R3: diagnostics / Minus: game map").c_str(),nullptr);
        }
    }
    brls::Box::draw(vg,x,y,width,height,style,context);
    // A device can open but fail before its first output. Keep the overlay
    // honest when the shared pipeline makes its safe, one-shot SW fallback.
    hardware_decoder_=media_.hardware_decoder();
    stream_rates_.sample(now_ms,media_.decoded(),video_bytes_.load(std::memory_order_relaxed));
    if(stream_health_.sample(now_ms,cloudgenshin::overlay::HealthTotals(media_,video_units_,new_render_submissions_,
        video_bytes_,render_cpu_timings_.snapshot(),peer_poll_gaps_.maximum(),&presentation_debug_,peer_poll_gaps_.takeIntervalMaximum()),
        quality_menu_open_ || diagnostics_visible_ || floating_ball_.captured())) {
        const auto& s=stream_health_.window();
        presentation_debug_.publish(s);
        if(s.ui_active) ui_present_skips_.fetch_add(s.display_skips);else gameplay_present_skips_.fetch_add(s.display_skips);
    }
    draw_overlay(vg,{x,y,width,height});
    nvgSave(vg);nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));
    cloudgenshin::overlay::DrawNetworkLatency(vg,x,y,width,height,
        finished_ ? std::nullopt : network_latency_.value(now_ms));
    nvgRestore(vg);
    presentation_debug_.endDraw();
}
