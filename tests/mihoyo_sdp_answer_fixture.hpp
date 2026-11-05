#pragma once
namespace sdp_fixture {
// Synthetic descriptions only. The differing order, MIDs and payloads model
// negotiation requirements, not a captured service session.
inline constexpr char offer[]=
    "v=0\r\na=group:BUNDLE 0 1 2\r\na=ice-ufrag:remote\r\na=ice-pwd:remote-password\r\n"
    "a=fingerprint:sha-256 REMOTE\r\na=setup:actpass\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 109 0\r\na=mid:0\r\na=sendonly\r\na=rtcp-mux\r\n"
    "a=rtpmap:109 opus/48000/2\r\na=fmtp:109 minptime=10;useinbandfec=1\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 100 101\r\na=mid:1\r\na=sendonly\r\na=rtcp-mux\r\n"
    "a=rtpmap:100 H264/90000\r\na=fmtp:100 packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtpmap:101 rtx/90000\r\na=fmtp:101 apt=100\r\na=rtcp-fb:100 nack\r\n"
    "a=rtcp-fb:100 nack pli\r\na=rtcp-fb:100 transport-cc\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:2\r\na=sctp-port:5000\r\n";
inline constexpr char local[]=
    "v=0\r\na=ice-ufrag:local\r\na=ice-pwd:local-password\r\n"
    "a=fingerprint:sha-256 LOCAL\r\na=setup:active\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVP 111\r\na=mid:audio\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:datachannel\r\na=sctp-port:5000\r\n"
    "a=candidate:0 1 UDP 120 192.0.2.2 50000 typ host\r\n";
}
