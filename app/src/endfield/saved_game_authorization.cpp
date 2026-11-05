#include "endfield/saved_game_authorization.hpp"
#include "endfield/endfield_store.hpp"
#include <utility>

namespace cloudgenshin::endfield::login {

SavedGameAuthorization::SavedGameAuthorization(std::string directory,std::string passport_token,
                                             std::string device_token,long initial_status,Profile profile)
    : directory_(std::move(directory)),passport_token_(std::move(passport_token)),
      device_token_(std::move(device_token)),blocked_(initial_status==109),profile_(profile) {}

GameAuthorization SavedGameAuthorization::request(const std::atomic_bool* cancelled) {
    if(cancelled && cancelled->load()) return {"",-2,0};
    const auto current=store::Load(directory_);
    // A changed account, missing vault or unreadable file must never authorize
    // the ongoing game with another user's device proof.
    if(current.status==store::LoadStatus::Ready && current.hg_token &&
       *current.hg_token==passport_token_ && !current.device_token.empty() &&
       current.device_token!=device_token_) {
        device_token_=current.device_token;
        blocked_=false;
    }
    if(blocked_) return {"",109,200};
    // In AndroidSdk, APK 1.6.0 EnterGameViewModel.refreshHGOauth2Code and LoginService.cloudLogin
    // call HGSDK.getOauth2Code(type=0) only. U8/cloud exchange belongs to cloud
    // allocation, not this game-login refresh: hand the fresh CLIENT code to
    // the game without redeeming it locally or replacing the active session.
    // H5's browser-wide refresh is deliberately a separate profile, not an
    // automatic replacement for the APK's independent game CLIENT code.
    // Neither profile falls back to the other on a server rejection.
    const auto result=profile_==Profile::WebRtc
        ? AuthorizeWebGame(passport_token_,device_token_,cancelled)
        : AuthorizeGame(passport_token_,device_token_,cancelled);
    if(result.status==109) blocked_=true;
    return result;
}

} // namespace cloudgenshin::endfield::login
