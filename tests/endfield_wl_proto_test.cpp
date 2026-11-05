// Host tests for the WeLink wire transform used by PackedData.
//
// The official client calls btoa(encodeURI(packedData)) and passes the result
// to a jspb *bytes* setter. jspb stores bytes as base64 and its writer decodes
// that string, so the wire bytes are UriEncode(packedData), not the btoa text.
#include "endfield/wl_proto.hpp"

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

}  // namespace

int main() {
    // Vectors produced by Node:
    //   encodeURI("a b+/中") = "a%20b+/%E4%B8%AD"
    //   btoa(encodeURI(...)) = "YSUyMGIrLyVFNCVCOCVBRA=="
    const std::string input = "a b+/\xE4\xB8\xAD";
    Check(cloudgenshin::endfield::UriEncode(input) == "a%20b+/%E4%B8%AD",
          "UriEncode matches encodeURI");
    Check(cloudgenshin::endfield::BtoaEncodeUri(input) ==
              "YSUyMGIrLyVFNCVCOCVBRA==",
          "BtoaEncodeUri matches btoa(encodeURI(...))");

    Check(cloudgenshin::endfield::UriEncode("eyJhIjoxfQ==") == "eyJhIjoxfQ==",
          "URI-safe base64-like payload is unchanged");
    Check(cloudgenshin::endfield::BtoaEncodeUri("eyJhIjoxfQ==") ==
              "ZXlKaElqb3hmUT09",
          "base64 of an ASCII payload");

    cloudgenshin::endfield::SimpleCmd packed;
    packed.cmd = "PackedData";
    const auto escaped = cloudgenshin::endfield::UriEncode(input);
    packed.data = std::vector<std::uint8_t>(escaped.begin(), escaped.end());
    const auto parsed = cloudgenshin::endfield::SimpleCmd::Parse(packed.Serialize());
    Check(parsed && parsed->data &&
              std::string(parsed->data->begin(), parsed->data->end()) ==
                  "a%20b+/%E4%B8%AD",
          "PackedData wire bytes contain encodeURI without a base64 layer");

    const auto resolution = cloudgenshin::endfield::SimpleCmd::Parse(
        cloudgenshin::endfield::GameResolutionFrame(1280,720));
    Check(resolution && resolution->cmd=="SetGameResolution" &&
              resolution->arg=="1280" && resolution->arg1=="720" && !resolution->data,
          "resolution uses width in arg and height in arg1");
    Check(cloudgenshin::endfield::GameResolutionFrame(0,720).empty() &&
              cloudgenshin::endfield::GameResolutionFrame(1280,-1).empty() &&
              cloudgenshin::endfield::GameResolutionFrame(8193,720).empty(),
          "invalid resolution is not sent");

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "endfield_wl_proto_test: all checks passed\n";
    return EXIT_SUCCESS;
}
