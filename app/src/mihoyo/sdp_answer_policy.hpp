#pragma once
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "mihoyo/ice_candidate_policy.hpp"

namespace cloudgenshin::rtc {
struct SdpMediaShape {
    std::string kind, port, transport, mid;
    std::vector<std::string> formats;
};
inline std::optional<std::vector<SdpMediaShape>> SdpMediaShapes(std::string_view sdp) {
    if(sdp.empty() || sdp.size()>65536) return std::nullopt;
    std::vector<SdpMediaShape> media;
    unsigned lines=0;
    while(!sdp.empty()) {
        const auto end=sdp.find('\n');
        auto line=sdp.substr(0,end);
        if(line.ends_with('\r')) line.remove_suffix(1);
        if(++lines>512 || line.size()>2048) return std::nullopt;
        for(unsigned char c:line) if(c<32 || c>126) return std::nullopt;
        if(line.starts_with("m=")) {
            if(media.size()==8) return std::nullopt;
            SdpMediaShape shape; std::string format;
            std::istringstream in(std::string{line.substr(2)});
            if(!(in>>shape.kind>>shape.port>>shape.transport)) return std::nullopt;
            while(in>>format) shape.formats.push_back(format);
            if(shape.formats.empty()) return std::nullopt;
            media.push_back(std::move(shape));
        } else if(line.starts_with("a=mid:") && !media.empty()) {
            if(!media.back().mid.empty()) return std::nullopt;
            media.back().mid=line.substr(6);
        }
        if(end==sdp.npos) break;
        sdp.remove_prefix(end+1);
    }
    return media;
}
// Bound every field copied by the legacy peer parser before passing it input.
inline bool PeerOfferWithinBounds(std::string_view sdp) {
    if(!SdpMediaShapes(sdp)) return false;
    unsigned candidates=0;
    while(!sdp.empty()) {
        const auto end=sdp.find("\r\n");
        if(end==sdp.npos || end>=1024) return false;
        const auto line=sdp.substr(0,end);
        if((line.starts_with("a=ice-ufrag:") && line.size()>12+256) ||
           (line.starts_with("a=ice-pwd:") && line.size()>10+256) ||
           (line.starts_with("a=fingerprint:") && (!line.starts_with("a=fingerprint:sha-256 ") || line.size()>=22+160))) return false;
        if(line.starts_with("a=candidate:") && (++candidates>10 || ClassifyCandidate(line)!=CandidateKind::Udp)) return false;
        sdp.remove_prefix(end+2);
    }
    return true;
}
// Fixed bit flags only, never return or log SDP text, mids or addresses.
// 1 count, 2 media order/type, 4 MID, 8 transport, 16 unoffered format, 32 invalid.
inline int SdpAnswerMismatchMask(std::string_view offer,std::string_view answer) {
    const auto remote=SdpMediaShapes(offer),local=SdpMediaShapes(answer);
    if(!remote || !local || remote->empty() || local->empty()) return 32;
    int flags=remote->size()==local->size() ? 0 : 1;
    for(std::size_t i=0;i<std::min(remote->size(),local->size());++i) {
        const auto& r=(*remote)[i];const auto& l=(*local)[i];
        if(r.kind!=l.kind) flags|=2;
        if(r.mid!=l.mid) flags|=4;
        if(r.transport!=l.transport) flags|=8;
        for(const auto& format:l.formats)
            if(std::find(r.formats.begin(),r.formats.end(),format)==r.formats.end()) flags|=16;
    }
    return flags;
}
} // namespace cloudgenshin::rtc
