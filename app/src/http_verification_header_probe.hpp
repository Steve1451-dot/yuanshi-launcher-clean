#pragma once
#include <string_view>

namespace opennow {
// Presence only: never retain a header value, cookie or challenge ticket.
struct VerificationHeaderProbe {
    bool aigis_present=false;
    void consume(std::string_view line) noexcept {
        if (line.starts_with("HTTP/")) {aigis_present=false;return;}
        constexpr std::string_view name="x-rpc-aigis:";
        if (line.size()<name.size() || line.size()>16384) return;
        for (std::size_t i=0;i<name.size();++i) {
            const char c=line[i]>='A' && line[i]<='Z' ? static_cast<char>(line[i]-'A'+'a') : line[i];
            if (c!=name[i]) return;
        }
        line.remove_prefix(name.size());
        for(const char c:line) if(c!=' ' && c!='\t' && c!='\r' && c!='\n') {aigis_present=true;return;}
    }
};
} // namespace opennow
