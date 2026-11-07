#include "endfield_stream_view.hpp"
#include "stream/KeyframeRequestPolicy.hpp"
#include "endfield/entry_probe_policy.hpp"
#include "endfield/diagnostic_event_policy.hpp"
#include "endfield/audio_offer_policy.hpp"
#include "mihoyo/adaptive_stream_policy.hpp"

#include "app_paths.hpp"
#include "app_version.hpp"
#include "endfield/endfield_profile.hpp"
#include "endfield/vlink_session.hpp"
#include "endfield/wl_input.hpp"
#include "endfield/xinput_state.hpp"
#include "mihoyo/autotest_policy.hpp"
#include "mihoyo/controller_source_policy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "stream_settings.hpp"
#include "stream/audio/AudioDiagnostics.hpp"
#include "mihoyo/media_diagnostics.hpp"
#include "webrtc/media_ingress_diagnostics.hpp"
#include "mihoyo/decoder_probe_policy.hpp"
#include "mihoyo/stream_health_sample.hpp"
#include "mihoyo/network_latency_draw.hpp"
#include "stream/ffmpeg/HardwareDecodePolicy.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <borealis/extern/nlohmann/json.hpp>
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}

#ifdef USE_DK_RENDERER
#include "stream/deko3d/DKVideoRenderer.hpp"
#elif defined(USE_GL_RENDERER)
#include "stream/opengl/GLVideoRenderer.hpp"
#endif

namespace cloudgenshin::endfield {
namespace {

// Console XInput mask for the welink CtrlMsg channel. The button bit values
// and the position-preserving mapping (Switch B(bottom)->XInput A,
// A(right)->B, Y(left)->X, X(top)->Y) come from the shared gamepad encoder;
// the official table g[] uses exactly these bits (findings 9.2).
using SentXInput = input::DeliveredXInput;

// Safe-log event names accept only [A-Z0-9_], so protocol identifiers that may
// contain non-ASCII bytes are carried as uppercase hex.
std::string HexEventSuffix(const std::string& text) {
    static const char digits[]="0123456789ABCDEF";
    std::string hex;
    hex.reserve(text.size()*2);
    for(const unsigned char c: text) {
        hex.push_back(digits[c>>4]);hex.push_back(digits[c&15]);
    }
    return hex;
}

// Opt-in autotest image: a bounded 24-bit BMP of the decoded frame, so the
// pod's actual screen is identifiable (desktop, loading page, dialog,
// in-game). Colour matters here: a grayscale dump cannot tell a dark game
// scene from a Windows lock screen. Only the decoder output is read; the
// console desktop is never captured, and normal sessions write nothing.
bool WriteLumaBmp(const std::string& path,AVFrame* frame) {
    constexpr int kWidth=960,kHeight=540;
    const int width=frame->width,height=frame->height;
    const unsigned char* y_plane=frame->data[0];
    const unsigned char* u_plane=frame->data[1];
    const unsigned char* v_plane=frame->data[2];
    const int y_stride=frame->linesize[0];
    const int u_stride=frame->linesize[1];
    const int v_stride=frame->linesize[2];
    const bool chroma=y_plane && u_plane && v_plane && y_stride>0 && u_stride>0 && v_stride>0;
    if(!y_plane || width<16 || height<16 || y_stride<=0) return false;
    // The decoder can hand us either TV-range (16..235) or JPEG-range planes.
    const bool full_range=frame->color_range==AVCOL_RANGE_JPEG ||
        frame->format==AV_PIX_FMT_YUVJ420P;
    const int luma_scale=full_range ? 256 : 298;
    const int luma_offset=full_range ? 0 : 16;
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
    const std::uint32_t row_bytes=static_cast<std::uint32_t>(kWidth*3+3)&~3u;
    const std::uint32_t image_bytes=row_bytes*static_cast<std::uint32_t>(kHeight);
    unsigned char header[54]{};
    header[0]='B';header[1]='M';
    put32(header,2,54+image_bytes);
    put32(header,10,54);
    put32(header,14,40);
    put32(header,18,kWidth);
    put32(header,22,kHeight);
    put16(header,26,1);put16(header,28,24);
    put32(header,34,image_bytes);
    put32(header,38,2835);put32(header,42,2835);
    put32(header,46,0);put32(header,50,0);
    FILE* file=fopen(path.c_str(),"wb");
    if(!file) return false;
    bool ok=fwrite(header,1,sizeof(header),file)==sizeof(header);
    std::vector<unsigned char> line(row_bytes,0);
    const auto clamp_byte=[](int value) {
        return static_cast<unsigned char>(value<0 ? 0 : (value>255 ? 255 : value));
    };
    for(int row=kHeight-1;ok && row>=0;--row) {
        const int source_row=row*height/kHeight;
        const unsigned char* y_line=y_plane+static_cast<long long>(source_row)*y_stride;
        const unsigned char* u_line=chroma
            ? u_plane+static_cast<long long>(source_row/2)*u_stride : nullptr;
        const unsigned char* v_line=chroma
            ? v_plane+static_cast<long long>(source_row/2)*v_stride : nullptr;
        for(int column=0;column<kWidth;++column) {
            const int source_column=column*width/kWidth;
            const int luma=y_line[source_column];
            int red=luma,green=luma,blue=luma;
            if(chroma) {
                const int u=u_line[source_column/2]-128;
                const int v=v_line[source_column/2]-128;
                const int c=luma-luma_offset;
                red=clamp_byte((luma_scale*c+409*v+128)>>8);
                green=clamp_byte((luma_scale*c-100*u-208*v+128)>>8);
                blue=clamp_byte((luma_scale*c+516*u+128)>>8);
            }
            line[column*3+0]=static_cast<unsigned char>(blue);
            line[column*3+1]=static_cast<unsigned char>(green);
            line[column*3+2]=static_cast<unsigned char>(red);
        }
        ok=fwrite(line.data(),1,line.size(),file)==line.size();
    }
    fclose(file);
    return ok;
}

}  // namespace

EndfieldStreamView::EndfieldStreamView(AllocatedStream launch,cloudgenshin::Progress progress,
    std::function<std::string()> log_snapshot,std::function<bool()> export_log,
    int automated_seconds)
    : brls::Box(brls::Axis::COLUMN),progress_(std::move(progress)),
      log_snapshot_(std::move(log_snapshot)),export_log_(std::move(export_log)),
      automated_seconds_(std::clamp(automated_seconds,-1,600)) {
    controller_layout_=opennow::LoadStreamSettings().controller_layout;
    const auto mapping=cloudgenshin::input::LoadControllerMapping(opennow::AppHomePath(),"endfield");
    controller_mapping_=mapping.value;
    progress_(mapping.restored?"CONTROLLER_MAPPING_RESTORED":"CONTROLLER_MAPPING_DEFAULT",3,0);
    if(!ValidBitrateTier(launch.settings.tier) || !ValidStreamFps(launch.settings.fps)) launch.settings={};
    initial_fps_=launch.settings.fps;
    bitrate_request_.request(launch.settings.tier);fps_request_.request(initial_fps_);
    if(launch.settings.valid) {persisted_tier_=launch.settings.tier;persisted_fps_=initial_fps_;}
    setWidth(brls::Application::windowWidth);setHeight(brls::Application::windowHeight);
    setFocusable(true);setHideHighlight(true);setHideHighlightBackground(true);setHideHighlightBorder(true);
    diagnostics_=new brls::Box(brls::Axis::COLUMN);
    diagnostics_->setWidthPercentage(95);diagnostics_->setMargins(24,24,24,24);
    diagnostics_->setPadding(20);diagnostics_->setBackgroundColor(nvgRGBA(12,20,32,238));
    addView(diagnostics_);
    diagnostics_->setVisibility(brls::Visibility::GONE);
    auto* heading=new brls::Box(brls::Axis::ROW);heading->setHeight(52);diagnostics_->addView(heading);
    auto* title=new brls::Label();title->setFontSize(24);title->setGrow(1);
    texts_.add(title,"Stream diagnostics",std::string(opennow::kNativeAppName)+" "+opennow::kCloudGenshinVersion+" / zmd / ");
    heading->addView(title);
    status_label_=new brls::Label();status_label_->setFontSize(20);
    status_label_->setTextColor(nvgRGB(90,225,235));diagnostics_->addView(status_label_);
    auto* help=new brls::Label();help->setFontSize(17);help->setMarginTop(10);help->setMarginBottom(12);
    const std::string face_help=controller_layout_=="Switch"
        ? "A/B/X/Y map to Xbox A/B/X/Y (Switch labels); + opens the game menu."
        : "B/A/Y/X map to Xbox A/B/X/Y (Xbox positions); + opens the game menu.";
    help->setText(ui::Text(face_help)+"\n"+
        ui::Text("Touch ball: settings / Hold L3+R3: diagnostics / Minus: game map")+"\n"+
        ui::Text("Hold + and -: stop and return.")+"\n"+
        opennow::AppHomePath()+"/cloudgenshin-safe.log");
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
    log_label_=new brls::Label();log_label_->setFontSize(17);log_label_->setMarginTop(12);
    log_label_->setTextColor(nvgRGB(198,212,228));diagnostics_->addView(log_label_);
    progress_("STREAM_ENTRY",0,0);
    progress_("ENDFIELD_CONTROLLER_LAYOUT",controller_layout_=="Switch"?1:0,0);
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
    const bool media_ready=renderer_ && media_.start(cloudgenshin::EffectiveStreamFps(initial_fps_),60,hardware_probe);
    if(!media_ready) {
        set_status("MEDIA_INITIALIZATION_FAILED");
    } else {
        progress_(media_.hardware_decoder() ? "VIDEO_DECODER_HARDWARE_INITIALIZED" : "VIDEO_DECODER_SOFTWARE_READY",0,0);
        const auto health=media_.performance();
        progress_("VIDEO_HW_SETUP_STAGE",health.hardware_setup_stage,0);
        progress_("VIDEO_HW_SETUP_ERROR",health.hardware_setup_error,0);
        hardware_decoder_=media_.hardware_decoder();
        if(!media_.sound_ready()) progress_("AUDIO_INITIALIZATION_FAILED",0,0);
    }
    worker_=std::thread([this,launch=std::move(launch),media_ready]() mutable {
        // Everything local to this iteration is freed before the log line is
        // written, so a late UI action cannot race a half-destroyed session.
        {
            try {if(media_ready) network_loop(std::move(launch.sdk),std::move(launch.game_auth_code),
                                              std::move(launch.authorize_game));}
            catch (...) {set_status("STREAM_RUNTIME_FAILED");}
            input_channel_ready_=false;
            settings_ready_=false;
            try {if(launch.release) launch.release();}
            catch(...) {progress_("ENDFIELD_ALLOCATION_RELEASE_FAILED",-1,0);}
            // AVFrameHolder lends a frame to draw(). Do not free it from this
            // network thread; the UI owner does final cleanup after joining us.
            progress_("STREAM_MEDIA_STOPPING",0,0);
            media_.request_stop();
            progress_("STREAM_MEDIA_STOP_REQUESTED",0,0);
            snapshot_counters();
        }
        // Set last: once the UI sees the worker finished it may pop this
        // activity, whose destructor joins this thread.
        progress_("STREAM_WORKER_FINISHED",0,0);
        finished_=true;
    });
}

EndfieldStreamView::~EndfieldStreamView() {
    ui_lifetime_.reset();
    cancelled_=true;
    if(worker_.joinable()) worker_.join();
    // Release GPU-held frame references before decoder/frame-store cleanup.
    renderer_.reset();media_.stop();AVFrameHolder::instance().releaseViewReference();
    decoder_selection_.finish();
}

void EndfieldStreamView::willDisappear(bool reset_state) {
    cancelled_=true;brls::Box::willDisappear(reset_state);
}

void EndfieldStreamView::request_activity_pop() {
    if(activity_pop_armed_.exchange(true)) return;
    const std::weak_ptr<bool> lifetime=ui_lifetime_;
    // Never pop from inside draw(): borealis may run the hide animation (and
    // its callback, which deletes this activity) synchronously, re-entering
    // this view while it is still being drawn. Defer one frame instead, and
    // take the default `free=true` path so the activity is actually destroyed
    // (this view's destructor joins the network worker and frees the decoder).
    brls::sync([this,lifetime] {
        if(lifetime.expired()) return;
        activity_pop_armed_=false;
        if(pop_requested_) return;
        const auto stack=brls::Application::getActivitiesStack();
        if(stack.empty() || !stack.back() || stack.back()->getContentView()!=this) return;
        const auto progress=progress_;
        const bool automated=automated_seconds_!=0;
        // Persist the final counters: once the activity is destroyed this
        // process has no other way to report why the session ended.
        try { export_log_(); } catch (...) {}
        pop_requested_=true;
        progress("STREAM_POP_REQUESTED",0,0);
        const bool popped=brls::Application::popActivity(brls::TransitionAnimation::FADE,[automated] {
            const auto activities=brls::Application::getActivitiesStack();
            if(!activities.empty() && activities.back() && activities.back()->getContentView())
                brls::Application::giveFocus(activities.back()->getContentView());
            if(automated) brls::sync([] {brls::Application::quit();});
        });
        // Successful pop may have destroyed the view synchronously.
        progress(popped ? "STREAM_POP_ACCEPTED" : "STREAM_POP_REFUSED",0,0);
        if(!popped && !lifetime.expired()) pop_requested_=false;
    });
}
brls::View* EndfieldStreamView::getDefaultFocus() {
    return diagnostics_visible_ ? static_cast<brls::View*>(export_button_) : this;
}
void EndfieldStreamView::set_status(const std::string& event) {
    {std::lock_guard lock(mutex_);status_=event;}
    // The tab's progress callback is network-thread safe and only appends to
    // the in-memory log; it never touches widgets.
    progress_(event,0,0);
}
void EndfieldStreamView::show_diagnostics(bool visible) {
    if(visible && quality_menu_open_) show_quality_menu(false);
    diagnostics_visible_=visible;
    diagnostics_->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    const auto neutral=input_capture_.arm();
    {std::lock_guard lock(mutex_);input_=neutral;motion_input_={};}
    diagnostics_refresh_={};
    brls::Application::giveFocus(visible ? static_cast<brls::View*>(export_button_) : this);
}
void EndfieldStreamView::request_exit() {
    if(exit_requested_) return;
    exit_requested_=true;cancelled_=true;
    set_status("STREAM_STOP_REQUESTED");
    progress_("STREAM_EXIT_REQUEST_ARMED",0,0);
    snapshot_counters();show_diagnostics(true);
}
void EndfieldStreamView::snapshot_counters() {
    const auto report=[this](const char* name,unsigned value) {
        progress_(name,static_cast<int>(std::min(value,static_cast<unsigned>(std::numeric_limits<int>::max()))),0);
    };
    report("VIDEO_UNITS_RECEIVED",video_units_);
    report("VIDEO_IDR_UNITS_RECEIVED",media_.idrs());
    report("VIDEO_DECODED_FRAMES",media_.decoded());
    report("VIDEO_DROPPED_UNITS",media_.drops());
    report("VIDEO_DECODE_ERRORS",media_.errors());
    // Separate "the pod stopped sending pictures" from "we stopped decoding
    // them": submitted/zero_output track the decoder, the size buckets track
    // what the pod actually put on the wire.
    report("VIDEO_SUBMITTED_UNITS",media_.submitted());
    report("VIDEO_ZERO_OUTPUT",media_.zero_output());
    report("VIDEO_QUEUE_DEPTH",media_.queue_depth());
    report("VIDEO_LAST_UNIT_BYTES",media_.last_unit_bytes());
    report("VIDEO_LAST_UNIT_IDR",media_.last_unit_idr());
    report("VIDEO_SMALL_UNITS",media_.small_units());
    report("VIDEO_MEDIUM_UNITS",media_.medium_units());
    report("VIDEO_LARGE_UNITS",media_.large_units());
    report("VIDEO_CORRUPT_FRAMES",media_.corrupt_frames());
    cloudgenshin::ReportMediaPerformance(media_.performance(),progress_);
    report("VIDEO_PRESENT_QUEUE_DROPS",static_cast<unsigned>(AVFrameHolder::instance().getFrameDropStat()));
    report("VIDEO_PAYLOAD_KBYTES",static_cast<unsigned>(std::min<std::uint64_t>(video_bytes_.load()/1024,INT_MAX)));
    progress_("VIDEO_LAST_DECODE_ERROR",media_.last_decode_error(),0);
    report("AUDIO_PACKETS_RECEIVED",audio_packets_);
    // Audio is the cheapest proof of what the pod is really doing: a running
    // game always produces sound (BGM at minimum), a frozen desktop is silent.
    // Levels are integers in 16-bit sample units, never PCM data.
    {
        const AudioLevels levels=media_.audio_levels();
        progress_("AUDIO_PACKETS_DECODED",
                  static_cast<int>(std::min<std::uint64_t>(levels.packets_decoded,999999)),0);
        progress_("AUDIO_DECODE_ERRORS",
                  static_cast<int>(std::min<std::uint64_t>(levels.decode_errors,999999)),0);
        progress_("AUDIO_IN_PEAK",levels.input_peak,0);
        progress_("AUDIO_IN_RMS",levels.input_rms,0);
        opennow::audio::ReportOutputHealth(levels,progress_);
    }
    report("VIDEO_RENDER_SUBMISSIONS",render_submissions_.load());
    report("VIDEO_NEW_FRAME_SUBMISSIONS",new_render_submissions_);
    report("VIDEO_RENDER_DEFERRED_SUBMISSIONS",deferred_render_submissions_);
    presentation_debug_.report(progress_);
    report("VIDEO_PRESENT_REFERENCE_FAILURES",AVFrameHolder::instance().getReferenceFailureStat());
    report("VIDEO_PRESENT_CLOCK_REBASES",AVFrameHolder::instance().getClockRebaseStat());
    report("VIDEO_PRESENT_TIMING_HOLDS",AVFrameHolder::instance().getTimingHoldStat());
    report("VIDEO_REUSED_FRAME_SUBMISSIONS",reused_render_submissions_);
    report("VIDEO_UI_INTERVAL_PRESENT_SKIPS",static_cast<unsigned>(std::min<std::uint64_t>(ui_present_skips_.load(),INT_MAX)));
    report("VIDEO_GAMEPLAY_PRESENT_SKIPS",static_cast<unsigned>(std::min<std::uint64_t>(gameplay_present_skips_.load(),INT_MAX)));
    report("VIDEO_FRAME_SAMPLE_COUNT",frame_samples_);
    report("ENDFIELD_GAMEPAD_WRITES_ACCEPTED",gamepad_writes_);
    report("ENDFIELD_GAMEPAD_WRITE_FAILURES",gamepad_write_failures_);
    report("ENDFIELD_INPUT_READY",input_channel_ready_?1:0);
    report("ENDFIELD_GAMEPAD_NEUTRAL_PENDING",gamepad_neutral_pending_?1:0);
    report("ENDFIELD_ACTIVE_INPUT_VERSION",static_cast<unsigned>(active_input_version_.load()));
    const auto lifecycle=game_lifecycle_.load(std::memory_order_relaxed);
    if(lifecycle>0) report("ENDFIELD_GAME_LIFECYCLE_STATE",lifecycle);
    if(automated_seconds_!=0) dump_video_unit_trace();
    progress_("STREAM_DIAGNOSTICS_SNAPSHOT",0,0);
}

void EndfieldStreamView::record_video_unit(const PeerVideoPacket& packet) {
    if(automated_seconds_==0 || !packet.data || packet.size==0) return;
    VideoUnitTrace trace;
    trace.size=static_cast<unsigned>(std::min<std::size_t>(packet.size,9999999));
    for(std::size_t at=0;at+3<packet.size;++at) {
        if(packet.data[at]!=0 || packet.data[at+1]!=0) continue;
        std::size_t header=at+2;
        if(packet.data[header]==0) ++header;
        if(header+1>=packet.size || packet.data[header]!=1) continue;
        const unsigned type=packet.data[++header]&31u;
        if(!trace.start_code) trace.first_nal=type;
        trace.start_code=1;
        trace.nal_mask|=1u<<type;
        if(type>=1 && type<=5) ++trace.slices;
        at=header;
    }
    std::lock_guard lock(trace_mutex_);
    video_unit_trace_[video_unit_trace_seq_++%kVideoUnitTraceSlots]=trace;
}

void EndfieldStreamView::dump_video_unit_trace() {
    VideoUnitTrace trace;
    {
        std::lock_guard lock(trace_mutex_);
        if(video_unit_trace_seq_==0) return;
        trace=video_unit_trace_[(video_unit_trace_seq_-1)%kVideoUnitTraceSlots];
    }
    progress_("VIDEO_LAST_NAL_MASK",trace.nal_mask,0);
    progress_("VIDEO_LAST_NAL_SLICES",trace.slices,0);
    progress_("VIDEO_LAST_NAL_FIRST",trace.first_nal,0);
}
// Numeric content check for the frame handed to the renderer. Normal play
// emits only sparse-luma summaries; explicit autotest may store bounded local
// stream screenshots below. Neither path captures the login/QR screen.
void EndfieldStreamView::sample_frame_content(AVFrame* frame) {
    const int width=frame->width,height=frame->height,stride=frame->linesize[0];
    if(frame_width_.load(std::memory_order_relaxed)==0) {
        frame_width_.store(width,std::memory_order_relaxed);
        frame_height_.store(height,std::memory_order_relaxed);
        progress_("VIDEO_FRAME_SIZE_W",width,0);
        progress_("VIDEO_FRAME_SIZE_H",height,0);
        // Colour signalling: an HDR (PQ/BT.2020) stream rendered without tone
        // mapping looks exactly like a near-black scene, so record the enum
        // values once. They are protocol numbers, never pixel data.
        progress_("VIDEO_FRAME_FORMAT",frame->format,0);
        progress_("VIDEO_FRAME_RANGE",frame->color_range,0);
        progress_("VIDEO_FRAME_PRIMARIES",frame->color_primaries,0);
        progress_("VIDEO_FRAME_TRC",frame->color_trc,0);
    }
    // NVTEGRA surfaces are block-linear GPU mappings, not flat CPU luma.
    // Never interpret their handles/addresses as a software screenshot.
    if(!opennow::video::LinearLumaReadable(frame->format)) {
        // Explicit autotest only: use FFmpeg's proper transfer API, at the
        // existing bounded screenshot cadence. Normal play never downloads
        // GPU surfaces or writes screenshots, preserving zero-copy rendering.
        const auto now=std::chrono::steady_clock::now();
        if(automated_seconds_!=0 && frame->format==AV_PIX_FMT_NVTEGRA && now>=frame_dump_next_) {
            AVFrame* cpu=av_frame_alloc();
            int result=AVERROR(ENOMEM);
            if(cpu && frame->hw_frames_ctx) {
                const auto* frames=reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
                const int storage_width=opennow::video::DiagnosticTransferWidth(width);
                if(storage_width && height>0 && height<=8192 &&
                   opennow::video::LinearLumaReadable(frames->sw_format)) {
                    cpu->format=frames->sw_format;cpu->width=width;cpu->height=height;
                    const int storage_height=(height+1)&~1;
                    const int bytes=av_image_get_buffer_size(frames->sw_format,storage_width,storage_height,32);
                    if(bytes>0) cpu->buf[0]=av_buffer_alloc(bytes);
                    if(cpu->buf[0]) {
                        result=av_image_fill_arrays(cpu->data,cpu->linesize,cpu->buf[0]->data,
                            frames->sw_format,storage_width,storage_height,32);
                        if(result>=0) result=av_hwframe_transfer_data(cpu,frame,0);
                    }
                } else result=AVERROR(ENOSYS);
            }
            if(result==0 && opennow::video::LinearLumaReadable(cpu->format)) {
                av_frame_copy_props(cpu,frame);
                sample_frame_content(cpu);
            } else {
                progress_("VIDEO_HW_DIAGNOSTIC_TRANSFER_FAILED",result,0);
            }
            av_frame_free(&cpu);
            if(frame_dump_next_<=now) frame_dump_next_=now+std::chrono::seconds(30);
        }
        return;
    }
    const unsigned char* plane=frame->data[0];
    if(!plane || width<16 || height<16 || stride<=0) return;
    frame_samples_.fetch_add(1);
    constexpr int kRows=36,kColumns=64;
    unsigned long long sum=0,gradient=0;
    unsigned luma_min=255,luma_max=0;
    int samples=0,gradient_samples=0,nonblack=0;
    for(int row=0;row<kRows;++row) {
        const unsigned char* line=plane+static_cast<long long>(row*height/kRows)*stride;
        int previous=-1;
        for(int column=0;column<kColumns;++column) {
            const int value=line[column*width/kColumns];
            sum+=static_cast<unsigned>(value);
            if(static_cast<unsigned>(value)<luma_min) luma_min=static_cast<unsigned>(value);
            if(static_cast<unsigned>(value)>luma_max) luma_max=static_cast<unsigned>(value);
            if(value>=24) ++nonblack;
            if(previous>=0) {
                const int delta=value>previous ? value-previous : previous-value;
                gradient+=static_cast<unsigned>(delta);++gradient_samples;
            }
            previous=value;++samples;
        }
    }
    if(samples==0) return;
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MEAN",static_cast<int>(sum/static_cast<unsigned>(samples)),0);
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MIN",static_cast<int>(luma_min),0);
    progress_("VIDEO_FRAME_SAMPLE_LUMA_MAX",static_cast<int>(luma_max),0);
    progress_("VIDEO_FRAME_SAMPLE_CONTRAST",gradient_samples
        ? static_cast<int>(gradient/static_cast<unsigned>(gradient_samples)) : 0,0);
    progress_("VIDEO_FRAME_SAMPLE_NONBLACK",nonblack*100/samples,0);
    // Bounded autotest screenshots of the stream itself (never the desktop).
    if(automated_seconds_!=0) {
        const auto now=std::chrono::steady_clock::now();
        if(now>=frame_dump_next_) {
            const std::string path=opennow::AppHomePath()+"/endfield_frame_"+
                std::to_string(frame_dump_slot_)+".bmp";
            if(WriteLumaBmp(path,frame)) {
                progress_("ENDFIELD_FRAME_DUMP_WRITTEN",static_cast<int>(frame_dump_slot_+1),0);
                frame_dump_slot_=(frame_dump_slot_+1)%24;
                frame_dump_next_=now+std::chrono::seconds(10);
            }
        }
    }
}

void EndfieldStreamView::network_loop(VlinkSdkData sdk,std::string auth_code,
    std::function<login::GameAuthorization(const std::atomic_bool*)> authorize_game) {
    DiagnosticEventPolicy unknown_events;
    VlinkSession session(std::move(sdk),{
        .video=[this](const PeerVideoPacket& packet){
            video_units_.fetch_add(1,std::memory_order_relaxed);
            video_bytes_.fetch_add(packet.size,std::memory_order_relaxed);
            record_video_unit(packet);
            media_.video(packet);
        },
        .audio=[this](const PeerAudioPacket& packet){
            audio_packets_.fetch_add(1,std::memory_order_relaxed);
            media_.audio(packet);
        },
        .transport_state=[this](PeerConnectionState state,const std::string&){
            progress_("RTC_PEER_STATE",static_cast<int>(state),0);
            if(state==PEER_CONNECTION_COMPLETED) set_status("RTC_CONNECTED_WAITING_VIDEO");
        },
        .datachannel_open=[this]{
            progress_("RTC_REMOTE_DATA_CHANNEL_OPEN",0,0);
            set_status("RTC_REMOTE_DATA_CHANNEL_OPEN");
        },
        .gs_message=[this,&unknown_events](const std::string& cmd,const std::string& arg){
            // Official 启动进度 stages (findings 8.3): READY -> archive ->
            // START_GAME. Expose them as status text so the user can see the
            // real server stage instead of a generic "connecting".
            if(cmd=="启动进度") {
                if(arg=="准备开始游戏") set_status("SERVER_READY_FOR_GAME");
                else if(arg=="服务端正在处理存档") set_status("SERVER_HANDLING_SAVE");
                else if(arg=="游戏开始") set_status("ENDFIELD_SERVER_GAME_STARTED");
                else progress_("ENDFIELD_STARTUP_STAGE_UNKNOWN",0,0);
            } else if(cmd=="踢下线") {
                set_status("SESSION_KICKED_OFFLINE");
            } else if(cmd=="protoVersion") {
                progress_("ENDFIELD_PROTO_VERSION_RECEIVED",0,0);
            } else if(cmd=="WEILINK_CHANNEL_SID") {
                progress_("ENDFIELD_WEILINK_SID_ADOPTED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_REMOTE_DC") {
                progress_("ENDFIELD_WEILINK_REMOTE_DC",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_LOCAL_DC_CREATED") {
                progress_("ENDFIELD_WEILINK_LOCAL_DC",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_STARTUP_SENT") {
                progress_("ENDFIELD_STARTUP_SENT",0,0);
            } else if(cmd=="WEILINK_STARTUP_PACKETS") {
                progress_("ENDFIELD_STARTUP_PACKETS",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_STARTUP_BYTES") {
                progress_("ENDFIELD_STARTUP_BYTES",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_PACKED_DATA_BYTES") {
                progress_("ENDFIELD_PACKED_DATA_BYTES",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_SIGNALING_CLOSED_AFTER_ANSWER") {
                progress_("ENDFIELD_SIGNALING_CLOSED_AFTER_ANSWER",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_STARTUP_MODE") {
                progress_("ENDFIELD_STARTUP_MODE",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_BRIDGE_STARTED") {
                progress_("ENDFIELD_GAME_BRIDGE_STARTED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_BRIDGE_REQUEST") {
                const auto request=static_cast<GameBridge::Request>(std::atoi(arg.c_str()));
                progress_("ENDFIELD_GAME_BRIDGE_REQUEST",static_cast<int>(request),0);
                if(request==GameBridge::Request::AccountLogin || request==GameBridge::Request::CharacterLogin ||
                   request==GameBridge::Request::CharacterLogout || request==GameBridge::Request::AccountLogout ||
                   request==GameBridge::Request::BrowserOpen)
                    game_lifecycle_.store(static_cast<int>(request),std::memory_order_relaxed);
                if(request==GameBridge::Request::AccountLogin) set_status("ENDFIELD_GAME_ACCOUNT_LOGGED_IN");
                else if(request==GameBridge::Request::CharacterLogin) set_status("ENDFIELD_GAME_CHARACTER_ENTERED");
                else if(request==GameBridge::Request::CharacterLogout) set_status("ENDFIELD_GAME_CHARACTER_LEFT");
                else if(request==GameBridge::Request::AccountLogout) set_status("ENDFIELD_GAME_ACCOUNT_LOGGED_OUT");
                else if(request==GameBridge::Request::BrowserOpen) set_status("ENDFIELD_GAME_BROWSER_REQUIRED");
            } else if(cmd=="WEILINK_GAME_BRIDGE_CONFIRMED") {
                progress_("ENDFIELD_GAME_BRIDGE_CONFIRMED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_BRIDGE_UNHANDLED_HASH") {
                progress_("ENDFIELD_GAME_BRIDGE_UNHANDLED_HASH",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_BRIDGE_EXPIRED") {
                progress_("ENDFIELD_GAME_BRIDGE_EXPIRED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_AUTH_STATUS") {
                const int status=std::atoi(arg.c_str());
                if(status==109) set_status("ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED");
                else progress_(status==0 ? "ENDFIELD_GAME_AUTH_READY" : "ENDFIELD_GAME_AUTH_FAILED",status,0);
            } else if(cmd=="WEILINK_INPUT_VERSION") {
                progress_("ENDFIELD_INPUT_VERSION_NEGOTIATED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_EXIT_QUEUED_LOCAL") {
                progress_("ENDFIELD_EXIT_QUEUED_LOCAL",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_EXIT_SEND_FAILED") {
                progress_("ENDFIELD_EXIT_SEND_FAILED",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_EXIT_DRAIN_MS") {
                progress_("ENDFIELD_EXIT_DRAIN_MS",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_CONTROL_CHANNEL_STATE") {
                progress_("ENDFIELD_CONTROL_CHANNEL_STATE",std::atoi(arg.c_str()),0);
            } else if(cmd=="WEILINK_GAME_DATA_TYPE") {
                progress_("ENDFIELD_GAME_DATA_TYPE",std::atoi(arg.c_str()),0);
            } else if(cmd=="OnAuthResult") {
                // Classify fixed status words; unexpected payloads stay private.
                const int result=arg=="success" ? 1 : (arg=="failure" ? 2 : 3);
                progress_("ENDFIELD_AUTH_RESULT",result,0);
            } else if(cmd=="PipeState") {
                progress_("ENDFIELD_PIPE_STATE",std::atoi(arg.c_str()),0);
            } else if(cmd=="BitrateChange") {
                progress_("ENDFIELD_BITRATE_CHANGE",std::atoi(arg.c_str()),0);
            } else if(cmd=="ginput_level") {
                progress_("ENDFIELD_GINPUT_LEVEL",std::atoi(arg.c_str()),0);
            } else if(cmd=="OnCursorData") {
                // Fixed protocol words only: "show" or "hide" tells whether the
                // pod's game owns the pointer (relative mouse) or not.
                const int state=arg.rfind("show",0)==0 ? 1 : (arg.rfind("hide",0)==0 ? 0 : 2);
                progress_("ENDFIELD_CURSOR_STATE",state,0);
            } else if(cmd=="swap_gs_info") {
                // Structure only: key names are protocol metadata, values are
                // never logged.
                const auto parsed=nlohmann::json::parse(arg,nullptr,false);
                progress_("ENDFIELD_SWAP_GS_INFO",parsed.is_object()?1:0,0);
                if(parsed.is_object()) {
                    for(const auto& item: parsed.items()) {
                        progress_("ENDFIELD_SWAP_KEY_HEX_"+HexEventSuffix(item.key()),0,0);
                        if(item.value().is_object())
                            for(const auto& inner: item.value().items())
                                progress_("ENDFIELD_SWAP_FN_KEY_HEX_"+HexEventSuffix(inner.key()),0,0);
                    }
                }
            } else if(cmd=="WEILINK_STARTUP_FAILED") {
                progress_("ENDFIELD_STARTUP_SEND_FAILED",0,0);
            } else if(cmd=="WEILINK_BITRATE_META") {
                // arg = "<codecType>,<WxH>". Both are protocol metadata.
                const auto comma=arg.find(',');
                const int codec=std::atoi(arg.substr(0,comma).c_str());
                int width=0,height=0;
                if(comma!=std::string::npos) {
                    const auto x=arg.find('x',comma);
                    if(x!=std::string::npos) {
                        width=std::atoi(arg.substr(comma+1,x-comma-1).c_str());
                        height=std::atoi(arg.substr(x+1).c_str());
                    }
                }
                progress_("ENDFIELD_BITRATE_CODEC",codec,0);
                progress_("ENDFIELD_BITRATE_WIDTH",width,0);
                progress_("ENDFIELD_BITRATE_HEIGHT",height,0);
            } else if(cmd=="WEILINK_UNPARSED") {
                // A pod message that is not a SimpleCmd protobuf. Length, first
                // byte and stream id tell whether it is a JSON/text channel we
                // ignore (first byte '{' or '['), a greeting on the pod-opened
                // sid, or a framing problem.
                std::vector<std::string> fields;
                std::size_t from=0;
                while(from<=arg.size()) {
                    const auto comma=arg.find(',',from);
                    fields.push_back(arg.substr(from,comma==std::string::npos
                        ? std::string::npos : comma-from));
                    if(comma==std::string::npos) break;
                    from=comma+1;
                }
                const int bytes=fields.empty() ? 0 : std::atoi(fields[0].c_str());
                const int first=fields.size()>1 ? std::atoi(fields[1].c_str()) : 0;
                const int sid=fields.size()>2 ? std::atoi(fields[2].c_str()) : -1;
                progress_("ENDFIELD_WEILINK_UNPARSED_BYTES",bytes,first);
                progress_("ENDFIELD_WEILINK_UNPARSED_SID",sid,0);
            }
            // Every other command is surfaced by name so a silent pod shows up
            // as "it sent X but nothing we act on" instead of nothing at all.
            // Unknown commands are protocol strings that may contain non-ASCII
            // text, so only a stable hash and the argument length are logged.
            if(cmd!="启动进度" && cmd!="踢下线" && cmd!="protoVersion" &&
               cmd!="WEILINK_CHANNEL_SID" && cmd!="WEILINK_REMOTE_DC" &&
               cmd!="WEILINK_LOCAL_DC_CREATED" && cmd!="WEILINK_STARTUP_SENT" &&
               cmd!="WEILINK_STARTUP_PACKETS" && cmd!="WEILINK_STARTUP_BYTES" &&
               cmd!="WEILINK_PACKED_DATA_BYTES" && cmd!="WEILINK_STARTUP_FAILED" &&
               cmd!="WEILINK_SIGNALING_CLOSED_AFTER_ANSWER" && cmd!="swap_gs_info" &&
               cmd!="WEILINK_STARTUP_MODE" && cmd!="OnAuthResult" &&
               cmd!="PipeState" && cmd!="BitrateChange" && cmd!="ginput_level" &&
               cmd!="OnCursorData" && cmd!="WEILINK_UNPARSED" &&
               cmd!="WEILINK_BITRATE_META" && cmd!="WEILINK_INPUT_VERSION" &&
               cmd!="WEILINK_EXIT_QUEUED_LOCAL" && cmd!="WEILINK_EXIT_SEND_FAILED" &&
               cmd!="WEILINK_EXIT_DRAIN_MS" && cmd!="WEILINK_CONTROL_CHANNEL_STATE" &&
               cmd!="WEILINK_GAME_DATA_TYPE") {
                unsigned int hash=2166136261u;
                for(const unsigned char c: cmd) {hash^=c;hash*=16777619u;}
                const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if(!unknown_events.admit(hash,now_ms)) return;
                progress_("ENDFIELD_WEILINK_CMD_HASH",
                          static_cast<int>(hash & 0x7fffffffu),0);
                progress_("ENDFIELD_WEILINK_ARG_LEN",
                          static_cast<int>(std::min<std::size_t>(arg.size(),9999)),0);
            }
        },
        .game_authorization=std::move(authorize_game),
        .video_discontinuity=[this] {
            // Complete pictures queued before the lost AU remain valid. A
            // hard resync here needlessly deletes them and resets decoding.
            // The depacketizer invokes this before delivering any newer AU.
            media_.notify_video_damage();
        },
    },std::move(auth_code));
    set_status("SIGNALING_CONNECTING");
    progress_("ENDFIELD_AUDIO_REQUESTED_BITRATE",kLowBandwidthAudioBitrate,0);
    if(!session.start(&cancelled_)) {
        set_status(cancelled_ ? "STREAM_STOPPED_BY_USER" : "ENDFIELD_STREAM_CONNECT_FAILED");
        return;
    }
    progress_("SIGNALING_UPGRADE_OK",0,0);
    set_status("RTC_ANSWER_SENT_WAITING_CONNECTION");
    const auto started=std::chrono::steady_clock::now();
    auto last_peer_poll=started;
    auto next_input=started,next_snapshot=started,next_title_click=started,next_settings=started;
    auto next_entry_check=started;
    auto next_video_loss_check=started;
    auto last_decode_progress=started;
    opennow::video::KeyframeRequestPolicy keyframe_policy;
    unsigned last_decoded=0,keyframe_requests=0,keyframe_failures=0;
    bool video_rtp_seen=false;
    bool first_frame=false,game_seen=false,fps_sent=false,intercept_sent=false,resolution_sent=false;
    SettingSendBudget bitrate_budget,fps_budget;
    bool settings_were_ready=false,keyframe_after_settings=false;
    auto settings_keyframe_at=started;
    AdaptiveStream adaptive_stream;
    auto next_adaptive_sample=started;
    bool settings_reported=false;
    bool entry_click_enabled=false;
    bool confirm_enabled=false;
    int confirm_stage=0;
    auto confirm_next=started,confirm_deadline=started;
    int title_click_stage=0;
    auto title_click_deadline=started;
    int entry_cursor_x=640,entry_cursor_y=360;
    SentXInput sent;
    cloudgenshin::input::ControllerNeutralHandoff controller_handoff;
    const auto send_input=[&](const auto& message) {
        const bool accepted=session.send_input(message);
        if(accepted) gamepad_writes_.fetch_add(1,std::memory_order_relaxed);
        else gamepad_write_failures_.fetch_add(1,std::memory_order_relaxed);
        return accepted;
    };
    while(!cancelled_ && session.running()) {
        // Describe the virtual slot only after its connection write was
        // accepted, rather than racing the UI's physical connection flag.
        session.note_gamepad(sent.connected.value_or(false));
        const auto poll_started=std::chrono::steady_clock::now();
        peer_poll_gaps_.observe(std::chrono::duration_cast<std::chrono::microseconds>(poll_started-last_peer_poll).count());
        last_peer_poll=poll_started;
        session.poll();
        if(!session.last_error().empty()) break;
        input_channel_ready_=session.startup_ready() && session.datachannel_open();
        active_input_version_=session.input_version();
        const auto now=std::chrono::steady_clock::now();
        network_latency_.sample(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count(),[&session]{return session.network_rtt_ms();});
        if(now>=next_video_loss_check) {
            next_video_loss_check=now+std::chrono::milliseconds(100);
            if(const auto stats=session.video_rtp_stats()) {
                video_rtp_seen=video_rtp_seen || stats->packets_received>0;
            }
        }
        // The separate opt-in may be armed after inspecting the real title
        // screen. Check at most once per second, with no file work under a
        // transport or input lock; it still permits exactly one click.
        if(automated_seconds_!=0 && !entry_click_enabled && now>=next_entry_check) {
            next_entry_check=now+std::chrono::seconds(1);
            entry_click_enabled=AutoTestEntryClickEnabled(
                opennow::AppHomePath()+"/cloudgenshin-autotest.click");
            if(entry_click_enabled) title_click_deadline=now+std::chrono::seconds(5);
            if(!confirm_enabled && AutoTestEndfieldConfirmEnabled(
                opennow::AppHomePath()+"/cloudgenshin-autotest.confirm")) {
                confirm_enabled=true;
                confirm_deadline=now+std::chrono::seconds(5);
            }
        }
        if(!game_seen && session.game_started()) {game_seen=true;set_status("ENDFIELD_SERVER_GAME_STARTED");}
        if(!first_frame && media_.decoded()>0) {
            first_frame=true;set_status("LIVE_VIDEO_DECODED");
            // Official client starts the 1 s SDKHeartBeat here, not at the
            // game's "start" message (handleStartGameInfo @2006433).
            session.note_video_frame();
        }
        // Official ensureResolutionSyncedAfterConnect always sends
        // SetGameResolution after first video, even when dispatch and display
        // dimensions are identical. SDP/capture dimensions do not replace it.
        if(first_frame && !resolution_sent && session.set_game_resolution(kDispatchWidth,kDispatchHeight)) {
            resolution_sent=true;
            progress_("ENDFIELD_GAME_RESOLUTION_SENT",kDispatchWidth,0);
        }
        // Damage can leave the decoder producing concealed/corrupt pictures,
        // so advancing frame counts alone do not prove recovery. Honor the
        // shared decoder's IDR requirement as well as a stalled decode count.
        // A lost first IDR must also recover before first_frame becomes true.
        {
            const unsigned decoded_now=media_.decoded();
            if(decoded_now!=last_decoded) {
                last_decoded=decoded_now;
                last_decode_progress=now;
            }
            if(video_rtp_seen || video_units_.load(std::memory_order_relaxed)>0) {
                std::uint64_t recovery_generation=0;
                const bool recovery=media_.take_keyframe_request(&recovery_generation);
                const bool needed=recovery ||
                    (first_frame && now-last_decode_progress>std::chrono::seconds(2)) ||
                    (keyframe_after_settings && now>=settings_keyframe_at);
                if(keyframe_policy.due(std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count(),needed,recovery_generation)) {
                    if(session.request_keyframe()) {++keyframe_requests;keyframe_after_settings=false;}
                    else {++keyframe_failures;keyframe_policy.failed();}
                }
            }
        }
        const bool settings_ready=first_frame && input_channel_ready_.load();
        if(settings_ready && !settings_were_ready) {
            bitrate_request_.retry();fps_request_.retry();
        }
        settings_were_ready=settings_ready;settings_ready_=settings_ready;
        if(first_frame && now>=next_settings) {
            next_settings=now+std::chrono::milliseconds(500);
            // The official web client applies the runtime refresh-rate choice
            // and enables app-data interception when the first frame arrives
            // (handleStartGameInfo @2006433). Sending SetFps before the pod is
            // ready is ignored, so keep this timing. Setting requests use
            // independent bounded retry budgets per UI selection.
            if(!intercept_sent && session.enable_app_intercept()) {
                intercept_sent=true;
                progress_("ENDFIELD_INTERCEPT_APP_SENT",0,0);
            }
            const auto setting_now=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            if(const auto request=bitrate_request_.pending();request && settings_ready_ && bitrate_budget.try_write(*request,setting_now)) {
                const bool accepted=session.set_bitrate_tier(request->value());
                bitrate_budget.sent(accepted);
                if(bitrate_budget.delivered() || bitrate_budget.exhausted())
                    bitrate_request_.complete(*request,bitrate_budget.delivered());
                if(bitrate_budget.delivered()) {
                    accepted_tier_=request->value();persist_stream_settings();
                    fps_request_.retry();
                    keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);
                    const auto range=kStreamBitrates[request->value()];
                    adaptive_stream.configure(range,fps_request_.snapshot().value(),setting_now);
                    smoothness_protection_=false;
                    active_bitrate_limit_=range.maximum;
                    progress_("ENDFIELD_BITRATE_REQUEST_SENT",kStreamBitrates[request->value()].maximum,0);
                    progress_("STREAM_BITRATE_MIN_KBPS",range.minimum,0);
                } else if(bitrate_budget.exhausted()) progress_("ENDFIELD_BITRATE_REQUEST_FAILED",request->value(),0);
            }
            if(const auto request=fps_request_.pending();request && settings_ready_ && fps_budget.try_write(*request,setting_now)) {
                const bool accepted=session.set_fps(request->value());
                fps_budget.sent(accepted);
                if(accepted) {media_.set_fps(request->value());active_fps_=request->value();}
                if(fps_budget.delivered() || fps_budget.exhausted())
                    fps_request_.complete(*request,fps_budget.delivered());
                if(fps_budget.delivered()) {
                    fps_sent=true;accepted_fps_=request->value();persist_stream_settings();
                    adaptive_stream.manual_fps(request->value(),setting_now);
                    smoothness_protection_=adaptive_stream.protecting();
                    keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);
                    progress_("STREAM_FPS_SENT",request->value(),0);
                } else if(fps_budget.exhausted()) progress_("ENDFIELD_FPS_REQUEST_FAILED",request->value(),0);
            }
            // Both timed out: report it instead of waiting for a black screen
            // that ends in FIRST_VIDEO_TIMEOUT. Streaming continues (inputs
            // still work once the pod accepts them); the diagnostics only
            // record which post-frame command was not accepted.
            if(now-started>std::chrono::seconds(30) && (!fps_sent || !intercept_sent)) {
                if(!settings_reported) {
                    settings_reported=true;
                    set_status("STREAM_SETTINGS_NOT_ACCEPTED");
                    progress_("STREAM_SETTINGS_FPS_ACK",fps_sent ? 1 : 0,0);
                    progress_("STREAM_SETTINGS_INTERCEPT_ACK",intercept_sent ? 1 : 0,0);
                }
            }
        }
        if(settings_ready_ && bitrate_request_.snapshot().state()==overlay::SettingRequest::State::Sent &&
           fps_request_.snapshot().state()==overlay::SettingRequest::State::Sent) {
            const auto at=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            if(now>=next_adaptive_sample) {
                next_adaptive_sample=now+std::chrono::seconds(1);
                const auto performance=media_.performance();
                if(const auto stats=session.video_rtp_stats()) adaptive_stream.observe(at,
                    {stats->access_units_completed,stats->access_units_dropped,stats->reorder_timeout_skips,
                     stats->reorder_capacity_skips,performance.congestion_events,performance.waiting_idr_drops});
            }
            if(const auto change=adaptive_stream.write_due(at)) {
                const bool fps=change->kind==AdaptiveStream::Kind::Fps;
                const bool accepted=fps ? session.set_fps(change->fps) :
                    session.set_bitrate_range(change->bitrate.minimum,change->bitrate.maximum);
                const bool completed=adaptive_stream.sent(at,accepted);
                if(accepted) {
                    active_fps_=adaptive_stream.fps();media_.set_fps(active_fps_);
                    active_bitrate_limit_=adaptive_stream.bitrate().maximum;
                    smoothness_protection_=adaptive_stream.protecting();
                }
                if(completed) {
                    progress_(fps ? "STREAM_AUTO_FPS_SENT" : "STREAM_AUTO_BITRATE_SENT",
                        fps ? change->fps : change->bitrate.maximum,0);
                    if(fps) {keyframe_after_settings=true;settings_keyframe_at=now+std::chrono::milliseconds(500);}
                }
            }
        } else {adaptive_stream.suspend();next_adaptive_sample=now;}
        if(now>=next_snapshot) {
            cloudgenshin::input::MotionSample motion;
            {std::lock_guard lock(mutex_);motion=motion_input_;}
            const auto motion_now=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            progress_("MOTION_SERVER_REQUESTED",session.motion_requested()?1:0,0);
            progress_("MOTION_SAMPLE_FRESH",motion.valid && motion_now>=motion.sampled_ms && motion_now-motion.sampled_ms<=100 ? 1:0,0);
            progress_("MOTION_REPORTS_SENT",static_cast<int>(std::min<std::uint64_t>(session.motion_sent(),INT_MAX)),0);
            progress_("MOTION_SEND_FAILURES",static_cast<int>(std::min<std::uint64_t>(session.motion_failures(),INT_MAX)),0);
            progress_("ENDFIELD_KEYFRAME_REQUESTS",static_cast<int>(std::min<unsigned>(keyframe_requests,INT_MAX)),0);
            progress_("ENDFIELD_KEYFRAME_SEND_FAILURES",static_cast<int>(std::min<unsigned>(keyframe_failures,INT_MAX)),0);
            progress_("RTC_POLL_GAP_MEAN_US",static_cast<int>(std::min<std::uint64_t>(peer_poll_gaps_.mean(),INT_MAX)),0);
            progress_("RTC_POLL_GAP_MAX_US",static_cast<int>(std::min<std::uint64_t>(peer_poll_gaps_.maximum(),INT_MAX)),0);
            progress_("RTC_SIGNAL_POLL_MAX_US",static_cast<int>(std::min<std::uint64_t>(session.signal_poll_peak_us(),INT_MAX)),0);
            progress_("RTC_PEER_DRAIN_MAX_US",static_cast<int>(std::min<std::uint64_t>(session.peer_drain_peak_us(),INT_MAX)),0);
            progress_("RTC_SESSION_TASKS_MAX_US",static_cast<int>(std::min<std::uint64_t>(session.session_tasks_peak_us(),INT_MAX)),0);
            progress_("RTC_RECEIVE_BUDGET_HITS",static_cast<int>(std::min<std::uint64_t>(session.receive_budget_hits(),INT_MAX)),0);
            // Counters only, at the existing snapshot cadence: accepted
            // writes are local SCTP enqueue, not remote/game acknowledgements.
            progress_("ENDFIELD_CONTROL_MESSAGES_SENT",static_cast<int>(session.control_messages_sent()),0);
            progress_("ENDFIELD_CONTROL_SEND_FAILURES",static_cast<int>(session.control_send_failures()),0);
            progress_("ENDFIELD_WEILINK_MESSAGES",static_cast<int>(session.welink_messages_received()),0);
            // ICE/DTLS progress is the only way to tell "waiting for a slow pod"
            // from "the transport never connected"; log it while attached, not
            // only on teardown.
            if(const auto stats=session.transport_stats()) {
                progress_("RTC_ICE_CANDIDATE_PAIRS",stats->ice_candidate_pairs,0);
                progress_("RTC_ICE_REQUESTS_SENT",stats->ice_requests_sent,0);
                progress_("RTC_ICE_RESPONSES_RECEIVED",stats->ice_responses_received,0);
                progress_("RTC_DTLS_ATTEMPTS",stats->dtls_attempts,0);
                progress_("RTC_DTLS_LAST_RESULT",stats->dtls_result,0);
                progress_("RTC_DTLS_REMOTE_PIN_PRESENT",stats->dtls_remote_pin_present,0);
                progress_("RTC_DTLS_DATAGRAMS_RECEIVED",stats->dtls_datagrams_received,0);
                progress_("RTC_UDP_DATAGRAMS_RECEIVED",stats->udp_datagrams_received,0);
                opennow::webrtc::ReportMediaIngress(*stats,progress_);
            }
            if(const auto stats=session.video_rtp_stats()) {
                progress_("VIDEO_RTP_PACKETS",stats->packets_received,0);
                progress_("VIDEO_RTP_GAPS",stats->sequence_gaps,0);
                progress_("VIDEO_RTP_AU_DROPPED",stats->access_units_dropped,0);
                progress_("VIDEO_RTP_NACK_REQUESTS",stats->nack_requests,0);
                progress_("VIDEO_RTP_REORDERED",stats->reordered_packets,0);
                progress_("VIDEO_RTP_LATE_DROPPED",stats->late_packets_dropped,0);
                progress_("VIDEO_RTP_FORCED_SKIPS",stats->forced_sequence_skips,0);
                progress_("VIDEO_RTP_BUFFERED",stats->reorder_buffered_packets,0);
                progress_("VIDEO_RTP_NACK_PACKETS",stats->nack_packets_requested,0);
                progress_("VIDEO_RTP_NACK_RETRIES",stats->nack_retry_packets,0);
                progress_("VIDEO_RTP_NACK_RECOVERED",stats->nack_recovered_packets,0);
                progress_("VIDEO_RTP_NACK_EXHAUSTED",stats->nack_exhausted_packets,0);
                progress_("VIDEO_RTP_NACK_SEND_FAILURES",stats->nack_send_failures,0);
                progress_("VIDEO_RTP_REPAIR_MAX_MS",stats->nack_repair_max_ms,0);
                progress_("VIDEO_RTP_TIMEOUT_SKIPS",stats->reorder_timeout_skips,0);
                progress_("VIDEO_RTP_CAPACITY_SKIPS",stats->reorder_capacity_skips,0);
                progress_("VIDEO_RTP_WINDOW_CAPACITY",stats->reorder_capacity,0);
                progress_("VIDEO_RTP_WINDOW_PEAK",stats->reorder_peak_packets,0);
                progress_("VIDEO_RTP_WINDOW_GROWTHS",stats->reorder_growths,0);
                progress_("VIDEO_RTP_WINDOW_ALLOC_FAILURES",stats->reorder_growth_failures,0);
                progress_("ENDFIELD_ADAPTIVE_BITRATE_CEILING",adaptive_stream.bitrate().maximum,0);
            }
            // snapshot_counters emits the completion marker. Transport stats
            // must precede it so archive readers do not pair this window's
            // decoder drops with the previous window's RTP damage counters.
            snapshot_counters();
            next_snapshot=now+std::chrono::seconds(5);
        }
        if(!first_frame && now-started>std::chrono::seconds(120)) {set_status("FIRST_VIDEO_TIMEOUT");break;}
        // One explicit entry click is allowed only by the separate SD marker.
        // No keyboard chords or desktop controls belong in an unattended run.
        const int entry_lifecycle=game_lifecycle_.load(std::memory_order_relaxed);
        const bool title_lifecycle=AllowTitleProbe(static_cast<GameBridge::Request>(entry_lifecycle));
        if(entry_click_enabled && title_click_stage<2 &&
           (now>=title_click_deadline || !title_lifecycle)) title_click_stage=3;
        // Server "game started" only means its process exists, including on
        // the title screen. It cannot disable an explicitly armed title tap.
        // Never begin one after character/browser entry; always finish
        // a button release already in flight, even if the lifecycle changes.
        if(entry_click_enabled && first_frame && title_click_stage<3 &&
           now>=next_title_click && now-started>=std::chrono::seconds(15)) {
            next_title_click=now+std::chrono::milliseconds(120);
            bool accepted=false;
            if(title_click_stage==0) {
                const int width=frame_width_.load(std::memory_order_relaxed);
                const int height=frame_height_.load(std::memory_order_relaxed);
                entry_cursor_x=width>0 ? width/2 : 640;
                entry_cursor_y=height>0 ? height/2 : 360;
                accepted=session.send_input(input::EncodeMouseMove(
                    entry_cursor_x,entry_cursor_y,0,0));
            } else {
                accepted=session.send_input(input::EncodeMouseButton(
                    input::kMouseButtonLeft,title_click_stage==1,entry_cursor_x,entry_cursor_y));
            }
            if(accepted) ++title_click_stage;
            if(title_click_stage==3) progress_("ENDFIELD_TITLE_CLICK_SENT",1,1);
        }
        if(continue_requested_.exchange(false) && (confirm_stage!=1)) {
            if(first_frame && input_channel_ready_ &&
               AllowTitleProbe(static_cast<GameBridge::Request>(game_lifecycle_.load()))) {
                confirm_enabled=true;confirm_stage=0;confirm_next=now;
                confirm_deadline=now+std::chrono::seconds(5);
            }
        }
        // A separate exact marker, or an explicit floating-menu action, permits one Xbox A tap after the title has
        // been inspected. Do not continue this probe into gameplay or
        // browser UI; a pending release is always retried before normal input.
        if(confirm_enabled && confirm_stage<2 && now>=confirm_next) {
            const int lifecycle=game_lifecycle_.load(std::memory_order_relaxed);
            if(confirm_stage==0 && (now>=confirm_deadline ||
               !AllowTitleProbe(static_cast<GameBridge::Request>(lifecycle))))
                confirm_stage=2;
            else if(first_frame && sent.connected && *sent.connected) {
                confirm_next=now+std::chrono::milliseconds(120);
                const bool down=confirm_stage==0;
                const auto confirm=session.input_version()>=2
                    ? input::EncodeXInputStateV2(0,down?input::kXButtonA:0,0,0,0,0,0,0)
                    : input::EncodeXInputButtons(0,down?input::kXButtonA:0);
                if(session.send_input(confirm)) {
                    if(session.input_version()>=2) {
                        // The probe sends a complete V2 state, including axes.
                        for(auto& value:sent.values) value=0;
                    }
                    sent.values[0]=down?input::kXButtonA:0;
                    ++confirm_stage;
                    if(confirm_stage==2) progress_("ENDFIELD_ENTRY_CONFIRM_SENT",1,0);
                }
            }
        }
        if(now>=next_input && confirm_stage!=1) {
            cloudgenshin::input::ControllerFrame frame;
            cloudgenshin::input::MotionSample motion;
            {std::lock_guard lock(mutex_);frame=input_;motion=motion_input_;
                frame.state=cloudgenshin::input::ApplyControllerMapping(controller_mapping_,frame.state);}
            const auto previous_connection=sent.connected;
            if(input_channel_ready_)
                input::SendCapturedXInput(sent,controller_handoff,frame,send_input,
                    session.input_version(),controller_layout_);
            gamepad_neutral_pending_=controller_handoff.pending(frame.generation);
            if(!gamepad_neutral_pending_)
                session.send_motion(motion,std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            if(previous_connection!=sent.connected && sent.connected)
                progress_("ENDFIELD_GAMEPAD_CONNECTION_SENT",*sent.connected?1:0,0);
            next_input=now+std::chrono::milliseconds(16);
        }
        // An exhausted receive budget is evidence of pending work, not idle.
        // Yield for the UI/decoder, but do not impose another fixed 2 ms wait.
        if(session.receive_backlog_hint()) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    input_channel_ready_=false;
    settings_ready_=false;
    // Bounded release retry before the session's existing exit drain. A failed
    // neutral must not be overwritten by detach or a later pressed frame.
    if(session.datachannel_open()) {
        if(title_click_stage==2)
            session.send_input(input::EncodeMouseButton(input::kMouseButtonLeft,false,entry_cursor_x,entry_cursor_y));
        if(sent.connected && *sent.connected) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
            do {
                if(input::SendConnectedXInput(sent,true,{},send_input,session.input_version()) &&
                   input::SendConnectedXInput(sent,false,{},send_input,session.input_version())) break;
                session.poll();
                if(!session.running() || !session.datachannel_open()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while(std::chrono::steady_clock::now()<deadline);
            progress_(sent.connected.value_or(false)?"ENDFIELD_GAMEPAD_RELEASE_NOT_QUEUED":
                "ENDFIELD_GAMEPAD_RELEASE_QUEUED",0,0);
        }
    }
    if(!session.last_error().empty()) {
        if(session.last_error()=="Cloud pod authorization rejected") set_status("ENDFIELD_POD_AUTH_REJECTED");
        else if(session.last_error()=="Cloud pod authorization timed out") set_status("ENDFIELD_POD_AUTH_TIMEOUT");
        else if(session.last_error()=="Cloud control channel disconnected") set_status("ENDFIELD_CONTROL_CHANNEL_LOST");
        else set_status(session.kicked_offline() ? "SESSION_KICKED_OFFLINE" : "RTC_CONNECTION_LOST");
    }
    else if(cancelled_) set_status("STREAM_STOPPED_BY_USER");
    if(const auto stats=session.transport_stats()) {
        const auto report=[&](const char* name,auto value) {
            progress_(name,static_cast<int>(std::clamp<long long>(value,INT_MIN,INT_MAX)),0);
        };
        report("RTC_DTLS_LOCAL_ROLE",stats->dtls_role);
        report("RTC_DTLS_LAST_RESULT",stats->dtls_result);
        report("RTC_DTLS_DATAGRAMS_SENT",stats->dtls_datagrams_sent);
        report("RTC_DTLS_DATAGRAMS_RECEIVED",stats->dtls_datagrams_received);
        report("RTC_ICE_LOCAL_CANDIDATES",stats->ice_local_candidates);
        report("RTC_ICE_REMOTE_CANDIDATES",stats->ice_remote_candidates);
        report("RTC_ICE_CANDIDATE_PAIRS",stats->ice_candidate_pairs);
        report("RTC_ICE_REQUESTS_SENT",stats->ice_requests_sent);
        report("RTC_ICE_RESPONSES_RECEIVED",stats->ice_responses_received);
        report("RTC_ICE_RESPONSES_REJECTED",stats->ice_responses_rejected);
        report("RTC_UDP_DATAGRAMS_RECEIVED",stats->udp_datagrams_received);
        report("RTC_UDP_SEND_ERRORS",stats->udp_send_errors);
        report("RTC_UDP_RECEIVE_ERRORS",stats->udp_receive_errors);
        report("RTC_UDP_LAST_ERROR",stats->udp_last_error);
        report("RTC_SCTP_CREATE_ATTEMPTED",stats->sctp_create_attempted);
        report("RTC_SCTP_CREATE_RESULT",stats->sctp_create_result);
    }
    // Startup handshake accounting: retries are expected, a permanently
    // unanswered handshake is what the failure counter must reveal.
    progress_("ENDFIELD_STARTUP_READY",session.startup_ready() ? 1 : 0,0);
    progress_("ENDFIELD_WEILINK_MESSAGES",static_cast<int>(session.welink_messages_received()),0);
    progress_("ENDFIELD_CONTROL_MESSAGES_SENT",static_cast<int>(session.control_messages_sent()),0);
    progress_("ENDFIELD_CONTROL_SEND_FAILURES",static_cast<int>(session.control_send_failures()),0);
    progress_("ENDFIELD_WEILINK_OTHER_SID",static_cast<int>(session.welink_messages_other_sid()),0);
    progress_("ENDFIELD_WEILINK_SID",session.channel_sid(),0);
    if(const auto attempts=session.startup_write_attempts()) {
        progress_("ENDFIELD_STARTUP_SEND_ATTEMPTS",static_cast<int>(attempts),0);
        progress_("ENDFIELD_STARTUP_SEND_FAILURES",
                  static_cast<int>(session.startup_write_failures()),0);
    }
    network_latency_.clear();
    session.stop();
    if(!session.last_error().empty()) {
        const std::string& error=session.last_error();
        int code=0;
        if(error=="Cloud signaling disconnected") code=1;
        else if(error=="WebRTC transport failed") code=2;
        else if(error=="WeLink startup messages timed out") code=3;
        else if(error=="Session was closed by the server") code=4;
        else if(error=="Cloud signaling connect timeout") code=5;
        else if(error=="Cloud signaling answer timeout") code=6;
        else if(error=="Cloud control channel disconnected") code=7;
        progress_("ENDFIELD_SESSION_ERROR",code,0);
    }
}

void EndfieldStreamView::draw(NVGcontext* vg,float x,float y,float width,float height,
                              brls::Style style,brls::FrameContext* context) {
    presentation_debug_.beginDraw();
    const auto now=std::chrono::steady_clock::now();
    if(automated_seconds_ && !exit_requested_ && now>=automatic_stop_check_) {
        automatic_stop_check_=now+std::chrono::seconds(1);
        if(std::ifstream(opennow::AppHomePath()+"/cloudgenshin-autotest.stop").good()) {
            progress_("AUTOTEST_STOP_REQUESTED",0,0);request_exit();
        }
    }
    if(automated_seconds_>0 && !exit_requested_ && now-created_>=std::chrono::seconds(automated_seconds_)) {
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
        exit_hold_={};
        diagnostics_chord_={};diagnostics_chord_captured_=false;
        quality_menu_up_=sampled.state.dpad_up;quality_menu_down_=sampled.state.dpad_down;
        quality_menu_a_=sampled.state.a;quality_menu_b_=sampled.state.b;
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
    std::vector<brls::RawTouchState> touches;
    input_manager->updateTouchStates(&touches);
    const auto finger=std::find_if(touches.begin(),touches.end(),[](const auto& touch){return touch.pressed;});
    const bool touch_down=finger!=touches.end();
    update_overlay_input(sampled.state,{x,y,width,height},touch_down,
        touch_down ? finger->fingerId : 0,touch_down ? overlay::Point{finger->position.x,finger->position.y} : overlay::Point{});
    const auto frame=input_capture_.update(sampled.state,sampled.source,
        diagnostics_visible_ || quality_menu_open_ || floating_ball_.captured() || diagnostics_chord_captured_ || exit_requested_ || finished_);
    auto motion=sampled.motion;
    if(!input_capture_.motion_allowed()) motion.valid=false;
    {std::lock_guard lock(mutex_);input_=frame;motion_input_=motion;}
    hardware_decoder_=media_.hardware_decoder();
    stream_rates_.sample(now_ms,media_.decoded(),video_bytes_.load());
    if(exit_hold_.update(plus,minus,now_ms)) request_exit();
    if(exit_requested_ && finished_ && !pop_requested_) request_activity_pop();
    presentation_debug_.videoBegin();
    bool got=false;
    AVFrameHolder::instance().get_paced([&](AVFrame* frame,std::uint64_t generation,bool){
        if(!frame_metadata_seen_) {
            frame_metadata_seen_=true;
            progress_("VIDEO_FRAME_WIDTH",frame->width,0);
            progress_("VIDEO_FRAME_HEIGHT",frame->height,0);
            progress_("VIDEO_FRAME_PIXEL_FORMAT",frame->format,0);
        }
        if(frame && now>=next_frame_sample_at_) {
            next_frame_sample_at_=now+std::chrono::seconds(5);
            sample_frame_content(frame);
        }
        if(renderer_) {
            const auto before=renderer_->video_render_stats()->rendered_frames;
            const auto render_started=std::chrono::steady_clock::now();
            renderer_->drawLatest(vg,static_cast<int>(width),static_cast<int>(height),frame,VIDEO_FORMAT_H264,generation);
            render_cpu_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-render_started).count());
            got=renderer_->video_render_stats()->rendered_frames!=before;
            if(got) {
                render_submissions_.fetch_add(1,std::memory_order_relaxed);
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
    if(!got) {
        nvgBeginPath(vg);nvgRect(vg,x,y,width,height);nvgFillColor(vg,nvgRGB(8,12,18));nvgFill(vg);
    }
    if(got && !first_render_) {
        first_render_=true;progress_("FIRST_FRAME_RENDER_SUBMITTED_NOT_DISPLAY_PROOF",0,0);
        if(!exit_requested_) show_diagnostics(false);
    }
    if(media_.decoded()>0 && !first_render_) {
        if(first_decoded_at_==std::chrono::steady_clock::time_point{}) {
            first_decoded_at_=now;
            progress_("VIDEO_VIEW_WIDTH",static_cast<int>(width),0);
            progress_("VIDEO_VIEW_HEIGHT",static_cast<int>(height),0);
        }
        if(!render_failure_reported_ && now-first_decoded_at_>std::chrono::seconds(5)) {
            render_failure_reported_=true;set_status("VIDEO_RENDER_NOT_SUBMITTED");
            snapshot_counters();show_diagnostics(true);
        }
    }
    if(finished_ && !finished_panel_shown_) {finished_panel_shown_=true;show_diagnostics(true);}
    if(diagnostics_visible_ && now>=diagnostics_refresh_) {
        if(status_label_->getFullText()!=status) status_label_->setText(status);
        const auto log=ui::LogText(log_snapshot_());
        if(log_label_->getFullText()!=log) log_label_->setText(log);
        diagnostics_refresh_=now+std::chrono::milliseconds(250);
    }
    if(!diagnostics_visible_) {
        nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));
        nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
        nvgFontSize(vg,24);nvgFillColor(vg,nvgRGB(242,242,242));
        if(!got || finished_) nvgText(vg,x+32,y+80,status.c_str(),nullptr);
        if(!got || now-created_<std::chrono::seconds(8)) {
            nvgFontSize(vg,19);
            nvgText(vg,x+32,y+height-42,
                ui::Text("Touch ball: settings / Hold L3+R3: diagnostics / Minus: game map").c_str(),nullptr);
        }
    }
    if(stream_health_.sample(now_ms,overlay::HealthTotals(media_,video_units_,new_render_submissions_,
        video_bytes_,render_cpu_timings_.snapshot(),peer_poll_gaps_.maximum(),&presentation_debug_,peer_poll_gaps_.takeIntervalMaximum()),
        quality_menu_open_ || diagnostics_visible_ || floating_ball_.captured())) {
        const auto& s=stream_health_.window();
        presentation_debug_.publish(s);
        if(s.ui_active) ui_present_skips_.fetch_add(s.display_skips);else gameplay_present_skips_.fetch_add(s.display_skips);
    }
    draw_overlay(vg,{x,y,width,height});
    brls::Box::draw(vg,x,y,width,height,style,context);
    nvgSave(vg);nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));
    overlay::DrawNetworkLatency(vg,x,y,width,height,
        finished_ ? std::nullopt : network_latency_.value(now_ms));
    nvgRestore(vg);
    presentation_debug_.endDraw();
}

} // namespace cloudgenshin::endfield
