#pragma once
#include <array>
#include <charconv>
#include <string_view>
namespace cloudgenshin::rtc {
// Numeric categories only; never export addresses or candidate contents.
enum class CandidateKind { Udp=0, Tcp=1, Component=2, Type=3, Invalid=4 };
inline CandidateKind ClassifyCandidate(std::string_view value) {
    if(value.ends_with("\r\n")) value.remove_suffix(2);
    for(unsigned char ch:value) if(ch<32 || ch>=127) return CandidateKind::Invalid;
    if(value.starts_with("a=")) value.remove_prefix(2);
    if(!value.starts_with("candidate:")) return CandidateKind::Invalid;
    value.remove_prefix(10);
    std::array<std::string_view,8> fields{};
    for(auto& field:fields) {
        const auto begin=value.find_first_not_of(' ');
        if(begin==value.npos) return CandidateKind::Invalid;
        value.remove_prefix(begin);
        const auto end=value.find(' ');
        field=value.substr(0,end);
        value=end==value.npos ? std::string_view{} : value.substr(end+1);
    }
    for(auto field:fields) for(unsigned char ch:field)
        if(ch<=32 || ch>=127) return CandidateKind::Invalid;
    // Bounds of the legacy libpeer sscanf destination arrays.
    if(fields[0].size()>32 || fields[2].size()>32 || fields[4].size()>45 ||
       fields[7].size()>15 || fields[6]!="typ") return CandidateKind::Invalid;
    const auto number=[](std::string_view text,unsigned long long max) {
        unsigned long long n=0;
        const auto result=std::from_chars(text.data(),text.data()+text.size(),n);
        return result.ec==std::errc{} && result.ptr==text.data()+text.size() && n<=max;
    };
    if(!number(fields[1],256) || !number(fields[3],0xffffffffULL) ||
       !number(fields[5],65535) || fields[5]=="0") return CandidateKind::Invalid;
    if(fields[2]=="tcp" || fields[2]=="TCP") return CandidateKind::Tcp;
    if(fields[2]!="udp" && fields[2]!="UDP") return CandidateKind::Invalid;
    if(fields[1]!="1") return CandidateKind::Component;
    if(fields[7]!="host" && fields[7]!="srflx" && fields[7]!="relay") return CandidateKind::Type;
    return CandidateKind::Udp;
}
}
