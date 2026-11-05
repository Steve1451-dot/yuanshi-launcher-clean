#include "webrtc/media_ingress_diagnostics.hpp"
#include <map>
#include <string>
#include <cassert>
#include <iostream>
int main() {
    PeerTransportStats stats{};
    stats.rtp_completed=UINT32_MAX;stats.rtp_decrypt_failures=3;stats.rtcp_completed=4;
    stats.rtp_last_payload_type=100;stats.rtp_payload_rejected=2;stats.rtp_ssrc_rejected=1;
    stats.rtp_audio_routed=77;stats.rtp_video_routed=88;stats.rtp_last_decrypt_error=7;
    stats.rtcp_last_decrypt_error=9;stats.rtcp_decrypt_failures=1;
    std::map<std::string,int> events;
    opennow::webrtc::ReportMediaIngress(stats,[&](const char* event,int value,long http) {
        assert(http==0 && !events.contains(event));events[event]=value;
    });
    assert(events.size()==11 && events["RTC_RTP_COMPLETED"]==INT_MAX && events["RTC_RTP_VIDEO_ROUTED"]==88 &&
        events["RTC_RTP_AUDIO_ROUTED"]==77 && events["RTC_RTP_PAYLOAD_REJECTED"]==2 &&
        events["RTC_RTP_SSRC_REJECTED"]==1 && events["RTC_RTP_LAST_PAYLOAD_TYPE"]==100 &&
        events["RTC_RTP_DECRYPT_FAILURES"]==3 && events["RTC_RTP_LAST_DECRYPT_ERROR"]==7 &&
        events["RTC_RTCP_COMPLETED"]==4 && events["RTC_RTCP_DECRYPT_FAILURES"]==1 &&
        events["RTC_RTCP_LAST_DECRYPT_ERROR"]==9);
    std::cout<<"PASS numeric-only media ingress counters, retained errors and saturation\n";
}
