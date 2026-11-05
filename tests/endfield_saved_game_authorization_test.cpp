#include "endfield/saved_game_authorization.hpp"
#include "endfield/endfield_store.hpp"
#include <cassert>
#include <iostream>

namespace {
using namespace cloudgenshin::endfield;
store::Loaded current;
login::GameAuthorization reply;
unsigned reads=0,requests=0,web_requests=0;
std::string proof_used;
}

// Synthetic account and HTTP boundaries: no files, sockets or real credentials.
store::Loaded store::Load(const std::string& directory) {
    assert(directory=="synthetic-app-home");++reads;return current;
}
login::GameAuthorization login::AuthorizeGame(const std::string& passport,const std::string& proof,
                                             const std::atomic_bool*) {
    assert(passport=="synthetic-passport");++requests;proof_used=proof;return reply;
}
login::GameAuthorization login::AuthorizeWebGame(const std::string& passport,const std::string& proof,
                                                const std::atomic_bool*) {
    assert(passport=="synthetic-passport");++web_requests;proof_used=proof;return reply;
}

int main() {
    using namespace cloudgenshin::endfield;
    assert((login::GameAuthorization{"synthetic",0,200}).ready());
    assert(!(login::GameAuthorization{"",0,200}).ready());
    assert(!(login::GameAuthorization{"synthetic",109,200}).ready());
    assert(!(login::GameAuthorization{std::string(4097,'x'),0,200}).ready());
    login::SavedGameAuthorization auth("synthetic-app-home","synthetic-passport","",109);
    current={store::LoadStatus::Ready,"synthetic-passport","",""};
    for(int i=0;i<3;++i) assert(auth.request(nullptr).status==109);
    assert(requests==0); // Repeated game requests cannot approve a device check.
    current={store::LoadStatus::Ready,"synthetic-other-account","","synthetic-wrong-proof"};
    assert(auth.request(nullptr).status==109 && requests==0);
    current={store::LoadStatus::Unreadable,std::nullopt,"",""};
    assert(auth.request(nullptr).status==109 && requests==0);
    current={store::LoadStatus::Ready,"synthetic-passport","","synthetic-verified-proof"};
    reply={"synthetic-oauth-code",0,200};
    const auto recovered=auth.request(nullptr);
    assert(recovered.status==0 && recovered.code=="synthetic-oauth-code");
    assert(requests==1 && proof_used=="synthetic-verified-proof");
    assert(auth.request(nullptr).status==0 && requests==2); // A new login needs a fresh code.
    reply={"",109,200};
    assert(auth.request(nullptr).status==109 && requests==3);
    assert(auth.request(nullptr).status==109 && requests==3); // Rejected proof stays blocked.
    current.device_token="synthetic-new-verified-proof";reply={"synthetic-next-code",0,200};
    assert(auth.request(nullptr).status==0 && requests==4 && proof_used==current.device_token);
    std::atomic_bool cancelled{true};const auto previous_reads=reads;
    assert(auth.request(&cancelled).status==-2 && reads==previous_reads && requests==4);
    reply={"",11204,200};
    const auto rejected=auth.request(nullptr);
    assert(rejected.status==11204 && rejected.code.empty());
    login::SavedGameAuthorization launch("synthetic-app-home","synthetic-passport",current.device_token,0);
    reply={"synthetic-post-queue-code",0,200};
    const auto previous_requests=requests;
    const auto fresh_start=launch.request(nullptr);
    assert(fresh_start.code=="synthetic-post-queue-code" && fresh_start.status==0);
    assert(requests==previous_requests+1 && proof_used==current.device_token);
    // Profiles must not accidentally exchange the APK code or bypass the
    // companion WebRTC authorization chain; no fallback on a server rejection.
    assert(web_requests==0);
    login::SavedGameAuthorization web("synthetic-app-home","synthetic-passport",current.device_token,0,
        login::SavedGameAuthorization::Profile::WebRtc);
    const auto android_requests=requests;
    assert(web.request(nullptr).status==0 && web_requests==1 && requests==android_requests);
    reply={"",109,200};
    assert(web.request(nullptr).status==109 && web_requests==2);
    assert(web.request(nullptr).status==109 && web_requests==2 && requests==android_requests);
    std::cout<<"PASS Endfield device proof: same-account recovery, bounded rejection, cancellation\n";
}
