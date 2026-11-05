#pragma once
#include <borealis.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "mihoyo/ui_language.hpp"
#include "mihoyo/autotest_policy.hpp"
#include "mihoyo/production_profile.hpp"

namespace cloudgenshin {
class NativeTab final : public brls::Box {
public:
    NativeTab();
    ~NativeTab() override;
    void draw(NVGcontext*,float,float,float,float,brls::Style,brls::FrameContext*) override;
    void willDisappear(bool reset_state) override;
private:
    struct State;
    // A QR login attempt; the worker only touches State (never widget
    // pointers). It is a joinable member thread: libnx's detach() has been
    // observed to fail, which aborts the app in ~thread when still joinable.
    struct EndfieldLoginJob {
        std::atomic_bool stop{false};
        std::atomic_bool running{true};
    };
    // The cloud-session start flow (exchange -> nodes -> dispatch -> queue).
    // Cancelled cooperatively between API calls; never joined from the UI
    // thread while a worker is running (each call has its own curl timeout).
    struct EndfieldStartJob {
        std::atomic_bool stop{false};
        std::atomic_bool running{true};
    };
    class QrView;
    class StatusCard;
    void begin(bool launch,bool restore_only=false,bool passport=false);
    void logout();
    void export_log();
    void select_game(const GameProfile& profile);
    void select_endfield();
    // Re-reads the Endfield vault state into the panel/status; `announce` also
    // publishes it to the status card and safe log.
    void refresh_endfield(bool announce);
    // Starts (or refreshes) the native Hypergryph passport QR login. `refresh`
    // replaces an in-flight QR when true; otherwise an active attempt is kept.
    void endfield_qr_login(bool refresh);
    static void endfield_login_worker(std::shared_ptr<State> state,
                                      std::shared_ptr<EndfieldLoginJob> job);
    void import_endfield_token();
    void clear_endfield_login();
    void start_endfield();
    void cancel_endfield_start();
    static void endfield_start_worker(std::shared_ptr<State> state,
                                      std::shared_ptr<EndfieldStartJob> job,
                                      std::string hg_token,
                                      std::string hg_uid,std::string device_token);
    void refresh_game_buttons();
    std::shared_ptr<State> state_;
    std::thread worker_;
    std::thread endfield_login_thread_;
    std::shared_ptr<EndfieldLoginJob> endfield_login_job_;
    std::thread endfield_start_thread_;
    std::shared_ptr<EndfieldStartJob> endfield_start_job_;
    brls::Button* game_buttons_[sizeof(kGameProfiles)/sizeof(kGameProfiles[0])] = {};
    brls::Button* endfield_button_ = nullptr;
    brls::Box* mihoyo_row_ = nullptr;
    brls::Box* endfield_panel_ = nullptr;
    brls::Label* hero_game_ = nullptr;
    brls::Label* hero_hint_ = nullptr;
    brls::Label* endfield_hint_ = nullptr;
    QrView* mihoyo_qr_ = nullptr;
    QrView* endfield_qr_ = nullptr;
    brls::Button* mihoyo_primary_ = nullptr;
    brls::Button* mihoyo_logout_ = nullptr;
    brls::Button* endfield_primary_ = nullptr;
    brls::Button* endfield_clear_ = nullptr;
    // Cached dynamic UI state so labels/colors are only touched when they
    // actually change (draw() runs every frame).
    int mihoyo_mode_ = -1;
    int endfield_mode_ = -1;
    const char* mihoyo_hint_key_ = nullptr;
    const char* endfield_hint_key_ = nullptr;
    bool initial_focus_done_ = false;
    // One bounded custom texture at a time, loaded only on game/visibility
    // changes. A failed load is cached too; it must not cause per-frame SD I/O.
    int background_image_=0;
    int background_game_=-1;
    ui::Bindings texts_;
    bool displayed_chinese_=true;
    // A queued user-requested relaunch must not outlive this tab.
    std::shared_ptr<bool> ui_lifetime_=std::make_shared<bool>(true);
    AutoTestPolicy automatic_;
    std::chrono::steady_clock::time_point automatic_stop_check_{};
    std::chrono::steady_clock::time_point automatic_log_flush_{};
};
} // namespace cloudgenshin
