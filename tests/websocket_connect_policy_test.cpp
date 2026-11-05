#include "websocket_connect_policy.hpp"
#include <cassert>

int main() {
    using namespace opennow::websocket;
    const auto query=ParseEndpoint("wss://example.invalid:443?sessionId=synthetic-secret");
    assert(query && query->host_header=="example.invalid:443");
    assert(query->request_target=="/?sessionId=synthetic-secret");
    assert(query->curl_url=="https://example.invalid:443?sessionId=synthetic-secret");
    const auto ipv6=ParseEndpoint("wss://[2001:db8::1]:8443/path?q=1");
    assert(ipv6 && ipv6->host_header=="[2001:db8::1]:8443" && ipv6->request_target=="/path?q=1");
    assert(ParseEndpoint("ws://example.invalid")->request_target=="/");
    for (const auto bad : {"wss://","https://example.invalid/","wss://u:p@example.invalid/",
        "wss://example.invalid/#x","wss://example.invalid/\r\nCookie:x","wss://[::1/x","wss://::1/x"})
        assert(!ParseEndpoint(bad));
    assert(UpgradeHttpStatus("HTTP/1.1 403 Forbidden\r\nCookie: synthetic-secret\r\n\r\n")==403);
    assert(UpgradeHttpStatus("HTTP/1.1 101\r\n")==101);
    assert(UpgradeHttpStatus("HTTP/1.1 101evil\r\n")==0);
    assert(UpgradeHttpStatus("HTTP/1.1 999 Bad\r\n")==0);
    assert(UpgradeHttpStatus("HTTP/1.1 abc Bad\r\n")==0);
    const auto endpoints=SecureEndpoints("wss://primary.invalid/",{"wss://primary.invalid/",
        "ws://insecure.invalid/","wss://alt.invalid/","wss://alt2.invalid/","wss://alt3.invalid/","wss://alt4.invalid/"});
    assert(endpoints.size()==4 && endpoints.front()=="wss://primary.invalid/" && endpoints.back()=="wss://alt3.invalid/");
    for (const auto status : {401,403,404,429}) {
        ConnectDiagnostics denied{ConnectStage::UpgradeHttpRejected,0,status,0};
        assert(!denied.may_try_alternative());
    }
    assert((ConnectDiagnostics{ConnectStage::UpgradeHttpRejected,0,503,0}).may_try_alternative());
    assert(!(ConnectDiagnostics{ConnectStage::Cancelled}).may_try_alternative());
    assert(!(ConnectDiagnostics{ConnectStage::UpgradeInvalid}).may_try_alternative());
}
