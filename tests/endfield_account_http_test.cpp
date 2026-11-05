#include "endfield/hg_api.hpp"
#include "endfield/hg_login.hpp"
#include "http_client.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
int calls=0;
opennow::HttpResponse grant_reply,binding_reply;
const std::atomic_bool* expected_cancel=nullptr;
std::string device_header;
bool qr_mode=false;
}

opennow::HttpResponse opennow::HttpClient::Request(const std::string& method,const std::string& url,
    const std::string&,const std::vector<std::string>& headers,const std::string& body,
    const std::string&,opennow::HttpTransferControl control) const {
    // Entirely synthetic transport. Never resolves a host or opens a socket.
    ++calls;assert(control.cancelled==expected_cancel);
    assert(control.timeout_ms==15000 && control.max_body_bytes>0);
    if(control.cancelled && *control.cancelled) throw std::runtime_error("cancelled synthetic request");
    const auto id=std::find_if(headers.begin(),headers.end(),[](const auto& h){return h.starts_with("X-DeviceId: ");});
    assert(id!=headers.end());
    if(device_header.empty()) device_header=*id;else assert(device_header==*id);
    assert(std::find(headers.begin(),headers.end(),"X-DeviceType: 7")!=headers.end());
    if(qr_mode) {
        if(calls==1) assert(method=="POST" && url.ends_with("/general/v1/gen_scan/login"));
        else if(calls==2) assert(method=="GET" && url.ends_with("/general/v1/scan_status?scanId=synthetic%2B%2F%3F"));
        else assert(calls==3 && method=="POST" && url.ends_with("/user/auth/v1/token_by_scan_code"));
        return {200,"{}"};
    }
    if(calls==1) {
        assert(method=="POST" && url.ends_with("/user/oauth2/v2/grant"));
        assert(body.find("synthetic-input")!=std::string::npos);
        return grant_reply;
    }
    assert(calls==2 && method=="GET");
    assert(url.ends_with("/account/binding/v1/binding_list?token=dummy%2B%2F%3D%26%3F%23&appCode=4df8f5a7c2ad711b497a"));
    return binding_reply;
}

int main() {
    using namespace cloudgenshin::endfield::api;
    std::atomic_bool qr_cancel{false};expected_cancel=&qr_cancel;qr_mode=true;
    cloudgenshin::endfield::login::CreateScanSession(&qr_cancel);
    cloudgenshin::endfield::login::PollScanStatus("synthetic+/?",&qr_cancel);
    cloudgenshin::endfield::login::ExchangeScanCode("synthetic-code",&qr_cancel);
    assert(calls==3);qr_mode=false;expected_cancel=nullptr;
    const auto setup=[] {calls=0;grant_reply={200,R"({"status":0,"data":{"token":"dummy+/=&?#"}})"};
        binding_reply={200,R"({"status":0,"data":{"list":[{"appCode":"4df8f5a7c2ad711b497a","bindingList":[{"uid":"test-uid"}]}]}})"};};
    setup();assert(FetchBindingUid("").uid.empty() && calls==0);
    auto result=FetchBindingUid("synthetic-input");
    assert(result.uid=="test-uid" && calls==2 && result.stage==UidResult::Stage::BindingList);
    setup();grant_reply={400,"{}"};result=FetchBindingUid("synthetic-input");
    assert(result.uid.empty() && calls==1 && result.stage==UidResult::Stage::Grant && result.error.http==400);
    setup();grant_reply={200,R"({"status":109,"data":{}})"};result=FetchBindingUid("synthetic-input");
    assert(result.error.code==109 && result.error.http==200 && calls==1);
    setup();binding_reply={400,"{}"};result=FetchBindingUid("synthetic-input");
    assert(result.stage==UidResult::Stage::BindingList && result.error.http==400 && result.error.code==-24);
    setup();binding_reply={200,R"({"status":73,"data":{}})"};result=FetchBindingUid("synthetic-input");
    assert(result.error.code==73 && result.error.http==200);
    setup();binding_reply={200,R"({"data":{"list":[]}})"};result=FetchBindingUid("synthetic-input");
    assert(result.uid.empty() && result.error.code==-25);
    setup();std::atomic_bool cancel{false};expected_cancel=&cancel;
    result=FetchBindingUid("synthetic-input",&cancel);assert(result.uid=="test-uid");
    setup();cancel=true;bool threw=false;
    try {FetchBindingUid("synthetic-input",&cancel);} catch(const std::runtime_error&) {threw=true;}
    assert(threw && calls==1);
}
