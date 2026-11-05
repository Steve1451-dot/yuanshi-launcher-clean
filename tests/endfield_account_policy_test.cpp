#include "endfield/account_policy.hpp"
#include <algorithm>
#include <cassert>

int main() {
    using namespace cloudgenshin::endfield::account;
    const auto headers=Headers("synthetic-client-id",true);
    for(const char* expected:{"X-DeviceType: 7","X-DeviceId: synthetic-client-id",
                             "X-Captcha-Version: 4.0","Content-Type: application/json"})
        assert(std::find(headers.begin(),headers.end(),expected)!=headers.end());
    const auto get=Headers("synthetic-client-id",false);
    assert(std::find(get.begin(),get.end(),"Content-Type: application/json")==get.end());
    assert(QueryValue("test+/=&?# %") == "test%2B%2F%3D%26%3F%23%20%25");
    assert(QueryValue("AZaz09-_.~")=="AZaz09-_.~");
    assert(Token(R"({"status":0,"data":{"token":"synthetic"}})")=="synthetic");
    assert(Token(R"({"code":0,"data":"{\"token\":\"synthetic\"}"})")=="synthetic");
    assert(!Token(R"({"data":{"token":"synthetic"}})")); // Missing status is not success.
    assert(!Token(R"({"status":1,"data":{"token":"synthetic"}})"));
    assert(!Token(R"({"status":"0","data":{"token":"synthetic"}})"));
    assert(!Token(R"({"status":0,"data":{"token":123}})"));
    assert(!Parse("not-json").parsed);
    assert(!Parse(R"({"status":18446744073709551615})").parsed);
    assert(Agreement(R"({"status":0,"data":{"authorized":true,"isLatestUserAgreement":true}})")==AgreementState::Accepted);
    assert(Agreement(R"({"status":0,"authorized":1,"isLatestUserAgreement":1})")==AgreementState::Accepted);
    assert(Agreement(R"({"status":0,"data":{"authorized":true,"isLatestUserAgreement":false}})")==AgreementState::NeedsConsent);
    assert(Agreement(R"({"status":0,"data":{"authorized":false,"isLatestUserAgreement":true}})")==AgreementState::NeedsConsent);
    assert(Agreement(R"({"status":0,"data":{"authorized":"true","isLatestUserAgreement":true}})")==AgreementState::Invalid);
    assert(Agreement(R"({"status":0,"data":{"authorized":true}})")==AgreementState::Invalid);
    assert(Agreement(R"({"status":109,"data":{"authorized":true,"isLatestUserAgreement":true}})")==AgreementState::Invalid);
    const auto error=Parse(R"({"status":109,"data":{}})");assert(error.parsed && error.status==109);
    const auto body=R"({"status":0,"data":{"list":[
        {"appCode":"other","bindingList":[{"uid":"wrong","isDefault":true}]},
        {"appCode":"game","bindingList":[{"uid":"fallback"},{"uid":"official","isOfficial":true},
          {"uid":"default","isDefault":true}]}]}})";
    assert(BindingUid(body,"game")=="default");
    assert(!BindingUid(body,"missing"));
    assert(BindingUid(R"({"status":0,"data":{"list":[{"appCode":"game","bindingList":[
        {"uid":"first-official","isOfficial":true},{"uid":"last-official","isOfficial":true}]}]}})","game")=="last-official");
    assert(BindingUid(R"({"status":0,"data":{"list":[{"appCode":"game","bindingList":[
        {"uid":2.3,"isDefault":true},{"uid":-4,"isDefault":true},{"uid":42,"isOfficial":true}]}]}})","game")=="42");
    assert(!BindingUid(R"({"status":0,"data":{"list":[]}})","game"));
    assert(!BindingUid(R"({"status":1,"data":{"list":[]}})","game"));
}
