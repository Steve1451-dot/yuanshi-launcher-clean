#include "endfield/launcher_status.hpp"
#include <cassert>
#include <string_view>

int main() {
    using namespace cloudgenshin::endfield;
    const auto ready=DescribeGameAuthorization({"synthetic-code",0,200});
    assert(std::string_view(ready.event)=="ENDFIELD_GAME_AUTH_READY" && ready.code==0);
    assert(!DescribeLauncherStatus(ready.event));
    const auto empty=DescribeGameAuthorization({"",0,200});
    assert(std::string_view(empty.event)=="ENDFIELD_GAME_AUTH_FAILED" && empty.code==-21);
    assert(DescribeLauncherStatus(empty.event)->needs_action);
    for(const auto code:{109L,-23L,-1028L,11204L}) {
        const auto blocked=DescribeGameAuthorization({"",code,200});
        assert(blocked.code==code && blocked.http==200);
        const auto notice=DescribeLauncherStatus(blocked.event);
        assert(notice && notice->needs_action);
        assert(std::string_view(notice->title)!="Signed in");
        assert(PublishesLauncherStatus(blocked.event,false)); // Regression: log-only failure.
    }
    assert(std::string_view(DescribeGameAuthorization({"synthetic-code",109,200}).event)==
        "ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED");
    assert(std::string_view(DescribeGameAuthorization({"",-23,200}).event)==
        "ENDFIELD_GAME_AGREEMENT_REQUIRED");
    assert(DescribeLauncherStatus("ENDFIELD_STREAM_DISPATCH_FAILED")->needs_action);
    assert(DescribeLauncherStatus("ENDFIELD_STREAM_NODES_FAILED")->needs_action);
    assert(!DescribeLauncherStatus("ENDFIELD_PREVIOUS_SAVE_PENDING")->needs_action);
    assert(PublishesLauncherStatus("ENDFIELD_STREAM_DISPATCH_FAILED",false));
    assert(PublishesLauncherStatus("ENDFIELD_PREVIOUS_SAVE_PENDING",false));
    assert(!PublishesLauncherStatus("ENDFIELD_QUEUE_NOT_ACTIVE",false)); // Cleanup cannot erase the cause.
    assert(!PublishesLauncherStatus("ENDFIELD_SDK_PACKED_KIND",false));
    for(const auto* normal:{"NODE_CANDIDATES_READY","ENDFIELD_TOKEN_READY","ENDFIELD_LOGIN_OK",
                           "QUEUED_POSITION","ENDFIELD_STREAM_STARTING"})
        assert(!DescribeLauncherStatus(normal));
}
