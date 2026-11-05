#include "stream/ffmpeg/HardwareDecodePolicy.hpp"
#include <array>
#include <cassert>
int main() {
    using namespace opennow::video;
    const std::array<AVCodecHWConfig,3> configs{{
        {AV_PIX_FMT_CUDA,AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,AV_HWDEVICE_TYPE_CUDA},
        {AV_PIX_FMT_NVTEGRA,AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX,AV_HWDEVICE_TYPE_NVTEGRA},
        {AV_PIX_FMT_NVTEGRA,AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,AV_HWDEVICE_TYPE_NVTEGRA}}};
    const auto at=[&](int i)->const AVCodecHWConfig* {return i<3 ? &configs[i] : nullptr;};
    assert(DevicePixelFormat(AV_HWDEVICE_TYPE_NVTEGRA,at)==AV_PIX_FMT_NVTEGRA);
    assert(DevicePixelFormat(AV_HWDEVICE_TYPE_VAAPI,at)==AV_PIX_FMT_NONE);
    const AVPixelFormat offered[]{AV_PIX_FMT_YUV420P,AV_PIX_FMT_NVTEGRA,AV_PIX_FMT_NONE};
    assert(OfferedHardwareFormat(offered,AV_PIX_FMT_NVTEGRA)==AV_PIX_FMT_NVTEGRA);
    assert(OfferedHardwareFormat(offered,AV_PIX_FMT_CUDA)==AV_PIX_FMT_NONE);
    assert(OfferedHardwareFormat(nullptr,AV_PIX_FMT_NVTEGRA)==AV_PIX_FMT_NONE);
    const AVPixelFormat software[]{AV_PIX_FMT_YUV420P,AV_PIX_FMT_NONE};
    assert(OfferedHardwareFormat(software,AV_PIX_FMT_NVTEGRA)==AV_PIX_FMT_NONE);
    assert(!LinearLumaReadable(AV_PIX_FMT_NVTEGRA));
    assert(LinearLumaReadable(AV_PIX_FMT_YUV420P) && LinearLumaReadable(AV_PIX_FMT_NV12));
    assert(DiagnosticTransferWidth(1280)==1312 && DiagnosticTransferWidth(1920)==2080);
    assert(DiagnosticTransferWidth(8192)==8224);
    assert(DiagnosticTransferWidth(0)==0 && DiagnosticTransferWidth(-1)==0 && DiagnosticTransferWidth(8193)==0);
    for(int width=1;width<=8192;++width) {
        assert(DiagnosticTransferWidth(width)>=width);
        assert(DiagnosticTransferWidth(width)%256==32);
    }
    assert(InitialHardwareFallbackAllowed(true,false,0,-1));
    assert(!InitialHardwareFallbackAllowed(true,true,0,-1));
    assert(!InitialHardwareFallbackAllowed(false,false,0,-1));
    assert(!InitialHardwareFallbackAllowed(true,false,1,-1));
    assert(!InitialHardwareFallbackAllowed(true,false,0,0));
}
