#pragma once

#include "endfield/wl_proto.hpp"
#include "mihoyo/motion_input.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace cloudgenshin::endfield {

// APK 1.6.0 CloudBridge contract, separate from the pod's transport auth.
// Owns only an ephemeral OAuth authorization code. U8 tickets are a different
// credential, used by the cloud API, and must never be substituted here.
// One network worker drives this object. No HTTP or UI work occurs here.
class GameBridge {
public:
    enum class Profile { AndroidSdk, WebRtc };
    enum class Request { Unknown=0, Pipeline=1, LaunchInfo=2, Login=3,
                         SafeArea=4, Gamepad=5, SetToken=6, Invalid=7,
                         GameData=8, AccountLogin=9, CharacterLogin=10,
                         CharacterLogout=11, AccountLogout=12, Unsupported=13,
                         BrowserControl=14, BrowserOpen=15, Language=16, Telemetry=17,
                         SensorSwitch=18 };
    explicit GameBridge(std::string auth_code = {},std::string client_device_id = {},
                        Profile profile = Profile::AndroidSdk);
    bool pipeline_connected(std::int64_t now);
    void gamepad_connected(bool connected,std::int64_t now);
    Request receive(const SimpleCmd& message, std::int64_t now);
    bool needs_login() const {return login_event_.has_value();}
    bool complete_login(std::string auth_code,std::int64_t now);
    void poll(std::int64_t now,
              const std::function<bool(const std::vector<std::uint8_t>&)>& send);
    std::size_t pending() const { return pending_.size(); }
    unsigned confirmed() const { return confirmed_; }
    unsigned expired() const { return expired_; }
    std::uint32_t unsupported_command_hash() const { return unsupported_command_hash_; }
    // Fixed SDK notification enum only, never uid/role/custom event data.
    int game_data_type() const {return game_data_type_;}
    bool gravity_requested() const {return gravity_requested_;}
    void stop_sensors() {gravity_requested_=false;}
    // Android GravitySensorReporter -> HighFqDataToGame; not the reliable
    // business-reply queue. Empty when the game has not requested gravity.
    std::vector<std::uint8_t> gravity_frame(const cloudgenshin::input::MotionSample& sample) const;
private:
    bool enqueue(const std::string& key, const std::string& json, std::int64_t now,
                 std::string request_identity = {},bool gamepad_snapshot=false);
    void expire_pending(std::int64_t now);
    std::string login_info() const;
    std::string launch_info() const;
    std::string gamepads() const;
    struct Pending {
        std::string index;
        std::vector<std::uint8_t> bytes;
        std::int64_t retry, deadline;
        std::string request_identity; // Ephemeral dedup key, never a diagnostic.
        bool gamepad_snapshot=false;
    };
    std::string auth_code_;
    std::string client_device_id_;
    Profile profile_;
    std::optional<std::string> login_event_;
    std::string login_identity_;
    bool started_=false;
    bool login_completed_=false;
    bool gamepad_connected_=false;
    unsigned next_index_=0, confirmed_=0, expired_=0;
    std::uint32_t unsupported_command_hash_=0;
    int game_data_type_=-1;
    bool gravity_requested_=false;
    std::deque<Pending> pending_;
    std::deque<std::string> handled_;
};
} // namespace cloudgenshin::endfield
