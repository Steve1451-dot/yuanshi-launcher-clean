#include "mihoyo/rtc_signal.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> Bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

} // namespace

int main() {
    using cloudgenshin::rtc::EncodeAnswer;
    using cloudgenshin::rtc::EncodeCandidate;
    using cloudgenshin::rtc::ParseSignalMessage;
    using cloudgenshin::rtc::SignalType;

    const auto offer = ParseSignalMessage(Bytes(R"({"type":"offer","sdp":"v=0\r\n"})"));
    assert(offer && offer->type == SignalType::Offer && offer->sdp == "v=0\r\n");
    const auto candidate = ParseSignalMessage(Bytes(
        R"({"type":"candidates","candidate":"candidate:1 1 UDP 1 192.0.2.1 9 typ host","sdp-mid":"0","sdp-mline-index":0})"));
    assert(candidate && candidate->type == SignalType::Candidate);
    assert(candidate->sdp_mid == "0" && candidate->sdp_mline_index == 0);
    const auto answer = EncodeAnswer("v=0\r\n");
    assert(answer);
    const auto decoded_answer = ParseSignalMessage(*answer);
    assert(decoded_answer && decoded_answer->type == SignalType::Answer);
    assert(decoded_answer->sdp == "v=0\r\n");
    const auto local_candidate = EncodeCandidate("candidate:1 1 UDP 1 192.0.2.2 9 typ host", "0", 0);
    assert(local_candidate);
    const auto decoded_candidate = ParseSignalMessage(*local_candidate);
    assert(decoded_candidate && decoded_candidate->type == SignalType::Candidate);
    assert(decoded_candidate->sdp_mline_index == 0);

    assert(!ParseSignalMessage({}));
    assert(!ParseSignalMessage(Bytes("not json")));
    assert(!ParseSignalMessage(Bytes("[]")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"offer"})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"offer","sdp":""})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"candidates","candidate":"x","sdp-mline-index":-1})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"candidates","candidate":"x","sdp-mline-index":256})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"candidates","candidate":"x","sdp-mline-index":18446744073709551615})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"candidates","candidate":"x","sdp-mline-index":"0"})")));
    assert(!ParseSignalMessage(Bytes(R"({"type":"unknown","sdp":"v=0"})")));
    assert(!EncodeAnswer(""));
    assert(!EncodeAnswer(std::string(256 * 1024, 'a')));
    assert(!EncodeCandidate("x", "0", -1));
    assert(!EncodeCandidate("x", "0", 256));
}
