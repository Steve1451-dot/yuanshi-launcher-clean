// Host tests for the Endfield sdk_msg parser. Focus: the first-connect
// packedData normalization and the official playerInfo/playerRole semantics.
#include "endfield/vlink_proto.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAIL " << what << '\n';
    ++failures;
}

void TestPackedString() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","webRtcPort":1234,"packedData":"eyJhIjoxfQ==",)"
        R"("userId":"u","gsmToken":"t"})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    Check(sdk.has_value(), "string: parsed");
    if (!sdk) return;
    Check(sdk->packed_data == "eyJhIjoxfQ==", "string: preserved verbatim");
    Check(sdk->packed_data_kind == "string", "string: kind");
    Check(sdk->packed_data_length == 12, "string: length");
    Check(sdk->packed_data_key_mask == 0, "string: no object keys");
}

void TestPackedObject() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","gsPort":1234,"packedData":{)"
        R"("gsm_id":7,"token":"secret","video":{"width":1280}}})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    Check(sdk.has_value(), "object: parsed");
    if (!sdk) return;
    Check(sdk->packed_data_kind == "object", "object: kind");
    Check(!sdk->packed_data.empty(), "object: serialized instead of empty");
    Check(sdk->packed_data.find("\"gsm_id\"") != std::string::npos,
          "object: contains original member");
    Check(sdk->packed_data_length == sdk->packed_data.size(), "object: length");
    Check((sdk->packed_data_key_mask & (1u << 0)) != 0, "object: video key");
    Check((sdk->packed_data_key_mask & (1u << 1)) != 0, "object: gsm_id key");
    Check((sdk->packed_data_key_mask & (1u << 2)) != 0, "object: token key");
    Check((sdk->packed_data_key_mask & (1u << 3)) == 0, "object: gsm_token absent");
}

void TestPackedMissing() {
    const std::string json = R"({"gsIp":"127.0.0.1","webRtcPort":1234})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    Check(sdk.has_value(), "missing: parsed");
    if (!sdk) return;
    Check(sdk->packed_data.empty(), "missing: empty");
    Check(sdk->packed_data_kind == "missing", "missing: kind");
    Check(sdk->packed_data_length == 0, "missing: length");
}

void TestPlayerRoleZeroNumber() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","webRtcPort":1234,"playerRole":0,"playerId":"p"})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    if (!sdk) return Check(false, "role0: parsed");
    Check(!sdk->player_info_valid, "role0 number: playerInfo absent");
    Check(sdk->player_role_number == 0, "role0 number: value");
    Check(sdk->player_id_json == "\"p\"", "role0 number: playerId type kept");
}

void TestPlayerRoleZeroString() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","webRtcPort":1234,"playerRole":"0","playerId":"p"})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    if (!sdk) return Check(false, "role\"0\": parsed");
    Check(sdk->player_info_valid, "role\"0\" string: playerInfo present");
    Check(sdk->player_role_number == 0, "role\"0\" string: number 0");
}

void TestPlayerRoleOne() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","webRtcPort":1234,"playerRole":1})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    if (!sdk) return Check(false, "role1: parsed");
    Check(sdk->player_info_valid, "role1: playerInfo present");
    Check(sdk->player_role_number == 1, "role1: number 1");
}

void TestIdentityTypes() {
    const std::string json =
        R"({"gsIp":"127.0.0.1","webRtcPort":1234,"userId":123,"gsmToken":"abc"})";
    const auto sdk = cloudgenshin::endfield::ParseVlinkSdkMsg(json);
    if (!sdk) return Check(false, "types: parsed");
    Check(sdk->user_id == "123", "types: text form");
    Check(sdk->user_id_json == "123", "types: JSON number preserved");
    Check(sdk->gsm_token_json == "\"abc\"", "types: JSON string preserved");
}

}  // namespace

int main() {
    TestPackedString();
    TestPackedObject();
    TestPackedMissing();
    TestPlayerRoleZeroNumber();
    TestPlayerRoleZeroString();
    TestPlayerRoleOne();
    TestIdentityTypes();
    using cloudgenshin::endfield::ParseServerInputVersion;
    Check(ParseServerInputVersion(R"({"function":{"input_ver":2}})")==2,"server input v2");
    Check(ParseServerInputVersion(R"({"function":{"input_ver":"3"}})")==3,"server input v3 string");
    Check(ParseServerInputVersion(R"({"function":{"input_ver":1}})")==1,"legacy server input");
    Check(!ParseServerInputVersion(R"({"function":{"input_ver":true}})"),"reject boolean version");
    Check(!ParseServerInputVersion(R"({"function":{"input_ver":9}})"),"reject unknown version");
    Check(!ParseServerInputVersion(R"({"function":{"input_ver":"2oops"}})"),"reject malformed string version");
    Check(!ParseServerInputVersion("[]"),"reject non-object info");
    Check(!ParseServerInputVersion(std::string(65537,'x')),"bounded info size");
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "endfield_vlink_proto_test: all checks passed\n";
    return EXIT_SUCCESS;
}
