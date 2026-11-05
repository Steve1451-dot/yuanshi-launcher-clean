#include "endfield/audio_offer_policy.hpp"
#include <cassert>
#include <string>
int main() {
    using cloudgenshin::endfield::LowBandwidthAudioOffer;
    for (const std::string newline : {"\r\n", "\n"}) {
        const std::string video="m=video 9 RTP/SAVPF 96"+newline+"a=fmtp:96 profile-level-id=42e01f"+newline;
        const std::string audio="m=audio 9 RTP/SAVPF 109"+newline+
            "a=fmtp:109 stereo=1"+newline+"a=rtpmap:109 opus/48000/2"+newline;
        const auto offer=video+audio+"m=application 9 UDP/DTLS/SCTP webrtc-datachannel"+newline+"a=fingerprint:sha-256 synthetic";
        const auto low=LowBandwidthAudioOffer(offer);
        assert(low.starts_with(video));
        assert(low.find("stereo=1")==std::string::npos);
        assert(low.find("a=fmtp:109 maxaveragebitrate=16000;maxplaybackrate=16000;stereo=0")!=std::string::npos);
        assert(low.find("opus/48000/2")!=std::string::npos);
        assert(low.ends_with("a=fingerprint:sha-256 synthetic"));
        assert(LowBandwidthAudioOffer(low)==low);
        const auto missing=video+"m=audio 9 RTP/SAVPF 109"+newline+"a=rtpmap:109 opus/48000/2"+newline;
        assert(LowBandwidthAudioOffer(missing).find("maxaveragebitrate=16000")!=std::string::npos);
        assert(LowBandwidthAudioOffer(video)==video);
    }
    assert(LowBandwidthAudioOffer(std::string(65537,'x'))==std::string(65537,'x'));
}
