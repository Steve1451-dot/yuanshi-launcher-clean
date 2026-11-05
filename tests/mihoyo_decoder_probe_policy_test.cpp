#include "mihoyo/decoder_probe_policy.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin;
    assert(!HardwareProbeMarker(""));
    assert(!HardwareProbeMarker("1"));
    assert(!HardwareProbeMarker("CGNX_AUTOTEST_V1\n"));
    assert(!HardwareProbeMarker("CGNX_NVDEC_PROBE_V1\nextra"));
    assert(!HardwareProbeMarker(std::string(10000,'x')));
    assert(HardwareProbeMarker("CGNX_NVDEC_PROBE_V1\n"));
    assert(HardwareProbeMarker("CGNX_NVDEC_PROBE_V1\r\n"));
    assert(!HardwareProbeEnabled(""));
}
