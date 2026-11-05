#include "mihoyo/sdk_report_diagnostics.hpp"
#include <cassert>
#include <optional>
#include <string>
int main() {
    using namespace cloudgenshin::sdk;using Json=nlohmann::json;
    const auto report=InspectGameReport(Json{{"c_body",R"({"data":{"retcode":-202,"error_code":"4301","message":"35-4301 synthetic-secret"},"uid":"secret-uid","token":"synthetic-token"})"}});
    assert(report.size()==4);
    assert(std::string(report[0].event)=="GAME_SDK_REPORT_BODY_KIND" && report[0].value==2);
    bool ret=false,code=false,known=false;
    for(const auto& item:report) {
        const std::string tag=item.event;assert(tag.find("secret")==std::string::npos);
        if(tag=="GAME_SDK_REPORT_RETCODE") {ret=true;assert(item.value==-202);}
        if(tag=="GAME_SDK_REPORT_ERROR_CODE") {code=true;assert(item.value==4301);}
        if(tag=="GAME_SDK_REPORT_LOGIN_35_4301") {known=true;assert(item.value==1);}
    }
    assert(ret && code && known);
    assert(InspectGameReport(Json{{"c_body",{{"retcode",18446744073709551615ULL},{"code","secret123"}}}}).size()==1);
    assert(InspectGameReport(Json{{"c_body",{{"code",true}}}}).size()==1);
    assert(InspectGameReport(Json{{"c_body",std::string(16385,'x')}}).size()==1);
    Json deep={{"code",123}};for(int i=0;i<8;++i) deep=Json{{"data",deep}};
    assert(InspectGameReport(Json{{"c_body",deep}}).size()==1);
    assert(InspectGameReport(Json::object()).empty());
    assert(GameReportSchema(Json{{"c_body",R"({"error_code":4301,"msg":"synthetic-secret"})"}})=="object error_code:number msg:string");
    assert(GameReportSchema(Json{{"c_body","opaque synthetic-secret"}})=="text");
    const auto array=InspectGameReport(Json{{"c_body",Json::array({Json{{"detail",{{"error_code",4301}}}}})}});
    assert(array.size()==2 && array.back().value==4301);
    const auto net=InspectGameReport(Json{{"c_body",R"({"msg":"network error 1001_2 retry"})"}});
    bool net_found=false;
    for(const auto& item:net) if(std::string(item.event)=="GAME_SDK_REPORT_NET_1001_2") net_found=true;
    assert(net_found);
    assert(GameCloudDataSchema(Json{{"token","synthetic-secret"},{"uid_123",5}})=="object token:string [dynamic-key]");
    assert(GameCloudDataSchema("opaque-secret")=="opaque-string");
    const std::string nested_text=std::string(4096,'[')+"0"+std::string(4096,']');
    assert(InspectGameReport(Json{{"c_body",nested_text}}).size()==1);
    assert(GameReportSchema(Json{{"c_body",nested_text}})=="text");
    assert(GameCloudDataSchema(nested_text)=="opaque-string");
    assert(ReportNetworkHintMask("Could not RESOLVE HOST synthetic-secret")==1);
    assert(ReportNetworkHintMask("TLS error; connection refused; timeout")==14);
    assert(ReportNetworkHintMask("HTTP/1.1 403; HTTP/1.1 503")==48);
    assert(ReportNetworkHintMask(std::string(16385,'x'))==0);
    assert(InspectGameReport(Json{{"c_body",{{"uid","timeout secret"}}}}).size()==1);
    const auto hint=InspectGameReport(Json{{"c_body",{{"message","getaddrinfo timeout synthetic-secret"}}}});
    assert(hint.size()==2 && std::string(hint.back().event)=="GAME_SDK_REPORT_NETWORK_HINT_MASK" && hint.back().value==3);
}
