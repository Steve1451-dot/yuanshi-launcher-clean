#include "mihoyo/sdp_answer.hpp"
#include "mihoyo/sdp_answer_policy.hpp"
#include <charconv>
#include <set>

namespace cloudgenshin::rtc {
namespace {
std::vector<std::string> Lines(std::string_view text) {
    std::vector<std::string> result;
    while(!text.empty()) {
        const auto end=text.find('\n'); auto line=text.substr(0,end);
        if(line.ends_with('\r')) line.remove_suffix(1);
        result.emplace_back(line);
        if(end==text.npos) break;
        text.remove_prefix(end+1);
    }
    return result;
}
std::string Value(const std::vector<std::string>& lines,std::string_view prefix) {
    for(const auto& line:lines) if(line.starts_with(prefix)) return line.substr(prefix.size());
    return {};
}
int Payload(std::string_view value) {
    int n=-1;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
    return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() && n>=0 && n<=127 ? n : -1;
}
}
std::optional<NegotiatedAnswer> NegotiateAnswer(std::string_view offer,std::string_view local,AnswerIssue* issue) {
    const auto stage=[&](AnswerIssue value) {if(issue) *issue=value;};
    stage(AnswerIssue::InvalidDescription);
    const auto shapes=SdpMediaShapes(offer),local_shapes=SdpMediaShapes(local);
    if(!shapes || !local_shapes || shapes->size()!=3) return std::nullopt;
    const auto local_lines=Lines(local),offer_lines=Lines(offer);
    stage(AnswerIssue::LocalTransport);
    const auto ufrag=Value(local_lines,"a=ice-ufrag:"),pwd=Value(local_lines,"a=ice-pwd:");
    const auto fingerprint=Value(local_lines,"a=fingerprint:sha-256 "),setup=Value(local_lines,"a=setup:");
    if(ufrag.empty() || pwd.empty() || fingerprint.empty() || (setup!="active" && setup!="passive")) return std::nullopt;
    std::vector<std::vector<std::string>> sections;
    for(const auto& line:offer_lines) {
        if(line.starts_with("m=")) sections.emplace_back();
        if(!sections.empty()) sections.back().push_back(line);
    }
    std::set<std::string> kinds,mids;
    stage(AnswerIssue::MediaLayout);
    std::string bundle="a=group:BUNDLE";
    for(const auto& shape:*shapes) {
        unsigned port=0;const auto parsed=std::from_chars(shape.port.data(),shape.port.data()+shape.port.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=shape.port.data()+shape.port.size() || port==0 || port>65535 ||
           shape.mid.empty() || shape.mid.size()>64 || shape.mid.find_first_of(" \t")!=shape.mid.npos ||
           !mids.insert(shape.mid).second || !kinds.insert(shape.kind).second) return std::nullopt;
        bundle+=' '+shape.mid;
    }
    // This peer has one shared UDP/DTLS transport, not independent transports.
    const auto offered_bundle=Value(offer_lines,"a=group:BUNDLE");
    stage(AnswerIssue::Bundle);
    std::istringstream bundle_in(offered_bundle); std::set<std::string> bundled;std::string mid;
    while(bundle_in>>mid) bundled.insert(mid);
    if(bundled!=mids || !kinds.contains("video") || !kinds.contains("audio") || !kinds.contains("application")) return std::nullopt;
    NegotiatedAnswer result;
    const auto append=[&](const std::string& line) {result.sdp+=line+"\r\n";};
    append("v=0");append("o=- 1 1 IN IP4 0.0.0.0");append("s=-");append("t=0 0");append(bundle);
    const auto transport=[&] {
        append("c=IN IP4 0.0.0.0");append("a=ice-ufrag:"+ufrag);append("a=ice-pwd:"+pwd);
        append("a=fingerprint:sha-256 "+fingerprint);append("a=setup:"+setup);
    };
    for(std::size_t i=0;i<shapes->size();++i) {
        const auto& shape=(*shapes)[i];const auto& lines=sections[i];
        if(shape.kind=="application") {
            stage(AnswerIssue::DataTransport);
            if(shape.transport!="UDP/DTLS/SCTP" || shape.formats!=std::vector<std::string>{"webrtc-datachannel"} ||
               Value(lines,"a=sctp-port:")!="5000") return std::nullopt;
            append("m=application 9 UDP/DTLS/SCTP webrtc-datachannel");transport();
            append("a=mid:"+shape.mid);append("a=sctp-port:5000");append("a=max-message-size:262144");
        } else {
            stage(AnswerIssue::MediaTransport);
            if(shape.transport!="UDP/TLS/RTP/SAVPF" ||
               std::find(lines.begin(),lines.end(),"a=rtcp-mux")==lines.end() ||
               std::find(lines.begin(),lines.end(),"a=recvonly")!=lines.end() ||
               std::find(lines.begin(),lines.end(),"a=inactive")!=lines.end()) return std::nullopt;
            const bool video=shape.kind=="video";
            stage(AnswerIssue::Codec);
            std::string selected,rtpmap;
            for(const auto& format:shape.formats) {
                if(Payload(format)<0) continue;
                const auto map=Value(lines,"a=rtpmap:"+format+" ");
                if(video) {
                    const auto fmtp=Value(lines,"a=fmtp:"+format+" ");
                    const auto mode=fmtp.find("packetization-mode=");
                    if(mode!=fmtp.npos) {
                        const auto value=fmtp.substr(mode+19,fmtp.find(';',mode)-(mode+19));
                        if(value!="0" && value!="1") continue;
                    }
                }
                if((video && map=="H264/90000") || (!video && map=="opus/48000/2")) {
                    selected=format;rtpmap=map;break;
                }
            }
            if(selected.empty()) return std::nullopt;
            (video ? result.video_payload : result.audio_payload)=Payload(selected);
            append("m="+shape.kind+" 9 "+shape.transport+" "+selected);transport();append("a=mid:"+shape.mid);
            append("a=recvonly");append("a=rtcp-mux");append("a=rtpmap:"+selected+" "+rtpmap);
            const auto fmtp=Value(lines,"a=fmtp:"+selected+" ");
            if(!fmtp.empty()) append("a=fmtp:"+selected+" "+fmtp);
            // Do not advertise transport-wide CC/extensions we do not implement.
            for(const auto& line:lines)
                if(line=="a=rtcp-fb:"+selected+" nack" || line=="a=rtcp-fb:"+selected+" nack pli") append(line);
        }
        for(const auto& line:local_lines) if(line.starts_with("a=candidate:")) append(line);
    }
    if(result.audio_payload==result.video_payload || SdpAnswerMismatchMask(offer,result.sdp)!=0) return std::nullopt;
    stage(AnswerIssue::None);
    return result;
}
}
