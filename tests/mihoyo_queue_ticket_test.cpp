#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/production_profile.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <string>

int main() {
    using namespace cloudgenshin;
    // Synthetic signing profile: production app_id/game_biz keep the historical
    // signing vector meaningful while the keys stay non-production.
    constexpr GameProfile test_profile{"hk4e-test","hk4e_cn","测试","Test",
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn","https://hk4e-sdk.mihoyo.com/hk4e_cn",
        "test-key","test-key","clgm_cn","1.0.0","1.0.0",4,"19","",""};
    const auth::ComboLoginFields fields{"A", "C", "O", "T"};
    const auth::WebDeviceIdentity device{
        "Mozilla/5.0", "WebPC", "1.0", "Windows", "synthetic-id",
        "test-device", "test-model", "hk4e_cn", "zh-cn", "",
    };
    const auto poll = dispatch::BuildQueueRequest(test_profile, dispatch::QueueOperation::Poll,
        fields, device, "synthetic-ticket");
    assert(poll);
    assert(poll->url == "https://api-cloudgame.mihoyo.com/hk4e_cg_cn/dispatcher/api/getDispatchTicketInfo");
    assert(poll->headers.size() == 15);
    assert(std::find(poll->headers.begin(), poll->headers.end(),
                     "Content-Type: application/json") != poll->headers.end());
    const auto body = nlohmann::json::parse(poll->body);
    assert(body["biz_key"] == "hk4e_cn");
    assert(body["ticket"] == "synthetic-ticket");
    assert(body["sign"] == "926820fa6b56ad9769048eee3c4f7fb4330c83f1cc58084bf454e51c0da5a556");
    // A profile may carry a combo key that differs from the dispatch key: the
    // combo token header changes while the signed body stays identical.
    GameProfile separate_keys=test_profile;separate_keys.combo_app_key="different-combo-key";
    const auto separated=dispatch::BuildQueueRequest(separate_keys,dispatch::QueueOperation::Poll,
        fields,device,"synthetic-ticket");
    assert(separated && separated->headers != poll->headers && separated->body == poll->body);

    const auto ack = dispatch::BuildQueueRequest(test_profile, dispatch::QueueOperation::Acknowledge,
        fields, device, "synthetic-ticket");
    const auto leave = dispatch::BuildQueueRequest(test_profile, dispatch::QueueOperation::Leave,
        fields, device, "synthetic-ticket");
    assert(ack && leave);
    assert(ack->url == "https://api-cloudgame.mihoyo.com/hk4e_cg_cn/dispatcher/api/ackDispatchTicket");
    assert(leave->url == "https://api-cloudgame.mihoyo.com/hk4e_cg_cn/dispatcher/api/leaveDispatchQueue");
    assert(ack->body == poll->body && leave->body == poll->body);

    // Per-game queue endpoints follow the selected profile.
    const auto& sr=*FindGameProfile("hkrpg");
    const auto sr_poll=dispatch::BuildQueueRequest(sr,dispatch::QueueOperation::Poll,
        fields,NativeWebIdentity(sr,"test-device"),"synthetic-ticket");
    assert(sr_poll && sr_poll->url=="https://cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg/dispatcher/api/getDispatchTicketInfo");
    assert(nlohmann::json::parse(sr_poll->body)["biz_key"]=="hkrpg_cn");

    assert(!dispatch::BuildQueueRequest(test_profile,dispatch::QueueOperation::Poll,
        fields, device, "ticket\r\nInjected: yes"));
    assert(!dispatch::BuildQueueRequest(test_profile,dispatch::QueueOperation::Poll,
        fields, device, std::string(4097, 'x')));
    assert(!dispatch::BuildQueueRequest(test_profile,dispatch::QueueOperation::Poll,
        fields, device, ""));

    using dispatch::QueueActionStatus;
    assert(dispatch::ParseQueueActionResponse(200, R"({"retcode":0})").status ==
           QueueActionStatus::Success);
    const auto rejected = dispatch::ParseQueueActionResponse(200, R"({"retcode":-100})");
    assert(rejected.status == QueueActionStatus::ApiRejected && rejected.retcode == -100);
    assert(dispatch::ParseQueueActionResponse(302, R"({"retcode":0})").status ==
           QueueActionStatus::HttpError);
    assert(dispatch::ParseQueueActionResponse(200, "not json").status ==
           QueueActionStatus::InvalidResponse);
    assert(dispatch::ParseQueueActionResponse(200,
           R"({"retcode":18446744073709551615})").status ==
           QueueActionStatus::InvalidResponse);
    assert(dispatch::ParseQueueActionResponse(200,
           std::string(1024 * 1024 + 1, 'x')).status ==
           QueueActionStatus::InvalidResponse);
}
