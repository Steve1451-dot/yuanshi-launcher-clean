#include "mihoyo/signaling_frame.hpp"

#include <cassert>
#include <vector>

int main() {
    using namespace cloudgenshin::signaling;
    const std::vector<std::uint8_t> payload{'a', 0, 'b'};
    const auto bytes = EncodeFrame(MessageType::Handshake, payload);
    assert(bytes == std::vector<std::uint8_t>({0, 0, 0, 3, 0, 0, 0, 3, 'a', 0, 'b'}));
    const auto decoded = DecodeFrame(bytes);
    assert(decoded && decoded->type == MessageType::Handshake && decoded->payload == payload);
    assert(!DecodeFrame({0, 0, 0, 3, 0, 0, 0, 3, 'a'}));
    assert(!DecodeFrame({0, 0, 0, 5, 0, 0, 0, 0}));
    assert(!DecodeFrame({0, 0, 0, 3, 0xff, 0xff, 0xff, 0xff}));
    assert(EncodeFrame(MessageType::Keepalive, {}).size() == 8);
}
