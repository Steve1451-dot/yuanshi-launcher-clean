#include "mihoyo/sdp_answer_policy.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::rtc;
    const std::string offer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96 97\r\na=mid:0\r\n";
    const std::string answer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:0\r\n";
    assert(SdpAnswerMismatchMask(offer,answer)==0);
    assert(SdpAnswerMismatchMask(offer,"m=video 9 UDP/TLS/RTP/SAVPF 96\na=mid:video\n")==4);
    assert(SdpAnswerMismatchMask(offer,"m=audio 9 UDP/TLS/RTP/SAVP 111\na=mid:audio\n")==30);
    assert(SdpAnswerMismatchMask(offer,answer+answer)==1);
    assert(SdpAnswerMismatchMask(offer,"v=0\n")==32);
    assert(!SdpMediaShapes(std::string(65537,'x')));
    assert(!SdpMediaShapes("m=video 9 P 96\na=mid:0\na=mid:1\n"));
    assert(!SdpMediaShapes(std::string("m=video 9 P 96\n\0",16)));
}
