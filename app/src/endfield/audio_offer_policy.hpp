#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace cloudgenshin::endfield {
inline constexpr int kLowBandwidthAudioBitrate = 16000;
// RFC 7587 receiver preference, NOT proof of the pod's actual encoder rate.
// Keep Opus RTP clock and decoder PCM at 48 kHz / two output channels even
// for mono. Alter only the audio Opus fmtp, never video/ICE/DTLS or ptime.
inline std::string LowBandwidthAudioOffer(const std::string& sdp) {
    if (sdp.size() > 65536) return sdp;
    const std::string newline = sdp.find("\r\n") == std::string::npos ? "\n" : "\r\n";
    const bool terminated = sdp.ends_with(newline);
    std::vector<std::string> lines;
    for (std::size_t from=0; from<sdp.size();) {
        const auto end=sdp.find(newline,from);
        lines.push_back(sdp.substr(from,end==std::string::npos ? end : end-from));
        if (end==std::string::npos) break;
        from=end+newline.size();
    }
    bool audio=false;
    std::string payload;
    for (const auto& line:lines) {
        if (line.starts_with("m=")) audio=line.starts_with("m=audio ");
        if (audio && line.starts_with("a=rtpmap:") && line.ends_with(" opus/48000/2")) {
            const auto space=line.find(' ');
            payload=line.substr(9,space-9);
            break;
        }
    }
    if (payload.empty() || payload.find_first_not_of("0123456789")!=std::string::npos) return sdp;
    const std::string prefix="a=fmtp:"+payload+" ";
    const std::string preference=prefix+"maxaveragebitrate=16000;maxplaybackrate=16000;stereo=0";
    audio=false;
    bool added=false;
    std::string result;
    const auto append=[&](const std::string& line) {result+=line;result+=newline;};
    for (const auto& line:lines) {
        if (line.starts_with("m=")) audio=line.starts_with("m=audio ");
        if (audio && line.starts_with(prefix)) continue; // Replace, never duplicate.
        append(line);
        if (audio && !added && line=="a=rtpmap:"+payload+" opus/48000/2") {
            append(preference);added=true;
        }
    }
    if (!terminated && result.ends_with(newline)) result.resize(result.size()-newline.size());
    return result;
}
}
