#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include <cassert>

namespace {int mode=0;unsigned calls=0;}
namespace opennow {
HttpResponse HttpClient::Post(const std::string&,const std::string&,const std::vector<std::string>&,
    const std::string&,const std::string&,HttpTransferControl control) const {
    ++calls;
    assert(!control.follow_redirects && control.max_body_bytes==1024*1024);
    if(mode>0) throw HttpRequestError(static_cast<CURLcode>(mode),503,"synthetic-secret-must-not-escape",
        {.captured=true,.requires_tls=true,.lookup_ms=1,.connect_ms=10,.tls_ms=25,.ready_ms=26,.total_ms=30000});
    if(mode==-1) throw std::runtime_error("synthetic-secret");
    return {200,R"({"retcode":0,"data":{"maintenance_info":null}})"};
}
}
int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::dispatch;
    for(const auto& profile:kGameProfiles) {
        PrepareRequest request{PrepareOperation::Status,std::string(profile.cloud_base)+"/dispatcher/api/statusCheck",
            "test-native","{}",{}};
        for(const int code:{6,7,28,35,60}) {
            mode=code;const auto result=SubmitPrepare(profile,request,nullptr);
            assert(result.status==PrepareStatus::TransportError && result.transport_error==code &&
                result.http_status==503 && result.retcode==0);
            assert(result.transport_diagnostics.captured && result.transport_diagnostics.ready_ms==26);
        }
        mode=-1;assert(SubmitPrepare(profile,request,nullptr).transport_error==0);
        mode=0;const auto good=SubmitPrepare(profile,request,nullptr);
        assert(good.status==PrepareStatus::Success && good.transport_error==0 && good.http_status==200);
        const auto before=calls;request.url="https://example.invalid/statusCheck";
        assert(SubmitPrepare(profile,request,nullptr).status==PrepareStatus::InvalidResponse && calls==before);
    }
}
