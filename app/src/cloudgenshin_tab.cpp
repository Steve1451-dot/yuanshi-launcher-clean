#include "cloudgenshin_tab.hpp"
#include "cloudgenshin_stream_view.hpp"
#include "endfield_stream_view.hpp"
#include "endfield/endfield_store.hpp"
#include "endfield/hg_api.hpp"
#include "endfield/hg_login.hpp"
#include "endfield/saved_game_authorization.hpp"
#include "endfield/queue_cancel_policy.hpp"
#include "endfield/launcher_status.hpp"
#include "mihoyo/launch_flow.hpp"
#include "mihoyo/launcher_status.hpp"
#include "mihoyo/safe_event_log.hpp"
#include "mihoyo/safe_log_writer.hpp"
#include "mihoyo/safe_log_archive.hpp"
#include "mihoyo/login_store.hpp"
#include "mihoyo/passport_login_flow.hpp"
#include "mihoyo/passport_token_resume.hpp"
#include "mihoyo/passport_signin_policy.hpp"
#include "mihoyo/production_profile.hpp"
#include "app_paths.hpp"
#include "app_version.hpp"
#include "home_background_draw.hpp"
#include <borealis/extern/nanovg/stb_image.h>
#include "atomic_file_replace.hpp"
#include "qrcodegen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace cloudgenshin {
namespace {
using SteadySeconds=std::chrono::duration<double>;
double NowSeconds() {
    return SteadySeconds(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Cooperative sleep used by the Endfield QR worker thread.
bool WaitFor(const std::atomic_bool& stop,std::chrono::milliseconds delay) {
    const auto until=std::chrono::steady_clock::now()+delay;
    while(!stop && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return !stop;
}
// Queue wording carries live numbers, so units live here instead of the
// static translation table. Chinese and English are the only shipped UI modes.
std::string EstimateText(int seconds) {
    const int minutes=seconds/60;
    if(ui::chinese) {
        if(seconds<3600) return "约 "+std::to_string(std::max(1,minutes))+" 分钟";
        const int hours=seconds/3600,rest=minutes%60;
        return "约 "+std::to_string(hours)+" 小时"+(rest>0 ? " "+std::to_string(rest)+" 分" : "");
    }
    if(seconds<3600) return "~"+std::to_string(std::max(1,minutes))+" min";
    const int hours=seconds/3600,rest=minutes%60;
    return "~"+std::to_string(hours)+" h"+(rest>0 ? " "+std::to_string(rest)+" min" : "");
}
std::string ElapsedText(int seconds) {
    seconds=std::max(0,seconds);
    const int hours=seconds/3600,minutes=(seconds%3600)/60,rest=seconds%60;
    char buffer[32]{};
    if(ui::chinese) {
        if(hours>0) return std::to_string(hours)+" 小时"+(minutes>0 ? " "+std::to_string(minutes)+" 分" : "");
        if(minutes>0) return std::to_string(minutes)+" 分 "+std::to_string(rest)+" 秒";
        return std::to_string(rest)+" 秒";
    }
    if(hours>0) std::snprintf(buffer,sizeof(buffer),"%d:%02d:%02d",hours,minutes,rest);
    else std::snprintf(buffer,sizeof(buffer),"%d:%02d",minutes,rest);
    return buffer;
}
// Raw selection text: the mihoyo helper maps known ids only, while the
// Endfield entry stores its own "endfield" value in the same file.
std::string LoadGameSelectionText(const std::string& path) {
    for(const auto& file : {path, path + ".bak"}) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::string value(16, '\0');
        in.read(value.data(), static_cast<std::streamsize>(value.size()));
        value.resize(static_cast<std::size_t>(in.gcount()));
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
        if (!value.empty()) return value;
    }
    return {};
}
bool SaveGameSelectionText(const std::string& path, const std::string& value) {
    const std::string temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << value;
    out.flush();
    const bool good = out.good();
    out.close();
    if (!good || out.fail()) return false;
    return opennow::storage::ReplaceWithTemporaryFile(temporary, path);
}

// One restrained palette for the whole page: deep ink background, a single
// cyan accent, and soft semantic colours for state only.
const NVGcolor kInk=nvgRGB(238,244,250);
const NVGcolor kMuted=nvgRGB(136,155,176);
const NVGcolor kAccent=nvgRGB(94,226,235);
const NVGcolor kGreen=nvgRGB(112,231,170);
const NVGcolor kAmber=nvgRGB(246,196,110);
const NVGcolor kRed=nvgRGB(244,145,136);
const NVGcolor kCard=nvgRGBA(15,23,34,205);
const NVGcolor kCardBorder=nvgRGBA(255,255,255,16);
const NVGcolor kTile=nvgRGB(10,17,26);
const NVGcolor kButtonInk=nvgRGB(7,14,20);

// Note: setTextColor() re-runs the button style, which resets
// hideHighlightBackground from the style table, so it must be re-applied
// after every setTextColor() call (focus uses the border only, never a
// full white fill).
void StylePrimary(brls::Button* button) {
    button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    button->setFontSize(17);
    button->setHeight(46);
    button->setCornerRadius(12);
    button->setHighlightCornerRadius(12);
    button->setHighlightPadding(0);
    button->setTextColor(kButtonInk);
    button->setHideHighlightBackground(true);
    button->setBackgroundColor(kAccent);
}
void StyleGhost(brls::Button* button,NVGcolor color) {
    button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    button->setFontSize(15);
    button->setHeight(46);
    button->setCornerRadius(12);
    button->setHighlightCornerRadius(12);
    button->setHighlightPadding(0);
    button->setTextColor(color);
    button->setHideHighlightBackground(true);
    button->setBackgroundColor(nvgRGBA(0,0,0,0));
}
void StyleTab(brls::Button* button) {
    button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    button->setFontSize(16);
    button->setHeight(46);
    button->setCornerRadius(12);
    button->setHighlightCornerRadius(12);
    button->setHighlightPadding(0);
    button->setHideHighlightBackground(true);
}

// Small cyan mark used as the wordmark accent.
class AccentMark final : public brls::View {
public:
    AccentMark() {setWidth(14);setHeight(14);}
    void draw(NVGcontext* vg,float x,float y,float width,float height,brls::Style,brls::FrameContext*) override {
        nvgBeginPath(vg);nvgRoundedRect(vg,x,y,width,height,4);nvgFillColor(vg,kAccent);nvgFill(vg);
        nvgBeginPath(vg);nvgRoundedRect(vg,x+4,y+4,width-8,height-8,2);nvgFillColor(vg,nvgRGB(8,13,20));nvgFill(vg);
    }
};

// Rounded card used for the two game panels.
class Surface final : public brls::Box {
public:
    Surface():brls::Box(brls::Axis::ROW) {}
    brls::View* getDefaultFocus() override {
        // Borealis Box searches children even when the box itself is GONE.
        // Hidden game panels must not contribute zero-position focus targets.
        if(getVisibility()!=brls::Visibility::VISIBLE) return nullptr;
        return brls::Box::getDefaultFocus();
    }
    void draw(NVGcontext* vg,float x,float y,float width,float height,brls::Style style,brls::FrameContext* context) override {
        nvgBeginPath(vg);nvgRoundedRect(vg,x,y,width,height,18);nvgFillColor(vg,kCard);nvgFill(vg);
        nvgBeginPath(vg);nvgRoundedRect(vg,x+0.5f,y+0.5f,width-1,height-1,18);
        nvgStrokeColor(vg,kCardBorder);nvgStrokeWidth(vg,1);nvgStroke(vg);
        brls::Box::draw(vg,x,y,width,height,style,context);
    }
};
}
struct NativeTab::State {
    std::mutex mutex;
    std::atomic_bool cancelled{false},active{false};
    const GameProfile* game=&DefaultGameProfile();
    // Endfield shares the game row but keeps its own credential vault and
    // flow; it is not a mihoyo GameProfile row.
    bool endfield_selected=false;
    std::optional<Authorization> authorization;
    std::optional<dispatch::SdkStartGameParams> startup;
    std::vector<std::uint8_t> qr;
    // Endfield dispatch result: the parsed sdk_msg of an assigned pod. The
    // start worker stores it under the mutex once the pod is granted, and
    // draw() consumes it exactly once to push the stream view.
    std::optional<endfield::AllocatedStream> endfield_stream;
    // UI string key for the Endfield panel status. Written by the QR login
    // worker and read by draw(); the label itself is only touched on the UI
    // thread.
    std::string endfield_status_key="ENDFIELD_SIGN_IN_REQUIRED";
    // Vault state cached for the UI. Kept separate from the transient status
    // key so refreshing a QR for an already signed-in account does not make
    // the panel look signed out.
    bool endfield_ready=false;
    void set_endfield_status(const std::string& key) {
        std::lock_guard lock(mutex);
        endfield_status_key=key;
    }
    void set_endfield_ready(bool ready) {
        std::lock_guard lock(mutex);
        endfield_ready=ready;
    }
    std::string status="READY";
    int status_code=0;
    long status_http=0;
    // Queue progress for the status card. Fed by QUEUED_POSITION events;
    // the estimate is derived from observed movement, never invented.
    struct QueueSample {double at;int position;};
    std::deque<QueueSample> queue_samples;
    bool queue_active=false;
    double queue_started=0;
    int queue_initial=0;
    int queue_estimate_seconds() const {
        if(queue_samples.size()<2) return -1;
        const auto& first=queue_samples.front();
        const auto& last=queue_samples.back();
        const double window=last.at-first.at;
        const int moved=first.position-last.position;
        if(window<20.0 || moved<=0 || last.position<=0) return -1;
        const double remaining=last.position*(window/moved);
        if(remaining>12*3600.0) return -1;
        return static_cast<int>(remaining);
    }
    SafeEventLog log{opennow::kCloudGenshinVersion};
    SafeLogArchive log_archive;
    std::atomic_bool log_worker_unavailable{false};
    // Declared after the log: joins and flushes before log/mutex destruction.
    SafeLogWriter log_writer{[this]{save_log(false);}};
    void queue_log_save() {
        if(!log_writer.request() && !log_worker_unavailable.exchange(true))
            log.append("LOG_BACKGROUND_WRITER_UNAVAILABLE_MEMORY_ONLY");
    }
    void event(const std::string& name,int code=0,long http=0,bool update_status=true) {
        const bool publish=launcher::PublishesStatus(name,
            endfield::PublishesLauncherStatus(name,update_status));
        if (publish) {
            std::lock_guard lock(mutex);
            status=name;status_code=code;status_http=http;
        }
        if(name=="QUEUED_POSITION") {
            std::lock_guard lock(mutex);
            const double now=NowSeconds();
            if(!queue_active) {
                queue_active=true;queue_started=now;queue_initial=0;queue_samples.clear();
            }
            if(code>=0) {
                if(queue_initial<=0 && code>0) queue_initial=code;
                queue_samples.push_back({now,code});
                while(!queue_samples.empty() && now-queue_samples.front().at>900.0) queue_samples.pop_front();
            } else {
                // A missing/malformed rank invalidates the old position and
                // estimate. Do not display stale rank as current progress.
                queue_samples.clear();queue_initial=0;
            }
        } else if(publish && name!="QUEUE_POLL_NETWORK_RETRY" &&
            name.rfind("AUTOTEST_",0)!=0 && name.rfind("LANGUAGE_",0)!=0) {
            std::lock_guard lock(mutex);queue_active=false;
        }
        log.append(name,code,http);
        if (name=="NATIVE_CLOUD_ENTRY_READY" || name=="LOGIN_OK" || name=="LOGIN_SAVED" ||
            name=="LOGIN_RESTORED" || name=="LOGIN_CLEARED" || name=="STREAM_ENTRY" ||
            name=="STREAM_DIAGNOSTICS_SNAPSHOT" || name=="STREAM_WORKER_FINISHED" ||
            name.find("FAILED")!=std::string::npos || name.find("TIMEOUT")!=std::string::npos ||
            name.find("REJECTED")!=std::string::npos ||
            name=="CLOUD_AUTH_SERVICE_UNAVAILABLE" || name.starts_with("AUTOTEST_") ||
            name.starts_with("ENDFIELD_")) {
            queue_log_save(); // Never stall the RTC/UI thread on SD fallback.
        }
    }
    bool save_log(bool manual) {
        opennow::PrepareAppStorage();
        if (manual) log.append("SD_LOG_EXPORT_REQUESTED");
        const bool original_ok=log.save(opennow::AppHomePath()+"/cloudgenshin-safe.log");
        const bool history_ok=log_archive.save(log,opennow::AppHomePath()+"/stream-logs");
        const bool chinese_ok=!manual || log.save(opennow::AppHomePath()+"/cloudgenshin-safe-zh.log",ui::ChineseLogText);
        const bool ok=original_ok && chinese_ok && history_ok;
        if (manual || !ok) log.append(ok ? "SD_LOG_EXPORT_OK" : "SD_LOG_EXPORT_FAILED_MEMORY_LOG_AVAILABLE");
        return ok;
    }
    void qr_code(const std::string& url) {
        std::vector<std::uint8_t> temporary(qrcodegen_BUFFER_LEN_MAX),encoded(qrcodegen_BUFFER_LEN_MAX);
        const bool ok=!url.empty() && qrcodegen_encodeText(url.c_str(),temporary.data(),encoded.data(),
            qrcodegen_Ecc_MEDIUM,qrcodegen_VERSION_MIN,qrcodegen_VERSION_MAX,qrcodegen_Mask_AUTO,true);
        const int modules=ok ? qrcodegen_getSize(encoded.data()) : 0;
        {std::lock_guard lock(mutex);qr=ok ? std::move(encoded) : std::vector<std::uint8_t>{};}
        if (!url.empty() && !ok) event("QR_RENDER_FAILED");
        else if (ok) event("QR_RENDERED modules="+std::to_string(modules)+
            " url_len="+std::to_string(url.size()),0,0,false);
    }
};
class NativeTab::QrView final : public brls::View {
public:
    explicit QrView(std::shared_ptr<State> state):state_(std::move(state)) {setWidth(360);setHeight(380);}
    // UI thread only. Lets the empty tile show a calm signed-in state instead
    // of a blank square once the vault has a token.
    void set_context(bool signed_in) {signed_in_=signed_in;}
    void draw(NVGcontext* vg,float x,float y,float width,float height,brls::Style,brls::FrameContext*) override {
        const float side=std::min(width,height-30.0f);
        const float card_x=x+(width-side)/2.0f,card_y=y;
        const float center_x=x+width/2.0f;
        const auto rounded=[&](float bx,float by,float bw,float bh,NVGcolor color,float radius) {
            nvgBeginPath(vg);nvgRoundedRect(vg,bx,by,bw,bh,radius);nvgFillColor(vg,color);nvgFill(vg);
        };
        nvgFontFaceId(vg,brls::Application::getFont(ui::chinese ? brls::FONT_CHINESE_SIMPLIFIED : brls::FONT_REGULAR));
        std::string status_key;
        bool endfield=false;
        {
            std::lock_guard lock(state_->mutex);
            endfield=state_->endfield_selected;
            status_key=endfield ? state_->endfield_status_key : state_->status;
            if(!state_->qr.empty()) {
                rounded(card_x,card_y,side,side,nvgRGB(255,255,255),18);
                const int size=qrcodegen_getSize(state_->qr.data());
                const float cell=std::floor((side-20.0f)/static_cast<float>(size+8));
                const float qr_side=cell*(size+8);
                const float left=card_x+(side-qr_side)/2.0f,top=card_y+(side-qr_side)/2.0f;
                nvgBeginPath(vg);
                for (int row=0;row<size;++row) for (int column=0;column<size;++column)
                    if (qrcodegen_getModule(state_->qr.data(),column,row))
                        nvgRect(vg,left+(column+4)*cell,top+(row+4)*cell,cell,cell);
                nvgFillColor(vg,nvgRGB(0,0,0));nvgFill(vg);
                nvgFontSize(vg,13);nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
                nvgFillColor(vg,kMuted);
                nvgText(vg,center_x,card_y+side+17,
                    ui::Text(endfield ? "Scan with Skland" : "Scan with MiYoushe").c_str(),nullptr);
                return;
            }
        }
        rounded(card_x,card_y,side,side,kTile,18);
        nvgBeginPath(vg);nvgRoundedRect(vg,card_x+0.5f,card_y+0.5f,side-1,side-1,18);
        nvgStrokeColor(vg,kCardBorder);nvgStrokeWidth(vg,1);nvgStroke(vg);
        const float glyph_y=card_y+side/2.0f-24;
        if(signed_in_) {
            nvgBeginPath(vg);nvgCircle(vg,center_x,glyph_y,32);
            nvgStrokeColor(vg,nvgRGBA(112,231,170,90));nvgStrokeWidth(vg,1.5f);nvgStroke(vg);
            nvgBeginPath(vg);nvgMoveTo(vg,center_x-13,glyph_y+1);nvgLineTo(vg,center_x-3,glyph_y+11);
            nvgLineTo(vg,center_x+15,glyph_y-10);nvgStrokeColor(vg,kGreen);nvgStrokeWidth(vg,4);
            nvgLineCap(vg,NVG_ROUND);nvgLineJoin(vg,NVG_ROUND);nvgStroke(vg);
            nvgFontSize(vg,18);nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kInk);
            nvgText(vg,center_x,glyph_y+54,ui::Text(endfield ? "Credentials saved" : "Signed in").c_str(),nullptr);
            nvgFontSize(vg,12.5f);nvgFillColor(vg,kMuted);
            nvgText(vg,center_x,glyph_y+78,
                ui::Text(endfield ? "Saved login is not a game connection." : "Credentials saved").c_str(),nullptr);
        } else {
            const float a=22;
            nvgBeginPath(vg);
            nvgMoveTo(vg,center_x-a,glyph_y-a+10);nvgLineTo(vg,center_x-a,glyph_y-a);nvgLineTo(vg,center_x-a+10,glyph_y-a);
            nvgMoveTo(vg,center_x+a-10,glyph_y-a);nvgLineTo(vg,center_x+a,glyph_y-a);nvgLineTo(vg,center_x+a,glyph_y-a+10);
            nvgMoveTo(vg,center_x+a,glyph_y+a-10);nvgLineTo(vg,center_x+a,glyph_y+a);nvgLineTo(vg,center_x+a-10,glyph_y+a);
            nvgMoveTo(vg,center_x-a+10,glyph_y+a);nvgLineTo(vg,center_x-a,glyph_y+a);nvgLineTo(vg,center_x-a,glyph_y+a-10);
            nvgStrokeColor(vg,nvgRGBA(94,226,235,170));nvgStrokeWidth(vg,2.5f);
            nvgLineCap(vg,NVG_ROUND);nvgLineJoin(vg,NVG_ROUND);nvgStroke(vg);
            nvgFontSize(vg,18);nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kInk);
            nvgText(vg,center_x,glyph_y+54,ui::Text("Scan to sign in").c_str(),nullptr);
            const bool failed=status_key.find("FAILED")!=std::string::npos ||
                status_key.find("REJECTED")!=std::string::npos ||
                status_key.find("EXPIRED")!=std::string::npos;
            nvgFontSize(vg,12.5f);nvgFillColor(vg,kMuted);
            nvgText(vg,center_x,glyph_y+78,
                ui::Text(failed ? "Tap refresh to get a new code" : "Waiting for the QR code…").c_str(),nullptr);
        }
    }
private:
    std::shared_ptr<State> state_;
    bool signed_in_=false;
};
class NativeTab::StatusCard final : public brls::View {
public:
    explicit StatusCard(std::shared_ptr<State> state):state_(std::move(state)) {setHeight(92);setMarginTop(16);}
    void draw(NVGcontext* vg,float x,float y,float width,float height,brls::Style,brls::FrameContext*) override {
        std::string status,ekey,game_label;
        bool working=false,queuing=false,signed_in=false,endfield=false,ready=false;
        int position=-1,estimate=-1,elapsed=0;
        int status_code=0;
        long status_http=0;
        float progress=0.0f;bool progress_valid=false;
        {
            std::lock_guard lock(state_->mutex);
            status=state_->status;working=state_->active;
            status_code=state_->status_code;status_http=state_->status_http;
            signed_in=state_->authorization.has_value();
            endfield=state_->endfield_selected;
            ekey=state_->endfield_status_key;
            ready=state_->endfield_ready;
            game_label=endfield ? ui::Text("Endfield Cloud")
                : ui::Text(ui::chinese ? state_->game->display_name : state_->game->latin_name);
            if(state_->queue_active) {
                queuing=true;
                position=state_->queue_samples.empty() ? -1 : state_->queue_samples.back().position;
                estimate=state_->queue_estimate_seconds();
                elapsed=static_cast<int>(NowSeconds()-state_->queue_started);
                if(state_->queue_initial>0 && position>=0) {
                    progress=1.0f-static_cast<float>(position)/static_cast<float>(state_->queue_initial);
                    progress=std::clamp(progress,0.0f,1.0f);progress_valid=true;
                }
            }
        }
        const auto has=[&](const char* needle){return status.find(needle)!=std::string::npos;};
        nvgSave(vg);nvgIntersectScissor(vg,x,y,width,height);
        const auto rounded=[&](float bx,float by,float bw,float bh,NVGcolor color,float radius) {
            nvgBeginPath(vg);nvgRoundedRect(vg,bx,by,bw,bh,radius);nvgFillColor(vg,color);nvgFill(vg);
        };
        rounded(x,y,width,height,kCard,18);
        nvgBeginPath(vg);nvgRoundedRect(vg,x+0.5f,y+0.5f,width-1,height-1,18);
        nvgStrokeColor(vg,kCardBorder);nvgStrokeWidth(vg,1);nvgStroke(vg);
        nvgFontFaceId(vg,brls::Application::getFont(ui::chinese ? brls::FONT_CHINESE_SIMPLIFIED : brls::FONT_REGULAR));
        const float pad=22;
        if(queuing) {
            nvgBeginPath(vg);nvgCircle(vg,x+pad+5,y+32,5);nvgFillColor(vg,kAmber);nvgFill(vg);
            nvgFontSize(vg,20);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kInk);
            nvgText(vg,x+pad+20,y+32,ui::Text("In queue").c_str(),nullptr);
            nvgFontSize(vg,13);nvgTextAlign(vg,NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kMuted);
            const auto waited=ui::Text("Waited")+" "+ElapsedText(elapsed);
            nvgText(vg,x+width-pad,y+32,waited.c_str(),nullptr);
            const std::string number=position>=0 ? std::to_string(position) : "--";
            nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kInk);nvgFontSize(vg,30);
            nvgText(vg,x+pad,y+64,number.c_str(),nullptr);
            const float number_width=nvgTextBounds(vg,0,0,number.c_str(),nullptr,nullptr);
            nvgFontSize(vg,13);nvgFillColor(vg,kMuted);
            nvgText(vg,x+pad+number_width+8,y+68,ui::Text("ahead").c_str(),nullptr);
            const auto wait=estimate>=0 ? ui::Text("Estimated wait")+" "+EstimateText(estimate)
                : ui::Text("Estimating…");
            nvgTextAlign(vg,NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
            nvgText(vg,x+width-pad,y+66,wait.c_str(),nullptr);
            const float bar_x=x+pad,bar_y=y+height-11,bar_w=width-pad*2;
            rounded(bar_x,bar_y,bar_w,4,nvgRGBA(255,255,255,22),2);
            if(progress_valid && progress>0.001f)
                rounded(bar_x,bar_y,std::max(4.0f,bar_w*progress),4,nvgRGBA(94,226,235,210),2);
            nvgRestore(vg);
            return;
        }
        NVGcolor tone=kAccent;
        std::string title=ui::Text("Sign in required");
        std::string detail=ui::Text(endfield ? "Scan the QR with Skland" : "Scan the QR with MiYoushe");
        const auto set=[&](const char* title_key,const char* detail_key,NVGcolor color) {
            title=ui::Text(title_key);detail=ui::Text(detail_key);tone=color;
        };
        if(const auto notice=endfield ? endfield::DescribeLauncherStatus(status) : std::nullopt) {
            set(notice->title,notice->detail,notice->needs_action ? kAmber : kAccent);
            if(notice->needs_action) {
                std::string numbers="["+ui::Text("Code")+" "+std::to_string(status_code);
                if(status_http) numbers+=", HTTP "+std::to_string(status_http);
                detail=numbers+"] "+detail;
            }
        } else if(status=="SAVED_LOGIN_UNREADABLE" || ekey=="ENDFIELD_TOKEN_UNREADABLE" || has("TOKEN_UNREADABLE")) {
            set("Login unreadable","Clear it and sign in again",kAmber);
        } else if(has("QR_") && has("EXPIRED")) {
            set("QR expired","Tap refresh to get a new code",kAmber);
        } else if(status=="QUEUE_EXPIRED") {
            set("Queue ended","You can start again",kAmber);
        } else if(status=="QUEUE_ABANDONED_TOO_LONG") {
            set("Long queue stopped","Over 100 players ahead. Login is kept; no need to scan again.",kAmber);
            detail=ui::Text("Players ahead")+": "+std::to_string(status_code)+". "+detail;
        } else if(has("MAINTENANCE")) {
            set("Game maintenance","Please try again later",kAmber);
        } else if(status=="CANCEL_REQUESTED" || has("CANCELLED")) {
            set("Cancelled","You can start again",kMuted);
        } else if(status=="CLOUD_AUTH_SERVICE_UNAVAILABLE") {
            set("Cloud play not enabled","This account is not enabled for this game",kRed);
        } else if(launcher::DispatchFailed(status) || has("FAILED") || has("REJECTED") || has("TIMEOUT") || has("CONFLICT") ||
                  has("NO_AVAILABLE") || has("KICKED_OUT")) {
            set("Something went wrong","Check your connection and try again",kRed);
            // Source-defined stage plus numeric codes, never response text or
            // credentials. In particular, HTTP/transport failures must not
            // fall through to "Signed in" after queue cleanup finishes.
            detail=ui::Text(status)+" ["+ui::Text("Code")+" "+std::to_string(status_code);
            if(status_http) detail+=", HTTP "+std::to_string(status_http);
            detail+="]";
        } else if(status=="ENDFIELD_STREAM_PENDING") {
            set("Stream coming soon","Login is saved; streaming will follow",kAccent);
        } else if(status=="QR_FETCH" || status=="ENDFIELD_QR_FETCH" || status=="ENDFIELD_QR_FETCHING") {
            set("Getting QR code","Contacting the official service",kAccent);
        } else if(status=="QR_WAITING" || status=="ENDFIELD_QR_WAITING") {
            set("Waiting for scan",endfield ? "Scan the QR with Skland" : "Scan the QR with MiYoushe",kAccent);
        } else if(has("QR_SCANNED")) {
            set("Confirm on your phone","Then the login is saved automatically",kAccent);
        } else if(status=="ENDFIELD_QR_EXCHANGING" || status=="QR_ACCOUNT_CONFIRMED_COMBO_EXCHANGE") {
            set("Signing in","Verifying your account with the official service",kAccent);
        } else if(working) {
            if(has("DISPATCH") || has("QUEUE")) set("Joining the queue","The official service is preparing your session",kAccent);
            else if(has("CHECK_SERVICE") || has("PING") || has("NODE") || has("GET_NODES"))
                set("Choosing a server","Measuring the closest node",kAccent);
            else if(has("STREAM")) set("Starting the stream","The game screen will appear shortly",kAccent);
            else if(has("LOGIN") || has("CLOUD_ACCOUNT") || has("RESUME"))
                set("Signing in","Verifying your account with the official service",kAccent);
            else set("Working","Please wait a moment",kAccent);
        } else if(endfield) {
            if(ready) set("Signed in","Credentials stay on this SD card",kGreen);
            else set("Sign in required","Scan the QR with Skland",kAccent);
        } else if(signed_in) {
            set("Signed in","Credentials stay on this SD card",kGreen);
        }
        nvgBeginPath(vg);nvgCircle(vg,x+pad+5,y+33,5);nvgFillColor(vg,tone);nvgFill(vg);
        nvgSave(vg);nvgIntersectScissor(vg,x+pad+20,y,std::max(1.0f,width-pad-20-170),height);
        nvgFontSize(vg,20);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kInk);
        nvgText(vg,x+pad+20,y+33,title.c_str(),nullptr);
        nvgFontSize(vg,13);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kMuted);
        nvgText(vg,x+pad+20,y+59,detail.c_str(),nullptr);
        nvgRestore(vg);
        nvgFontSize(vg,12);nvgTextAlign(vg,NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,kMuted);
        nvgText(vg,x+width-pad,y+33,game_label.c_str(),nullptr);
        nvgRestore(vg);
    }
private:
    std::shared_ptr<State> state_;
};

NativeTab::NativeTab():brls::Box(brls::Axis::COLUMN),state_(std::make_shared<State>()),
    automatic_(AutoTestEnabled(opennow::AppHomePath()+"/cloudgenshin-autotest.txt"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) {
    ui::LoadLanguage(opennow::AppHomePath()+"/cloudgenshin-language.txt");
    displayed_chinese_=ui::chinese;
    state_->endfield_selected=LoadGameSelectionText(opennow::AppHomePath()+"/cloudgenshin-game.txt")=="endfield";
    state_->game=&LoadGameSelection(opennow::AppHomePath()+"/cloudgenshin-game.txt");
    // The platform loads the system Chinese font (or the user's replacement).
    // Register it also when a custom font took the platform's alternate branch.
    const int regular_font=brls::Application::getFont(brls::FONT_REGULAR);
    const int chinese_font=brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    if(regular_font>=0 && chinese_font>=0 && regular_font!=chinese_font)
        nvgAddFallbackFontId(brls::Application::getNVGContext(),regular_font,chinese_font);
    setGrow(1);setPadding(22,40,16,40);setBackgroundColor(nvgRGB(8,13,20));

    // Header: wordmark + language toggle. No debug copy on the main page.
    auto* heading=new brls::Box(brls::Axis::ROW);heading->setHeight(44);
    heading->setAlignItems(brls::AlignItems::CENTER);addView(heading);
    auto* mark=new AccentMark();mark->setMarginRight(10);heading->addView(mark);
    auto* cloud=new brls::Label();cloud->setText("yuanshi ");cloud->setFontSize(23);cloud->setTextColor(kMuted);
    heading->addView(cloud);
    auto* genshin=new brls::Label();genshin->setText("launcher");genshin->setFontSize(23);
    genshin->setTextColor(kInk);genshin->setGrow(1);heading->addView(genshin);
    auto* language=new brls::Button();language->setText("中文 / English");language->setWidth(150);
    StyleGhost(language,kMuted);language->setFontSize(14);language->setHeight(34);
    language->registerClickAction([this](brls::View*) {
        ui::chinese=!ui::chinese;texts_.refresh();refresh_game_buttons();
        updateActionHint(brls::BUTTON_START,ui::Text("Exit"));
        opennow::PrepareAppStorage();
        state_->event(ui::SaveLanguage(opennow::AppHomePath()+"/cloudgenshin-language.txt") ? "LANGUAGE_SAVED" : "LANGUAGE_SAVE_FAILED");
        return true;
    });heading->addView(language);

    // Game tabs: three equal segments, one restrained selected tint.
    auto* game_row=new brls::Box(brls::Axis::ROW);game_row->setHeight(46);game_row->setMarginTop(16);addView(game_row);
    for(std::size_t index=0;index<sizeof(kGameProfiles)/sizeof(kGameProfiles[0]);++index) {
        auto* button=new brls::Button();StyleTab(button);button->setGrow(1);button->setMarginRight(10);
        button->registerClickAction([this,profile=&kGameProfiles[index]](brls::View*){select_game(*profile);return true;});
        game_buttons_[index]=button;game_row->addView(button);
    }
    {
        auto* button=new brls::Button();StyleTab(button);button->setGrow(1);
        button->registerClickAction([this](brls::View*){select_endfield();return true;});
        endfield_button_=button;game_row->addView(button);
    }

    // One status line. Friendly copy, colour-coded state, queue progress.
    addView(new StatusCard(state_));

    // Mihoyo panel: game identity, one adaptive primary action, QR/account tile.
    {
        auto* row=new Surface();row->setGrow(1);row->setMarginTop(16);row->setPadding(26);
        mihoyo_row_=row;addView(row);
        auto* details=new brls::Box(brls::Axis::COLUMN);details->setGrow(1);
        details->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);row->addView(details);
        auto* top=new brls::Box(brls::Axis::COLUMN);details->addView(top);
        auto* eyebrow=new brls::Label();eyebrow->setFontSize(13);eyebrow->setTextColor(kAccent);
        eyebrow->setMarginBottom(8);texts_.add(eyebrow,"Cloud gaming");top->addView(eyebrow);
        hero_game_=new brls::Label();hero_game_->setFontSize(34);hero_game_->setTextColor(kInk);
        hero_game_->setMarginBottom(8);top->addView(hero_game_);
        hero_hint_=new brls::Label();hero_hint_->setFontSize(15);hero_hint_->setTextColor(kMuted);
        hero_hint_->setSingleLine(false);
        top->addView(hero_hint_);
        auto* actions=new brls::Box(brls::Axis::ROW);actions->setHeight(46);
        actions->setAlignItems(brls::AlignItems::CENTER);details->addView(actions);
        mihoyo_primary_=new brls::Button();mihoyo_primary_->setWidth(180);StylePrimary(mihoyo_primary_);
        mihoyo_primary_->registerClickAction([this](brls::View*) {
            if(state_->active) {
                automatic_.stop();state_->cancelled=true;state_->qr_code("");
                state_->event("CANCEL_REQUESTED");
            } else {
                bool signed_in=false,update_web=false;
                {std::lock_guard lock(state_->mutex);
                    signed_in=state_->authorization.has_value();
                    update_web=auth::NeedsWebLoginUpdate(state_->game->id,signed_in,
                        signed_in && auth::ValidPassportMid(state_->authorization->passport_mid),state_->status);
                }
                begin(update_web ? false : signed_in,false,update_web);
            }
            return true;
        });actions->addView(mihoyo_primary_);
        mihoyo_logout_=new brls::Button();mihoyo_logout_->setMarginLeft(10);
        texts_.add(mihoyo_logout_,"Sign out");StyleGhost(mihoyo_logout_,kRed);
        mihoyo_logout_->registerClickAction([this](brls::View*){logout();return true;});
        actions->addView(mihoyo_logout_);
        // One primary action: Star Rail upgrades legacy login through the
        // official web flow; Genshin keeps its existing game-QR entry.
        auto* qrcol=new brls::Box(brls::Axis::COLUMN);qrcol->setWidth(360);qrcol->setMarginLeft(26);
        qrcol->setAlignItems(brls::AlignItems::CENTER);qrcol->setJustifyContent(brls::JustifyContent::CENTER);
        row->addView(qrcol);
        mihoyo_qr_=new QrView(state_);qrcol->addView(mihoyo_qr_);
    }

    // Endfield panel: same geometry, native Hypergryph QR login.
    {
        auto* panel=new Surface();panel->setGrow(1);panel->setMarginTop(16);panel->setPadding(26);
        endfield_panel_=panel;addView(panel);
        auto* info=new brls::Box(brls::Axis::COLUMN);info->setGrow(1);
        info->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);panel->addView(info);
        auto* top=new brls::Box(brls::Axis::COLUMN);info->addView(top);
        auto* eyebrow=new brls::Label();eyebrow->setFontSize(13);eyebrow->setTextColor(kAccent);
        eyebrow->setMarginBottom(8);texts_.add(eyebrow,"Cloud gaming");top->addView(eyebrow);
        auto* title=new brls::Label();texts_.add(title,"Endfield Cloud");title->setFontSize(34);
        title->setTextColor(kInk);title->setMarginBottom(8);top->addView(title);
        endfield_hint_=new brls::Label();endfield_hint_->setFontSize(15);
        endfield_hint_->setSingleLine(false);
        endfield_hint_->setTextColor(kMuted);top->addView(endfield_hint_);
        auto* actions=new brls::Box(brls::Axis::ROW);actions->setHeight(46);
        actions->setAlignItems(brls::AlignItems::CENTER);info->addView(actions);
        endfield_primary_=new brls::Button();endfield_primary_->setWidth(180);StylePrimary(endfield_primary_);
        endfield_primary_->registerClickAction([this](brls::View*) {
            if(endfield_start_job_ && endfield_start_job_->running) {cancel_endfield_start();return true;}
            bool ready=false;
            {std::lock_guard lock(state_->mutex);ready=state_->endfield_ready;}
            if(ready) start_endfield(); else endfield_qr_login(true);
            return true;
        });actions->addView(endfield_primary_);
        endfield_clear_=new brls::Button();endfield_clear_->setMarginLeft(10);
        texts_.add(endfield_clear_,"Sign out");StyleGhost(endfield_clear_,kRed);
        endfield_clear_->registerClickAction([this](brls::View*){clear_endfield_login();return true;});
        actions->addView(endfield_clear_);
        auto* qrcol=new brls::Box(brls::Axis::COLUMN);qrcol->setWidth(360);qrcol->setMarginLeft(26);
        qrcol->setAlignItems(brls::AlignItems::CENTER);qrcol->setJustifyContent(brls::JustifyContent::CENTER);
        panel->addView(qrcol);
        endfield_qr_=new QrView(state_);qrcol->addView(endfield_qr_);
    }
    refresh_game_buttons();

    // Footer: version and the one global key hint.
    auto* footer=new brls::Box(brls::Axis::ROW);footer->setHeight(20);footer->setMarginTop(10);addView(footer);
    auto* version=new brls::Label();version->setText("v"+std::string(opennow::kCloudGenshinVersion));
    version->setFontSize(12);version->setTextColor(kMuted);version->setGrow(1);footer->addView(version);
    auto* exit_hint=new brls::Label();texts_.add(exit_hint,"Exit","START  ");
    exit_hint->setFontSize(12);exit_hint->setTextColor(kMuted);footer->addView(exit_hint);
    state_->event("NATIVE_CLOUD_ENTRY_READY");
    state_->event("LOG_MEMORY_READY_SAFE_SD_AUTOSAVE_ENABLED");
    state_->event("LOG_HISTORY_LIMIT_MIB",8,0,false);
    if(automatic_.enabled()) state_->event("AUTOTEST_HARNESS_QUEUE_WAIT_30M");
    registerAction(ui::Text("Exit"),brls::BUTTON_START,[this](brls::View*) {
        // Unattended runs can last through a long normal queue; a stray
        // PLUS press must not silently end the evidence collection.
        if(automatic_.enabled()) {
            state_->event("AUTOTEST_EXIT_IGNORED");
            return true;
        }
        state_->cancelled=true;
        if(endfield_login_job_) endfield_login_job_->stop=true;
        if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
        if(endfield_start_job_) endfield_start_job_->stop=true;
        if(endfield_start_thread_.joinable()) endfield_start_thread_.join();
        if(worker_.joinable()) worker_.join();
        brls::Application::quit();return true;
    });
    bool endfield_at_boot=false;
    {std::lock_guard lock(state_->mutex);endfield_at_boot=state_->endfield_selected;}
    if(endfield_at_boot) {
        refresh_endfield(true);
        // Native QR login instead of the old manual token import: fetch a QR
        // when Endfield was the last selected game and no login is saved.
        opennow::PrepareAppStorage();
        if(endfield::store::Load(opennow::AppHomePath()).status!=endfield::store::LoadStatus::Ready)
            endfield_qr_login(false);
        // Same one-shot autotest opt-in as the mihoyo normal queue: a vault
        // that is already valid immediately starts one cloud session; the
        // stream view itself stops it after its automated limit.
        else if(automatic_.enabled())
            start_endfield();
    }
    else {
        if(AutoTestPassportLoginEnabled(opennow::AppHomePath()+"/cloudgenshin-autotest.web-login")) {
            automatic_.stop();state_->event("AUTOTEST_PASSPORT_LOGIN_ONLY");
            begin(false,false,true);
        } else {
            if(automatic_.enabled()) state_->event("AUTOTEST_ENABLED_NORMAL_QUEUE_ONCE");
            begin(false,!automatic_.enabled());
        }
    }
}
NativeTab::~NativeTab() {
    ui_lifetime_.reset();
    if(background_image_>0) nvgDeleteImage(brls::Application::getNVGContext(),background_image_);
    state_->cancelled=true;
    if(endfield_login_job_) endfield_login_job_->stop=true;
    if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
    if(endfield_start_job_) endfield_start_job_->stop=true;
    if(endfield_start_thread_.joinable()) endfield_start_thread_.join();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(state_->mutex);
    state_->authorization.reset();state_->startup.reset();state_->qr.clear();
}
void NativeTab::willDisappear(bool reset_state) {
    // Free the homepage texture before allocating stream/decoder surfaces.
    if(background_image_>0) nvgDeleteImage(brls::Application::getNVGContext(),background_image_);
    background_image_=0;background_game_=-1;
    state_->cancelled=true;
    if(endfield_login_job_) endfield_login_job_->stop=true;
    if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
    // An in-flight start flow is cancelled here; joining is safe because the
    // stream view is only pushed after the worker finished.
    if(endfield_start_job_) endfield_start_job_->stop=true;
    if(endfield_start_thread_.joinable()) endfield_start_thread_.join();
    endfield_login_job_.reset();
    state_->qr_code("");
    // Abandoning an in-flight scan leaves no QR behind; show the vault state
    // again instead of a stale "waiting for scan" label on return.
    if(state_->endfield_selected) refresh_endfield(false);
    brls::Box::willDisappear(reset_state);
}
void NativeTab::begin(bool launch,bool restore_only,bool passport) {
    {std::lock_guard lock(state_->mutex);if(state_->endfield_selected) return;}
    if (state_->active.exchange(true)) return;
    if (worker_.joinable()) worker_.join();
    state_->cancelled=false;
    const auto state=state_;
    worker_=std::thread([state,launch,restore_only,passport]{
        struct Finish {std::atomic_bool& active;~Finish(){active=false;}} finish{state->active};
        // The selected game is fixed for the whole operation so a profile
        // table entry cannot change mid-flight.
        const GameProfile* game=nullptr;
        {std::lock_guard lock(state->mutex);game=state->game;}
        const Progress progress=[state](const std::string& event,int code,long http){
            state->event(event,code,http);
            // Low-frequency stage boundaries, not per-packet logging.
            if(event=="QR_WAITING" || event=="SIGN_IN_REQUIRED" || event=="SAVED_LOGIN_RETRY_LATER" ||
               event=="QUEUED_POSITION" || event=="CHECK_SERVICE") state->queue_log_save();
        };
        try {
            if(passport) {
                // A failed/cancelled web sign-in never wipes the previous
                // in-memory login or its encrypted vault. Replace on success.
                state->qr_code("");
                {std::lock_guard lock(state->mutex);state->startup.reset();}
                auto authorization=LoginPassport(*game,state->cancelled,
                    [state](const std::string& url){state->qr_code(url);},progress);
                if(state->cancelled || !authorization) return;
                opennow::PrepareAppStorage();
                state->event(login_store::Save(opennow::AppHomePath(),*game,*authorization)
                    ? "LOGIN_SAVED" : "LOGIN_SAVE_FAILED_SESSION_ACTIVE");
                std::lock_guard lock(state->mutex);state->authorization=std::move(authorization);
                return;
            }
            if (launch) {
                std::optional<Authorization> authorization;
                {std::lock_guard lock(state->mutex);authorization=state->authorization;}
                if (!authorization) state->event("START_BLOCKED_SIGN_IN_REQUIRED",0,0,false);
                else {
                    const bool web_context=auth::UsesPassportQr(game->id) && auth::ValidPassportMid(authorization->passport_mid);
                    if(web_context || AutoTestStarRailWebResumeEnabled(opennow::AppHomePath(),authorization->combo.app_id)) {
                        // Refresh the proper H5 grant before allocation. A
                        // typed web session never guesses another token kind.
                        const auto source=!web_context && AutoTestStarRailWebGameTokenEnabled(opennow::AppHomePath(),authorization->combo.app_id)
                            ? PassportResumeSource::GameToken : PassportResumeSource::SToken;
                        auto web=ResumePassportFromAccount(*game,*authorization,state->cancelled,progress,source);
                        if(!web || state->cancelled) return;
                        if(ResumeLogin(*game,*web,state->cancelled,progress)!=ResumeStatus::Valid || state->cancelled) return;
                        // Only a complete, same-account grant validated by the
                        // cloud service may replace the encrypted saved login.
                        opennow::PrepareAppStorage();
                        state->event(login_store::Save(opennow::AppHomePath(),*game,*web) ?
                            "LOGIN_SAVED" : "LOGIN_SAVE_FAILED_SESSION_ACTIVE");
                        authorization=std::move(web);
                        {std::lock_guard lock(state->mutex);state->authorization=authorization;}
                    }
                    auto startup=Launch(*game,*authorization,state->cancelled,progress,
                        {.wait_in_long_normal_queue=AutoTestLongNormalQueueEnabled(opennow::AppHomePath())});
                    std::lock_guard lock(state->mutex);
                    if (!state->cancelled) state->startup=std::move(startup);
                }
            } else {
                state->qr_code("");
                std::optional<Authorization> existing;
                {std::lock_guard lock(state->mutex);existing=state->authorization;state->startup.reset();}
                if(!existing) {
                    auto saved=login_store::Load(opennow::AppHomePath(),*game);
                    if(saved.status==login_store::LoadStatus::Unreadable) {
                        state->event("SAVED_LOGIN_UNREADABLE");
                        if(restore_only) return;
                    }
                    existing=std::move(saved.authorization);
                }
                if(existing) {
                    std::optional<Authorization> refreshed;
                    const auto result=ResumeLogin(*game,*existing,state->cancelled,progress,&refreshed);
                    if(result==ResumeStatus::Valid) {
                        if(state->cancelled) return;
                        const bool was_refreshed=refreshed.has_value();
                        if(was_refreshed) existing=std::move(refreshed);
                        if(!restore_only || was_refreshed) {
                            opennow::PrepareAppStorage();
                            state->event(login_store::Save(opennow::AppHomePath(),*game,*existing) ? "LOGIN_SAVED" : "LOGIN_SAVE_FAILED_SESSION_ACTIVE");
                        }
                        std::lock_guard lock(state->mutex);
                        if(!state->cancelled) state->authorization=std::move(existing);
                        return;
                    }
                    {std::lock_guard lock(state->mutex);state->authorization.reset();}
                    if(result==ResumeStatus::Cancelled || result==ResumeStatus::RetryLater || restore_only) return;
                }
                if(restore_only) {state->event("SIGN_IN_REQUIRED");return;}
                {std::lock_guard lock(state->mutex);state->authorization.reset();}
                const auto render=[state](const std::string& url){state->qr_code(url);};
                auto authorization=auth::UsesPassportQr(game->id)
                    ? LoginPassport(*game,state->cancelled,render,progress)
                    : Login(*game,state->cancelled,render,progress);
                if(state->cancelled) return;
                if(authorization) {
                    opennow::PrepareAppStorage();
                    state->event(login_store::Save(opennow::AppHomePath(),*game,*authorization) ? "LOGIN_SAVED" : "LOGIN_SAVE_FAILED_SESSION_ACTIVE");
                }
                std::lock_guard lock(state->mutex);
                if (!state->cancelled) state->authorization=std::move(authorization);
            }
        } catch (...) {state->qr_code("");state->event("NATIVE_OPERATION_FAILED");}
    });
}
void NativeTab::select_game(const GameProfile& profile) {
    if(state_->active) {state_->event("GAME_SWITCH_BLOCKED_WORKING",0,0,false);return;}
    {
        std::lock_guard lock(state_->mutex);
        if(state_->game==&profile && !state_->endfield_selected) return;
        state_->game=&profile;
        state_->endfield_selected=false;
    }
    if(endfield_login_job_) endfield_login_job_->stop=true;
    if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
    endfield_login_job_.reset();
    opennow::PrepareAppStorage();
    state_->event(SaveGameSelection(opennow::AppHomePath()+"/cloudgenshin-game.txt",profile)
        ? "GAME_SELECTED" : "GAME_SELECTION_SAVE_FAILED");
    // Each miHoYo game has its own encrypted vault entry. Replacing the
    // in-memory authorization here made switching back look signed out until a
    // restart, so resolve the selected game's saved login immediately instead.
    std::optional<Authorization> selected_authorization;
    if(const auto loaded=login_store::Load(opennow::AppHomePath(),profile);
       loaded.status==login_store::LoadStatus::Ready && loaded.authorization)
        selected_authorization=std::move(loaded.authorization);
    {
        std::lock_guard lock(state_->mutex);
        state_->authorization=std::move(selected_authorization);
        state_->startup.reset();
        // Leaving the Endfield panel must not resurrect an old pod grant.
        state_->endfield_stream.reset();
    }
    state_->qr_code("");
    refresh_game_buttons();
    // Verify the selected game's saved login; if there is none, the usual QR
    // path runs for that game. This never reuses another game's credentials.
    begin(false,!automatic_.enabled());
}
void NativeTab::select_endfield() {
    if(state_->active) {state_->event("GAME_SWITCH_BLOCKED_WORKING",0,0,false);return;}
    {
        std::lock_guard lock(state_->mutex);
        if(state_->endfield_selected) return;
        state_->endfield_selected=true;
    }
    opennow::PrepareAppStorage();
    state_->event(SaveGameSelectionText(opennow::AppHomePath()+"/cloudgenshin-game.txt","endfield")
        ? "GAME_SELECTED" : "GAME_SELECTION_SAVE_FAILED");
    {
        std::lock_guard lock(state_->mutex);
        state_->authorization.reset();state_->startup.reset();
        // A pod grant from an earlier cancelled flow is never valid to push.
        state_->endfield_stream.reset();
    }
    state_->qr_code("");
    refresh_game_buttons();
    refresh_endfield(true);
    opennow::PrepareAppStorage();
    if(endfield::store::Load(opennow::AppHomePath()).status!=endfield::store::LoadStatus::Ready)
        endfield_qr_login(false);
}
void NativeTab::refresh_endfield(bool announce) {
    opennow::PrepareAppStorage();
    const auto loaded=endfield::store::Load(opennow::AppHomePath());
    const bool ready=loaded.status==endfield::store::LoadStatus::Ready;
    const std::string key=ready ? "ENDFIELD_TOKEN_READY"
        : loaded.status==endfield::store::LoadStatus::Unreadable ? "ENDFIELD_TOKEN_UNREADABLE"
        : "ENDFIELD_SIGN_IN_REQUIRED";
    // A language refresh must not overwrite the live QR-login status.
    const bool login_active=endfield_login_job_ && endfield_login_job_->running && !endfield_login_job_->stop;
    {
        std::lock_guard lock(state_->mutex);
        state_->endfield_ready=ready;
        if(!login_active) state_->endfield_status_key=key;
    }
    if(announce) state_->event(key);
}
void NativeTab::import_endfield_token() {
    opennow::PrepareAppStorage();
    const std::string home=opennow::AppHomePath();
    const std::string source=home+"/endfield-login.txt";
    std::ifstream in(source,std::ios::binary);
    if(!in) {state_->event("ENDFIELD_TOKEN_FILE_MISSING");return;}
    std::string token(8192,'\0');
    in.read(token.data(),static_cast<std::streamsize>(token.size()));
    token.resize(static_cast<std::size_t>(in.gcount()));
    in.close();
    const auto whitespace=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(!token.empty() && whitespace(token.front())) token.erase(token.begin());
    while(!token.empty() && whitespace(token.back())) token.pop_back();
    if(!token.empty() && endfield::store::Save(home,token)) {
        // The plaintext copy must not outlive the import.
        std::error_code ignored;
        std::filesystem::remove(source,ignored);
        refresh_endfield(false);
        state_->event("ENDFIELD_TOKEN_IMPORTED");
    } else {
        state_->event("ENDFIELD_TOKEN_IMPORT_FAILED");
    }
}
void NativeTab::clear_endfield_login() {
    if(endfield_login_job_) endfield_login_job_->stop=true;
    if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
    endfield_login_job_.reset();
    // A running start flow loses its credential basis with the vault; cancel
    // it best-effort (it exits between its own API calls).
    if(endfield_start_job_) endfield_start_job_->stop=true;
    state_->qr_code("");
    opennow::PrepareAppStorage();
    const bool cleared=endfield::store::Clear(opennow::AppHomePath());
    refresh_endfield(false);
    state_->event(cleared ? "ENDFIELD_TOKEN_CLEARED" : "ENDFIELD_TOKEN_CLEAR_FAILED");
}
void NativeTab::start_endfield() {
    // A press while the flow is running means cancel; the primary button is
    // already showing the Cancel label at that point.
    if(endfield_start_job_ && endfield_start_job_->running) {cancel_endfield_start();return;}
    if(endfield_start_job_) {
        // Previous attempt finished; joining it is instant now.
        if(endfield_start_thread_.joinable()) endfield_start_thread_.join();
        endfield_start_job_.reset();
    }
    if(state_->active) {state_->event("GAME_SWITCH_BLOCKED_WORKING",0,0,false);return;}
    opennow::PrepareAppStorage();
    const auto loaded=endfield::store::Load(opennow::AppHomePath());
    if(loaded.status!=endfield::store::LoadStatus::Ready || !loaded.hg_token) {
        state_->event("ENDFIELD_SIGN_IN_REQUIRED");
        endfield_qr_login(false);
        return;
    }
    {
        std::lock_guard lock(state_->mutex);
        state_->endfield_stream.reset();
    }
    state_->cancelled=false;
    auto job=std::make_shared<EndfieldStartJob>();
    endfield_start_job_=job;
    const auto state=state_;
    try {
        endfield_start_thread_=std::thread([state,job,token=*loaded.hg_token,
                                            uid=loaded.uid,device_token=loaded.device_token] {
            NativeTab::endfield_start_worker(state,job,token,uid,device_token);
        });
    } catch (...) {
        // Thread creation failure must not escape into borealis.
        job->stop=true;
        endfield_start_job_.reset();
        state_->event("NATIVE_OPERATION_FAILED");
    }
}
void NativeTab::endfield_start_worker(std::shared_ptr<State> state,
                                      std::shared_ptr<EndfieldStartJob> job,
                                      std::string hg_token,
                                      std::string hg_uid,std::string device_token) {
    struct Running {std::atomic_bool& flag;~Running(){flag=false;}} running{job->running};
    // The status card and the game-switch guard read `active`; representing
    // the whole queue as one busy flow keeps both behaviours consistent.
    struct Busy {std::atomic_bool& flag;bool armed=false;~Busy(){if(armed) flag=false;}} busy{state->active};
    const auto stopped=[&]{return job->stop.load();};
    const auto fail=[&](const std::string& key,int code=0,long http=0) {
        if(stopped()) return;
        state->event(key,code,http);
    };
    try {
        busy.armed=true;state->active=true;
        // Keep the persisted passport token intact. Obtain separate short-lived
        // cloud-app and game tickets using the SDK's passport login APIs.
        (void)hg_uid; // Optional legacy binding metadata; not required here.
        state->event("CLOUD_ACCOUNT_AUTH");
        const auto tickets=endfield::api::FetchCloudCredentials(hg_token,&job->stop,device_token);
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        if(!tickets.credentials) {
            state->event(tickets.stage==endfield::api::CloudCredentialsResult::Stage::Grant
                ? "ENDFIELD_CLOUD_GRANT_FAILED" : "ENDFIELD_GAME_TICKET_FAILED",
                static_cast<int>(tickets.error.code),tickets.error.http);
            return;
        }
        state->event("ENDFIELD_CLOUD_TICKETS_READY");
        const auto exchanged=endfield::api::Exchange(tickets.credentials->hg_token,
                                                       tickets.credentials->u8_token,&job->stop);
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        if(!exchanged.session) {
            state->event("ENDFIELD_STREAM_LOGIN_FAILED",
                         static_cast<int>(exchanged.error.code),
                         static_cast<long>(exchanged.error.http));
            return;
        }
        const auto& login_session=*exchanged.session;
        state->event("CHECK_SERVICE");
        // The official getNodeList(callback,error) uses the tenant list.
        auto nodes=endfield::api::GetNodeList(login_session.login_session_id,
                                                    login_session.tenant_key,&job->stop);
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        if(nodes.nodes.empty()) {fail("ENDFIELD_STREAM_NODES_FAILED",static_cast<int>(nodes.error.code),nodes.error.http);return;}
        if(nodes.nodes.size()>1 && AutoTestEnabled(opennow::AppHomePath()+"/cloudgenshin-autotest.txt") &&
           AutoTestEndfieldAlternateNodeEnabled(opennow::AppHomePath()+"/cloudgenshin-autotest.node")) {
            std::swap(nodes.nodes[0],nodes.nodes[1]);
            state->event("ENDFIELD_ALTERNATE_NODE_SELECTED",1,0,false);
        }
        state->event("NODE_CANDIDATES_READY",static_cast<int>(nodes.nodes.size()));
        endfield::api::DispatchInputs inputs;
        const auto stream_settings=endfield::LoadStreamSettings(opennow::AppHomePath()+"/endfield-stream-settings.txt");
        inputs.bit_rate=endfield::kStreamBitrates[stream_settings.tier].maximum;
        inputs.fps=stream_settings.fps;
        inputs.tenant_key=login_session.tenant_key;
        inputs.game_id=login_session.game_id;
        inputs.node_id=nodes.nodes.front().node_id;
        // Delay order is the official preference, but a node can reject the
        // dispatch with 21003 ("too many players queued", findings §3). In that
        // case try the next node instead of failing the whole start.
        endfield::api::DispatchResult dispatched;
        bool dispatched_ok=false;
        for(const auto& node : nodes.nodes) {
            if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
            inputs.node_id=node.node_id;
            for(int attempt=0;attempt<4;++attempt) {
                state->event("ENDFIELD_DISPATCH_REQUESTED");
                dispatched=endfield::api::Dispatch(login_session.login_session_id,inputs,&job->stop);
                // Official 21002: the previous game's save is being processed.
                // Keep the valid login and give it a bounded, cancellable wait.
                if(dispatched.state || dispatched.error.code!=21002 || attempt==3) break;
                state->event("ENDFIELD_PREVIOUS_SAVE_PENDING",21002,dispatched.error.http);
                if(!WaitFor(job->stop,std::chrono::seconds(20))) {
                    state->event("LAUNCH_CANCELLED");return;
                }
            }
            if(dispatched.state) {dispatched_ok=true;break;}
            if(dispatched.error.code!=21003) break;
            state->event("ENDFIELD_NODE_BUSY",static_cast<int>(dispatched.error.code));
        }
        if(!dispatched_ok) {fail("ENDFIELD_STREAM_DISPATCH_FAILED",static_cast<int>(dispatched.error.code),dispatched.error.http);return;}
        std::string session_id=dispatched.state->session_id;
        // Cancel any outstanding queue on early return or exception. Once a
        // pod is assigned this queue-only endpoint may say NOT_QUEUING; it
        // cannot confirm termination of a running game.
        struct AllocationCleanup {
            std::function<void()> release;
            ~AllocationCleanup(){if(release) {try {release();} catch(...) {}}}
        } allocation{[&] {
            if(session_id.empty()) return;
            const auto result=endfield::api::Cancel(login_session.login_session_id,session_id);
            state->event(endfield::QueueCancelEvent(result.code),
                         static_cast<int>(result.code),result.http,false);
        }};
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        std::string sdk_msg=dispatched.state->sdk_msg;
        bool pod_assigned=dispatched.state->pod_assigned();
        long long queue_seat=dispatched.state->queuing_seat_no;
        if(!pod_assigned) {
            state->event("DISPATCH_NORMAL_QUEUE");
            state->event("QUEUED_POSITION",static_cast<int>(queue_seat));
            int failures=0;
            // The normal queue can legitimately be long (hundreds of seats on
            // popular evenings). The official web client keeps waiting until a
            // pod is assigned or the server expires the session, so there is no
            // self-imposed seat threshold here: only the user's Cancel aborts,
            // which explicitly releases the pod allocation below.
            while(!stopped() && !pod_assigned) {
                // 2 s poll cadence in 100 ms slices so cancel is responsive.
                for(int slice=0;slice<20 && !stopped();++slice)
                    WaitFor(job->stop,std::chrono::milliseconds(100));
                if(stopped()) break;
                const auto polled=endfield::api::Poll(login_session.login_session_id,session_id,&job->stop);
                if(!polled.state) {
                    // Server-side cancel now exists; answer stop even on a failed poll.
                    if(stopped()) break;
                    if(++failures>=3) {fail("ENDFIELD_STREAM_QUEUE_FAILED",static_cast<int>(polled.error.code));return;}
                    continue;
                }
                failures=0;
                if(session_id.empty()) session_id=polled.state->session_id;
                queue_seat=polled.state->queuing_seat_no;
                pod_assigned=polled.state->pod_assigned();
                if(queue_seat>0 && !pod_assigned)
                    state->event("QUEUED_POSITION",static_cast<int>(queue_seat));
                if(pod_assigned) sdk_msg=polled.state->sdk_msg;
            }
            if(stopped()) {
                // The guard cancels a still-active queue; it cannot stop a pod.
                state->event("LAUNCH_CANCELLED");
                return;
            }
        } else {
            if(dispatched.state->queuing_seat_no>0)
                state->event("QUEUED_POSITION",static_cast<int>(dispatched.state->queuing_seat_no));
        }
        const auto sdk=endfield::ParseVlinkSdkMsg(sdk_msg);
        if(!sdk || !sdk->ready()) {
            fail("ENDFIELD_STREAM_SDK_INVALID");
            return;
        }
        // Shape-only evidence for the first-connect payload. Never log the
        // payload itself: it carries the pod auth/verify material.
        {
            const auto length=[&](std::size_t value) {
                return static_cast<int>(std::min<std::size_t>(value,999999));
            };
            int kind=0;
            if(sdk->packed_data_kind=="string") kind=1;
            else if(sdk->packed_data_kind=="object") kind=2;
            else if(sdk->packed_data_kind=="array") kind=3;
            else if(sdk->packed_data_kind=="scalar") kind=4;
            else if(sdk->packed_data_kind=="null") kind=5;
            state->event("ENDFIELD_SDK_PACKED_KIND",kind,0,false);
            state->event("ENDFIELD_SDK_PACKED_LENGTH",length(sdk->packed_data_length),0,false);
            state->event("ENDFIELD_SDK_PACKED_KEYS",
                         static_cast<int>(sdk->packed_data_key_mask),0,false);
            state->event("ENDFIELD_SDK_GSM_TOKEN_LENGTH",length(sdk->gsm_token.size()),0,false);
            state->event("ENDFIELD_SDK_USER_ID_LENGTH",length(sdk->user_id.size()),0,false);
            state->event("ENDFIELD_SDK_AUTH_LENGTH",length(sdk->auth.size()),0,false);
            state->event("ENDFIELD_SDK_VERIFY_LENGTH",length(sdk->verify.size()),0,false);
            state->event("ENDFIELD_SDK_PLAYER_INFO",sdk->player_info_valid ? 1 : 0,0,false);
            state->event("ENDFIELD_SDK_PLAYER_ROLE",
                         static_cast<int>(std::clamp<long long>(sdk->player_role_number,0,999)),0,false);
            // Protocol constants only (schemeType/gsVer select the ARM branches).
            int scheme=0;
            if(sdk->scheme_type=="arm_ks") scheme=1;
            else if(sdk->scheme_type=="x86") scheme=2;
            state->event("ENDFIELD_SDK_SCHEME",scheme,0,false);
            int gs_major=0;
            for(const char c: sdk->gs_ver) {
                if(c>='0'&&c<='9') gs_major=gs_major*10+(c-'0');
                else break;
            }
            state->event("ENDFIELD_SDK_GSVER_MAJOR",std::min(gs_major,999),0,false);
            state->event("ENDFIELD_SDK_PROXY_ARM",sdk->is_proxy_arm() ? 1 : 0,0,false);
            state->event("ENDFIELD_SDK_CODEC_TYPE",sdk->codec_type,0,false);
            // The launch blob is a URL query string in the browser path. Its
            // values carry account/pod material, so only field names and value
            // lengths are recorded: an empty or truncated blob would explain a
            // launcher that never receives its game-start parameters.
            {
                const auto url_decode=[](std::string_view in) {
                    std::string out;
                    out.reserve(in.size());
                    const auto hex=[](char h) {
                        if(h>='0'&&h<='9') return h-'0';
                        if(h>='a'&&h<='f') return h-'a'+10;
                        if(h>='A'&&h<='F') return h-'A'+10;
                        return -1;
                    };
                    for(std::size_t i=0;i<in.size();++i) {
                        const char c=in[i];
                        if(c=='%'&&i+2<in.size()) {
                            const int high=hex(in[i+1]),low=hex(in[i+2]);
                            if(high>=0&&low>=0) {
                                out.push_back(static_cast<char>((high<<4)|low));
                                i+=2;continue;
                            }
                        }
                        out.push_back(c=='+' ? ' ' : c);
                    }
                    return out;
                };
                const auto hex_suffix=[](const std::string& text) {
                    static const char digits[]="0123456789ABCDEF";
                    std::string hex;
                    hex.reserve(text.size()*2);
                    for(const unsigned char c: text) {
                        hex.push_back(digits[c>>4]);hex.push_back(digits[c&15]);
                    }
                    return hex;
                };
                const std::string& blob=sdk->packed_data;
                int shape=0,fields=0;
                if(!blob.empty()&&blob.front()=='{') shape=1;
                else if(blob.find('=')!=std::string::npos) shape=2;
                else if(!blob.empty()) shape=3;
                state->event("ENDFIELD_PACKED_SHAPE",shape,0,false);
                if(shape==2) {
                    std::size_t start=0;
                    while(start<blob.size()&&fields<24) {
                        const auto end=blob.find('&',start);
                        const auto part=blob.substr(start,end==std::string::npos
                            ? std::string::npos : end-start);
                        const auto equals=part.find('=');
                        const std::string key=url_decode(part.substr(0,equals));
                        const std::size_t value_length=equals==std::string::npos ? 0
                            : url_decode(part.substr(equals+1)).size();
                        if(!key.empty()) {
                            ++fields;
                            if(key.size()<=32)
                                state->event("ENDFIELD_PACKED_KEY_HEX_"+hex_suffix(key),0,false);
                            state->event("ENDFIELD_PACKED_VALUE_LEN",
                                         static_cast<int>(std::min<std::size_t>(value_length,9999)),0,false);
                        }
                        if(end==std::string::npos) break;
                        start=end+1;
                    }
                }
                state->event("ENDFIELD_PACKED_FIELDS",fields,0,false);
            }
        }
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        // Official gameManager.startGame runs checkAndUpdateToken again after
        // queueing, before attaching the pod. The pre-dispatch code may have
        // expired during the queue; do not reuse it for PipelineConnected.
        // Keep this account/proof-aware owner for later RequestLogin as well.
        // Preserve the APK's independent game CLIENT refresh in production.
        // The complete H5-browser refresh also renews its cloud session; that
        // additional exchange has not proved safe for an already assigned pod
        // in native testing. Keep that separate, tested profile opt-in only.
        const auto saved_game_authorization=std::make_shared<endfield::login::SavedGameAuthorization>(
            opennow::AppHomePath(),hg_token,device_token,0,
            endfield::login::SavedGameAuthorization::Profile::AndroidSdk);
        state->event("ENDFIELD_GAME_AUTH_REFRESHING");
        const auto game_authorization=saved_game_authorization->request(&job->stop);
        if(stopped()) {state->event("LAUNCH_CANCELLED");return;}
        const auto authorization_status=endfield::DescribeGameAuthorization(game_authorization);
        // A terminal refusal must replace the old "Choosing a server" state.
        // Previously update_status=false made the idle card say "Signed in"
        // even though there was no valid game grant and the worker returned.
        state->event(authorization_status.event,static_cast<int>(authorization_status.code),
                     authorization_status.http);
        // Do not enter a perpetual title screen with an unusable initial
        // loginInfo. The allocation guard still releases on this return; the
        // saved account and device proof are left intact for an explicit retry.
        if(!game_authorization.ready()) return;
        {
            std::lock_guard lock(state->mutex);
            state->endfield_stream=endfield::AllocatedStream{*sdk,
                [state,login_id=login_session.login_session_id,session_id] {
                    const auto result=endfield::api::Cancel(login_id,session_id);
                    state->event(endfield::QueueCancelEvent(result.code),
                                 static_cast<int>(result.code),result.http,false);
                },game_authorization.code,
                [saved_game_authorization](const std::atomic_bool* cancelled) {
                    return saved_game_authorization->request(cancelled);
                },stream_settings};
            allocation.release={};
        }
        state->event("ENDFIELD_STREAM_STARTING");
    } catch (...) {
        if(!stopped())
            state->event("NATIVE_OPERATION_FAILED");
    }
}
void NativeTab::cancel_endfield_start() {
    // Stop only: joining on the UI thread could stall until a curl timeout.
    // The start button joins the finished thread lazily before its next run.
    if(endfield_start_job_) endfield_start_job_->stop=true;
    state_->event("CANCEL_REQUESTED");
}
void NativeTab::endfield_qr_login(bool refresh) {
    if(endfield_login_job_ && endfield_login_job_->running && !endfield_login_job_->stop) {
        if(!refresh) return;
        endfield_login_job_->stop=true;
    }
    // Never detach: libnx's pthread detach has been observed to fail, and the
    // still-joinable temporary then aborts the app inside ~thread. The cancel
    // callback makes joining an in-flight request fast.
    if(endfield_login_job_) endfield_login_job_->stop=true;
    if(endfield_login_thread_.joinable()) endfield_login_thread_.join();
    endfield_login_job_.reset();
    opennow::PrepareAppStorage();
    state_->qr_code("");
    state_->set_endfield_status("ENDFIELD_QR_FETCHING");
    state_->event("ENDFIELD_QR_FETCH");
    auto job=std::make_shared<EndfieldLoginJob>();
    endfield_login_job_=job;
    const auto state=state_;
    // The worker only writes State (mutex-protected) and never touches widgets.
    try {
        endfield_login_thread_=std::thread([state,job]{NativeTab::endfield_login_worker(state,job);});
    } catch (...) {
        // Thread creation failure must not escape into borealis.
        job->stop=true;
        endfield_login_job_.reset();
        state_->qr_code("");
        state_->set_endfield_status("ENDFIELD_QR_NETWORK_FAILED");
        state_->event("ENDFIELD_QR_NETWORK_FAILED");
    }
}
void NativeTab::endfield_login_worker(std::shared_ptr<State> state,
                                      std::shared_ptr<EndfieldLoginJob> job) {
    using namespace endfield::login;
    struct Running {std::atomic_bool& flag;~Running(){flag=false;}} running{job->running};
    const auto set_status=[&](const std::string& key){state->set_endfield_status(key);};
    const auto fail=[&](const std::string& key,long code=0,long http=0) {
        if(job->stop) return;
        state->qr_code("");
        set_status(key);
        state->event(key,static_cast<int>(code),http);
    };

    // HttpClient throws on transport/TLS failures; the worker thread must never
    // let an exception escape or the whole app aborts.
    try {
    const auto created=CreateScanSession(&job->stop);
    if(job->stop) return;
    if(created.http_status<200 || created.http_status>=300) {
        fail("ENDFIELD_QR_NETWORK_FAILED",created.http_status);return;
    }
    const auto session=ParseGenScanLogin(ParseEnvelope(created.body));
    if(!session) {fail("ENDFIELD_QR_CREATE_FAILED");return;}
    if(job->stop) return;
    state->qr_code(session->scan_url);
    set_status("ENDFIELD_QR_WAITING");
    state->event("ENDFIELD_QR_WAITING");

    const auto deadline=std::chrono::steady_clock::now()+
        std::chrono::milliseconds(kScanSessionLifetimeMs);
    bool scanned=false;
    int failures=0;
    while(!job->stop) {
        if(!WaitFor(job->stop,std::chrono::milliseconds(kPollIntervalMs))) break;
        if(std::chrono::steady_clock::now()>=deadline) {fail("ENDFIELD_QR_EXPIRED");return;}
        const auto polled=PollScanStatus(session->scan_id,&job->stop);
        if(job->stop) return;
        if(polled.http_status<200 || polled.http_status>=300) {
            if(++failures>=3) {fail("ENDFIELD_QR_NETWORK_FAILED",polled.http_status);return;}
            continue;
        }
        failures=0;
        const auto result=ParseScanStatus(ParseEnvelope(polled.body));
        switch(result.status) {
            case PollStatus::Waiting: break;
            case PollStatus::Scanned:
                if(!scanned) {
                    scanned=true;
                    set_status("ENDFIELD_QR_SCANNED");
                    state->event("ENDFIELD_QR_SCANNED");
                }
                break;
            case PollStatus::Authorized: {
                state->qr_code("");
                set_status("ENDFIELD_QR_EXCHANGING");
                const auto exchanged=ExchangeScanCode(result.scan_code,&job->stop);
                if(job->stop) return;
                if(exchanged.http_status<200 || exchanged.http_status>=300) {
                    fail("ENDFIELD_LOGIN_EXCHANGE_FAILED",exchanged.http_status);return;
                }
                const auto token=ParseTokenByScanCode(ParseEnvelope(exchanged.body));
                if(!token) {fail("ENDFIELD_LOGIN_EXCHANGE_FAILED");return;}
                // Save the original passport token before optional role work.
                // Cloud-app and U8 tickets are obtained from it on each start;
                // a failed binding lookup must not discard a confirmed login.
                std::string uid;
                if(job->stop) return;
                opennow::PrepareAppStorage();
                int stage=0;
                if(!endfield::store::Save(opennow::AppHomePath(),token->token,uid,&stage,token->device_token)) {
                    state->event("ENDFIELD_LOGIN_SAVE_STAGE",stage);
                    fail("ENDFIELD_LOGIN_SAVE_FAILED");return;
                }
                set_status("ENDFIELD_TOKEN_READY");
                state->set_endfield_ready(true);
                state->event("ENDFIELD_LOGIN_OK");
                return;
            }
            case PollStatus::Expired: fail("ENDFIELD_QR_EXPIRED");return;
            case PollStatus::ApiError: fail("ENDFIELD_QR_STATUS_REJECTED",result.code);return;
            case PollStatus::Invalid: fail("ENDFIELD_QR_RESPONSE_INVALID",result.code);return;
        }
    }
    } catch (...) {
        fail("ENDFIELD_QR_NETWORK_FAILED");
    }
    // No cleanup here: on stop the caller (refresh/clear/leave) already owns
    // the QR state, and clearing here could wipe a newer attempt's QR.
}
void NativeTab::refresh_game_buttons() {
    const GameProfile* selected=nullptr;bool endfield=false;
    {std::lock_guard lock(state_->mutex);selected=state_->game;endfield=state_->endfield_selected;}
    for(std::size_t index=0;index<sizeof(kGameProfiles)/sizeof(kGameProfiles[0]);++index) {
        const bool active=!endfield && &kGameProfiles[index]==selected;
        auto* button=game_buttons_[index];
        button->setText(ui::Text(kGameProfiles[index].latin_name));
        // setTextColor re-applies the button style, so the background tint and
        // the border-only focus highlight are set afterwards.
        button->setTextColor(active ? kAccent : kMuted);
        button->setHideHighlightBackground(true);
        button->setBackgroundColor(active ? nvgRGBA(94,226,235,28) : nvgRGBA(255,255,255,8));
    }
    if(endfield_button_) {
        endfield_button_->setText(ui::Text("Endfield Cloud"));
        endfield_button_->setTextColor(endfield ? kAccent : kMuted);
        endfield_button_->setHideHighlightBackground(true);
        endfield_button_->setBackgroundColor(endfield ? nvgRGBA(94,226,235,28) : nvgRGBA(255,255,255,8));
    }
    if(hero_game_)
        hero_game_->setText(ui::Text(ui::chinese ? selected->display_name : selected->latin_name));
    if(mihoyo_row_) mihoyo_row_->setVisibility(endfield ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if(endfield_panel_) endfield_panel_->setVisibility(endfield ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    // Re-apply dynamic labels and hints on the next draw (the language or the
    // selected game may have changed).
    mihoyo_mode_=-1;endfield_mode_=-1;
    mihoyo_hint_key_=nullptr;endfield_hint_key_=nullptr;
}
void NativeTab::logout() {
    automatic_.stop();
    state_->cancelled=true;
    if(worker_.joinable()) worker_.join();
    {std::lock_guard lock(state_->mutex);state_->authorization.reset();state_->startup.reset();state_->qr.clear();}
    opennow::PrepareAppStorage();
    state_->event(login_store::Clear(opennow::AppHomePath()) ? "LOGIN_CLEARED" : "LOGIN_CLEAR_FAILED");
}
void NativeTab::export_log() {
    state_->save_log(true);
}
void NativeTab::draw(NVGcontext* vg,float x,float y,float width,float height,brls::Style style,brls::FrameContext* context) {
    unsigned game=0;
    {
        std::lock_guard lock(state_->mutex);
        if(state_->endfield_selected) game=2;
        else if(state_->game==&kGameProfiles[1]) game=1;
    }
    if(static_cast<int>(game)!=background_game_) {
        if(background_image_>0) nvgDeleteImage(vg,background_image_);
        background_game_=static_cast<int>(game);
        background_image_=background::Load(opennow::AppHomePath(),game,
            [](const auto& bytes,int& width,int& height) {
                int components=0;
                return stbi_info_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&components)!=0;
            },[vg](auto& bytes) {
                return nvgCreateImageMem(vg,0,bytes.data(),static_cast<int>(bytes.size()));
            });
        state_->event(background_image_>0 ? "HOME_BACKGROUND_CUSTOM_LOADED" : "HOME_BACKGROUND_ORIGINAL_FALLBACK",
            static_cast<int>(game),0,false);
    }
    // Also under transparent custom images: deterministic and readable.
    background::DrawDefault(vg,x,y,width,height,game);
    if(background_image_>0) {
        int iw=0,ih=0;nvgImageSize(vg,background_image_,&iw,&ih);
        if(iw>0 && ih>0) {
            const float scale=std::max(width/iw,height/ih);
            nvgSave(vg);nvgIntersectScissor(vg,x,y,width,height);
            nvgBeginPath(vg);nvgRect(vg,x,y,width,height);
            nvgFillPaint(vg,nvgImagePattern(vg,x+(width-iw*scale)/2,y+(height-ih*scale)/2,
                iw*scale,ih*scale,0,background_image_,1));nvgFill(vg);
            nvgBeginPath(vg);nvgRect(vg,x,y,width,height);
            nvgFillPaint(vg,nvgLinearGradient(vg,x,y,x,y+height,nvgRGBA(8,13,20,150),nvgRGBA(8,13,20,195)));
            nvgFill(vg);nvgRestore(vg);
        }
    }
    const auto now=std::chrono::steady_clock::now();
    if(automatic_.enabled() && !state_->cancelled && now>=automatic_stop_check_) {
        automatic_stop_check_=now+std::chrono::seconds(1);
        if(std::ifstream(opennow::AppHomePath()+"/cloudgenshin-autotest.stop").good()) {
            state_->cancelled=true;state_->qr_code("");state_->event("AUTOTEST_STOP_REQUESTED");
        }
    }
    // Keep the SD evidence within 30 s of RAM so an unexpected exit still
    // shows the final queue state instead of a stale early snapshot.
    if(automatic_.enabled() && !state_->cancelled && now>=automatic_log_flush_) {
        automatic_log_flush_=now+std::chrono::seconds(30);
        state_->queue_log_save();
    }
    if(displayed_chinese_!=ui::chinese) {
        mihoyo_mode_=-1;
        displayed_chinese_=ui::chinese;texts_.refresh();refresh_game_buttons();refresh_endfield(false);updateActionHint(brls::BUTTON_START,ui::Text("Exit"));
    }
    std::optional<dispatch::SdkStartGameParams> startup;
    std::optional<endfield::AllocatedStream> endfield_sdk;
    std::optional<cloudgenshin::Authorization> startup_authorization;
    std::string sdk_version;
    bool signed_in=false,needs_web_update=false;
    bool endfield=false;
    bool working=false;
    bool endfield_ready=false;
    bool endfield_starting=false;
    std::string status;
    {
        std::lock_guard lock(state_->mutex);
        signed_in=state_->authorization.has_value();
        needs_web_update=auth::NeedsWebLoginUpdate(state_->game->id,signed_in,
            signed_in && auth::ValidPassportMid(state_->authorization->passport_mid),state_->status);
        endfield=state_->endfield_selected;
        working=state_->active;
        status=state_->status;
        endfield_ready=state_->endfield_ready;
        if (!state_->active && !state_->cancelled && state_->startup && state_->authorization) {
            startup=std::move(state_->startup);state_->startup.reset();startup_authorization=*state_->authorization;
            sdk_version=std::string(state_->game->sdk_version);
        }
        if(endfield) {
            // Read on the UI thread; the flag itself is an atomic owned by the
            // start worker.
            endfield_starting=endfield_start_job_ && endfield_start_job_->running;
            if(!state_->active && state_->endfield_stream) {
                endfield_sdk=std::move(state_->endfield_stream);
                state_->endfield_stream.reset();
            }
        }
    }
    if(!endfield) {
        // One action adapts to the session: Start when signed in, Sign in when
        // not, Cancel while the flow is running, Refresh QR after expiry.
        // Focus never lands on a hidden control.
        const bool qr_expired=!working && !signed_in &&
            status.find("QR_EXPIRED")!=std::string::npos;
        const int mode=working ? 0 : (needs_web_update ? 4 : (signed_in ? 1 : (qr_expired ? 3 : 2)));
        if(mode!=mihoyo_mode_) {
            mihoyo_mode_=mode;
            mihoyo_primary_->setText(ui::Text(mode==0 ? "Cancel" : mode==1 ? "Start"
                : mode==4 ? "Update web sign-in" : mode==3 ? "Refresh QR" : "Sign in"));
            mihoyo_primary_->setBackgroundColor(mode==0 ? kAmber : kAccent);
            const bool show_logout=mode==1 || mode==4;
            if(!show_logout && mihoyo_logout_->isFocused())
                brls::Application::giveFocus(mihoyo_primary_);
            mihoyo_logout_->setVisibility(show_logout ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        const char* hint=status=="QUEUE_ABANDONED_TOO_LONG"
            ? "Over 100 players ahead. Login is kept; no need to scan again." : working
            ? (status=="QR_WAITING"
                ? "Scan with MiYoushe. The code is valid for about 2 minutes."
                : "Connecting to the official service…")
            : (needs_web_update ? "Legacy cloud login is kept. Update web sign-in once for Star Rail."
            : signed_in ? "Ready to start."
            : (qr_expired ? "QR expired. Tap refresh to get a new code."
                          : "Scan with MiYoushe to sign in."));
        if(hint!=mihoyo_hint_key_) {mihoyo_hint_key_=hint;hero_hint_->setText(ui::Text(hint));}
        mihoyo_qr_->set_context(signed_in);
    } else {
        const int mode=endfield_starting ? 2 : (endfield_ready ? 1 : 0);
        if(mode!=endfield_mode_) {
            endfield_mode_=mode;
            endfield_primary_->setText(ui::Text(mode==2 ? "Cancel" : mode==1 ? "Start" : "Sign in"));
            endfield_primary_->setBackgroundColor(mode==2 ? kAmber : kAccent);
            if(mode==2 && endfield_clear_->isFocused())
                brls::Application::giveFocus(endfield_primary_);
            // A running queue must not be confused by a login wipe.
            endfield_clear_->setVisibility(mode==2 ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        }
        const auto notice=endfield::DescribeLauncherStatus(status);
        const char* hint=notice ? notice->detail : mode==2 ? "The official service is preparing your session"
            : mode==1 ? "Ready to start."
            : "Scan with Skland and confirm on your phone.";
        if(hint!=endfield_hint_key_) {endfield_hint_key_=hint;endfield_hint_->setText(ui::Text(hint));}
        endfield_qr_->set_context(endfield_ready);
    }
    if(!initial_focus_done_) {
        initial_focus_done_=true;
        brls::Application::giveFocus(endfield ? static_cast<brls::View*>(endfield_primary_)
                                              : static_cast<brls::View*>(mihoyo_primary_));
    }
    brls::Box::draw(vg,x,y,width,height,style,context);
    if (startup) {
        automatic_.stop();
        const auto state=state_;
        const GameProfile* reconnect_game=nullptr;
        {std::lock_guard lock(state->mutex);reconnect_game=state->game;}
        brls::Application::pushActivity(new brls::Activity(new CloudGenshinStreamView(
            std::move(*startup),startup_authorization ? std::move(*startup_authorization)
                                                       : cloudgenshin::Authorization{},
            [state](const std::string& name,int code,long http){state->event(name,code,http);},
            [state]{return state->log.recent(8);}, [state]{return state->save_log(true);},
            std::move(sdk_version), automatic_.enabled() ? AutoTestStreamSeconds(
                opennow::AppHomePath()+"/cloudgenshin-autotest.soak") : 0,
            [this,lifetime=std::weak_ptr<bool>(ui_lifetime_),reconnect_game]{
                if(lifetime.expired()) return;
                {std::lock_guard lock(state_->mutex);
                 if(!state_->authorization || state_->active || state_->endfield_selected ||
                    state_->game!=reconnect_game) return;}
                // Explicit user action only: reuse the in-memory/saved login and
                // the ordinary queue. Any required QR/login is left to the UI.
                // cancelled belongs to the previous launcher job (including
                // willDisappear); begin() resets it for this new request.
                state_->event("SESSION_END_RECONNECT_HINT");
                begin(true);
            } )));
    } else if (endfield_sdk) {
        automatic_.stop();
        const auto state=state_;
        brls::Application::pushActivity(new brls::Activity(new endfield::EndfieldStreamView(
            std::move(*endfield_sdk),
            [state](const std::string& name,int code,long http){state->event(name,code,http);},
            [state]{return state->log.recent(8);}, [state]{return state->save_log(true);},
            automatic_.enabled() ? -1 : 0 )));
    } else if(!endfield) {
        const auto now_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        // A worker can finish after this draw took its startup snapshot. Keep
        // that frame busy until the next frame consumes the prepared stream;
        // otherwise the harness may kill a successfully dispatched session.
        switch(automatic_.poll(now_ms,signed_in,working || state_->active,state_->cancelled)) {
            case AutoTestAction::LaunchOnce:
                if(needs_web_update && !AutoTestStarRailWebResumeEnabled(opennow::AppHomePath(),"8")) {
                    automatic_.stop();state_->event("STAR_RAIL_WEB_LOGIN_UPDATE_REQUIRED");
                    state_->event("AUTOTEST_LAUNCH_FINISHED");
                } else {state_->event("AUTOTEST_NORMAL_QUEUE_START");begin(true);}
                break;
            case AutoTestAction::CancelTimeout:
                state_->cancelled=true;state_->qr_code("");state_->event("AUTOTEST_TIMEOUT");break;
            case AutoTestAction::Finished: state_->event("AUTOTEST_LAUNCH_FINISHED");break;
            default: break;
        }
    }
}
} // namespace cloudgenshin
