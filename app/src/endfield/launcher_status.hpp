#pragma once

#include "endfield/hg_login.hpp"
#include <optional>
#include <string_view>

namespace cloudgenshin::endfield {
struct GameAuthorizationStatus {
    const char* event;
    long code;
    long http;
};
// A saved account is not a usable game grant. Publish every terminal result
// to the launcher, including HTTP 200 responses that require user action.
inline GameAuthorizationStatus DescribeGameAuthorization(const login::GameAuthorization& result) {
    const char* event=result.status==109 ? "ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED"
        : result.status==-23 ? "ENDFIELD_GAME_AGREEMENT_REQUIRED"
        : result.ready() ? "ENDFIELD_GAME_AUTH_READY" : "ENDFIELD_GAME_AUTH_FAILED";
    return {event,result.status==0 && !result.ready() ? -21 : result.status,result.http};
}

struct LauncherNotice {
    const char* title;
    const char* detail;
    bool needs_action;
};
inline std::optional<LauncherNotice> DescribeLauncherStatus(std::string_view status) {
    if(status=="ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED")
        return LauncherNotice{"Device confirmation required",
            "Complete device confirmation in the official client, then try Start again.",true};
    if(status=="ENDFIELD_GAME_AGREEMENT_REQUIRED")
        return LauncherNotice{"Agreement confirmation required",
            "Confirm the agreement in the official client, then try Start again.",true};
    if(status=="ENDFIELD_GAME_AUTH_FAILED")
        return LauncherNotice{"Game authorization failed",
            "Login is kept. Game authorization did not complete.",true};
    if(status=="ENDFIELD_PREVIOUS_SAVE_PENDING")
        return LauncherNotice{"Finishing the previous save",
            "Waiting for the official service. You can still cancel.",false};
    if(status=="ENDFIELD_STREAM_DISPATCH_FAILED")
        return LauncherNotice{"Could not join the queue",
            "The launch request failed. Login is kept.",true};
    if(status=="ENDFIELD_STREAM_NODES_FAILED")
        return LauncherNotice{"Could not choose a server",
            "No usable server was returned. Login is kept.",true};
    return std::nullopt;
}
inline bool PublishesLauncherStatus(std::string_view event,bool requested) {
    // A critical result may not become a log-only event and leave the old
    // server-selection/queue state on screen, even if a caller asks for that.
    return requested || DescribeLauncherStatus(event).has_value();
}
} // namespace cloudgenshin::endfield
