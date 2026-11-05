#pragma once
#include "IFFmpegVideoDecoder.hpp"
#include "AVFrameHolder.hpp"

class FFmpegVideoDecoder : public IFFmpegVideoDecoder {
  public:
    FFmpegVideoDecoder();
    ~FFmpegVideoDecoder();

    int setup(int video_format, int width, int height, int redraw_rate,
              void* context, int dr_flags) override;
    void cleanup() override;
    int submit_decode_unit(uint8_t* indata, int inlen, int64_t pts = AV_NOPTS_VALUE) override;
    void reset_stream();
    int capabilities() const override;
    VideoDecodeStats* video_decode_stats() override;
    bool uses_hardware_frames() const override { return m_uses_hardware_frames; }
    // Frames the decoder produced but which carried a corruption flag. These
    // are dropped silently by design, so a picture that freezes while the pod
    // still sends data can be caused by every frame being flagged corrupt.
    int corrupt_frames_dropped() const { return m_corrupt_frames_dropped; }
    int setup_stage() const {return m_setup_stage;}
    // Decoder-worker only. A first-frame fallback must not free UI/GPU frames.
    bool has_output_frames() const {return m_frames_out!=0;}

  private:
    int decode(char* indata, int inlen, int64_t pts);
    AVFrame* get_frame(bool native_frame, int& decode_error);
    static AVPixelFormat choose_hardware_format(AVCodecContext*,const AVPixelFormat*);

    AVPacket* m_packet = nullptr;
    AVBufferRef *hw_device_ctx = nullptr;
    const AVCodec* m_decoder = nullptr;
    AVCodecContext* m_decoder_context = nullptr;
    AVFrame *tmp_frame = nullptr;
    AVFrame** m_frames = nullptr;
    int m_frames_size = 0;

    int m_stream_fps = 0;
    int m_frames_in = 0;
    int m_frames_out = 0;
    int m_corrupt_frames_dropped = 0;
    int m_current_frame = 0, m_next_frame = 0;
    uint32_t m_last_frame = 0;

    VideoDecodeStats m_video_decode_stats_progress = {};
    VideoDecodeStats m_video_decode_stats_cache = {};
    uint64_t timeCount = 0;

    AVFrame* m_frame = nullptr;
    bool m_uses_hardware_frames = false;
    AVPixelFormat m_hardware_pixel_format = AV_PIX_FMT_NONE;
    int m_setup_stage = 0; // 1 packet, 2 codec, 3 config, 4 device, 5 open, 6 frames, 7 ready.
};
