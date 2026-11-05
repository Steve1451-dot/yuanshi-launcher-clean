#include "mihoyo/gamer_login.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include <cassert>
#include <iostream>
namespace {
int mode=28;unsigned calls=0;
}
namespace opennow {
HttpResponse HttpClient::Post(const std::string&,const std::string&,const std::vector<std::string>&,
    const std::string&,const std::string&,HttpTransferControl) const {
    ++calls;
    if(mode>0) throw HttpRequestError(static_cast<CURLcode>(mode),0,"synthetic secret text must not escape");
    if(mode==-1) throw std::runtime_error("synthetic secret");
    return {200,"{\"retcode\":0}"};
}
}
int main() {
    using namespace cloudgenshin;
    const auto& profile=DefaultGameProfile();
    auth::GamerLoginRequest request;request.url=std::string(profile.cloud_base)+"/gamer/api/login";
    for(const int code:{6,28,35,60}) {
        mode=code;const auto result=auth::CheckGamerLogin(profile,request);
        assert(result.status==auth::GamerLoginStatus::TransportError && result.transport_error==code &&
            result.http_status==0 && result.retcode==0);
    }
    mode=-1;assert(auth::CheckGamerLogin(profile,request).transport_error==0);
    mode=0;const auto good=auth::CheckGamerLogin(profile,request);
    assert(good.status==auth::GamerLoginStatus::Success && good.transport_error==0 && good.http_status==200);
    request.url="https://example.invalid/login";
    assert(auth::CheckGamerLogin(profile,request).status==auth::GamerLoginStatus::InvalidResponse && calls==6);
    std::cout<<"PASS login HTTP numeric-only transport diagnostics, success and fixed-host rejection\n";
}
