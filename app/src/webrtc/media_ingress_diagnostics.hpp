#pragma once
#include <peer_connection.h>
#include <algorithm>
#include <climits>
#include <cstdint>

namespace opennow::webrtc {
// Called only at the existing five-second snapshots/teardown. Never raw packet
// bytes, SSRCs, endpoints, certificates, keys or per-packet log output.
template<class Report>
void ReportMediaIngress(const PeerTransportStats& s,Report report) {
    const auto count=[](std::uint32_t v) {return static_cast<int>(std::min(v,static_cast<std::uint32_t>(INT_MAX)));};
    report("RTC_RTP_COMPLETED",count(s.rtp_completed),0);
    report("RTC_RTP_DECRYPT_FAILURES",count(s.rtp_decrypt_failures),0);
    report("RTC_RTP_LAST_DECRYPT_ERROR",s.rtp_last_decrypt_error,0);
    report("RTC_RTCP_COMPLETED",count(s.rtcp_completed),0);
    report("RTC_RTCP_DECRYPT_FAILURES",count(s.rtcp_decrypt_failures),0);
    report("RTC_RTCP_LAST_DECRYPT_ERROR",s.rtcp_last_decrypt_error,0);
    report("RTC_RTP_LAST_PAYLOAD_TYPE",count(s.rtp_last_payload_type),0);
    report("RTC_RTP_PAYLOAD_REJECTED",count(s.rtp_payload_rejected),0);
    report("RTC_RTP_SSRC_REJECTED",count(s.rtp_ssrc_rejected),0);
    report("RTC_RTP_AUDIO_ROUTED",count(s.rtp_audio_routed),0);
    report("RTC_RTP_VIDEO_ROUTED",count(s.rtp_video_routed),0);
}
}
