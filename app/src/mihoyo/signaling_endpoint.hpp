#pragma once
#include "websocket_connect_policy.hpp"

namespace cloudgenshin {
// Public cg-sdk 6.1.0.62 WebSessionInfo.getServerWssUrl adds /rtc-sdk
// to the dispatch base address before opening the game signaling socket.
// Keep this game-specific path out of the shared WebSocket/ping transport.
inline std::string GameSignalingEndpoint(const std::string& base) {
    if (!base.starts_with("wss://") || !opennow::websocket::ParseEndpoint(base)) return {};
    const auto query=base.find('?');
    std::string address=base.substr(0,query);
    while (address.ends_with('/')) address.pop_back();
    if (!address.ends_with("/rtc-sdk")) address+="/rtc-sdk";
    if (query!=std::string::npos) address+=base.substr(query);
    return address;
}
inline std::vector<std::string> GameSignalingEndpoints(const std::string& primary,
                                                       const std::vector<std::string>& alternatives) {
    std::vector<std::string> normalized;
    for (const auto& alternative:alternatives) normalized.push_back(GameSignalingEndpoint(alternative));
    return opennow::websocket::SecureEndpoints(GameSignalingEndpoint(primary),normalized);
}
}
