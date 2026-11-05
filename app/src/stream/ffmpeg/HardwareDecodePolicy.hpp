#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

namespace opennow::video {
// NVTEGRA is a hwaccel attached to the ordinary h264 decoder, not a decoder
// named "h264_nvtegra". Use the library's advertised configuration.
template<class ConfigAt> AVPixelFormat DevicePixelFormat(AVHWDeviceType device, ConfigAt&& config_at) {
    for(int i=0;;++i) {
        const AVCodecHWConfig* config=config_at(i);
        if(!config) return AV_PIX_FMT_NONE;
        if(config->device_type==device &&
           (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)!=0)
            return config->pix_fmt;
    }
}
inline AVPixelFormat OfferedHardwareFormat(const AVPixelFormat* offered,AVPixelFormat wanted) {
    if(!offered || wanted==AV_PIX_FMT_NONE) return AV_PIX_FMT_NONE;
    for(;*offered!=AV_PIX_FMT_NONE;++offered) if(*offered==wanted) return wanted;
    // Never label a silent software-format fallback as successful NVDEC.
    return AV_PIX_FMT_NONE;
}
inline bool LinearLumaReadable(int format) {
    return format==AV_PIX_FMT_YUV420P || format==AV_PIX_FMT_YUVJ420P || format==AV_PIX_FMT_NV12;
}
// Diagnostic download only: a non-256-byte pitch selects the backend's CPU
// transfer instead of its VIC copy. Keep this independent of emulator VIC
// support and never use it in the normal zero-copy presentation path.
inline int DiagnosticTransferWidth(int width) {
    return width>0 && width<=8192 ? ((width+255)/256)*256+32 : 0;
}
inline bool InitialHardwareFallbackAllowed(bool hardware,bool published_frames,unsigned attempts,int result) {
    return hardware && !published_frames && attempts==0 && result<0;
}
}
