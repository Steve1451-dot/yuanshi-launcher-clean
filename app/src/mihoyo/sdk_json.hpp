#pragma once
#include <borealis/extern/nlohmann/json.hpp>
#include <cstddef>
#include <string_view>

namespace cloudgenshin::sdk {
inline constexpr std::size_t kSdkJsonMaximumBytes=256*1024;
inline constexpr unsigned kSdkJsonMaximumDepth=64;
// Inspect without recursion/allocation before the DOM parser and destructor
// see an SDK payload. Braces inside quoted/escaped strings are just text.
inline bool SdkJsonWithinBudget(std::string_view text) {
    if(text.empty() || text.size()>kSdkJsonMaximumBytes) return false;
    unsigned depth=0;bool quoted=false,escaped=false;
    for(const char c:text) {
        if(quoted) {
            if(escaped) escaped=false;
            else if(c=='\\') escaped=true;
            else if(c=='"') quoted=false;
        } else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') {if(++depth>kSdkJsonMaximumDepth) return false;}
        else if(c=='}' || c==']') {if(depth==0) return false;--depth;}
    }
    return depth==0 && !quoted;
}
inline nlohmann::json ParseSdkJson(std::string_view text) {
    if(!SdkJsonWithinBudget(text)) return nlohmann::json(nlohmann::json::value_t::discarded);
    return nlohmann::json::parse(text,nullptr,false);
}
} // namespace cloudgenshin::sdk
