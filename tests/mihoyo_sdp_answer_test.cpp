#include "mihoyo/sdp_answer.hpp"
#include "mihoyo/sdp_answer_policy.hpp"
#include "mihoyo_sdp_answer_fixture.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::rtc;
    assert(PeerOfferWithinBounds(sdp_fixture::offer));
    assert(!PeerOfferWithinBounds(std::string(sdp_fixture::offer)+"a=ice-pwd:"+std::string(257,'x')+"\r\n"));
    assert(SdpAnswerMismatchMask(sdp_fixture::offer,sdp_fixture::local)==30);
    const auto result=NegotiateAnswer(sdp_fixture::offer,sdp_fixture::local);
    assert(result && result->video_payload==100 && result->audio_payload==109);
    assert(SdpAnswerMismatchMask(sdp_fixture::offer,result->sdp)==0);
    assert(result->sdp.find("sha-256 LOCAL")!=result->sdp.npos);
    assert(result->sdp.find("REMOTE")==result->sdp.npos && result->sdp.find("remote-password")==result->sdp.npos);
    assert(result->sdp.find("a=recvonly")!=result->sdp.npos && result->sdp.find("transport-cc")==result->sdp.npos);
    assert(result->sdp.find("rtx/90000")==result->sdp.npos);
    auto unsupported=std::string(sdp_fixture::offer);
    unsupported.replace(unsupported.find("H264/90000"),10,"VP9/90000");
    assert(!NegotiateAnswer(unsupported,sdp_fixture::local));
    unsupported=sdp_fixture::offer;unsupported.replace(unsupported.find("packetization-mode=1"),20,"packetization-mode=2");
    assert(!NegotiateAnswer(unsupported,sdp_fixture::local));
    unsupported=sdp_fixture::offer;unsupported.replace(unsupported.find("m=audio 9"),9,"m=audio 0");
    assert(!NegotiateAnswer(unsupported,sdp_fixture::local));
    unsupported=sdp_fixture::offer;unsupported.replace(unsupported.find("sctp-port:5000"),14,"sctp-port:6000");
    assert(!NegotiateAnswer(unsupported,sdp_fixture::local));
    assert(!NegotiateAnswer(sdp_fixture::offer,"v=0\r\n"));
    assert(!NegotiateAnswer(std::string(65537,'x'),sdp_fixture::local));
}
