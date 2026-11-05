#include "mihoyo/sdk_json.hpp"
#include <cassert>
#include <string>

int main() {
    using namespace cloudgenshin::sdk;
    const auto nested=[](unsigned depth){return std::string(depth,'[')+"0"+std::string(depth,']');};
    assert(!ParseSdkJson(nested(kSdkJsonMaximumDepth)).is_discarded());
    assert(ParseSdkJson(nested(kSdkJsonMaximumDepth+1)).is_discarded());
    assert(ParseSdkJson(nested(4096)).is_discarded());
    assert(!ParseSdkJson("0").is_discarded() && !ParseSdkJson("null").is_discarded());
    assert(ParseSdkJson("").is_discarded() && ParseSdkJson("}").is_discarded());
    assert(ParseSdkJson("[1}").is_discarded() && ParseSdkJson("\"unterminated").is_discarded());
    const auto quoted=nlohmann::json(std::string(4096,'[')+"\\\"text}").dump();
    assert(!ParseSdkJson(quoted).is_discarded()); // Brackets/escaped quotes inside a string.
    const std::string boundary="0"+std::string(kSdkJsonMaximumBytes-1,' ');
    assert(!ParseSdkJson(boundary).is_discarded());
    assert(ParseSdkJson(boundary+" ").is_discarded());
    assert(!ParseSdkJson(R"({"f":"invoke","p":"{\"f\":\"Init\"}","i":1})").is_discarded());
}
