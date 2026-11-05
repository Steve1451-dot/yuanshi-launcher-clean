#pragma once

#include "endfield/hg_login.hpp"

namespace cloudgenshin::endfield::login {

// Used by the launch worker after queueing, then the stream's single
// asynchronous login task, never its RTC worker.
// An official device-check rejection stays blocked until the same account has
// a different encrypted device proof. A user can then retry login in the game
// without discarding the account or reopening the cloud allocation.
class SavedGameAuthorization {
public:
    enum class Profile { AndroidSdk, WebRtc };
    SavedGameAuthorization(std::string directory,std::string passport_token,
                           std::string device_token,long initial_status,
                           Profile profile=Profile::AndroidSdk);
    GameAuthorization request(const std::atomic_bool* cancelled);
private:
    std::string directory_,passport_token_,device_token_;
    bool blocked_;
    Profile profile_;
};

} // namespace cloudgenshin::endfield::login
