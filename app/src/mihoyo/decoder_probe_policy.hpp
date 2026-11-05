#pragma once
#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin {
// A device opening and returning hardware-format frames is not pixel proof.
// Keep the known working software path until the user explicitly opts into
// the experimental NVDEC path on their device. This never enables autotest.
inline bool HardwareProbeMarker(std::string_view value) {
    return value=="CGNX_NVDEC_PROBE_V1\n" || value=="CGNX_NVDEC_PROBE_V1\r\n";
}
inline bool HardwareProbeEnabled(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    char bytes[64]{};input.read(bytes,sizeof(bytes));
    return input.eof() && HardwareProbeMarker(
        std::string_view(bytes,static_cast<std::size_t>(input.gcount())));
}
}
