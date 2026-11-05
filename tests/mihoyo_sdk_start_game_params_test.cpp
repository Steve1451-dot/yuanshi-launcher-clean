#include "mihoyo/sdk_start_game_params.hpp"
#include "mihoyo_sdk_start_game_fixture.hpp"

#include <cassert>
#include <initializer_list>
#include <limits>

namespace {

using Bytes = std::vector<std::uint8_t>;

void Varint(Bytes& out, std::uint64_t number) {
    do {
        const auto byte = static_cast<std::uint8_t>(number & 0x7f);
        number >>= 7;
        out.push_back(byte | (number ? 0x80 : 0));
    } while (number);
}

void Text(Bytes& out, unsigned field, std::string_view value) {
    Varint(out, (static_cast<std::uint64_t>(field) << 3) | 2);
    Varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void Reject(std::initializer_list<std::uint8_t> bytes) {
    assert(!cloudgenshin::dispatch::ParseSdkStartGameParams(Bytes(bytes)));
}

} // namespace

int main() {
    using namespace cloudgenshin::dispatch;
    const auto params = DecodeSdkStartGameParams(kSyntheticSdkStartGameBase64);
    assert(params);
    assert(params->sid == "synthetic-session");
    assert(params->ca_id == "synthetic-ca");
    assert(params->pod_id == "synthetic-pod");
    assert(params->game_token == "synthetic-not-a-credential");
    assert(params->resolution == "1280x720" && params->target_fps == 60);
    assert(params->cmd_line == "test");
    assert(params->game_svr_addr == "192.0.2.1" && params->rtc_udp_port == 12345);
    assert((params->additional_game_svr_addrs == std::vector<std::string>{"192.0.2.2", "192.0.2.3"}));
    assert(params->game_server_wss_url == "wss://signal.example.invalid/rtc");
    assert((params->additional_game_server_wss_urls == std::vector<std::string>{"wss://backup.example.invalid/rtc"}));
    assert(params->big_isp == 7);
    assert(params->game_control_channel_url == "wss://control.example.invalid/game");
    assert((params->additional_game_control_channel_urls == std::vector<std::string>{"wss://backup.example.invalid/game"}));
    assert(params->is_ipv6);
    assert(params->game_svr_addr_ipv6 == "2001:db8::1" && params->rtc_udp_port_ipv6 == 23456);
    assert((params->additional_game_svr_addrs_ipv6 == std::vector<std::string>{"2001:db8::2"}));
    assert(params->game_server_wss_url_ipv6 == "wss://signal6.example.invalid/rtc");
    assert((params->additional_game_server_wss_urls_ipv6 == std::vector<std::string>{"wss://backup6.example.invalid/rtc"}));
    assert(params->game_control_channel_url_ipv6 == "wss://control6.example.invalid/game");
    assert((params->additional_game_control_channel_urls_ipv6 == std::vector<std::string>{"wss://backup6.example.invalid/game"}));
    assert(DecodeSdkStartGameParams(kSyntheticSdkStartGameBase64.substr(
        0, kSyntheticSdkStartGameBase64.size() - 2))->sid == params->sid);

    Bytes bytes;
    Text(bytes, 1, "old");
    Text(bytes, 1, "new");
    // Future unknown fields: varint, fixed64, bytes and fixed32 are skipped.
    Varint(bytes, 100 << 3); Varint(bytes, 123);
    Varint(bytes, (101 << 3) | 1); bytes.insert(bytes.end(), 8, 0xff);
    Text(bytes, 102, "future");
    Varint(bytes, (103 << 3) | 5); bytes.insert(bytes.end(), 4, 0xff);
    const auto future = ParseSdkStartGameParams(bytes);
    assert(future && future->sid == "new" && future->target_fps == 0);
    assert(future->game_token.empty() && future->additional_game_server_wss_urls.empty());

    bytes = {6 << 3}; Varint(bytes, std::numeric_limits<std::uint64_t>::max());
    assert(ParseSdkStartGameParams(bytes)->target_fps == -1);
    bytes = {9 << 3}; Varint(bytes, std::numeric_limits<std::uint32_t>::max());
    assert(ParseSdkStartGameParams(bytes)->rtc_udp_port == 0xffffffffU);
    bytes = {9 << 3}; Varint(bytes, 0x100000000ULL);
    assert(!ParseSdkStartGameParams(bytes));
    bytes = {6 << 3}; Varint(bytes, 0x100000000ULL);
    assert(!ParseSdkStartGameParams(bytes));

    assert(!ParseSdkStartGameParams({}));
    Reject({0}); // Field zero.
    Reject({0x80}); // Truncated key varint.
    Reject({0x08, 1}); // String with wrong wire type.
    Reject({0x32, 0}); // Number with wrong wire type.
    Reject({0x0a, 2, 'x'}); // Truncated length-delimited field.
    Reject({0x0a, 0x80});
    Reject({0x30, 0x80}); // Truncated number.
    Reject({0x30, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2});
    Reject({0x0a, 1, 0});
    Reject({0x0a, 1, '\n'});
    Reject({0x0a, 2, 0xc0, 0x80}); // Overlong UTF-8.
    Reject({0x0a, 3, 0xed, 0xa0, 0x80}); // Surrogate.
    Reject({0x0a, 4, 0xf4, 0x90, 0x80, 0x80}); // Out of Unicode range.
    Reject({0xc1, 0x0c, 1}); // Truncated unknown fixed64.
    Reject({0xc2, 0x0c, 2, 1}); // Truncated unknown bytes.
    Reject({0xc3, 0x0c}); // Unknown group.
    Reject({0xc5, 0x0c, 1}); // Truncated unknown fixed32.
    Reject({0xc7, 0x0c}); // Invalid wire type.
    bytes.clear(); Text(bytes, 7, "\xe4\xba\x91\xe5\x8e\x9f\xe7\xa5\x9e");
    assert(ParseSdkStartGameParams(bytes));
    bytes.clear(); Text(bytes, 4, std::string(8192, 'x'));
    assert(ParseSdkStartGameParams(bytes));
    bytes.clear(); Text(bytes, 4, std::string(8193, 'x'));
    assert(!ParseSdkStartGameParams(bytes));
    bytes.clear();
    for (int i = 0; i < 16; ++i) Text(bytes, 12, "wss://example.invalid");
    assert(ParseSdkStartGameParams(bytes)->additional_game_server_wss_urls.size() == 16);
    Text(bytes, 12, "wss://example.invalid");
    assert(!ParseSdkStartGameParams(bytes));
    bytes.clear();
    for (int i = 0; i < 1024; ++i) Text(bytes, 1, "x");
    assert(ParseSdkStartGameParams(bytes));
    Text(bytes, 1, "x"); assert(!ParseSdkStartGameParams(bytes));
    assert(!ParseSdkStartGameParams(Bytes(65537, 0)));

    assert(DecodeSdkStartGameParams("CgF4")->sid == "x");
    assert(DecodeSdkStartGameParams("CgJ4eA==")->sid == "xx");
    assert(DecodeSdkStartGameParams("CgJ4eA")->sid == "xx");
    for (const auto bad : {"", "A", "====", "CgJ4eB==", "CgJ4eA=", "CgF4==",
                           "CgF4\n", "Cg-4", "Cg=4", "AA==", "ChFzeW50"})
        assert(!DecodeSdkStartGameParams(bad));
    assert(!DecodeSdkStartGameParams(std::string(87385, 'A')));

    // Deterministic malformed-wire smoke test. No network/captured data.
    std::uint32_t random = 0x5a17b00d;
    for (unsigned trial = 0; trial < 4096; ++trial) {
        random = random * 1664525U + 1013904223U;
        bytes.resize(random % 256);
        for (auto& byte : bytes) {
            random = random * 1664525U + 1013904223U;
            byte = static_cast<std::uint8_t>(random >> 24);
        }
        const auto parsed = ParseSdkStartGameParams(bytes);
        if (parsed) {
            assert(parsed->sid.size() <= 8192);
            assert(parsed->additional_game_server_wss_urls.size() <= 16);
        }
    }
}
