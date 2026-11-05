#include "mihoyo/qr_login.hpp"
#include <iostream>
// Optional anonymous live-response verification. Input stays in process memory;
// output is ONLY status booleans. Do not redirect stdin to a capture/log file.
int main() {
    using namespace cloudgenshin::auth;
    std::string body;
    while (std::getline(std::cin,body)) {
        if (body.size()>64*1024) return 1;
        const auto fetched=ParseQrFetchResponse(200,body);
        if (fetched.status==QrStatus::Ready) {std::cout<<"PASS anonymous QR fetch parser\n";continue;}
        const auto queried=ParseQrQueryResponse(200,body);
        if (queried.status!=QrStatus::Waiting) {std::cout<<"FAIL anonymous QR response schema\n";return 1;}
        std::cout<<"PASS anonymous QR waiting parser\n";
    }
}
